// test_juno106_hpf.cpp — host tests for dsp/juno106_hpf.h (WO-13e-i).
//
// Verifies the four switch positions against the ADR 0026 calibration targets
// and the derivation in specs/notes/juno106-hpf-analysis.md: the two HPF
// corners land within tolerance of -3 dB, bypass is flat unity, the bass-boost
// shelf shows the expected +3 dB-ish lift at 70 Hz and unity up high, output
// stays finite on silence/signal, position switches produce a bounded
// transient (not a blow-up), and the state never leaks a denormal.
#include <math.h>
#include "dsp/juno106_hpf.h"
#include "runner.h"

static const float kFs = 48000.0f;

// Drives a steady-state sine through the filter and returns the settled
// output peak amplitude in dB relative to a unity input sine (0 dB = unity
// gain). Settles for `settle_cycles` full periods before measuring.
static float measure_gain_db(dsp::Juno106Hpf& hpf, float freq) {
    const float w       = 2.0f * (float)M_PI * freq / kFs;
    // Settle: enough samples for the (slow, sub-Hz-ish at worst) transient
    // to decay — first-order pole here is never near z=1, so a few thousand
    // samples is ample; use a generous fixed window shared by all probes.
    const int   settle  = 20000;
    const int   measure = 20000;
    for (int i = 0; i < settle; i++) hpf.process(sinf(w * (float)i));
    float peak = 0.0f;
    for (int i = settle; i < settle + measure; i++) {
        float y = hpf.process(sinf(w * (float)i));
        if (fabsf(y) > peak) peak = fabsf(y);
    }
    // Input sine has unity amplitude, so peak *is* the linear gain.
    return 20.0f * log10f(peak > 1e-9f ? peak : 1e-9f);
}

static void test_position2_hpf_225_near_minus3db(void) {
    test_begin("juno106_hpf: position 2 (225 Hz) ~ -3 dB at corner");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_225HZ);
    float g = measure_gain_db(hpf, 225.0f);
    TEST_ASSERT(fabsf(g - (-3.0f)) < 0.5f, "225 Hz corner not within 0.5 dB of -3 dB");
    test_pass();
}

static void test_position3_hpf_700_near_minus3db(void) {
    test_begin("juno106_hpf: position 3 (700 Hz) ~ -3 dB at corner");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_700HZ);
    float g = measure_gain_db(hpf, 700.0f);
    TEST_ASSERT(fabsf(g - (-3.0f)) < 0.5f, "700 Hz corner not within 0.5 dB of -3 dB");
    test_pass();
}

static void test_position2_below_corner_rolls_off(void) {
    test_begin("juno106_hpf: position 2 rolls off well below 225 Hz");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_225HZ);
    float g = measure_gain_db(hpf, 70.0f);
    TEST_ASSERT(g < -8.0f, "70 Hz should be well attenuated by the 225 Hz HPF");
    test_pass();
}

static void test_position1_bypass_flat_unity(void) {
    test_begin("juno106_hpf: position 1 (bypass) flat unity");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_BYPASS);
    static const float kProbeFreqs[] = {70.0f, 225.0f, 700.0f, 1000.0f, 5000.0f};
    for (unsigned i = 0; i < sizeof(kProbeFreqs) / sizeof(kProbeFreqs[0]); i++) {
        float g = measure_gain_db(hpf, kProbeFreqs[i]);
        TEST_ASSERT(fabsf(g) < 0.05f, "bypass not flat unity at probed frequency");
    }
    test_pass();
}

static void test_position0_bass_boost_shape(void) {
    test_begin("juno106_hpf: position 0 bass boost ~+3 dB at 70 Hz, unity up high");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_BASS_BOOST);
    float g70 = measure_gain_db(hpf, 70.0f);
    TEST_ASSERT(fabsf(g70 - 3.0f) < 0.5f, "70 Hz boost not within 0.5 dB of +3 dB");

    dsp::Juno106Hpf hpf2;
    hpf2.init(kFs);
    hpf2.set_position(dsp::JUNO106_HPF_BASS_BOOST);
    float g5k = measure_gain_db(hpf2, 5000.0f);
    TEST_ASSERT(fabsf(g5k) < 0.2f, "position 0 should be ~flat/unity well above the shelf");
    test_pass();
}

static void test_silence_and_signal_finite(void) {
    test_begin("juno106_hpf: finite output on silence and signal, all positions");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    for (int pos = 0; pos < 4; pos++) {
        hpf.set_position((dsp::Juno106HpfPosition)pos);
        float y = 0.0f;
        for (int i = 0; i < 5000; i++) y = hpf.process(0.0f);
        TEST_ASSERT(isfinite(y), "non-finite output on silence");
        for (int i = 0; i < 5000; i++) {
            y = hpf.process(sinf(2.0f * (float)M_PI * 440.0f * (float)i / kFs));
        }
        TEST_ASSERT(isfinite(y), "non-finite output on signal");
    }
    test_pass();
}

// A position switch on a running filter must not produce a discontinuity
// blow-up: the sample immediately after the switch stays within a small
// bounded multiple of the pre-switch signal level (first-order pole here is
// always stable/well inside the unit circle, so no ringing explosion).
static void test_position_switch_bounded_transient(void) {
    test_begin("juno106_hpf: position switch has bounded transient");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    hpf.set_position(dsp::JUNO106_HPF_BYPASS);
    const float w = 2.0f * (float)M_PI * 440.0f / kFs;
    for (int i = 0; i < 4800; i++) hpf.process(sinf(w * (float)i));

    float pre = hpf.process(sinf(w * 4800.0f));
    hpf.set_position(dsp::JUNO106_HPF_BASS_BOOST);
    float peak = fabsf(pre);
    for (int i = 4801; i < 4801 + 200; i++) {
        float y = hpf.process(sinf(w * (float)i));
        TEST_ASSERT(isfinite(y), "position switch produced non-finite output");
        if (fabsf(y) > peak) peak = fabsf(y);
    }
    // Input amplitude is 1.0; even with the boldest position (bass boost,
    // ~+4.76 dB asymptote) the transient should never exceed ~2x amplitude.
    TEST_ASSERT(peak < 2.0f, "position switch transient exceeded bounded envelope");
    test_pass();
}

// After a long silence, state must not settle into a subnormal (denormal)
// float — the anti-denormal bias (ADR 0012) should keep it clear.
static void test_no_denormal_leak(void) {
    test_begin("juno106_hpf: no denormal leak on silence, all positions");
    dsp::Juno106Hpf hpf;
    hpf.init(kFs);
    for (int pos = 0; pos < 4; pos++) {
        hpf.set_position((dsp::Juno106HpfPosition)pos);
        hpf.process(1.0f);  // kick the state, then decay to silence
        float y = 0.0f;
        for (int i = 0; i < 200000; i++) y = hpf.process(0.0f);
        TEST_ASSERT(isfinite(y), "non-finite after long silence");
        TEST_ASSERT(fpclassify(y) != FP_SUBNORMAL, "output settled into a denormal");
    }
    test_pass();
}

void test_juno106_hpf_suite(void) {
    printf("--- dsp/juno106_hpf.h ---\n");
    test_position2_hpf_225_near_minus3db();
    test_position3_hpf_700_near_minus3db();
    test_position2_below_corner_rolls_off();
    test_position1_bypass_flat_unity();
    test_position0_bass_boost_shape();
    test_silence_and_signal_finite();
    test_position_switch_bounded_transient();
    test_no_denormal_leak();
}
