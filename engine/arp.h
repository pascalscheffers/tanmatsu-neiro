// engine/arp.h — pure arpeggiator pattern core (ADR 0019).
//
// Header-only (pure): no ESP-IDF, no I/O, no logging, no globals, no alloc.
// Implements held-note tracking + arp pattern generation (next()).
// Timing, params, synth wiring live in 4b-ii/iii (out of scope here).
//
// Threading: Arp is NOT thread-safe by itself.
// All mutations happen exclusively on the audio thread (4b-iii wiring).
// Read-only queries (held_count, has_notes) are safe for display use.
#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr int kMaxHeld    = 16;  // max simultaneously-held notes
static constexpr int kMaxOctaves = 4;   // max octave spread

// ---------------------------------------------------------------------------
// ArpMode — pattern traversal style
// ---------------------------------------------------------------------------
enum class ArpMode : uint8_t {
    kUp     = 0,  // ascending by pitch, traverse expanded list forward
    kDown   = 1,  // ascending sort, traverse expanded list backward
    kUpDown = 2,  // ping-pong — endpoints not repeated, period 2L-2
    kOrder  = 3,  // as-played insertion order, traverse forward
    kRandom = 4,  // deterministic LCG random index each step
};

// ---------------------------------------------------------------------------
// ArpNote — output of next()
// ---------------------------------------------------------------------------
struct ArpNote {
    uint8_t pitch;     // MIDI pitch [0,127]
    uint8_t velocity;  // MIDI velocity [0,127]
    bool    valid;     // false when held set is empty
};

// ---------------------------------------------------------------------------
// Arp — fixed-size arpeggiator pattern core
// ---------------------------------------------------------------------------
class Arp {
public:
    // Initialise with sensible defaults. Safe to call before audio starts.
    void init() {
        mode_    = ArpMode::kUp;
        octaves_ = 1;
        latch_   = false;
        clear();
    }

    // Empty the held set, reset step to start, reset LCG seed.
    // mode/octaves/latch keep their last-set values.
    void clear() {
        held_count_     = 0;
        physical_count_ = 0;
        step_           = 0;
        lcg_state_      = 0x12345u;
        for (int i = 0; i < kMaxHeld; ++i) {
            held_[i].pitch    = 0;
            held_[i].velocity = 0;
        }
    }

    // --- Config (no timing here) -------------------------------------------

    void set_mode(ArpMode m) { mode_ = m; }

    // Clamp octaves to [1, kMaxOctaves].
    void set_octaves(int n) {
        if (n < 1) n = 1;
        if (n > kMaxOctaves) n = kMaxOctaves;
        octaves_ = n;
    }

    // Latch on/off.
    // Turning latch OFF with no physical keys down: clear the held set so
    // notes stop. Turning it ON with keys held keeps them.
    void set_latch(bool on) {
        bool was_on = latch_;
        latch_ = on;
        // latch off → drop latched notes if no physical key is currently down
        if (was_on && !on && physical_count_ == 0) {
            held_count_ = 0;
            step_       = 0;
        }
    }

    // --- Held-note input (called by 4b-iii wiring on note on/off) ----------

    // Track a physical key-down.
    // Latch semantics: if latch is ON and this is the FIRST physical key down
    // after all keys were released (physical_count_ was 0) and the held set is
    // non-empty, clear the held set first (start a fresh chord).
    void note_on(uint8_t pitch, uint8_t velocity) {
        // Latch: first key after full release → start fresh chord
        if (latch_ && physical_count_ == 0 && held_count_ > 0) {
            held_count_ = 0;
            step_       = 0;
        }

        // Insert if not already present and set isn't full
        if (!find_held(pitch) && held_count_ < kMaxHeld) {
            held_[held_count_].pitch    = pitch;
            held_[held_count_].velocity = velocity;
            held_count_++;
            // held set just became non-empty → reset to start of pattern
            if (held_count_ == 1) {
                step_ = 0;
            }
        }

        physical_count_++;
    }

    // Track a physical key-up.
    // Latch OFF: remove note from the held set (compact insertion order).
    // Latch ON:  keep the note latched.
    void note_off(uint8_t pitch) {
        if (physical_count_ > 0) physical_count_--;

        if (!latch_) {
            remove_held(pitch);
        }
        // latch is on: released note stays in the held set
    }

    // --- Pattern generation ------------------------------------------------

