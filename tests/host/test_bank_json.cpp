/* tests/host/test_bank_json.cpp — PresetPatch + JSON bank codec tests (WO-13-fmt) */
#include <string.h>
#include <string>
#include "engine/bank_json.h"
#include "engine/mod_matrix.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "runner.h"

// ---------------------------------------------------------------------------
// Basic single-patch parse
// ---------------------------------------------------------------------------

static void test_parse_minimal_single_patch(void) {
    test_begin("parse: minimal single-patch bank");
    const char* json = R"([
        { "name": "Bass", "params": { "Osc Level": 0.85, "Sub Level": 0.6 } }
    ])";
    PresetPatch patches[4];
    int         n = bank_json_parse(json, strlen(json), patches, 4);
    TEST_ASSERT(n == 1, "expected exactly 1 patch");
    TEST_ASSERT(strcmp(patches[0].name, "Bass") == 0, "name must be 'Bass'");
    TEST_ASSERT(patches[0].count == 2, "expected 2 params");
    bool found_osc = false, found_sub = false;
    for (int i = 0; i < patches[0].count; i++) {
        if (patches[0].ids[i] == ParamId::OSC_LEVEL) {
            TEST_ASSERT(patches[0].vals[i] == 0.85f, "OSC_LEVEL physical value must be exact");
            found_osc = true;
        }
        if (patches[0].ids[i] == ParamId::SUB_LEVEL) {
            TEST_ASSERT(patches[0].vals[i] == 0.6f, "SUB_LEVEL physical value must be exact");
            found_sub = true;
        }
    }
    TEST_ASSERT(found_osc && found_sub, "both named params must resolve");
    test_pass();
}

// ---------------------------------------------------------------------------
// id-keyed vs name-keyed params resolve identically
// ---------------------------------------------------------------------------

static void test_id_and_name_keys_equivalent(void) {
    test_begin("parse: id-keyed and name-keyed params match");
    char json_by_name[256];
    snprintf(json_by_name, sizeof(json_by_name), R"([{"name":"A","params":{"Osc Level":0.5}}])");
    char json_by_id[256];
    snprintf(json_by_id, sizeof(json_by_id), R"([{"name":"A","params":{"%d":0.5}}])", (int)ParamId::OSC_LEVEL);

    PresetPatch p_name[1], p_id[1];
    int         n1 = bank_json_parse(json_by_name, strlen(json_by_name), p_name, 1);
    int         n2 = bank_json_parse(json_by_id, strlen(json_by_id), p_id, 1);
    TEST_ASSERT(n1 == 1 && n2 == 1, "both must parse one patch");
    TEST_ASSERT(p_name[0].count == 1 && p_id[0].count == 1, "both must have one param");
    TEST_ASSERT(p_name[0].ids[0] == p_id[0].ids[0], "id-keyed and name-keyed must resolve to same id");
    TEST_ASSERT(p_name[0].vals[0] == p_id[0].vals[0], "values must match");
    test_pass();
}

// ---------------------------------------------------------------------------
// Unknown / FLAG_NO_PRESET params skipped
// ---------------------------------------------------------------------------

static void test_unknown_and_no_preset_params_skipped(void) {
    test_begin("parse: unknown id/name and FLAG_NO_PRESET skipped");
    char json[512];
    snprintf(json, sizeof(json), R"([{"name":"X","params":{
        "Osc Level": 0.7,
        "NoSuchParamName": 1.0,
        "9999": 1.0,
        "%d": 1.0
    }}])",
             (int)ParamId::RECORD);  // RECORD carries FLAG_NO_PRESET
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].count == 1, "only Osc Level should survive");
    TEST_ASSERT(patches[0].ids[0] == ParamId::OSC_LEVEL, "surviving param must be OSC_LEVEL");
    test_pass();
}

// ---------------------------------------------------------------------------
// Routes: by name and by number, pitch/pwm sentinels, drops
// ---------------------------------------------------------------------------

