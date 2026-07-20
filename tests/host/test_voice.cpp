/* tests/host/test_voice.cpp
 *
 * Host DSP tests for JunoVoice.
 *
 * 1. ADSR shape — output rises through attack, holds at sustain.
 * 2. Silent after release — after note_off + release time, output ≈ 0
 *    and is_active() returns false.
 * 3. reset() silences — output is immediately zero after reset().
 * 4. J106 VCF LP — lower cutoff attenuates voice output more.
 * 8. HPF signal order + four positions (WO-13e-ii) — with the VCF forced
 *    wide open/no-res (near-transparent), the voice's output must show the
 *    dsp::Juno106Hpf block's own per-position shaping, proving the HPF is
 *    live in the per-voice chain ahead of the VCF (a transparent VCF can't
 *    be masking or replacing the HPF's effect).
 * 9. HPF position switch stays bounded — switching position mid-note on a
 *    running voice produces no non-finite samples and no unbounded blow-up.
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math; tests run without
 * hardware flush-to-zero so denormal behaviour matches the device.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp/juno106_hpf.h"
#include "dsp/vendor/kr106/KR106VCA.h"
#include "juno_voice.h"
#include "param_id.h"
#include "runner.h"

static const float kSampleRate = 48000.0f;

static float rms(const float* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
    return sqrtf(sum / (float)n);
}

/* --- 1. ADSR shape -------------------------------------------------------- */
void test_voice_adsr_shape() {
    printf("--- JunoVoice ADSR shape ---\n");
    test_begin("ADSR: attack ramps from near-zero");

    JunoVoice v;
    v.init(kSampleRate);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);  // A4, full velocity

    float buf[64];

    // The public 10 ms attack maps to the nearest firmware slider step.
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    float rms_early = rms(buf, 64);

    // Render 200 more blocks (~0.27 s) to reach sustain.
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    float rms_sustain = rms(buf, 64);

    TEST_ASSERT(rms_early > 0.001f, "attack: first block must be non-zero");
    TEST_ASSERT(rms_sustain > 0.01f, "sustain: voice must produce output");
    TEST_ASSERT(rms_sustain > rms_early * 1.8f, "sustain RMS must exceed early-attack RMS (ramp verified)");
    test_pass();
}

/* --- 2. Silent after release ---------------------------------------------- */
void test_voice_silent_after_release() {
    test_begin("ADSR: silent after note_off + release");

    JunoVoice v;
    v.init(kSampleRate);

    // Short release for a deterministic test: 0.05 s.
    v.set_param((int)ParamId::ENV_RELEASE, 0.05f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Reach sustain (~200 blocks).
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }

    v.note_off();

    // Run at most two seconds through the firmware-timed release tail.
    for (int b = 0; b < 1500 && v.is_active(); b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }

    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    float rms_after = rms(buf, 64);

    TEST_ASSERT(rms_after < 0.001f, "voice must be near-silent after full release");
    TEST_ASSERT(!v.is_active(), "is_active() must return false after release completes");
    test_pass();
}

/* --- 3. reset() silences immediately -------------------------------------- */
void test_voice_reset_silences() {
    test_begin("reset() silences voice immediately");

    JunoVoice v;
    v.init(kSampleRate);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Reach sustain.
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }

    // Confirm there is output.
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    TEST_ASSERT(rms(buf, 64) > 0.01f, "must have output before reset");

    v.reset();

    // render() must leave the buffer untouched (early-exit path).
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    TEST_ASSERT(rms(buf, 64) < 0.0001f, "output must be zero immediately after reset()");
    TEST_ASSERT(!v.is_active(), "is_active() must return false after reset()");
    test_pass();
}

void test_voice_unrendered_note_has_no_tail() {
    test_begin("note_on/note_off between audio blocks leaves no amp tail");

    JunoVoice v;
    v.init(kSampleRate);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);
    v.note_off();
    TEST_ASSERT(!v.is_active(), "an unrendered note must finish silently on note_off");
    test_pass();
}

/* --- 4. set_param via ParamId — zero levels → silence ------------------- */
void test_voice_set_param_zero_levels() {
    printf("--- JunoVoice set_param (Stage 2b) ---\n");
    test_begin("set_param: zero all mix levels silences output");

    // Set all mix levels to 0 BEFORE note_on so the filter input is always 0;
    // that way the SVF accumulates no energy and the output stays near-zero
    // throughout the sustain phase (tests the live set_param path).
    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::OSC_LEVEL, 0.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Advance to sustain — filter input has always been 0, so no energy stored.
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    TEST_ASSERT(rms(buf, 64) < 0.0001f, "all mix levels 0 → near-silent output");
    test_pass();
}

