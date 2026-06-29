// control/sustain.h — sustain-pedal deferred note-off helper (pure, no platform/engine deps).
//
// Tracks which note-off events are being held back while the sustain pedal (CC64)
// is down. When the pedal is released, all deferred note-offs fire via a callback.
// Used by midi_router.c; lives in its own module so it can be unit-tested on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback invoked when a sustained note must finally be released.
typedef void (*sustain_release_fn)(uint8_t pitch);

// Sustain pedal state: which note-offs are being held back while the pedal is down.
// pending[] is a 128-bit bitmap: bit N is set if pitch N's note-off is deferred.
typedef struct {
    bool    pedal_down;
    uint8_t pending[16];  // 128-bit bitmap: pitches whose note-off is deferred
} SustainPedal;

// Initialise (or reset) the sustain state. Call before any other function.
void sustain_init(SustainPedal* s);

// A key was pressed: cancel any deferred release for this pitch (it is retriggering).
// Call this before triggering the engine note-on.
void sustain_note_on(SustainPedal* s, uint8_t pitch);

// A key was released.
// If the pedal is down, the release is deferred (the pitch is marked pending) and
// this returns true — the caller must NOT release the voice now.
// If the pedal is up, returns false — the caller releases the voice immediately.
bool sustain_note_off(SustainPedal* s, uint8_t pitch);

// Pedal state change. On a down→up transition, every pending pitch is flushed via
// `release` (called once per still-pending pitch), then pending is cleared.
// Setting the same value (e.g. down→down) updates the flag but does not flush.
void sustain_set_pedal(SustainPedal* s, bool down, sustain_release_fn release);

// Panic/reset: clear pedal + all pending pitches (no callbacks fired).
void sustain_clear(SustainPedal* s);

#ifdef __cplusplus
}
#endif
