// ui.c — Stage 1d synth page.
//
// Title, 8 voice-activity indicators, current octave, and a key hint.
// Stage 2 replaces this with the full param-table-driven page; keep clean
// constants here so Stage 2 can lift them into table rows easily.
#include "ui.h"
#include <stdio.h>
#include "pax_fonts.h"
#include "pax_text.h"

#define COL_BG         0xFF101018
#define COL_TEXT       0xFFFFFFFF
#define COL_DIM        0xFF7A7A8A
#define COL_VOICE_ON   0xFF30C0FF
#define COL_VOICE_OFF  0xFF2A2A3A

// Number of voice indicator cells — matches kNumVoices (Stage 2 will source
// this from the param table; hardcoded here to keep ui/ free of engine deps).
#define NUM_VOICE_CELLS 8

void ui_draw(pax_buf_t* fb, uint64_t millis, int active_voices, int octave) {
    (void)millis;
    float w = (float)pax_buf_get_width(fb);
    float h = (float)pax_buf_get_height(fb);

    pax_background(fb, COL_BG);

    pax_draw_text(fb, COL_TEXT, pax_font_sky_mono, 24, 12, 12, "Tanmatsu Synth");

    // Voice activity indicators — 8 cells centred horizontally.
    const float cell_w = 40.0f;
    const float cell_h = 40.0f;
    const float gap    = 10.0f;
    float total_w = NUM_VOICE_CELLS * cell_w + (NUM_VOICE_CELLS - 1) * gap;
    float start_x = (w - total_w) * 0.5f;
    float cell_y  = h * 0.5f - cell_h * 0.5f;

    for (int i = 0; i < NUM_VOICE_CELLS; i++) {
        uint32_t col = (i < active_voices) ? COL_VOICE_ON : COL_VOICE_OFF;
        float    x   = start_x + (float)i * (cell_w + gap);
        pax_simple_rect(fb, col, x, cell_y, cell_w, cell_h);
    }

    // Octave and key hint at the bottom.
    char buf[32];
    snprintf(buf, sizeof(buf), "Oct %d", octave);
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, 20, 12, h - 48.0f, buf);
    pax_draw_text(fb, COL_DIM, pax_font_sky_mono, 16, 12, h - 24.0f,
                  "a-; = notes   z/x = oct down/up   ESC = exit");
}
