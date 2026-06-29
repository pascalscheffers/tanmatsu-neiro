/* tests/host/test_voice.cpp
 *
 * Host DSP tests for Stage 1b: JunoVoice and dsp::Filter.
 *
 * 1. ADSR shape — output rises through attack, holds at sustain.
 * 2. Silent after release — after note_off + release time, output ≈ 0
 *    and is_active() returns false.
 * 3. reset() silences — output is immediately zero after reset().
 * 4. SVF LP filter — lower cutoff attenuates a high-frequency signal more.
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math; tests run without
 * hardware flush-to-zero so denormal behaviour matches the device.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp/filter.h"
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

    // First block (0-64 samples): attack is 0.01 s = 480 samples, so we're
    // at the very start of the ramp — output should be small but rising.
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
    TEST_ASSERT(rms_sustain > rms_early * 2.0f, "sustain RMS must exceed early-attack RMS (ramp verified)");
    test_pass();
}

/* --- 2. Silent after release ---------------------------------------------- */
void test_voice_silent_after_release() {
    test_begin("ADSR: silent after note_off + release");

    JunoVoice v;
    v.init(kSampleRate);

    // Short release for a deterministic test: 0.05 s.
    // DaisySP ADSR uses an RC time constant; the envelope reaches ~0.014× of
    // sustain after ~4.26 × time_constant samples, which for 0.05 s at 48 kHz
    // is ~10 240 samples (~160 blocks).  Run 220 blocks to be safe.
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

    // Run 220 blocks (14 080 samples) through the release tail.
    for (int b = 0; b < 220; b++) {
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

/* --- 4. SVF LP filter attenuates high frequencies ------------------------ */
void test_filter_lp_attenuation() {
    printf("--- dsp::Filter SVF LP ---\n");
    test_begin("SVF LP: low cutoff attenuates high-freq more than high cutoff");

    // Drive both filters with an alternating ±0.5 signal (Nyquist frequency —
    // worst case for LP: maximum attenuation should be greatest here).
    // Measure steady-state RMS after a settling period.

    auto measure_lp_rms = [](float cutoff) -> float {
        dsp::Filter f;
        f.init(kSampleRate);
        f.set_freq(cutoff);
        f.set_res(0.0f);

        float sum    = 0.0f;
        int   settle = 2048, measure = 4096;
        for (int i = 0; i < settle; i++) {
            float in = (i & 1) ? 0.5f : -0.5f;
            f.process(in);
            (void)f.output();
        }
        for (int i = 0; i < measure; i++) {
            float in = (i & 1) ? 0.5f : -0.5f;
            f.process(in);
            float out = f.output();
            sum      += out * out;
        }
        return sqrtf(sum / (float)measure);
    };

    float rms_low  = measure_lp_rms(200.0f);
    float rms_high = measure_lp_rms(10000.0f);

    // LP at 10 kHz passes much more Nyquist-rate energy than LP at 200 Hz.
    TEST_ASSERT(rms_high > rms_low * 5.0f, "SVF LP: cutoff=10kHz must pass ≥5× more Nyquist energy than 200Hz");
    test_pass();
}

/* --- 5. set_param via ParamId — zero levels → silence ------------------- */
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

/* --- 6. set_param via ParamId — cutoff change affects output RMS -------- */
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

/* Entry points declared in main.cpp */
void test_voice_suite() {
    test_voice_adsr_shape();
    test_voice_silent_after_release();
    test_voice_reset_silences();
    test_filter_lp_attenuation();
    test_voice_set_param_zero_levels();
    test_voice_set_param_cutoff();
}
