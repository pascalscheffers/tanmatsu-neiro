// engine/voice_alloc.h — model-agnostic polyphonic voice allocator (Stage 1c).
//
// Fixed pool of kNumVoices slots populated by SynthModel::make_voice() at init.
// Steal policy (oldest-first at each tier):
//   1. Idle slot       (gate down + envelope done)
//   2. Released slot   (gate down, release tail still running)
//   3. Steal oldest gated voice
// O(n) in all operations.
//
// Stage 3d-i additions — play modes (mono, portamento, legato/retrigger):
//   PlayMode::kPoly    — original behaviour (multi-voice, no change).
//   PlayMode::kMono    — one voice, last-note priority, envelopes retrigger on
//                        each new note (even while a prior note is held).
//   PlayMode::kLegato  — one voice, last-note priority, envelopes continue when
//                        a new note arrives while a prior note is held (legato
//                        transition); retrigger only when all prior notes have
//                        been released before the new note.
//   Portamento applies in kMono and kLegato when portamento_time > 0. The
//   allocator tracks a per-block glide offset (semitones) and advances it each
//   call to advance_glide(). The voice receives the offset via IVoice::set_pitch_offset().
//
//   Note stack (mono): overlap detection uses a fixed 8-slot last-note list. When
//   note_off is called for a held note, the list pops back to the previous sounding
//   note — classic "note steal back" mono behaviour.
//
// Stage 3d-ii additions — unison (voice stacking + detune):
//   set_unison_count(U): U voices allocated per note_on (U=1 = no change).
//   set_unison_detune(cents): symmetric spread across ±cents/2 applied via
//   IVoice::set_pitch_offset(). Draws from the existing fixed pool; effective
//   polyphony = floor(kNumVoices / U). Pool exhaustion handled by the normal
//   steal policy. note_off releases all U voices for the note together.
//   Mono + unison: U detuned voices for the single held note (steal-back rebuilds
//   the full group). Portamento applies to the group's centre (index 0) voice;
//   all group members share the same base pitch.
#pragma once

#include <stdint.h>
#include "synth_config.h"
#include "synth_model.h"
#include "voice.h"

struct VoiceSlot {
    IVoice*  voice;      // owned; created by SynthModel::make_voice()
    uint8_t  pitch;      // note currently playing (0 when never assigned)
    bool     gate;       // true while note_on (key held)
    uint32_t timestamp;  // monotonic; incremented each note_on for oldest-first
};

// Play mode selector (matches PLAY_MODE param values).
enum class PlayMode : uint8_t {
    kPoly   = 0,  // standard polyphony (default)
    kMono   = 1,  // mono + retrigger
    kLegato = 2,  // mono + legato (no retrigger while key held)
};

class VoiceAlloc {
public:
    // Populate the pool by calling model->make_voice() kNumVoices times.
    // Not RT-safe (allocates). Call once before audio starts.
    void init(SynthModel* model);

    // Set the play mode (poly / mono+retrigger / mono+legato).
    // RT-safe: called from synth_render() at block boundary.
    void set_play_mode(PlayMode mode);

    // Set portamento glide time in seconds (0 = snap, no glide).
    // RT-safe: called from synth_render() at block boundary.
    void set_portamento_time(float seconds);

    // Set unison voice count (1 = off / no unison, 2..kNumVoices = stacked).
    // RT-safe: called from synth_render() at block boundary.
    void set_unison_count(int count);

    // Set unison detune spread in cents (total spread; 0 = no detune).
    // Voices are distributed evenly across ±(detune/2) cents.
    // RT-safe: called from synth_render() at block boundary.
    void set_unison_detune(float cents);

    // Advance the portamento glide by one block_time seconds.
    // Must be called once per audio block (before note events are drained is fine).
    // Updates the glide offset and pushes it to the active mono voice via
    // set_pitch_offset(). RT-safe (no alloc, no log).
    void advance_glide(float block_time_secs);

    // Start a note. Finds, reclaims, or steals a slot.
    void note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr);

    // Begin release for the given pitch. No-op if pitch is not active.
    void note_off(uint8_t pitch);

    // Force all voices silent/idle immediately (MIDI panic).
    void reset_all();

    // Read-only slot access for the render loop in synth.cpp.
    const VoiceSlot* slots() const { return slots_; }

    // White-box accessor for host tests: current glide offset (semitones).
    float glide_offset() const { return glide_offset_; }

private:
    VoiceSlot slots_[kNumVoices] = {};
    uint32_t  tick_              = 0;

    // Play mode state.
    PlayMode play_mode_       = PlayMode::kPoly;
    float    portamento_time_ = 0.0f;  // seconds; < 0.001 treated as zero

    // Mono voice state.
    // mono_slot_: index of the one active mono voice (-1 = none).
    int mono_slot_ = -1;

    // Glide state: current pitch offset in semitones (approaching 0 when note is
    // held, or ramping from old→new when a new note is triggered).
    float glide_offset_ = 0.0f;  // current offset (semitones) applied to pitch
    float glide_rate_   = 0.0f;  // semitones per second (magnitude); direction implicit

    // Mono note stack: tracks held notes in order for "steal-back" on note_off.
    // Uses last-note priority: newest is at the top (index mono_stack_top_-1).
    // Maximum depth = kNumVoices (more held notes than voices is pathological but safe).
    static constexpr int kMonoStackMax              = kNumVoices;
    uint8_t              mono_stack_[kMonoStackMax] = {};
    int                  mono_stack_top_            = 0;

    // Unison state.
    // Each slot carries a group tag (the pitch of the note it belongs to, or 0xFF =
    // ungrouped). tag 0xFF is safe because MIDI pitch 0xFF is out of range.
    static constexpr uint8_t kNoGroup   = 0xFF;
    static constexpr int     kMaxUnison = kNumVoices;

    int     unison_count_           = 1;     // U voices per note; 1 = off (unchanged behaviour)
    float   unison_detune_          = 0.0f;  // total spread in cents (0 = no detune)
    uint8_t unison_tag_[kNumVoices] = {};    // group tag per slot (kNoGroup = no group)

    // Internal helpers.
    int find_slot_for_pitch(uint8_t pitch) const;
    int find_free_slot() const;
    int find_steal_slot() const;

    // Unison helpers.
    // Allocate up to unison_count_ slots for a note (fewer if pool is lean),
    // call voice->note_on + spread detune offsets.
    void note_on_unison(uint8_t pitch, uint8_t velocity, NoteExpression expr);
    // Release all slots tagged with pitch (if U=1 falls through to the slot tagged pitch).
    void note_off_unison(uint8_t pitch);
    // Apply symmetric detune offsets to slots with the given tag.
    void apply_detune(uint8_t tag, int slot_count, int first_slot_idx);

    // Mono-specific note_on / note_off.
    void note_on_mono(uint8_t pitch, uint8_t velocity, NoteExpression expr);
    void note_off_mono(uint8_t pitch);

    // Push/pop/remove helpers for the mono note stack.
    void stack_push(uint8_t pitch);
    void stack_remove(uint8_t pitch);
};
