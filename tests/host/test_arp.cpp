/* tests/host/test_arp.cpp — host tests for engine/arp.h */
#include "engine/arp.h"
#include "runner.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Press a chord (notes_on all in order), returns the arp ready to step.
static void press(Arp& a, const uint8_t* pitches, int n, uint8_t vel = 80) {
    for (int i = 0; i < n; ++i) a.note_on(pitches[i], vel);
}

static void release(Arp& a, const uint8_t* pitches, int n) {
    for (int i = 0; i < n; ++i) a.note_off(pitches[i]);
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

void test_arp_suite() {
    // -----------------------------------------------------------------------
    // 1. kUp: [C4=60, E4=64, G4=67], octaves=1 → 60,64,67,60,64,67,...
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kUp basic + wrap");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(1);
        const uint8_t chord[] = {60, 64, 67};
        press(a, chord, 3);

        // Sorted ascending: 60,64,67
        TEST_ASSERT(a.next().pitch == 60, "kUp step 0");
        TEST_ASSERT(a.next().pitch == 64, "kUp step 1");
        TEST_ASSERT(a.next().pitch == 67, "kUp step 2");
        // Wrap
        TEST_ASSERT(a.next().pitch == 60, "kUp step 3 (wrap)");
        TEST_ASSERT(a.next().pitch == 64, "kUp step 4");

        // valid flag
        TEST_ASSERT(a.next().valid, "kUp valid");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 2. kDown: same chord → 67,64,60,67,...
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kDown basic + wrap");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kDown);
        a.set_octaves(1);
        const uint8_t chord[] = {60, 64, 67};
        press(a, chord, 3);

        TEST_ASSERT(a.next().pitch == 67, "kDown step 0");
        TEST_ASSERT(a.next().pitch == 64, "kDown step 1");
        TEST_ASSERT(a.next().pitch == 60, "kDown step 2");
        // Wrap
        TEST_ASSERT(a.next().pitch == 67, "kDown step 3 (wrap)");
        TEST_ASSERT(a.next().pitch == 64, "kDown step 4");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 3. kUpDown: [60,64,67], period=2L-2=4 → 60,64,67,64,60,64,67,64,...
    //    Verify endpoints not repeated and full period.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kUpDown endpoints not repeated");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUpDown);
        a.set_octaves(1);
        const uint8_t chord[] = {60, 64, 67};
        press(a, chord, 3);

        // L=3, period=4: s=0→60, s=1→64, s=2→67, s=3→64 (=period-3=1 → sorted[1])
        const uint8_t expect[] = {60, 64, 67, 64, 60, 64, 67, 64};
        for (int i = 0; i < 8; ++i) {
            uint8_t got = a.next().pitch;
            TEST_ASSERT(got == expect[i], "kUpDown sequence mismatch");
        }
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 4. kOrder: press G,C,E (67,60,64) → 67,60,64,67,... (as-played)
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kOrder as-played");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kOrder);
        a.set_octaves(1);
        const uint8_t chord[] = {67, 60, 64};
        press(a, chord, 3);

        TEST_ASSERT(a.next().pitch == 67, "kOrder step 0");
        TEST_ASSERT(a.next().pitch == 60, "kOrder step 1");
        TEST_ASSERT(a.next().pitch == 64, "kOrder step 2");
        // Wrap
        TEST_ASSERT(a.next().pitch == 67, "kOrder step 3 (wrap)");
        TEST_ASSERT(a.next().pitch == 60, "kOrder step 4");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 5. Octaves: [60,64], octaves=2, kUp → 60,64,72,76,60,...
    //    Octave is outer dimension: base notes at +0, then +12.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: octaves=2 kUp");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(2);
        const uint8_t chord[] = {60, 64};
        press(a, chord, 2);

        // L=4: sorted=[60,64]; octave=0→60,64; octave=1→72,76
        TEST_ASSERT(a.next().pitch == 60, "oct2 kUp step 0");
        TEST_ASSERT(a.next().pitch == 64, "oct2 kUp step 1");
        TEST_ASSERT(a.next().pitch == 72, "oct2 kUp step 2");
        TEST_ASSERT(a.next().pitch == 76, "oct2 kUp step 3");
        // Wrap
        TEST_ASSERT(a.next().pitch == 60, "oct2 kUp step 4 (wrap)");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 6. Random determinism: same seed+chord → same sequence after clear()
    //    and every pitch is in the valid expanded pitch set.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kRandom determinism");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kRandom);
        a.set_octaves(1);
        const uint8_t chord[] = {60, 64, 67};
        press(a, chord, 3);

        // Capture first 12 outputs
        uint8_t seq1[12];
        for (int i = 0; i < 12; ++i) {
            ArpNote n = a.next();
            TEST_ASSERT(n.valid, "kRandom valid");
            seq1[i] = n.pitch;
        }

        // Replay: clear and re-add same chord (same seed reset)
        a.clear();
        a.set_mode(ArpMode::kRandom);
        press(a, chord, 3);
        for (int i = 0; i < 12; ++i) {
            uint8_t got = a.next().pitch;
            TEST_ASSERT(got == seq1[i], "kRandom determinism mismatch");
        }

        // All pitches must be one of {60, 64, 67}
        a.clear();
        a.set_mode(ArpMode::kRandom);
        press(a, chord, 3);
        for (int i = 0; i < 30; ++i) {
            uint8_t p = a.next().pitch;
            TEST_ASSERT(p == 60 || p == 64 || p == 67, "kRandom pitch out of chord");
        }
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 7. Empty: no held notes → next().valid == false
    // -----------------------------------------------------------------------
    {
        test_begin("arp: empty → valid=false");
        Arp a;
        a.init();
        ArpNote n = a.next();
        TEST_ASSERT(!n.valid, "empty valid should be false");
        TEST_ASSERT(n.pitch == 0, "empty pitch should be 0");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 8. Latch
    // -----------------------------------------------------------------------
    {
        test_begin("arp: latch on — notes retained after release");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(1);
        a.set_latch(true);

        // Press and release C,E,G
        a.note_on(60, 80);
        a.note_on(64, 80);
        a.note_on(67, 80);
        a.note_off(60);
        a.note_off(64);
        a.note_off(67);

        // physical_count_ is now 0, but latch is ON so held set kept
        TEST_ASSERT(a.held_count() == 3, "latch: held count should be 3 after release");
        TEST_ASSERT(a.next().pitch == 60, "latch: step 0 = 60");
        TEST_ASSERT(a.next().pitch == 64, "latch: step 1 = 64");
        TEST_ASSERT(a.next().pitch == 67, "latch: step 2 = 67");
        TEST_ASSERT(a.next().pitch == 60, "latch: step 3 = 60 (wrap)");
        test_pass();
    }

    {
        test_begin("arp: latch on — new note_on clears old chord");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(1);
        a.set_latch(true);

        // First chord: C,E,G — press and release all
        a.note_on(60, 80);
        a.note_on(64, 80);
        a.note_on(67, 80);
        a.note_off(60);
        a.note_off(64);
        a.note_off(67);
        // physical_count==0, held_count==3

        // New note_on: should clear old chord first (first key after full release)
        a.note_on(69, 90);  // A4 = 69
        TEST_ASSERT(a.held_count() == 1, "latch: new chord should have only new note");
        TEST_ASSERT(a.next().pitch == 69, "latch: new chord step 0");
        test_pass();
    }

    {
        test_begin("arp: latch off with no keys — set clears");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(1);
        a.set_latch(true);

        // Press and release to latch
        a.note_on(60, 80);
        a.note_on(64, 80);
        a.note_off(60);
        a.note_off(64);
        TEST_ASSERT(a.held_count() == 2, "latch off test: should have 2 latched notes");

        // Turn latch off with no physical keys down → clear
        a.set_latch(false);
        TEST_ASSERT(a.held_count() == 0, "latch off: held set should clear");
        ArpNote n = a.next();
        TEST_ASSERT(!n.valid, "latch off: next() should be invalid");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 9. Dynamic held-set change: remove a note mid-pattern, no crash,
    //    index stays in range, remaining notes still cycle.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: dynamic note removal mid-pattern");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(1);

        a.note_on(60, 80);
        a.note_on(64, 80);
        a.note_on(67, 80);

        // Advance a couple steps
        a.next();  // 60
        a.next();  // 64

        // Remove E4 mid-pattern
        a.note_off(64);
        TEST_ASSERT(a.held_count() == 2, "dynamic: held_count after note_off");

        // Next calls should cycle over {60,67} without crash
        for (int i = 0; i < 6; ++i) {
            ArpNote n = a.next();
            TEST_ASSERT(n.valid, "dynamic: note should be valid");
            TEST_ASSERT(n.pitch == 60 || n.pitch == 67, "dynamic: pitch should be 60 or 67");
        }
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 10. kUpDown, L=1 (single note): always returns the same note, no crash.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: kUpDown L=1 single note");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUpDown);
        a.set_octaves(1);
        a.note_on(60, 80);

        for (int i = 0; i < 4; ++i) {
            ArpNote n = a.next();
            TEST_ASSERT(n.valid, "kUpDown L=1 valid");
            TEST_ASSERT(n.pitch == 60, "kUpDown L=1 pitch");
        }
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 11. Velocity is preserved per note
    // -----------------------------------------------------------------------
    {
        test_begin("arp: velocity preserved");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kOrder);
        a.set_octaves(1);
        a.note_on(60, 100);
        a.note_on(64, 50);
        a.note_on(67, 75);

        // kOrder: 67,60,64 — no, notes are in insertion order: 60,64,67
        ArpNote n0 = a.next();
        ArpNote n1 = a.next();
        ArpNote n2 = a.next();
        TEST_ASSERT(n0.pitch == 60 && n0.velocity == 100, "velocity 60");
        TEST_ASSERT(n1.pitch == 64 && n1.velocity == 50, "velocity 64");
        TEST_ASSERT(n2.pitch == 67 && n2.velocity == 75, "velocity 67");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 12. Pitch clamping: octave stacking that would exceed 127 clamps.
    // -----------------------------------------------------------------------
    {
        test_begin("arp: pitch clamped at 127");
        Arp a;
        a.init();
        a.set_mode(ArpMode::kUp);
        a.set_octaves(4);
        a.note_on(120, 80);  // 120 + 36 (3 octaves) = 156 → clamp to 127

        // Step 0: oct=0, pitch=120
        TEST_ASSERT(a.next().pitch == 120, "clamp oct0");
        // Step 1: oct=1, pitch=132 → 127
        TEST_ASSERT(a.next().pitch == 127, "clamp oct1");
        // Step 2: oct=2, pitch=144 → 127
        TEST_ASSERT(a.next().pitch == 127, "clamp oct2");
        // Step 3: oct=3, pitch=156 → 127
        TEST_ASSERT(a.next().pitch == 127, "clamp oct3");
        test_pass();
    }
}
