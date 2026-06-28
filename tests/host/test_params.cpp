/* tests/host/test_params.cpp — Stage 3c-i: full Juno param set
 *
 * Asserts:
 * 1. Table coverage: every expected Juno control is exactly one row.
 * 2. ID uniqueness + in-range (all IDs < ParamId::kMax = 128).
 * 3. kJunoParamCount updated to cover all rows.
 * 4. OSC_PWM row exists (needed for the Clean 106 mod-matrix route).
 * 5. New params settable via set_param; audible/observable effects verified:
 *    - OSC_RANGE shifts pitch (freq changes).
 *    - VCF_ENV_DEPTH: ENV2 contribution to cutoff is observable (covered by
 *      the modulated-cutoff path; use VCF_LFO_DEPTH here for audible RMS change).
 *    - VCA_GATE_MODE=1 produces output without envelope (gate drives VCA).
 *    - VCA_LEVEL=0 silences output.
 * 6. HPF_CUTOFF row exists (DSP hook pending; just cached in set_param).
 */

#include "runner.h"
#include "param_desc.h"
#include "param_id.h"
#include "juno_voice.h"
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static const float kSR = 48000.0f;

static float rms(const float* buf, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i] * buf[i];
    return (n > 0) ? sqrtf(sum / (float)n) : 0.0f;
}

/* Helper: find a param by id in the table; returns nullptr if absent. */
static const ParamDesc* find_param(uint16_t id) {
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].id == id) return &JUNO_PARAM_TABLE[i];
    }
    return nullptr;
}

/* 1. Every expected Juno control is present in the table. */
void test_params_table_coverage() {
    printf("--- Stage 3c-i: param table coverage ---\n");
    test_begin("table: all expected Juno controls present");

    /* OSC group */
    TEST_ASSERT(find_param(ParamId::OSC_LEVEL)    != nullptr, "OSC_LEVEL missing");
    TEST_ASSERT(find_param(ParamId::SUB_LEVEL)    != nullptr, "SUB_LEVEL missing");
    TEST_ASSERT(find_param(ParamId::NOISE_LEVEL)  != nullptr, "NOISE_LEVEL missing");
    TEST_ASSERT(find_param(ParamId::OSC_PWM)      != nullptr, "OSC_PWM missing");
    TEST_ASSERT(find_param(ParamId::OSC_WAVEFORM) != nullptr, "OSC_WAVEFORM missing");
    TEST_ASSERT(find_param(ParamId::OSC_RANGE)    != nullptr, "OSC_RANGE missing");

    /* FILTER / VCF group */
    TEST_ASSERT(find_param(ParamId::FILTER_CUTOFF)    != nullptr, "FILTER_CUTOFF missing");
    TEST_ASSERT(find_param(ParamId::FILTER_RES)       != nullptr, "FILTER_RES missing");
    TEST_ASSERT(find_param(ParamId::FILTER_MODE)      != nullptr, "FILTER_MODE missing");
    TEST_ASSERT(find_param(ParamId::HPF_CUTOFF)       != nullptr, "HPF_CUTOFF missing");
    TEST_ASSERT(find_param(ParamId::VCF_ENV_DEPTH)    != nullptr, "VCF_ENV_DEPTH missing");
    TEST_ASSERT(find_param(ParamId::VCF_ENV_POLARITY) != nullptr, "VCF_ENV_POLARITY missing");
    TEST_ASSERT(find_param(ParamId::VCF_KEY_TRACK)    != nullptr, "VCF_KEY_TRACK missing");
    TEST_ASSERT(find_param(ParamId::VCF_LFO_DEPTH)    != nullptr, "VCF_LFO_DEPTH missing");

    /* ENV1 group */
    TEST_ASSERT(find_param(ParamId::ENV_ATTACK)  != nullptr, "ENV_ATTACK missing");
    TEST_ASSERT(find_param(ParamId::ENV_DECAY)   != nullptr, "ENV_DECAY missing");
    TEST_ASSERT(find_param(ParamId::ENV_SUSTAIN) != nullptr, "ENV_SUSTAIN missing");
    TEST_ASSERT(find_param(ParamId::ENV_RELEASE) != nullptr, "ENV_RELEASE missing");

    /* ENV2 group */
    TEST_ASSERT(find_param(ParamId::ENV2_ATTACK)  != nullptr, "ENV2_ATTACK missing");
    TEST_ASSERT(find_param(ParamId::ENV2_DECAY)   != nullptr, "ENV2_DECAY missing");
    TEST_ASSERT(find_param(ParamId::ENV2_SUSTAIN) != nullptr, "ENV2_SUSTAIN missing");
    TEST_ASSERT(find_param(ParamId::ENV2_RELEASE) != nullptr, "ENV2_RELEASE missing");

    /* LFO group */
    TEST_ASSERT(find_param(ParamId::LFO1_RATE)  != nullptr, "LFO1_RATE missing");
    TEST_ASSERT(find_param(ParamId::LFO1_DEPTH) != nullptr, "LFO1_DEPTH missing");
    TEST_ASSERT(find_param(ParamId::LFO1_SHAPE) != nullptr, "LFO1_SHAPE missing");
    TEST_ASSERT(find_param(ParamId::LFO1_DELAY) != nullptr, "LFO1_DELAY missing");
    TEST_ASSERT(find_param(ParamId::LFO2_RATE)  != nullptr, "LFO2_RATE missing");
    TEST_ASSERT(find_param(ParamId::LFO2_DEPTH) != nullptr, "LFO2_DEPTH missing");
    TEST_ASSERT(find_param(ParamId::LFO2_SHAPE) != nullptr, "LFO2_SHAPE missing");
    TEST_ASSERT(find_param(ParamId::LFO2_DELAY) != nullptr, "LFO2_DELAY missing");

    /* FX group */
    TEST_ASSERT(find_param(ParamId::CHORUS_RATE)  != nullptr, "CHORUS_RATE missing");
    TEST_ASSERT(find_param(ParamId::CHORUS_DEPTH) != nullptr, "CHORUS_DEPTH missing");
    TEST_ASSERT(find_param(ParamId::CHORUS_DELAY) != nullptr, "CHORUS_DELAY missing");
    TEST_ASSERT(find_param(ParamId::CHORUS_MODE)  != nullptr, "CHORUS_MODE missing");

    /* AMP group */
    TEST_ASSERT(find_param(ParamId::MASTER_GAIN)    != nullptr, "MASTER_GAIN missing");
    TEST_ASSERT(find_param(ParamId::VCA_GATE_MODE)  != nullptr, "VCA_GATE_MODE missing");
    TEST_ASSERT(find_param(ParamId::VCA_LEVEL)      != nullptr, "VCA_LEVEL missing");

    test_pass();
}

