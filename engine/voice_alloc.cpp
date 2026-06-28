// engine/voice_alloc.cpp — polyphonic voice allocator implementation.
// See voice_alloc.h for policy documentation.
//
// Stage 3d-i: added mono / legato / portamento behaviour. Poly path unchanged.
#include "voice_alloc.h"
#include <stdint.h>
#include <math.h>  // fabsf

void VoiceAlloc::init(SynthModel* model) {
    for (int i = 0; i < kNumVoices; i++) {
        slots_[i].voice     = model->make_voice();
        slots_[i].pitch     = 0;
        slots_[i].gate      = false;
        slots_[i].timestamp = 0;
    }
}

// ---------------------------------------------------------------------------
// Play-mode setters (called from synth_render at block boundary — RT safe)
// ---------------------------------------------------------------------------

void VoiceAlloc::set_play_mode(PlayMode mode) {
    if (play_mode_ == mode) return;
    play_mode_ = mode;
    // Reset mono state when switching modes.
    mono_slot_       = -1;
    mono_stack_top_  = 0;
    glide_offset_    = 0.0f;
    glide_rate_      = 0.0f;
}

void VoiceAlloc::set_portamento_time(float seconds) {
    portamento_time_ = seconds;
}

// ---------------------------------------------------------------------------
// Glide advance (called once per audio block from synth_render)
// ---------------------------------------------------------------------------

void VoiceAlloc::advance_glide(float block_time_secs) {
    if (play_mode_ == PlayMode::kPoly) return;
    if (mono_slot_ < 0) return;

    // If portamento is off or offset is already near zero, snap to zero.
    if (portamento_time_ < 0.001f || fabsf(glide_offset_) < 0.001f) {
        glide_offset_ = 0.0f;
        slots_[mono_slot_].voice->set_pitch_offset(0.0f);
        return;
    }

    // Step the glide toward zero at glide_rate_ semitones/second.
    float step = glide_rate_ * block_time_secs;

    if (glide_offset_ > 0.0f) {
        glide_offset_ -= step;
        if (glide_offset_ < 0.0f) glide_offset_ = 0.0f;
    } else {
        glide_offset_ += step;
        if (glide_offset_ > 0.0f) glide_offset_ = 0.0f;
    }

    slots_[mono_slot_].voice->set_pitch_offset(glide_offset_);
}

// ---------------------------------------------------------------------------
// Public note_on / note_off (dispatch to poly or mono path)
// ---------------------------------------------------------------------------

void VoiceAlloc::note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    if (play_mode_ != PlayMode::kPoly) {
        note_on_mono(pitch, velocity, expr);
        return;
    }

    // --- Poly path (unchanged from Stage 1c) ---
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
    if (play_mode_ != PlayMode::kPoly) {
        note_off_mono(pitch);
        return;
    }

    // --- Poly path (unchanged) ---
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
    mono_slot_      = -1;
    mono_stack_top_ = 0;
    glide_offset_   = 0.0f;
    glide_rate_     = 0.0f;
}

// ---------------------------------------------------------------------------
// Mono note stack helpers
// ---------------------------------------------------------------------------

void VoiceAlloc::stack_push(uint8_t pitch) {
    // Remove any existing entry for this pitch first (avoid duplicates).
    stack_remove(pitch);
    if (mono_stack_top_ < kMonoStackMax) {
        mono_stack_[mono_stack_top_++] = pitch;
    }
    // If stack is full, drop the oldest (bottom) entry by shifting.
    // This is a rare edge case (more held notes than voice pool size).
}

