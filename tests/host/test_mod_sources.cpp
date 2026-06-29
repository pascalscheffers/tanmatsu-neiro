/* tests/host/test_mod_sources.cpp
 *
 * Host DSP tests for Stage 3a: modulation sources (ENV2 + LFO1/2).
 *
 * 1. ENV2 runs independently of ENV1 — tweaking ENV2 params doesn't change
 *    the amp-envelope output, and ENV2 accumulates its own attack/decay.
 * 2. ENV2 follows gate: rises on note_on, falls on note_off.
 * 3. LFO injection path: injected raw value is scaled by depth (no delay).
 * 4. LFO depth: depth param scales the applied LFO output amplitude.
 * 5. LFO delay fade-in: lfo1_value_ ramps 0→1 over the delay window.
 * 6. LFO determinism: two voices fed identical set_lfo_inputs() produce
 *    identical lfo1_value() (structural fix for the stale-phase bug, ADR 0018).
 * 7. LFO S&H waveform: output is piecewise constant (held within a cycle).
 * 8. dsp::Lfo direct: all waveform types produce output in [-1, +1].
 * 9. ENV2 is independent across two voices (per-voice, not shared).
 *
 * Tests 3-6 reworked for ADR 0018 (shared engine LFO, per-voice injection).
 * The old oscillation / waveform / rate tests were voice-level and assumed the
 * voice owns the oscillator — that contract is now in synth.cpp + dsp::Lfo.
 * The pure dsp::Lfo unit tests (7, 8, process_block) remain unchanged.
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math; tests run without
 * hardware flush-to-zero so denormal behaviour matches the device.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp/lfo.h"
#include "juno_voice.h"
#include "param_id.h"
#include "runner.h"

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
        v.set_param((int)ParamId::ENV_ATTACK, 0.010f);
        v.set_param((int)ParamId::ENV2_ATTACK, env2_attack);
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
    TEST_ASSERT(ratio < 1.1f, "ENV2 attack change must not affect ENV1 sustain amplitude");
    test_pass();
}

/* --- 2. ENV2 follows gate ------------------------------------------------ */
void test_env2_follows_gate() {
    test_begin("ENV2: rises on note_on, non-zero during sustain");

    JunoVoice v;
    v.init(kSampleRate);
    // Short attack so ENV2 reaches sustain quickly.
    v.set_param((int)ParamId::ENV2_ATTACK, 0.001f);
    v.set_param((int)ParamId::ENV2_DECAY, 0.001f);
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
    TEST_ASSERT(env2_at_sustain > 0.5f, "ENV2 value at sustain must be > 0.5 (sustain level=0.8)");

    // After note_off + release tail, ENV2 should drop toward zero.
    v.set_param((int)ParamId::ENV2_RELEASE, 0.05f);
    v.note_off();
    for (int b = 0; b < 240; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    float env2_after_release = v.env2_value();
    TEST_ASSERT(env2_after_release < 0.05f, "ENV2 must decay to near-zero after release");
    test_pass();
}

/* --- 3. LFO injection: injected raw value is depth-scaled ---------------- */
void test_lfo_injection_depth() {
    printf("--- Stage 3a: LFOs (ADR 0018: shared-LFO injection path) ---\n");
    test_begin("LFO1: set_lfo_inputs(raw) → lfo1_value() ≈ raw * depth (no delay)");

    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::LFO1_DEPTH, 0.75f);
    v.set_param((int)ParamId::LFO1_DELAY, 0.0f);  // no delay → scale = 1.0

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Test with a positive and a negative raw value.
    float raw_vals[] = {0.8f, -0.5f, 1.0f, 0.0f};
    for (int i = 0; i < 4; i++) {
        float raw = raw_vals[i];
        v.set_lfo_inputs(raw, 0.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        float expected = raw * 0.75f;
        float got      = v.lfo1_value();
        float err      = fabsf(got - expected);
        TEST_ASSERT(err < 1e-5f, "lfo1_value() must equal raw * depth within 1e-5");
    }
    test_pass();
}

/* --- 4. LFO depth param scales the injected output ----------------------- */
void test_lfo_depth_scaling() {
    test_begin("LFO1: LFO1_DEPTH param scales set_lfo_inputs output");

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    float          buf[64];

    auto measure_value = [&](float depth, float raw) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::LFO1_DEPTH, depth);
        v.set_param((int)ParamId::LFO1_DELAY, 0.0f);
        v.note_on(69, 127, expr);
        v.set_lfo_inputs(raw, 0.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        return v.lfo1_value();
    };

    float raw      = 1.0f;
    float val_full = measure_value(1.0f, raw);
    float val_half = measure_value(0.5f, raw);

    TEST_ASSERT(fabsf(val_full - 1.0f) < 1e-5f, "depth=1.0, raw=1.0 → lfo1_value() must be ~1.0");
    TEST_ASSERT(fabsf(val_half - 0.5f) < 1e-5f, "depth=0.5, raw=1.0 → lfo1_value() must be ~0.5");
    test_pass();
}

