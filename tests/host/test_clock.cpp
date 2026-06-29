/* tests/host/test_clock.cpp — unit tests for the master musical Clock */
#include <math.h>
#include <stdio.h>
#include "engine/clock.h"
#include "runner.h"

void test_clock_suite() {
    printf("--- Clock ---\n");

    // 1. samples-per-tick math at 120 BPM / 48 kHz
    // Expected: 48000 * 60 / (120 * 96) = 250.0 exactly.
    {
        test_begin("samples_per_tick: 120 BPM @ 48k == 250.0");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        double spt = c.samples_per_tick();
        TEST_ASSERT(fabs(spt - 250.0) < 1e-9, "samples_per_tick must be exactly 250.0");
        test_pass();
    }

    // 2a. Advancing exactly one tick's worth of samples returns 1 tick.
    {
        test_begin("advance 250 frames -> 1 tick at 120/48k");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.start();
        int ticks = c.advance(250);
        TEST_ASSERT(ticks == 1, "250 frames at 120/48k must cross exactly 1 tick");
        TEST_ASSERT(c.tick_pos() == 1, "tick_pos must be 1 after one tick");
        test_pass();
    }

    // 2b. 64-frame blocks: tick_pos after N total samples == floor(N/250).
    {
        test_begin("64-frame blocks accumulate ticks correctly");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.start();

        uint64_t total_frames = 0;
        // Run 200 blocks of 64 samples = 12800 samples.
        for (int i = 0; i < 200; i++) {
            c.advance(64);
            total_frames += 64;
        }
        uint64_t expected_ticks = total_frames / 250;
        TEST_ASSERT(c.tick_pos() == expected_ticks, "tick_pos must equal floor(total_frames / 250)");
        test_pass();
    }

    // 3a. While stopped, advance returns 0 and tick_pos stays 0, but free_pos increases.
    {
        test_begin("stopped: advance returns 0, tick_pos stays 0, free_pos grows");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        // do NOT call start() — transport is stopped after init()
        int ticks = c.advance(500);
        TEST_ASSERT(ticks == 0, "stopped clock must return 0 ticks");
        TEST_ASSERT(c.tick_pos() == 0, "tick_pos must stay 0 while stopped");
        TEST_ASSERT(c.free_pos() == 500, "free_pos must advance even while stopped");
        test_pass();
    }

    // 3b. start() then advancing produces ticks.
    {
        test_begin("start() then advance produces ticks");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.advance(250);  // stopped — no ticks
        c.start();
        int ticks = c.advance(250);
        TEST_ASSERT(ticks == 1, "after start(), 250 frames must produce 1 tick");
        test_pass();
    }

    // 3c. stop() freezes tick_pos.
    {
        test_begin("stop() freezes tick_pos");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.start();
        c.advance(250);  // 1 tick
        c.stop();
        uint64_t frozen = c.tick_pos();
        c.advance(250);  // should produce no more ticks
        TEST_ASSERT(c.tick_pos() == frozen, "tick_pos must not change after stop()");
        test_pass();
    }

    // 3d. cont() resumes without resetting position.
    {
        test_begin("cont() resumes without resetting position");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.start();
        c.advance(250);  // tick_pos = 1
        c.stop();
        uint64_t pos_before_cont = c.tick_pos();
        c.cont();
        c.advance(250);  // should add 1 more tick
        TEST_ASSERT(c.tick_pos() == pos_before_cont + 1, "cont() must resume without resetting tick_pos");
        test_pass();
    }

    // 3e. start() resets tick_pos to 0.
    {
        test_begin("start() resets position to 0");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(120.0f);
        c.start();
        c.advance(500);  // tick_pos > 0
        TEST_ASSERT(c.tick_pos() > 0, "pre-condition: tick_pos > 0 after advance");
        c.start();  // restart
        TEST_ASSERT(c.tick_pos() == 0, "start() must reset tick_pos to 0");
        TEST_ASSERT(c.sample_pos() == 0, "start() must reset sample_pos to 0");
        test_pass();
    }

    // 4. BPM clamping.
    {
        test_begin("BPM clamp: set_bpm(5) -> 20, set_bpm(1000) -> 300");
        Clock c;
        c.init(48000.0f);
        c.set_bpm(5.0f);
        TEST_ASSERT(c.bpm() == 20.0f, "BPM must clamp to 20 at the low end");
        c.set_bpm(1000.0f);
        TEST_ASSERT(c.bpm() == 300.0f, "BPM must clamp to 300 at the high end");
        test_pass();
    }

    // 5a. Tap tempo: two taps at a known interval set the correct BPM.
    //     At 48 kHz and 120 BPM, one quarter note = 24000 samples.
    {
        test_begin("tap tempo: two taps at 24000 samples -> ~120 BPM");
        Clock c;
        c.init(48000.0f);
        c.start();

        // First tap: free_pos = 0 (after init, before any advance).
        c.tap();

        // Advance exactly 24000 samples (one quarter note at 120 BPM).
        c.advance(24000);

        // Second tap: free_pos = 24000.
        c.tap();

        // BPM must be close to 120.
        float bpm = c.bpm();
        TEST_ASSERT(fabs((double)bpm - 120.0) < 0.01, "tap BPM must be ~120");
        test_pass();
    }

    // 5b. Implausibly long gap (> 2 s) restarts the tap sequence (no BPM jump).
    {
        test_begin("tap tempo: gap > 2 s restarts sequence, no BPM jump");
        Clock c;
        c.init(48000.0f);
        c.start();
        c.set_bpm(120.0f);

        // First tap.
        c.tap();

        // Advance > 2 s = 96001 samples (just over the 2-s guard at 48k).
        c.advance(96001);

        // Second tap — gap is too long; should restart sequence, not set BPM.
        c.tap();

        // BPM must still be 120 (the gap was rejected as first-tap restart).
        TEST_ASSERT(c.bpm() == 120.0f, "BPM must not change after an implausible gap");

        // A subsequent tap at 24000 samples later should now compute 120 BPM.
        c.advance(24000);
        c.tap();
        TEST_ASSERT(fabs((double)c.bpm() - 120.0) < 0.01, "after restart, correct interval must set BPM");
        test_pass();
    }
}