/* --- 5. set_param via ParamId — cutoff change affects output RMS -------- */
void test_voice_set_param_cutoff() {
    test_begin("set_param: low cutoff attenuates output vs high cutoff");

    // Measure RMS at sustain with different cutoff values.
    // Sub and noise off so we only hear the saw; low cutoff attenuates even
    // the fundamental (A4 = 440 Hz) when the cutoff is below 100 Hz.
    auto measure_rms = [](float cutoff) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
        v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
        v.set_param((int)ParamId::FILTER_CUTOFF, cutoff);
        v.set_param((int)ParamId::FILTER_RES, 0.0f);
        // Disable new panel mods so only the base cutoff is tested.
        v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
        v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
        v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);  // A4 = 440 Hz
        float buf[64];
        for (int b = 0; b < 200; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
        }
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        return rms(buf, 64);
    };

    float rms_low  = measure_rms(80.0f);     // well below 440 Hz — strong LP attenuation
    float rms_high = measure_rms(10000.0f);  // above 440 Hz — fundamental passes cleanly

    TEST_ASSERT(rms_high > rms_low * 3.0f, "high cutoff (10kHz) must pass ≥3× more RMS than low cutoff (80Hz)");
    test_pass();
}

/* --- 6. Zero-sustain idle voice retriggers after note_off ---------------- */
void test_voice_zero_sustain_retrigger() {
    test_begin("zero-sustain idle voice retriggers after note_off");

    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::ENV_DECAY, 0.01f);
    v.set_param((int)ParamId::ENV_SUSTAIN, 0.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Fixed-duration render is well beyond attack + short decay. A held J106
    // envelope remains busy at zero sustain until it receives NoteOff.
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    TEST_ASSERT(v.is_active(), "held gate must keep an idle-envelope voice active");

    v.note_off();
    for (int b = 0; b < 8 && v.is_active(); ++b) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    TEST_ASSERT(!v.is_active(), "zero-level release must reach firmware idle promptly");

    v.note_on(69, 127, expr);
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    TEST_ASSERT(rms(buf, 64) > 0.001f, "reused zero-sustain voice must retrigger");
    test_pass();
}

/* --- KR-106 firmware envelope and measured VCA --------------------------- */
void test_voice_kr106_amp_components() {
    test_begin("KR-106 ADSR: tick timing, sustain, release, gate slew, and VCA curve");

    kr106::ADSR env;
    env.mModel = kr106::kJ106;
    env.SetSampleRate(kSampleRate);
    env.Set106Attack(1.0f / 127.0f);
    env.Set106Decay(0);
    env.SetSustain(0.5f);
    env.Set106Release(0);
    env.NoteOn();

    const uint16_t first_tick = env.mEnvInt;
    TEST_ASSERT(first_tick > 0 && first_tick < kr106::ADSR::kEnvMax, "note-on must apply one quantized attack tick");
    for (int i = 0; i < 100; ++i) env.Process();
    TEST_ASSERT(env.mEnvInt == first_tick, "integer attack must wait for the next firmware tick");
    for (int i = 0; i < 200; ++i) env.Process();
    TEST_ASSERT(env.mEnvInt > first_tick, "integer attack must advance at the next firmware tick");

    env.Set106Attack(0.0f);
    env.NoteOn();
    TEST_ASSERT(env.mState == kr106::ADSR::kDecay, "shortest attack must enter decay on the note-on tick");
    for (int i = 0; i < 1000; ++i) env.Process();
    TEST_ASSERT(env.mEnvInt == env.mSusInt, "fast decay must settle exactly at the quantized sustain level");

    env.NoteOff();
    for (int i = 0; i < 1000 && env.GetBusy(); ++i) env.Process();
    TEST_ASSERT(!env.GetBusy(), "fast release must terminate at firmware idle");

    kr106::ADSR gate_env;
    gate_env.mModel = kr106::kJ106;
    gate_env.SetSampleRate(kSampleRate);
    gate_env.Set106Attack(0.0f);
    gate_env.Set106Decay(0);
    gate_env.SetSustain(1.0f);
    gate_env.Set106Release(0);
    gate_env.NoteOn();
    gate_env.Process();
    TEST_ASSERT(gate_env.mGateEnv > 0.0f && gate_env.mGateEnv < 1.0f, "gate-mode attack edge must be smoothed");
    for (int i = 0; i < 31; ++i) gate_env.Process();
    TEST_ASSERT(fabsf(gate_env.mGateEnv - 1.0f) < 1e-6f, "gate-mode attack slew must reach unity in 32 samples");
    gate_env.NoteOff();
    gate_env.Process();
    TEST_ASSERT(gate_env.mGateEnv > 0.0f && gate_env.mGateEnv < 1.0f, "gate-mode release edge must be smoothed");

    TEST_ASSERT(kr106::VCAGainJ106(0.0f) == 0.0f, "measured VCA curve must be silent at zero envelope");
    TEST_ASSERT(fabsf(kr106::VCAGainJ106(1.0f) - 1.0f) < 1e-6f, "measured VCA curve must reach unity at full envelope");
    TEST_ASSERT(kr106::VCAGainJ106(0.05f) < 0.04f, "measured VCA curve must be nonlinear in its low-level region");

    kr106::ADSR longest;
    longest.mModel = kr106::kJ106;
    longest.SetSampleRate(kSampleRate);
    longest.Set106Attack(1.0f);
    longest.Set106Decay(127);
    longest.SetSustain(0.0f);
    longest.Set106Release(127);
    longest.NoteOn();
    for (int i = 0; i < (int)(4.0f * kSampleRate) && longest.mState == kr106::ADSR::kAttack; ++i) longest.Process();
    TEST_ASSERT(longest.mState == kr106::ADSR::kDecay, "longest attack slider must complete in bounded time");
    longest.NoteOff();
    for (int i = 0; i < (int)(30.0f * kSampleRate) && longest.GetBusy(); ++i) longest.Process();
    TEST_ASSERT(!longest.GetBusy(), "longest release slider must reach idle in bounded time");
    test_pass();
}

