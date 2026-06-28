// bench.c — CPU profiling harness (Stage 0.5).
//
// Proxy kernels time individual DSP primitives; the fused fake-voice render fn
// drives the audio API's load ramp. printf is intentional here (bench is a
// standalone diagnostic mode, not the audio path — CLAUDE.md allows it).
//
// All kernel state is module-static so the compiler cannot dead-store-eliminate
// the loops. The buffer passed to each kernel is reused across kernels — its
// content after each kernel feeds the next, preventing constant-folding.
#ifdef SYNTH_BENCH

#include "bench.h"
#include "platform.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

// How many block-iterations per kernel measurement. 10 000 × 64 samples at
// 400 MHz ≈ 130 ms of wall time per kernel — enough to swamp timer overhead.
#define BENCH_REPEATS 10000
#define BENCH_BLOCK   64

// Maximum voices in the load ramp (far beyond any target; we stop when >95%).
#define MAX_BENCH_VOICES 32

// -------------------------------------------------------------------------
// Kernel state (kept in module statics to survive across blocks and prevent
// dead-store elimination; tiny DC offsets guard against denormals — ADR 0012)
// -------------------------------------------------------------------------

// SVF 2-pole state
static float s_svf_low  = 1e-20f;
static float s_svf_band = 1e-20f;

// Moog ladder 4-pole state
static float s_ldr[4] = {1e-20f, 1e-20f, 1e-20f, 1e-20f};

// PolyBLEP saw phase
static float s_saw_phase = 0.0f;

// Biquad TDF-II state
static float s_bq_z1 = 0.0f;
static float s_bq_z2 = 0.0f;

// Sink: written by every kernel so the results are observable.
static volatile float s_sink = 0.0f;

// Separate destination for the memcpy kernel.
static float s_memcpy_dst[BENCH_BLOCK];

// -------------------------------------------------------------------------
// Proxy kernels (each processes one 64-sample block in-place)
// -------------------------------------------------------------------------

// Empty-loop baseline — measures loop overhead + memory write cost.
static void kernel_baseline(float* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = 1e-20f;
}

// transcendentals
static void kernel_sinf(float* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = sinf(buf[i]);
}
static void kernel_expf(float* buf, size_t n) {
    // scale down to avoid overflow while keeping non-trivial argument
    for (size_t i = 0; i < n; i++) buf[i] = expf(buf[i] * 0.01f);
}

// 2-pole state-variable filter (SVF) — lowpass output
static void kernel_svf(float* buf, size_t n) {
    // normalized cutoff ≈ Fc/Fs = 0.15; Q = 0.7
    static const float F = 0.15f;
    static const float Q = 0.70f;
    float low = s_svf_low, band = s_svf_band;
    for (size_t i = 0; i < n; i++) {
        float high = buf[i] - low - Q * band;
        band += F * high;
        low  += F * band;
        buf[i] = low;
    }
    s_svf_low = low; s_svf_band = band;
    s_sink = low;
}

// 4-pole Moog ladder (linearised — no tanhf so ladder cost is isolated)
static void kernel_ladder(float* buf, size_t n) {
    static const float F = 0.30f;
    static const float K = 3.5f;  // resonance (below self-oscillation)
    for (size_t i = 0; i < n; i++) {
        float in   = buf[i] - K * s_ldr[3];
        s_ldr[0]  += F * (in       - s_ldr[0]);
        s_ldr[1]  += F * (s_ldr[0] - s_ldr[1]);
        s_ldr[2]  += F * (s_ldr[1] - s_ldr[2]);
        s_ldr[3]  += F * (s_ldr[2] - s_ldr[3]);
        buf[i]     = s_ldr[3];
    }
    s_sink = s_ldr[3];
}

// PolyBLEP band-limited saw (simplified single-sample blep at the wrap point)
static void kernel_polyblep_saw(float* buf, size_t n) {
    static const float INC = 440.0f / 48000.0f;  // A4 at 48 kHz
    float ph = s_saw_phase;
    for (size_t i = 0; i < n; i++) {
        float saw = 2.0f * ph - 1.0f;
        // PolyBLEP correction near the discontinuity
        if (ph < INC) {
            float t = ph / INC;
            saw += (t + t - t * t - 1.0f);
        } else if (ph > 1.0f - INC) {
            float t = (ph - 1.0f) / INC;
            saw += (t * t + t + t + 1.0f);
        }
        ph += INC;
        if (ph >= 1.0f) ph -= 1.0f;
        buf[i] = saw;
    }
    s_saw_phase = ph;
    s_sink = ph;
}

