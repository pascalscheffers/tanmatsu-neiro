// engine/synth.cpp — Stage 1b: single JunoVoice render.
// Replaces the Stage 0 sine. Stage 1c wires the full voice allocator + chorus.
// The extern "C" interface in synth.h is unchanged — callers (platform_*.c)
// see C linkage regardless of this file being compiled as C++.
#include "synth.h"
#include "juno_voice.h"
#include <string.h>

// Pre-allocated mono accumulation buffer (ADR RT rule #1: no alloc in render).
// Must be >= the block_size passed to platform_audio_start (currently 64; 256
// is the device platform ceiling defined by MAX_BLOCK in platform_device.c).
static const size_t kMaxBlock = 256;
static float        s_mono[kMaxBlock];
static JunoVoice    s_voice;

void synth_init(uint32_t sample_rate) {
    s_voice.init((float)sample_rate);

    // Stage 1b: trigger a sustained A4 so make host-run is audible.
    // Stage 1c/1d replace this with the voice allocator + musical typing.
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    s_voice.note_on(69, 100, expr);
}

void synth_render(float* left, float* right, size_t n, void* user) {
    (void)user;
    size_t frames = n < kMaxBlock ? n : kMaxBlock;
    memset(s_mono, 0, frames * sizeof(float));
    s_voice.render(s_mono, frames);
    for (size_t i = 0; i < frames; i++) {
        left[i]  = s_mono[i];
        right[i] = s_mono[i];
    }
}
