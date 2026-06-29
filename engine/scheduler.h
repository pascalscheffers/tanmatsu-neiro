// engine/scheduler.h — sample-accurate event scheduler (ADR 0010).
//
// Header-only (pure): no ESP-IDF, no I/O, no logging, no globals.
// Producers (arp, sequencer, MIDI-file player) timestamp note events in
// Clock::sample_pos() units and push them into a per-producer SpscRing.
// The audio thread drains those rings into this Scheduler, then calls
// dispatch_due() once per block to fire events whose time has come.
//
// Dispatch granularity: block-level. The sub-block sample offset is
// computed and passed to the callback, but the engine applies events
// at the block boundary (true sub-block render-splitting is deferred
// per ADR 0010 — "a profile says it's needed").
//
// Threading: Scheduler itself is NOT thread-safe. It is only touched
// from the audio thread (drain ring → schedule; dispatch_due in render).
#pragma once

#include <cstdint>
#include "command_queue.h"  // NoteCmd (type/pitch/velocity)

// ---------------------------------------------------------------------------
// ScheduledEvent — one timestamped note command.
// Must be trivially copyable for SpscRing<ScheduledEvent, Cap>.
// ---------------------------------------------------------------------------
struct ScheduledEvent {
    uint64_t sample_time;  // absolute Clock::sample_pos() value when this fires
    NoteCmd  cmd;          // reuse the note payload (type/pitch/velocity)
};

// ---------------------------------------------------------------------------
// Scheduler<Cap> — fixed-capacity, no-alloc, audio-thread-only holder.
//
// Cap: maximum pending events (power of two not required; 64 is the default).
// ---------------------------------------------------------------------------
template <int Cap = 64>
class Scheduler {
    static_assert(Cap > 0, "Cap must be positive");

public:
    // Drop all pending events.
    void clear() {
        count_ = 0;
    }

    // Store an event. Returns false (and discards) if Cap is already reached.
    bool schedule(uint64_t sample_time, const NoteCmd& cmd) {
        if (count_ >= Cap) {
            return false;
        }
        events_[count_].sample_time = sample_time;
        events_[count_].cmd         = cmd;
        count_++;
        return true;
    }

    // Number of events currently waiting.
    int pending() const { return count_; }

    // Dispatch every event with sample_time < (now + frames), in ascending
    // sample_time order, by calling fn(const NoteCmd& cmd, uint32_t offset).
    //
    // offset: sample offset from the block start where the event fires.
    //   offset = (sample_time > now) ? (uint32_t)(sample_time - now) : 0
    //   Clamped so offset < frames (late events dispatch at offset 0).
    //
    // Dispatched events are removed; undispatched events are kept.
    // Returns the number of events dispatched.
    //
    // Algorithm: repeated "find earliest due, dispatch, remove". O(Cap^2)
    // worst-case — correct and simple; Cap is small (64).
    template <class Fn>
    int dispatch_due(uint64_t now, uint32_t frames, Fn&& fn) {
        int dispatched = 0;
        uint64_t window_end = now + (uint64_t)frames;  // exclusive upper bound

        // Keep looping until no due event remains in the window.
        for (;;) {
            // Find the index of the earliest due event (sample_time < window_end).
            int earliest = -1;
            for (int i = 0; i < count_; i++) {
                if (events_[i].sample_time < window_end) {
                    if (earliest == -1 ||
                        events_[i].sample_time < events_[earliest].sample_time) {
                        earliest = i;
                    }
                }
            }

            if (earliest == -1) {
                break;  // nothing more due in this block
            }

            // Compute block-relative offset; clamp to [0, frames-1].
            uint32_t offset;
            if (events_[earliest].sample_time > now) {
                uint64_t delta = events_[earliest].sample_time - now;
                offset = (delta < (uint64_t)frames) ? (uint32_t)delta : frames - 1u;
            } else {
                // Late event (sample_time <= now): fire at the block start.
                offset = 0;
            }

            fn(events_[earliest].cmd, offset);
            dispatched++;

            // Remove by swapping with the last element (O(1) removal).
            events_[earliest] = events_[count_ - 1];
            count_--;
        }

        return dispatched;
    }

private:
    ScheduledEvent events_[Cap];
    int            count_ = 0;
};
