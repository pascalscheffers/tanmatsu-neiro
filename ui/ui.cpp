// ui/ui.cpp — Stage 2c: parameter-page UI rendered from the param table.
//
// Pages are defined by the static PAGE_TABLE (explicit order, multi-group pages
// supported). No model-specific knowledge here (ADR 0008). Arrows navigate
// pages/rows; F1/F2 nudge the selected parameter; F3 back; F6 save.
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <climits>
#include "engine/mod_matrix.h"
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "engine/synth.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "platform.h"
#include "ui_icons.h"
#include "ui_overlay.h"
#include "ui_page_table.h"
#include "ui_presets.h"

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define COL_BG      0xFF101018u
#define COL_TEXT    0xFFFFFFFFu
#define COL_DIM     0xFF7A7A8Au
#define COL_ACCENT  0xFF30C0FFu  // neon cyan
#define COL_ACCENT2 0xFFFF2D9Du  // neon magenta/pink
#define COL_BAR_BG  0xFF2A2A3Au
#define COL_SEL_BG  0xFF16222Eu
#define COL_TAB_DIM 0xFF1A1A28u
#define COL_SEP     0xFF2A2A4Au
#define COL_HDR_BG  0xFF1A1A2Eu  // section header background

// ---------------------------------------------------------------------------
// Layout (800 × 480 device panel, ADR 0011)
// ---------------------------------------------------------------------------
#define SCREEN_W  800.0f
#define SCREEN_H  480.0f
#define TAB_H     40.0f
#define STATUS_H  38.0f
#define CONTENT_Y TAB_H
#define CONTENT_H (SCREEN_H - TAB_H - STATUS_H)
#define ROW_H     43.0f  // tighter rows: floor(402/43)=9 fit without scroll
#define HEADER_H  18.0f  // section sub-header height (multi-group pages only)
#define NAME_X    48.0f
#define BAR_X     260.0f
#define BAR_W     360.0f
#define BAR_H     10.0f
#define VAL_X     634.0f
#define FONT_SM   12.0f
#define FONT_MD   16.0f

// Gradient approximation: 3-segment stacked rects cyan→magenta
// Each segment is 1/3 of bar width, color lerped at segment midpoints.
// Colors: segment 0 = COL_ACCENT, segment 2 = COL_ACCENT2.
// Mid segment: lerp ARGB channels at t=0.5.
static inline uint32_t lerp_col(uint32_t a, uint32_t b, float t) {
    uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF, aa = (a >> 24) & 0xFF;
    uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF, ba = (b >> 24) & 0xFF;
    uint32_t r  = (uint32_t)((float)ar + t * (float)((int)br - (int)ar));
    uint32_t g  = (uint32_t)((float)ag + t * (float)((int)bg - (int)ag));
    uint32_t bv = (uint32_t)((float)ab + t * (float)((int)bb - (int)ab));
    uint32_t av = (uint32_t)((float)aa + t * (float)((int)ba - (int)aa));
    return (av << 24) | (r << 16) | (g << 8) | bv;
}

// Draw a 3-segment horizontal gradient rect (cyan → mid → magenta).
static void draw_gradient_bar(pax_buf_t* fb, float x, float y, float w, float h, bool sel) {
    if (w < 0.5f) return;
    uint32_t c0  = sel ? COL_ACCENT : lerp_col(COL_ACCENT, COL_BG, 0.4f);
    uint32_t c2  = sel ? COL_ACCENT2 : lerp_col(COL_ACCENT2, COL_BG, 0.4f);
    uint32_t c1  = lerp_col(c0, c2, 0.5f);
    float    seg = w / 3.0f;
    pax_simple_rect(fb, c0, x, y, seg, h);
    pax_simple_rect(fb, c1, x + seg, y, seg, h);
    pax_simple_rect(fb, c2, x + seg * 2.0f, y, w - seg * 2.0f, h);
}

