// engine/arp_clock.h — arp timing helpers (header-only, pure).
//
// Header-only: no ESP-IDF, no I/O, no logging, no globals, no alloc.
// Converts ARP_RATE stepped-index to ticks at 96 PPQN and advances
// the free-running arp step phase by one audio block.
//
// Threading: helpers are stateless or take all state by pointer.
// The caller (synth_render, audio thread only) owns the phase double.
#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// arp_rate_ticks — ARP_RATE stepped index → ticks-per-step at 96 PPQN.
//
// Index mapping:
//   0 → 1/4  note = 96 ticks
//   1 → 1/8  note = 48 ticks
//   2 → 1/8T note = 32 ticks  (triplet)
//   3 → 1/16 note = 24 ticks
//   4 → 1/16T note = 16 ticks  (triplet)
//   5 → 1/32 note = 12 ticks
//
// Out-of-range (< 0 or > 5) clamps to 1/16 (24 ticks).
// ---------------------------------------------------------------------------
inline int arp_rate_ticks(int rate_index) {
    static const int kTable[6] = {96, 48, 32, 24, 16, 12};
    if (rate_index < 0 || rate_index > 5) {
        return 24;  // default: 1/16 note
    }
    return kTable[rate_index];
}

// ---------------------------------------------------------------------------
// ArpPhaseResult — return value of arp_advance_phase.
// ---------------------------------------------------------------------------
struct ArpPhaseResult {
    bool     fire;    // true if a step fires this block
    uint32_t offset;  // sample offset within the block where the step lands
};

// ---------------------------------------------------------------------------
// arp_advance_phase — advance the arp step phase by `frames` samples.
//
// *phase_samples: samples remaining until the next step at block start.
//   Initialise to 0.0 to fire on the very first block.
//
// Returns {fire=true, offset} when a step crosses zero in this block.
// offset = sample offset within the block: clamp((uint32_t)phase_at_entry, 0, frames-1).
// After firing, *phase is rolled forward by one step_period so it will
// count down to the NEXT step.
//
// Returns {false, 0} when no step fires (or step_period_samples <= 0).
//
// Single-step-per-block assumption: steps are far apart vs the block size
// (e.g. 1/16 @ 120 BPM = ~6000 samples >> 64-frame block), so at most one
// step fires per block. Guard: step_period_samples <= 0 → never fire.
// ---------------------------------------------------------------------------
inline ArpPhaseResult arp_advance_phase(double* phase_samples,
                                         uint32_t frames,
                                         double   step_period_samples) {
    if (step_period_samples <= 0.0) {
        return {false, 0};
    }

    double p0  = *phase_samples;
    double rem = p0 - (double)frames;

    if (rem <= 0.0) {
        // A step crosses zero in this block.
        // offset: where within the block the step falls.
        uint32_t offset = (p0 >= 0.0 && p0 < (double)frames)
                              ? (uint32_t)p0
                              : 0u;
        *phase_samples  = rem + step_period_samples;
        return {true, offset};
    } else {
        *phase_samples = rem;
        return {false, 0};
    }
}
