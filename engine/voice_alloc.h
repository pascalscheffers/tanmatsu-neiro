// engine/voice_alloc.h — model-agnostic polyphonic voice allocator (Stage 1c).
//
// Fixed pool of kNumVoices slots populated by SynthModel::make_voice() at init.
// Steal policy (oldest-first at each tier):
//   1. Idle slot       (gate down + envelope done)
//   2. Released slot   (gate down, release tail still running)
//   3. Steal oldest gated voice
// O(n) in all operations.
#pragma once

#include "voice.h"
#include "synth_model.h"
#include "synth_config.h"
#include <stdint.h>

struct VoiceSlot {
    IVoice*  voice;      // owned; created by SynthModel::make_voice()
    uint8_t  pitch;      // note currently playing (0 when never assigned)
    bool     gate;       // true while note_on (key held)
    uint32_t timestamp;  // monotonic; incremented each note_on for oldest-first
};

class VoiceAlloc {
public:
    // Populate the pool by calling model->make_voice() kNumVoices times.
    // Not RT-safe (allocates). Call once before audio starts.
    void init(SynthModel* model);

    // Start a note. Finds, reclaims, or steals a slot.
    void note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr);

    // Begin release for the given pitch. No-op if pitch is not active.
    void note_off(uint8_t pitch);

    // Force all voices silent/idle immediately (MIDI panic).
    void reset_all();

    // Read-only slot access for the render loop in synth.cpp.
    const VoiceSlot* slots() const { return slots_; }

private:
    VoiceSlot slots_[kNumVoices] = {};
    uint32_t  tick_              = 0;

    int find_slot_for_pitch(uint8_t pitch) const;
    int find_free_slot() const;
    int find_steal_slot() const;
};