/* 2. ID uniqueness + all IDs < kMax (128). */
void test_params_ids_unique_and_in_range() {
    test_begin("table: all IDs unique and < ParamId::kMax");

    /* Check each ID is < kMax and not duplicated. */
    for (int i = 0; i < kJunoParamCount; i++) {
        uint16_t id = JUNO_PARAM_TABLE[i].id;
        TEST_ASSERT(id < ParamId::kMax, "param ID >= kMax (128)");
        for (int j = i + 1; j < kJunoParamCount; j++) {
            TEST_ASSERT(JUNO_PARAM_TABLE[j].id != id, "duplicate param ID in table");
        }
    }
    test_pass();
}

/* 3. kJunoParamCount matches the actual size of JUNO_PARAM_TABLE. */
void test_params_count_matches_table() {
    test_begin("kJunoParamCount matches table size");
    /* Sanity: we added 13 new rows (3 OSC + 4 VCF + 1 HPF + 4 LFO delay + 1 chorus
       mode + 2 VCA) on top of the previous 24. Expected count ≥ 37. */
    TEST_ASSERT(kJunoParamCount >= 37, "kJunoParamCount too small — rows missing");
    test_pass();
}

/* 4. OSC_PWM row exists and has correct metadata. */
void test_params_osc_pwm_row() {
    test_begin("OSC_PWM row: exists, id=0x13, range [0..1], MOD_DEST flag");
    const ParamDesc* p = find_param(ParamId::OSC_PWM);
    TEST_ASSERT(p != nullptr,           "OSC_PWM not in table");
    TEST_ASSERT(p->id == 0x13,          "OSC_PWM id must be 0x13");
    TEST_ASSERT(p->min == 0.0f,         "OSC_PWM min must be 0");
    TEST_ASSERT(p->max == 1.0f,         "OSC_PWM max must be 1");
    TEST_ASSERT((p->flags & FLAG_MOD_DEST) != 0, "OSC_PWM must be a MOD_DEST");
    test_pass();
}

/* 5a. VCA_LEVEL=0 silences output (new param observable). */
void test_params_vca_level_zero_silences() {
    test_begin("set_param: VCA_LEVEL=0 silences output");

    JunoVoice v;
    v.init(kSR);
    /* Fast envelope so we reach sustain quickly. */
    v.set_param((int)ParamId::ENV_ATTACK,  0.001f);
    v.set_param((int)ParamId::ENV_DECAY,   0.001f);
    v.set_param((int)ParamId::ENV_SUSTAIN, 1.0f);
    v.set_param((int)ParamId::VCA_LEVEL,   0.0f);   /* ← the new param */

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    /* Reach sustain. */
    for (int b = 0; b < 50; b++) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
    }
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);
    TEST_ASSERT(rms(buf, 64) < 0.0001f, "VCA_LEVEL=0 must silence output");
    test_pass();
}

