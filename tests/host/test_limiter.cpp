// test_limiter.cpp — host tests for dsp/limiter.h (ADR 0021).
#include <math.h>
#include "dsp/limiter.h"
#include "dsp/saturate.h"
#include "runner.h"

static const float kSr     = 48000.0f;
static const float kThresh = 0.92f;

// Case 1: below threshold → gr stays exactly 1.0 (bit-transparent)
static void test_limiter_below_threshold(void) {
    test_begin("limiter: below thresh — gr == 1.0 (bit-transparent)");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    for (int i = 0; i < 2000; i++) {
        float gr = lim.process(0.5f);
        TEST_ASSERT(gr == 1.0f, "gr must be exactly 1.0 below threshold");
    }
    test_pass();
}

// Case 2: sustained over-threshold → steady-state converges to thresh/peak
static void test_limiter_sustained_over_threshold(void) {
    test_begin("limiter: sustained over-thresh — steady state at thresh/peak");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    float gr = 1.0f;
    // 48000 samples is >> 120 ms release at 48 kHz — enough to converge
    for (int i = 0; i < 48000; i++) {
        gr = lim.process(1.5f);
    }
    // After convergence: 1.5 * gr should equal thresh within 1e-3
    float limited  = 1.5f * gr;
    float expected = kThresh;  // 0.92
    TEST_ASSERT(fabsf(limited - expected) < 1e-3f, "steady-state limited level != thresh");
    test_pass();
}

// Case 3: attack — instant clamp on first over-threshold sample.
// With instantaneous attack, the very first over-threshold sample must result in a gr
// that keeps peak*gr at or below threshold (no transient escape into soft_clip's ±1.5 wall).
// This is the regression test for the crackle bug: under the OLD 1 ms one-pole attack,
// the first-sample gr was ~1.0 so peak*gr ≈ 2.0, which sailed into soft_clip's hard ±1.5
// ceiling producing audible crackle on hard playing.
static void test_limiter_attack(void) {
    test_begin("limiter: attack — first over-thresh sample clamped to threshold immediately");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // Warm up below threshold so env_gr starts at 1.0
    for (int i = 0; i < 2000; i++) {
        lim.process(0.5f);
    }
    // First over-threshold sample at peak = 2.0: instantaneous attack must clamp now.
    // gr must bring peak to ≤ thresh immediately (gr ≤ thresh/peak + epsilon).
    float gr_first = lim.process(2.0f);
    // Instantaneous attack: peak * gr must be at threshold
    TEST_ASSERT(2.0f * gr_first <= kThresh + 1e-4f,
                "instant attack: first over-thresh sample not clamped to threshold");
    // As a consequence, soft_clip output must stay well below 1.0 (not hitting hard wall)
    float clipped = soft_clip(2.0f * gr_first);
    TEST_ASSERT(clipped < 0.95f, "instant attack: soft_clip output too high — transient escaped into hard clip");
    test_pass();
}

// Case 3b: transient overshoot regression — specifically tests that crackle scenario
// (hard/loud playing with env starting at unity) is fixed.
static void test_limiter_no_transient_overshoot(void) {
    test_begin("limiter: no transient overshoot — soft_clip(peak*gr) < 0.95 on first loud frame");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // Env starts at unity (init state). Feed a loud frame immediately — worst case.
    float gr      = lim.process(2.0f);
    float limited = 2.0f * gr;
    // Must be at or below threshold (instantaneous attack)
    TEST_ASSERT(limited <= kThresh + 1e-4f,
                "first-sample overshoot: peak*gr exceeds threshold (crackle bug regression)");
    // Must NOT be routed into soft_clip's ±1.5 hard-clip zone
    float clipped = soft_clip(limited);
    TEST_ASSERT(clipped < 0.95f, "first-sample overshoot: soft_clip output near hard ceiling (crackle zone)");
    test_pass();
}

// Case 4: net safety — soft_clip(peak * gr) never exceeds full scale.
// With instant attack, the first over-threshold sample is clamped immediately, so
// soft_clip(peak*gr) must be <= 1.0 without needing soft_clip as a safety net.
static void test_limiter_net_safety(void) {
    test_begin("limiter: soft_clip(peak*gr) <= 1.0 with instant attack on first sample");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // warm up at unity
    for (int i = 0; i < 2000; i++) {
        lim.process(0.5f);
    }
    // With instant attack: first sample at peak 2.0 → gr = thresh/2.0 → soft_clip(thresh)
    float gr      = lim.process(2.0f);
    float clipped = soft_clip(2.0f * gr);
    TEST_ASSERT(clipped <= 1.0f, "soft_clip(peak*gr) exceeds 1.0 — limiter did not clamp");
    test_pass();
}

