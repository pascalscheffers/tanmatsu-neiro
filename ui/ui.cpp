// ui/ui.cpp — Stage 2c: parameter-page UI rendered from the param table.
//
// Pages are derived from the groups present in JUNO_PARAM_TABLE — no
// model-specific knowledge here (ADR 0008). Arrows navigate pages/rows;
// comma/dot nudge the selected parameter; Shift = coarse step.
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "engine/mod_matrix.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "engine/synth.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "platform.h"

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define COL_BG      0xFF101018u
#define COL_TEXT    0xFFFFFFFFu
#define COL_DIM     0xFF7A7A8Au
#define COL_ACCENT  0xFF30C0FFu
#define COL_BAR_BG  0xFF2A2A3Au
#define COL_SEL_BG  0xFF16222Eu
#define COL_TAB_DIM 0xFF1A1A28u
#define COL_SEP     0xFF2A2A4Au

// ---------------------------------------------------------------------------
// Layout (800 × 480 device panel, ADR 0011)
// ---------------------------------------------------------------------------
#define SCREEN_W  800.0f
#define SCREEN_H  480.0f
#define TAB_H     40.0f
#define STATUS_H  38.0f
#define CONTENT_Y TAB_H
#define CONTENT_H (SCREEN_H - TAB_H - STATUS_H)
#define ROW_H     56.0f
#define NAME_X    48.0f
#define BAR_X     260.0f
#define BAR_W     360.0f
#define BAR_H     14.0f
#define VAL_X     634.0f
#define FONT_SM   14.0f
#define FONT_MD   18.0f

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static const char* group_name(uint8_t g) {
    switch (g) {
        case GROUP_OSC:
            return "OSC";
        case GROUP_FILTER:
            return "FILTER";
        case GROUP_HPF:
            return "HPF";
        case GROUP_ENV:
            return "ENV";
        case GROUP_ENV2:
            return "ENV2";
        case GROUP_LFO:
            return "LFO";
        case GROUP_FX:
            return "FX";
        case GROUP_AMP:
            return "AMP";
        case GROUP_GLOBAL:
            return "Clock";
        case GROUP_ARP:
            return "Arp";
        default:
            return "?";
    }
}

// Collect all params for a group into out[], table order. Returns count.
static int group_params(uint8_t group, const ParamDesc** out, int max_out) {
    int n = 0;
    for (int i = 0; i < kJunoParamCount && n < max_out; i++) {
        if ((uint8_t)JUNO_PARAM_TABLE[i].group == group) {
            out[n++] = &JUNO_PARAM_TABLE[i];
        }
    }
    return n;
}

// Inverse of ParamStore::apply_curve — physical value → normalised [0,1].
static float phys_to_norm(const ParamDesc& d, float v) {
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
        case CURVE_LOG: {
            // v = min + (max-min)*log2(1+norm)  =>  norm = 2^((v-min)/(max-min)) - 1
            return powf(2.0f, (v - d.min) / range) - 1.0f;
        }
    }
    return (v - d.min) / range;
}

static float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
// Apply physical param values to the engine + sync the norms shadow.
// Shared by factory-preset and user-preset loading paths.
static void ui_apply_params(UIState* s, const char* name, int preset_idx, const uint16_t* ids, const float* values,
                            int count) {
    for (int i = 0; i < count; i++) {
        engine_set_param(ids[i], values[i]);
        if (ids[i] < UI_NORM_TABLE_SIZE) {
            for (int j = 0; j < kJunoParamCount; j++) {
                if (JUNO_PARAM_TABLE[j].id == ids[i]) {
                    s->norms[ids[i]] = phys_to_norm(JUNO_PARAM_TABLE[j], values[i]);
                    break;
                }
            }
        }
    }
    strncpy(s->preset_name, name, sizeof(s->preset_name) - 1);
    s->preset_name[sizeof(s->preset_name) - 1] = '\0';
    s->preset_idx                              = preset_idx;
}

extern "C" void ui_state_init(UIState* s) {
    memset(s, 0, sizeof(*s));
    s->preset_idx = 0;
    strncpy(s->preset_name, "INIT", sizeof(s->preset_name) - 1);

    // Build page list: unique groups in table order.
    for (int i = 0; i < kJunoParamCount; i++) {
        uint8_t g     = (uint8_t)JUNO_PARAM_TABLE[i].group;
        bool    found = false;
        for (int j = 0; j < s->num_pages; j++) {
            if (s->page_groups[j] == g) {
                found = true;
                break;
            }
        }
        if (!found && s->num_pages < 8) {
            s->page_groups[s->num_pages++] = g;
        }
    }

    // Initialise normalised shadow values from table defaults.
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        if (d.id < UI_NORM_TABLE_SIZE) {
            s->norms[d.id] = phys_to_norm(d, d.def);
        }
    }

    // Load the INIT factory routings ("Clean 106") into the mod matrix at startup.
    // Overridden below if a stored user preset with routings is found.
    {
        Routing routings[PRESET_MAX_ROUTINGS];
        int     r_count = preset_factory_routings(0, routings, PRESET_MAX_ROUTINGS);
        engine_set_routings(routings, r_count);
    }

    // Try to restore the user preset from storage.
    static uint8_t blob[PRESET_BLOB_MAX];
    int            bytes = platform_storage_load("user", blob, sizeof(blob));
    if (bytes > 0) {
        static uint16_t ids[32];
        static float    vals[32];
        Routing         routings[PRESET_MAX_ROUTINGS];
        int             r_count = 0;
        char            name[PRESET_NAME_LEN + 1];
        int count = preset_parse(blob, (size_t)bytes, name, sizeof(name), ids, vals, 32, routings, PRESET_MAX_ROUTINGS,
                                 &r_count);
        if (count > 0) {
            ui_apply_params(s, name, -1, ids, vals, count);
            engine_set_routings(routings, r_count);
        }
    }
}

