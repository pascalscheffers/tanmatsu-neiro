// control/midi_router.c — MIDI byte-stream → engine dispatch (Stage 5a-ii).
//
// Drains platform_midi_read() each frame, feeds raw bytes into the incremental
// MidiParser, and dispatches completed note messages to the engine. Omni mode:
// channel field is ignored. CC/expression handling arrives in Stage 5c.
#include "control/midi_router.h"
#include "control/midi_in.h"
#include "platform.h"
#include "synth.h"

static MidiParser s_parser;

void midi_router_init(void) {
    midi_parser_init(&s_parser);
}

void midi_router_poll(void) {
    uint8_t buf[64];
    size_t  n;
    while ((n = platform_midi_read(buf, sizeof buf)) > 0) {
        for (size_t i = 0; i < n; i++) {
            MidiMsg m;
            if (midi_parse_byte(&s_parser, buf[i], &m)) {
                if (m.type == MIDI_NOTE_ON) {
                    engine_note_on(m.data1, m.data2);
                } else if (m.type == MIDI_NOTE_OFF) {
                    engine_note_off(m.data1);
                }
                // MIDI_CC / MIDI_OTHER: deferred to Stage 5c.
            }
        }
        if (n < sizeof buf) break;
    }
}
