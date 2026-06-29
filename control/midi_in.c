/* control/midi_in.c — incremental MIDI byte-stream parser. */
#include "control/midi_in.h"

/* -------------------------------------------------------------------------
 * MIDI status byte constants / masks
 * ------------------------------------------------------------------------- */

/* Status byte high nibble (channel-voice message type) */
#define MIDI_STATUS_NOTE_OFF        0x80u
#define MIDI_STATUS_NOTE_ON         0x90u
#define MIDI_STATUS_POLY_AFTERTOUCH 0xA0u
#define MIDI_STATUS_CC              0xB0u
#define MIDI_STATUS_PROG_CHANGE     0xC0u
#define MIDI_STATUS_CHAN_AFTERTOUCH 0xD0u
#define MIDI_STATUS_PITCH_BEND      0xE0u

/* System message boundaries */
#define MIDI_SYSEX_START    0xF0u
#define MIDI_SYS_COMMON_END 0xF7u /* 0xF1–0xF7: System Common        */
#define MIDI_RT_START       0xF8u /* 0xF8–0xFF: System Real-Time     */

/* Masks */
#define MIDI_STATUS_BIT   0x80u /* Set in any status byte          */
#define MIDI_CHANNEL_MASK 0x0Fu
#define MIDI_TYPE_MASK    0xF0u

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Returns the number of data bytes expected for a channel-voice status byte.
 * Returns 0 for unknown/unhandled status (should not happen for valid input). */
static uint8_t data_bytes_for_status(uint8_t status) {
    switch (status & MIDI_TYPE_MASK) {
        case MIDI_STATUS_NOTE_OFF:
        case MIDI_STATUS_NOTE_ON:
        case MIDI_STATUS_POLY_AFTERTOUCH:
        case MIDI_STATUS_CC:
        case MIDI_STATUS_PITCH_BEND:
            return 2u;
        case MIDI_STATUS_PROG_CHANGE:
        case MIDI_STATUS_CHAN_AFTERTOUCH:
            return 1u;
        default:
            return 0u;
    }
}

/* True if b is a channel-voice status byte (0x80–0xEF). */
static bool is_channel_voice(uint8_t b) {
    return (b >= MIDI_STATUS_NOTE_OFF) && (b < MIDI_SYSEX_START);
}

/* True if b is a System Real-Time byte (0xF8–0xFF). */
static bool is_realtime(uint8_t b) {
    return b >= MIDI_RT_START;
}

/* True if b is a System Common byte (0xF1–0xF7) or SysEx (0xF0). */
static bool is_system_common(uint8_t b) {
    return (b >= MIDI_SYSEX_START) && (b < MIDI_RT_START);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void midi_parser_init(MidiParser* p) {
    p->running_status = 0u;
    p->expected       = 0u;
    p->total          = 0u;
    p->data[0]        = 0u;
    p->data[1]        = 0u;
    p->data_count     = 0u;
}

bool midi_parse_byte(MidiParser* p, uint8_t byte, MidiMsg* out) {
    /* --- System Real-Time: pass-through, do NOT disturb parser state --- */
    if (is_realtime(byte)) {
        return false;
    }

    /* --- System Common (including SysEx 0xF0): cancel running status --- */
    if (is_system_common(byte)) {
        p->running_status = 0u;
        p->expected       = 0u;
        p->data_count     = 0u;
        return false;
    }

    /* --- Channel-voice status byte --- */
    if (is_channel_voice(byte)) {
        uint8_t needed = data_bytes_for_status(byte);
        if (needed == 0u) {
            /* Unrecognised channel-voice type: ignore, clear state */
            p->running_status = 0u;
            p->expected       = 0u;
            p->data_count     = 0u;
            return false;
        }
        p->running_status = byte;
        p->total          = needed;
        p->expected       = needed;
        p->data_count     = 0u;
        return false;
    }

    /* --- Data byte --- */
    /* If we have no running status, the byte is orphaned; discard. */
    if (p->running_status == 0u) {
        return false;
    }

    /* Accumulate */
    if (p->data_count < 2u) {
        p->data[p->data_count] = byte;
    }
    p->data_count++;

    if (p->data_count < p->total) {
        /* Still waiting for more data bytes */
        return false;
    }

    /* ---- Complete message — decode ---- */
    uint8_t status  = p->running_status;
    uint8_t channel = status & MIDI_CHANNEL_MASK;
    uint8_t type    = status & MIDI_TYPE_MASK;

    MidiMsgType msg_type;
    switch (type) {
        case MIDI_STATUS_NOTE_ON:
            /* Velocity 0 → treat as Note-Off (standard MIDI normalisation) */
            msg_type = (p->data[1] == 0u) ? MIDI_NOTE_OFF : MIDI_NOTE_ON;
            break;
        case MIDI_STATUS_NOTE_OFF:
            msg_type = MIDI_NOTE_OFF;
            break;
        case MIDI_STATUS_CC:
            msg_type = MIDI_CC;
            break;
        case MIDI_STATUS_PITCH_BEND:
            msg_type = MIDI_PITCH_BEND;
            break;
        case MIDI_STATUS_CHAN_AFTERTOUCH:
            msg_type = MIDI_CHANNEL_PRESSURE;
            break;
        default:
            msg_type = MIDI_OTHER;
            break;
    }

    out->type    = msg_type;
    out->channel = channel;
    out->data1   = p->data[0];
    out->data2   = (p->total == 2u) ? p->data[1] : 0u;

    /* Reset data collection for next running-status message;
     * running_status is preserved so the next data byte starts a new msg. */
    p->data_count = 0u;

    return true;
}
