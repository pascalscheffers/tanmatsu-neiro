// engine/preset.cpp — preset serialisation + factory bank (Stage 2d, bumped 3b-ii).
#include "preset.h"
#include <math.h>
#include <string.h>
#include "bank_json.h"
#include "factory_bank.h"
#include "mod_matrix.h"
#include "param_desc.h"
#include "param_id.h"

// ---------------------------------------------------------------------------
// Curve helper (mirrors ParamStore::apply_curve; kept local, no coupling)
// ---------------------------------------------------------------------------
static float apply_curve_local(const ParamDesc& d, float norm) {
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    switch (d.curve) {
        case CURVE_LIN:
            return d.min + norm * (d.max - d.min);
        case CURVE_EXP:
            if (d.min > 0.0f && d.max > 0.0f) return d.min * powf(d.max / d.min, norm);
            return d.min + norm * (d.max - d.min);
        case CURVE_LOG: {
            const float t = log2f(1.0f + norm);
            return d.min + t * (d.max - d.min);
        }
        case CURVE_STEPPED: {
            const int steps = (int)(d.max - d.min);
            int       step  = (int)(norm * (float)(steps + 1));
            if (step > steps) step = steps;
            if (step < 0) step = 0;
            return d.min + (float)step;
        }
    }
    return d.min + norm * (d.max - d.min);
}

static const ParamDesc* find_param_desc(uint16_t id) {
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].id == id) return &JUNO_PARAM_TABLE[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Factory bank — embedded JSON (WO-13-neiro-bank, ADR 0027)
// ---------------------------------------------------------------------------
//
// The 12 Neiro factory patches live in engine/banks/neiro_factory.json (the
// single source of truth — see that file's history for the original
// hardcoded-array provenance). factory_bank_neiro_json() (engine/factory_bank.h)
// resolves to those exact bytes on every build (device: EMBED_TXTFILES;
// host/test: a generated raw-string-literal wrapper). Parsed once, lazily, on
// first use — control-path only (boot/UI), never called from engine::process().
//
// WO-13d (ADR 0026): ENV2->cutoff, LFO1->PWM, and LFO1->pitch are direct Juno
// panel paths (VCF_ENV_DEPTH, PWM_MODE/OSC_PWM, DCO_LFO_DEPTH — see
// juno_voice.cpp render()), so none of the 12 patches carry matrix routes.

// Headroom above the current 12 patches for future growth without a format
// change; bank_json_parse() silently caps at this count if the bank ever grows
// past it.
static constexpr int kNeiroBankMaxPatches = 16;

static PresetPatch g_neiro[kNeiroBankMaxPatches];
static int         g_neiro_count       = -1;  // -1 = not yet loaded
static bool        g_neiro_load_failed = false;

// Lazily parses the embedded bank on first use. Idempotent; safe to call from
// every preset_factory_* entry point.
static void ensure_neiro_bank_loaded(void) {
    if (g_neiro_count >= 0 || g_neiro_load_failed) return;
    size_t      len  = 0;
    const char* json = factory_bank_neiro_json(&len);
    int         n    = (json && len > 0) ? bank_json_parse(json, len, g_neiro, kNeiroBankMaxPatches) : -1;
    if (n < 0) {
        g_neiro_load_failed = true;  // fail closed: preset_factory_count() reports 0
        return;
    }
    g_neiro_count = n;
}

int preset_factory_count(void) {
    ensure_neiro_bank_loaded();
    return g_neiro_count < 0 ? 0 : g_neiro_count;
}

const char* preset_factory_name(int idx) {
    ensure_neiro_bank_loaded();
    if (idx < 0 || idx >= preset_factory_count()) return "";
    return g_neiro[idx].name;
}

int preset_factory_default(void) {
    static const char* k_default_name = "Solo Lead";
    ensure_neiro_bank_loaded();
    const int count = preset_factory_count();
    for (int i = 0; i < count; i++) {
        if (strcmp(g_neiro[i].name, k_default_name) == 0) return i;
    }
    return 0;  // named patch missing — fall back to INIT
}

int preset_factory_params(int idx, uint16_t* ids_out, float* vals_out, int max_count) {
    ensure_neiro_bank_loaded();
    if (idx < 0 || idx >= preset_factory_count()) return -1;
    const PresetPatch& p = g_neiro[idx];
    int                n = (p.count < max_count) ? p.count : max_count;
    for (int i = 0; i < n; i++) {
        ids_out[i]  = p.ids[i];
        vals_out[i] = p.vals[i];
    }
    return n;
}

int preset_factory_routings(int idx, Routing* routings_out, int max_count) {
    ensure_neiro_bank_loaded();
    if (idx < 0 || idx >= preset_factory_count()) return -1;
    const PresetPatch& p = g_neiro[idx];
    if (p.route_count == 0) return 0;
    int n = (p.route_count < max_count) ? p.route_count : max_count;
    for (int i = 0; i < n; i++) {
        routings_out[i] = p.routes[i];
    }
    return n;
}

// ---------------------------------------------------------------------------
// Serialisation helpers (explicit byte-level I/O avoids alignment/ABI issues)
// ---------------------------------------------------------------------------
static void wr_u8(uint8_t** p, uint8_t v) {
    **p = v;
    (*p)++;
}
static void wr_u16(uint8_t** p, uint16_t v) {
    memcpy(*p, &v, 2);
    (*p) += 2;
}
static void wr_f32(uint8_t** p, float v) {
    memcpy(*p, &v, 4);
    (*p) += 4;
}

static uint8_t rd_u8(const uint8_t** p) {
    uint8_t v = **p;
    (*p)++;
    return v;
}
static uint16_t rd_u16(const uint8_t** p) {
    uint16_t v;
    memcpy(&v, *p, 2);
    (*p) += 2;
    return v;
}
static float rd_f32(const uint8_t** p) {
    float v;
    memcpy(&v, *p, 4);
    (*p) += 4;
    return v;
}

// ---------------------------------------------------------------------------
// Public serialisation API
// ---------------------------------------------------------------------------
int preset_serialize(void* buf, size_t buf_max, const char* name, const float* norms, int norms_len,
                     const Routing* routings, int routings_len) {
    size_t param_count = 0;
    for (int i = 0; i < kJunoParamCount; i++) {
        if ((JUNO_PARAM_TABLE[i].flags & FLAG_NO_PRESET) == 0) param_count++;
    }
    const int    r_count = (routings && routings_len > 0) ? routings_len : 0;
    // header(42) + params(6 each) + routing_count(2) + routings(8 each)
    const size_t need    = 42u + param_count * 6u + 2u + (size_t)r_count * 8u;
    if (buf_max < need) return -1;

    uint8_t* p = (uint8_t*)buf;

    // Header
    memcpy(p, "TNMT", 4);
    p += 4;
    wr_u8(&p, PRESET_FORMAT_VERSION);  // v2
    wr_u8(&p, PRESET_MODEL_JUNO);
    wr_u16(&p, 0u);  // flags, reserved

    // Name (32 bytes, null-padded)
    char name_buf[PRESET_NAME_LEN] = {};
    if (name) strncpy(name_buf, name, PRESET_NAME_LEN - 1);
    memcpy(p, name_buf, PRESET_NAME_LEN);
    p += PRESET_NAME_LEN;

    // Count
    wr_u16(&p, (uint16_t)param_count);

    // Param entries: norm → physical via the table's curve
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        if ((d.flags & FLAG_NO_PRESET) != 0) continue;
        float norm = (d.id < (uint16_t)norms_len) ? norms[d.id] : 0.0f;
        float phys = apply_curve_local(d, norm);
        wr_u16(&p, d.id);
        wr_f32(&p, phys);
    }

    // Routings block (v2): count + records, field-by-field (1+2+4+1 = 8 bytes each)
    wr_u16(&p, (uint16_t)r_count);
    for (int i = 0; i < r_count; i++) {
        wr_u8(&p, routings[i].source);
        wr_u16(&p, routings[i].dest_param_id);
        wr_f32(&p, routings[i].depth);
        wr_u8(&p, routings[i].curve);
    }

    return (int)(p - (uint8_t*)buf);
}