// ---------------------------------------------------------------------------
// Internal helpers (group_params, page_rows, PAGE_TABLE defined in ui_page_table.cpp)
// ---------------------------------------------------------------------------
static const char* group_name(uint8_t g) {
    switch (g) {
        case GROUP_OSC:
            return "OSC";
        case GROUP_FILTER:
            return "VCF";
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
            return "CLOCK";
        case GROUP_ARP:
            return "ARP";
        default:
            return "?";
    }
}

// ---------------------------------------------------------------------------
// Draw-item list: interleaves headers and param rows for multi-group pages.
// Items are purely for layout; selection semantics stay over row indices.
// ---------------------------------------------------------------------------
enum ItemKind : uint8_t {
    ITEM_HEADER = 0,
    ITEM_ROW    = 1
};

struct DrawItem {
    ItemKind         kind;
    const char*      header_label;  // ITEM_HEADER only
    const ParamDesc* row;           // ITEM_ROW only
    int              row_idx;       // global row index (for selection tracking)
};

// Build draw-item list for current page. Returns item count (≤ kMaxItems).
static const int kMaxItems = 32;
static int       build_items(int page_index, DrawItem* items) {
    if (page_index < 0 || page_index >= kNumPages) return 0;
    const PageDef& pd = PAGE_TABLE[page_index];
    if (pd.kind != PAGE_PARAMS) return 0;
    bool             multi = (pd.num_groups > 1);
    int              n     = 0;
    int              row_i = 0;
    const ParamDesc* group_rows[24];
    for (int g = 0; g < (int)pd.num_groups; g++) {
        if (n >= kMaxItems) break;
        if (multi) {
            items[n].kind         = ITEM_HEADER;
            items[n].header_label = group_name(pd.groups[g]);
            items[n].row          = nullptr;
            items[n].row_idx      = -1;
            n++;
        }
        int gc = group_params(pd.groups[g], group_rows, 24);
        for (int r = 0; r < gc && n < kMaxItems; r++) {
            items[n].kind         = ITEM_ROW;
            items[n].header_label = nullptr;
            items[n].row          = group_rows[r];
            items[n].row_idx      = row_i++;
            n++;
        }
    }
    return n;
}

// Total pixel height of item list (headers + rows).
static float items_height(const DrawItem* items, int n) {
    float h = 0.0f;
    for (int i = 0; i < n; i++) {
        h += (items[i].kind == ITEM_HEADER) ? HEADER_H : ROW_H;
    }
    return h;
}

// ---------------------------------------------------------------------------
// param curve inversion — physical value → normalised [0,1]
// ---------------------------------------------------------------------------
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
    // Clear all voices and arp state before loading a preset so no gated voice
    // from the previous preset survives the switch (stuck-tone / accumulate-mute).
    // engine_all_notes_off() is lock-free (atomic flag); safe to call from the
    // control thread; the audio thread consumes it at the top of the next block.
    engine_all_notes_off();
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
    // page=0 (PRESET), row=0 — set by memset above.

    // Initialise normalised shadow values from table defaults.
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        if (d.id < UI_NORM_TABLE_SIZE) {
            s->norms[d.id] = phys_to_norm(d, d.def);
        }
    }

    // Default boot patch: load the chosen factory preset (params + routings) so
    // the synth comes up playing it. Overridden below if a stored user preset
    // is found.
    {
        int      def = preset_factory_default();
        uint16_t ids[PRESET_MAX_PARAMS];
        float    vals[PRESET_MAX_PARAMS];
        int      count = preset_factory_params(def, ids, vals, PRESET_MAX_PARAMS);
        if (count > 0) {
            ui_apply_params(s, preset_factory_name(def), def, ids, vals, count);
        }
        Routing routings[PRESET_MAX_ROUTINGS];
        int     r_count = preset_factory_routings(def, routings, PRESET_MAX_ROUTINGS);
        engine_set_routings(routings, r_count);
    }

    // Try to restore the user preset from storage.
    static uint8_t blob[PRESET_BLOB_MAX];
    int            bytes = platform_storage_load("user", blob, sizeof(blob));
    if (bytes > 0) {
        static uint16_t ids[PRESET_MAX_PARAMS];
        static float    vals[PRESET_MAX_PARAMS];
        Routing         routings[PRESET_MAX_ROUTINGS];
        int             r_count = 0;
        char            name[PRESET_NAME_LEN + 1];
        int count = preset_parse(blob, (size_t)bytes, name, sizeof(name), ids, vals, PRESET_MAX_PARAMS, routings,
                                 PRESET_MAX_ROUTINGS, &r_count);
        if (count > 0) {
            ui_apply_params(s, name, -1, ids, vals, count);
            engine_set_routings(routings, r_count);
        }
    }

    // Boot lands on page 0 (PRESET) with the active preset committed (not auditioning).
    // Set cursor to the active factory preset (or last row for user preset).
    s->row = (s->preset_idx >= 0) ? s->preset_idx : preset_factory_count();
    ui_presets_snapshot(s);

    // Force first paint: start change_seq at 1 so the render task (which starts
    // from last-seen 0) immediately paints the first frame.
    s->change_seq  = 1;
    s->last_voices = -1;
    s->last_octave = INT_MIN;
}

