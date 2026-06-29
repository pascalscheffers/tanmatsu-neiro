// control/keyboard.c — QWERTY musical-typing input (Stage 1d).
//
// Two-row piano layout (GarageBand-style):
//   Middle row  a w s e d f t g y h u j k o l p ;
//   Semitone    0 1 2 3 4 5 6 7 8 9 A B C D E F G  (hex for brevity)
//   Note        C C# D D# E F F# G G# A A# B C+1 ...
//
// z = octave down, x = octave up. Default octave 4 → 'a' = C4 = MIDI 60.
// Press and release both arrive on host (SDL) and device (BSP scancode events,
// which carry make/break state — see platform/device/platform_device.c).
#include "control/keyboard.h"
#include <stdint.h>
#include "synth.h"

#define OCT_DEFAULT   4
#define OCT_MIN       1
#define OCT_MAX       7
#define BASE_VELOCITY 100

static int s_octave = OCT_DEFAULT;

// Returns 0-based semitone offset from C for note keys, -1 otherwise.
static int key_to_semitone(int k) {
    switch (k) {
        case 'a':
            return 0;
        case 'w':
            return 1;
        case 's':
            return 2;
        case 'e':
            return 3;
        case 'd':
            return 4;
        case 'f':
            return 5;
        case 't':
            return 6;
        case 'g':
            return 7;
        case 'y':
            return 8;
        case 'h':
            return 9;
        case 'u':
            return 10;
        case 'j':
            return 11;
        case 'k':
            return 12;
        case 'o':
            return 13;
        case 'l':
            return 14;
        case 'p':
            return 15;
        case ';':
            return 16;
        default:
            return -1;
    }
}

void keyboard_init(void) {
    s_octave = OCT_DEFAULT;
}

bool keyboard_handle_event(const platform_event_t* ev) {
    if (ev->type != PLATFORM_EV_KEY) return false;

    int k = ev->key;

    if (ev->pressed) {
        if (k == 'z') {
            if (s_octave > OCT_MIN) s_octave--;
            return true;
        }
        if (k == 'x') {
            if (s_octave < OCT_MAX) s_octave++;
            return true;
        }
    }

    int semi = key_to_semitone(k);
    if (semi < 0) return false;

    // MIDI C4 = 60 = 12*(4+1). Formula: 12*(octave+1) + semitone.
    int note = 12 * (s_octave + 1) + semi;
    if (note < 0) note = 0;
    if (note > 127) note = 127;

    if (ev->pressed) {
        engine_note_on((uint8_t)note, BASE_VELOCITY);
    } else {
        engine_note_off((uint8_t)note);
    }
    return true;
}

int keyboard_octave(void) {
    return s_octave;
}
