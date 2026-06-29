// ui/ui_presets.cpp — preset-browser page: drawing (WO-3).
//
// State logic (snapshot/revert/event handling) lives in ui_presets_state.cpp.
// This file owns only the PAX rendering path; it has no direct engine calls.
// No voice-model knowledge here (ADR 0008).
#include "ui_presets.h"
#include <stdio.h>
#include <string.h>
#include "engine/preset.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "ui_presets_state.h"

// ---------------------------------------------------------------------------
// Layout constants — must stay in sync with ui.cpp values (WO-2 tightened).
// ---------------------------------------------------------------------------
#define SCREEN_W  800.0f
#define SCREEN_H  480.0f
#define TAB_H     40.0f
#define STATUS_H  38.0f
#define CONTENT_Y TAB_H
#define CONTENT_H (SCREEN_H - TAB_H - STATUS_H)
#define ROW_H     43.0f
#define FONT_SM   12.0f
#define FONT_MD   16.0f

// Palette — same tokens as ui.cpp.
#define COL_BG      0xFF101018u
#define COL_TEXT    0xFFFFFFFFu
#define COL_DIM     0xFF7A7A8Au
#define COL_ACCENT  0xFF30C0FFu
#define COL_ACCENT2 0xFFFF2D9Du
#define COL_BAR_BG  0xFF2A2A3Au
#define COL_SEL_BG  0xFF16222Eu
#define COL_SEP     0xFF2A2A4Au

// "User" row sentinel: one past the last factory index.
static inline int user_row_idx(void) {
    return preset_factory_count();
}

// Total number of rows: factory count + 1 User entry.
static inline int total_rows(void) {
    return preset_factory_count() + 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

extern "C" void ui_presets_draw(pax_buf_t* fb, const UIState* s) {
    int n   = total_rows();
    int usr = user_row_idx();

    // Total pixel height of the list.
    float total_h = (float)n * ROW_H;

    // Scroll: keep the selected row centred, clamped to content bounds.
    float start_y;
    if (total_h <= CONTENT_H) {
        start_y = CONTENT_Y + (CONTENT_H - total_h) * 0.5f;
    } else {
        float sel_centre = (float)s->row * ROW_H + ROW_H * 0.5f;
        float ideal      = CONTENT_Y + CONTENT_H * 0.5f - sel_centre;
        if (ideal > CONTENT_Y) ideal = CONTENT_Y;
        if (ideal + total_h < CONTENT_Y + CONTENT_H) ideal = CONTENT_Y + CONTENT_H - total_h;
        start_y = ideal;
    }

    // Clip to the content area so a partially-scrolled row can't bleed over the
    // tab strip above or the status bar below.
    pax_clip(fb, 0, (int)CONTENT_Y, (int)SCREEN_W, (int)CONTENT_H);

    float y = start_y;
    for (int i = 0; i < n; i++) {
        float row_top = y;
        float row_bot = y + ROW_H;

        // Skip rows fully outside the content area.
        if (row_bot <= CONTENT_Y || row_top >= CONTENT_Y + CONTENT_H) {
            y += ROW_H;
            continue;
        }

        bool is_cursor = (i == s->row);
        bool is_active = (i == s->preset_idx) || (i == usr && s->preset_idx < 0);
        bool is_user   = (i == usr);

        // Row background.
        if (is_cursor) {
            pax_simple_rect(fb, COL_SEL_BG, 0.0f, y, SCREEN_W, ROW_H - 1.0f);
        } else if (i % 2 == 0) {
            // Subtle alternating stripe for readability.
            pax_simple_rect(fb, 0xFF121220u, 0.0f, y, SCREEN_W, ROW_H - 1.0f);
        }

        float mid_y = y + ROW_H * 0.5f;

        // Cursor arrow.
        if (is_cursor) {
            pax_draw_text(fb, COL_ACCENT, pax_font_sky_mono, FONT_MD, 8.0f, mid_y - FONT_MD * 0.5f, ">");
        }

        // Active-preset dot — shows which patch is currently committed.
        if (is_active) {
            // Cyan dot = committed; magenta dot = snapshot behind an audition.
            uint32_t dot_col = (s->auditioning && is_cursor) ? COL_ACCENT2 : COL_ACCENT;
            pax_draw_text(fb, dot_col, pax_font_sky_mono, FONT_MD, 28.0f, mid_y - FONT_MD * 0.5f,
                          "\xE2\x97\x8F");  // UTF-8 U+25CF BLACK CIRCLE
        }

        // Preset name.
        const char* row_name;
        char        idx_buf[8];
        if (is_user) {
            row_name = "User";
        } else {
            row_name = preset_factory_name(i);
        }

        // Index prefix (right-aligned in a fixed field).
        snprintf(idx_buf, sizeof(idx_buf), is_user ? "  " : "%2d", i + 1);
        pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, 48.0f, mid_y - FONT_SM * 0.5f, idx_buf);
        pax_draw_text(fb, is_cursor ? COL_TEXT : COL_DIM, pax_font_sky_mono, FONT_MD, 80.0f, mid_y - FONT_MD * 0.5f,
                      row_name);

        // "AUDITIONING" badge on the currently-highlighted row.
        if (is_cursor && s->auditioning) {
            pax_draw_text(fb, COL_ACCENT2, pax_font_sky_mono, FONT_SM, 500.0f, mid_y - FONT_SM * 0.5f, "AUDITIONING");
        }

        // Separator line at bottom of each row.
        pax_simple_rect(fb, COL_SEP, 0.0f, y + ROW_H - 1.0f, SCREEN_W, 1.0f);

        y += ROW_H;
    }

    pax_noclip(fb);

    // Hint strip near bottom of content (above status bar).
    float hint_y = CONTENT_Y + CONTENT_H - FONT_SM - 6.0f;
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM - 1.0f, 8.0f, hint_y,
                  "^/v: browse  F4/Enter: confirm  F3/Esc: revert  <>: page");
}
