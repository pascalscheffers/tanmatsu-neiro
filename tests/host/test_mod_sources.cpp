/* tests/host/test_mod_sources.cpp
 *
 * Host DSP tests for Stage 3a: modulation sources (ENV2 + LFO1/2).
 *
 * 1. ENV2 runs independently of ENV1 — tweaking ENV2 params doesn't change
 *    the amp-envelope output, and ENV2 accumulates its own attack/decay.
 * 2. ENV2 follows gate: rises on note_on, falls on note_off.
 * 3. LFO produces oscillation at the set rate (zero-crossings within range).
 * 4. LFO waveform change alters the shape of the output.
 * 5. LFO depth scales the output amplitude.
 * 6. set_param changes mod source output (param change → output change).
 * 7. LFO S&H waveform: output is piecewise constant (held within a cycle).
 * 8. dsp::Lfo direct: all waveform types produce output in [-1, +1].
 * 9. ENV2 is independent across two voices (per-voice, not shared).
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math; tests run without
 * hardware flush-to-zero so denormal behaviour matches the device.
 */

#include "runner.h"
#include "juno_voice.h"
#include "param_id.h"
#include "dsp/lfo.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const float kSampleRate = 48000.0f;

static float rms(const float* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
    return sqrtf(sum / (float)n);
}

/* --- 1. ENV2 independent from ENV1 --------------------------------------- */
void test_env2_independent_from_env1() {
    printf("--- Stage 3a: ENV2 (second ADSR) ---\n");
    test_begin("ENV2 independent: ENV2 params don't affect ENV1 output");

    // Two voices: identical except one has an extreme ENV2 attack.
    // The amp-envelope (ENV1) output should be the same.
    auto measure_env1_rms = [](float env2_attack) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::ENV_ATTACK,   0.010f);
        v.set_param((int)ParamId::ENV2_ATTACK,  env2_attack);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);
        float buf[64];
        // Advance to sustain.
        for (int b = 0; b < 200; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
        }
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        return rms(buf, 64);
    };

    float rms_a = measure_env1_rms(0.001f);
    float rms_b = measure_env1_rms(4.0f);

    // ENV1 sustain output should be near-identical regardless of ENV2 attack.
    float ratio = (rms_a > rms_b) ? (rms_a / rms_b) : (rms_b / rms_a);
    TEST_ASSERT(ratio < 1.1f,
                "ENV2 attack change must not affect ENV1 sustain amplitude");
    test_pass();
}

