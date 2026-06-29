// ui/ui_presets_state.cpp — preset-browser state machine (WO-3).
//
// Snapshot, audition, revert, confirm — pure UIState manipulation + engine
// calls.  No PAX dependency; this file is testable without a display context.
// Drawing lives in ui_presets.cpp (separate compilation unit).
#include "ui_presets_state.h"
#include <math.h>
#include <string.h>
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "engine/synth.h"
#include "platform.h"

// "User" row sentinel: one past the last factory index.
static inline int user_row_idx(void) {
    return preset_factory_count();
}

// Total number of rows: factory count + 1 User entry.
static inline int total_rows(void) {
    return preset_factory_count() + 1;
}

// ---------------------------------------------------------------------------
// Param curve inversion — physical value → normalised [0,1].
// Mirrors ui.cpp; kept local so ui_presets_state is self-contained.
// ---------------------------------------------------------------------------
static float phys_to_norm_local(const ParamDesc& d, float v) {
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    float range = d.max - d.min;
    if (range <= 0.0f) return 0.0f;
    switch (d.curve) {
        case CURVE_LIN:
        case CURVE_STEPPED:
            return (v - d.min) / range;
        case CURVE_EXP:
            if (d.min > 0.0f && d.max > 0.0f) {
                float lr = logf(d.max / d.min);
                return (lr > 0.0f) ? logf(v / d.min) / lr : 0.0f;
            }
            return (v - d.min) / range;
        case CURVE_LOG:
            return powf(2.0f, (v - d.min) / range) - 1.0f;
    }
    return (v - d.min) / range;
}

static float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// ---------------------------------------------------------------------------
// Apply physical param values to the engine + sync the norms shadow.
// ---------------------------------------------------------------------------
static void apply_params(UIState* s, const char* name, int idx, const uint16_t* ids, const float* vals, int count) {
    for (int i = 0; i < count; i++) {
        engine_set_param(ids[i], vals[i]);
        if (ids[i] < UI_NORM_TABLE_SIZE) {
            for (int j = 0; j < kJunoParamCount; j++) {
                if (JUNO_PARAM_TABLE[j].id == ids[i]) {
                    s->norms[ids[i]] = phys_to_norm_local(JUNO_PARAM_TABLE[j], vals[i]);
                    break;
                }
            }
        }
    }
    strncpy(s->preset_name, name, sizeof(s->preset_name) - 1);
    s->preset_name[sizeof(s->preset_name) - 1] = '\0';
    s->preset_idx                              = idx;
}

// ---------------------------------------------------------------------------
// Audition helpers — load a preset into the engine + UIState.
// ---------------------------------------------------------------------------
static void audition_factory(UIState* s, int row) {
    uint16_t ids[64];
    float    vals[64];
    int      count = preset_factory_params(row, ids, vals, 64);
    if (count > 0) {
        apply_params(s, preset_factory_name(row), row, ids, vals, count);
    }
    Routing routings[PRESET_MAX_ROUTINGS];
    int     r_count = preset_factory_routings(row, routings, PRESET_MAX_ROUTINGS);
    engine_set_routings(routings, r_count);
}

static void audition_user(UIState* s) {
    static uint8_t blob[PRESET_BLOB_MAX];
    int            bytes = platform_storage_load("user", blob, sizeof(blob));
    if (bytes <= 0) return;
    static uint16_t ids[64];
    static float    vals[64];
    Routing         routings[PRESET_MAX_ROUTINGS];
    int             r_count = 0;
    char            name[PRESET_NAME_LEN + 1];
    int             count =
        preset_parse(blob, (size_t)bytes, name, sizeof(name), ids, vals, 64, routings, PRESET_MAX_ROUTINGS, &r_count);
    if (count > 0) {
        apply_params(s, name, -1, ids, vals, count);
        engine_set_routings(routings, r_count);
    }
}

static void audition_current_row(UIState* s) {
    if (s->row == user_row_idx()) {
        audition_user(s);
    } else {
        audition_factory(s, s->row);
    }
    s->auditioning = true;
}

// ---------------------------------------------------------------------------
// Revert: restore the snapshot to the engine + UIState.
// ---------------------------------------------------------------------------
static void revert_snapshot(UIState* s) {
    if (!s->auditioning) return;
    for (int j = 0; j < kJunoParamCount; j++) {
        uint16_t id = JUNO_PARAM_TABLE[j].id;
        if (id < UI_NORM_TABLE_SIZE) {
            float norm   = clamp01(s->audition_snapshot[id]);
            s->norms[id] = norm;
            engine_set_param_norm(id, norm);
        }
    }
    strncpy(s->preset_name, s->audition_preset_name, sizeof(s->preset_name) - 1);
    s->preset_name[sizeof(s->preset_name) - 1] = '\0';
    s->preset_idx                              = s->audition_preset_idx;
    s->auditioning                             = false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" void ui_presets_snapshot(UIState* s) {
    memcpy(s->audition_snapshot, s->norms, sizeof(s->norms));
    strncpy(s->audition_preset_name, s->preset_name, sizeof(s->audition_preset_name));
    s->audition_preset_name[sizeof(s->audition_preset_name) - 1] = '\0';
    s->audition_preset_idx                                       = s->preset_idx;
    s->auditioning                                               = false;
}

extern "C" bool ui_presets_handle_event(UIState* s, const platform_event_t* ev) {
    if (ev->type != PLATFORM_EV_KEY || !ev->pressed) return false;

    int n = total_rows();

    switch (ev->key) {
        case PLATFORM_KEY_UP: {
            if (!s->auditioning) {
                ui_presets_snapshot(s);
            }
            s->row = (s->row - 1 + n) % n;
            audition_current_row(s);
            return true;
        }
        case PLATFORM_KEY_DOWN: {
            if (!s->auditioning) {
                ui_presets_snapshot(s);
            }
            s->row = (s->row + 1) % n;
            audition_current_row(s);
            return true;
        }

        // Confirm: F4 (circle) or Enter/CR/LF.
        case PLATFORM_KEY_F4:
        case '\n':
        case '\r': {
            ui_presets_snapshot(s);
            return true;
        }

        // Back / revert: F3 (square) or Esc.
        case PLATFORM_KEY_F3:
        case '\x1B': {
            revert_snapshot(s);
            return true;
        }

        // Page navigation: revert first, return false to pass through.
        case PLATFORM_KEY_LEFT:
        case PLATFORM_KEY_RIGHT: {
            revert_snapshot(s);
            return false;
        }

        default:
            return false;
    }
}