int preset_parse(const void* buf, size_t len, char* name_out, int name_max, uint16_t* ids_out, float* vals_out,
                 int max_count, Routing* routings_out, int max_routings, int* routings_count_out) {
    static constexpr size_t kHeaderSize = 42u;
    if (len < kHeaderSize) return -1;

    const uint8_t* p   = (const uint8_t*)buf;
    const uint8_t* end = p + len;

    // Magic
    if (memcmp(p, "TNMT", 4) != 0) return -1;
    p += 4;

    uint8_t version  = rd_u8(&p);
    uint8_t model_id = rd_u8(&p);
    rd_u16(&p);  // flags, ignored

    // Accept v1 (no routings block) and v2 (with routings block).
    if ((version != 1 && version != PRESET_FORMAT_VERSION) || model_id != PRESET_MODEL_JUNO) return -1;

    // Name
    if (name_out && name_max > 0) {
        int copy = (name_max - 1 < PRESET_NAME_LEN) ? name_max - 1 : PRESET_NAME_LEN;
        memcpy(name_out, p, copy);
        name_out[copy]         = '\0';
        // Ensure null-termination at the actual string end.
        name_out[name_max - 1] = '\0';
    }
    p += PRESET_NAME_LEN;

    uint16_t count = rd_u16(&p);

    // Validate enough bytes remain for the declared param count.
    if (len < kHeaderSize + (size_t)count * 6u) return -1;

    int n = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t         id  = rd_u16(&p);
        float            val = rd_f32(&p);
        const ParamDesc* d   = find_param_desc(id);
        // Unknown IDs are forward-compatible, while known session-only rows must never
        // be restored even if a crafted or future blob contains them.
        if (d && (d->flags & FLAG_NO_PRESET) == 0 && n < max_count) {
            ids_out[n]  = id;
            vals_out[n] = val;
            n++;
        }
    }

    // Routings block (v2 only — v1 blobs end here).
    int r_out = 0;
    if (version == 2 && p < end) {
        // Need at least 2 bytes for the routing count.
        if ((size_t)(end - p) >= 2u) {
            uint16_t r_count = rd_u16(&p);
            // Each routing = 1+2+4+1 = 8 bytes.
            for (uint16_t i = 0; i < r_count; i++) {
                if ((size_t)(end - p) < 8u) break;  // truncated — stop safely
                uint8_t  src   = rd_u8(&p);
                uint16_t dest  = rd_u16(&p);
                float    depth = rd_f32(&p);
                uint8_t  curve = rd_u8(&p);
                // Skip unknown source ids (forward-compat).
                if (src >= (uint8_t)ModSource::_COUNT && src != 0) {
                    // unknown source: skip (dest/depth/curve already consumed above)
                    continue;
                }
                if (routings_out && r_out < max_routings) {
                    routings_out[r_out].source        = src;
                    routings_out[r_out].dest_param_id = dest;
                    routings_out[r_out].depth         = depth;
                    routings_out[r_out].curve         = curve;
                    r_out++;
                }
            }
        }
    }

    if (routings_count_out) *routings_count_out = r_out;
    return n;
}
