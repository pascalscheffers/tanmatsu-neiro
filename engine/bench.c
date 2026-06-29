// bench.c — CPU profiling harness (Stage 0.5 + Stage 3d-ii).
//
// Section 1 — Proxy kernels: time individual DSP primitives.
// Section 2 — Proxy voice load ramp: fused fake-voice render via audio callback
//             (Stage 0.5 baseline; kept for comparison).
// Section 3 — Real-voice load ramp: drives synth_render() directly, N=1..8
//             genuine Juno voices with Clean 106 routings (ENV2+LFO active),
//             plus a worst-case unison+chorus line. Answers the 🛑 Stage 3d-ii
//             CPU gate: does the real voice fit the 480k cyc/blk budget?
//
// printf is intentional here (bench is a standalone diagnostic mode excluded
// from the shipping image — CLAUDE.md allows it).
//
// All kernel state is module-static so the compiler cannot dead-store-eliminate
// the loops. The buffer passed to each kernel is reused across kernels — its
// content after each kernel feeds the next, preventing constant-folding.
#ifdef SYNTH_BENCH

#include "bench.h"
#include "platform.h"
#include "synth.h"       // synth_init, synth_render, engine_note_on, engine_set_param,
                         // engine_set_routings, engine_active_voices

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

// Monitoring loudness for the Section-2 proxy ramp, which plays through the real
// codec. Lower to taste — it only attenuates the audible output, not the cycle
// measurement (each voice still does identical work). Section 3 is silent
// (renders into scratch buffers, never reaches the audio sink). ~ -16 dB.
#define BENCH_OUTPUT_GAIN 0.15f

// Real-voice ramp: how many synth_render calls per measurement step.
// 500 blocks × 64 samples / 48 000 Hz ≈ 667 ms — enough to average jitter
// and keep envelopes stable across the count.
#define REAL_REPEATS 500

// Warmup blocks before timing: enough to push envelopes through attack+decay
// into sustain (at default INIT ADSR, attack ≈ 10 ms → ~7 blocks at 64/48k).
// 200 blocks ≈ 267 ms — generous buffer for any default attack/decay.
#define REAL_WARMUP  200

// C-visible Routing struct layout — must exactly match engine/mod_matrix.h's
// C++ Routing struct. Fields: source(u8) + 1-byte align pad + dest_param_id(u16)
// + depth(f32) + curve(u8) + 3-byte tail pad = 12 bytes.
// The _Static_assert below guards against layout drift.
typedef struct {
    uint8_t  source;
    uint8_t  _pad1;
    uint16_t dest_param_id;
    float    depth;
    uint8_t  curve;
    uint8_t  _pad2[3];
} BenchRouting;
// ModSource::NONE=0, ModSource::LFO1=1, ModSource::ENV2=4, ModCurve::LIN=0
// kPresetDestPwm=0xFFFD (PWM sentinel, same as in preset.cpp Stage 3b-ii).
// We pass this array to engine_set_routings as 'const struct Routing*'; the
// BenchRouting and Routing structs are layout-compatible by the assert above.
// Verify size matches the C++ struct (12 bytes on both RV32 and x86-64).
_Static_assert(sizeof(BenchRouting) == 12,
               "BenchRouting size mismatch — update padding to match mod_matrix.h Routing");

// "Clean 106" factory routings (same as the INIT preset in preset.cpp):
//   slot 0: ENV2 → FILTER_CUTOFF, depth +0.35, LIN
//   slot 1: LFO1 → OSC_PWM (sentinel 0xFFFD), depth +0.20, LIN
// ModSource: NONE=0, LFO1=1, ENV2=4; dest: FILTER_CUTOFF=0x20, PWM=0xFFFD.
static const BenchRouting s_clean106_routings[2] = {
    { .source = 4, ._pad1 = 0, .dest_param_id = 0x20, .depth = 0.35f, .curve = 0, ._pad2 = {0,0,0} },
    { .source = 1, ._pad1 = 0, .dest_param_id = 0xFFFD, .depth = 0.20f, .curve = 0, ._pad2 = {0,0,0} },
};

