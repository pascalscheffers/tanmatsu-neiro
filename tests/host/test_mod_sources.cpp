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
#include <cmath>
#include <vector>
#include "dsp/lfo.h"
#include "juno_model.h"
#include "juno_voice.h"
#include "mod_matrix.h"
#include "param_desc.h"
#include "param_id.h"
#include "runner.h"
#include "synth_config.h"
#include "voice_alloc.h"

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

// ── helpers ────────────────────────────────────────────────────────────────────

// Estimate pitch from a rendered voice by counting zero-crossings (pos→neg) over
// a large number of blocks.  Returns crossings-per-second as a float frequency estimate.
// Uses 500 measurement blocks (32000 samples at 48k) for reliable counting.
// Must be called after the voice is warmed up (gate set, past attack).
static float estimate_pitch_hz(JunoVoice& v, float mod_wheel, float pitch_bend, float aftertouch) {
    float buf[64];
    int   total  = 0;
    int   blocks = 500;
    float prev   = 0.0f;  // carry last sample across blocks for edge detection

    for (int b = 0; b < blocks; b++) {
        v.set_expression(mod_wheel, pitch_bend, aftertouch);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        // Count positive→negative crossings (one per oscillator period for a saw).
        float last = prev;
        for (int i = 0; i < 64; i++) {
            if (last >= 0.0f && buf[i] < 0.0f) total++;
            last = buf[i];
        }
        prev = buf[63];
    }

    int total_samples = blocks * 64;  // 32000
    return (float)total * kSampleRate / (float)total_samples;
}

