// engine/bank_json.cpp — JSON bank codec (ADR 0027). Control-path only; see
// bank_json.h for the heap-usage note and schema.
#include "bank_json.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "param_desc.h"
#include "param_id.h"

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

static const ParamDesc* find_param_by_id(uint16_t id) {
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].id == id) return &JUNO_PARAM_TABLE[i];
    }
    return nullptr;
}

// Canonical param name is ParamDesc::name (the full display name, e.g.
// "Osc Level"), not short_name.
static const ParamDesc* find_param_by_name(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].name && strcmp(JUNO_PARAM_TABLE[i].name, name) == 0) return &JUNO_PARAM_TABLE[i];
    }
    return nullptr;
}

struct NamedEnum {
    const char* name;
    uint8_t     value;
};

static const NamedEnum kModSourceNames[] = {
    {"NONE", (uint8_t)ModSource::NONE},
    {"LFO1", (uint8_t)ModSource::LFO1},
    {"LFO2", (uint8_t)ModSource::LFO2},
    {"ENV1", (uint8_t)ModSource::ENV1},
    {"ENV2", (uint8_t)ModSource::ENV2},
    {"VELOCITY", (uint8_t)ModSource::VELOCITY},
    {"KEY_TRACK", (uint8_t)ModSource::KEY_TRACK},
    {"MOD_WHEEL", (uint8_t)ModSource::MOD_WHEEL},
    {"PITCH_BEND", (uint8_t)ModSource::PITCH_BEND},
    {"AFTERTOUCH", (uint8_t)ModSource::AFTERTOUCH},
};
static constexpr int kModSourceNameCount = (int)(sizeof(kModSourceNames) / sizeof(kModSourceNames[0]));

static const NamedEnum kModCurveNames[] = {
    {"LIN", (uint8_t)ModCurve::LIN},
    {"SQR", (uint8_t)ModCurve::SQR},
    {"CUBE", (uint8_t)ModCurve::CUBE},
};
static constexpr int kModCurveNameCount = (int)(sizeof(kModCurveNames) / sizeof(kModCurveNames[0]));

// Resolve a cJSON number-or-name node against a NamedEnum table. Returns
// false (leaving *out untouched) if the node is neither a recognised number
// nor a recognised name.
static bool resolve_named_enum(const cJSON* node, const NamedEnum* table, int table_len, uint8_t* out) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        const double value = node->valuedouble;
        if (!std::isfinite(value) || value < 0.0 || value > UINT8_MAX || std::floor(value) != value) return false;
        const uint8_t id = (uint8_t)value;
        for (int i = 0; i < table_len; i++) {
            if (table[i].value == id) {
                *out = id;
                return true;
            }
        }
        return false;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        for (int i = 0; i < table_len; i++) {
            if (strcmp(table[i].name, node->valuestring) == 0) {
                *out = table[i].value;
                return true;
            }
        }
    }
    return false;
}