// ParamId values needed in bench (keep in sync with param_id.h — these are
// stable IDs, never renumbered, so literals here are safe).
#define BENCH_PARAM_CHORUS_MODE   0x53u   // 0=off, 1=chorus I, 2=chorus II
#define BENCH_PARAM_UNISON_COUNT  0x65u   // stepped 1..8
#define BENCH_PARAM_UNISON_DETUNE 0x66u   // cents 0..50

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

            // Envelope multiply + stereo accumulate (BENCH_OUTPUT_GAIN keeps
            // the monitored level comfortable; does not change the work done).
            float s    = low * env * BENCH_OUTPUT_GAIN;
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
// real_voice_row — time one synth_render call after nv voices are active.
//
// Reuses the same t0/t1 cycle-counter and table-format logic as the proxy
// ramp.  Called from bench_run for each ramp step and the worst-case line.
// -------------------------------------------------------------------------
static void real_voice_row(uint32_t nv_label, uint32_t block_period, uint32_t cps,
                           uint32_t block_size, float* left, float* right) {
    // Warmup: drain note events into the engine + let envelopes settle.
    for (int w = 0; w < REAL_WARMUP; w++) {
        synth_render(left, right, block_size, NULL);
    }

    int active = engine_active_voices();

    // Time REAL_REPEATS consecutive synth_render calls.
    uint64_t t0 = platform_cycles_now();
    for (int r = 0; r < REAL_REPEATS; r++) {
        synth_render(left, right, block_size, NULL);
    }
    uint64_t t1 = platform_cycles_now();

    uint32_t cyc_blk = (uint32_t)((t1 - t0) / REAL_REPEATS);
    float    pct     = 100.0f * (float)cyc_blk / (float)block_period;
    int32_t  mrg     = (int32_t)block_period - (int32_t)cyc_blk;
    const char* verdict = (pct <= 70.0f) ? "OK" : (pct <= 85.0f) ? "WARN" : "OVER";

    printf("  %6" PRIu32 "  %3d active  %8" PRIu32 "  %7.1f%%  %10" PRId32 "  %s\n",
           nv_label, active, cyc_blk, pct, mrg, verdict);
}