static void warm_up_voice(JunoVoice& v, float bend, int warmup_blocks = 30) {
    float buf[64];
    for (int b = 0; b < warmup_blocks; b++) {
        v.set_expression(0.0f, bend, 0.0f);
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
}

static JunoVoice make_pitch_test_voice(int note) {
    JunoVoice v;
    v.init(kSampleRate);
    v.set_param((int)ParamId::OSC_WAVEFORM, 0);  // SAW — clean zero crossings
    v.set_param((int)ParamId::OSC_LEVEL, 1.0f);
    v.set_param((int)ParamId::SUB_LEVEL, 0.0f);
    v.set_param((int)ParamId::NOISE_LEVEL, 0.0f);
    v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);  // wide open — no crossings lost to LPF
    v.set_param((int)ParamId::FILTER_RES, 0.0f);
    v.set_param((int)ParamId::VCF_ENV_DEPTH, 0.0f);
    v.set_param((int)ParamId::VCF_KEY_TRACK, 0.0f);
    v.set_param((int)ParamId::VCF_LFO_DEPTH, 0.0f);
    v.set_param((int)ParamId::ENV_ATTACK, 0.001f);
    v.set_param((int)ParamId::ENV_DECAY, 0.001f);
    v.set_param((int)ParamId::ENV_SUSTAIN, 1.0f);
    v.set_param((int)ParamId::VCA_GATE_MODE, 0);  // env mode (gate mode clips DC offset)
    v.set_param((int)ParamId::VCA_LEVEL, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on((uint8_t)note, 127, expr);
    return v;
}

/* --- 11. Pitch-bend: positive bend raises pitch ----------------------------- */
void test_pitch_bend_shifts_pitch_up() {
    printf("--- Stage 5c: MIDI expression (pitch-bend, mod-wheel, aftertouch) ---\n");
    test_begin("Pitch bend +1.0 raises pitch toward note+2semis");

    // Note 60 (C4) = 261.63 Hz, note 62 (D4) = 293.66 Hz.
    // Bend ±1 → ±kPitchBendRangeSemis (2 semis). Expected: ≈293 Hz at bend=+1.
    // Measure via zero-crossings over 32000 samples → ~140-200 crossings at these pitches.

    JunoVoice v0 = make_pitch_test_voice(60);
    JunoVoice v2 = make_pitch_test_voice(62);
    JunoVoice vb = make_pitch_test_voice(60);

    warm_up_voice(v0, 0.0f);
    warm_up_voice(v2, 0.0f);
    warm_up_voice(vb, +1.0f);

    float freq_60   = estimate_pitch_hz(v0, 0.0f, 0.0f, 0.0f);
    float freq_62   = estimate_pitch_hz(v2, 0.0f, 0.0f, 0.0f);
    float freq_b_up = estimate_pitch_hz(vb, 0.0f, +1.0f, 0.0f);

    // bend=+1 must be higher than bend=0.
    TEST_ASSERT(freq_b_up > freq_60, "Pitch bend +1 must raise frequency above note-60 base");

    // bend=+1 on note 60 should approximate note 62 within 10%.
    float ratio = freq_b_up / (freq_62 > 1.0f ? freq_62 : 1.0f);
    TEST_ASSERT(ratio > 0.90f && ratio < 1.10f,
                "Pitch bend +1 on note 60 must approximate note 62 frequency within 10%");

    test_pass();
}

/* --- 12. Pitch-bend: negative bend lowers pitch symmetrically -------------- */
void test_pitch_bend_shifts_pitch_down() {
    test_begin("Pitch bend -1.0 lowers pitch toward note-2semis");

    JunoVoice v0  = make_pitch_test_voice(60);
    JunoVoice v58 = make_pitch_test_voice(58);
    JunoVoice vb  = make_pitch_test_voice(60);

    warm_up_voice(v0, 0.0f);
    warm_up_voice(v58, 0.0f);
    warm_up_voice(vb, -1.0f);

    float freq_60   = estimate_pitch_hz(v0, 0.0f, 0.0f, 0.0f);
    float freq_58   = estimate_pitch_hz(v58, 0.0f, 0.0f, 0.0f);
    float freq_b_dn = estimate_pitch_hz(vb, 0.0f, -1.0f, 0.0f);

    // bend=-1 must be lower than bend=0.
    TEST_ASSERT(freq_b_dn < freq_60, "Pitch bend -1 must lower frequency below note-60 base");

    // bend=-1 on note 60 should approximate note 58 within 10%.
    float ratio = freq_b_dn / (freq_58 > 1.0f ? freq_58 : 1.0f);
    TEST_ASSERT(ratio > 0.90f && ratio < 1.10f,
                "Pitch bend -1 on note 60 must approximate note 58 frequency within 10%");

    test_pass();
}

/* --- 13. Mod-wheel feeds ModSources.mod_wheel to the mod matrix ------------ */
void test_mod_wheel_feeds_matrix() {
    test_begin("Mod-wheel set_expression(1.0) changes output via mod-matrix routing");

    // Wire MOD_WHEEL → FILTER_CUTOFF with depth=0.5 (LIN).
    // At mod_wheel=0: cutoff at base; at mod_wheel=1: cutoff shifted → different tone.
    auto measure_rms_with_wheel = [](float wheel) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_LEVEL, 1.0f);
        v.set_param((int)ParamId::FILTER_CUTOFF, 500.0f);  // low base cutoff
        v.set_param((int)ParamId::FILTER_RES, 0.0f);
        v.set_param((int)ParamId::ENV_ATTACK, 0.001f);
        v.set_param((int)ParamId::ENV_DECAY, 0.001f);
        v.set_param((int)ParamId::ENV_SUSTAIN, 1.0f);
        v.set_param((int)ParamId::VCA_GATE_MODE, 1);

        // Route MOD_WHEEL → FILTER_CUTOFF with depth 5000 Hz
        // (mod_wheel=1 adds 5000 Hz to a 500 Hz base → wide-open filter).
        ModMatrix mat;
        mat.clear();
        Routing r;
        r.source        = static_cast<uint8_t>(ModSource::MOD_WHEEL);
        r.dest_param_id = ParamId::FILTER_CUTOFF;
        r.depth         = 5000.0f;  // additive Hz (matches test_mod_matrix convention)
        r.curve         = static_cast<uint8_t>(ModCurve::LIN);
        mat.set_route(0, r);
        v.set_mod_matrix(mat);

        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);

        float buf[64];
        // Warm-up: steady state.
        for (int b = 0; b < 50; b++) {
            v.set_expression(wheel, 0.0f, 0.0f);
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
        }
        // Measure RMS of last block.
        memset(buf, 0, sizeof(buf));
        v.set_expression(wheel, 0.0f, 0.0f);
        v.render(buf, 64);
        return rms(buf, 64);
    };

    float rms_wheel0 = measure_rms_with_wheel(0.0f);
    float rms_wheel1 = measure_rms_with_wheel(1.0f);

    // Higher cutoff → more high-frequency content → higher RMS on a rich oscillator.
    // The mod-matrix route must cause a measurable difference.
    TEST_ASSERT(rms_wheel1 != rms_wheel0,
                "Mod-wheel at 1.0 with MOD_WHEEL→CUTOFF routing must change output RMS vs wheel=0");
    test_pass();
}

