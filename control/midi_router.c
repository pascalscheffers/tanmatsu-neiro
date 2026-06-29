// control/midi_router.c — MIDI byte-stream → engine dispatch (Stage 5c).
//
// Drains platform_midi_read() each frame, feeds raw bytes into the incremental
// MidiParser, and dispatches completed messages to the engine. Omni mode:
// channel field is ignored. Sustain-pedal deferred note-off is handled by the
// pure control/sustain module so it can be tested independently.
#include "control/midi_router.h"
#include "control/midi_in.h"
#include "control/sustain.h"
#include "platform.h"
#include "synth.h"

static MidiParser   s_parser;
static SustainPedal s_sustain;

// CC-driven param focus state: stashed whenever a generic CC moves a mapped
// param. Cleared on read by midi_router_take_param_focus().
static uint16_t s_focus_id      = 0;
static float    s_focus_norm    = 0.0f;
static bool     s_focus_pending = false;

// Callback from sustain_set_pedal: release a deferred voice.
static void router_release(uint8_t pitch) {
    engine_note_off(pitch);
}

void midi_router_init(void) {
    midi_parser_init(&s_parser);
    sustain_init(&s_sustain);
}

void midi_router_poll(void) {
    uint8_t buf[64];
    size_t  n;
    while ((n = platform_midi_read(buf, sizeof buf)) > 0) {
        for (size_t i = 0; i < n; i++) {
            MidiMsg m;
            if (midi_parse_byte(&s_parser, buf[i], &m)) {
                switch (m.type) {
                    case MIDI_NOTE_ON:
                        sustain_note_on(&s_sustain, m.data1);
                        engine_note_on(m.data1, m.data2);
                        break;

                    case MIDI_NOTE_OFF:
                        if (!sustain_note_off(&s_sustain, m.data1)) {
                            engine_note_off(m.data1);
                        }
                        break;

                    case MIDI_PITCH_BEND: {
                        // data1 = LSB (0-127), data2 = MSB (0-127); centre = 0x2000 = 8192.
                        int   v14  = (int)m.data1 | ((int)m.data2 << 7);
                        float bend = (float)(v14 - 8192) / 8192.0f;
                        engine_set_pitch_bend(bend);
                        break;
                    }

                    case MIDI_CHANNEL_PRESSURE:
                        engine_set_aftertouch((float)m.data1 / 127.0f);
                        break;

                    case MIDI_CC:
                        switch (m.data1) {
                            case 1:  // Mod wheel
                                engine_set_mod_wheel((float)m.data2 / 127.0f);
                                break;
                            case 64:  // Sustain pedal
                                sustain_set_pedal(&s_sustain, m.data2 >= 64, router_release);
                                break;
                            case 120:  // All Sound Off / panic
                            case 123:  // All Notes Off / panic
                                engine_all_notes_off();
                                sustain_clear(&s_sustain);
                                break;
                            default: {
                                uint16_t id   = engine_cc_to_param(m.data1);
                                float    norm = (float)m.data2 / 127.0f;
                                if (id) {
                                    engine_set_param_norm(id, norm);
                                    // Stash for UI focus (one-shot; cleared by
                                    // midi_router_take_param_focus on the control task).
                                    s_focus_id      = id;
                                    s_focus_norm    = norm;
                                    s_focus_pending = true;
                                }
                                break;
                            }
                        }
                        break;

                    case MIDI_OTHER:
                    default:
                        break;
                }
            }
        }
        if (n < sizeof buf) break;
    }
}

bool midi_router_take_param_focus(uint16_t* out_id, float* out_norm) {
    if (!s_focus_pending) return false;
    *out_id         = s_focus_id;
    *out_norm       = s_focus_norm;
    s_focus_pending = false;
    return true;
}