// Case 5: release recovery — returns to unity well within 10× release time constant.
// 120 ms release → τ = 5760 samples. Recovery from env_gr≈0.46 to 0.999 requires
// ~6.3 τ ≈ 36 k samples. We check against 60 k (generous budget) and also assert
// that release is much slower than attack (attack is instantaneous = 1 sample;
// release must take >> 1000 samples — verifying the asymmetry).
static void test_limiter_release(void) {
    test_begin("limiter: release — recovers to >= 0.999 within 60000 samples");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // Drive into reduction
    for (int i = 0; i < 48000; i++) {
        lim.process(2.0f);
    }
    // Now feed below threshold; measure recovery
    int   recover_samples = 0;
    float gr              = 0.0f;
    for (int i = 0; i < 60000; i++) {
        gr = lim.process(0.1f);
        if (gr >= 0.999f) {
            recover_samples = i + 1;
            break;
        }
    }
    TEST_ASSERT(gr >= 0.999f, "limiter did not recover to >= 0.999 within 60000 samples");

    // Attack is instantaneous (1 sample); release must take >> 1000 samples.
    // This confirms the asymmetry: instant attack, slow 120 ms release — no pumping.
    TEST_ASSERT(recover_samples > 1000, "release recovery suspiciously fast (< 1000 samples)");
    test_pass();
}

// Case 6: threshold boundary — no reduction for peak 0..0.9
static void test_limiter_threshold_boundary(void) {
    test_begin("limiter: gr == 1.0 for all peaks 0..0.9");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // sweep peak from 0 to 0.9 in steps of 0.001
    for (int i = 0; i <= 900; i++) {
        float peak = (float)i * 0.001f;
        float gr   = lim.process(peak);
        TEST_ASSERT(gr == 1.0f, "gr != 1.0 below threshold");
    }
    test_pass();
}

// Case 7: NaN / denormal robustness
static void test_limiter_nan_denormal(void) {
    test_begin("limiter: NaN input — all returned gr values finite");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // Feed a NaN once
    float nan_val = 0.0f / 0.0f;
    float gr      = lim.process(nan_val);
    TEST_ASSERT(gr == gr, "gr is NaN after NaN input");  // NaN != NaN
    TEST_ASSERT(gr > 0.0f && gr <= 1.0f, "gr out of (0,1] after NaN input");

    // Recover with normal input and assert everything stays finite
    for (int i = 0; i < 5000; i++) {
        gr = lim.process(0.92f);
        TEST_ASSERT(gr == gr, "gr became NaN during recovery");
        TEST_ASSERT(gr > 0.0f && gr <= 1.0f, "gr out of (0,1] during recovery");
    }
    test_pass();

    test_begin("limiter: huge peak — env stays finite and non-negative");
    dsp::LimiterStereo lim2;
    lim2.init(kSr);
    // Drive with a huge peak
    for (int i = 0; i < 100; i++) {
        gr = lim2.process(1e6f);
        TEST_ASSERT(gr == gr, "gr became NaN on huge peak");
        TEST_ASSERT(gr >= 1e-7f, "gr went below finite floor on huge peak");
    }
    // Back to normal: should stay finite
    for (int i = 0; i < 5000; i++) {
        gr = lim2.process(0.92f);
        TEST_ASSERT(gr == gr, "gr became NaN recovering from huge peak");
        TEST_ASSERT(gr > 0.0f && gr <= 1.0f, "gr out of (0,1] recovering from huge peak");
    }
    test_pass();
}

// Case 8: staged CC7 scenario — gain=0.5, dense-chord peak ~2.0 → clean output
static void test_limiter_cc7_scenario(void) {
    test_begin("limiter: CC7 staged (gain=0.5, peak~2.0) — output <= 1.0, > 0.7");
    dsp::LimiterStereo lim;
    lim.init(kSr);
    // Simulate: MASTER_GAIN=0.5 × channel_vol=1.0; dense chord sums to ~2.0 on mono bus.
    // After gain multiply: peak = 2.0 * 0.5 = 1.0 (just above thresh).
    // Drive to steady state
    float gr = 1.0f;
    for (int i = 0; i < 48000; i++) {
        gr = lim.process(1.0f);  // peak post-gain
    }
    float out = soft_clip(1.0f * gr);
    TEST_ASSERT(out <= 1.0f, "CC7 scenario: soft_clip output exceeds 1.0");
    TEST_ASSERT(out > 0.7f, "CC7 scenario: soft_clip output unexpectedly low (< 0.7)");
    test_pass();
}

void test_limiter_suite(void) {
    printf("--- dsp/limiter.h ---\n");
    test_limiter_below_threshold();
    test_limiter_sustained_over_threshold();
    test_limiter_attack();
    test_limiter_no_transient_overshoot();
    test_limiter_net_safety();
    test_limiter_release();
    test_limiter_threshold_boundary();
    test_limiter_nan_denormal();
    test_limiter_cc7_scenario();
}
