// engine/preset.h — preset serialisation for the Juno model (Stage 2d).
//
// Pure functions: no engine calls, no platform I/O. Callers (ui/ or app/)
// apply the parsed values via engine_set_param() and persist via
// platform_storage_save/load() (platform.h).
//
// Wire format v1 (little-endian, packed):
//   bytes 0–3   : magic "TNMT"
//   byte  4     : format_version (1)
//   byte  5     : model_id (1 = juno106)
//   bytes 6–7   : flags (reserved, 0)
//   bytes 8–39  : name, 32 bytes, null-padded
//   bytes 40–41 : count (uint16_t, number of param entries)
//   bytes 42+   : count × { uint16_t id; float value }  (6 bytes each)
//
// Forward-compat: unknown param IDs from a newer version are silently skipped;
// the caller fills missing IDs from table defaults (spec 05).
#pragma once

#include <cstddef>
#include <cstdint>

static constexpr int     PRESET_NAME_LEN        = 32;
static constexpr size_t  PRESET_BLOB_MAX        = 256;
static constexpr uint8_t PRESET_FORMAT_VERSION  = 1;
static constexpr uint8_t PRESET_MODEL_JUNO      = 1;

// ---------------------------------------------------------------------------
// Factory presets (hardcoded; no storage, no engine calls required)
// ---------------------------------------------------------------------------

int         preset_factory_count(void);
const char* preset_factory_name(int idx);

// Fill `ids_out`/`vals_out` with physical param values for factory preset `idx`.
// Both arrays must hold at least `max_count` entries.
// Returns the number of entries written, or -1 if idx is out of range.
int preset_factory_params(int idx,
                          uint16_t* ids_out, float* vals_out, int max_count);

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

// Encode the UI normalised-value shadow into a blob.
// Applies each param's curve (norm → physical) via JUNO_PARAM_TABLE.
// Returns byte count written, or -1 if buf_max is too small.
int preset_serialize(void* buf, size_t buf_max,
                     const char* name,
                     const float* norms, int norms_len);

// Parse a preset blob into physical param values.
// `name_out` receives the preset name (null-terminated, at most name_max bytes).
// `ids_out`/`vals_out` receive physical param values.
// Unknown IDs (newer format) are silently skipped.
// Returns param count parsed (≥ 0), or -1 on bad/unsupported format.
int preset_parse(const void* buf, size_t len,
                 char* name_out, int name_max,
                 uint16_t* ids_out, float* vals_out, int max_count);
