// ui/ui_presets.h — preset-browser page public interface (WO-3).
//
// Drawing: ui_presets_draw() (implemented in ui_presets.cpp, PAX-dependent).
// State:   ui_presets_snapshot() + ui_presets_handle_event() (ui_presets_state.cpp,
//          no PAX — testable without a display).
#pragma once

#include <stdbool.h>
#include "pax_gfx.h"
#include "ui.h"
#include "ui_presets_state.h"  // snapshot + handle_event declared here

#ifdef __cplusplus
extern "C" {
#endif

// Draw the preset-browser page into fb.
// Called from ui_draw() when PAGE_TABLE[s->page].kind == PAGE_PRESETS.
void ui_presets_draw(pax_buf_t* fb, const UIState* s);

#ifdef __cplusplus
}
#endif
