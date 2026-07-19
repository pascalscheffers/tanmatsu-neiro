// engine/preset.h — preset serialisation for the Juno model (Stage 2d, bumped 3b-ii).
//
// Pure functions: no engine calls, no platform I/O. Callers (ui/ or app/)
// apply the parsed values via engine_set_param() / engine_set_routings() and
// persist via platform_storage_save/load() (platform.h).
//
// Wire format v2 (little-endian, packed):
//   bytes 0–3   : magic "TNMT"
//   byte  4     : format_version (2)
//   byte  5     : model_id (1 = juno106)
//   bytes 6–7   : flags (reserved, 0)
//   bytes 8–39  : name, 32 bytes, null-padded
//   bytes 40–41 : param count (uint16_t)
//   bytes 42+   : count × { uint16_t id; float value }  (6 bytes each)
//   after params: routing count (uint16_t)
//                 count × { uint8_t source; uint16_t dest_param_id; float depth; uint8_t curve }
//                          (8 bytes each, field-by-field — no struct memcpy)
//
// Back-compat: v1 blobs (no routings block) still parse — zero routings returned.
// Forward-compat: unknown param IDs, session-only params, and unknown source/curve ids are
// silently skipped.
#pragma once

#include <cstddef>
#include <cstdint>
#include "mod_matrix.h"

static constexpr int     PRESET_NAME_LEN       = 32;
// v2 max: 42 (header) + 51*6 (preset-eligible params) + 2 (routing count) +
// 16*8 (16 routing slots) = 476
// 512 gives headroom for future params without a format-version bump.
static constexpr size_t  PRESET_BLOB_MAX       = 512;
static constexpr uint8_t PRESET_FORMAT_VERSION = 2;
static constexpr uint8_t PRESET_MODEL_JUNO     = 1;

// Maximum number of routing slots returned by preset_parse_routings().
static constexpr int PRESET_MAX_ROUTINGS = kMaxRoutes;

// Maximum number of params a preset can carry — buffers passed to
// preset_factory_params()/preset_parse() must hold at least this many, or trailing
// params (PLAY_MODE, UNISON, CHORUS_MODE, ARP_*, …) are silently dropped and the
// patch loads at table defaults. Must cover all preset-eligible rows (currently 51); the
// 512-byte blob format tops out near 78 params, so 96 is safe with headroom.
static constexpr int PRESET_MAX_PARAMS = 96;

// ---------------------------------------------------------------------------
// Factory presets (WO-13-neiro-bank: embedded JSON bank, ADR 0027 — no
// storage/engine calls required. See engine/banks/neiro_factory.json and
// engine/factory_bank.h. Parsed lazily on first call to any preset_factory_*
// function below; the bank is fixed-capacity and control-path only.)
// ---------------------------------------------------------------------------

int         preset_factory_count(void);
const char* preset_factory_name(int idx);

// Index of the patch loaded at boot when no user preset is stored. Resolved by
// name so reordering the factory bank can't silently change the boot sound;
// falls back to 0 (INIT) if the named patch is missing.
int preset_factory_default(void);

// Fill `ids_out`/`vals_out` with physical param values for factory preset `idx`.
// Both arrays must hold at least `max_count` entries.
// Returns the number of entries written, or -1 if idx is out of range.
int preset_factory_params(int idx, uint16_t* ids_out, float* vals_out, int max_count);

// Fill `routings_out` with the modulation routings for factory preset `idx`.
// `routings_out` must hold at least `max_count` entries (PRESET_MAX_ROUTINGS is safe).
// Returns the number of routings written (may be 0), or -1 if idx is out of range.
int preset_factory_routings(int idx, Routing* routings_out, int max_count);

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

// Encode the UI normalised-value shadow + routings into a blob (format v2).
// Applies each param's curve (norm → physical) via JUNO_PARAM_TABLE.
// `routings`: array of routing slots to embed; `routings_len` entries serialized.
//   Pass NULL/0 to omit the routings block (writes count=0; still v2 format).
// Returns byte count written, or -1 if buf_max is too small.
int preset_serialize(void* buf, size_t buf_max, const char* name, const float* norms, int norms_len,
                     const Routing* routings, int routings_len);

// Parse a preset blob into physical param values and modulation routings.
// `name_out` receives the preset name (null-terminated, at most name_max bytes).
// `ids_out`/`vals_out` receive physical param values (max_count entries max).
// `routings_out` receives modulation routings (max_routings entries max); may be NULL.
// `routings_count_out` receives the number of routings parsed; may be NULL.
// Unknown param IDs, known FLAG_NO_PRESET params, and unknown source/curve ids are silently skipped.
// v1 blobs (no routings block): succeeds, *routings_count_out = 0.
// Returns param count parsed (≥ 0), or -1 on bad/unsupported format.
int preset_parse(const void* buf, size_t len, char* name_out, int name_max, uint16_t* ids_out, float* vals_out,
                 int max_count, Routing* routings_out, int max_routings, int* routings_count_out);
