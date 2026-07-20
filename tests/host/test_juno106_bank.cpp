/* tests/host/test_juno106_bank.cpp — regression guard for the embedded 128-patch
 * Juno-106 factory bank (WO-13h, ADR 0027). Proves the bank offline-mapped by
 * tools/build_juno106_bank.py from third_party/juno106-factory/records.json
 * parses cleanly through the existing bank_json codec: 128 patches, correct
 * slot-label boundaries, the 8 tape-decode-residue slots visibly marked
 * "(uncertain)", every value finite and within its ParamId's declared range,
 * and a couple of hand-verified spot-checks against the generator's own
 * output. No runtime provider exists yet (that's WO-13i) — this test talks
 * to bank_json_parse() directly against the embedded bytes. */
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

static void test_juno106_bank_slot_label_boundaries(void) {
    test_begin("juno106 bank: slot labels run A11..A88, B11..B88 in order");
    TEST_ASSERT(strcmp(g_patches[0].name, "A11") == 0, "patch 0 must be A11");
    TEST_ASSERT(strcmp(g_patches[63].name, "A88 (uncertain)") == 0, "patch 63 must be A88 (uncertain)");
    TEST_ASSERT(strcmp(g_patches[64].name, "B11") == 0, "patch 64 must be B11");
    TEST_ASSERT(strcmp(g_patches[127].name, "B88 (uncertain)") == 0, "patch 127 must be B88 (uncertain)");
    test_pass();
}

static void test_juno106_bank_uncertain_slots(void) {
    test_begin("juno106 bank: exactly 8 slots are marked (uncertain)");
    static const char* kUncertainSlots[] = {
        "A73 (uncertain)", "A74 (uncertain)", "A86 (uncertain)", "A87 (uncertain)",
        "A88 (uncertain)", "B65 (uncertain)", "B74 (uncertain)", "B88 (uncertain)",
    };
    for (const char* slot : kUncertainSlots) {
        TEST_ASSERT(find_index(slot) >= 0, "expected uncertain-flagged slot not found");
    }
    int uncertain_count = 0;
    for (int i = 0; i < kExpectedCount; i++) {
        if (strstr(g_patches[i].name, "(uncertain)") != nullptr) uncertain_count++;
    }
    TEST_ASSERT(uncertain_count == 8, "expected exactly 8 uncertain-flagged patches");
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

// Spot-checks pulled from tools/build_juno106_bank.py's own generated output
// for A11 (third_party/juno106-factory/records.json record 0) — proves the
// checked-in bank matches the documented decode, not just "parses".
static void test_juno106_bank_a11_spot_check(void) {
    test_begin("juno106 bank: A11 spot-checked decoded values");
    int idx = find_index("A11");
    TEST_ASSERT(idx >= 0, "A11 must exist");
    const PresetPatch& p = g_patches[idx];
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_RANGE) - (-12.0f)) < 1e-3f, "A11 OSC_RANGE");
    TEST_ASSERT(fabsf(find_param(p, ParamId::HPF_CUTOFF) - 3.0f) < 1e-3f, "A11 HPF_CUTOFF");
    TEST_ASSERT(fabsf(find_param(p, ParamId::CHORUS_MODE) - 2.0f) < 1e-3f, "A11 CHORUS_MODE");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_SAW_ON) - 1.0f) < 1e-3f, "A11 OSC_SAW_ON");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_PULSE_ON) - 1.0f) < 1e-3f, "A11 OSC_PULSE_ON");
    TEST_ASSERT(fabsf(find_param(p, ParamId::OSC_LEVEL) - 1.0f) < 1e-5f,
                "A11 OSC_LEVEL must load unity (Neiro extension)");
    TEST_ASSERT(fabsf(find_param(p, ParamId::FILTER_MODE) - 0.0f) < 1e-5f,
                "A11 FILTER_MODE must load LP (Neiro extension)");
    TEST_ASSERT(fabsf(find_param(p, ParamId::MASTER_GAIN) - 1.0f) < 1e-5f,
                "A11 MASTER_GAIN must load unity (Neiro extension)");
    // The one real Juno ADSR must be duplicated identically into ENV2.
    TEST_ASSERT(find_param(p, ParamId::ENV_ATTACK) == find_param(p, ParamId::ENV2_ATTACK),
                "A11 ENV_ATTACK must equal ENV2_ATTACK (shared ADSR duplication)");
    test_pass();
}

void test_juno106_bank_suite(void) {
    test_juno106_bank_parses_128_patches();
    test_juno106_bank_slot_label_boundaries();
    test_juno106_bank_uncertain_slots();
    test_juno106_bank_values_finite_and_in_range();
    test_juno106_bank_a11_spot_check();
}
