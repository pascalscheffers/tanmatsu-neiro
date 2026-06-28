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

// Prepare the engine for the given output sample rate and block size.
// Call before the audio sink starts. Allocation/setup is allowed here.
// block_size must match the platform_audio_config_t block_size used.
void synth_init(uint32_t sample_rate, size_t block_size);

// Render `n` frames of stereo audio. Matches platform_audio_render_fn so it can
// be handed directly to platform_audio_start(). Real-time path: no alloc, no
// logging, no blocking (CLAUDE.md). `user` is unused in Stage 0.
void synth_render(float* left, float* right, size_t n, void* user);

// Note input API — called by control/ (musical typing, MIDI). Lock-free: these
// mutate voice-allocator state from the UI thread; synth_render reads it. Safe
// only because note_on/note_off are called from one writer thread (Stage 1c
// single-threaded; Stage 5 adds a command ring buffer for true RT isolation).
void engine_note_on(uint8_t pitch, uint8_t velocity);
void engine_note_off(uint8_t pitch);

// Count of currently active voices (gate-on or envelope still running).
// Called from the UI thread; may read a frame-stale value — display use only.
int engine_active_voices(void);

// Set a parameter from the control thread (UI, MIDI, preset load).
// Lock-free: the update is enqueued in the param store and drained each block.
// id: a ParamId::* constant. value: physical units, clamped to [min, max].
void engine_set_param(uint16_t id, float value);

// Set a parameter by normalised position [0, 1]; curve mapping applied.
void engine_set_param_norm(uint16_t id, float norm);

// Read the current smoothed value for a param (control-thread safe; may lag
// the audio thread by one block — use for display only, not for audio logic).
float engine_get_param(uint16_t id);

#ifdef __cplusplus
}
#endif
