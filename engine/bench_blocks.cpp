// engine/bench_blocks.cpp — Section 5: per-block DSP micro-bench.
//
// Times each real C++ DSP building block in isolation at the device sample
// rate, so we know the -O2 per-block contribution of each piece.
//
// Excluded from the shipping image (SYNTH_BENCH guard), same as bench.c.
// Lives in engine/ to reach dsp/ wrappers + DaisySP headers without touching
// the audio path or including esp_/bsp_/SDL/miniaudio headers.
//
// Findings feed the next optimisation round (see specs/MEMORY.md Stage 3d-ii
// gate): confirms/denies the LFO-sinf hypothesis, shows ModMatrix eval cost,
// and gives a per-block breakdown that explains the Section 3 voice cost.
#ifdef SYNTH_BENCH

#include "bench.h"

// dsp/ wrappers (thin header-only C++ over DaisySP).
#include "dsp/osc.h"
#include "dsp/filter.h"
#include "dsp/env.h"
#include "dsp/lfo.h"

// DaisySP headers (direct, for modules not wrapped in dsp/).
#include "Noise/whitenoise.h"

// ModMatrix (engine, C++).
#include "mod_matrix.h"

#include <inttypes.h>
#include <stdio.h>

// How many block-iterations per kernel measurement.
// 5000 × 64 smp / 48 kHz ≈ 6.7 s total — enough to swamp timer overhead.
// ModMatrix eval is per-block (n=1 semantics), uses a higher count.
#define BB_REPEATS      5000
#define BB_MOD_REPEATS  20000

// Sink: written after every kernel so nothing is dead-stripped.
static volatile float bb_sink = 0.0f;

// -------------------------------------------------------------------------
// Local timing helper (mirrors bench.c measure() but for C++ objects).
// The fn pointer approach doesn't work for lambdas, so we macro-wrap instead.
// -------------------------------------------------------------------------

// Print one row in the Section-5 table.
static void bb_print_row(const char* label, uint64_t t0, uint64_t t1,
                          uint32_t reps, uint32_t n,
                          uint32_t block_period, uint32_t cps)
{
    uint32_t cyc_blk = (uint32_t)((t1 - t0) / reps);
    uint32_t cyc_smp = (n > 0) ? (uint32_t)(cyc_blk / n) : cyc_blk;
    float    us_blk  = (float)cyc_blk * 1e6f / (float)cps;
    float    pct     = 100.0f * (float)cyc_blk / (float)block_period;
    printf("  %-32s  %7" PRIu32 "  %7" PRIu32 "  %6.2f  %7.2f%%\n",
           label, cyc_blk, cyc_smp, us_blk, pct);
}

// Needed for platform_cycles_now().
#include "platform.h"

