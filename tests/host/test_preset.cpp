/* tests/host/test_preset.cpp — round-trip and edge-case tests for preset.h */
#include "runner.h"
#include "engine/preset.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include <string.h>
#include <stdint.h>

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

static void test_serialize_parse_roundtrip(void) {
    test_begin("serialize/parse round-trip preserves values in range");

    float norms[128] = {};
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        if (d.id < 128) norms[d.id] = 0.5f;
    }

    uint8_t blob[PRESET_BLOB_MAX];
    int len = preset_serialize(blob, sizeof(blob), "RoundTrip", norms, 128);
    TEST_ASSERT(len > 0, "serialize returned error");
    TEST_ASSERT((size_t)len <= PRESET_BLOB_MAX, "blob exceeds max size");

    char     name[PRESET_NAME_LEN + 1];
    uint16_t ids[32];
    float    vals[32];
    int count = preset_parse(blob, (size_t)len, name, sizeof(name), ids, vals, 32);
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
    int len = preset_serialize(tiny, sizeof(tiny), "X", norms, 128);
    TEST_ASSERT(len == -1, "expected -1 for undersized buffer");
    test_pass();
}

static void test_parse_bad_magic(void) {
    test_begin("parse returns -1 on wrong magic");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    int len = preset_serialize(blob, sizeof(blob), "Test", norms, 128);
    TEST_ASSERT(len > 0, "serialize failed in bad-magic setup");
    blob[0] ^= 0xFF;  // corrupt magic byte
    char name[33]; uint16_t ids[32]; float vals[32];
    TEST_ASSERT(preset_parse(blob, (size_t)len, name, sizeof(name), ids, vals, 32) == -1,
                "expected -1 for bad magic");
    test_pass();
}

static void test_parse_truncated_blob(void) {
    test_begin("parse returns -1 on truncated blob");
    float norms[128] = {};
    uint8_t blob[PRESET_BLOB_MAX];
    int len = preset_serialize(blob, sizeof(blob), "Trunc", norms, 128);
    TEST_ASSERT(len > 0, "serialize failed in truncated-blob setup");
    char name[33]; uint16_t ids[32]; float vals[32];
    // Only pass the header (42 bytes) — not enough for the param body.
    TEST_ASSERT(preset_parse(blob, 42, name, sizeof(name), ids, vals, 32) == -1,
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
    int len = preset_serialize(blob, sizeof(blob), long_name, norms, 128);
    TEST_ASSERT(len > 0, "serialize failed with 31-char name");
    char name[PRESET_NAME_LEN + 1]; uint16_t ids[32]; float vals[32];
    int count = preset_parse(blob, (size_t)len, name, sizeof(name), ids, vals, 32);
    TEST_ASSERT(count > 0, "parse failed for 31-char name");
    TEST_ASSERT(strlen(name) <= (size_t)PRESET_NAME_LEN, "name too long after parse");
    test_pass();
}

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
}
