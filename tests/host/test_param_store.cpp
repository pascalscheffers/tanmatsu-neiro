// tests/host/test_param_store.cpp — ParamStore + ParamDesc table host tests.
// Covers: curve mapping, smoothing convergence, instant params, ring capacity.
// FTZ-off (no -ffast-math in CMakeLists) so denormal behaviour matches device.
#include "engine/param_store.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "runner.h"
#include <math.h>
#include <stdio.h>

// Minimal table helpers — build a one-entry table for isolated curve/smoothing
// tests without depending on the full Juno table.
static ParamDesc make_desc(uint16_t id, float min, float max, float def,
                            ParamCurve curve, float smooth_ms) {
    ParamDesc d{};
    d.id           = id;
    d.group        = GROUP_OSC;
    d.name         = "Test";
    d.short_name   = "T";
    d.min          = min;
    d.max          = max;
    d.def          = def;
    d.curve        = curve;
    d.unit         = UNIT_NONE;
    d.display_fmt  = "%.2f";
    d.midi_cc      = 0xFF;
    d.smoothing_ms = smooth_ms;
    d.flags        = 0;
    return d;
}

// ---- Curve tests --------------------------------------------------------

static void test_curves() {
    printf("--- ParamDesc curves ---\n");

    {
        test_begin("LIN: norm=0 → min");
        ParamDesc d = make_desc(1, 10.0f, 100.0f, 10.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 0.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 10.0f) < 1e-4f, "LIN norm=0 should give min");
        test_pass();
    }
    {
        test_begin("LIN: norm=1 → max");
        ParamDesc d = make_desc(1, 10.0f, 100.0f, 10.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 1.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 100.0f) < 1e-4f, "LIN norm=1 should give max");
        test_pass();
    }
    {
        test_begin("LIN: norm=0.5 → midpoint");
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 0.5f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 0.5f) < 1e-5f, "LIN norm=0.5 should give 0.5");
        test_pass();
    }
    {
        test_begin("EXP: norm=0 → min");
        ParamDesc d = make_desc(1, 20.0f, 20000.0f, 20.0f, CURVE_EXP, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 0.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 20.0f) < 1e-3f, "EXP norm=0 should give min");
        test_pass();
    }
    {
        test_begin("EXP: norm=1 → max");
        ParamDesc d = make_desc(1, 20.0f, 20000.0f, 20.0f, CURVE_EXP, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 1.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 20000.0f) < 1.0f, "EXP norm=1 should give max");
        test_pass();
    }
    {
        test_begin("EXP: norm=0.5 → geometric mean");
        // min * sqrt(max/min) = sqrt(min*max)
        ParamDesc d = make_desc(1, 20.0f, 20000.0f, 20.0f, CURVE_EXP, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 0.5f);
        ps.drain();
        const float expected = sqrtf(20.0f * 20000.0f);  // ≈ 632.5 Hz
        TEST_ASSERT(fabsf(ps.get(1) - expected) < 1.0f,
                    "EXP norm=0.5 should give geometric mean");
        test_pass();
    }
    {
        test_begin("LOG: norm=0 → min, norm=1 → max");
        ParamDesc d = make_desc(1, 0.0f, 100.0f, 0.0f, CURVE_LOG, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 0.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 0.0f) < 1e-4f, "LOG norm=0 should give min");
        ps.param_set_norm(1, 1.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 100.0f) < 1e-4f, "LOG norm=1 should give max");
        test_pass();
    }
    {
        test_begin("STEPPED: 3 steps [0..2]");
        ParamDesc d = make_desc(1, 0.0f, 2.0f, 0.0f, CURVE_STEPPED, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        // norm in [0, 1/3) → 0
        ps.param_set_norm(1, 0.0f);  ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 0.0f) < 0.1f, "STEPPED norm=0 → 0");
        // norm in [1/3, 2/3) → 1
        ps.param_set_norm(1, 0.5f);  ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 1.0f) < 0.1f, "STEPPED norm=0.5 → 1");
        // norm=1 → 2
        ps.param_set_norm(1, 1.0f);  ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 2.0f) < 0.1f, "STEPPED norm=1 → 2");
        test_pass();
    }
}

// ---- Smoothing tests ----------------------------------------------------