// -------------------------------------------------------------------------
// bench_blocks_run — public C-ABI entry (declared in bench.h)
// -------------------------------------------------------------------------
extern "C" void bench_blocks_run(uint32_t block_period, uint32_t cps, uint32_t n)
{
    // Single scratch buffer; reused across kernels so content feeds the next
    // (prevents constant-folding across iterations).
    static float buf[256];
    if (n > 256) n = 256;   // safety cap (block_size is always 64)
    for (uint32_t i = 0; i < n; i++) buf[i] = 0.01f * ((float)i - 32.0f);

    const float sr = 48000.0f;

    // ------------------------------------------------------------------
    // dsp::Osc — PolyBLEP sawtooth
    // ------------------------------------------------------------------
    {
        static dsp::Osc osc;
        osc.init(sr);
        osc.set_freq(220.0f);

        // Warmup.
        for (uint32_t i = 0; i < n; i++) buf[i] = osc.process();
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = osc.process();
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("dsp::Osc PolyBLEP saw",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // dsp::Filter — SVF, block-rate freq (set_freq once per block)
    // ------------------------------------------------------------------
    {
        static dsp::Filter flt;
        flt.init(sr);
        flt.set_res(0.3f);
        flt.set_freq(2000.0f);   // set once before loop — block-rate design

        // Warmup.
        for (uint32_t i = 0; i < n; i++) {
            flt.process(buf[i]);
            buf[i] = flt.output();
        }
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            flt.set_freq(2000.0f);   // block-rate set_freq (once per block)
            for (uint32_t i = 0; i < n; i++) {
                flt.process(buf[i]);
                buf[i] = flt.output();
            }
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("dsp::Filter SVF (block-rate freq)",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // dsp::Env — ADSR (gate=true, riding sustain)
    // ------------------------------------------------------------------
    {
        static dsp::Env env;
        env.init(sr);
        env.set_attack(0.01f);
        env.set_decay(0.1f);
        env.set_sustain(0.7f);
        env.set_release(0.3f);

        // Warmup — process enough to reach sustain.
        for (uint32_t i = 0; i < n; i++) buf[i] = env.process(true);
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = env.process(true);
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("dsp::Env ADSR",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // dsp::Lfo SINE — per-sample sinf
    // ------------------------------------------------------------------
    {
        static dsp::Lfo lfo_sine;
        lfo_sine.init(sr);
        lfo_sine.set_rate(1.0f);
        lfo_sine.set_waveform(dsp::LfoWave::SINE);

        // Warmup.
        for (uint32_t i = 0; i < n; i++) buf[i] = lfo_sine.process();
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = lfo_sine.process();
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("dsp::Lfo SINE (per-sample sinf)",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // dsp::Lfo TRI — triangle, no transcendental
    // ------------------------------------------------------------------
    {
        static dsp::Lfo lfo_tri;
        lfo_tri.init(sr);
        lfo_tri.set_rate(1.0f);
        lfo_tri.set_waveform(dsp::LfoWave::TRI);

        // Warmup.
        for (uint32_t i = 0; i < n; i++) buf[i] = lfo_tri.process();
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = lfo_tri.process();
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("dsp::Lfo TRI",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // WhiteNoise (DaisySP direct)
    // ------------------------------------------------------------------
    {
        static daisysp::WhiteNoise noise;
        noise.Init();

        // Warmup.
        for (uint32_t i = 0; i < n; i++) buf[i] = noise.Process();
        bb_sink = buf[0];

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_REPEATS; r++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = noise.Process();
        }
        uint64_t t1 = platform_cycles_now();
        bb_sink = buf[n - 1];
        bb_print_row("WhiteNoise",
                     t0, t1, BB_REPEATS, n, block_period, cps);
    }

    // ------------------------------------------------------------------
    // ModMatrix eval — per-block call (n=1 semantics: one eval per block)
    //
    // Build the 2-slot Clean-106 matrix (same as s_clean106_routings in bench.c)
    // and measure one eval() call per iteration. cyc/blk = cost of a single eval.
    //   slot 0: ENV2 → FILTER_CUTOFF, depth +0.35, LIN
    //   slot 1: LFO1 → kPresetDestPwm=0xFFFD, depth +0.20, LIN
    // ------------------------------------------------------------------
    {
        static ModMatrix mat;
        mat.clear();
        Routing r0;
        r0.source       = static_cast<uint8_t>(ModSource::ENV2);
        r0.dest_param_id = 0x20u;   // FILTER_CUTOFF
        r0.depth        = 0.35f;
        r0.curve        = static_cast<uint8_t>(ModCurve::LIN);
        mat.set_route(0, r0);

        Routing r1;
        r1.source       = static_cast<uint8_t>(ModSource::LFO1);
        r1.dest_param_id = 0xFFFDu;  // kPresetDestPwm sentinel
        r1.depth        = 0.20f;
        r1.curve        = static_cast<uint8_t>(ModCurve::LIN);
        mat.set_route(1, r1);

        ModSources msrc;
        msrc.lfo1 = 0.5f;
        msrc.env2 = 0.8f;

        // Warmup.
        volatile ModOutputs out = mat.eval(msrc);
        bb_sink = out.cutoff_mod;

        uint64_t t0 = platform_cycles_now();
        for (int r = 0; r < BB_MOD_REPEATS; r++) {
            ModOutputs o = mat.eval(msrc);
            bb_sink = o.cutoff_mod;   // prevent dead-strip
        }
        uint64_t t1 = platform_cycles_now();
        // Report as cyc/blk (one eval per block); pass n=1 so cyc/smp = cyc/blk.
        bb_print_row("ModMatrix eval (per block)",
                     t0, t1, BB_MOD_REPEATS, 1u, block_period, cps);
    }
}

#endif  // SYNTH_BENCH