void test_voice_amp_public_extremes_finite() {
    test_begin("JunoVoice amp envelope: public 1 ms and 5 s A/D/R endpoints stay finite");

    const float endpoints[2] = {0.001f, 5.0f};
    for (float seconds : endpoints) {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::ENV_ATTACK, seconds);
        v.set_param((int)ParamId::ENV_DECAY, seconds);
        v.set_param((int)ParamId::ENV_RELEASE, seconds);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);
        float buf[64] = {};
        for (int block = 0; block < 64; ++block) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
            for (float sample : buf) TEST_ASSERT(isfinite(sample), "public A/D/R endpoint produced non-finite output");
        }
    }
    test_pass();
}

/* --- 7. HPF signal order + four positions (WO-13e-ii) --------------------
 * The VCF is forced wide open (cutoff at the param max, res 0, all panel
 * mods off) so it is near-transparent at these low test frequencies. What
 * reaches the output is then dominated by the per-voice HPF's own shaping —
 * proving the HPF is live in the voice's render path ahead of the VCF (a
 * bypassed-looking VCF can't be substituting for it). Note 36 (~65.4 Hz)
 * sits near the bass-boost corner (70 Hz) and well below both HPF corners
 * (225 Hz, 700 Hz), giving a clear separation across all four positions.
 */
static float measure_voice_hpf_rms(int hpf_position, uint8_t note) {
    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
    v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);  // wide open — near-transparent VCF
    v.set_param((int)ParamId::FILTER_RES, 0.0f);
    v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
    v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
    v.set_param((int)ParamId::HPF_CUTOFF, (float)hpf_position);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(note, 127, expr);

    float buf[64];
    for (int b = 0; b < 200; b++) {  // settle past attack
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    // Measure over a wider window (40 blocks = 2560 samples) so at least a
    // couple of full periods of the ~65 Hz test note are captured.
    float sum   = 0.0f;
    int   count = 0;
    for (int b = 0; b < 40; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        for (int i = 0; i < 64; i++) sum += buf[i] * buf[i];
        count += 64;
    }
    return sqrtf(sum / (float)count);
}

void test_voice_hpf_signal_order_and_positions() {
    test_begin("HPF signal order: four positions shape voice output ahead of a transparent VCF");

    const uint8_t note       = 36;  // ~65.4 Hz
    float         rms_bypass = measure_voice_hpf_rms(dsp::JUNO106_HPF_BYPASS, note);
    float         rms_boost  = measure_voice_hpf_rms(dsp::JUNO106_HPF_BASS_BOOST, note);
    float         rms_225    = measure_voice_hpf_rms(dsp::JUNO106_HPF_225HZ, note);
    float         rms_700    = measure_voice_hpf_rms(dsp::JUNO106_HPF_700HZ, note);

    TEST_ASSERT(rms_boost > rms_bypass * 1.02f, "bass-boost position must pass more RMS than bypass at ~65 Hz");
    TEST_ASSERT(rms_225 < rms_bypass * 0.6f, "225 Hz HPF must attenuate a ~65 Hz tone well below bypass");
    TEST_ASSERT(rms_700 < rms_225 * 0.6f, "700 Hz HPF must attenuate a ~65 Hz tone further than the 225 Hz position");
    test_pass();
}

