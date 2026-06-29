// control/keyboard.h — QWERTY musical-typing input.
//
// Maps a two-row piano layout (GarageBand-style) to engine note events.
// Lives in control/ so it never touches the audio path directly.
#pragma once

#include <stdbool.h>
#include "platform.h"

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

// Return the 0-based semitone offset (0=C … 16=E+1) for a physical key character,
// or -1 if the key is not a note key. The same table used by keyboard_handle_event;
// callers (e.g. the UI key-guide overlay) derive note names without duplicating it.
int keyboard_semitone_for_key(int key);

#ifdef __cplusplus
}
#endif
