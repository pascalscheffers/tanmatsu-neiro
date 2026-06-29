// engine/clock.h — sample-accurate master musical clock (ADR 0010).
//
// Header-only (pure): no ESP-IDF, no I/O, no logging, no globals.
// BPM → ticks at 96 PPQN, derived from the audio sample counter.
// Tap tempo, transport (start/stop/continue), free-running monotonic counter.
//
// Threading: Clock is NOT thread-safe by itself.
// All mutations happen exclusively on the audio thread (via ClockCmd drain
// in synth_render). Read-only queries (bpm, tick_pos, etc.) are read from
// the control thread — frame-stale values are fine for display use.
//
// ClockCmd: tiny trivially-copyable command struct for the SPSC ring.
// Pattern mirrors NoteCmd (engine/command_queue.h).
#pragma once

#include <math.h>
#include <cstdint>
#include "spsc_ring.h"

// ---------------------------------------------------------------------------
// ClockCmd — control thread → audio thread transport/bpm command.
// Must be trivially copyable for SpscRing push/pop.
// ---------------------------------------------------------------------------
struct ClockCmd {
    enum Type : uint8_t {
        kSetBpm   = 0,
        kStart    = 1,
        kStop     = 2,
        kContinue = 3,
        kTap      = 4,
    };
    uint8_t type;
    float   arg;  // BPM for kSetBpm; unused (0) for others
};

// ---------------------------------------------------------------------------
// Clock — sample-accurate musical clock at 96 PPQN (ADR 0010).
// ---------------------------------------------------------------------------
class Clock {
public:
    static constexpr int   kPpqn       = 96;
    static constexpr float kBpmMin     = 20.0f;
    static constexpr float kBpmMax     = 300.0f;
    static constexpr float kDefaultBpm = 120.0f;

    // Initialise with the given sample rate. Stops the transport, zeros all
    // positions, sets BPM to 120. Safe to call on any thread before audio starts.
    void init(float sample_rate) {
        sample_rate_  = sample_rate;
        running_      = false;
        bpm_          = kDefaultBpm;
        tick_pos_     = 0;
        sample_pos_   = 0;
        free_pos_     = 0;
        accum_        = 0.0;
        last_tap_pos_ = 0;
        has_prev_tap_ = false;
        recompute_spt();
    }

    // --- BPM ----------------------------------------------------------------

    // Set BPM, clamped to [kBpmMin, kBpmMax]. Recomputes samples-per-tick.
    // Called from the audio thread via ClockCmd drain.
    void set_bpm(float bpm) {
        if (bpm < kBpmMin) bpm = kBpmMin;
        if (bpm > kBpmMax) bpm = kBpmMax;
        bpm_ = bpm;
        recompute_spt();
    }

    float bpm() const { return bpm_; }

    // --- Transport ----------------------------------------------------------

    // Start: reset tick position, sample position, and accumulator; begin running.
    void start() {
        tick_pos_   = 0;
        sample_pos_ = 0;
        accum_      = 0.0;
        running_    = true;
    }

    // Continue: resume from the current position (no reset).
    void cont() { running_ = true; }

    // Stop: halt transport; positions are preserved.
    void stop() { running_ = false; }

    bool running() const { return running_; }

    // --- Block advance ------------------------------------------------------

    // Call once per audio block (from synth_render).
    // Always advances the free-running monotonic counter free_pos_.
    // If running: advances sample_pos_ and accumulates ticks; returns the number
    // of whole ticks crossed this block.
    // If stopped: returns 0.
    int advance(uint32_t frames) {
        free_pos_ += frames;

        if (!running_) {
            return 0;
        }

        sample_pos_ += frames;
        accum_      += (double)frames;

        int ticks = 0;
        while (accum_ >= samples_per_tick_) {
            accum_ -= samples_per_tick_;
            tick_pos_++;
            ticks++;
        }
        return ticks;
    }

    // --- Queries (control-thread safe; may be frame-stale) ------------------

    // Monotonic tick count since the last start().
    uint64_t tick_pos() const { return tick_pos_; }

    // Monotonic sample count since the last start().
    uint64_t sample_pos() const { return sample_pos_; }

    // Samples per tick (double for precision; used in tests).
    double samples_per_tick() const { return samples_per_tick_; }

    // Free-running monotonic sample counter (never reset; unaffected by transport).
    uint64_t free_pos() const { return free_pos_; }

    // --- Tap tempo ----------------------------------------------------------

    // Record a tap. Must be called from the AUDIO THREAD (reads free_pos_).
    // On the first tap (or after a long gap), records the timestamp and waits.
    // On subsequent taps, computes the interval and sets BPM if musically plausible.
    // A gap > 2 s (at the stored sample rate) restarts the tap sequence.
    void tap() {
        uint64_t now = free_pos_;

        if (has_prev_tap_) {
            uint64_t interval = now - last_tap_pos_;

            // 2 s guard: if gap is too long, restart the sequence.
            double two_sec_samples = 2.0 * (double)sample_rate_;
            if ((double)interval > two_sec_samples) {
                // Too long — treat as first tap.
                last_tap_pos_ = now;
                has_prev_tap_ = true;
                return;
            }

            // Compute BPM from interval (one tap interval = one quarter note).
            double candidate_bpm = 60.0 * (double)sample_rate_ / (double)interval;

            // Plausibility check: must be within [kBpmMin, kBpmMax].
            if (candidate_bpm >= (double)kBpmMin && candidate_bpm <= (double)kBpmMax) {
                set_bpm((float)candidate_bpm);
            }
            // Whether plausible or not, record this tap for the next interval.
        }

        last_tap_pos_ = now;
        has_prev_tap_ = true;
    }

    // Clear the tap sequence (forget the last tap timestamp).
    void reset_tap() {
        has_prev_tap_ = false;
        last_tap_pos_ = 0;
    }

private:
    void recompute_spt() {
        // samples_per_tick = sample_rate * 60 / (bpm * kPpqn)
        samples_per_tick_ = (double)sample_rate_ * 60.0 / ((double)bpm_ * (double)kPpqn);
    }

    float  sample_rate_      = 48000.0f;
    float  bpm_              = kDefaultBpm;
    double samples_per_tick_ = 0.0;
    double accum_            = 0.0;  // fractional-sample tick accumulator

    uint64_t tick_pos_   = 0;  // ticks since last start()
    uint64_t sample_pos_ = 0;  // samples since last start()
    uint64_t free_pos_   = 0;  // monotonic, never reset

    bool running_ = false;

    // Tap tempo state
    uint64_t last_tap_pos_ = 0;
    bool     has_prev_tap_ = false;
};
