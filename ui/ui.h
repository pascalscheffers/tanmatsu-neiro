// ui.h — portable UI rendering.
//
// Draws into a PAX framebuffer the platform owns. PAX is portable C, so this
// exact code runs on host and device; only presenting the buffer differs.
#pragma once

#include <stdint.h>
#include "pax_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Render one frame of the Stage 1d synth page into `fb`.
//   millis       — wall-clock time (platform_millis), for animation only.
//   active_voices — count from engine_active_voices(); displayed as indicators.
//   octave       — current keyboard octave from keyboard_octave().
void ui_draw(pax_buf_t* fb, uint64_t millis, int active_voices, int octave);

#ifdef __cplusplus
}
#endif