static void test_routes_parse_by_name_and_number(void) {
    test_begin("parse: routes by name and number");
    const char* json = R"([{
        "name": "R",
        "params": {},
        "routes": [
            { "source": "LFO1", "dest": "pitch", "depth": 0.5, "curve": "SQR" },
            { "source": 4, "dest": "pwm", "depth": -0.25, "curve": 0 }
        ]
    }])";
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].route_count == 2, "expected 2 routes");

    const Routing& r0 = patches[0].routes[0];
    TEST_ASSERT(r0.source == (uint8_t)ModSource::LFO1, "route0 source must be LFO1");
    TEST_ASSERT(r0.dest_param_id == kModDestPitch, "route0 dest must be pitch sentinel");
    TEST_ASSERT(r0.depth == 0.5f, "route0 depth must be 0.5");
    TEST_ASSERT(r0.curve == (uint8_t)ModCurve::SQR, "route0 curve must be SQR");

    const Routing& r1 = patches[0].routes[1];
    TEST_ASSERT(r1.source == (uint8_t)ModSource::ENV2, "route1 source (numeric 4) must be ENV2");
    TEST_ASSERT(r1.dest_param_id == kModDestPwm, "route1 dest must be pwm sentinel");
    TEST_ASSERT(r1.depth == -0.25f, "route1 depth must be -0.25");
    TEST_ASSERT(r1.curve == (uint8_t)ModCurve::LIN, "route1 curve must be LIN");
    test_pass();
}

static void test_routes_depth_zero_and_none_source_dropped(void) {
    test_begin("parse: depth==0 and source NONE routes dropped");
    const char* json = R"([{
        "name": "Z",
        "params": {},
        "routes": [
            { "source": "LFO1", "dest": "pitch", "depth": 0.0, "curve": "LIN" },
            { "source": "NONE", "dest": "pitch", "depth": 0.5, "curve": "LIN" },
            { "source": "LFO2", "dest": "pitch", "depth": 0.3, "curve": "LIN" }
        ]
    }])";
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].route_count == 1, "only the LFO2 route should survive");
    TEST_ASSERT(patches[0].routes[0].source == (uint8_t)ModSource::LFO2, "surviving route must be LFO2");
    test_pass();
}

// ---------------------------------------------------------------------------
// Multi-patch array count
// ---------------------------------------------------------------------------

static void test_multi_patch_array_count(void) {
    test_begin("parse: multi-patch array count");
    const char* json = R"([
        {"name":"One","params":{}},
        {"name":"Two","params":{}},
        {"name":"Three","params":{}}
    ])";
    PresetPatch patches[8];
    int         n = bank_json_parse(json, strlen(json), patches, 8);
    TEST_ASSERT(n == 3, "expected 3 patches");
    TEST_ASSERT(strcmp(patches[0].name, "One") == 0, "patch 0 name");
    TEST_ASSERT(strcmp(patches[1].name, "Two") == 0, "patch 1 name");
    TEST_ASSERT(strcmp(patches[2].name, "Three") == 0, "patch 2 name");
    test_pass();
}

// ---------------------------------------------------------------------------
// Fail-closed cases
// ---------------------------------------------------------------------------

static void test_non_array_root_fails(void) {
    test_begin("parse: non-array root returns -1");
    const char* json = R"({"name":"Not an array"})";
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == -1, "non-array root must return -1");
    test_pass();
}

static void test_truncated_json_fails(void) {
    test_begin("parse: truncated/garbage JSON returns -1");
    const char* json1 = R"([{"name": "Truncated", "params": {)";
    PresetPatch patches[1];
    int         n1 = bank_json_parse(json1, strlen(json1), patches, 1);
    TEST_ASSERT(n1 == -1, "truncated JSON must return -1");

    const char* json2 = "this is not json at all {{{";
    int         n2    = bank_json_parse(json2, strlen(json2), patches, 1);
    TEST_ASSERT(n2 == -1, "garbage JSON must return -1");

    const char* json3 = "[] trailing garbage";
    int         n3    = bank_json_parse(json3, strlen(json3), patches, 1);
    TEST_ASSERT(n3 == -1, "valid JSON followed by garbage must return -1");
    test_pass();
}