// ---------------------------------------------------------------------------
// Nudge / save helpers (WO-5)
// ---------------------------------------------------------------------------

// Apply one fine nudge step (dir = -1 or +1) to the currently selected param.
// For stepped params: one integer step in norm space.
// For continuous params: fine step = 0.01 norm.
static void nudge_selected(UIState* s, int dir, bool coarse) {
    const ParamDesc* rows[24];
    int              n = page_rows(s->page, rows, 24);
    if (n <= 0 || s->row < 0 || s->row >= n) return;
    const ParamDesc* d = rows[s->row];
    float            step;
    if (d->curve == CURVE_STEPPED) {
        step = 1.0f / (d->max - d->min);  // one integer step in norm space
    } else {
        step = coarse ? 0.10f : 0.01f;
    }
    float norm      = clamp01(s->norms[d->id] + (float)dir * step);
    s->norms[d->id] = norm;
    engine_set_param_norm(d->id, norm);
}

// Apply a raw norm delta to the selected param and clamp.
// Used by ui_tick() to drive continuous repeat without re-deriving step size.
static void nudge_selected_norm(UIState* s, float delta) {
    const ParamDesc* rows[24];
    int              n = page_rows(s->page, rows, 24);
    if (n <= 0 || s->row < 0 || s->row >= n) return;
    const ParamDesc* d    = rows[s->row];
    float            norm = clamp01(s->norms[d->id] + delta);
    s->norms[d->id]       = norm;
    engine_set_param_norm(d->id, norm);
}

// Save the current UI state as the user preset ("=" shortcut + F6 shape button).
static void save_user_preset(UIState* s) {
    static uint8_t blob[PRESET_BLOB_MAX];
    Routing        routings[PRESET_MAX_ROUTINGS];
    int r_count = preset_factory_routings(s->preset_idx >= 0 ? s->preset_idx : 0, routings, PRESET_MAX_ROUTINGS);
    int len     = preset_serialize(blob, sizeof(blob), s->preset_name, s->norms, UI_NORM_TABLE_SIZE, routings, r_count);
    if (len > 0) {
        platform_storage_save("user", blob, (size_t)len);
    }
}

// Begin a hold-repeat sequence in direction dir (-1 or +1).
static void hold_begin(UIState* s, int dir, uint64_t now_ms) {
    s->held_dir      = dir;
    s->held_row      = s->row;
    s->held_since_ms = now_ms;
    s->last_step_ms  = now_ms;
    s->repeat_accum  = 0.0f;
}

static void hold_stop(UIState* s) {
    s->held_dir = 0;
}

