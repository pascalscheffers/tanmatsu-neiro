// control/midi_in.h — incremental MIDI byte-stream parser (pure, no I/O).
//
// Parses a raw MIDI byte stream one byte at a time, handling running status
// and interleaved System Real-Time bytes (0xF8–0xFF).  Emits channel-voice
// messages only.  No engine, platform, or I/O dependencies.
//
// Usage:
//   MidiParser p;
//   midi_parser_init(&p);
//   MidiMsg msg;
//   if (midi_parse_byte(&p, byte, &msg)) { /* handle msg */ }
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Message type ------------------------------------------------- */

typedef enum {
    MIDI_NOTE_OFF = 0, /* Note-Off (and Note-On with velocity 0) */
    MIDI_NOTE_ON,      /* Note-On, velocity > 0                  */
    MIDI_CC,           /* Control Change                          */
    MIDI_OTHER,        /* Any other channel-voice message         */
} MidiMsgType;

/* ---------- Decoded message ----------------------------------------------- */

typedef struct {
    MidiMsgType type;
    uint8_t     channel; /* 0–15 */
    uint8_t     data1;
    uint8_t     data2;
} MidiMsg;

/* ---------- Parser state -------------------------------------------------- */

typedef struct {
    uint8_t running_status; /* Last channel status byte (0 = none) */
    uint8_t expected;       /* Data bytes still needed (0, 1, or 2) */
    uint8_t total;          /* Total data bytes for current message  */
    uint8_t data[2];        /* Collected data bytes                  */
    uint8_t data_count;     /* Data bytes collected so far           */
} MidiParser;

/* ---------- API ----------------------------------------------------------- */

/* Initialise (or reset) a parser. Must be called before midi_parse_byte. */
void midi_parser_init(MidiParser* p);

/* Feed one raw byte.
 * Returns true and fills *out when a complete channel-voice message is ready.
 * Returns false for data bytes still being accumulated, real-time bytes,
 * System Common bytes, or SysEx.
 * Note-On with velocity 0 is normalised to MIDI_NOTE_OFF on output.
 */
bool midi_parse_byte(MidiParser* p, uint8_t byte, MidiMsg* out);

#ifdef __cplusplus
}
#endif
