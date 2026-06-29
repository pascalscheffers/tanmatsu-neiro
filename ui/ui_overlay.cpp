// ui/ui_overlay.cpp — on-demand overlay panels (WO-6).
//
// Draws the musical-typing key-guide overlay when s->show_keyguide is true.
// Sources the key→semitone mapping exclusively from keyboard_semitone_for_key()
// (control/keyboard.h) — no duplication of the mapping table (Prime Directive 2).
#include "ui_overlay.h"
#include <stdio.h>
#include "control/keyboard.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "ui_icons.h"

// ---------------------------------------------------------------------------
// Colors (synthwave palette — mirrors ui.cpp definitions; kept local so this
// file is self-contained and ui.cpp internals stay private)
// ---------------------------------------------------------------------------
#define COL_BG       0xFF101018u
#define COL_TEXT     0xFFFFFFFFu
#define COL_DIM      0xFF7A7A8Au
#define COL_ACCENT   0xFF30C0FFu  // neon cyan
#define COL_ACCENT2  0xFF2D9DFFu  // neon magenta/pink — reused for border
#define COL_SEP      0xFF2A2A4Au
#define COL_KEY_BG   0xFF1E1E30u  // panel background (opaque dark)
#define COL_WHITE_BG 0xFF28283Au  // white-key cell fill
#define COL_BLACK_BG 0xFF0C0C1Au  // black-key cell fill (darker)

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
#define SCREEN_W 800.0f
#define SCREEN_H 480.0f
#define FONT_SM  12.0f
#define FONT_MD  16.0f

// Panel dimensions
#define PANEL_W 680.0f
#define PANEL_H 260.0f
#define PANEL_X ((SCREEN_W - PANEL_W) * 0.5f)
#define PANEL_Y ((SCREEN_H - PANEL_H) * 0.5f)
#define BORDER  2.0f

// Key cell geometry (two rows: top = accidentals, bottom = naturals)
// Ten columns (a..;) wide; top row has gaps for E–F and B–C transitions.
#define CELL_COLS 10
#define CELL_W    54.0f
#define CELL_H    84.0f
#define CELL_PAD  3.0f

// Grid origin within the panel
#define GRID_MARGIN_X 18.0f
#define GRID_MARGIN_Y 56.0f  // below title

// ---------------------------------------------------------------------------
// Note-name helpers
// ---------------------------------------------------------------------------
static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Given a 0-based semitone offset (0=C … 16=E+1) and base octave, fill buf
// with the note name + octave number (e.g. "C4", "C#4", "E5").
static void note_label(char* buf, int buf_sz, int semi, int base_octave) {
    int oct  = base_octave + semi / 12;
    int note = semi % 12;
    snprintf(buf, (size_t)buf_sz, "%s%d", NOTE_NAMES[note], oct);
}

// ---------------------------------------------------------------------------
// Key descriptor: one entry per physical key in layout order
// ---------------------------------------------------------------------------
struct KeyDesc {
    char key;  // physical key character
    int  col;  // column index 0..9 in the 10-column grid
    int  row;  // 0 = top (accidentals), 1 = bottom (naturals)
};