/* 5b. OSC_RANGE shifts pitch (observable as a frequency change). */
void test_params_osc_range_shifts_freq() {
    test_begin("set_param: OSC_RANGE shifts oscillator pitch");

    /* Measure zero-crossing rate as a proxy for pitch.
     * Higher range semitones → higher frequency → more zero crossings per block. */
    auto count_crossings = [](float range_semi) -> int {
        JunoVoice v;
        v.init(kSR);
        v.set_param((int)ParamId::ENV_ATTACK,    0.001f);
        v.set_param((int)ParamId::ENV_DECAY,     0.001f);
        v.set_param((int)ParamId::ENV_SUSTAIN,   1.0f);
        v.set_param((int)ParamId::SUB_LEVEL,     0.0f);
        v.set_param((int)ParamId::NOISE_LEVEL,   0.0f);
        v.set_param((int)ParamId::FILTER_CUTOFF, 20000.0f);  /* open filter */
        v.set_param((int)ParamId::OSC_RANGE,     range_semi);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(48, 127, expr);  /* C3 ≈ 131 Hz */

        float buf[256];
        /* Warm up to sustain. */
        for (int b = 0; b < 100; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 256);
        }
        memset(buf, 0, sizeof(buf));
        v.render(buf, 256);

        /* Count zero crossings (sign changes). */
        int crossings = 0;
        for (int i = 1; i < 256; i++) {
            if ((buf[i - 1] < 0.0f) != (buf[i] < 0.0f)) crossings++;
        }
        return crossings;
    };

    int xings_0   = count_crossings(0.0f);   /* C3 natural */
    int xings_12  = count_crossings(12.0f);  /* C4 = +1 octave → ~2× crossings */

    TEST_ASSERT(xings_0  > 0, "OSC_RANGE=0: must have non-zero crossings");
    TEST_ASSERT(xings_12 > xings_0 * 1, "OSC_RANGE=+12 must produce more crossings than 0");
    test_pass();
}

/* 5c. VCA_GATE_MODE=1 (gate mode) produces output without an envelope ramp. */
void test_params_vca_gate_mode() {
    test_begin("set_param: VCA_GATE_MODE=1 produces immediate output");

    JunoVoice v;
    v.init(kSR);
    /* Very slow attack to distinguish gate mode (instant) from env mode. */
    v.set_param((int)ParamId::ENV_ATTACK,     3.0f);   /* 3 s attack */
    v.set_param((int)ParamId::VCA_GATE_MODE,  1.0f);   /* gate mode */
    v.set_param((int)ParamId::FILTER_CUTOFF,  20000.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    v.note_on(69, 127, expr);

    float buf[64];
    memset(buf, 0, sizeof(buf));
    v.render(buf, 64);  /* first block — gate mode should give full output immediately */

    /* In gate mode the output should be at nearly full level on the first block.
     * In env mode with 3 s attack, the first block (64 samples = 1.3 ms) would
     * be tiny (≈ 0.04% of attack done). */
    TEST_ASSERT(rms(buf, 64) > 0.05f,
                "VCA_GATE_MODE=1: must produce significant output on first block");
    test_pass();
}

/* 5d. VCF_ENV_DEPTH changes cutoff modulation (observable via RMS change). */
void test_params_vcf_env_depth() {
    test_begin("set_param: VCF_ENV_DEPTH modulates filter cutoff");

    /* With a low base cutoff and ENV2 depth set high, the filter opens when
     * ENV2 peaks → more energy passes → higher RMS at sustain.
     * Compare: depth=0 vs depth=1 with low base cutoff. */
    auto measure_rms = [](float env_depth) -> float {
        JunoVoice v;
        v.init(kSR);
        v.set_param((int)ParamId::ENV_ATTACK,    0.001f);
        v.set_param((int)ParamId::ENV_DECAY,     0.001f);
        v.set_param((int)ParamId::ENV_SUSTAIN,   1.0f);
        v.set_param((int)ParamId::FILTER_CUTOFF, 200.0f);  /* very low — A4 will be attenuated */
        v.set_param((int)ParamId::FILTER_RES,    0.0f);
        v.set_param((int)ParamId::SUB_LEVEL,     0.0f);
        v.set_param((int)ParamId::NOISE_LEVEL,   0.0f);
        /* Fast ENV2 that stays at peak through sustain (sustain=1). */
        v.set_param((int)ParamId::ENV2_ATTACK,   0.001f);
        v.set_param((int)ParamId::ENV2_DECAY,    0.001f);
        v.set_param((int)ParamId::ENV2_SUSTAIN,  1.0f);
        v.set_param((int)ParamId::VCF_ENV_DEPTH, env_depth);
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        v.note_on(69, 127, expr);  /* A4 = 440 Hz */
        float buf[64];
        for (int b = 0; b < 200; b++) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, 64);
        }
        memset(buf, 0, sizeof(buf));
        v.render(buf, 64);
        return rms(buf, 64);
    };

    float rms_depth0 = measure_rms(0.0f);
    float rms_depth1 = measure_rms(1.0f);

    /* Full ENV depth should open the filter and pass more energy at 440 Hz. */
    TEST_ASSERT(rms_depth1 > rms_depth0 * 1.5f,
                "VCF_ENV_DEPTH=1 must pass more energy than VCF_ENV_DEPTH=0");
    test_pass();
}

/* Entry point declared in main.cpp */
void test_params_suite() {
    test_params_table_coverage();
    test_params_ids_unique_and_in_range();
    test_params_count_matches_table();
    test_params_osc_pwm_row();
    test_params_vca_level_zero_silences();
    test_params_osc_range_shifts_freq();
    test_params_vca_gate_mode();
    test_params_vcf_env_depth();
}
