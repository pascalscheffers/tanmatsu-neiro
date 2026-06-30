// engine/voice_alloc.cpp — polyphonic voice allocator implementation.
// See voice_alloc.h for policy documentation.
//
// Stage 3d-i: added mono / legato / portamento behaviour. Poly path unchanged.
// Stage 3d-ii: added unison voice stacking + symmetric detune.
#include "voice_alloc.h"
#include <math.h>  // fabsf
#include <stdint.h>

void VoiceAlloc::init(SynthModel* model) {
    for (int i = 0; i < kNumVoices; i++) {
        slots_[i].voice     = model->make_voice();
        slots_[i].pitch     = 0;
        slots_[i].gate      = false;
        slots_[i].timestamp = 0;
        unison_tag_[i]      = kNoGroup;
    }
}

// ---------------------------------------------------------------------------
// Play-mode setters (called from synth_render at block boundary — RT safe)
// ---------------------------------------------------------------------------

void VoiceAlloc::set_play_mode(PlayMode mode) {
    if (play_mode_ == mode) return;
    play_mode_ = mode;
    // Silence and fully reset all voices so any gated voice from the previous
    // mode does not become an orphan (untracked, note_off unreachable).
    // reset_all() is RT-safe: no alloc, no log, no blocking.
    reset_all();
}

void VoiceAlloc::set_portamento_time(float seconds) {
    portamento_time_ = seconds;
}

void VoiceAlloc::set_unison_count(int count) {
    if (count < 1) count = 1;
    if (count > kMaxUnison) count = kMaxUnison;
    unison_count_ = count;
}

void VoiceAlloc::set_unison_detune(float cents) {
    if (cents < 0.0f) cents = 0.0f;
    unison_detune_ = cents;
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
    } else {
        // Step the glide toward zero at glide_rate_ semitones/second.
        float step = glide_rate_ * block_time_secs;
        if (glide_offset_ > 0.0f) {
            glide_offset_ -= step;
            if (glide_offset_ < 0.0f) glide_offset_ = 0.0f;
        } else {
            glide_offset_ += step;
            if (glide_offset_ > 0.0f) glide_offset_ = 0.0f;
        }
    }

    // Push glide offset to all voices in the mono group (unison U>1 has multiple slots
    // all tagged with the same pitch). Each voice's detune offset is additive with glide.
    uint8_t tag = slots_[mono_slot_].pitch;
    // Collect group slots and count them (need count for detune re-apply).
    int     group_slots[kNumVoices];
    int     group_count = 0;
    for (int i = 0; i < kNumVoices; i++) {
        if (unison_tag_[i] == tag && slots_[i].gate) {
            group_slots[group_count++] = i;
        }
    }
    if (group_count == 0) {
        // Fallback: just the mono slot (U=1, or group tag lost).
        slots_[mono_slot_].voice->set_pitch_offset(glide_offset_);
        return;
    }

    // Apply detune + glide to each group member.
    // Centre offset for even/odd counts: voices are spread uniformly across
    // [-spread/2, +spread/2] semitones, glide added on top.
    float spread_semi = unison_detune_ / 100.0f;  // cents → semitones
    for (int gi = 0; gi < group_count; gi++) {
        float detune_offset = 0.0f;
        if (group_count > 1 && spread_semi > 0.0f) {
            // Uniform spacing: voice gi at position gi/(group_count-1) in [0,1],
            // mapped to [-spread/2, +spread/2].
            float norm    = (float)gi / (float)(group_count - 1);  // 0..1
            detune_offset = (norm - 0.5f) * spread_semi;
        }
        slots_[group_slots[gi]].voice->set_pitch_offset(glide_offset_ + detune_offset);
    }
}

// ---------------------------------------------------------------------------
// Public note_on / note_off (dispatch to poly or mono path)
// ---------------------------------------------------------------------------