/* --- 13b. Mod-wheel hardwired to cutoff (Launchkey mapping, no routing) ----- */
void test_mod_wheel_hardwired_cutoff() {
    test_begin("Mod-wheel opens the filter via the built-in voice term (no mod-matrix route)");

    // No mod-matrix routing at all — the brightening must come from the hardwired
    // p_mod_wheel_ * kModWheelCutoffRange term in JunoVoice::render(). Low base cutoff
    // so wheel-up has clear headroom to open.
    auto measure_rms_with_wheel = [](float wheel) -> float {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_LEVEL, 1.0f);
        v.set_param((int)ParamId::FILTER_CUTOFF, 300.0f);  // low base cutoff
        v.set_param((int)ParamId::FILTER_RES, 0.0f);
        v.set_param((int)ParamId::ENV_ATTACK, 0.001f);
        v.set_param((int)ParamId::ENV_DECAY, 0.001f);
        v.set_param((int)ParamId::ENV_SUSTAIN, 1.0f);
        v.set_param((int)ParamId::VCA_GATE_MODE, 1);

        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);

        float buf[64];
        for (int b = 0; b < 50; b++) {
            v.set_expression(wheel, 0.0f, 0.0f);
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
        }
        memset(buf, 0, sizeof(buf));
        v.set_expression(wheel, 0.0f, 0.0f);
        v.render(buf, 64);
        return rms(buf, 64);
    };

    float rms_wheel0 = measure_rms_with_wheel(0.0f);
    float rms_wheel1 = measure_rms_with_wheel(1.0f);

    // Wheel up opens the LPF → more high-frequency content passes → higher RMS.
    TEST_ASSERT(rms_wheel1 > rms_wheel0,
                "Mod-wheel at 1.0 must brighten output (open filter) vs wheel=0, with no routing");
    test_pass();
}

/* --- 14. engine_cc_to_param: CC→ParamId lookup via JUNO_PARAM_TABLE -------- */
void test_engine_cc_to_param() {
    test_begin("CC→ParamId lookup: CC21→FILTER_CUTOFF, unassigned CC→0");

    // Replicate the engine_cc_to_param logic directly (synth.cpp not linked in
    // the host test build; JUNO_PARAM_TABLE and kJunoParamCount are available).
    auto cc_to_param = [](uint8_t cc) -> uint16_t {
        if (cc == 0xFF) return 0;
        for (int i = 0; i < kJunoParamCount; i++) {
            if (JUNO_PARAM_TABLE[i].midi_cc == cc) return JUNO_PARAM_TABLE[i].id;
        }
        return 0;
    };

    // Launchkey 37 pots: CC21 = FILTER_CUTOFF, CC22 = FILTER_RES (repointed from GM 74/71).
    uint16_t id_21 = cc_to_param(21);
    TEST_ASSERT(id_21 == ParamId::FILTER_CUTOFF, "CC21 must map to ParamId::FILTER_CUTOFF (0x20)");

    uint16_t id_22 = cc_to_param(22);
    TEST_ASSERT(id_22 == ParamId::FILTER_RES, "CC22 must map to ParamId::FILTER_RES (0x21)");

    // CC74 was freed (no longer in the table) → 0.
    uint16_t id_74 = cc_to_param(74);
    TEST_ASSERT(id_74 == 0, "CC74 must now be unassigned (freed for the Launchkey pot map)");

    // Unassigned CC (0x7E = 126, not in the table) → 0.
    uint16_t id_unk = cc_to_param(0x7E);
    TEST_ASSERT(id_unk == 0, "Unassigned CC (0x7E) must return 0");

    // 0xFF sentinel → 0.
    uint16_t id_ff = cc_to_param(0xFF);
    TEST_ASSERT(id_ff == 0, "CC=0xFF must return 0 (unassigned sentinel)");

    test_pass();
}

