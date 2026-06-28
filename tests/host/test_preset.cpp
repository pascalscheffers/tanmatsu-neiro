/* tests/host/test_preset.cpp — round-trip and edge-case tests for preset.h
 * Stage 3b-ii: updated for v2 format (routings block); back-compat v1 tested. */
#include "runner.h"
#include "engine/preset.h"
#include "engine/mod_matrix.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Serialize with no routings (NULL/0 sentinel — valid for v2, writes count=0).
static int serialize_no_routings(void* buf, size_t max, const char* name,
                                  const float* norms, int norms_len) {
    return preset_serialize(buf, max, name, norms, norms_len, nullptr, 0);
}

// Parse with no interest in routings (pass NULL out pointers).
static int parse_params_only(const void* buf, size_t len,
                              char* name, int name_max,
                              uint16_t* ids, float* vals, int max) {
    return preset_parse(buf, len, name, name_max, ids, vals, max,
                        nullptr, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Factory tests
// ---------------------------------------------------------------------------

static void test_factory_count_positive(void) {
    test_begin("factory_count > 0");
    TEST_ASSERT(preset_factory_count() > 0, "expected at least one factory preset");
    test_pass();
}

static void test_factory_init_name(void) {
    test_begin("factory preset 0 is INIT");
    const char* name = preset_factory_name(0);
    TEST_ASSERT(name != nullptr, "factory_name(0) returned null");
    TEST_ASSERT(strcmp(name, "INIT") == 0, "factory preset 0 name should be INIT");
    test_pass();
}

static void test_factory_params_count(void) {
    test_begin("factory_params returns kJunoParamCount entries");
    uint16_t ids[32];
    float    vals[32];
    int n = preset_factory_params(0, ids, vals, 32);
    TEST_ASSERT(n == kJunoParamCount, "factory INIT should have all params");
    test_pass();
}

static void test_factory_oob_returns_minus1(void) {
    test_begin("factory_params out-of-range returns -1");
    uint16_t ids[32]; float vals[32];
    TEST_ASSERT(preset_factory_params(-1, ids, vals, 32) == -1, "idx=-1 should fail");
    TEST_ASSERT(preset_factory_params(preset_factory_count(), ids, vals, 32) == -1,
                "idx=count should fail");
    test_pass();
}

// ---------------------------------------------------------------------------
// Param serialise / parse tests (updated for v2 signature)
// ---------------------------------------------------------------------------

static void test_serialize_parse_roundtrip(void) {
    test_begin("serialize/parse round-trip preserves values in range");

    float norms[128] = {};
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        if (d.id < 128) norms[d.id] = 0.5f;
    }

    uint8_t blob[PRESET_BLOB_MAX];
    int len = serialize_no_routings(blob, sizeof(blob), "RoundTrip", norms, 128);
    TEST_ASSERT(len > 0, "serialize returned error");
    TEST_ASSERT((size_t)len <= PRESET_BLOB_MAX, "blob exceeds max size");

    char     name[PRESET_NAME_LEN + 1];
    uint16_t ids[32];
    float    vals[32];
    int count = parse_params_only(blob, (size_t)len, name, sizeof(name), ids, vals, 32);
    TEST_ASSERT(count == kJunoParamCount, "parsed count should equal table count");
    TEST_ASSERT(strcmp(name, "RoundTrip") == 0, "preset name mismatch after parse");

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < kJunoParamCount; j++) {
            if (JUNO_PARAM_TABLE[j].id == ids[i]) {
                TEST_ASSERT(vals[i] >= JUNO_PARAM_TABLE[j].min - 1e-4f,
                            "parsed value below param min");
                TEST_ASSERT(vals[i] <= JUNO_PARAM_TABLE[j].max + 1e-4f,
                            "parsed value above param max");
                break;
            }
        }
    }
    test_pass();
}

static void test_serialize_bad_buf_too_small(void) {
    test_begin("serialize returns -1 when buf too small");
    float norms[128] = {};
    uint8_t tiny[10];
    int len = serialize_no_routings(tiny, sizeof(tiny), "X", norms, 128);
    TEST_ASSERT(len == -1, "expected -1 for undersized buffer");
    test_pass();
}

static void test_parse_bad_magic(void) {
    test_begin("parse returns -1 on wrong magic");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    int len = serialize_no_routings(blob, sizeof(blob), "Test", norms, 128);
    TEST_ASSERT(len > 0, "serialize failed in bad-magic setup");
    blob[0] ^= 0xFF;  // corrupt magic byte
    char name[33]; uint16_t ids[32]; float vals[32];
    TEST_ASSERT(parse_params_only(blob, (size_t)len, name, sizeof(name), ids, vals, 32) == -1,
                "expected -1 for bad magic");
    test_pass();
}