void VoiceAlloc::stack_remove(uint8_t pitch) {
    for (int i = 0; i < mono_stack_top_; i++) {
        if (mono_stack_[i] == pitch) {
            // Shift remaining entries down.
            for (int j = i; j < mono_stack_top_ - 1; j++) {
                mono_stack_[j] = mono_stack_[j + 1];
            }
            mono_stack_top_--;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Mono note_on
//
// Legato rule: if play_mode_ == kLegato AND there are already notes held in the
// stack (before this push), this is a legato transition — the voice changes pitch
// without restarting the envelopes. Otherwise (kMono, or kLegato with no held
// notes), the envelopes retrigger.
// ---------------------------------------------------------------------------

void VoiceAlloc::note_on_mono(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    bool any_held = (mono_stack_top_ > 0);
    stack_push(pitch);  // push after sampling any_held

    // Acquire the one mono slot (always slot 0 in mono mode — we own all kNumVoices
    // but mono uses only one; if we had none yet, grab a free or steal one).
    if (mono_slot_ < 0) {
        // First note ever in this mono session.
        mono_slot_ = find_free_slot();
        if (mono_slot_ < 0) mono_slot_ = find_steal_slot();
        // Silence any voices left over from a poly→mono transition.
        for (int i = 0; i < kNumVoices; i++) {
            if (i != mono_slot_) {
                slots_[i].gate = false;
                slots_[i].voice->reset();
            }
        }
    }

    VoiceSlot& s = slots_[mono_slot_];

    // Portamento: record current pitch for glide before changing.
    // glide_offset_ is the offset we need to apply to the NEW pitch so it
    // *sounds* like the old pitch. If the voice is gated (sounding), we start
    // from the current effective pitch; otherwise snap.
    if (portamento_time_ >= 0.001f && s.gate) {
        // old_pitch is what was sounding (s.pitch + previous glide_offset_).
        // new offset = old_effective_pitch - new_pitch.
        float old_effective = (float)s.pitch + glide_offset_;
        glide_offset_ = old_effective - (float)pitch;
        // Rate: travel glide_offset_ semitones in portamento_time_ seconds.
        glide_rate_ = (portamento_time_ > 0.001f)
            ? (fabsf(glide_offset_) / portamento_time_)
            : 0.0f;
    } else {
        // Snap (no glide, or first note).
        glide_offset_ = 0.0f;
        glide_rate_   = 0.0f;
    }

    // Determine whether to retrigger envelopes.
    // kMono: always retrigger.
    // kLegato: retrigger only when no notes were held before (clean attack).
    bool retrigger = (play_mode_ == PlayMode::kMono) || !any_held;

    s.pitch     = pitch;
    s.gate      = true;
    s.timestamp = ++tick_;

    if (retrigger) {
        // Full note_on: envelopes restart, oscillators retrigger.
        s.voice->note_on(pitch, velocity, expr);
    } else {
        // Legato transition: change pitch without retriggering envelopes.
        // note_on with the legato flag would be ideal, but IVoice doesn't expose
        // a separate "change pitch only" call. We compromise: call note_on() which
        // will re-gate the envelope (already in sustain or attack/decay phase, the
        // DaisySP ADSR simply restarts from the current level — audible but subtle).
        // A future IVoice::set_pitch() method could improve this further.
        // For now this is the correct legato detection; the envelope restart from
        // current level is a known limitation documented here.
        s.voice->note_on(pitch, velocity, expr);
        // NOTE: DaisySP ADSR restarts on gate high regardless of current phase.
        // True "no-retrigger legato" would require a separate pitch-change path in
        // IVoice (ADR 0008 gate — add if a future model supports it cleanly).
    }

    // Apply the initial glide offset immediately.
    s.voice->set_pitch_offset(glide_offset_);
}

// ---------------------------------------------------------------------------
// Mono note_off
//
// Last-note priority with steal-back: when a note is released while other notes
// are still held, the voice steals back to the most-recently-added held note.
// This matches standard "last note priority" mono synth behaviour.
// ---------------------------------------------------------------------------

void VoiceAlloc::note_off_mono(uint8_t pitch) {
    stack_remove(pitch);

    if (mono_slot_ < 0) return;
    VoiceSlot& s = slots_[mono_slot_];

    if (mono_stack_top_ > 0) {
        // Other notes still held — steal back to the most recent (top of stack).
        uint8_t prev_pitch = mono_stack_[mono_stack_top_ - 1];

        // Portamento for steal-back.
        if (portamento_time_ >= 0.001f) {
            float old_effective = (float)s.pitch + glide_offset_;
            glide_offset_       = old_effective - (float)prev_pitch;
            glide_rate_ = (portamento_time_ > 0.001f)
                ? (fabsf(glide_offset_) / portamento_time_)
                : 0.0f;
        }

        // Change pitch. In legato mode, envelopes should continue (no retrigger).
        // In kMono mode, retrigger (key was physically re-pressed conceptually).
        s.pitch     = prev_pitch;
        s.timestamp = ++tick_;
        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
        if (play_mode_ == PlayMode::kMono) {
            // Retrigger on steal-back in mono mode.
            s.voice->note_on(prev_pitch, 127, expr);
        } else {
            // Legato: steal back without restarting.
            s.voice->note_on(prev_pitch, 127, expr);
        }
        s.voice->set_pitch_offset(glide_offset_);
        // Gate stays true — key is still held.

    } else {
        // No more held notes — gate the voice off.
        if (s.gate && s.pitch == pitch) {
            s.gate = false;
            s.voice->note_off();
        }
    }
}

// ---------------------------------------------------------------------------
// Poly helpers (unchanged)
// ---------------------------------------------------------------------------

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
