// control/sustain.c — sustain-pedal deferred note-off implementation.
//
// Pure module: no engine, platform, or logging deps. All state lives in the
// caller-owned SustainPedal struct. See sustain.h for the API contract.
#include "control/sustain.h"
#include <string.h>

/* ---------- Bitmap helpers ------------------------------------------------ */

static inline void bit_set(uint8_t* map, uint8_t pitch) {
    map[pitch >> 3] |= (uint8_t)(1u << (pitch & 7u));
}

static inline void bit_clear(uint8_t* map, uint8_t pitch) {
    map[pitch >> 3] &= (uint8_t)~(1u << (pitch & 7u));
}

static inline bool bit_test(const uint8_t* map, uint8_t pitch) {
    return (map[pitch >> 3] >> (pitch & 7u)) & 1u;
}

/* ---------- API ----------------------------------------------------------- */

void sustain_init(SustainPedal* s) {
    s->pedal_down = false;
    memset(s->pending, 0, sizeof(s->pending));
}

void sustain_note_on(SustainPedal* s, uint8_t pitch) {
    if (pitch >= 128) return;
    // Re-press: cancel any deferred release so the voice stays alive.
    bit_clear(s->pending, pitch);
}

bool sustain_note_off(SustainPedal* s, uint8_t pitch) {
    if (pitch >= 128) return false;
    if (s->pedal_down) {
        bit_set(s->pending, pitch);
        return true;  // deferred — caller must NOT release now
    }
    return false;  // immediate — caller releases
}

void sustain_set_pedal(SustainPedal* s, bool down, sustain_release_fn release) {
    bool was_down = s->pedal_down;
    s->pedal_down = down;

    // Flush only on the down→up edge.
    if (was_down && !down) {
        for (int pitch = 0; pitch < 128; pitch++) {
            if (bit_test(s->pending, (uint8_t)pitch)) {
                bit_clear(s->pending, (uint8_t)pitch);
                if (release) {
                    release((uint8_t)pitch);
                }
            }
        }
    }
}

void sustain_clear(SustainPedal* s) {
    s->pedal_down = false;
    memset(s->pending, 0, sizeof(s->pending));
}
