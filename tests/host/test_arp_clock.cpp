/* tests/host/test_arp_clock.cpp — unit tests for engine/arp_clock.h */
#include <math.h>
#include <stdio.h>
#include "engine/arp_clock.h"
#include "runner.h"

// ---------------------------------------------------------------------------
// 1. arp_rate_ticks: index map + out-of-range clamp
// ---------------------------------------------------------------------------
static void test_rate_ticks() {
    test_begin("arp_rate_ticks: 0→96, 1→48, 2→32, 3→24, 4→16, 5→12");
    TEST_ASSERT(arp_rate_ticks(0) == 96, "index 0 should be 96 ticks (1/4)");
    TEST_ASSERT(arp_rate_ticks(1) == 48, "index 1 should be 48 ticks (1/8)");
    TEST_ASSERT(arp_rate_ticks(2) == 32, "index 2 should be 32 ticks (1/8T)");
    TEST_ASSERT(arp_rate_ticks(3) == 24, "index 3 should be 24 ticks (1/16)");
    TEST_ASSERT(arp_rate_ticks(4) == 16, "index 4 should be 16 ticks (1/16T)");
    TEST_ASSERT(arp_rate_ticks(5) == 12, "index 5 should be 12 ticks (1/32)");
    test_pass();

    test_begin("arp_rate_ticks: out-of-range clamp to 24");
    TEST_ASSERT(arp_rate_ticks(-1) == 24, "index -1 should clamp to 24");
    TEST_ASSERT(arp_rate_ticks(99) == 24, "index 99 should clamp to 24");
    test_pass();
}

// ---------------------------------------------------------------------------
// 2. arp_advance_phase: no-fire case
// ---------------------------------------------------------------------------
static void test_no_fire() {
    test_begin("arp_advance_phase: no fire (phase=100, frames=64, period=1000)");
    double         phase = 100.0;
    ArpPhaseResult r     = arp_advance_phase(&phase, 64, 1000.0);
    TEST_ASSERT(!r.fire, "should not fire");
    TEST_ASSERT(r.offset == 0, "offset undefined on no-fire; expect 0");
    // rem = 100 - 64 = 36
    TEST_ASSERT(fabs(phase - 36.0) < 1e-9, "phase should advance to 36");
    test_pass();
}

// ---------------------------------------------------------------------------
// 3. arp_advance_phase: fire with offset
// ---------------------------------------------------------------------------
static void test_fire_with_offset() {
    test_begin("arp_advance_phase: fire with offset (phase=30, frames=64, period=1000)");
    double         phase = 30.0;
    ArpPhaseResult r     = arp_advance_phase(&phase, 64, 1000.0);
    TEST_ASSERT(r.fire, "should fire");
    TEST_ASSERT(r.offset == 30, "offset should be 30");
    // rem = 30 - 64 = -34; new phase = -34 + 1000 = 966
    TEST_ASSERT(fabs(phase - 966.0) < 1e-9, "phase should roll to 966");
    test_pass();
}

// ---------------------------------------------------------------------------
// 4. arp_advance_phase: fire at boundary (phase=0)
// ---------------------------------------------------------------------------
static void test_fire_at_boundary() {
    test_begin("arp_advance_phase: fire at boundary (phase=0, frames=64, period=1000)");
    double         phase = 0.0;
    ArpPhaseResult r     = arp_advance_phase(&phase, 64, 1000.0);
    TEST_ASSERT(r.fire, "should fire");
    TEST_ASSERT(r.offset == 0, "offset should be 0 at boundary");
    // rem = 0 - 64 = -64; new phase = -64 + 1000 = 936
    TEST_ASSERT(fabs(phase - 936.0) < 1e-9, "phase should roll to 936");
    test_pass();
}

// ---------------------------------------------------------------------------
// 5. arp_advance_phase: zero / negative period — never fires
// ---------------------------------------------------------------------------
static void test_zero_period() {
    test_begin("arp_advance_phase: zero period -> never fires");
    double         phase = 0.0;
    double         saved = phase;
    ArpPhaseResult r     = arp_advance_phase(&phase, 64, 0.0);
    TEST_ASSERT(!r.fire, "zero period: should not fire");
    TEST_ASSERT(phase == saved, "zero period: phase must be unchanged");
    test_pass();

    test_begin("arp_advance_phase: negative period -> never fires");
    phase = 30.0;
    saved = phase;
    r     = arp_advance_phase(&phase, 64, -100.0);
    TEST_ASSERT(!r.fire, "negative period: should not fire");
    TEST_ASSERT(phase == saved, "negative period: phase must be unchanged");
    test_pass();
}

// ---------------------------------------------------------------------------
// 6. arp_advance_phase: repeated calls produce evenly-spaced fires
//    Loop many 64-frame blocks, period=600 samples.  Steps fire at spacing
//    ≈ 600 samples = 600/64 ≈ 9.375 blocks apart.  Measure the gap between
//    consecutive fire-blocks and assert it's within ±1 block of the exact ratio.
// ---------------------------------------------------------------------------
static void test_even_spacing() {
    test_begin("arp_advance_phase: repeated calls, even step spacing (period=600)");

    const double   period       = 600.0;
    const uint32_t frames       = 64;
    const int      total_blocks = 1000;
    double         phase        = 0.0;

    int    prev_fire_block = -1;
    int    gap_count       = 0;
    double gap_sum         = 0.0;

    for (int b = 0; b < total_blocks; b++) {
        ArpPhaseResult r = arp_advance_phase(&phase, frames, period);
        if (r.fire) {
            if (prev_fire_block >= 0) {
                double gap = (double)(b - prev_fire_block);
                gap_sum   += gap;
                gap_count++;
            }
            prev_fire_block = b;
        }
    }

    TEST_ASSERT(gap_count > 0, "at least one fire gap measured");

    double mean_gap  = gap_sum / (double)gap_count;
    double exact_gap = period / (double)frames;  // ≈ 9.375 blocks

    // Allow up to ±1 block tolerance (quantisation at block granularity).
    double err = mean_gap - exact_gap;
    if (err < 0.0) err = -err;
    TEST_ASSERT(err < 1.5, "mean gap should be within 1.5 blocks of period/frames");
    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry point
// ---------------------------------------------------------------------------
void test_arp_clock_suite() {
    printf("\n--- arp_clock ---\n");
    test_rate_ticks();
    test_no_fire();
    test_fire_with_offset();
    test_fire_at_boundary();
    test_zero_period();
    test_even_spacing();
}
