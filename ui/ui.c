// ui.c — Stage 0 hello screen.
//
// Background, two lines of text, and one bar that sweeps across the screen so a
// glance confirms the render loop and present path are both alive. PAX-only:
// the same source compiles and runs on host (SDL present) and device (blit).
#include "ui.h"
#include "pax_fonts.h"
#include "pax_text.h"

// ARGB colors; the alpha byte is ignored on the device's 24-bit buffer.
#define COL_BG    0xFF101018  // near-black with a faint blue cast
#define COL_TEXT  0xFFFFFFFF  // white
#define COL_DIM   0xFF7A7A8A  // muted grey
#define COL_SWEEP 0xFF30C0FF  // cyan sweep bar

static const char TITLE[]    = "Tanmatsu Synth";
static const char SUBTITLE[] = "Stage 0 - hello audio";

void ui_draw(pax_buf_t* fb, uint64_t millis) {
    float w = (float)pax_buf_get_width(fb);
    float h = (float)pax_buf_get_height(fb);

    pax_background(fb, COL_BG);

    pax_draw_text(fb, COL_TEXT, pax_font_sky_mono, 24, 12, 12, TITLE);
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, 16, 12, 44, SUBTITLE);

    // A bar that sweeps left->right and back over ~2 s, driven by wall-clock
    // time so any stall in the loop is immediately visible on screen.
    float period_ms = 2000.0f;
    float t         = (float)(millis % (uint64_t)period_ms) / period_ms;  // 0..1
    float tri       = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);        // 0..1..0
    float bar_w     = 48.0f;
    float bar_x     = tri * (w - bar_w);
    float bar_y     = h - 24.0f;
    pax_simple_rect(fb, COL_SWEEP, bar_x, bar_y, bar_w, 12.0f);
}