static void test_parse_truncated_blob(void) {
    test_begin("parse returns -1 on truncated blob");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    int len = serialize_no_routings(blob, sizeof(blob), "Trunc", norms, 128);
    TEST_ASSERT(len > 0, "serialize failed in truncated-blob setup");
    char name[33]; uint16_t ids[32]; float vals[32];
    // Only pass the header (42 bytes) — not enough for the param body.
    TEST_ASSERT(parse_params_only(blob, 42, name, sizeof(name), ids, vals, 32) == -1,
                "expected -1 for truncated blob");
    test_pass();
}

static void test_factory_init_values_match_defaults(void) {
    test_begin("factory INIT physical values match table defaults");
    uint16_t ids[32]; float vals[32];
    int n = preset_factory_params(0, ids, vals, 32);
    TEST_ASSERT(n == kJunoParamCount, "INIT should have all params");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < kJunoParamCount; j++) {
            if (JUNO_PARAM_TABLE[j].id == ids[i]) {
                float diff = vals[i] - JUNO_PARAM_TABLE[j].def;
                if (diff < 0.0f) diff = -diff;
                TEST_ASSERT(diff < 1e-5f, "INIT value deviates from table default");
                break;
            }
        }
    }
    test_pass();
}

static void test_preset_name_fits_in_32_chars(void) {
    test_begin("parsed preset name fits within PRESET_NAME_LEN");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    // 31 printable chars (max that fits null-terminated in a 32-char field).
    const char long_name[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";
    int len = serialize_no_routings(blob, sizeof(blob), long_name, norms, 128);
    TEST_ASSERT(len > 0, "serialize failed with 31-char name");
    char name[PRESET_NAME_LEN + 1]; uint16_t ids[32]; float vals[32];
    int count = parse_params_only(blob, (size_t)len, name, sizeof(name), ids, vals, 32);
    TEST_ASSERT(count > 0, "parse failed for 31-char name");
    TEST_ASSERT(strlen(name) <= (size_t)PRESET_NAME_LEN, "name too long after parse");
    test_pass();
}

// ---------------------------------------------------------------------------
// Routing round-trip tests (Stage 3b-ii)
// ---------------------------------------------------------------------------

static void test_routing_roundtrip(void) {
    test_begin("routing round-trip: source/dest/depth/curve all preserved");

    float norms[128] = {};
    // Two routings: ENV2→cutoff +0.35 LIN, LFO1→0xFFFD +0.20 LIN
    Routing r_in[2] = {
        { (uint8_t)ModSource::ENV2, (uint16_t)ParamId::FILTER_CUTOFF, +0.35f, (uint8_t)ModCurve::LIN },
        { (uint8_t)ModSource::LFO1, 0xFFFDu,                          +0.20f, (uint8_t)ModCurve::LIN },
    };

    uint8_t blob[PRESET_BLOB_MAX];
    int len = preset_serialize(blob, sizeof(blob), "RoutingTest",
                               norms, 128, r_in, 2);
    TEST_ASSERT(len > 0, "serialize with routings failed");
    TEST_ASSERT((size_t)len <= PRESET_BLOB_MAX, "blob with routings exceeds max size");

    char     name[PRESET_NAME_LEN + 1];
    uint16_t ids[32]; float vals[32];
    Routing  r_out[PRESET_MAX_ROUTINGS];
    int      r_count = 0;
    int count = preset_parse(blob, (size_t)len, name, sizeof(name),
                             ids, vals, 32,
                             r_out, PRESET_MAX_ROUTINGS, &r_count);
    TEST_ASSERT(count == kJunoParamCount, "param count wrong after routing round-trip");
    TEST_ASSERT(r_count == 2, "routing count should be 2");
    TEST_ASSERT(r_out[0].source        == (uint8_t)ModSource::ENV2,           "r[0] source mismatch");
    TEST_ASSERT(r_out[0].dest_param_id == (uint16_t)ParamId::FILTER_CUTOFF,   "r[0] dest mismatch");
    TEST_ASSERT(fabsf(r_out[0].depth - 0.35f) < 1e-6f,                        "r[0] depth mismatch");
    TEST_ASSERT(r_out[0].curve         == (uint8_t)ModCurve::LIN,             "r[0] curve mismatch");
    TEST_ASSERT(r_out[1].source        == (uint8_t)ModSource::LFO1,           "r[1] source mismatch");
    TEST_ASSERT(r_out[1].dest_param_id == 0xFFFDu,                            "r[1] dest mismatch");
    TEST_ASSERT(fabsf(r_out[1].depth - 0.20f) < 1e-6f,                        "r[1] depth mismatch");
    TEST_ASSERT(r_out[1].curve         == (uint8_t)ModCurve::LIN,             "r[1] curve mismatch");
    test_pass();
}

static void test_v1_blob_back_compat(void) {
    test_begin("v1 blob (no routings block) still parses; returns 0 routings");

    // Hand-craft a minimal v1 blob: magic + v1 + juno_model + flags + name + count=0
    uint8_t v1blob[42] = {};
    v1blob[0] = 'T'; v1blob[1] = 'N'; v1blob[2] = 'M'; v1blob[3] = 'T';
    v1blob[4] = 1;  // version = 1
    v1blob[5] = 1;  // model = PRESET_MODEL_JUNO
    // flags bytes 6-7 = 0
    // name bytes 8-39 = 0 (empty string)
    // count bytes 40-41 = 0 (zero param entries)
    // Total = 42 bytes — valid v1 with zero params.

    char     name[PRESET_NAME_LEN + 1] = {};
    uint16_t ids[32]; float vals[32];
    Routing  r_out[PRESET_MAX_ROUTINGS];
    int      r_count = -99;
    int count = preset_parse(v1blob, sizeof(v1blob), name, sizeof(name),
                             ids, vals, 32,
                             r_out, PRESET_MAX_ROUTINGS, &r_count);
    TEST_ASSERT(count == 0, "v1 blob with 0 params should return count=0");
    TEST_ASSERT(r_count == 0, "v1 blob should return 0 routings");
    test_pass();
}

static void test_factory_init_has_clean_106_routings(void) {
    test_begin("factory INIT preset carries Clean 106 routings (ADR 0009)");

    Routing r[PRESET_MAX_ROUTINGS];
    int count = preset_factory_routings(0, r, PRESET_MAX_ROUTINGS);

    // Must have at least 2 routings (ENV2→cutoff, LFO1→PWM).
    TEST_ASSERT(count >= 2, "INIT must have at least 2 routings");

    // Find ENV2→FILTER_CUTOFF +0.35 LIN
    bool found_env2_cutoff = false;
    for (int i = 0; i < count; i++) {
        if (r[i].source        == (uint8_t)ModSource::ENV2 &&
            r[i].dest_param_id == (uint16_t)ParamId::FILTER_CUTOFF &&
            fabsf(r[i].depth - 0.35f) < 1e-5f &&
            r[i].curve         == (uint8_t)ModCurve::LIN) {
            found_env2_cutoff = true;
        }
    }
    TEST_ASSERT(found_env2_cutoff, "INIT missing ENV2→cutoff +0.35 LIN routing");

    // Find LFO1→PWM dest (0xFFFD) +0.20 LIN
    bool found_lfo1_pwm = false;
    for (int i = 0; i < count; i++) {
        if (r[i].source        == (uint8_t)ModSource::LFO1 &&
            r[i].dest_param_id == 0xFFFDu &&
            fabsf(r[i].depth - 0.20f) < 1e-5f &&
            r[i].curve         == (uint8_t)ModCurve::LIN) {
            found_lfo1_pwm = true;
        }
    }
    TEST_ASSERT(found_lfo1_pwm, "INIT missing LFO1→PWM +0.20 LIN routing");
    test_pass();
}

static void test_factory_routings_oob_returns_minus1(void) {
    test_begin("factory_routings out-of-range returns -1");
    Routing r[PRESET_MAX_ROUTINGS];
    TEST_ASSERT(preset_factory_routings(-1, r, PRESET_MAX_ROUTINGS) == -1,
                "idx=-1 should fail");
    TEST_ASSERT(preset_factory_routings(preset_factory_count(), r, PRESET_MAX_ROUTINGS) == -1,
                "idx=count should fail");
    test_pass();
}

static void test_serialize_parse_with_zero_routings(void) {
    test_begin("serialize/parse with explicit 0 routings: round-trip returns r_count=0");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    int len = preset_serialize(blob, sizeof(blob), "ZeroR", norms, 128, nullptr, 0);
    TEST_ASSERT(len > 0, "serialize with 0 routings failed");

    Routing r[PRESET_MAX_ROUTINGS];
    int r_count = -1;
    char name[33]; uint16_t ids[32]; float vals[32];
    int count = preset_parse(blob, (size_t)len, name, sizeof(name),
                             ids, vals, 32,
                             r, PRESET_MAX_ROUTINGS, &r_count);
    TEST_ASSERT(count == kJunoParamCount, "param count wrong for zero-routings blob");
    TEST_ASSERT(r_count == 0, "r_count should be 0 for zero-routings blob");
    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry point
// ---------------------------------------------------------------------------

void test_preset_suite(void) {
    test_factory_count_positive();
    test_factory_init_name();
    test_factory_params_count();
    test_factory_oob_returns_minus1();
    test_serialize_parse_roundtrip();
    test_serialize_bad_buf_too_small();
    test_parse_bad_magic();
    test_parse_truncated_blob();
    test_factory_init_values_match_defaults();
    test_preset_name_fits_in_32_chars();
    // Stage 3b-ii routing tests:
    test_routing_roundtrip();
    test_v1_blob_back_compat();
    test_factory_init_has_clean_106_routings();
    test_factory_routings_oob_returns_minus1();
    test_serialize_parse_with_zero_routings();
}