extern "C" bool ui_handle_event(UIState* s, const platform_event_t* ev) {
    if (ev->type != PLATFORM_EV_KEY) return false;

    // Handle F1/F2 release to stop hold-repeat (must happen before press-only guard).
    if (!ev->pressed) {
        if (ev->key == PLATFORM_KEY_F1 && s->held_dir == -1) {
            hold_stop(s);
            return true;
        }
        if (ev->key == PLATFORM_KEY_F2 && s->held_dir == +1) {
            hold_stop(s);
            return true;
        }
        return false;
    }

    // From here: pressed events only.

    // F5 (three-lobe): global key-guide overlay toggle — handled before page branching
    // so it works on any page and is never swallowed by the preset handler.
    if (ev->key == PLATFORM_KEY_F5) {
        s->show_keyguide = !s->show_keyguide;
        return true;
    }

    // Delegate all events on the preset page to ui_presets_handle_event().
    // Left/Right are special: the preset handler reverts then returns false so
    // we fall through to do the page switch below.
    if (PAGE_TABLE[s->page].kind == PAGE_PRESETS) {
        bool consumed = ui_presets_handle_event(s, ev);
        if (consumed) return true;
        // ui_presets_handle_event returned false for Left/Right after revert —
        // fall through to the page-switch logic.
    }

    const ParamDesc* rows[24];
    int              n = page_rows(s->page, rows, 24);

    // F-button shape actions on parameter pages only.
    if (PAGE_TABLE[s->page].kind == PAGE_PARAMS) {
        switch (ev->key) {
            case PLATFORM_KEY_F1:
                // X button: nudge down, begin hold-repeat.
                nudge_selected(s, -1, false);
                hold_begin(s, -1, platform_millis());
                return true;
            case PLATFORM_KEY_F2:
                // Triangle button: nudge up, begin hold-repeat.
                nudge_selected(s, +1, false);
                hold_begin(s, +1, platform_millis());
                return true;
            case PLATFORM_KEY_F3:
                // Square button: back to PRESET page (index 0).
                hold_stop(s);
                // Revert audition if we somehow arrive here (shouldn't happen on
                // PAGE_PARAMS, but guard anyway).
                s->page = 0;
                s->row  = 0;
                ui_presets_snapshot(s);
                return true;
            case PLATFORM_KEY_F4:
                // Circle button: no-op on parameter pages (confirm is preset-page only).
                return true;
            case PLATFORM_KEY_F6:
                // Diamond button: save user preset.
                save_user_preset(s);
                return true;
            default:
                break;
        }
    }

    switch (ev->key) {
        case PLATFORM_KEY_UP:
            if (n > 0) {
                hold_stop(s);
                s->row = (s->row - 1 + n) % n;
            }
            return true;
        case PLATFORM_KEY_DOWN:
            if (n > 0) {
                hold_stop(s);
                s->row = (s->row + 1) % n;
            }
            return true;
        case PLATFORM_KEY_LEFT: {
            // Revert audition if leaving the preset page.
            if (PAGE_TABLE[s->page].kind == PAGE_PRESETS && s->auditioning) {
                // Already reverted by ui_presets_handle_event above; just switch page.
            }
            hold_stop(s);
            s->page = (s->page - 1 + kNumPages) % kNumPages;
            s->row  = 0;
            return true;
        }
        case PLATFORM_KEY_RIGHT: {
            if (PAGE_TABLE[s->page].kind == PAGE_PRESETS && s->auditioning) {
                // Already reverted by ui_presets_handle_event above; just switch page.
            }
            hold_stop(s);
            s->page = (s->page + 1) % kNumPages;
            s->row  = 0;
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
            uint16_t ids[PRESET_MAX_PARAMS];
            float    vals[PRESET_MAX_PARAMS];
            int      count = preset_factory_params(next, ids, vals, PRESET_MAX_PARAMS);
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
            save_user_preset(s);
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Hold-to-repeat tick (WO-5)
// ---------------------------------------------------------------------------
// Constants for the repeat model:
//   - 250 ms initial delay before repeating starts
//   - Continuous: ramp rate 0.15 norm/s → 0.50 norm/s over 500 ms easing window
//     (full 0→1 sweep at max rate ≈ 2 s)
//   - Stepped: one step every 150 ms, no acceleration
#define HOLD_DELAY_MS    250u
#define HOLD_STEP_PERIOD 150u    // stepped param repeat interval (ms)
#define HOLD_RATE_MIN    0.15f   // continuous: initial repeat rate (norm/s)
#define HOLD_RATE_MAX    0.50f   // continuous: full-speed rate (norm/s)
#define HOLD_EASE_MS     500.0f  // duration of rate ramp (ms)

extern "C" void ui_tick(UIState* s, uint64_t now_ms) {
    if (s->held_dir == 0) return;
    if (PAGE_TABLE[s->page].kind != PAGE_PARAMS) {
        hold_stop(s);
        return;
    }

    // Stop if row changed since hold began.
    if (s->row != s->held_row) {
        hold_stop(s);
        return;
    }

    // Check initial delay.
    if (now_ms < s->held_since_ms + HOLD_DELAY_MS) return;

    // Determine param kind for the selected row.
    const ParamDesc* rows[24];
    int              n = page_rows(s->page, rows, 24);
    if (n <= 0 || s->row >= n) {
        hold_stop(s);
        return;
    }
    const ParamDesc* d = rows[s->row];

    uint64_t dt_ms = now_ms - s->last_step_ms;

    if (d->curve == CURVE_STEPPED) {
        // Stepped: emit one step every HOLD_STEP_PERIOD ms.
        if (dt_ms >= HOLD_STEP_PERIOD) {
            nudge_selected(s, s->held_dir, false);
            s->last_step_ms = now_ms;
        }
    } else {
        // Continuous: ramp from HOLD_RATE_MIN to HOLD_RATE_MAX over HOLD_EASE_MS,
        // measured from when repeating started (held_since_ms + HOLD_DELAY_MS).
        uint64_t repeat_age_ms = now_ms - (s->held_since_ms + HOLD_DELAY_MS);
        float    t             = (float)repeat_age_ms / HOLD_EASE_MS;
        if (t > 1.0f) t = 1.0f;
        float rate      = HOLD_RATE_MIN + t * (HOLD_RATE_MAX - HOLD_RATE_MIN);
        float delta     = (float)s->held_dir * rate * (float)dt_ms / 1000.0f;
        s->last_step_ms = now_ms;

        // Accumulate and apply when we've built up enough.
        s->repeat_accum += delta;
        if (s->repeat_accum > 0.001f || s->repeat_accum < -0.001f) {
            nudge_selected_norm(s, s->repeat_accum);
            s->repeat_accum = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
static void draw_tabs(pax_buf_t* fb, const UIState* s) {
    // Tab strip with neon cyan active tab + magenta underline glow.
    float tab_w = SCREEN_W / (float)kNumPages;
    for (int i = 0; i < kNumPages; i++) {
        bool sel = (i == s->page);
        // Tab background.
        pax_simple_rect(fb, sel ? COL_ACCENT : COL_TAB_DIM, (float)i * tab_w, 0.0f, tab_w - 1.0f,
                        sel ? TAB_H - 3.0f : TAB_H - 1.0f);
        // Magenta underline on selected tab.
        if (sel) {
            pax_simple_rect(fb, COL_ACCENT2, (float)i * tab_w, TAB_H - 3.0f, tab_w - 1.0f, 3.0f);
        }
        // Tab label — use FONT_SM for 9 tabs to fit across 800px (~88px each).
        pax_draw_text(fb, sel ? COL_BG : COL_DIM, pax_font_sky_mono, FONT_SM, (float)i * tab_w + 6.0f,
                      (TAB_H - FONT_SM) * 0.5f - 1.0f, PAGE_TABLE[i].title);
    }
    // Thin cyan→magenta gradient rule under the tab strip.
    float seg = SCREEN_W / 3.0f;
    pax_simple_rect(fb, COL_ACCENT, 0.0f, TAB_H - 1.0f, seg, 1.0f);
    pax_simple_rect(fb, lerp_col(COL_ACCENT, COL_ACCENT2, 0.5f), seg, TAB_H - 1.0f, seg, 1.0f);
    pax_simple_rect(fb, COL_ACCENT2, seg * 2.0f, TAB_H - 1.0f, SCREEN_W - seg * 2.0f, 1.0f);
}

// Draw a single section sub-header at given y (multi-group pages only).
static void draw_section_header(pax_buf_t* fb, float y, const char* label) {
    // Dim background strip.
    pax_simple_rect(fb, COL_HDR_BG, 0.0f, y, SCREEN_W, HEADER_H);
    // Thin magenta left accent bar.
    pax_simple_rect(fb, COL_ACCENT2, 0.0f, y, 3.0f, HEADER_H);
    // Separator line at top of header.
    pax_simple_rect(fb, COL_SEP, 0.0f, y, SCREEN_W, 1.0f);
    // Label text, small-caps style at FONT_SM.
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, NAME_X, y + (HEADER_H - FONT_SM) * 0.5f, label);
}

static void draw_rows(pax_buf_t* fb, const UIState* s) {
    // Build the mixed header+row item list for the current page.
    DrawItem items[kMaxItems];
    int      ni = build_items(s->page, items);
    if (ni == 0) return;

    float total_h = items_height(items, ni);

    // Determine the draw window. If everything fits, centre it. Otherwise scroll.
    float start_y;
    int   first_item;  // index into items[] of the first rendered item
    int   last_item;   // exclusive upper bound

    if (total_h <= CONTENT_H) {
        // Everything fits — centre and render all items.
        start_y    = CONTENT_Y + (CONTENT_H - total_h) * 0.5f;
        first_item = 0;
        last_item  = ni;
    } else {
        // Scroll: find the selected row item, then walk backward to find a good
        // first_item such that the selected row is centred / in view.
        // Strategy: find the y-offset of the selected row within the full block,
        // then pick start_y so it's centred, clamping so the block doesn't go
        // past the content edges.

        // Find the selected row item's offset from the top of the block.
        float sel_offset = 0.0f;
        bool  found      = false;
        {
            float acc = 0.0f;
            for (int i = 0; i < ni; i++) {
                float ih = (items[i].kind == ITEM_HEADER) ? HEADER_H : ROW_H;
                if (items[i].kind == ITEM_ROW && items[i].row_idx == s->row) {
                    sel_offset = acc + ROW_H * 0.5f;  // centre of the row
                    found      = true;
                    break;
                }
                acc += ih;
            }
        }
        if (!found) sel_offset = CONTENT_H * 0.5f;

        // Centre the selected row in the content area.
        float ideal_start = CONTENT_Y + CONTENT_H * 0.5f - sel_offset;
        // Clamp: don't show before the first item or leave gap after the last.
        if (ideal_start > CONTENT_Y) ideal_start = CONTENT_Y;
        if (ideal_start + total_h < CONTENT_Y + CONTENT_H) {
            ideal_start = CONTENT_Y + CONTENT_H - total_h;
        }
        start_y    = ideal_start;
        first_item = 0;
        last_item  = ni;
    }

    // Render items.
    float y = start_y;
    for (int i = first_item; i < last_item; i++) {
        float ih = (items[i].kind == ITEM_HEADER) ? HEADER_H : ROW_H;

        // Skip items that are fully outside the content area.
        if (y + ih <= CONTENT_Y || y >= CONTENT_Y + CONTENT_H) {
            y += ih;
            continue;
        }

        if (items[i].kind == ITEM_HEADER) {
            draw_section_header(fb, y, items[i].header_label);
        } else {
            const ParamDesc* d   = items[i].row;
            int              idx = items[i].row_idx;
            bool             sel = (idx == s->row);

            if (sel) {
                pax_simple_rect(fb, COL_SEL_BG, 0.0f, y, SCREEN_W, ROW_H - 1.0f);
            }

            float mid_y = y + ROW_H * 0.5f;

            // Selection arrow.
            if (sel) {
                pax_draw_text(fb, COL_ACCENT, pax_font_sky_mono, FONT_MD, 8.0f, mid_y - FONT_MD * 0.5f, ">");
            }

            // Param name.
            pax_draw_text(fb, sel ? COL_TEXT : COL_DIM, pax_font_sky_mono, FONT_MD, NAME_X, mid_y - FONT_MD * 0.5f,
                          d->name);

            // Value bar — cyan→magenta gradient fill.
            float norm   = (d->id < UI_NORM_TABLE_SIZE) ? s->norms[d->id] : 0.0f;
            float bar_y  = mid_y - BAR_H * 0.5f;
            float filled = norm * BAR_W;
            pax_simple_rect(fb, COL_BAR_BG, BAR_X, bar_y, BAR_W, BAR_H);
            draw_gradient_bar(fb, BAR_X, bar_y, filled, BAR_H, sel);

            // Value text.
            char  val_buf[24];
            float phys = engine_get_param(d->id);
            if (d->display_fmt) {
                snprintf(val_buf, sizeof(val_buf), d->display_fmt, (double)phys);
            } else {
                snprintf(val_buf, sizeof(val_buf), "%.2f", (double)phys);
            }
            pax_draw_text(fb, sel ? COL_TEXT : COL_DIM, pax_font_sky_mono, FONT_MD, VAL_X, mid_y - FONT_MD * 0.5f,
                          val_buf);

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

        y += ih;
    }
}

static void draw_status(pax_buf_t* fb, const UIState* s) {
    float y   = SCREEN_H - STATUS_H;
    // Thin magenta→cyan gradient rule at top of status bar (synthwave motif).
    float seg = SCREEN_W / 3.0f;
    pax_simple_rect(fb, COL_ACCENT2, 0.0f, y, seg, 1.0f);
    pax_simple_rect(fb, lerp_col(COL_ACCENT2, COL_ACCENT, 0.5f), seg, y, seg, 1.0f);
    pax_simple_rect(fb, COL_ACCENT, seg * 2.0f, y, SCREEN_W - seg * 2.0f, 1.0f);
    pax_simple_rect(fb, COL_BG, 0.0f, y + 1.0f, SCREEN_W, STATUS_H - 1.0f);

    float text_y = y + (STATUS_H - FONT_SM) * 0.5f;

    // Voice activity dots — lit in cyan, dimmed in bar-bg.
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

    // Key hints — icons for the badge button shapes, followed by action words.
    // Icon size matches text height; cy is vertically centred on the text row.
    {
        const float isz = FONT_SM + 2.0f;           // icon bounding size
        const float icy = text_y + FONT_SM * 0.5f;  // icon cy (centred on text)
        const float tsz = FONT_SM - 2.0f;           // hint text size (compact)
        float       hx  = 430.0f;

        // Static prefix.
        pax_vec2f adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "<>pg  ^v row  ");
        hx           += adv.x;

        // △ ✕ nudge
        ui_icon_draw(fb, UI_ICON_TRIANGLE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        ui_icon_draw(fb, UI_ICON_CROSS, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "nudge  ");
        hx += adv.x;

        // □ back
        ui_icon_draw(fb, UI_ICON_SQUARE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "back  ");
        hx += adv.x;

        // ☘ keys
        ui_icon_draw(fb, UI_ICON_TRILOBE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "keys  ");
        hx += adv.x;

        // ◇ save
        ui_icon_draw(fb, UI_ICON_DIAMOND, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "save  ");
        hx += adv.x;

        // ESC (no shape — key cap text only).
        (void)hx;
        pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, text_y, "ESC");
    }
}

extern "C" void ui_draw(pax_buf_t* fb, uint64_t millis, const UIState* s) {
    (void)millis;
    pax_background(fb, COL_BG);
    draw_tabs(fb, s);
    if (PAGE_TABLE[s->page].kind == PAGE_PRESETS) {
        ui_presets_draw(fb, s);
    } else {
        draw_rows(fb, s);
    }
    draw_status(fb, s);
    if (s->show_keyguide) {
        ui_overlay_draw_keyguide(fb, s);
    }
}

// Chrome-band accessors (ADR 0022): expose the status-strip and content-area
// scanline ranges so app.c can invalidate the right band without duplicating
// the layout #defines above.
extern "C" void ui_band_status(int* y0, int* y1) {
    *y0 = (int)(SCREEN_H - STATUS_H);
    *y1 = (int)SCREEN_H;
}

extern "C" void ui_band_content(int* y0, int* y1) {
    *y0 = (int)CONTENT_Y;
    *y1 = (int)(SCREEN_H - STATUS_H);
}
