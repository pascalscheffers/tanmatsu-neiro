// control/keyboard.h — QWERTY musical-typing input.
//
// Maps a two-row piano layout (GarageBand-style) to engine note events.
// Lives in control/ so it never touches the audio path directly.
#pragma once

#include "platform.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Prepare state. Call once before the first handle_event.
void keyboard_init(void);

// Feed a platform event to the keyboard handler.
// Issues engine_note_on/note_off for note keys; adjusts octave for z/x.
// Returns true if the event was consumed.
bool keyboard_handle_event(const platform_event_t* ev);

// Current octave (default 4; z/x shift within [1..7]).
int keyboard_octave(void);

#ifdef __cplusplus
}
#endif