/* --- 8. HPF position switch stays bounded through the voice path --------- */
void test_voice_hpf_switch_bounded() {
    test_begin("HPF position switch on a running voice stays bounded (no non-finite, no runaway)");

    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
    v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);
    v.set_param((int)ParamId::FILTER_RES, 0.0f);
    v.set_param((int)ParamId::HPF_CUTOFF, (float)dsp::JUNO106_HPF_BYPASS);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);  // A4

    float buf[64];
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    float pre_rms = rms(buf, 64);

    // Sweep through every position mid-note (each is a coefficient change on
    // a running filter — the switch itself must never blow up or NaN).
    const int positions[] = {(int)dsp::JUNO106_HPF_225HZ, (int)dsp::JUNO106_HPF_700HZ, (int)dsp::JUNO106_HPF_BASS_BOOST,
                             (int)dsp::JUNO106_HPF_BYPASS};
    float     peak        = pre_rms;
    for (int p : positions) {
        v.set_param((int)ParamId::HPF_CUTOFF, (float)p);
        for (int b = 0; b < 10; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
            for (int i = 0; i < 64; i++) {
                TEST_ASSERT(isfinite(buf[i]), "HPF position switch produced non-finite voice output");
                float a = fabsf(buf[i]);
                if (a > peak) peak = a;
            }
        }
    }
    // Bounded, not runaway: even the most extreme jump (bypass <-> bass boost)
    // shouldn't blow the settled level up by more than a generous safety margin.
    TEST_ASSERT(peak < pre_rms * 10.0f + 0.01f, "HPF position switch transient exceeded bounded margin");
    test_pass();
}

/* --- 9. KR-106 oscillator phase coherence -------------------------------- */
static void render_waveform(bool saw_on, bool pulse_on, float* out, int blocks) {
    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::OSC_SAW_ON, saw_on ? 1.0f : 0.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, pulse_on ? 1.0f : 0.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
    v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);
    v.set_param((int)ParamId::FILTER_RES, 0.0f);
    v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
    v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCA_GATE_MODE, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);
    for (int b = 0; b < blocks; ++b) v.render(out + b * 64, 64);
}

void test_voice_kr106_phase_coherence() {
    test_begin("KR-106 DCO: saw+pulse is reset-deterministic and distinct from either waveform");

    static constexpr int kBlocks                  = 8;
    float                combined_a[kBlocks * 64] = {};
    float                combined_b[kBlocks * 64] = {};
    float                saw[kBlocks * 64]        = {};
    float                pulse[kBlocks * 64]      = {};
    render_waveform(true, true, combined_a, kBlocks);
    render_waveform(true, true, combined_b, kBlocks);
    render_waveform(true, false, saw, kBlocks);
    render_waveform(false, true, pulse, kBlocks);

    float deterministic_error = 0.0f;
    float saw_difference      = 0.0f;
    float pulse_difference    = 0.0f;
    for (int i = 0; i < kBlocks * 64; ++i) {
        deterministic_error += fabsf(combined_a[i] - combined_b[i]);
        saw_difference      += fabsf(combined_a[i] - saw[i]);
        pulse_difference    += fabsf(combined_a[i] - pulse[i]);
    }
    TEST_ASSERT(deterministic_error < 1e-6f, "combined DCO rendering must be deterministic after reset/init");
    TEST_ASSERT(saw_difference > 0.1f, "coherent saw+pulse must differ from saw alone");
    TEST_ASSERT(pulse_difference > 0.1f, "coherent saw+pulse must differ from pulse alone");
    test_pass();
}

/* --- 10. KR-106 VCF high-resonance bounded sweep ------------------------- */
void test_voice_kr106_vcf_high_res_sweep() {
    test_begin("KR-106 J106 VCF: 1x high-resonance cutoff sweep stays finite and bounded");

    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
    v.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 1.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 1.0f);
    v.set_param((int)ParamId::FILTER_RES, 1.0f);
    v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
    v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCA_GATE_MODE, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    float peak = 0.0f;
    for (int block = 0; block < 512; ++block) {
        float sweep = (float)(block % 256) / 255.0f;
        if ((block / 256) != 0) sweep = 1.0f - sweep;
        v.set_param((int)ParamId::FILTER_CUTOFF, 20.0f + sweep * 19980.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        for (float sample : buf) {
            TEST_ASSERT(isfinite(sample), "high-resonance VCF sweep produced non-finite output");
            float magnitude = fabsf(sample);
            if (magnitude > peak) peak = magnitude;
        }
    }
    TEST_ASSERT(peak < 128.0f, "high-resonance VCF sweep exceeded bounded-output margin");
    test_pass();
}

/* Entry points declared in main.cpp */
void test_voice_suite() {
    test_voice_adsr_shape();
    test_voice_silent_after_release();
    test_voice_reset_silences();
    test_voice_unrendered_note_has_no_tail();
    test_voice_set_param_zero_levels();
    test_voice_set_param_cutoff();
    test_voice_zero_sustain_retrigger();
    test_voice_kr106_amp_components();
    test_voice_amp_public_extremes_finite();
    test_voice_hpf_signal_order_and_positions();
    test_voice_hpf_switch_bounded();
    test_voice_kr106_phase_coherence();
    test_voice_kr106_vcf_high_res_sweep();
}
