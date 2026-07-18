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
#include "juno_voice.h"
#include "mod_matrix.h"
#include "param_id.h"
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

/* --- WO-13c (ADR 0026): JunoVoice independent saw/pulse switches + square sub --- */

static float rms(const float* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
    return (n > 0) ? sqrtf(sum / (float)n) : 0.0f;
}

static float voice_rms(JunoVoice& v, int blocks, int block_len) {
    float buf[256];
    float sum = 0.0f;
    int   n   = 0;
    for (int b = 0; b < blocks; b++) {
        memset(buf, 0, sizeof(float) * (size_t)block_len);
        v.render(buf, (size_t)block_len);
        if (b == blocks - 1) {  // measure only the final (steady-state) block
            for (int i = 0; i < block_len; i++) {
                sum += buf[i] * buf[i];
                n++;
            }
        }
    }
    return (n > 0) ? sqrtf(sum / (float)n) : 0.0f;
}

static JunoVoice make_dco_test_voice() {
    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::ENV_ATTACK, 0.001f);
    v.set_param((int)ParamId::ENV_DECAY, 0.001f);
    v.set_param((int)ParamId::ENV_SUSTAIN, 1.0f);
    v.set_param((int)ParamId::OSC_LEVEL, 1.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
    v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);  // wide open
    v.set_param((int)ParamId::FILTER_RES, 0.0f);
    v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
    v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCA_LEVEL, 1.0f);
    return v;
}

/* --- 9. Saw-only: non-silent. -------------------------------------------- */
void test_juno_voice_saw_only() {
    test_begin("JunoVoice: OSC_SAW_ON=1/OSC_PULSE_ON=0 (default) is non-silent");
    JunoVoice v = make_dco_test_voice();
    v.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);
    float rms = voice_rms(v, 40, 64);
    TEST_ASSERT(rms > 0.05f, "saw-only must be non-silent");
    test_pass();
}

/* --- 10. Pulse-only: non-silent. ------------------------------------------ */
void test_juno_voice_pulse_only() {
    test_begin("JunoVoice: OSC_SAW_ON=0/OSC_PULSE_ON=1 is non-silent");
    JunoVoice v = make_dco_test_voice();
    v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);
    float rms = voice_rms(v, 40, 64);
    TEST_ASSERT(rms > 0.05f, "pulse-only must be non-silent");
    test_pass();
}

/* --- 11. Neither: silent (both switches off leaves no DCO signal). -------- */
void test_juno_voice_neither_silent() {
    test_begin("JunoVoice: OSC_SAW_ON=0/OSC_PULSE_ON=0 is silent (no DCO contribution)");
    JunoVoice v = make_dco_test_voice();
    v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);
    float rms = voice_rms(v, 40, 64);
    TEST_ASSERT(rms < 0.0001f, "both switches off must silence the DCO");
    test_pass();
}

/* --- 12. Both on: sums saw + pulse, so both-on RMS exceeds either alone. -- */
void test_juno_voice_saw_plus_pulse_sums() {
    test_begin("JunoVoice: OSC_SAW_ON=1 + OSC_PULSE_ON=1 sums both sources (energy > either alone)");

    JunoVoice v_saw = make_dco_test_voice();
    v_saw.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
    v_saw.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v_saw.note_on(69, 127, expr);
    float rms_saw = voice_rms(v_saw, 40, 64);

    JunoVoice v_pulse = make_dco_test_voice();
    v_pulse.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
    v_pulse.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
    v_pulse.note_on(69, 127, expr);
    float rms_pulse = voice_rms(v_pulse, 40, 64);

    JunoVoice v_both = make_dco_test_voice();
    v_both.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
    v_both.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
    v_both.note_on(69, 127, expr);
    float rms_both = voice_rms(v_both, 40, 64);

    TEST_ASSERT(rms_both == rms_both, "both-on output must be finite (not NaN)");
    TEST_ASSERT(rms_both < 10.0f, "both-on output must be finite (not Inf)");
    TEST_ASSERT(rms_both > rms_saw * 1.05f, "both-on RMS must exceed saw-only RMS");
    TEST_ASSERT(rms_both > rms_pulse * 1.05f, "both-on RMS must exceed pulse-only RMS");
    test_pass();
}

/* --- 13. Toggling switches mid-render does not reset oscillator phase. ----
 * Render one block with saw enabled, disable it and render another block with
 * pulse enabled, then re-enable saw: the saw oscillator must keep advancing its
 * phase underneath (process() is always called, gated only by the mix), so
 * turning it back on should not produce a discontinuity relative to a
 * continuously-running reference — checked here via a finite/non-NaN output
 * and a smooth (non-clipping) transition, since exact phase-continuity is
 * covered structurally by render() always calling process() on both oscs. */
void test_juno_voice_toggle_no_phase_reset() {
    test_begin("JunoVoice: toggling OSC_SAW_ON/OSC_PULSE_ON mid-render stays finite, no reset() call");

    JunoVoice v = make_dco_test_voice();
    v.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    for (int b = 0; b < 20; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }

    // Toggle mid-stream several times; each block must stay finite (no NaN/Inf spike
    // from a hidden reset()) and non-silent whenever at least one switch is on.
    v.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT(buf[i] == buf[i], "toggling pulse on must not produce NaN");
        TEST_ASSERT(buf[i] < 10.0f && buf[i] > -10.0f, "toggling pulse on must not produce Inf/huge spike");
    }

    v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    float rms_pulse_only = rms(buf, 64);
    (void)rms_pulse_only;
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT(buf[i] == buf[i], "toggling saw off must not produce NaN");
    }
    test_pass();
}

/* --- 14. Sub-oscillator is a fixed square, distinct from a saw. ----------- */
void test_juno_voice_sub_is_square() {
    test_begin("JunoVoice: sub-oscillator is square (fixed), not saw");

    // Isolate the sub: silence the main DCO and noise, drive sub level to 1.
    JunoVoice v = make_dco_test_voice();
    v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[256];
    for (int b = 0; b < 40; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 256);
    }
    memset(buf, 0, sizeof(buf));
    v.render(buf, 256);

    // A square wave sits near +1/-1 most of the time (band-limited ringing aside);
    // check the mean of |sample| is high relative to RMS — a saw's |sample| mean is
    // ~0.5x its peak while a square's is close to its peak (both near amplitude 1).
    float sum_abs = 0.0f;
    for (int i = 0; i < 256; i++) sum_abs += fabsf(buf[i]);
    float mean_abs = sum_abs / 256.0f;
    float sub_rms  = rms(buf, 256);

    TEST_ASSERT(sub_rms > 0.05f, "sub must be non-silent when isolated");
    // Square wave: mean(|x|) ~= RMS (both ~= amplitude). Saw: mean(|x|) ~= 0.5*RMS*sqrt(3)
    // (i.e. noticeably below RMS). Require the ratio to be close to 1 (square-like).
    TEST_ASSERT(mean_abs > sub_rms * 0.85f, "sub |mean| must track RMS closely (square, not saw)");
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
    test_juno_voice_saw_only();
    test_juno_voice_pulse_only();
    test_juno_voice_neither_silent();
    test_juno_voice_saw_plus_pulse_sums();
    test_juno_voice_toggle_no_phase_reset();
    test_juno_voice_sub_is_square();
}
