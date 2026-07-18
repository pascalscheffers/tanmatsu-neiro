// engine/bank_json.h — PresetPatch value object + JSON bank codec (ADR 0027).
//
// Control-path only: cJSON allocates from the heap during parse/serialize.
// NEVER call bank_json_parse()/bank_json_serialize_patch() from the audio
// thread (engine::process() and below) — this module is for boot-time /
// UI-triggered bank loads and offline authoring only.
//
// Bank schema (a bank is a JSON array of patch objects):
//   [ { "name": "Bass",
//       "params": { "<id-or-name>": <number>, ... },
//       "routes": [ { "source": <id-or-name>, "dest": <id-or-name-or-"pitch"/"pwm">,
//                     "depth": <float>, "curve": <id-or-name> }, ... ] },
//     ... ]
//
// "params" keys may be either the numeric ParamId (as a JSON object key
// string, e.g. "16") or the param's canonical name — ParamDesc::name, the
// full display name exactly as it appears in JUNO_PARAM_TABLE (e.g.
// "Osc Level"), NOT short_name. Values are PHYSICAL units (the same space
// engine_set_param() expects), matching FactoryPreset in preset.cpp.
//
// "routes" is optional; absent or empty means no routings. "source" and
// "curve" accept either a numeric enum id or the enum's name (ModSource /
// ModCurve, e.g. "LFO1", "SQR"). "dest" accepts a numeric ParamId, a param
// name, or the literal strings "pitch"/"pwm" for the virtual mod-matrix
// destinations (kModDestPitch/kModDestPwm).
//
// Fail-closed / forward-compat rules (mirrors preset.cpp's preset_parse):
//   - Unknown param id/name, or a param carrying FLAG_NO_PRESET: silently
//     skipped (not an error).
//   - Non-numeric param value: skipped.
//   - Route with unknown source, source == NONE, or depth == 0: dropped.
//   - Params capped at PRESET_MAX_PARAMS, routes capped at PRESET_MAX_ROUTINGS
//     (extras silently dropped, never overflowed).
//   - Malformed/truncated JSON, or a root that isn't an array: bank_json_parse
//     returns -1. Never crashes, never reads OOB.
#pragma once

#include <cstddef>
#include <cstdint>
#include "cJSON.h"
#include "mod_matrix.h"
#include "preset.h"  // PRESET_NAME_LEN, PRESET_MAX_PARAMS, PRESET_MAX_ROUTINGS

// Fixed-capacity preset value object — no heap, sized to the shared preset
// caps (engine/preset.h) so it can carry anything preset.cpp's factory bank
// can express, including the full modulation-routing set.
struct PresetPatch {
    char     name[PRESET_NAME_LEN];
    uint16_t ids[PRESET_MAX_PARAMS];
    float    vals[PRESET_MAX_PARAMS];
    int      count;
    Routing  routes[PRESET_MAX_ROUTINGS];
    int      route_count;
};

// Parse a JSON bank (array of patch objects) from `json`/`len` into `out`,
// filling up to `max_patches` entries.
// Returns the number of patches parsed (>= 0), or -1 if the root is not a
// JSON array or the JSON itself is malformed/unparseable.
int bank_json_parse(const char* json, size_t len, PresetPatch* out, int max_patches);

// Parse a single patch JSON object (as produced by bank_json_parse's per-
// element iteration, or a standalone object) into `out`. Applies the same
// fail-closed/forward-compat rules as bank_json_parse. Returns true on
// success (even if some fields were skipped), false if `obj`/`out` are null
// or `obj` is not a JSON object.
bool bank_json_parse_patch(const cJSON* obj, PresetPatch* out);

// Serialize one PresetPatch as a single JSON object (not wrapped in an
// array) into `buf` (max `buf_max` bytes, NUL-terminated on success).
// Params whose id carries FLAG_NO_PRESET are skipped. Returns the number of
// bytes written (excluding the NUL terminator), or -1 if `buf` is too small
// or `p`/`buf` are null.
int bank_json_serialize_patch(const PresetPatch* p, char* buf, size_t buf_max);
