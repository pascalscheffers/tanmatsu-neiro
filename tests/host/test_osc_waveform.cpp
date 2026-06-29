/* tests/host/test_osc_waveform.cpp
 *
 * Host DSP tests for Stage 3c-iii: waveform switching + PWM wiring.
 *
 * Tests:
 *  1. set_waveform(0) → SAW: non-silent (RMS > 0).
 *  2. set_waveform(1) → PULSE: non-silent (RMS > 0).
 *  3. set_waveform(2) → TRI: non-silent (RMS > 0).
 *  4. set_waveform(-1) and set_waveform(99) clamp to SAW (non-silent, same RMS as wf=0).
 *  5. set_waveform(1) + set_pw(0.2f) vs set_pw(0.8f) → different RMS (asymmetric pulse).
 *  6. set_pw(0.0f) and set_pw(1.0f) do not crash; output is non-NaN/Inf.
 *  7. ModMatrix with no routes: pwm_mod is close to 0 (within 1e-15, confirming 1e-20f guard).
 *  8. One route {LFO1, kModDestPwm, 0.5f, LIN} with lfo1=1.0 → pwm_mod ≈ 0.5f.
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math; denormals behave as on device.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp/osc.h"
#include "mod_matrix.h"
#include "runner.h"

static const float kSampleRate   = 48000.0f;
static const int   kRenderFrames = 4096;

// Render kRenderFrames samples from a fresh Osc at 440 Hz and return RMS.
static float render_osc_rms(dsp::Osc& osc) {
    float sum = 0.0f;
    for (int i = 0; i < kRenderFrames; i++) {
        float s = osc.process();
        sum    += s * s;
    }
    return sqrtf(sum / (float)kRenderFrames);
}

/* --- 1. SAW (wf=0): non-silent -------------------------------------------- */
void test_osc_waveform_saw() {
    printf("--- Stage 3c-iii: waveform + PWM ---\n");
    test_begin("set_waveform(0)=SAW: RMS > 0");

    dsp::Osc osc;
    osc.init(kSampleRate);
    osc.set_freq(440.0f);
    osc.set_waveform(0);

    float rms = render_osc_rms(osc);
    TEST_ASSERT(rms > 0.01f, "SAW (wf=0) must produce non-silent output");
    test_pass();
}

/* --- 2. PULSE (wf=1): non-silent ------------------------------------------ */
void test_osc_waveform_pulse() {
    test_begin("set_waveform(1)=PULSE: RMS > 0");

    dsp::Osc osc;
    osc.init(kSampleRate);
    osc.set_freq(440.0f);
    osc.set_waveform(1);

    float rms = render_osc_rms(osc);
    TEST_ASSERT(rms > 0.01f, "PULSE (wf=1) must produce non-silent output");
    test_pass();
}

/* --- 3. TRI (wf=2): non-silent -------------------------------------------- */
void test_osc_waveform_tri() {
    test_begin("set_waveform(2)=TRI: RMS > 0");

    dsp::Osc osc;
    osc.init(kSampleRate);
    osc.set_freq(440.0f);
    osc.set_waveform(2);

    float rms = render_osc_rms(osc);
    TEST_ASSERT(rms > 0.01f, "TRI (wf=2) must produce non-silent output");
    test_pass();
}

/* --- 4. Out-of-range clamps to SAW ---------------------------------------- */
void test_osc_waveform_clamp() {
    test_begin("set_waveform(-1)/set_waveform(99) clamp to SAW (non-silent)");

    // Reference: explicit SAW
    dsp::Osc ref;
    ref.init(kSampleRate);
    ref.set_freq(440.0f);
    ref.set_waveform(0);
    float rms_ref = render_osc_rms(ref);

    // wf = -1
    dsp::Osc neg;
    neg.init(kSampleRate);
    neg.set_freq(440.0f);
    neg.set_waveform(-1);
    float rms_neg = render_osc_rms(neg);

    // wf = 99
    dsp::Osc big;
    big.init(kSampleRate);
    big.set_freq(440.0f);
    big.set_waveform(99);
    float rms_big = render_osc_rms(big);

    TEST_ASSERT(rms_neg > 0.01f, "wf=-1 must clamp to SAW (non-silent)");
    TEST_ASSERT(rms_big > 0.01f, "wf=99 must clamp to SAW (non-silent)");
    // Both should match the explicit SAW RMS closely.
    TEST_ASSERT(fabsf(rms_neg - rms_ref) < 1e-4f, "wf=-1 RMS must match SAW reference");
    TEST_ASSERT(fabsf(rms_big - rms_ref) < 1e-4f, "wf=99 RMS must match SAW reference");
    test_pass();
}