static void test_out_of_range_numbers_are_skipped(void) {
    test_begin("parse: out-of-range numeric fields are skipped");
    const char* json = R"([{
        "name": "Bounds",
        "params": { "65552": 0.5, "Osc Level": 1e999 },
        "routes": [
            { "source": 257, "dest": "pitch", "depth": 0.5, "curve": 0 },
            { "source": 1, "dest": 65552, "depth": 0.5, "curve": 0 },
            { "source": 1, "dest": "pitch", "depth": 2.0, "curve": 0 }
        ]
    }])";
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].count == 0, "wrapped ids and non-finite values must be skipped");
    TEST_ASSERT(patches[0].route_count == 0, "invalid route numbers must be skipped");
    test_pass();
}

static void test_empty_array_returns_zero(void) {
    test_begin("parse: empty array returns 0");
    const char* json = "[]";
    PresetPatch patches[1];
    int         n = bank_json_parse(json, strlen(json), patches, 1);
    TEST_ASSERT(n == 0, "empty bank must return 0, not -1");
    test_pass();
}

// ---------------------------------------------------------------------------
// Capacity caps (params and routes)
// ---------------------------------------------------------------------------

static void test_params_capped_at_max(void) {
    test_begin("parse: params capped at PRESET_MAX_PARAMS, not overflowed");
    // Build a params object using every real param name from the table, repeated
    // if needed, guaranteed to exceed PRESET_MAX_PARAMS entries is not possible
    // since the table itself has fewer rows than PRESET_MAX_PARAMS typically;
    // instead build unique object keys from every ParamId we know plus bogus
    // numeric ids that also resolve (reuse real ids won't work as JSON object
    // keys must be unique). Use every valid ParamId's numeric id — this is the
    // set of well-known params in the table, which the codec must still cap at
    // PRESET_MAX_PARAMS even if the table itself is smaller today.
    std::string json    = "[{\"name\":\"Cap\",\"params\":{";
    int         emitted = 0;
    for (int i = 0; i < kJunoParamCount && i < PRESET_MAX_PARAMS + 10; i++) {
        if (emitted > 0) json += ",";
        json += "\"" + std::to_string(JUNO_PARAM_TABLE[i].id) + "\":1.0";
        emitted++;
    }
    json += "}}]";

    PresetPatch patches[1];
    int         n = bank_json_parse(json.c_str(), json.size(), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].count <= PRESET_MAX_PARAMS, "params must never exceed PRESET_MAX_PARAMS");
    test_pass();
}

static void test_routes_capped_at_max(void) {
    test_begin("parse: routes capped at PRESET_MAX_ROUTINGS, not overflowed");
    std::string json     = "[{\"name\":\"Cap\",\"params\":{},\"routes\":[";
    const int   kAttempt = PRESET_MAX_ROUTINGS + 8;
    for (int i = 0; i < kAttempt; i++) {
        if (i > 0) json += ",";
        json += R"({"source":"LFO1","dest":"pitch","depth":)";
        json += std::to_string(0.1f + (float)i * 0.001f);
        json += R"(,"curve":"LIN"})";
    }
    json += "]}]";

    PresetPatch patches[1];
    int         n = bank_json_parse(json.c_str(), json.size(), patches, 1);
    TEST_ASSERT(n == 1, "expected 1 patch");
    TEST_ASSERT(patches[0].route_count <= PRESET_MAX_ROUTINGS, "routes must never exceed PRESET_MAX_ROUTINGS");
    TEST_ASSERT(patches[0].route_count == PRESET_MAX_ROUTINGS, "routes should be capped exactly at the max");
    test_pass();
}

// ---------------------------------------------------------------------------
// Round-trip: serialize then parse
// ---------------------------------------------------------------------------

