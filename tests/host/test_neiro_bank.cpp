/* tests/host/test_neiro_bank.cpp — regression guard for the embedded Neiro
 * factory bank (WO-13-neiro-bank). Confirms the JSON-backed preset_factory_*
 * API still serves the same 12 patches, in the same order, with byte-identical
 * physical values to the old hardcoded FactoryPreset table it replaced. */
#include <math.h>
#include <string.h>
#include "engine/param_id.h"
#include "engine/preset.h"
#include "runner.h"

static const char* kExpectedNames[] = {
    "INIT",    "Bass",      "Pad",        "Lead",     "106 Strings", "106 Brass",
    "Juno EP", "Solo Lead", "8-Bit Lead", "Chip Arp", "8-Bit Bass",  "Chip Noise",
};
static constexpr int kExpectedCount = (int)(sizeof(kExpectedNames) / sizeof(kExpectedNames[0]));

static float find_param(int idx, uint16_t id) {
    uint16_t ids[64];
    float    vals[64];
    int      n = preset_factory_params(idx, ids, vals, 64);
    for (int i = 0; i < n; i++) {
        if (ids[i] == id) return vals[i];
    }
    return NAN;
}

static void test_neiro_bank_count_and_names(void) {
    test_begin("neiro bank: 12 patches, in the original order");
    TEST_ASSERT(preset_factory_count() == kExpectedCount, "expected 12 factory patches");
    for (int i = 0; i < kExpectedCount; i++) {
        TEST_ASSERT(strcmp(preset_factory_name(i), kExpectedNames[i]) == 0, "patch name/order mismatch");
    }
    test_pass();
}

static void test_neiro_bank_default_is_solo_lead(void) {
    test_begin("neiro bank: default patch resolves to Solo Lead");
    int idx = preset_factory_default();
    TEST_ASSERT(idx >= 0 && idx < preset_factory_count(), "default index out of range");
    TEST_ASSERT(strcmp(preset_factory_name(idx), "Solo Lead") == 0, "default patch must be Solo Lead");
    test_pass();
}

static void test_neiro_bank_all_patches_have_52_params(void) {
    test_begin("neiro bank: every patch carries 52 params");
    uint16_t ids[64];
    float    vals[64];
    for (int i = 0; i < preset_factory_count(); i++) {
        int n = preset_factory_params(i, ids, vals, 64);
        TEST_ASSERT(n == 52, "expected 52 params per factory patch");
    }
    test_pass();
}

// Spot-checks pulled from the original hardcoded FactoryPreset table (git
// history: engine/preset.cpp pre-WO-13-neiro-bank) — proves no drift through
// the JSON round-trip.
static void test_neiro_bank_values_match_original_data(void) {
    test_begin("neiro bank: spot-checked physical values match pre-JSON data");

    auto idx_of = [](const char* name) -> int {
        for (int i = 0; i < preset_factory_count(); i++) {
            if (strcmp(preset_factory_name(i), name) == 0) return i;
        }
        return -1;
    };

    int init = idx_of("INIT");
    TEST_ASSERT(init >= 0, "INIT patch must exist");
    TEST_ASSERT(fabsf(find_param(init, ParamId::FILTER_CUTOFF) - 2000.0f) < 1e-3f, "INIT FILTER_CUTOFF");
    TEST_ASSERT(fabsf(find_param(init, ParamId::MASTER_GAIN) - 1.0f) < 1e-5f, "INIT MASTER_GAIN");

    int bass = idx_of("Bass");
    TEST_ASSERT(bass >= 0, "Bass patch must exist");
    TEST_ASSERT(fabsf(find_param(bass, ParamId::OSC_LEVEL) - 0.85f) < 1e-5f, "Bass OSC_LEVEL");
    TEST_ASSERT(fabsf(find_param(bass, ParamId::SUB_LEVEL) - 0.60f) < 1e-5f, "Bass SUB_LEVEL");
    TEST_ASSERT(fabsf(find_param(bass, ParamId::OSC_RANGE) - (-12.0f)) < 1e-3f, "Bass OSC_RANGE");

    int strings = idx_of("106 Strings");
    TEST_ASSERT(strings >= 0, "106 Strings patch must exist");
    TEST_ASSERT(fabsf(find_param(strings, ParamId::PWM_MODE) - 0.0f) < 1e-5f, "106 Strings PWM_MODE");
    TEST_ASSERT(fabsf(find_param(strings, ParamId::OSC_PWM) - 0.75f) < 1e-5f, "106 Strings OSC_PWM");

    int solo = idx_of("Solo Lead");
    TEST_ASSERT(solo >= 0, "Solo Lead patch must exist");
    TEST_ASSERT(fabsf(find_param(solo, ParamId::DCO_LFO_DEPTH) - 0.05f) < 1e-5f, "Solo Lead DCO_LFO_DEPTH");

    int arp = idx_of("Chip Arp");
    TEST_ASSERT(arp >= 0, "Chip Arp patch must exist");
    TEST_ASSERT(fabsf(find_param(arp, ParamId::CLOCK_BPM) - 130.0f) < 1e-3f, "Chip Arp CLOCK_BPM");
    TEST_ASSERT(fabsf(find_param(arp, ParamId::ARP_ON) - 1.0f) < 1e-5f, "Chip Arp ARP_ON");

    test_pass();
}

void test_neiro_bank_suite(void) {
    test_neiro_bank_count_and_names();
    test_neiro_bank_default_is_solo_lead();
    test_neiro_bank_all_patches_have_52_params();
    test_neiro_bank_values_match_original_data();
}
