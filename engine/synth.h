// synth.h — the engine's audio entry point.
//
// Stage 0 is a placeholder sine generator; the real Juno voice arrives in
// Stage 1. The signature is the permanent render contract (spec 04): the HAL
// owns the thread, the engine fills stereo float buffers in [-1, 1].
#pragma once

#include <stdbool.h>
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
int      engine_clock_running(void);   // 1 if transport is running, 0 if stopped
uint64_t engine_clock_tick_pos(void);  // ticks elapsed since last start()
float    engine_clock_bpm(void);       // current BPM

// --- Event scheduler (ADR 0010, Stage 4a-ii) --------------------------------
// Schedule a note event to fire at a specific sample-time. Lock-free:
// control-thread safe (pushes into s_sched_in SpscRing). sample_time is in
// Clock::sample_pos() units (samples since the last transport start()).
// The event fires on the first audio block whose start <= sample_time < start+frames.
void engine_schedule_note(uint64_t sample_time, uint8_t pitch, uint8_t velocity, int on);

// --- MIDI expression (Stage 5c) — channel-wide (omni), lock-free control thread.
// Continuous controllers use latest-value-wins atomics (no queue to overflow).

// Pitch-bend, bipolar normalized [-1, +1] (router maps 14-bit, center 0x2000 → 0).
// Applied directly to all voices' pitch over kPitchBendRangeSemis.
void engine_set_pitch_bend(float norm_bipolar);

// Mod-wheel (CC1), [0, 1]. Fed to every voice as the MOD_WHEEL mod source.
void engine_set_mod_wheel(float norm);

// Channel aftertouch, [0, 1]. Fed to every voice as the AFTERTOUCH mod source.
void engine_set_aftertouch(float norm);

// CC7 channel volume, [0, 1] (ADR 0021). Attenuation-only; square-law taper applied
// by the caller. Multiplied into the output gain as MASTER_GAIN × channel_vol × 1/√U.
// Not a preset value — performance state only. Default 1.0 (unity); reset on init/panic.
void engine_set_channel_volume(float vol);

// Panic: release/silence all voices immediately and clear the arp held set.
void engine_all_notes_off(void);

// Map a MIDI CC number to a ParamId, using ParamDesc.midi_cc (the table is the
// single CC→param map). Returns 0 (ParamId none/invalid) if no row uses this CC.
// Control-thread helper for the MIDI router (Stage 5c-iii).
uint16_t engine_cc_to_param(uint8_t cc);

// Diagnostic (SYNTH_PROFILE): snapshot + reset the master-chain signal-magnitude probe.
// Benign cross-core race acceptable for a diagnostic readout.
// pk_mono:     peak of the raw voice-sum (pre-gain) since the last call.
// pk_postgain: peak value fed into the limiter (post-gain, pre-GR) since the last call.
// min_gr:      worst (lowest) limiter gain-reduction factor seen since the last call.
// pk_out:      peak output after soft_clip since the last call.
// Returns zeros in all four outputs when SYNTH_PROFILE is not defined.
void engine_profile_read(float* pk_mono, float* pk_postgain, float* min_gr, float* pk_out);

// Diagnostic (SYNTH_PROFILE): per-region CPU sub-timers, per-block AVG and MAX in
// CPU cycles (divide by platform_cycles_per_sec()/1e6 for us). The four regions
// tile the whole audio block; the MAX fields expose the smash-crackle spike (one
// rare block/window) that an avg-only readout hides. Zeros when off.
// Stage 8 diag: worst-block snapshot fields. avg/max hides the one rare bad
// block's full shape; these freeze it (selection key: largest voices-region
// cycle cost seen in the read-window). Read them together as the discriminator:
//   ipc = worst_voices_instret / worst_voices_cyc, low + instret flat vs a
//     normal block -> memory/cache stall; worst_vmax_cyc dominating
//     worst_voices_cyc -> one cold voice (PSRAM/wavetable); vmax small vs
//     voices -> spread/global stall (flash I-cache XIP).
//   instret high with worst_active low -> preemption by another task.
//   instret high scaling with worst_active, ipc normal -> genuine compute in
//     voice render.
typedef struct {
    uint32_t drain_avg, drain_max;    // steps 1..1b: note/clock/sched drain
    uint32_t setup_avg, setup_max;    // steps 2..4: param drain/push, arp, LFO, chorus
    uint32_t voices_avg, voices_max;  // step 5: voice-sum loop
    uint32_t master_avg, master_max;  // step 6: master chain loop

    uint32_t worst_voices_cyc;      // the worst block's voices-region cycle cost (selection key)
    uint32_t worst_voices_instret;  // retired instructions in that same block/region
    uint32_t worst_active;          // active voice count in that block
    uint32_t worst_vmax_cyc;        // single worst voice's render() cycle cost in that block
    uint32_t worst_vmax_idx;        // that voice's slot index
    uint32_t worst_drain_cyc;       // that block's drain-region cost (context)
    uint32_t worst_setup_cyc;       // that block's setup-region cost (context)
    uint32_t worst_master_cyc;      // that block's master-region cost (context)
} EngineCpuProfile;
void engine_profile_read_cpu(EngineCpuProfile* out);

#ifdef __cplusplus
}
#endif
