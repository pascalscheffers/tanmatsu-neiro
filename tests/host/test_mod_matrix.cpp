/* tests/host/test_mod_matrix.cpp
 *
 * Host DSP tests for Stage 3b-i: modulation matrix engine.
 *
 * 1. Depth d moves dest by expected amount (single route, exact math).
 * 2. Zero depth → no effect on dest.
 * 3. NONE source → no effect on dest.
 * 4. Multiple sources targeting one dest sum correctly.
 * 5. Audio-rate dest (cutoff) is actually modulated in voice output.
 * 6. Pitch modulation (kModDestPitch) shifts oscillator frequency.
 * 7. Curve shaping: LIN, SQR, CUBE produce distinct outputs.
 * 8. Inactive slots are skipped (O(active) check: all-inactive == zero outputs).
 * 9. Full 16-slot table: all slots active, accumulators sum correctly.
 * 10. ENV2 source routes to cutoff and affects voice cutoff.
 * 11. Velocity source with depth=1 gives full-range mod at vel=127.
 * 12. key_track routes to pitch: higher note → higher pitch offset.
 *
 * ADR 0012 (FTZ-off): CMakeLists enforces -fno-fast-math.
 */

#include "runner.h"
#include "mod_matrix.h"
#include "juno_voice.h"
#include "param_id.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const float kSampleRate = 48000.0f;
static const int   kBlock      = 64;

// Helper: fill ModSources with all-zero except the named field.
static ModSources make_sources(ModSource which, float value) {
    ModSources s{};
    switch (which) {
        case ModSource::LFO1:       s.lfo1       = value; break;
        case ModSource::LFO2:       s.lfo2       = value; break;
        case ModSource::ENV1:       s.env1       = value; break;
        case ModSource::ENV2:       s.env2       = value; break;
        case ModSource::VELOCITY:   s.velocity   = value; break;
        case ModSource::KEY_TRACK:  s.key_track  = value; break;
        case ModSource::MOD_WHEEL:  s.mod_wheel  = value; break;
        case ModSource::PITCH_BEND: s.pitch_bend = value; break;
        case ModSource::AFTERTOUCH: s.aftertouch = value; break;
        default: break;
    }
    return s;
}

// Helper: render n blocks into buf (accumulation mode), return rms of last block.
static float voice_rms_after_blocks(JunoVoice& v, int n) {
    float buf[kBlock];
    float last_rms = 0.0f;
    for (int b = 0; b < n; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, kBlock);
        if (b == n - 1) {
            float sum = 0.0f;
            for (int i = 0; i < kBlock; i++) sum += buf[i] * buf[i];
            last_rms = sqrtf(sum / (float)kBlock);
        }
    }
    return last_rms;
}

/* --- 1. Depth d moves cutoff by expected amount (direct matrix eval) ------- */
void test_matrix_depth_moves_dest() {
    printf("--- Stage 3b-i: ModMatrix engine ---\n");
    test_begin("matrix: depth d scales source into cutoff accumulator");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{
        (uint8_t)ModSource::ENV2,
        ParamId::FILTER_CUTOFF,
        0.5f,
        (uint8_t)ModCurve::LIN
    });

    // ENV2 source value = 1.0 → contribution = 1.0 * 0.5 = 0.5
    ModSources src{};
    src.env2 = 1.0f;
    ModOutputs out = mat.eval(src);

    // Allow for the tiny anti-denormal offset (1e-20f).
    float expected = 0.5f;
    float got      = out.cutoff_mod;
    float err      = fabsf(got - expected);
    TEST_ASSERT(err < 1e-6f,
                "cutoff_mod with env2=1.0 and depth=0.5 must equal 0.5");
    test_pass();
}