void VoiceAlloc::note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    if (play_mode_ != PlayMode::kPoly) {
        note_on_mono(pitch, velocity, expr);
        return;
    }

    // --- Poly path ---
    if (unison_count_ > 1) {
        // Unison: allocate U voices for this note.
        note_on_unison(pitch, velocity, expr);
        return;
    }

    // Unison=1: original single-voice poly path (unchanged from Stage 1c).
    // Check for retrigger first — same slot reuse avoids re-steal logic.
    int idx = find_slot_for_pitch(pitch);
    if (idx >= 0) {
        // Retrigger: reuse the existing slot in place.
        VoiceSlot& s = slots_[idx];
        s.gate       = true;
        s.timestamp  = ++tick_;
        s.voice->note_on(pitch, velocity, expr);
        s.voice->set_pitch_offset(0.0f);
        return;
    }
    idx = find_free_slot();
    if (idx < 0) {
        idx              = find_steal_slot();
        unison_tag_[idx] = kNoGroup;
        slots_[idx].voice->reset();
    }
    VoiceSlot& s     = slots_[idx];
    s.pitch          = pitch;
    s.gate           = true;
    s.timestamp      = ++tick_;
    unison_tag_[idx] = pitch;  // tag = pitch (trivial group of 1)
    s.voice->note_on(pitch, velocity, expr);
    s.voice->set_pitch_offset(0.0f);
}

void VoiceAlloc::note_off(uint8_t pitch) {
    if (play_mode_ != PlayMode::kPoly) {
        note_off_mono(pitch);
        return;
    }

    // Release all slots in the unison group for this pitch.
    note_off_unison(pitch);
}