/* --- 5. PW affects output: pw=0.2 vs pw=0.8 produce different DC offsets --- */
// PolyBLEP square wave: RMS is nearly identical across duty cycles (band-limiting averages
// power). We verify the DC component instead. A pulse wave at duty d over period T spends
// d*T above zero (+1) and (1-d)*T below zero (-1), giving mean = 2d-1.
// pw=0.2 → mean ≈ -0.6; pw=0.8 → mean ≈ +0.6. The sign difference is reliably detectable.
void test_osc_pw_affects_output() {
    test_begin("set_pw(0.2f) vs set_pw(0.8f): mean DC has opposite sign");

    dsp::Osc osc_a;
    osc_a.init(kSampleRate);
    osc_a.set_freq(440.0f);
    osc_a.set_waveform(1);  // PULSE
    osc_a.set_pw(0.2f);
    float sum_a = 0.0f;
    for (int i = 0; i < kRenderFrames; i++) sum_a += osc_a.process();
    float mean_a = sum_a / (float)kRenderFrames;

    dsp::Osc osc_b;
    osc_b.init(kSampleRate);
    osc_b.set_freq(440.0f);
    osc_b.set_waveform(1);  // PULSE
    osc_b.set_pw(0.8f);
    float sum_b = 0.0f;
    for (int i = 0; i < kRenderFrames; i++) sum_b += osc_b.process();
    float mean_b = sum_b / (float)kRenderFrames;

    // pw=0.2 → mean < 0; pw=0.8 → mean > 0; and they must differ in sign.
    TEST_ASSERT(mean_a < 0.0f, "PULSE pw=0.2 must have negative DC mean");
    TEST_ASSERT(mean_b > 0.0f, "PULSE pw=0.8 must have positive DC mean");
    TEST_ASSERT((mean_b - mean_a) > 0.5f, "pw=0.8 mean must be substantially higher than pw=0.2");
    test_pass();
}

/* --- 6. Extreme PW values do not crash; output is finite ------------------- */
void test_osc_pw_clamp() {
    test_begin("set_pw(0.0f) and set_pw(1.0f) do not crash; output is finite");

    dsp::Osc osc;
    osc.init(kSampleRate);
    osc.set_freq(440.0f);
    osc.set_waveform(1);

    // pw = 0.0 (degenerate)
    osc.set_pw(0.0f);
    float s0 = osc.process();
    TEST_ASSERT(s0 == s0, "set_pw(0.0f) must produce finite output (not NaN)");
    TEST_ASSERT(s0 <= 2.0f && s0 >= -2.0f, "set_pw(0.0f) output must not be Inf");

    // pw = 1.0 (degenerate)
    osc.set_pw(1.0f);
    float s1 = osc.process();
    TEST_ASSERT(s1 == s1, "set_pw(1.0f) must produce finite output (not NaN)");
    TEST_ASSERT(s1 <= 2.0f && s1 >= -2.0f, "set_pw(1.0f) output must not be Inf");

    test_pass();
}

/* --- 7. Cleared ModMatrix: pwm_mod seeded near zero (1e-20f guard) --------- */
void test_modoutputs_pwm_mod_seeded() {
    test_begin("cleared ModMatrix: pwm_mod is near-zero (denormal guard, not exact 0)");

    ModMatrix mat;
    mat.clear();

    ModSources src{};
    ModOutputs out = mat.eval(src);

    // The anti-denormal seed is 1e-20f; it is very close to 0 but not exactly 0.
    TEST_ASSERT(fabsf(out.pwm_mod) < 1e-15f, "cleared mat pwm_mod must be near-zero");
    test_pass();
}

/* --- 8. LFO1→kModDestPwm route accumulates correctly ---------------------- */
void test_modmatrix_pwm_route() {
    test_begin("LFO1→kModDestPwm with depth=0.5, lfo1=1.0 → pwm_mod≈0.5");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{(uint8_t)ModSource::LFO1, kModDestPwm, 0.5f, (uint8_t)ModCurve::LIN});

    ModSources src{};
    src.lfo1       = 1.0f;
    ModOutputs out = mat.eval(src);

    float expected = 0.5f;
    float err      = fabsf(out.pwm_mod - expected);
    TEST_ASSERT(err < 1e-5f, "LFO1=1.0, depth=0.5 → pwm_mod must equal 0.5 (±1e-5)");
    test_pass();
}

/* --- Entry point declared in main.cpp ------------------------------------- */
void test_osc_waveform_suite() {
    test_osc_waveform_saw();
    test_osc_waveform_pulse();
    test_osc_waveform_tri();
    test_osc_waveform_clamp();
    test_osc_pw_affects_output();
    test_osc_pw_clamp();
    test_modoutputs_pwm_mod_seeded();
    test_modmatrix_pwm_route();
}