// -------------------------------------------------------------------------
// bench_run — public entry point
// -------------------------------------------------------------------------
void bench_run(uint32_t sample_rate, uint32_t block_size) {
    // The device console is USB-Serial-JTAG, which isn't a TTY — so newlib
    // block-buffers stdout and our small printf table never fills the buffer,
    // staying invisible in the monitor (ESP_LOG bypasses stdio, so radio logs
    // still show). Force unbuffered so the table streams live. No-op-ish on host.
    setvbuf(stdout, NULL, _IONBF, 0);

    uint32_t cps          = platform_cycles_per_sec();
    uint32_t block_period = (uint32_t)((uint64_t)cps * block_size / sample_rate);
    float    block_us     = block_size * 1e6f / (float)sample_rate;

    printf("\n");
    printf("============================================================\n");
    printf("  Tanmatsu Synth — CPU Benchmark (Stage 0.5 + Stage 3d-ii)\n");
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
    // Section 2 — Load ramp: proxy voice (Stage 0.5 baseline)
    // Fake voice (PolyBLEP saw + SVF + env) via the real audio callback.
    // Kept for comparison; the real-voice ramp in Section 3 is the deliverable.
    // ----------------------------------------------------------------
    for (int v = 0; v < MAX_BENCH_VOICES; v++) {
        s_voices[v].phase    = (float)v / (float)MAX_BENCH_VOICES;
        s_voices[v].svf_low  = 1e-20f;
        s_voices[v].svf_band = 1e-20f;
        s_voices[v].env      = 1.0f;
    }
    atomic_store(&s_n_voices, 1u);
    atomic_store(&s_render_cycles, 0u);

    printf("  [Section 2] Proxy load ramp: fake voice (osc+SVF+env) via audio callback\n");
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
        printf("  Safe proxy ceiling (<=70%%): ~%" PRIu32 " voices\n",
               (ceiling_voices > 1) ? ceiling_voices - 1 : 1);
    }
    printf("  (Proxy numbers for context — see Section 3 for real-voice cost.)\n");
    printf("============================================================\n");
    printf("\n");

    // ----------------------------------------------------------------
    // Section 3 — Real-voice load ramp (Stage 3d-ii CPU gate)
    //
    // Drives synth_render() directly (not through the audio callback) in
    // a tight timing loop — the same t0/t1 cycle-counter pattern as the
    // proxy kernels.  N=1..kNumVoices genuine Juno voices, each with the
    // Clean 106 factory routings active (ENV2→cutoff + LFO1→PWM), so the
    // mod matrix, two LFOs, and ENV2 are all running during measurement.
    //
    // After the ramp, a worst-case "unison ceiling" line measures all
    // kNumVoices voices detuned on a single note with chorus ON.
    //
    // Results answer: does the real 8-voice Juno voice fit the budget?
    //   Budget: 480 000 cyc/blk (360 MHz, 64 smp @ 48 kHz).
    //   Safe ceiling: 70% = 336 000 cyc/blk.
    //   Hard ceiling: 95% = 456 000 cyc/blk.
    // ----------------------------------------------------------------

    // Stereo output buffers for synth_render (static: no alloc in bench).
    static float s_left[BENCH_BLOCK];
    static float s_right[BENCH_BLOCK];

    // Initialise the engine.  synth_init prepares the voice pool and param
    // store; all params start at their JUNO_PARAM_TABLE defaults.
    synth_init(sample_rate, block_size);

    // Load the Clean 106 factory routings so ENV2→cutoff and LFO1→PWM are
    // active during measurement (same as the INIT preset applied at startup).
    // engine_set_routings takes 'const struct Routing*'; BenchRouting is
    // layout-compatible (verified by _Static_assert above).
    engine_set_routings((const struct Routing*)s_clean106_routings,
                        (int)(sizeof(s_clean106_routings) / sizeof(s_clean106_routings[0])));

    printf("  [Section 3] Real-voice load ramp (Stage 3d-ii gate)\n");
    printf("  Engine: real Juno voice — PolyBLEP saw+sub+noise → SVF\n");
    printf("  → 2×ADSR + 2×LFO + mod matrix (Clean 106 routings active).\n");
    printf("  Timing: synth_render() direct call (REAL_REPEATS=%d blocks).\n", REAL_REPEATS);
    printf("  Target ceiling: 70%% period (headroom for FX + UI jitter)\n");
    printf("  Budget: block_period=%" PRIu32 " cyc  70%%=%" PRIu32 "  95%%=%" PRIu32 "\n",
           block_period,
           (uint32_t)(0.70f * (float)block_period),
           (uint32_t)(0.95f * (float)block_period));
    printf("  voices  active    cyc/blk   %%period  margin_cyc  verdict\n");
    printf("------------------------------------------------------------\n");

    // Ramp N=1..kNumVoices.  Pitches spread over a minor-seventh chord to
    // avoid any voice-allocation degenerate case.
    static const uint8_t kRampPitches[] = {48, 52, 55, 59, 60, 64, 67, 71};
    _Static_assert(sizeof(kRampPitches) >= 8, "need at least kNumVoices pitches");

    for (int nv = 1; nv <= 8 /*kNumVoices*/; nv++) {
        // Re-init engine for a clean slate each ramp step.
        synth_init(sample_rate, block_size);
        engine_set_routings((const struct Routing*)s_clean106_routings,
                            (int)(sizeof(s_clean106_routings) / sizeof(s_clean106_routings[0])));

        // Trigger nv notes (one call per voice, distinct pitches).
        for (int i = 0; i < nv; i++) {
            engine_note_on(kRampPitches[i], 100);
        }

        real_voice_row((uint32_t)nv, block_period, cps, block_size, s_left, s_right);
    }

    printf("------------------------------------------------------------\n");

    // Worst-case line: UNISON_COUNT=8 (all voices) on one note + Chorus I.
    // This is the true CPU ceiling: full 8-voice pool, mod matrix active on
    // every voice, detuned by UNISON_DETUNE, chorus BBD running.
    printf("  Worst-case: UNISON_COUNT=8, CHORUS_MODE=1 (Chorus I)\n");
    {
        synth_init(sample_rate, block_size);
        engine_set_routings((const struct Routing*)s_clean106_routings,
                            (int)(sizeof(s_clean106_routings) / sizeof(s_clean106_routings[0])));
        // Set unison stack to all 8 voices with 7-cent default detune.
        engine_set_param(BENCH_PARAM_UNISON_COUNT,  8.0f);
        engine_set_param(BENCH_PARAM_UNISON_DETUNE, 7.0f);
        // Enable Chorus I.
        engine_set_param(BENCH_PARAM_CHORUS_MODE, 1.0f);
        // Drain params via one render call so UNISON_COUNT=8 is applied to
        // the allocator BEFORE note_on — otherwise the note arrives when
        // unison_count is still 1 (param drain happens after note drain
        // inside synth_render, so we must flush params first).
        synth_render(s_left, s_right, block_size, NULL);
        // Single note now triggers U=8 voices (allocator already sees count=8).
        engine_note_on(60, 100);   // middle C

        real_voice_row(8u /*nv_label=8 voices in the pool*/, block_period, cps,
                       block_size, s_left, s_right);
    }

    printf("============================================================\n");
    printf("  Budget: 480 000 cyc/blk @ 360 MHz (64 smp / 48 kHz).\n");
    printf("  Safe ceiling (<=70%%): 336 000 cyc/blk.\n");
    printf("  Hard ceiling (<=95%%): 456 000 cyc/blk.\n");
    printf("  Record these numbers in specs/stages/stage-0.5-results.md\n");
    printf("  (or a new stage-3d-ii-results.md) to close the 3d-ii gate.\n");
    printf("============================================================\n");
    printf("\n");

    // ----------------------------------------------------------------
    // Section 4 — Fixed-overhead decomposition
    //
    // Isolates the cost of synth_render() with ALL voices idle (no active
    // notes), so what we measure is:
    //   - param-event drain (command ring)
    //   - the 8-slot voice allocator loop (early-return in render())
    //   - master chorus (on/off comparison)
    //   - soft-clip + stereo bus accumulation
    //
    // Procedure:
    //   4a) No notes, chorus off  → "idle render (chorus off)"
    //   4b) No notes, chorus I    → "idle render (Chorus I)"
    //       derived line          → "--> chorus I cost" = 4b − 4a
    //   4c) No notes, chorus II   → "idle render (Chorus II)"
    //
    // The Section 3 ramp minus this idle overhead gives the pure per-voice
    // DSP cost as a sanity check.
    // ----------------------------------------------------------------
    printf("  [Section 4] Fixed-overhead decomposition (idle render)\n");
    printf("  Measures: param drain + allocator loop + master bus;\n");
    printf("  chorus variants isolate BBD chorus cost.\n");
    printf("  Timing: synth_render() direct call (REAL_REPEATS=%d blocks).\n",
           REAL_REPEATS);
    printf("  %-28s  %8s  %7s\n", "label", "cyc/blk", "%period");
    printf("------------------------------------------------------------\n");

    // Helper lambda-equivalent: time REAL_REPEATS idle renders and print a row.
    // We keep the body inline so it stays pure C (no compound literals / closures).
    uint32_t idle_chorus_off, idle_chorus_I, idle_chorus_II;

    // 4a: clean init, no notes, chorus off (default after synth_init).
    {
        synth_init(sample_rate, block_size);
        engine_set_routings((const struct Routing*)s_clean106_routings,
                            (int)(sizeof(s_clean106_routings) /
                                  sizeof(s_clean106_routings[0])));
        // Warmup: drain any param events and let the engine settle.
        for (int w = 0; w < 10; w++) synth_render(s_left, s_right, block_size, NULL);

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < REAL_REPEATS; r++)
            synth_render(s_left, s_right, block_size, NULL);
        uint64_t t1 = platform_cycles_now();
        idle_chorus_off = (uint32_t)((t1 - t0) / REAL_REPEATS);
        float pct = 100.0f * (float)idle_chorus_off / (float)block_period;
        printf("  %-28s  %8" PRIu32 "  %7.2f%%\n",
               "idle render (chorus off)", idle_chorus_off, pct);
    }

    // 4b: same engine, enable Chorus I, flush with one render, then measure.
    {
        // Re-init to guarantee a clean state (chorus off at init).
        synth_init(sample_rate, block_size);
        engine_set_routings((const struct Routing*)s_clean106_routings,
                            (int)(sizeof(s_clean106_routings) /
                                  sizeof(s_clean106_routings[0])));
        engine_set_param(BENCH_PARAM_CHORUS_MODE, 1.0f);
        // Flush: one render drains the param event so chorus is active.
        synth_render(s_left, s_right, block_size, NULL);
        // Short warmup.
        for (int w = 0; w < 9; w++) synth_render(s_left, s_right, block_size, NULL);

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < REAL_REPEATS; r++)
            synth_render(s_left, s_right, block_size, NULL);
        uint64_t t1 = platform_cycles_now();
        idle_chorus_I = (uint32_t)((t1 - t0) / REAL_REPEATS);
        float pct = 100.0f * (float)idle_chorus_I / (float)block_period;
        printf("  %-28s  %8" PRIu32 "  %7.2f%%\n",
               "idle render (Chorus I)", idle_chorus_I, pct);

        // Derived: chorus I cost = 4b − 4a.
        uint32_t chorus_cost = (idle_chorus_I > idle_chorus_off)
            ? (idle_chorus_I - idle_chorus_off) : 0u;
        float chorus_pct = 100.0f * (float)chorus_cost / (float)block_period;
        printf("  %-28s  %8" PRIu32 "  %7.2f%%\n",
               "--> chorus I cost", chorus_cost, chorus_pct);
    }

    // 4c: Chorus II.
    {
        synth_init(sample_rate, block_size);
        engine_set_routings((const struct Routing*)s_clean106_routings,
                            (int)(sizeof(s_clean106_routings) /
                                  sizeof(s_clean106_routings[0])));
        engine_set_param(BENCH_PARAM_CHORUS_MODE, 2.0f);
        synth_render(s_left, s_right, block_size, NULL);
        for (int w = 0; w < 9; w++) synth_render(s_left, s_right, block_size, NULL);

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < REAL_REPEATS; r++)
            synth_render(s_left, s_right, block_size, NULL);
        uint64_t t1 = platform_cycles_now();
        idle_chorus_II = (uint32_t)((t1 - t0) / REAL_REPEATS);
        float pct = 100.0f * (float)idle_chorus_II / (float)block_period;
        printf("  %-28s  %8" PRIu32 "  %7.2f%%\n",
               "idle render (Chorus II)", idle_chorus_II, pct);

        uint32_t chorus_cost = (idle_chorus_II > idle_chorus_off)
            ? (idle_chorus_II - idle_chorus_off) : 0u;
        float chorus_pct = 100.0f * (float)chorus_cost / (float)block_period;
        printf("  %-28s  %8" PRIu32 "  %7.2f%%\n",
               "--> chorus II cost", chorus_cost, chorus_pct);
    }

    printf("============================================================\n");
    printf("  Section 4 note: subtract idle_chorus_off from a Section-3\n");
    printf("  N-voice row to get the pure per-voice DSP contribution.\n");
    printf("============================================================\n");
    printf("\n");

    // ----------------------------------------------------------------
    // Section 5 — Per-voice DSP-block micro-bench (C++ wrappers)
    //
    // Implemented in engine/bench_blocks.cpp to allow C++ header includes.
    // bench_blocks_run() times each real building block in isolation.
    // ----------------------------------------------------------------
    printf("  [Section 5] Per-block DSP micro-bench (real wrappers, -O2)\n");
    printf("  Measures each DSP building block at block size = %" PRIu32 " samples.\n",
           block_size);
    printf("  %-32s  %7s  %7s  %6s  %7s\n",
           "label", "cyc/blk", "cyc/smp", "us/blk", "%period");
    printf("------------------------------------------------------------\n");
    bench_blocks_run(block_period, cps, block_size);
    printf("============================================================\n");
    printf("\n");

    // Hang so the serial monitor can capture the output before watchdog fires.
    // On host, exit normally after printing.
#ifndef ESP_PLATFORM
    return;
#else
    for (;;) platform_sleep_ms(1000);
#endif
}

#endif  // SYNTH_BENCH