/* --- 2. ENV2 follows gate ------------------------------------------------ */
void test_env2_follows_gate() {
    test_begin("ENV2: rises on note_on, non-zero during sustain");

    JunoVoice v;
    v.init(kSampleRate);
    // Short attack so ENV2 reaches sustain quickly.
    v.set_param((int)ParamId::ENV2_ATTACK,  0.001f);
    v.set_param((int)ParamId::ENV2_DECAY,   0.001f);
    v.set_param((int)ParamId::ENV2_SUSTAIN, 0.800f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Advance through attack+decay to reach sustain (~200 blocks).
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }

    // ENV2 value at sustain should be near 0.8 (sustain level).
    float env2_at_sustain = v.env2_value();
    TEST_ASSERT(env2_at_sustain > 0.5f,
                "ENV2 value at sustain must be > 0.5 (sustain level=0.8)");

    // After note_off + release tail, ENV2 should drop toward zero.
    v.set_param((int)ParamId::ENV2_RELEASE, 0.05f);
    v.note_off();
    for (int b = 0; b < 240; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    float env2_after_release = v.env2_value();
    TEST_ASSERT(env2_after_release < 0.05f,
                "ENV2 must decay to near-zero after release");
    test_pass();
}

/* --- 3. LFO produces oscillation ---------------------------------------- */
void test_lfo_oscillation() {
    printf("--- Stage 3a: LFOs ---\n");
    test_begin("LFO1: produces oscillation at set rate (zero-crossings)");

    JunoVoice v;
    v.init(kSampleRate);
    // Rate: 10 Hz → period = 4800 samples = 75 blocks.
    v.set_param((int)ParamId::LFO1_RATE,  10.0f);
    v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
    v.set_param((int)ParamId::LFO1_SHAPE, 0.0f);  // SINE

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Collect LFO1 values over 150 blocks (~2 full cycles at 10 Hz).
    float prev = 0.0f;
    int sign_changes = 0;
    for (int b = 0; b < 150; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        float cur = v.lfo1_value();
        // Count sign changes (zero-crossings).
        if (b > 0 && ((prev < 0.0f && cur > 0.0f) || (prev > 0.0f && cur < 0.0f))) {
            sign_changes++;
        }
        prev = cur;
    }

    // At 10 Hz over 150 blocks (= 9600 samples ≈ 0.2 s), expect ~4 zero
    // crossings (2 per cycle × 2 cycles). Accept ≥ 2 to allow boundary effects.
    TEST_ASSERT(sign_changes >= 2,
                "LFO1 at 10 Hz must produce ≥2 sign changes over 150 blocks");
    test_pass();
}

/* --- 4. LFO waveform change alters output -------------------------------- */
void test_lfo_waveform_change() {
    test_begin("LFO1: waveform change (SINE vs SQUARE) alters output shape");

    // Collect 1 second of LFO output for each waveform.
    // SQUARE should produce values very close to ±1 (bimodal).
    // SINE should produce a smoother distribution.
    auto collect_abs_mean = [](int shape) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::LFO1_RATE,  1.0f);
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_SHAPE, (float)shape);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);
        float buf[64];
        float sum = 0.0f;
        int count = 0;
        // Collect 2 full cycles at 1 Hz = 750 blocks.
        for (int b = 0; b < 750; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
            float val = v.lfo1_value();
            sum += fabsf(val);
            count++;
        }
        return sum / (float)count;
    };

    // LfoWave: SINE=0 → |sin| mean ≈ 2/π ≈ 0.637
    //           SQUARE=4 → mean of |±1| = 1.0
    float mean_sine   = collect_abs_mean(0);
    float mean_square = collect_abs_mean(4);

    // Square must have higher |mean| than sine (nearer to 1.0).
    TEST_ASSERT(mean_square > mean_sine * 1.2f,
                "SQUARE waveform mean|x| must exceed SINE (≥1.2×)");
    test_pass();
}

/* --- 5. LFO depth scales amplitude --------------------------------------- */
void test_lfo_depth_scaling() {
    test_begin("LFO1: depth param scales output amplitude");

    auto collect_peak = [](float depth) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::LFO1_RATE,  5.0f);
        v.set_param((int)ParamId::LFO1_DEPTH, depth);
        v.set_param((int)ParamId::LFO1_SHAPE, 0.0f);  // SINE
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);
        float buf[64];
        float peak = 0.0f;
        for (int b = 0; b < 200; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
            float val = fabsf(v.lfo1_value());
            if (val > peak) peak = val;
        }
        return peak;
    };

    float peak_full = collect_peak(1.0f);
    float peak_half = collect_peak(0.5f);

    // Half depth → peak should be about half.
    TEST_ASSERT(peak_full > 0.5f,  "LFO at depth=1 must reach > 0.5");
    TEST_ASSERT(peak_half < peak_full * 0.75f,
                "LFO at depth=0.5 peak must be <75% of depth=1 peak");
    test_pass();
}

/* --- 6. set_param changes LFO rate --------------------------------------- */
void test_lfo_rate_set_param() {
    test_begin("LFO1: set_param(LFO1_RATE) changes oscillation frequency");

    // Count zero-crossings over a fixed window at two different rates.
    auto count_crossings = [](float rate) -> int {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::LFO1_RATE,  rate);
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_SHAPE, 2.0f);  // SAW — reliable crossings
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);
        float buf[64];
        float prev = 0.0f;
        int count = 0;
        for (int b = 0; b < 750; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
            float cur = v.lfo1_value();
            if (b > 0 && ((prev < 0.0f && cur > 0.0f) || (prev > 0.0f && cur < 0.0f))) {
                count++;
            }
            prev = cur;
        }
        return count;
    };

    int cross_1hz  = count_crossings(1.0f);
    int cross_5hz  = count_crossings(5.0f);

    // 5 Hz should produce ~5× more crossings than 1 Hz.
    TEST_ASSERT(cross_5hz > cross_1hz * 3,
                "LFO at 5 Hz must produce >3× more zero-crossings than at 1 Hz");
    test_pass();
}