static void test_serialize_then_parse_roundtrip(void) {
    test_begin("round-trip: serialize_patch then parse equal");
    PresetPatch src;
    memset(&src, 0, sizeof(src));
    strncpy(src.name, "RoundTrip", PRESET_NAME_LEN - 1);
    src.ids[0]  = ParamId::OSC_LEVEL;
    src.vals[0] = 0.77f;
    src.ids[1]  = ParamId::FILTER_CUTOFF;
    src.vals[1] = 1234.5f;
    src.count   = 2;

    src.routes[0]   = {(uint8_t)ModSource::LFO1, kModDestPitch, 0.42f, (uint8_t)ModCurve::CUBE};
    src.route_count = 1;

    char buf[2048];
    int  written = bank_json_serialize_patch(&src, buf, sizeof(buf));
    TEST_ASSERT(written > 0, "serialize must succeed");

    // Wrap the single object in an array for bank_json_parse.
    std::string wrapped = "[";
    wrapped            += buf;
    wrapped            += "]";

    PresetPatch out[1];
    int         n = bank_json_parse(wrapped.c_str(), wrapped.size(), out, 1);
    TEST_ASSERT(n == 1, "round-trip parse must yield 1 patch");
    TEST_ASSERT(strcmp(out[0].name, src.name) == 0, "name must round-trip");
    TEST_ASSERT(out[0].count == src.count, "param count must round-trip");
    TEST_ASSERT(out[0].route_count == src.route_count, "route count must round-trip");

    bool found_osc = false, found_cutoff = false;
    for (int i = 0; i < out[0].count; i++) {
        if (out[0].ids[i] == ParamId::OSC_LEVEL) {
            TEST_ASSERT(out[0].vals[i] == 0.77f, "OSC_LEVEL must round-trip exactly");
            found_osc = true;
        }
        if (out[0].ids[i] == ParamId::FILTER_CUTOFF) {
            TEST_ASSERT(out[0].vals[i] == 1234.5f, "FILTER_CUTOFF must round-trip exactly");
            found_cutoff = true;
        }
    }
    TEST_ASSERT(found_osc && found_cutoff, "both params must round-trip");

    TEST_ASSERT(out[0].routes[0].source == (uint8_t)ModSource::LFO1, "route source must round-trip");
    TEST_ASSERT(out[0].routes[0].dest_param_id == kModDestPitch, "route dest must round-trip");
    TEST_ASSERT(out[0].routes[0].depth == 0.42f, "route depth must round-trip");
    TEST_ASSERT(out[0].routes[0].curve == (uint8_t)ModCurve::CUBE, "route curve must round-trip");
    test_pass();
}

static void test_serialize_skips_no_preset_params(void) {
    test_begin("serialize: FLAG_NO_PRESET params are skipped");
    PresetPatch src;
    memset(&src, 0, sizeof(src));
    strncpy(src.name, "SkipSession", PRESET_NAME_LEN - 1);
    src.ids[0]  = ParamId::RECORD;  // FLAG_NO_PRESET
    src.vals[0] = 1.0f;
    src.count   = 1;

    char buf[512];
    int  written = bank_json_serialize_patch(&src, buf, sizeof(buf));
    TEST_ASSERT(written > 0, "serialize must succeed");
    TEST_ASSERT(strstr(buf, "Record") == nullptr, "RECORD's display name must not appear in output");
    test_pass();
}

static void test_serialize_buf_too_small_fails(void) {
    test_begin("serialize: buffer too small returns -1");
    PresetPatch src;
    memset(&src, 0, sizeof(src));
    strncpy(src.name, "TooSmall", PRESET_NAME_LEN - 1);
    src.ids[0]  = ParamId::OSC_LEVEL;
    src.vals[0] = 0.5f;
    src.count   = 1;

    char tiny[4];
    int  written = bank_json_serialize_patch(&src, tiny, sizeof(tiny));
    TEST_ASSERT(written == -1, "too-small buffer must return -1");
    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry point
// ---------------------------------------------------------------------------

void test_bank_json_suite(void) {
    test_parse_minimal_single_patch();
    test_id_and_name_keys_equivalent();
    test_unknown_and_no_preset_params_skipped();
    test_routes_parse_by_name_and_number();
    test_routes_depth_zero_and_none_source_dropped();
    test_multi_patch_array_count();
    test_non_array_root_fails();
    test_truncated_json_fails();
    test_out_of_range_numbers_are_skipped();
    test_empty_array_returns_zero();
    test_params_capped_at_max();
    test_routes_capped_at_max();
    test_serialize_then_parse_roundtrip();
    test_serialize_skips_no_preset_params();
    test_serialize_buf_too_small_fails();
}
