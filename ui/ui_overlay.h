// ui/ui_overlay.h — on-demand overlay panels drawn on top of the main UI (WO-6).
//
// Each overlay is drawn LAST in ui_draw() so it appears over all other content.
// Overlays read UIState for toggle flags and keyboard state; they must not write
// UIState or call engine functions.
#pragma once

#include "pax_gfx.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draw the musical-typing key-guide overlay.
// Shows the QWERTY→note mapping at the current octave, styled as a keyboard grid.
// Safe to call unconditionally; the caller guards with s->show_keyguide.
void ui_overlay_draw_keyguide(pax_buf_t* fb, const UIState* s);

#ifdef __cplusplus
}
#endif
