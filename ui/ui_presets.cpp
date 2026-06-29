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
#include "ui_icons.h"
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

    // Reserve a band at the bottom of the content for the hint strip; the list
    // scrolls within the region above it so the last row never sits under it.
    const float hint_h = FONT_SM + 10.0f;
    const float list_h = CONTENT_H - hint_h;

    // Total pixel height of the list.
    float total_h = (float)n * ROW_H;

    // Scroll: keep the selected row centred, clamped to the list region.
    float start_y;
    if (total_h <= list_h) {
        start_y = CONTENT_Y + (list_h - total_h) * 0.5f;
    } else {
        float sel_centre = (float)s->row * ROW_H + ROW_H * 0.5f;
        float ideal      = CONTENT_Y + list_h * 0.5f - sel_centre;
        if (ideal > CONTENT_Y) ideal = CONTENT_Y;
        if (ideal + total_h < CONTENT_Y + list_h) ideal = CONTENT_Y + list_h - total_h;
        start_y = ideal;
    }

    // Clip to the list region so a partially-scrolled row can't bleed over the
    // tab strip above or the hint strip / status bar below.
    pax_clip(fb, 0, (int)CONTENT_Y, (int)SCREEN_W, (int)list_h);

    float y = start_y;
    for (int i = 0; i < n; i++) {
        float row_top = y;
        float row_bot = y + ROW_H;

        // Skip rows fully outside the list region.
        if (row_bot <= CONTENT_Y || row_top >= CONTENT_Y + list_h) {
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
        // Drawn as a filled circle rather than a glyph (U+25CF is not in pax_font_sky_mono).
        if (is_active) {
            // Cyan dot = committed; magenta dot = snapshot behind an audition.
            uint32_t dot_col = (s->auditioning && is_cursor) ? COL_ACCENT2 : COL_ACCENT;
            float    dot_r   = FONT_MD * 0.18f;
            pax_simple_circle(fb, dot_col, 28.0f + dot_r, mid_y, dot_r);
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

    // Hint strip in its own reserved band below the list (above the status
    // bar). Paint the band first so clipped rows above can't bleed into it,
    // with a separator rule along its top edge.
    float band_y = CONTENT_Y + list_h;
    pax_simple_rect(fb, COL_BG, 0.0f, band_y, SCREEN_W, hint_h);
    pax_simple_rect(fb, COL_SEP, 0.0f, band_y, SCREEN_W, 1.0f);
    float hint_y = band_y + (hint_h - (FONT_SM - 1.0f)) * 0.5f;
    // Hint strip: arrows stay as ASCII; ○ = load/confirm, □ = back, drawn as icons.
    {
        const float tsz = FONT_SM - 1.0f;
        const float isz = tsz + 2.0f;           // icon bounding size
        const float icy = hint_y + tsz * 0.5f;  // icon cy centred on text
        float       hx  = 8.0f;
        pax_vec2f   adv;

        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, hint_y, "^v browse  ");
        hx += adv.x;

        ui_icon_draw(fb, UI_ICON_CIRCLE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, hint_y, " load  ");
        hx += adv.x;

        ui_icon_draw(fb, UI_ICON_SQUARE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        adv = pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, hint_y, " back  ");
        hx += adv.x;

        (void)hx;
        pax_draw_text(fb, COL_DIM, pax_font_sky_mono, tsz, hx, hint_y, "<> page");
    }
}