/* --- 15. Panic (alloc level): reset_all() silences all voices -------------- */
void test_panic_silences_voices() {
    test_begin("Panic: reset_all() on VoiceAlloc silences all voices immediately");

    // Use VoiceAlloc + JunoModel directly (synth.cpp not linked in host test build;
    // the engine's panic handler calls s_alloc.reset_all() — same logic tested here).
    JunoModel model;
    model.init(kSampleRate);
    VoiceAlloc alloc;
    alloc.init(&model);

    // Trigger several notes.
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);
    alloc.note_on(64, 100, expr);
    alloc.note_on(67, 100, expr);

    // Warm up: advance a few blocks so envelopes are running.
    const VoiceSlot* slots = alloc.slots();
    float            buf[64];
    for (int b = 0; b < 10; b++) {
        for (int v = 0; v < kNumVoices; v++) {
            if (slots[v].voice->is_active()) {
                memset(buf, 0, sizeof(buf));
                slots[v].voice->render(buf, 64);
            }
        }
    }

    // Confirm voices are active before panic.
    int active_before = 0;
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) active_before++;
    }
    TEST_ASSERT(active_before > 0, "Must have active voices before panic");

    // Panic: reset_all silences everything immediately.
    alloc.reset_all();

    int active_after = 0;
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) active_after++;
    }
    TEST_ASSERT(active_after == 0, "reset_all() must silence all voices immediately");
    test_pass();
}

/* --- WO-13d: direct Juno panel modulation semantics ---------------------- */

/* DCO_LFO_DEPTH=0 must be neutral: render output is identical regardless of
 * the injected LFO1 raw value (no pitch modulation leaks through at zero
 * depth). */
void test_dco_lfo_depth_zero_is_neutral() {
    printf("--- WO-13d: direct panel modulation (DCO LFO, PWM mode) ---\n");
    test_begin("DCO_LFO_DEPTH=0: render output identical for any LFO1 raw value");

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    auto render_with = [&](float lfo1_raw) {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
        v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
        v.set_param((int)ParamId::DCO_LFO_DEPTH, 0.0f);
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_DELAY, 0.0f);
        v.note_on(69, 127, expr);
        v.set_lfo_inputs(lfo1_raw, 0.0f);
        // One-block cache latency (ADR 0018): a priming render lets lfo1_value_
        // catch up to the injected raw before the measured block reads it.
        std::vector<float> primer(64, 0.0f);
        v.render(primer.data(), primer.size());
        std::vector<float> buf(64, 0.0f);
        v.render(buf.data(), buf.size());
        return buf;
    };

    std::vector<float> buf_pos = render_with(1.0f);
    std::vector<float> buf_neg = render_with(-1.0f);
    for (size_t i = 0; i < buf_pos.size(); i++) {
        TEST_ASSERT(fabsf(buf_pos[i] - buf_neg[i]) < 1e-6f,
                    "DCO_LFO_DEPTH=0 must not let LFO1 raw affect pitch/output");
    }
    test_pass();
}

/* DCO_LFO_DEPTH at max (1.0) with a fully-swung LFO must stay bounded (finite,
 * no runaway pitch/NaN) and must audibly differ from the depth=0 case. */
void test_dco_lfo_depth_max_is_bounded_and_distinct() {
    test_begin("DCO_LFO_DEPTH=1.0: output stays finite and differs from depth=0");

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    auto render_with_depth = [&](float depth) {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_SAW_ON, 1.0f);
        v.set_param((int)ParamId::OSC_PULSE_ON, 0.0f);
        v.set_param((int)ParamId::DCO_LFO_DEPTH, depth);
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_DELAY, 0.0f);
        v.note_on(69, 127, expr);
        v.set_lfo_inputs(1.0f, 0.0f);
        // One-block cache latency (ADR 0018): prime lfo1_value_ before measuring.
        std::vector<float> primer(64, 0.0f);
        v.render(primer.data(), primer.size());
        std::vector<float> buf(64, 0.0f);
        v.render(buf.data(), buf.size());
        return buf;
    };

    std::vector<float> buf_zero = render_with_depth(0.0f);
    std::vector<float> buf_max  = render_with_depth(1.0f);

    bool differs = false;
    for (size_t i = 0; i < buf_max.size(); i++) {
        TEST_ASSERT(std::isfinite(buf_max[i]), "DCO_LFO_DEPTH=1.0 output must stay finite (bounded)");
        if (fabsf(buf_max[i] - buf_zero[i]) > 1e-6f) differs = true;
    }
    TEST_ASSERT(differs, "DCO_LFO_DEPTH=1.0 must audibly differ from depth=0");
    test_pass();
}