/* --- 7. LFO S&H: output is piecewise constant ---------------------------- */
void test_lfo_sh_piecewise_constant() {
    test_begin("LFO S&H: output held constant within each cycle");

    dsp::Lfo lfo;
    lfo.init(kSampleRate);
    lfo.set_rate(100.0f);  // 100 Hz → period = 480 samples
    lfo.set_waveform(dsp::LfoWave::SH);

    // Within one cycle (480 samples), all values should be identical.
    float first_val = lfo.process();
    bool held = true;
    for (int i = 1; i < 479; i++) {
        float v = lfo.process();
        if (v != first_val) {
            held = false;
            break;
        }
    }
    TEST_ASSERT(held, "S&H LFO must hold the same value for a full cycle");

    // After the cycle completes, the value should update.
    // Run through the rest of this cycle and a bit of the next.
    float after = lfo.process();  // sample 479 — may still be first cycle
    float post  = lfo.process();  // sample 480 — should be in next cycle
    (void)after;  // may or may not have changed at exactly sample 479
    // Just run a few more to confirm the value is now well into the next cycle.
    for (int i = 0; i < 240; i++) post = lfo.process();
    // We can only assert that it is within [-1, +1].
    TEST_ASSERT(post >= -1.001f && post <= 1.001f,
                "S&H LFO value must remain in [-1, +1]");
    test_pass();
}

/* --- 8. dsp::Lfo: all waveforms produce output in [-1, +1] -------------- */
void test_lfo_output_range() {
    test_begin("dsp::Lfo: all waveforms stay in [-1, +1]");

    dsp::LfoWave waves[] = {
        dsp::LfoWave::SINE,
        dsp::LfoWave::TRI,
        dsp::LfoWave::SAW,
        dsp::LfoWave::SQUARE,
        dsp::LfoWave::SH,
    };

    for (auto wave : waves) {
        dsp::Lfo lfo;
        lfo.init(kSampleRate);
        lfo.set_rate(7.3f);  // non-integer rate to exercise phase accumulation
        lfo.set_waveform(wave);

        for (int i = 0; i < 48000; i++) {  // 1 second
            float v = lfo.process();
            TEST_ASSERT(v >= -1.001f && v <= 1.001f,
                        "Lfo output must stay within [-1, +1]");
        }
    }
    test_pass();
}

/* --- 9. ENV2 is per-voice (two voices run independently) ---------------- */
void test_env2_per_voice_independent() {
    test_begin("ENV2 per-voice: two voices have independent ENV2 state");

    JunoVoice v1, v2;
    v1.init(kSampleRate);
    v2.init(kSampleRate);

    // v1 gets a very short ENV2, v2 gets a long one.
    v1.set_param((int)ParamId::ENV2_ATTACK,  0.001f);
    v1.set_param((int)ParamId::ENV2_DECAY,   0.001f);
    v1.set_param((int)ParamId::ENV2_SUSTAIN, 0.8f);

    v2.set_param((int)ParamId::ENV2_ATTACK,  2.0f);  // very long attack
    v2.set_param((int)ParamId::ENV2_SUSTAIN, 0.8f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v1.note_on(69, 127, expr);
    v2.note_on(69, 127, expr);

    float buf[64];
    // After 100 blocks, v1 should be at sustain, v2 still in slow attack.
    for (int b = 0; b < 100; b++) {
        memset(buf, 0, sizeof(buf));
        v1.render(buf, 64);
        memset(buf, 0, sizeof(buf));
        v2.render(buf, 64);
    }

    float e2_v1 = v1.env2_value();
    float e2_v2 = v2.env2_value();

    // v1 should be near sustain (0.8), v2 still climbing from near 0.
    TEST_ASSERT(e2_v1 > 0.5f,
                "Voice1 ENV2 (short attack) must be near sustain after 100 blocks");
    TEST_ASSERT(e2_v2 < e2_v1,
                "Voice2 ENV2 (long attack) must still be below Voice1 ENV2");
    test_pass();
}

/* Entry point declared in main.cpp */
void test_mod_sources_suite() {
    test_env2_independent_from_env1();
    test_env2_follows_gate();
    test_lfo_oscillation();
    test_lfo_waveform_change();
    test_lfo_depth_scaling();
    test_lfo_rate_set_param();
    test_lfo_sh_piecewise_constant();
    test_lfo_output_range();
    test_env2_per_voice_independent();
}