// Direct-form II transposed biquad
static void kernel_biquad(float* buf, size_t n) {
    // Butterworth LP ~1 kHz / 48 kHz (approximate coefficients)
    static const float B0 =  0.00391f;
    static const float B1 =  0.00782f;
    static const float B2 =  0.00391f;
    static const float A1 = -1.81530f;
    static const float A2 =  0.82694f;
    float z1 = s_bq_z1, z2 = s_bq_z2;
    for (size_t i = 0; i < n; i++) {
        float out = B0 * buf[i] + z1;
        z1 = B1 * buf[i] - A1 * out + z2;
        z2 = B2 * buf[i] - A2 * out;
        buf[i] = out;
    }
    s_bq_z1 = z1; s_bq_z2 = z2;
    s_sink = z1;
}

// Block memcpy — buffer-shuffling overhead floor
static void kernel_memcpy(float* buf, size_t n) {
    memcpy(s_memcpy_dst, buf, n * sizeof(float));
    s_sink = s_memcpy_dst[0];
}

// -------------------------------------------------------------------------
// Kernel measurement helper
// -------------------------------------------------------------------------
typedef void (*kernel_fn)(float*, size_t);

static void measure(const char* name, kernel_fn fn, float* buf, size_t n,
                    uint32_t block_period, uint32_t cps) {
    fn(buf, n);  // warmup (fills pipeline, evicts cold caches)
    uint64_t t0 = platform_cycles_now();
    for (int r = 0; r < BENCH_REPEATS; r++) fn(buf, n);
    uint64_t t1 = platform_cycles_now();
    uint32_t cyc_blk = (uint32_t)((t1 - t0) / BENCH_REPEATS);
    float    us_blk  = cyc_blk * 1e6f / (float)cps;
    float    pct     = 100.0f * cyc_blk / (float)block_period;
    printf("  %-22s  %7" PRIu32 " cyc/blk  %5" PRIu32 " cyc/smp  %6.2f us  %5.1f%%\n",
           name, cyc_blk, (uint32_t)(cyc_blk / n), us_blk, pct);
}

// -------------------------------------------------------------------------
// Fused fake-voice state + render callback (0.5c)
// -------------------------------------------------------------------------
typedef struct {
    float phase;
    float svf_low;
    float svf_band;
    float env;
} fake_voice_t;

static fake_voice_t      s_voices[MAX_BENCH_VOICES];
static _Atomic uint32_t  s_n_voices      = 1;
static _Atomic uint32_t  s_render_cycles = 0;

// Called by the platform audio backend on its thread. Measures its own cost
// and stores it atomically for the ramp loop to read from the main thread.
static void bench_render_fn(float* left, float* right, size_t n, void* user) {
    (void)user;
    uint32_t nv = atomic_load_explicit(&s_n_voices, memory_order_relaxed);
    uint64_t t0 = platform_cycles_now();

    for (size_t i = 0; i < n; i++) left[i] = right[i] = 0.0f;

    for (uint32_t v = 0; v < nv; v++) {
        fake_voice_t* vp   = &s_voices[v];
        float         ph   = vp->phase;
        float         low  = vp->svf_low;
        float         band = vp->svf_band;
        float         env  = vp->env;
        // Spread pitches slightly so voices don't fold identically
        float inc = (220.0f * (1.0f + 0.02f * (float)v)) / 48000.0f;

        for (size_t i = 0; i < n; i++) {
            // PolyBLEP saw
            float saw = 2.0f * ph - 1.0f;
            ph += inc;
            if (ph >= 1.0f) ph -= 1.0f;

            // SVF lowpass
            float high = saw - low - 0.7f * band;
            band += 0.15f * high;
            low  += 0.15f * band;

            // Envelope multiply + stereo accumulate
            float s    = low * env;
            left[i]   += s;
            right[i]  += s;
        }
        // Slow decay so the voice stays alive through the ramp
        env *= 0.99999f;
        if (env < 1e-20f) env = 1e-20f;  // denormal guard (ADR 0012)

        vp->phase    = ph;
        vp->svf_low  = low;
        vp->svf_band = band;
        vp->env      = env;
    }

    uint64_t t1 = platform_cycles_now();
    atomic_store_explicit(&s_render_cycles, (uint32_t)(t1 - t0),
                          memory_order_relaxed);
}

