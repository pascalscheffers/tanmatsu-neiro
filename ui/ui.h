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

// Render one frame of the Stage 0 hello screen into `fb`. `millis` is wall-clock
// time (platform_millis) used only to animate, proving the loop is live.
void ui_draw(pax_buf_t* fb, uint64_t millis);

#ifdef __cplusplus
}
#endif