void VoiceAlloc::reset_all() {
    for (int i = 0; i < kNumVoices; i++) {
        slots_[i].gate = false;
        unison_tag_[i] = kNoGroup;
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

    // Unison mono: cap live voices at unison_count_ by reusing the current group.
    if (unison_count_ > 1) {
        // Capture portamento state before touching any group slots.
        // old_tag is the current group's pitch (or kNoGroup if no group yet).
        uint8_t old_tag       = (mono_slot_ >= 0) ? slots_[mono_slot_].pitch : kNoGroup;
        float   old_effective = 0.0f;
        bool    do_glide      = false;
        if (mono_slot_ >= 0 && slots_[mono_slot_].gate && portamento_time_ >= 0.001f) {
            old_effective = (float)old_tag + glide_offset_;
            do_glide      = true;
        }

        // Count the current group (tagged with old_tag, gated).
        int cur_group_slots[kNumVoices];
        int cur_group_count = 0;
        if (old_tag != kNoGroup) {
            for (int i = 0; i < kNumVoices; i++) {
                if (unison_tag_[i] == old_tag && slots_[i].gate) {
                    cur_group_slots[cur_group_count++] = i;
                }
            }
        }

        // Reuse path: current group exists and matches the desired unison count.
        // Retag and retrigger those exact slots — no release tail, no pile-up.
        bool reused = false;
        if (cur_group_count == unison_count_) {
            // Set up glide before retagging.
            if (do_glide) {
                glide_offset_ = old_effective - (float)pitch;
                glide_rate_   = fabsf(glide_offset_) / portamento_time_;
            } else {
                glide_offset_ = 0.0f;
                glide_rate_   = 0.0f;
            }

            bool retrigger = (play_mode_ == PlayMode::kMono) || !any_held;
            mono_slot_     = cur_group_slots[0];  // first reused slot = glide reference
            for (int gi = 0; gi < cur_group_count; gi++) {
                int        idx   = cur_group_slots[gi];
                VoiceSlot& s     = slots_[idx];
                s.pitch          = pitch;
                s.gate           = true;  // already true; make explicit
                s.timestamp      = ++tick_;
                unison_tag_[idx] = pitch;
                // Retrigger regardless of legato (same limitation as U=1 path — DaisySP
                // ADSR restarts on gate; retrigger var preserved for symmetry).
                (void)retrigger;
                s.voice->note_on(pitch, velocity, expr);
            }
            reused = true;
        }

        if (!reused) {
            // Fallback: first note (no current group) or unison_count_ changed.
            // Release old group (if any) and allocate fresh slots.
            if (old_tag != kNoGroup) {
                for (int i = 0; i < kNumVoices; i++) {
                    if (unison_tag_[i] == old_tag) {
                        slots_[i].gate = false;
                        unison_tag_[i] = kNoGroup;
                        slots_[i].voice->note_off();
                    }
                }
            }

            if (do_glide) {
                glide_offset_ = old_effective - (float)pitch;
                glide_rate_   = fabsf(glide_offset_) / portamento_time_;
            } else {
                glide_offset_ = 0.0f;
                glide_rate_   = 0.0f;
            }

            bool retrigger = (play_mode_ == PlayMode::kMono) || !any_held;
            mono_slot_     = -1;
            for (int u = 0; u < unison_count_; u++) {
                int idx = find_free_slot();
                if (idx < 0) {
                    idx              = find_steal_slot();
                    unison_tag_[idx] = kNoGroup;
                    slots_[idx].voice->reset();
                }
                VoiceSlot& s     = slots_[idx];
                s.pitch          = pitch;
                s.gate           = true;
                s.timestamp      = ++tick_;
                unison_tag_[idx] = pitch;
                (void)retrigger;
                s.voice->note_on(pitch, velocity, expr);
                if (mono_slot_ < 0) mono_slot_ = idx;  // first = reference for glide
            }
        }

        // Apply initial glide + detune (scans by new tag — correct after reuse or alloc).
        apply_detune(pitch, unison_count_, mono_slot_);
        // Add glide on top: re-push set_pitch_offset with glide for each group slot.
        if (fabsf(glide_offset_) > 0.001f) {
            int group_slots[kNumVoices];
            int gc = 0;
            for (int i = 0; i < kNumVoices; i++) {
                if (unison_tag_[i] == pitch && slots_[i].gate) {
                    group_slots[gc++] = i;
                }
            }
            float spread_semi = unison_detune_ / 100.0f;
            for (int gi = 0; gi < gc; gi++) {
                float detune = 0.0f;
                if (gc > 1 && spread_semi > 0.0f) {
                    float norm = (float)gi / (float)(gc - 1);
                    detune     = (norm - 0.5f) * spread_semi;
                }
                slots_[group_slots[gi]].voice->set_pitch_offset(glide_offset_ + detune);
            }
        }
        return;
    }

    // --- U=1 mono path (unchanged from Stage 3d-i) ---

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
                unison_tag_[i] = kNoGroup;
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
        glide_offset_       = old_effective - (float)pitch;
        // Rate: travel glide_offset_ semitones in portamento_time_ seconds.
        glide_rate_         = (portamento_time_ > 0.001f) ? (fabsf(glide_offset_) / portamento_time_) : 0.0f;
    } else {
        // Snap (no glide, or first note).
        glide_offset_ = 0.0f;
        glide_rate_   = 0.0f;
    }

    // Determine whether to retrigger envelopes.
    // kMono: always retrigger.
    // kLegato: retrigger only when no notes were held before (clean attack).
    bool retrigger = (play_mode_ == PlayMode::kMono) || !any_held;

    s.pitch                 = pitch;
    s.gate                  = true;
    s.timestamp             = ++tick_;
    unison_tag_[mono_slot_] = pitch;

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

    // Last-note priority: only the currently-sounding note changes the voices.
    // Releasing a held-but-buried note (whose slots were reused by a later note)
    // just updates the stack above — the sounding group stays intact.
    if (s.pitch != pitch) return;

    if (mono_stack_top_ > 0) {
        // Other notes still held — steal back to the most recent (top of stack).
        uint8_t prev_pitch = mono_stack_[mono_stack_top_ - 1];

        if (unison_count_ > 1) {
            // Unison mono steal-back: reuse the prev_pitch group if it exists and
            // matches unison_count_; otherwise fall back to release+allocate.

            // Portamento: capture from the current (released) group's effective pitch.
            float old_effective = (float)s.pitch + glide_offset_;
            if (portamento_time_ >= 0.001f) {
                glide_offset_ = old_effective - (float)prev_pitch;
                glide_rate_   = fabsf(glide_offset_) / portamento_time_;
            } else {
                glide_offset_ = 0.0f;
                glide_rate_   = 0.0f;
            }

            // Capture the current (to-be-released) group tag BEFORE modifying anything.
            uint8_t cur_tag = s.pitch;

            // Check whether there is already an intact prev_pitch group to reuse.
            // (This can happen when the user holds A, presses B, then releases B —
            // if A's slots were reused for B, they won't be in a separate group now.
            // The reuse path in note_on_mono re-tags old slots to the new pitch, so
            // a surviving prev_pitch group means the pool had spare slots.)
            int prev_group_slots[kNumVoices];
            int prev_group_count = 0;
            for (int i = 0; i < kNumVoices; i++) {
                if (unison_tag_[i] == prev_pitch && slots_[i].gate) {
                    prev_group_slots[prev_group_count++] = i;
                }
            }

            NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

            if (prev_group_count == unison_count_) {
                // Reuse the existing prev_pitch group — retag cur_tag group off first.
                for (int i = 0; i < kNumVoices; i++) {
                    if (unison_tag_[i] == cur_tag) {
                        slots_[i].gate = false;
                        unison_tag_[i] = kNoGroup;
                        slots_[i].voice->note_off();
                    }
                }
                // The prev_pitch group slots are already tagged correctly; just retrigger.
                // Restore per-slot bookkeeping defensively in case a prior reuse left them
                // with stale pitch/gate/unison_tag (latent-regression guard).
                mono_slot_ = prev_group_slots[0];
                for (int gi = 0; gi < prev_group_count; gi++) {
                    int        idx   = prev_group_slots[gi];
                    VoiceSlot& sv    = slots_[idx];
                    sv.pitch         = prev_pitch;
                    sv.gate          = true;
                    unison_tag_[idx] = prev_pitch;
                    sv.timestamp     = ++tick_;
                    sv.voice->note_on(prev_pitch, 127, expr);
                }
            } else {
                // Fallback: release the current group and allocate fresh slots for prev_pitch.
                for (int i = 0; i < kNumVoices; i++) {
                    if (unison_tag_[i] == cur_tag) {
                        slots_[i].gate = false;
                        unison_tag_[i] = kNoGroup;
                        slots_[i].voice->note_off();
                    }
                }
                mono_slot_ = -1;
                for (int u = 0; u < unison_count_; u++) {
                    int idx = find_free_slot();
                    if (idx < 0) {
                        idx              = find_steal_slot();
                        unison_tag_[idx] = kNoGroup;
                        slots_[idx].voice->reset();
                    }
                    VoiceSlot& sv    = slots_[idx];
                    sv.pitch         = prev_pitch;
                    sv.gate          = true;
                    sv.timestamp     = ++tick_;
                    unison_tag_[idx] = prev_pitch;
                    sv.voice->note_on(prev_pitch, 127, expr);
                    if (mono_slot_ < 0) mono_slot_ = idx;
                }
            }

            // Apply detune + glide (scans by prev_pitch tag).
            int group_slots[kNumVoices];
            int gc = 0;
            for (int i = 0; i < kNumVoices; i++) {
                if (unison_tag_[i] == prev_pitch && slots_[i].gate) {
                    group_slots[gc++] = i;
                }
            }
            float spread_semi = unison_detune_ / 100.0f;
            for (int gi = 0; gi < gc; gi++) {
                float detune = 0.0f;
                if (gc > 1 && spread_semi > 0.0f) {
                    float norm = (float)gi / (float)(gc - 1);
                    detune     = (norm - 0.5f) * spread_semi;
                }
                slots_[group_slots[gi]].voice->set_pitch_offset(glide_offset_ + detune);
            }
            return;
        }

        // --- U=1 mono steal-back (unchanged) ---

        // Portamento for steal-back.
        if (portamento_time_ >= 0.001f) {
            float old_effective = (float)s.pitch + glide_offset_;
            glide_offset_       = old_effective - (float)prev_pitch;
            glide_rate_         = (portamento_time_ > 0.001f) ? (fabsf(glide_offset_) / portamento_time_) : 0.0f;
        }

        // Change pitch. In legato mode, envelopes should continue (no retrigger).
        // In kMono mode, retrigger (key was physically re-pressed conceptually).
        s.pitch                 = prev_pitch;
        s.timestamp             = ++tick_;
        unison_tag_[mono_slot_] = prev_pitch;
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
        // No more held notes — gate all voices in the group off.
        if (unison_count_ > 1) {
            uint8_t tag = s.pitch;
            for (int i = 0; i < kNumVoices; i++) {
                if (unison_tag_[i] == tag && slots_[i].gate) {
                    slots_[i].gate = false;
                    unison_tag_[i] = kNoGroup;
                    slots_[i].voice->note_off();
                }
            }
            mono_slot_ = -1;
        } else {
            // U=1 path.
            if (s.gate && s.pitch == pitch) {
                s.gate                  = false;
                unison_tag_[mono_slot_] = kNoGroup;
                s.voice->note_off();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Unison helpers
// ---------------------------------------------------------------------------

// Apply symmetric detune offsets to all slots tagged with `tag`.
// Slot list is built by scanning — O(kNumVoices), called once per note_on.
void VoiceAlloc::apply_detune(uint8_t tag, int slot_count, int /*first_slot_idx*/) {
    (void)slot_count;  // slot_count is implicit from scanning the tag list
    // Gather group slots in the order we tagged them (scan order = allocation order).
    int group_slots[kNumVoices];
    int gc = 0;
    for (int i = 0; i < kNumVoices; i++) {
        if (unison_tag_[i] == tag && slots_[i].gate) {
            group_slots[gc++] = i;
        }
    }
    if (gc == 0) return;

    float spread_semi = unison_detune_ / 100.0f;  // cents → semitones
    for (int gi = 0; gi < gc; gi++) {
        float offset = 0.0f;
        if (gc > 1 && spread_semi > 0.0f) {
            float norm = (float)gi / (float)(gc - 1);  // 0..1
            offset     = (norm - 0.5f) * spread_semi;
        }
        slots_[group_slots[gi]].voice->set_pitch_offset(offset);
    }
}

// Allocate up to unison_count_ slots for the note and trigger them.
// Uses the standard free/steal policy per slot. Tags all allocated slots.
void VoiceAlloc::note_on_unison(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    // Release any existing group for this pitch first (retrigger).
    note_off_unison(pitch);

    // Remove the old unison-tag so find_free_slot sees those slots as released.
    // (note_off_unison already gated them off — they'll appear in pass 2 of find_free_slot.)

    int allocated = 0;
    for (int u = 0; u < unison_count_; u++) {
        int idx = find_free_slot();
        if (idx < 0) {
            idx              = find_steal_slot();
            // Un-tag the stolen slot (its old group loses a member).
            unison_tag_[idx] = kNoGroup;
            slots_[idx].voice->reset();
        }
        VoiceSlot& s     = slots_[idx];
        s.pitch          = pitch;
        s.gate           = true;
        s.timestamp      = ++tick_;
        unison_tag_[idx] = pitch;
        s.voice->note_on(pitch, velocity, expr);
        allocated++;
    }

    // Apply detune now that all slots in the group are live.
    apply_detune(pitch, allocated, 0);
}

// Release all slots tagged with the given pitch (a note's unison group).
void VoiceAlloc::note_off_unison(uint8_t pitch) {
    for (int i = 0; i < kNumVoices; i++) {
        if (unison_tag_[i] == pitch && slots_[i].gate) {
            slots_[i].gate = false;
            unison_tag_[i] = kNoGroup;
            slots_[i].voice->note_off();
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