/* --- 2. Zero depth → no effect -------------------------------------------- */
void test_matrix_zero_depth_no_effect() {
    test_begin("matrix: depth=0 slot is skipped (no dest change)");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{
        (uint8_t)ModSource::LFO1,
        ParamId::FILTER_CUTOFF,
        0.0f,          // zero depth → inactive
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.lfo1 = 1.0f;
    ModOutputs out = mat.eval(src);

    // Anti-denormal offset only, no real contribution.
    TEST_ASSERT(fabsf(out.cutoff_mod) < 1e-15f,
                "depth=0 route must not contribute to cutoff_mod");
    test_pass();
}

/* --- 3. NONE source → no effect ------------------------------------------- */
void test_matrix_none_source_no_effect() {
    test_begin("matrix: NONE source is skipped (no dest change)");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{
        (uint8_t)ModSource::NONE,
        ParamId::FILTER_CUTOFF,
        1.0f,
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.lfo1 = 1.0f;   // active but source is NONE
    ModOutputs out = mat.eval(src);

    TEST_ASSERT(fabsf(out.cutoff_mod) < 1e-15f,
                "NONE source must not contribute to cutoff_mod");
    test_pass();
}

/* --- 4. Multiple sources to one dest sum correctly ------------------------- */
void test_matrix_multi_source_summing() {
    test_begin("matrix: two routes to same dest sum their contributions");

    ModMatrix mat;
    mat.clear();
    // Route 0: LFO1 → FILTER_CUTOFF, depth 0.3 → contribution: 1.0 × 0.3 = 0.3
    mat.set_route(0, Routing{
        (uint8_t)ModSource::LFO1,
        ParamId::FILTER_CUTOFF,
        0.3f,
        (uint8_t)ModCurve::LIN
    });
    // Route 1: ENV2 → FILTER_CUTOFF, depth 0.4 → contribution: 1.0 × 0.4 = 0.4
    mat.set_route(1, Routing{
        (uint8_t)ModSource::ENV2,
        ParamId::FILTER_CUTOFF,
        0.4f,
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.lfo1 = 1.0f;
    src.env2 = 1.0f;
    ModOutputs out = mat.eval(src);

    // Expected: 0.3 + 0.4 = 0.7 (plus tiny anti-denormal offset ≈1e-20).
    float expected = 0.7f;
    float err      = fabsf(out.cutoff_mod - expected);
    TEST_ASSERT(err < 1e-6f,
                "sum of two routes to cutoff_mod must be 0.3+0.4=0.7");
    test_pass();
}

/* --- 5. Audio-rate dest (cutoff) is modulated in voice output -------------- */
void test_matrix_cutoff_mod_in_voice() {
    test_begin("matrix: ENV2→cutoff routing audibly affects voice RMS");

    // Voice A: no routing.
    JunoVoice va;
    va.init(kSampleRate);
    va.set_param((int)ParamId::ENV_ATTACK,   0.001f);
    va.set_param((int)ParamId::ENV_SUSTAIN,  0.8f);
    va.set_param((int)ParamId::FILTER_CUTOFF, 200.0f);  // very low cutoff
    va.set_param((int)ParamId::ENV2_ATTACK,  0.001f);
    va.set_param((int)ParamId::ENV2_DECAY,   0.001f);
    va.set_param((int)ParamId::ENV2_SUSTAIN, 1.0f);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    va.note_on(69, 100, expr);

    // Voice B: ENV2→cutoff with depth=1 (adds full env2 value to 200Hz base).
    JunoVoice vb;
    vb.init(kSampleRate);
    vb.set_param((int)ParamId::ENV_ATTACK,   0.001f);
    vb.set_param((int)ParamId::ENV_SUSTAIN,  0.8f);
    vb.set_param((int)ParamId::FILTER_CUTOFF, 200.0f);
    vb.set_param((int)ParamId::ENV2_ATTACK,  0.001f);
    vb.set_param((int)ParamId::ENV2_DECAY,   0.001f);
    vb.set_param((int)ParamId::ENV2_SUSTAIN, 1.0f);
    // Route: ENV2 → cutoff, depth 5000 (maps 0..1 env → 0..5000 Hz offset)
    vb.mod_matrix().set_route(0, Routing{
        (uint8_t)ModSource::ENV2,
        ParamId::FILTER_CUTOFF,
        5000.0f,  // depth in Hz (cutoff mod is additive Hz)
        (uint8_t)ModCurve::LIN
    });
    vb.note_on(69, 100, expr);

    float rms_a = voice_rms_after_blocks(va, 200);
    float rms_b = voice_rms_after_blocks(vb, 200);

    // With ENV2 opening the filter further, voice B should have higher RMS.
    TEST_ASSERT(rms_b > rms_a * 1.1f,
                "ENV2->cutoff routing must increase voice RMS vs no routing");
    test_pass();
}

/* --- 6. Pitch modulation shifts oscillator frequency ----------------------- */
void test_matrix_pitch_mod() {
    test_begin("matrix: kModDestPitch route shifts pitch (pitch_semi accumulates)");

    ModMatrix mat;
    mat.clear();
    // LFO1 at +1.0 with depth 12 → +12 semitone offset (one octave up).
    mat.set_route(0, Routing{
        (uint8_t)ModSource::LFO1,
        kModDestPitch,
        12.0f,
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.lfo1 = 1.0f;
    ModOutputs out = mat.eval(src);

    float expected = 12.0f;
    float err      = fabsf(out.pitch_semi - expected);
    TEST_ASSERT(err < 1e-6f,
                "LFO1=1.0 with depth=12 must give pitch_semi=12 (one octave)");
    test_pass();
}

/* --- 7. Curve shaping: LIN, SQR, CUBE ------------ */
void test_matrix_curve_shaping() {
    test_begin("matrix: LIN/SQR/CUBE curves produce distinct outputs");

    ModMatrix mat;
    mat.clear();

    ModSources src{};
    src.env2 = 0.5f;  // mid-range value

    // LIN: 0.5 * 1.0 = 0.5
    mat.set_route(0, Routing{(uint8_t)ModSource::ENV2, ParamId::FILTER_CUTOFF,
                             1.0f, (uint8_t)ModCurve::LIN});
    float lin_out = mat.eval(src).cutoff_mod;

    // SQR: signed square of (0.5*1.0)=0.5 → 0.5*|0.5|=0.25
    mat.set_route(0, Routing{(uint8_t)ModSource::ENV2, ParamId::FILTER_CUTOFF,
                             1.0f, (uint8_t)ModCurve::SQR});
    float sqr_out = mat.eval(src).cutoff_mod;

    // CUBE: (0.5)^3 = 0.125
    mat.set_route(0, Routing{(uint8_t)ModSource::ENV2, ParamId::FILTER_CUTOFF,
                             1.0f, (uint8_t)ModCurve::CUBE});
    float cub_out = mat.eval(src).cutoff_mod;

    // LIN > SQR > CUBE for x=0.5 in [0,1].
    TEST_ASSERT(lin_out > sqr_out,  "LIN(0.5) must be > SQR(0.5)");
    TEST_ASSERT(sqr_out > cub_out,  "SQR(0.5) must be > CUBE(0.5)");

    float err_lin  = fabsf(lin_out - 0.5f);
    float err_sqr  = fabsf(sqr_out - 0.25f);
    float err_cub  = fabsf(cub_out - 0.125f);
    TEST_ASSERT(err_lin  < 1e-6f, "LIN(0.5) must equal 0.5");
    TEST_ASSERT(err_sqr  < 1e-6f, "SQR(0.5*1.0) must equal 0.25");
    TEST_ASSERT(err_cub  < 1e-6f, "CUBE(0.5) must equal 0.125");
    test_pass();
}

/* --- 8. All-inactive matrix → zero outputs --------------------------------- */
void test_matrix_all_inactive_zero() {
    test_begin("matrix: cleared (all-inactive) matrix produces near-zero outputs");

    ModMatrix mat;
    mat.clear();

    ModSources src{};
    src.lfo1 = 1.0f; src.env2 = 1.0f; src.velocity = 1.0f;
    ModOutputs out = mat.eval(src);

    TEST_ASSERT(fabsf(out.cutoff_mod) < 1e-15f, "cleared mat cutoff_mod ~0");
    TEST_ASSERT(fabsf(out.pitch_semi) < 1e-15f, "cleared mat pitch_semi ~0");
    TEST_ASSERT(fabsf(out.amp_mod)    < 1e-15f, "cleared mat amp_mod ~0");
    TEST_ASSERT(fabsf(out.res_mod)    < 1e-15f, "cleared mat res_mod ~0");
    test_pass();
}

/* --- 9. Full 16-slot table sums correctly ---------------------------------- */
void test_matrix_full_16_slots() {
    test_begin("matrix: 16 active routes all sum into cutoff_mod");

    ModMatrix mat;
    mat.clear();
    // Fill all 16 slots with LFO1→FILTER_CUTOFF depth 0.1
    for (int i = 0; i < kMaxRoutes; i++) {
        mat.set_route(i, Routing{
            (uint8_t)ModSource::LFO1,
            ParamId::FILTER_CUTOFF,
            0.1f,
            (uint8_t)ModCurve::LIN
        });
    }

    ModSources src{};
    src.lfo1 = 1.0f;
    ModOutputs out = mat.eval(src);

    // 16 × (1.0 × 0.1) = 1.6
    float expected = (float)kMaxRoutes * 0.1f;
    float err      = fabsf(out.cutoff_mod - expected);
    TEST_ASSERT(err < 1e-5f,
                "16 active routes depth=0.1 must sum to 1.6 in cutoff_mod");
    test_pass();
}

/* --- 10. ENV2 source routes to cutoff via direct matrix eval --------------- */
void test_matrix_env2_to_cutoff_math() {
    test_begin("matrix: ENV2 source at 0.75 with depth 0.8 → cutoff_mod=0.6");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{
        (uint8_t)ModSource::ENV2,
        ParamId::FILTER_CUTOFF,
        0.8f,
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.env2 = 0.75f;
    ModOutputs out = mat.eval(src);

    float expected = 0.75f * 0.8f;  // 0.6
    float err      = fabsf(out.cutoff_mod - expected);
    TEST_ASSERT(err < 1e-6f,
                "ENV2=0.75, depth=0.8 must give cutoff_mod=0.6 (±1e-6)");
    test_pass();
}

/* --- 11. Velocity source, full range at vel=127 ---------------------------- */
void test_matrix_velocity_source() {
    test_begin("matrix: VELOCITY source with depth=1 at vel=127 gives amp_mod≈1");

    ModMatrix mat;
    mat.clear();
    mat.set_route(0, Routing{
        (uint8_t)ModSource::VELOCITY,
        ParamId::OSC_LEVEL,  // amp dest
        1.0f,
        (uint8_t)ModCurve::LIN
    });

    ModSources src{};
    src.velocity = 127.0f / 127.0f;  // 1.0
    ModOutputs out = mat.eval(src);

    float err = fabsf(out.amp_mod - 1.0f);
    TEST_ASSERT(err < 1e-6f,
                "velocity=1.0, depth=1 must give amp_mod=1.0");
    test_pass();
}

/* --- 12. key_track routes to pitch: higher note → positive pitch offset ----- */
void test_matrix_key_track_pitch() {
    test_begin("matrix: KEY_TRACK→pitch: higher note gives positive pitch_semi");

    ModMatrix mat;
    mat.clear();
    // KEY_TRACK at depth 12: +1 normalized unit (12 semitones from A4) = +12 semi offset
    mat.set_route(0, Routing{
        (uint8_t)ModSource::KEY_TRACK,
        kModDestPitch,
        12.0f,
        (uint8_t)ModCurve::LIN
    });

    // Key track for note 81 (A5, one octave above A4=69): (81-69)/12 = 1.0 normalized
    ModSources src_high{};
    src_high.key_track = (81.0f - 69.0f) / 12.0f;  // +1.0
    ModOutputs out_high = mat.eval(src_high);

    // Key track for note 57 (A3, one octave below A4): (57-69)/12 = -1.0 normalized
    ModSources src_low{};
    src_low.key_track = (57.0f - 69.0f) / 12.0f;  // -1.0
    ModOutputs out_low = mat.eval(src_low);

    TEST_ASSERT(out_high.pitch_semi > 0.0f,
                "KEY_TRACK above A4 must produce positive pitch_semi");
    TEST_ASSERT(out_low.pitch_semi < 0.0f,
                "KEY_TRACK below A4 must produce negative pitch_semi");
    // High: 1.0 * 12 = 12; Low: -1.0 * 12 = -12
    TEST_ASSERT(fabsf(out_high.pitch_semi - 12.0f) < 1e-5f,
                "A5 key_track with depth=12 must give pitch_semi≈12");
    TEST_ASSERT(fabsf(out_low.pitch_semi  + 12.0f) < 1e-5f,
                "A3 key_track with depth=12 must give pitch_semi≈-12");
    test_pass();
}

/* --- Entry point declared in main.cpp ------------------------------------- */
void test_mod_matrix_suite() {
    test_matrix_depth_moves_dest();
    test_matrix_zero_depth_no_effect();
    test_matrix_none_source_no_effect();
    test_matrix_multi_source_summing();
    test_matrix_cutoff_mod_in_voice();
    test_matrix_pitch_mod();
    test_matrix_curve_shaping();
    test_matrix_all_inactive_zero();
    test_matrix_full_16_slots();
    test_matrix_env2_to_cutoff_math();
    test_matrix_velocity_source();
    test_matrix_key_track_pitch();
}