/* PWM_MODE=Manual (1, default): OSC_PWM is the fixed pulse width — the shared
 * LFO1 must NOT move it, so render output is identical regardless of the
 * injected LFO1 raw value. */
void test_pwm_mode_manual_ignores_lfo() {
    test_begin("PWM_MODE=Manual: OSC_PWM fixed width, unaffected by LFO1");

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    auto render_with = [&](float lfo1_raw) {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
        v.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
        v.set_param((int)ParamId::OSC_PWM, 0.30f);
        v.set_param((int)ParamId::PWM_MODE, 1.0f);  // Manual
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_DELAY, 0.0f);
        v.note_on(69, 127, expr);
        v.set_lfo_inputs(lfo1_raw, 0.0f);
        // One-block cache latency (ADR 0018): a priming render lets lfo1_value_
        // catch up to the injected raw before the measured block reads it.
        std::vector<float> primer(64, 0.0f);
        v.render(primer.data(), primer.size());
        std::vector<float> buf(64, 0.0f);
        v.render(buf.data(), buf.size());
        return buf;
    };

    std::vector<float> buf_pos = render_with(1.0f);
    std::vector<float> buf_neg = render_with(-1.0f);
    for (size_t i = 0; i < buf_pos.size(); i++) {
        TEST_ASSERT(fabsf(buf_pos[i] - buf_neg[i]) < 1e-6f, "Manual PWM mode must ignore LFO1 raw entirely");
    }
    test_pass();
}

/* PWM_MODE=LFO (0): OSC_PWM is a modulation amount around the hardware-neutral
 * 50% center — the shared LFO1 must audibly move the duty cycle, so a
 * positive vs negative LFO1 raw swing must produce distinct output. */
void test_pwm_mode_lfo_is_distinct_from_manual() {
    test_begin("PWM_MODE=LFO: OSC_PWM as amount around center, audibly modulated by LFO1");

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    auto render_with = [&](float lfo1_raw) {
        JunoVoice v;
        v.init(kSampleRate);
        v.set_param((int)ParamId::OSC_SAW_ON, 0.0f);
        v.set_param((int)ParamId::OSC_PULSE_ON, 1.0f);
        v.set_param((int)ParamId::OSC_PWM, 0.80f);  // large amount around center
        v.set_param((int)ParamId::PWM_MODE, 0.0f);  // LFO
        v.set_param((int)ParamId::LFO1_DEPTH, 1.0f);
        v.set_param((int)ParamId::LFO1_DELAY, 0.0f);
        v.note_on(69, 127, expr);
        v.set_lfo_inputs(lfo1_raw, 0.0f);
        // One-block cache latency (ADR 0018): a priming render lets lfo1_value_
        // catch up to the injected raw before the measured block reads it.
        std::vector<float> primer(64, 0.0f);
        v.render(primer.data(), primer.size());
        std::vector<float> buf(64, 0.0f);
        v.render(buf.data(), buf.size());
        return buf;
    };

    std::vector<float> buf_pos = render_with(1.0f);
    std::vector<float> buf_neg = render_with(-1.0f);
    bool               differs = false;
    for (size_t i = 0; i < buf_pos.size(); i++) {
        TEST_ASSERT(std::isfinite(buf_pos[i]) && std::isfinite(buf_neg[i]), "PWM LFO mode output must stay finite");
        if (fabsf(buf_pos[i] - buf_neg[i]) > 1e-6f) differs = true;
    }
    TEST_ASSERT(differs, "PWM_MODE=LFO must audibly move the duty cycle with LFO1");
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
    // Stage 5c: MIDI expression tests.
    test_pitch_bend_shifts_pitch_up();
    test_pitch_bend_shifts_pitch_down();
    test_mod_wheel_feeds_matrix();
    test_mod_wheel_hardwired_cutoff();
    test_engine_cc_to_param();
    test_panic_silences_voices();
    // WO-13d: direct Juno panel modulation semantics.
    test_dco_lfo_depth_zero_is_neutral();
    test_dco_lfo_depth_max_is_bounded_and_distinct();
    test_pwm_mode_manual_ignores_lfo();
    test_pwm_mode_lfo_is_distinct_from_manual();
}