// Two-row piano grid (left-to-right):
//   bottom row: a  s  d  f  g  h  j  k  l  ;
//   top row:    w  e  _  t  y  u  _  o  p  _
// Gaps (no black key between E/F and B/C) — those top-row slots are empty.
// clang-format off
static const KeyDesc KEY_LAYOUT[] = {
    // Bottom row — natural keys, columns 0..9
    { 'a', 0, 1 },
    { 's', 1, 1 },
    { 'd', 2, 1 },
    { 'f', 3, 1 },
    { 'g', 4, 1 },
    { 'h', 5, 1 },
    { 'j', 6, 1 },
    { 'k', 7, 1 },
    { 'l', 8, 1 },
    { ';', 9, 1 },
    // Top row — accidental keys, with gaps at cols 2 (E/F seam) and 6 (B/C seam)
    { 'w', 0, 0 },
    { 'e', 1, 0 },
    // col 2: gap (no black key between E and F)
    { 't', 3, 0 },
    { 'y', 4, 0 },
    { 'u', 5, 0 },
    // col 6: gap (no black key between B and C)
    { 'o', 7, 0 },
    { 'p', 8, 0 },
    // col 9: gap (no black key after E+1 in this layout)
};
// clang-format on
static const int KEY_COUNT = (int)(sizeof(KEY_LAYOUT) / sizeof(KEY_LAYOUT[0]));

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
extern "C" void ui_overlay_draw_keyguide(pax_buf_t* fb, const UIState* s) {
    // --- Panel background + border ---
    pax_simple_rect(fb, COL_ACCENT, PANEL_X - BORDER, PANEL_Y - BORDER, PANEL_W + BORDER * 2.0f,
                    PANEL_H + BORDER * 2.0f);
    pax_simple_rect(fb, COL_KEY_BG, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);

    // --- Title ---
    pax_draw_text(fb, COL_ACCENT, pax_font_sky_mono, FONT_MD, PANEL_X + 12.0f, PANEL_Y + 10.0f, "MUSICAL TYPING");

    // Thin separator under title
    pax_simple_rect(fb, COL_SEP, PANEL_X, PANEL_Y + 34.0f, PANEL_W, 1.0f);

    // --- Key grid ---
    int   oct = keyboard_octave();
    float gx0 = PANEL_X + GRID_MARGIN_X;
    float gy0 = PANEL_Y + GRID_MARGIN_Y;

    for (int ki = 0; ki < KEY_COUNT; ki++) {
        const KeyDesc& kd = KEY_LAYOUT[ki];

        float cx = gx0 + (float)kd.col * (CELL_W + CELL_PAD);
        float cy = gy0 + (float)kd.row * (CELL_H + CELL_PAD);

        int semi = keyboard_semitone_for_key((int)(unsigned char)kd.key);

        // Choose cell color: accidental row → black-key style; natural → white-key style.
        uint32_t bg = (kd.row == 0) ? COL_BLACK_BG : COL_WHITE_BG;
        pax_simple_rect(fb, bg, cx, cy, CELL_W, CELL_H);

        // Thin accent border on selected (top-row) keys
        if (kd.row == 0) {
            pax_simple_rect(fb, COL_ACCENT2, cx, cy, CELL_W, 2.0f);
        }

        float mid_x = cx + CELL_W * 0.5f;

        // Key letter — centred horizontally at top of cell
        char key_str[2] = {kd.key, '\0'};
        // uppercase display
        char upper      = (kd.key >= 'a' && kd.key <= 'z') ? (char)(kd.key - 32) : kd.key;
        if (kd.key == ';') upper = ';';
        char  disp[2] = {upper, '\0'};
        float kw      = pax_text_size(pax_font_sky_mono, FONT_MD, disp).x;
        (void)key_str;
        pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_MD, mid_x - kw * 0.5f, cy + 6.0f, disp);

        // Note name — centred, below the key letter
        if (semi >= 0) {
            char note_buf[8];
            note_label(note_buf, sizeof(note_buf), semi, oct);
            float nw = pax_text_size(pax_font_sky_mono, FONT_SM, note_buf).x;
            pax_draw_text(fb, COL_TEXT, pax_font_sky_mono, FONT_SM, mid_x - nw * 0.5f, cy + 6.0f + FONT_MD + 6.0f,
                          note_buf);
        }
    }

    // --- Footer ---
    float footer_y = PANEL_Y + PANEL_H - FONT_SM - 10.0f;

    char oct_buf[24];
    snprintf(oct_buf, sizeof(oct_buf), "Oct %d", oct);
    pax_draw_text(fb, COL_ACCENT, pax_font_sky_mono, FONT_SM, PANEL_X + 12.0f, footer_y, oct_buf);

    // "Z/X = oct down/up" stays as text; F5 (trilobe) drawn as a shape icon + "close".
    {
        pax_vec2f adv =
            pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, PANEL_X + 70.0f, footer_y, "Z/X = oct down/up     ");
        float hx  = PANEL_X + 70.0f + adv.x;
        float isz = FONT_SM + 2.0f;
        float icy = footer_y + FONT_SM * 0.5f;
        ui_icon_draw(fb, UI_ICON_TRILOBE, hx + isz * 0.5f, icy, isz, COL_DIM);
        hx += ui_icon_width(isz);
        pax_draw_text(fb, COL_DIM, pax_font_sky_mono, FONT_SM, hx, footer_y, " close");
    }
}
