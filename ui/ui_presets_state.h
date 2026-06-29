// ui/ui_presets_state.h — pure UIState logic for the preset browser (WO-3).
//
// No PAX dependency; testable without a display.  ui_presets.cpp (drawing)
// and ui.cpp (event dispatch) both include this header.
#pragma once

#include <stdbool.h>
#include "platform.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Snapshot the current UIState (norms/name/idx) into the audition fields and
// clear auditioning.  Called by ui_state_init() and on confirm (F4/Enter).
void ui_presets_snapshot(UIState* s);

// Handle a key event on the preset page.
// Returns true if the event was consumed; false to pass through to the page
// switcher (Left/Right after revert).
bool ui_presets_handle_event(UIState* s, const platform_event_t* ev);

#ifdef __cplusplus
}
#endif
