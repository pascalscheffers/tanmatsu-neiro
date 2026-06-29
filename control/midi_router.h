// control/midi_router.h — MIDI byte-stream → engine dispatch (Stage 5a-ii).
//
// Owns a MidiParser; drains platform_midi_read() each frame and converts
// completed note messages into engine_note_on/off calls. Omni mode: channel
// is ignored. CC/expression dispatch arrives in Stage 5c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void midi_router_init(void);
void midi_router_poll(void);

// Returns true once per new CC-driven param change since the last call,
// handing back the param id and its normalised [0,1] value, then clears.
// Note: mod wheel (CC1) is hardwired and intentionally does NOT trigger a
// jump — it is not a table param, so no focus event is emitted for CC1.
bool midi_router_take_param_focus(uint16_t* out_id, float* out_norm);

#ifdef __cplusplus
}
#endif
