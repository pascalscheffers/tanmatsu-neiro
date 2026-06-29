// synth.h — the engine's audio entry point.
//
// Stage 0 is a placeholder sine generator; the real Juno voice arrives in
// Stage 1. The signature is the permanent render contract (spec 04): the HAL
// owns the thread, the engine fills stereo float buffers in [-1, 1].
#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declaration for C-compatible use in the engine_set_routings prototype.
// C++ callers can #include "mod_matrix.h" for the full Routing definition.
#ifdef __cplusplus
#include "mod_matrix.h"
#else
struct Routing;  // opaque in C; see engine/mod_matrix.h for the full definition
#endif

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

// Load a complete set of modulation routings into every active voice.
// Called from the control thread (preset load, init).  The routings are copied
// into each voice's ModMatrix immediately; audio thread picks them up next block.
// `routings`: array of Routing records to load.
// `count`: number of valid entries in `routings` (max kMaxRoutes).
// Slots beyond `count` are cleared (set to NONE/0).
void engine_set_routings(const struct Routing* routings, int count);

// --- Master musical clock (ADR 0010) ----------------------------------------
// Lock-free: each setter enqueues a ClockCmd for the audio thread to drain.
// The tap is also enqueued so free_pos_ is read on the audio thread.

// Set the tempo in BPM. Clamped to [20, 300] on the audio thread.
void engine_set_bpm(float bpm);

// Start transport: resets tick/sample position to 0 then begins running.
void engine_transport_start(void);

// Stop transport: halts; positions preserved.
void engine_transport_stop(void);

// Continue transport: resumes from the current position without reset.
void engine_transport_continue(void);

// Tap tempo: records a tap timestamp; two taps set BPM from the interval.
// Enqueued so the actual timestamp is captured on the audio thread.
void engine_tap_tempo(void);

// Read-only clock queries — control-thread safe; frame-stale, display use only.
int      engine_clock_running(void);      // 1 if transport is running, 0 if stopped
uint64_t engine_clock_tick_pos(void);     // ticks elapsed since last start()
float    engine_clock_bpm(void);          // current BPM

#ifdef __cplusplus
}
#endif
