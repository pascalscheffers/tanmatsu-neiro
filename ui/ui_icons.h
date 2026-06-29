// ui/ui_icons.h — badge button shape icon drawing (WO-8).
//
// The six Tanmatsu badge buttons (F1..F6) have physical shapes:
//   F1 = cross (✕), F2 = triangle (△), F3 = square (□),
//   F4 = circle (○), F5 = trilobe (☘), F6 = diamond (◇)
//
// PAX fonts do not contain these glyphs, so this module draws them as vector
// outlines using PAX primitives. All draws are stroked/outline so they read
// clearly at hint-strip size (~12–14px).
#pragma once

#include <stdint.h>
#include "pax_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ICON_CROSS    = 0,  // F1 — ✕
    UI_ICON_TRIANGLE = 1,  // F2 — △
    UI_ICON_SQUARE   = 2,  // F3 — □
    UI_ICON_CIRCLE   = 3,  // F4 — ○
    UI_ICON_TRILOBE  = 4,  // F5 — ☘ (three-lobe / clover)
    UI_ICON_DIAMOND  = 5,  // F6 — ◇
} UiIconShape;

// Draw `shape` centred at (cx, cy) fitting within a box of side `size`,
// stroked in `color`. Cheap enough for soft-real-time hint-strip rendering.
void ui_icon_draw(pax_buf_t* fb, UiIconShape shape, float cx, float cy, float size, uint32_t color);

// Convenience: advance x past the icon (icon width = size + gap).
// Use as: x += ui_icon_width(size);
static inline float ui_icon_width(float size) {
    return size + 3.0f;
}

#ifdef __cplusplus
}
#endif