static void test_smoothing() {
    printf("--- ParamStore smoothing ---\n");

    {
        test_begin("instant (smoothing_ms=0): updates in one drain()");
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 1.0f);
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 1.0f) < 1e-5f,
                    "Instant param should reach target after one drain");
        test_pass();
    }

    {
        test_begin("smoothing: converges within 5x smoothing_ms");
        // smoothing_ms = 20 ms, block = 64/48000 ≈ 1.333 ms
        // tau = 20 ms → alpha ≈ 0.0638
        // After 5 tau (75 blocks): error < 1% of range
        const float sm_ms = 20.0f;
        const float sr    = 48000.0f;
        const int   bs    = 64;
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, sm_ms);
        ParamStore ps;
        ps.init(&d, 1, sr, bs);
        ps.param_set_norm(1, 1.0f);  // target = 1.0, start from 0.0

        const float block_dt = (float)bs / sr;
        const int   n_blocks = (int)(5.0f * sm_ms * 0.001f / block_dt) + 1;
        for (int b = 0; b < n_blocks; b++) ps.drain();

        const float val = ps.get(1);
        TEST_ASSERT(fabsf(val - 1.0f) < 0.01f,
                    "After 5x tau, smoothed value should be within 1% of target");
        test_pass();
    }

    {
        test_begin("smoothing: does not overshoot");
        const float sm_ms = 10.0f;
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, sm_ms);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        ps.param_set_norm(1, 1.0f);
        // Run 200 blocks (well past convergence)
        for (int b = 0; b < 200; b++) ps.drain();
        const float val = ps.get(1);
        // One-pole lowpass never overshoots.
        TEST_ASSERT(val <= 1.0f + 1e-5f, "Smoothed value must not exceed target");
        TEST_ASSERT(val >= 0.0f,         "Smoothed value must stay >= 0");
        test_pass();
    }

    {
        test_begin("init: get() returns default before any drain");
        ParamDesc d = make_desc(1, 0.0f, 2.0f, 0.75f, CURVE_LIN, 5.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        TEST_ASSERT(fabsf(ps.get(1) - 0.75f) < 1e-5f,
                    "Default value should be available immediately after init");
        test_pass();
    }
}

// ---- Ring / burst test --------------------------------------------------

static void test_ring() {
    printf("--- ParamStore ring ---\n");

    {
        test_begin("ring burst: 63 updates land in one drain");
        // Ring capacity = 64 → 63 usable slots.
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        int pushed = 0;
        for (int i = 0; i < 63; i++) {
            if (ps.param_set_norm(1, 1.0f)) pushed++;
        }
        TEST_ASSERT(pushed == 63, "63 pushes into a 64-slot ring should all succeed");
        ps.drain();
        TEST_ASSERT(fabsf(ps.get(1) - 1.0f) < 1e-5f,
                    "After drain, final value should reflect last pushed update");
        test_pass();
    }

    {
        test_begin("ring full: 65th push is dropped gracefully");
        ParamDesc d = make_desc(1, 0.0f, 1.0f, 0.0f, CURVE_LIN, 0.0f);
        ParamStore ps;
        ps.init(&d, 1, 48000.0f, 64);
        for (int i = 0; i < 63; i++) ps.param_set_norm(1, 0.9f);
        const bool accepted = ps.param_set_norm(1, 0.5f);  // 64th push → full
        (void)accepted;  // allowed to drop — not an error, just a bool
        // This must not crash or corrupt state.
        ps.drain();
        test_pass();
    }
}

// ---- Juno table sanity --------------------------------------------------

static void test_juno_table() {
    printf("--- Juno parameter table ---\n");

    {
        test_begin("table: kJunoParamCount > 0");
        TEST_ASSERT(kJunoParamCount > 0, "Juno param table must be non-empty");
        test_pass();
    }

    {
        test_begin("table: all IDs < kParamIdMax");
        for (int i = 0; i < kJunoParamCount; i++) {
            TEST_ASSERT(JUNO_PARAM_TABLE[i].id < kParamIdMax,
                        "Every Juno param ID must be < kParamIdMax");
        }
        test_pass();
    }

    {
        test_begin("table: all IDs unique");
        for (int i = 0; i < kJunoParamCount; i++) {
            for (int j = i + 1; j < kJunoParamCount; j++) {
                TEST_ASSERT(JUNO_PARAM_TABLE[i].id != JUNO_PARAM_TABLE[j].id,
                            "Duplicate param IDs in JUNO_PARAM_TABLE");
            }
        }
        test_pass();
    }

    {
        test_begin("table: defaults initialise ParamStore correctly");
        ParamStore ps;
        ps.init(JUNO_PARAM_TABLE, kJunoParamCount, 48000.0f, 64);
        // Filter cutoff default = 2000 Hz.
        const float cutoff = ps.get(ParamId::FILTER_CUTOFF);
        TEST_ASSERT(fabsf(cutoff - 2000.0f) < 1.0f,
                    "Filter cutoff default should be 2000 Hz");
        // Master gain default = 0.5.
        const float gain = ps.get(ParamId::MASTER_GAIN);
        TEST_ASSERT(fabsf(gain - 0.5f) < 1e-5f,
                    "Master gain default should be 0.5");
        test_pass();
    }

    {
        test_begin("table: param_set physical clamps to [min, max]");
        ParamStore ps;
        ps.init(JUNO_PARAM_TABLE, kJunoParamCount, 48000.0f, 64);
        ps.param_set(ParamId::FILTER_CUTOFF, -100.0f);  // below min → clamped to 20
        ps.drain();
        TEST_ASSERT(ps.get(ParamId::FILTER_CUTOFF) >= 20.0f - 0.1f,
                    "Cutoff below min must be clamped to 20 Hz");
        ps.param_set(ParamId::FILTER_CUTOFF, 999999.0f);  // above max → clamped to 20000
        ps.drain();
        // Need many drains to converge to 20000 (smoothed)
        for (int i = 0; i < 500; i++) ps.drain();
        TEST_ASSERT(ps.get(ParamId::FILTER_CUTOFF) <= 20000.0f + 0.5f,
                    "Cutoff above max must be clamped to 20000 Hz");
        test_pass();
    }
}

void test_param_store_suite() {
    test_curves();
    test_smoothing();
    test_ring();
    test_juno_table();
}