extern "C" bool ui_handle_event(UIState* s, const platform_event_t* ev) {
    if (ev->type != PLATFORM_EV_KEY || !ev->pressed) return false;

    const ParamDesc* rows[16];
    int              n = (s->num_pages > 0) ? group_params(s->page_groups[s->page], rows, 16) : 0;

    switch (ev->key) {
        case PLATFORM_KEY_UP:
            if (n > 0) s->row = (s->row - 1 + n) % n;
            return true;
        case PLATFORM_KEY_DOWN:
            if (n > 0) s->row = (s->row + 1) % n;
            return true;
        case PLATFORM_KEY_LEFT:
            if (s->num_pages > 0) {
                s->page = (s->page - 1 + s->num_pages) % s->num_pages;
                s->row  = 0;
            }
            return true;
        case PLATFORM_KEY_RIGHT:
            if (s->num_pages > 0) {
                s->page = (s->page + 1) % s->num_pages;
                s->row  = 0;
            }
            return true;
        case ',':
        case '.': {
            if (n <= 0 || s->row >= n) return true;
            const ParamDesc* d = rows[s->row];
            float            step;
            if (d->curve == CURVE_STEPPED) {
                step = 1.0f / (d->max - d->min);  // one integer step in norm space
            } else {
                step = (ev->mods & PLATFORM_MOD_SHIFT) ? 0.10f : 0.01f;
            }
            float norm      = clamp01(s->norms[d->id] + ((ev->key == '.') ? step : -step));
            s->norms[d->id] = norm;
            engine_set_param_norm(d->id, norm);
            return true;
        }
        case '[':
        case ']': {
            // Cycle through the factory bank.
            int total = preset_factory_count();
            if (total <= 0) return true;
            int next = s->preset_idx + ((ev->key == ']') ? 1 : -1);
            if (next < 0) next = total - 1;
            if (next >= total) next = 0;
            uint16_t ids[32];
            float    vals[32];
            int      count = preset_factory_params(next, ids, vals, 32);
            if (count > 0) {
                ui_apply_params(s, preset_factory_name(next), next, ids, vals, count);
            }
            // Load the factory routings into the mod matrix.
            Routing routings[PRESET_MAX_ROUTINGS];
            int     r_count = preset_factory_routings(next, routings, PRESET_MAX_ROUTINGS);
            engine_set_routings(routings, r_count);
            return true;
        }
        case '=': {
            // Save the current UI state (params + active routings) as the user preset.
            // Routings come from the INIT factory bank (index 0 = "Clean 106").
            // Full per-patch routing edit is a later stage; for now we persist whatever
            // the factory loaded so round-trips stay consistent.
            static uint8_t blob[PRESET_BLOB_MAX];
            Routing        routings[PRESET_MAX_ROUTINGS];
            int            r_count =
                preset_factory_routings(s->preset_idx >= 0 ? s->preset_idx : 0, routings, PRESET_MAX_ROUTINGS);
            int len =
                preset_serialize(blob, sizeof(blob), s->preset_name, s->norms, UI_NORM_TABLE_SIZE, routings, r_count);
            if (len > 0) {
                platform_storage_save("user", blob, (size_t)len);
            }
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
static void draw_tabs(pax_buf_t* fb, const UIState* s) {
    if (s->num_pages == 0) return;
    float tab_w = SCREEN_W / (float)s->num_pages;
    for (int i = 0; i < s->num_pages; i++) {
        bool sel = (i == s->page);
        pax_simple_rect(fb, sel ? COL_ACCENT : COL_TAB_DIM, (float)i * tab_w, 0.0f, tab_w - 1.0f, TAB_H);
        pax_draw_text(fb, sel ? COL_BG : COL_DIM, pax_font_sky_mono, FONT_MD, (float)i * tab_w + 10.0f,
                      (TAB_H - FONT_MD) * 0.5f, group_name(s->page_groups[i]));
    }
}

static void draw_rows(pax_buf_t* fb, const UIState* s) {
    if (s->num_pages == 0) return;
    const ParamDesc* rows[16];
    int              n = group_params(s->page_groups[s->page], rows, 16);
    if (n == 0) return;

    // How many rows fit in the content area (integer, floor).
    int visible = (int)(CONTENT_H / ROW_H);
    if (visible < 1) visible = 1;
    if (visible > n) visible = n;

    // Scroll to keep the selected row in view.  scroll_top is the index of
    // the first rendered row; clamp so the window never runs past the last row.
    int scroll_top = s->row - visible / 2;
    if (scroll_top < 0) scroll_top = 0;
    if (scroll_top + visible > n) scroll_top = n - visible;

    // Centre a full page (or partial if fewer rows than fit) in the content area.
    float block_h = (float)visible * ROW_H;
    float start_y = CONTENT_Y + (CONTENT_H - block_h) * 0.5f;
    if (start_y < CONTENT_Y) start_y = CONTENT_Y;

    for (int i = scroll_top; i < scroll_top + visible; i++) {
        const ParamDesc* d     = rows[i];
        float            row_y = start_y + (float)(i - scroll_top) * ROW_H;
        float            mid_y = row_y + ROW_H * 0.5f;
        bool             sel   = (i == s->row);

        if (sel) {
            pax_simple_rect(fb, COL_SEL_BG, 0.0f, row_y, SCREEN_W, ROW_H - 1.0f);
        }

        // Selection arrow.
        if (sel) {
            pax_draw_text(fb, COL_ACCENT, pax_font_sky_mono, FONT_MD, 8.0f, mid_y - FONT_MD * 0.5f, ">");
        }

        // Param name.
        pax_draw_text(fb, sel ? COL_TEXT : COL_DIM, pax_font_sky_mono, FONT_MD, NAME_X, mid_y - FONT_MD * 0.5f,
                      d->name);

        // Value bar.
        float norm   = (d->id < UI_NORM_TABLE_SIZE) ? s->norms[d->id] : 0.0f;
        float bar_y  = mid_y - BAR_H * 0.5f;
        float filled = norm * BAR_W;
        pax_simple_rect(fb, COL_BAR_BG, BAR_X, bar_y, BAR_W, BAR_H);
        if (filled > 0.5f) {
            pax_simple_rect(fb, sel ? COL_ACCENT : COL_DIM, BAR_X, bar_y, filled, BAR_H);
        }

        // Value text.
        char  val_buf[24];
        float phys = engine_get_param(d->id);
        if (d->display_fmt) {
            snprintf(val_buf, sizeof(val_buf), d->display_fmt, (double)phys);
        } else {
            snprintf(val_buf, sizeof(val_buf), "%.2f", (double)phys);
        }
        pax_draw_text(fb, sel ? COL_TEXT : COL_DIM, pax_font_sky_mono, FONT_MD, VAL_X, mid_y - FONT_MD * 0.5f, val_buf);

        // Unit label.
        const char* unit_str = nullptr;
        switch (d->unit) {
            case UNIT_HZ:
                unit_str = "Hz";
                break;
            case UNIT_PCT:
                unit_str = "%";
                break;
            case UNIT_DB:
                unit_str = "dB";
                break;
            case UNIT_SEMI:
                unit_str = "st";
                break;
            case UNIT_SEC:
                unit_str = "s";
                break;
            case UNIT_MS:
                unit_str = "ms";
                break;
            default:
                break;
        }
        if (unit_str) {
            pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, VAL_X + 80.0f, mid_y - FONT_SM * 0.5f, unit_str);
        }
    }
}

static void draw_status(pax_buf_t* fb, const UIState* s) {
    float y = SCREEN_H - STATUS_H;
    pax_simple_rect(fb, COL_SEP, 0.0f, y, SCREEN_W, 1.0f);
    pax_simple_rect(fb, COL_BG, 0.0f, y + 1.0f, SCREEN_W, STATUS_H - 1.0f);

    float text_y = y + (STATUS_H - FONT_SM) * 0.5f;

    // Voice activity dots.
    for (int i = 0; i < 8; i++) {
        uint32_t col = (i < s->active_voices) ? COL_ACCENT : COL_BAR_BG;
        pax_simple_rect(fb, col, 12.0f + (float)i * 14.0f, y + (STATUS_H - 10.0f) * 0.5f, 10.0f, 10.0f);
    }

    // Octave.
    char buf[16];
    snprintf(buf, sizeof(buf), "Oct %d", s->octave);
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, 140.0f, text_y, buf);

    // Preset name.
    pax_draw_text(fb, COL_TEXT, pax_font_sky_mono, FONT_SM, 280.0f, text_y, s->preset_name);

    // Key hints.
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM - 2.0f, 430.0f, text_y,
                  "<>pg  ^v row  ,/.nudge  [/]preset  =save  ESC");
}

extern "C" void ui_draw(pax_buf_t* fb, uint64_t millis, const UIState* s) {
    (void)millis;
    pax_background(fb, COL_BG);
    draw_tabs(fb, s);
    draw_rows(fb, s);
    draw_status(fb, s);
}
