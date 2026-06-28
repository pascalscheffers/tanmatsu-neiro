// engine/voice_alloc.cpp — polyphonic voice allocator implementation.
// See voice_alloc.h for policy documentation.
#include "voice_alloc.h"
#include <stdint.h>

void VoiceAlloc::init(SynthModel* model) {
    for (int i = 0; i < kNumVoices; i++) {
        slots_[i].voice     = model->make_voice();
        slots_[i].pitch     = 0;
        slots_[i].gate      = false;
        slots_[i].timestamp = 0;
    }
}

void VoiceAlloc::note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    // Retrigger if the same pitch is already held (no double-allocate).
    int idx = find_slot_for_pitch(pitch);
    if (idx < 0) idx = find_free_slot();
    if (idx < 0) {
        idx = find_steal_slot();
        slots_[idx].voice->reset();
    }
    VoiceSlot& s = slots_[idx];
    s.pitch      = pitch;
    s.gate       = true;
    s.timestamp  = ++tick_;
    s.voice->note_on(pitch, velocity, expr);
}

void VoiceAlloc::note_off(uint8_t pitch) {
    for (int i = 0; i < kNumVoices; i++) {
        if (slots_[i].gate && slots_[i].pitch == pitch) {
            slots_[i].gate = false;
            slots_[i].voice->note_off();
        }
    }
}

void VoiceAlloc::reset_all() {
    for (int i = 0; i < kNumVoices; i++) {
        slots_[i].gate = false;
        slots_[i].voice->reset();
    }
}

int VoiceAlloc::find_slot_for_pitch(uint8_t pitch) const {
    for (int i = 0; i < kNumVoices; i++) {
        if (slots_[i].gate && slots_[i].pitch == pitch) return i;
    }
    return -1;
}

int VoiceAlloc::find_free_slot() const {
    // Pass 1: prefer completely idle (envelope done), oldest timestamp first.
    int      best   = -1;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < kNumVoices; i++) {
        if (!slots_[i].gate && !slots_[i].voice->is_active()) {
            if (slots_[i].timestamp < oldest) {
                oldest = slots_[i].timestamp;
                best   = i;
            }
        }
    }
    if (best >= 0) return best;

    // Pass 2: released but tail still running, oldest first.
    oldest = UINT32_MAX;
    for (int i = 0; i < kNumVoices; i++) {
        if (!slots_[i].gate) {
            if (slots_[i].timestamp < oldest) {
                oldest = slots_[i].timestamp;
                best   = i;
            }
        }
    }
    return best;
}

int VoiceAlloc::find_steal_slot() const {
    // Steal the oldest (lowest timestamp) gated voice.
    int oldest = 0;
    for (int i = 1; i < kNumVoices; i++) {
        if (slots_[i].timestamp < slots_[oldest].timestamp) oldest = i;
    }
    return oldest;
}