    // Advance one step and return the next ArpNote.
    // Returns {0,0,false} when the held set is empty.
    //
    // Expanded note list, length L = held_count * octaves:
    //   flat index i → octave_off = i / held_count, base_idx = i % held_count
    // Octave is the outer dimension: all base notes at +0, then +12, etc.
    //
    // Mode traversal:
    //   kUp      — base sorted ascending;  walk 0,1,...,L-1, wrap.
    //   kDown    — base sorted ascending;  walk L-1,...,0, wrap.
    //   kUpDown  — base sorted ascending;  walk 0,...,L-1,L-2,...,1 (period
    //              2L-2 for L>1; period 1 for L==1 — endpoint not duplicated).
    //   kOrder   — base in insertion order; walk 0,1,...,L-1, wrap.
    //   kRandom  — uniform LCG random index in [0,L); same seed+chord ⇒ same sequence.
    //
    // Out-of-range pitch from octave stacking is clamped to [0,127].
    ArpNote next() {
        if (held_count_ == 0) {
            return {0, 0, false};
        }

        const int L = held_count_ * octaves_;  // expanded list length

        // --- Compute the flat expanded-list index for this step ---
        int flat_idx = 0;

        switch (mode_) {
            case ArpMode::kUp:
            case ArpMode::kOrder: {
                // Forward linear walk
                flat_idx = step_;
                step_    = (step_ + 1) % L;
                break;
            }
            case ArpMode::kDown: {
                // Backward walk: step_ counts 0,1,...,L-1 but maps to L-1,L-2,...,0
                flat_idx = L - 1 - step_;
                step_    = (step_ + 1) % L;
                break;
            }
            case ArpMode::kUpDown: {
                if (L == 1) {
                    // Single note: just return it; step stays 0
                    flat_idx = 0;
                } else {
                    // Period = 2L-2; step_ in [0, period)
                    // s in [0, L-1] → flat_idx = s (ascending)
                    // s in [L, 2L-3] → flat_idx = 2L-2-s (descending, skipping endpoints)
                    // e.g. L=3: period=4, s=0→0, s=1→1, s=2→2, s=3→1
                    const int period = 2 * L - 2;
                    int       s      = step_;
                    flat_idx         = (s < L) ? s : (period - s);
                    step_            = (step_ + 1) % period;
                }
                break;
            }
            case ArpMode::kRandom: {
                // Deterministic LCG (Knuth MMIX 64-bit coefficients)
                // Use upper 32 bits for better distribution
                lcg_state_ = lcg_state_ * 6364136223846793005ULL + 1442695040888963407ULL;
                flat_idx   = (int)((uint32_t)(lcg_state_ >> 33) % (uint32_t)L);
                // step_ not used for random, but keep at 0
                break;
            }
        }

        // --- Decompose flat index into (base_idx, octave_offset) ---
        const int base_idx   = flat_idx % held_count_;
        const int octave_off = flat_idx / held_count_;

        // --- Look up the base note (sort if needed) ---
        int base_pitch    = 0;
        int base_velocity = 64;

        if (mode_ == ArpMode::kUp || mode_ == ArpMode::kDown || mode_ == ArpMode::kUpDown) {
            // Build a sorted (ascending by pitch) view of the held notes.
            // Zero-init to silence -Werror=maybe-uninitialized on device GCC.
            uint8_t sorted_pitches[kMaxHeld] = {};
            build_sorted(sorted_pitches);
            base_pitch    = sorted_pitches[base_idx];
            base_velocity = velocity_for_pitch(sorted_pitches[base_idx]);
        } else {
            // kOrder or kRandom: insertion order
            base_pitch    = held_[base_idx].pitch;
            base_velocity = held_[base_idx].velocity;
        }

        // Apply octave offset; clamp to [0,127]
        // (out-of-range octave stacking simply clamps — e.g. G9 + octave → 127)
        int final_pitch = base_pitch + 12 * octave_off;
        if (final_pitch > 127) final_pitch = 127;
        if (final_pitch < 0)   final_pitch = 0;

        return {(uint8_t)final_pitch, (uint8_t)base_velocity, true};
    }

    // --- Queries -----------------------------------------------------------

    int  held_count()     const { return held_count_; }
    bool has_notes()      const { return held_count_ > 0; }
    int  physical_count() const { return physical_count_; }

private:
    // --- Held-note storage (insertion order) -------------------------------
    struct HeldNote {
        uint8_t pitch;
        uint8_t velocity;
    };

    HeldNote held_[kMaxHeld];
    int      held_count_     = 0;
    int      physical_count_ = 0;

    // --- Pattern state -----------------------------------------------------
    // step_: position in the current traversal sequence.
    //   kUp/kDown/kOrder: position in [0, L)
    //   kUpDown:          position in [0, 2L-2)
    //   kRandom:          unused (0)
    int      step_      = 0;
    uint64_t lcg_state_ = 0x12345u;

    // --- Config -----------------------------------------------------------
    ArpMode mode_    = ArpMode::kUp;
    int     octaves_ = 1;
    bool    latch_   = false;

    // --- Private helpers ---------------------------------------------------

    // Returns true if pitch is already in the held set.
    bool find_held(uint8_t pitch) const {
        for (int i = 0; i < held_count_; ++i) {
            if (held_[i].pitch == pitch) return true;
        }
        return false;
    }

    // Remove a pitch from the held set, compacting insertion order.
    // Keeps step_ in range after shrinking.
    void remove_held(uint8_t pitch) {
        for (int i = 0; i < held_count_; ++i) {
            if (held_[i].pitch == pitch) {
                for (int j = i; j < held_count_ - 1; ++j) {
                    held_[j] = held_[j + 1];
                }
                held_count_--;
                // Keep step_ in range after list shrank
                if (held_count_ == 0) {
                    step_ = 0;
                } else {
                    // For kUpDown the period is 2*L-2; for others it's L
                    int L = held_count_ * octaves_;
                    if (L > 0) {
                        int period = (mode_ == ArpMode::kUpDown && L > 1) ? (2 * L - 2) : L;
                        if (step_ >= period) step_ = step_ % period;
                    }
                }
                return;
            }
        }
    }

    // Build a sorted (ascending pitch) array of the held pitches.
    void build_sorted(uint8_t sorted_pitches[]) const {
        for (int i = 0; i < held_count_; ++i) {
            sorted_pitches[i] = held_[i].pitch;
        }
        // Insertion sort — kMaxHeld=16, negligible overhead
        for (int i = 1; i < held_count_; ++i) {
            uint8_t key = sorted_pitches[i];
            int     j   = i - 1;
            while (j >= 0 && sorted_pitches[j] > key) {
                sorted_pitches[j + 1] = sorted_pitches[j];
                j--;
            }
            sorted_pitches[j + 1] = key;
        }
    }

    // Return velocity for a given pitch (scans insertion order; fallback 64).
    int velocity_for_pitch(uint8_t pitch) const {
        for (int i = 0; i < held_count_; ++i) {
            if (held_[i].pitch == pitch) return held_[i].velocity;
        }
        return 64;
    }
};