// -------------------------------------------------------------------------
// bench_run — public entry point
// -------------------------------------------------------------------------
void bench_run(uint32_t sample_rate, uint32_t block_size) {
    uint32_t cps          = platform_cycles_per_sec();
    uint32_t block_period = (uint32_t)((uint64_t)cps * block_size / sample_rate);
    float    block_us     = block_size * 1e6f / (float)sample_rate;

    printf("\n");
    printf("============================================================\n");
    printf("  Tanmatsu Synth — Stage 0.5 CPU Benchmark\n");
    printf("============================================================\n");
    printf("  Block : %" PRIu32 " samples @ %" PRIu32 " Hz (%.2f us/block)\n",
           block_size, sample_rate, block_us);
    printf("  CPU   : ~%" PRIu32 " MHz   block_period = %" PRIu32 " cycles\n",
           cps / 1000000u, block_period);
    printf("  Note  : host numbers are pseudo-1GHz ns reference;\n");
    printf("          device UART numbers are the actual budget.\n");
    printf("------------------------------------------------------------\n");
    printf("  kernel                   cyc/blk  cyc/smp    us/blk  %%period\n");
    printf("------------------------------------------------------------\n");

    // Shared working buffer; initialise with small non-zero values.
    static float buf[BENCH_BLOCK];
    for (size_t i = 0; i < BENCH_BLOCK; i++) buf[i] = 0.01f * ((float)i - 32.0f);

    measure("baseline (empty loop)",  kernel_baseline,     buf, block_size, block_period, cps);
    measure("sinf (per sample)",       kernel_sinf,         buf, block_size, block_period, cps);
    measure("expf (per sample)",       kernel_expf,         buf, block_size, block_period, cps);
    measure("SVF 2-pole",              kernel_svf,          buf, block_size, block_period, cps);
    measure("biquad TDF-II",           kernel_biquad,       buf, block_size, block_period, cps);
    measure("Moog ladder 4-pole",      kernel_ladder,       buf, block_size, block_period, cps);
    measure("PolyBLEP saw",            kernel_polyblep_saw, buf, block_size, block_period, cps);
    measure("memcpy 64*4B",            kernel_memcpy,       buf, block_size, block_period, cps);

    printf("============================================================\n");
    printf("\n");

    // ----------------------------------------------------------------
    // Load ramp — fake voices through the real audio callback path
    // ----------------------------------------------------------------
    for (int v = 0; v < MAX_BENCH_VOICES; v++) {
        s_voices[v].phase    = (float)v / (float)MAX_BENCH_VOICES;
        s_voices[v].svf_low  = 1e-20f;
        s_voices[v].svf_band = 1e-20f;
        s_voices[v].env      = 1.0f;
    }
    atomic_store(&s_n_voices, 1u);
    atomic_store(&s_render_cycles, 0u);

    printf("  Load ramp: fake voice (osc + SVF + env) via audio callback\n");
    printf("  Target ceiling: 70%% period (headroom for FX + UI jitter)\n");
    printf("  voices   cyc/blk   %%period  margin_cyc  verdict\n");
    printf("------------------------------------------------------------\n");

    platform_audio_config_t cfg = {.sample_rate = sample_rate,
                                   .block_size   = block_size};
    if (!platform_audio_start(&cfg, bench_render_fn, NULL)) {
        printf("  ERROR: platform_audio_start failed — ramp aborted.\n");
        return;
    }

    uint32_t ceiling_voices = 0;
    for (uint32_t nv = 1; nv <= (uint32_t)MAX_BENCH_VOICES; nv++) {
        atomic_store_explicit(&s_n_voices, nv, memory_order_relaxed);
        // Wait 2 s for the audio thread to produce representative numbers.
        platform_sleep_ms(2000);

        uint32_t rc  = atomic_load_explicit(&s_render_cycles, memory_order_relaxed);
        float    pct = 100.0f * (float)rc / (float)block_period;
        int32_t  mrg = (int32_t)block_period - (int32_t)rc;
        const char* verdict = (pct <= 70.0f) ? "OK" : (pct <= 85.0f) ? "WARN" : "OVER";

        printf("  %6" PRIu32 "  %8" PRIu32 "  %7.1f%%  %10" PRId32 "  %s\n", nv, rc, pct,
               mrg, verdict);

        if (pct > 95.0f) {
            ceiling_voices = nv;
            printf("  --> ceiling reached at %" PRIu32 " voices (>95%% period)\n", nv);
            break;
        }
    }

    platform_audio_stop();

    printf("============================================================\n");
    if (ceiling_voices) {
        printf("  Safe voice ceiling (<=70%%): ~%" PRIu32 " voices\n",
               (ceiling_voices > 1) ? ceiling_voices - 1 : 1);
    }
    printf("  Record these numbers in specs/stages/stage-0.5-results.md\n");
    printf("  then raise the Opus gate for budget ratification.\n");
    printf("============================================================\n");

    // Hang so the serial monitor can capture the output before watchdog fires.
    // On host, exit normally after printing.
#ifndef ESP_PLATFORM
    return;
#else
    for (;;) platform_sleep_ms(1000);
#endif
}

#endif  // SYNTH_BENCH