// Resolve a "dest" node: numeric ParamId, param name, or the literals
// "pitch"/"pwm" for the virtual mod-matrix destinations. Returns false if
// unresolvable.
static bool resolve_dest(const cJSON* node, uint16_t* out) {
    if (!node) return false;
    if (cJSON_IsNumber(node)) {
        const double value = node->valuedouble;
        if (!std::isfinite(value) || value < 0.0 || value > UINT16_MAX || std::floor(value) != value) return false;
        const uint16_t id = (uint16_t)value;
        if (id != kModDestPitch && id != kModDestPwm && !find_param_by_id(id)) return false;
        *out = id;
        return true;
    }
    if (cJSON_IsString(node) && node->valuestring) {
        if (strcmp(node->valuestring, "pitch") == 0) {
            *out = kModDestPitch;
            return true;
        }
        if (strcmp(node->valuestring, "pwm") == 0) {
            *out = kModDestPwm;
            return true;
        }
        const ParamDesc* d = find_param_by_name(node->valuestring);
        if (d) {
            *out = d->id;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

bool bank_json_parse_patch(const cJSON* obj, PresetPatch* out) {
    if (!obj || !out || !cJSON_IsObject(obj)) return false;

    memset(out, 0, sizeof(*out));

    const cJSON* name_node = cJSON_GetObjectItemCaseSensitive(obj, "name");
    if (name_node && cJSON_IsString(name_node) && name_node->valuestring) {
        strncpy(out->name, name_node->valuestring, PRESET_NAME_LEN - 1);
        out->name[PRESET_NAME_LEN - 1] = '\0';
    }

    // --- params ---
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(obj, "params");
    if (params && cJSON_IsObject(params)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, params) {
            if (!item->string || !cJSON_IsNumber(item)) continue;  // non-number value: skip
            const ParamDesc* d      = nullptr;
            // Numeric-string key ("22") vs name key ("FILTER_CUTOFF"/display name).
            char*            endptr = nullptr;
            errno                   = 0;
            unsigned long id_val    = strtoul(item->string, &endptr, 10);
            if (endptr != item->string && *endptr == '\0') {
                if (errno == 0 && item->string[0] != '-' && id_val <= UINT16_MAX) {
                    d = find_param_by_id((uint16_t)id_val);
                }
            } else {
                d = find_param_by_name(item->string);
            }
            if (!d) continue;                                 // unknown id/name: skip
            if (d->flags & FLAG_NO_PRESET) continue;          // session-only: skip
            if (!std::isfinite(item->valuedouble)) continue;  // never pass NaN/Inf into the engine
            if (out->count >= PRESET_MAX_PARAMS) continue;    // capacity cap: drop extras
            out->ids[out->count]  = d->id;
            out->vals[out->count] = (float)item->valuedouble;
            out->count++;
        }
    }

    // --- routes (optional) ---
    const cJSON* routes = cJSON_GetObjectItemCaseSensitive(obj, "routes");
    if (routes && cJSON_IsArray(routes)) {
        const cJSON* rnode = nullptr;
        cJSON_ArrayForEach(rnode, routes) {
            if (!cJSON_IsObject(rnode)) continue;
            if (out->route_count >= PRESET_MAX_ROUTINGS) break;  // capacity cap

            const cJSON* src_node   = cJSON_GetObjectItemCaseSensitive(rnode, "source");
            const cJSON* dest_node  = cJSON_GetObjectItemCaseSensitive(rnode, "dest");
            const cJSON* depth_node = cJSON_GetObjectItemCaseSensitive(rnode, "depth");
            const cJSON* curve_node = cJSON_GetObjectItemCaseSensitive(rnode, "curve");

            uint8_t source = (uint8_t)ModSource::NONE;
            if (!resolve_named_enum(src_node, kModSourceNames, kModSourceNameCount, &source)) continue;
            if (source == (uint8_t)ModSource::NONE) continue;    // NONE source: dropped
            if (source >= (uint8_t)ModSource::_COUNT) continue;  // unknown source: dropped

            uint16_t dest = 0;
            if (!resolve_dest(dest_node, &dest)) continue;

            float depth = 0.0f;
            if (depth_node && cJSON_IsNumber(depth_node)) depth = (float)depth_node->valuedouble;
            if (!std::isfinite(depth) || depth < -1.0f || depth > 1.0f || depth == 0.0f) continue;

            uint8_t curve = (uint8_t)ModCurve::LIN;
            if (curve_node) {
                if (!resolve_named_enum(curve_node, kModCurveNames, kModCurveNameCount, &curve))
                    curve = (uint8_t)ModCurve::LIN;
            }

            Routing& r      = out->routes[out->route_count];
            r.source        = source;
            r.dest_param_id = dest;
            r.depth         = depth;
            r.curve         = curve;
            out->route_count++;
        }
    }

    return true;
}

int bank_json_parse(const char* json, size_t len, PresetPatch* out, int max_patches) {
    if (!json || !out || max_patches <= 0) return -1;

    const char* parse_end = nullptr;
    cJSON*      root      = cJSON_ParseWithLengthOpts(json, len, &parse_end, false);
    if (!root) return -1;  // malformed/truncated JSON
    const char* const input_end = json + len;
    while (parse_end < input_end &&
           (*parse_end == ' ' || *parse_end == '\t' || *parse_end == '\r' || *parse_end == '\n')) {
        parse_end++;
    }
    if (parse_end != input_end) {
        cJSON_Delete(root);
        return -1;  // a valid prefix followed by garbage is still malformed input
    }
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return -1;  // root must be an array
    }

    int          n    = 0;
    const cJSON* elem = nullptr;
    cJSON_ArrayForEach(elem, root) {
        if (n >= max_patches) break;
        if (!cJSON_IsObject(elem)) continue;  // skip non-object entries, forward-compat
        if (bank_json_parse_patch(elem, &out[n])) n++;
    }

    cJSON_Delete(root);
    return n;
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------

int bank_json_serialize_patch(const PresetPatch* p, char* buf, size_t buf_max) {
    if (!p || !buf || buf_max == 0) return -1;

    cJSON* obj = cJSON_CreateObject();
    if (!obj) return -1;

    cJSON_AddStringToObject(obj, "name", p->name);

    cJSON*    params      = cJSON_AddObjectToObject(obj, "params");
    const int param_count = p->count < PRESET_MAX_PARAMS ? p->count : PRESET_MAX_PARAMS;
    for (int i = 0; i < param_count; i++) {
        const ParamDesc* d = find_param_by_id(p->ids[i]);
        if (!d) continue;                         // shouldn't happen, but stay defensive
        if (d->flags & FLAG_NO_PRESET) continue;  // never serialize session-only params
        cJSON_AddNumberToObject(params, d->name, (double)p->vals[i]);
    }

    if (p->route_count > 0) {
        cJSON*    routes      = cJSON_AddArrayToObject(obj, "routes");
        const int route_count = p->route_count < PRESET_MAX_ROUTINGS ? p->route_count : PRESET_MAX_ROUTINGS;
        for (int i = 0; i < route_count; i++) {
            const Routing& r        = p->routes[i];
            cJSON*         rnode    = cJSON_CreateObject();
            const char*    src_name = "NONE";
            for (int j = 0; j < kModSourceNameCount; j++) {
                if (kModSourceNames[j].value == r.source) {
                    src_name = kModSourceNames[j].name;
                    break;
                }
            }
            cJSON_AddStringToObject(rnode, "source", src_name);

            if (r.dest_param_id == kModDestPitch) {
                cJSON_AddStringToObject(rnode, "dest", "pitch");
            } else if (r.dest_param_id == kModDestPwm) {
                cJSON_AddStringToObject(rnode, "dest", "pwm");
            } else {
                const ParamDesc* d = find_param_by_id(r.dest_param_id);
                if (d) {
                    cJSON_AddStringToObject(rnode, "dest", d->name);
                } else {
                    cJSON_AddNumberToObject(rnode, "dest", r.dest_param_id);
                }
            }

            cJSON_AddNumberToObject(rnode, "depth", (double)r.depth);

            const char* curve_name = "LIN";
            for (int j = 0; j < kModCurveNameCount; j++) {
                if (kModCurveNames[j].value == r.curve) {
                    curve_name = kModCurveNames[j].name;
                    break;
                }
            }
            cJSON_AddStringToObject(rnode, "curve", curve_name);

            cJSON_AddItemToArray(routes, rnode);
        }
    }

    bool ok = cJSON_PrintPreallocated(obj, buf, (int)buf_max, /*format=*/false);
    cJSON_Delete(obj);
    if (!ok) return -1;
    return (int)strlen(buf);
}
