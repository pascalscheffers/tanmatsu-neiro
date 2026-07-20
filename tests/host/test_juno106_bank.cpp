/* Regression guard for the embedded 128-patch Juno-106 factory bank. WO-14g
 * rebuilds it from pinned KR-106 controller data while retaining the existing
 * JSON codec and provider seam. */
#include <math.h>
#include <string.h>
#include "engine/bank_json.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "runner.h"

// Provided by the generated embed (tests/host/CMakeLists.txt, WO-13h) —
// wraps engine/banks/juno106_factory.json verbatim, mirroring
// factory_bank_neiro_json()'s mechanism without a runtime provider seam.
const char* juno106_factory_bank_json(size_t* len_out);

static constexpr int kExpectedCount = 128;

// 96 patches would fit PRESET_MAX_PARAMS=96 per patch; the array below holds
// 128 whole patches (static: keeps this off the test's stack).
static PresetPatch g_patches[kExpectedCount];

static const ParamDesc* find_desc(uint16_t id) {
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].id == id) return &JUNO_PARAM_TABLE[i];
    }
    return nullptr;
}

static float find_param(const PresetPatch& p, uint16_t id) {
    for (int i = 0; i < p.count; i++) {
        if (p.ids[i] == id) return p.vals[i];
    }
    return NAN;
}

static int find_index(const char* name) {
    for (int i = 0; i < kExpectedCount; i++) {
        if (strcmp(g_patches[i].name, name) == 0) return i;
    }
    return -1;
}

static void test_juno106_bank_parses_128_patches(void) {
    test_begin("juno106 bank: parses to exactly 128 patches");
    size_t      len  = 0;
    const char* json = juno106_factory_bank_json(&len);
    TEST_ASSERT(json != nullptr && len > 0, "embedded juno106 bank must be non-empty");
    int n = bank_json_parse(json, len, g_patches, kExpectedCount);
    TEST_ASSERT(n == kExpectedCount, "expected 128 patches from the embedded bank");
    test_pass();
}

static void test_juno106_bank_descriptive_boundaries(void) {
    test_begin("juno106 bank: descriptive names span A11 Brass through B88 Owgan");
    TEST_ASSERT(strcmp(g_patches[0].name, "A11 Brass") == 0, "patch 0 must be A11 Brass");
    TEST_ASSERT(strcmp(g_patches[63].name, "A88 Caverns") == 0, "patch 63 must be A88 Caverns");
    TEST_ASSERT(strcmp(g_patches[64].name, "B11 Strings") == 0, "patch 64 must be B11 Strings");
    TEST_ASSERT(strcmp(g_patches[127].name, "B88 Owgan") == 0, "patch 127 must be B88 Owgan");
    for (int i = 0; i < kExpectedCount; i++) {
        TEST_ASSERT(strstr(g_patches[i].name, "(uncertain)") == nullptr, "KR names must not carry tape uncertainty");
    }
    test_pass();
}

static void test_juno106_bank_values_finite_and_in_range(void) {
    test_begin("juno106 bank: every param value is finite and within its declared range");
    for (int i = 0; i < kExpectedCount; i++) {
        const PresetPatch& p = g_patches[i];
        TEST_ASSERT(p.count > 0, "every patch must carry at least one param");
        for (int j = 0; j < p.count; j++) {
            float v = p.vals[j];
            TEST_ASSERT(isfinite(v), "param value must be finite (no NaN/Inf)");
            const ParamDesc* d = find_desc(p.ids[j]);
            TEST_ASSERT(d != nullptr, "every parsed id must resolve to a known ParamId");
            TEST_ASSERT(v >= d->min - 1e-4f && v <= d->max + 1e-4f, "param value must be within its declared range");
        }
    }
    test_pass();
}

static void test_juno106_bank_a11_spot_check(void) {
    test_begin("juno106 bank: A11 spot-checked decoded values");
    int idx = find_index("A11 Brass");
    TEST_ASSERT(idx >= 0, "A11 Brass must exist");
    const PresetPatch& p = g_patches[idx];
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_RANGE) - (-12.0f)) < 1e-3f, "A11 OSC_RANGE");
    TEST_ASSERT(fabsf(find_param(p, ParamId::HPF_CUTOFF) - 1.0f) < 1e-3f, "A11 HPF_CUTOFF");
    TEST_ASSERT(fabsf(find_param(p, ParamId::CHORUS_MODE) - 1.0f) < 1e-3f, "A11 CHORUS_MODE");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_SAW_ON) - 1.0f) < 1e-3f, "A11 OSC_SAW_ON");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_PULSE_ON)) < 1e-3f, "A11 OSC_PULSE_ON");
    TEST_ASSERT(fabsf(find_param(p, ParamId::FILTER_CUTOFF) - 20.0f * powf(1000.0f, 35.0f / 127.0f)) < 1e-3f,
                "A11 FILTER_CUTOFF");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_PWM) - 102.0f / 127.0f) < 1e-5f, "A11 OSC_PWM");
    TEST_ASSERT(fabsf(find_param(p, ParamId::FILTER_RES) - 13.0f / 127.0f) < 1e-5f, "A11 FILTER_RES");
    TEST_ASSERT(fabsf(find_param(p, ParamId::VCF_ENV_DEPTH) - 58.0f / 127.0f) < 1e-5f, "A11 VCF_ENV_DEPTH");
    TEST_ASSERT(fabsf(find_param(p, ParamId::VCF_KEY_TRACK) - 86.0f / 127.0f) < 1e-5f, "A11 VCF_KEY_TRACK");
    TEST_ASSERT(fabsf(find_param(p, ParamId::VCA_LEVEL) - 108.0f / 127.0f) < 1e-5f, "A11 VCA_LEVEL");
    TEST_ASSERT(fabsf(find_param(p, ParamId::ENV_ATTACK) - 0.0221675001f) < 1e-6f, "A11 ENV_ATTACK");
    TEST_ASSERT(fabsf(find_param(p, ParamId::ENV_DECAY) - 1.39282155f) < 1e-6f, "A11 ENV_DECAY");
    TEST_ASSERT(fabsf(find_param(p, ParamId::ENV_SUSTAIN) - 45.0f / 127.0f) < 1e-5f, "A11 ENV_SUSTAIN");
    TEST_ASSERT(fabsf(find_param(p, ParamId::ENV_RELEASE) - 0.325979501f) < 1e-6f, "A11 ENV_RELEASE");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_LEVEL) - 1.0f) < 1e-5f,
                "A11 OSC_LEVEL must load unity (Neiro extension)");
    TEST_ASSERT(isnan(find_param(p, ParamId::FILTER_MODE)), "A11 must omit legacy FILTER_MODE no-op");
    TEST_ASSERT(fabsf(find_param(p, ParamId::MASTER_GAIN) - 1.0f) < 1e-5f,
                "A11 MASTER_GAIN must load unity (Neiro extension)");
    // The one real Juno ADSR must be duplicated identically into ENV2.
    TEST_ASSERT(find_param(p, ParamId::ENV_ATTACK) == find_param(p, ParamId::ENV2_ATTACK),
                "A11 ENV_ATTACK must equal ENV2_ATTACK (shared ADSR duplication)");
    test_pass();
}

void test_juno106_bank_suite(void) {
    test_juno106_bank_parses_128_patches();
    test_juno106_bank_descriptive_boundaries();
    test_juno106_bank_values_finite_and_in_range();
    test_juno106_bank_a11_spot_check();
}