/* --- 5. LFO delay fade-in: value ramps 0→full over delay window ---------- */
void test_lfo_delay_fade_in() {
    test_begin("LFO1: delay param causes lfo1_value() to ramp from 0→depth over delay");

    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
    // 1-second delay → 48000 samples = 750 blocks of 64.
    v.set_param((int)ParamId::LFO1_DELAY, 1.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    // Inject raw=1.0 each block; at start scale=0, at end scale=1.
    float first_val = 0.0f;
    float late_val  = 0.0f;

    // First block: delay scale should be near 0 (just started).
    v.set_lfo_inputs(1.0f, 0.0f);
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    first_val = v.lfo1_value();

    // Advance most of the delay window (740 blocks out of 750).
    for (int b = 1; b < 740; b++) {
        v.set_lfo_inputs(1.0f, 0.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    float mid_val = v.lfo1_value();

    // After the full delay (run beyond 750 blocks).
    for (int b = 740; b < 800; b++) {
        v.set_lfo_inputs(1.0f, 0.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    late_val = v.lfo1_value();

    // At the very start the scale must be near zero.
    TEST_ASSERT(first_val < 0.02f, "LFO1 with delay=1s must start near 0");
    // Mid-way (740/750) must be meaningfully higher than start but not full.
    TEST_ASSERT(mid_val > first_val, "LFO1 delay scale must increase over time");
    // After the delay window the value must be at full depth (raw=1, depth=1).
    TEST_ASSERT(late_val > 0.99f, "LFO1 delay must reach full depth after 1s");
    test_pass();
}

/* --- 6. LFO determinism: two voices fed same inputs → identical values --- */
void test_lfo_voice_determinism() {
    test_begin("LFO1 determinism: two voices with same inputs produce identical lfo1_value()");

    JunoVoice v1, v2;
    v1.init(kSampleRate);
    v2.init(kSampleRate);

    // Identical params.
    for (auto* v : {&v1, &v2}) {
        v->set_param((int)ParamId::LFO1_DEPTH, 0.8f);
        v->set_param((int)ParamId::LFO1_DELAY, 0.0f);
    }

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v1.note_on(69, 127, expr);
    v2.note_on(69, 127, expr);

    float    buf1[64], buf2[64];
    // Feed both voices an identical sequence of LFO raw values (simulating
    // what synth_render does with s_lfo1.process_block each block).
    dsp::Lfo shared_lfo;
    shared_lfo.init(kSampleRate);
    shared_lfo.set_rate(3.0f);
    shared_lfo.set_waveform(dsp::LfoWave::SINE);

    for (int b = 0; b < 200; b++) {
        float raw = shared_lfo.process_block(64);
        v1.set_lfo_inputs(raw, 0.0f);
        v2.set_lfo_inputs(raw, 0.0f);
        memset(buf1, 0, sizeof(buf1));
        memset(buf2, 0, sizeof(buf2));
        v1.render(buf1, 64);
        v2.render(buf2, 64);
        // lfo1_value() must be identical (no per-voice phase divergence).
        float diff = fabsf(v1.lfo1_value() - v2.lfo1_value());
        TEST_ASSERT(diff < 1e-6f, "Two voices fed same set_lfo_inputs must have identical lfo1_value()");
    }
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
    bool  held      = true;
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
    (void)after;                  // may or may not have changed at exactly sample 479
    // Just run a few more to confirm the value is now well into the next cycle.
    for (int i = 0; i < 240; i++) post = lfo.process();
    // We can only assert that it is within [-1, +1].
    TEST_ASSERT(post >= -1.001f && post <= 1.001f, "S&H LFO value must remain in [-1, +1]");
    test_pass();
}

/* --- 8. dsp::Lfo: all waveforms produce output in [-1, +1] -------------- */
void test_lfo_output_range() {
    test_begin("dsp::Lfo: all waveforms stay in [-1, +1]");

    dsp::LfoWave waves[] = {
        dsp::LfoWave::SINE, dsp::LfoWave::TRI, dsp::LfoWave::SAW, dsp::LfoWave::SQUARE, dsp::LfoWave::SH,
    };

    for (auto wave : waves) {
        dsp::Lfo lfo;
        lfo.init(kSampleRate);
        lfo.set_rate(7.3f);  // non-integer rate to exercise phase accumulation
        lfo.set_waveform(wave);

        for (int i = 0; i < 48000; i++) {  // 1 second
            float v = lfo.process();
            TEST_ASSERT(v >= -1.001f && v <= 1.001f, "Lfo output must stay within [-1, +1]");
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
    v1.set_param((int)ParamId::ENV2_ATTACK, 0.001f);
    v1.set_param((int)ParamId::ENV2_DECAY, 0.001f);
    v1.set_param((int)ParamId::ENV2_SUSTAIN, 0.8f);

    v2.set_param((int)ParamId::ENV2_ATTACK, 2.0f);  // very long attack
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
    TEST_ASSERT(e2_v1 > 0.5f, "Voice1 ENV2 (short attack) must be near sustain after 100 blocks");
    TEST_ASSERT(e2_v2 < e2_v1, "Voice2 ENV2 (long attack) must still be below Voice1 ENV2");
    test_pass();
}

/* --- 10. process_block(n) advances phase by exactly n steps (SINE) ---------- */
void test_lfo_process_block() {
    test_begin("dsp::Lfo: process_block(n) advances phase by n steps, output matches");

    const uint32_t N = 64;

    // process() returns compute(phase_before_advance), then advances phase.
    // process_block(N) advances phase by N, then returns compute(phase_after_advance).
    // So process_block(N) is equivalent to calling process() N+1 times and using
    // only the LAST call — or equivalently, calling process() N times and calling
    // process() one more time (which advances phase to the same point as process_block
    // but also samples there).
    //
    // Verify: after N calls to process(), calling process() once more gives the
    // same value as process_block(N) did (both sample at phase = N * phase_inc).

    dsp::Lfo lfo_ref;
    lfo_ref.init(kSampleRate);
    lfo_ref.set_rate(5.3f);
    lfo_ref.set_waveform(dsp::LfoWave::SINE);
    // Run N per-sample steps, discard outputs (phase advances to N * phase_inc).
    for (uint32_t i = 0; i < N; i++) lfo_ref.process();
    // One more process() samples at phase N * phase_inc (before its own advance).
    float ref_val = lfo_ref.process();

    // Block path: process_block(N) advances phase by N then samples at that phase.
    dsp::Lfo lfo_block;
    lfo_block.init(kSampleRate);
    lfo_block.set_rate(5.3f);
    lfo_block.set_waveform(dsp::LfoWave::SINE);
    float block_val = lfo_block.process_block(N);

    // Both sampled at phase = N * phase_inc → should agree within float rounding.
    float diff = fabsf(block_val - ref_val);
    TEST_ASSERT(diff < 1e-5f, "process_block(64) must match reference (64 process() steps + 1 sample) within 1e-5");
    test_pass();
}

/* Entry point declared in main.cpp */
void test_mod_sources_suite() {
    test_env2_independent_from_env1();
    test_env2_follows_gate();
    test_lfo_injection_depth();
    test_lfo_depth_scaling();
    test_lfo_delay_fade_in();
    test_lfo_voice_determinism();
    test_lfo_sh_piecewise_constant();
    test_lfo_output_range();
    test_env2_per_voice_independent();
    test_lfo_process_block();
}
