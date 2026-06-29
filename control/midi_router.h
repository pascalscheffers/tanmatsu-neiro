// control/midi_router.h — MIDI byte-stream → engine dispatch (Stage 5a-ii).
//
// Owns a MidiParser; drains platform_midi_read() each frame and converts
// completed note messages into engine_note_on/off calls. Omni mode: channel
// is ignored. CC/expression dispatch arrives in Stage 5c.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void midi_router_init(void);
void midi_router_poll(void);

#ifdef __cplusplus
}
#endif
