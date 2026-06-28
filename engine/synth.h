// synth.h — the engine's audio entry point.
//
// Stage 0 is a placeholder sine generator; the real Juno voice arrives in
// Stage 1. The signature is the permanent render contract (spec 04): the HAL
// owns the thread, the engine fills stereo float buffers in [-1, 1].
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Prepare the engine for the given output sample rate. Call before the audio
// sink starts. Allocation/setup is allowed here; the audio path is not.
void synth_init(uint32_t sample_rate);

// Render `n` frames of stereo audio. Matches platform_audio_render_fn so it can
// be handed directly to platform_audio_start(). Real-time path: no alloc, no
// logging, no blocking (CLAUDE.md). `user` is unused in Stage 0.
void synth_render(float* left, float* right, size_t n, void* user);

#ifdef __cplusplus
}
#endif
