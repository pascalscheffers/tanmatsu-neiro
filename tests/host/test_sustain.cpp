/* tests/host/test_sustain.cpp — unit tests for control/sustain (deferred note-off) */
#include "control/sustain.h"
#include "runner.h"

/* ---------- Callback capture ------------------------------------------------ */

static uint8_t s_released[128];
static int     s_released_count;

static void capture_release(uint8_t pitch) {
    if (s_released_count < 128) {
        s_released[s_released_count++] = pitch;
    }
}

static void reset_capture(void) {
    s_released_count = 0;
}

static int capture_contains(uint8_t pitch) {
    for (int i = 0; i < s_released_count; i++) {
        if (s_released[i] == pitch) return 1;
    }
    return 0;
}

/* ---------- Suite ----------------------------------------------------------- */

void test_sustain_suite() {
    SustainPedal s;

    // -----------------------------------------------------------------------
    // 1. Pedal up: note_off returns false (immediate), nothing pending.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: pedal up — note_off is immediate");
        sustain_init(&s);
        sustain_note_on(&s, 60);
        bool deferred = sustain_note_off(&s, 60);
        TEST_ASSERT(!deferred, "pedal up: note_off must return false");
        reset_capture();
        sustain_set_pedal(&s, false, capture_release);
        TEST_ASSERT(s_released_count == 0, "pedal up: no callbacks on up→up");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 2. Pedal down: note_off returns true (deferred); callback NOT yet fired.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: pedal down — note_off is deferred");
        sustain_init(&s);
        sustain_set_pedal(&s, true, capture_release);
        sustain_note_on(&s, 60);
        reset_capture();
        bool deferred = sustain_note_off(&s, 60);
        TEST_ASSERT(deferred, "pedal down: note_off must return true");
        TEST_ASSERT(s_released_count == 0, "pedal down: callback must not fire yet");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 3. Pedal down→up flushes all pending pitches exactly once each.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: pedal up flushes all pending");
        sustain_init(&s);
        sustain_set_pedal(&s, true, capture_release);
        // Press and release three pitches while pedal is down
        sustain_note_on(&s, 60);
        sustain_note_on(&s, 64);
        sustain_note_on(&s, 67);
        sustain_note_off(&s, 60);
        sustain_note_off(&s, 64);
        sustain_note_off(&s, 67);
        reset_capture();
        // Lift pedal — should flush all three
        sustain_set_pedal(&s, false, capture_release);
        TEST_ASSERT(s_released_count == 3, "flush: should release 3 pitches");
        TEST_ASSERT(capture_contains(60), "flush: pitch 60 released");
        TEST_ASSERT(capture_contains(64), "flush: pitch 64 released");
        TEST_ASSERT(capture_contains(67), "flush: pitch 67 released");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 4. Re-press before flush cancels the deferred release for that pitch.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: re-press before flush cancels deferred release");
        sustain_init(&s);
        sustain_set_pedal(&s, true, capture_release);
        // Defer pitch 60
        sustain_note_on(&s, 60);
        sustain_note_off(&s, 60);
        // Re-press pitch 60 while pedal is still down → cancel the pending release
        sustain_note_on(&s, 60);
        reset_capture();
        // Lift pedal
        sustain_set_pedal(&s, false, capture_release);
        TEST_ASSERT(!capture_contains(60), "re-press: pitch 60 must NOT be released on pedal up");
        TEST_ASSERT(s_released_count == 0, "re-press: no callbacks");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 5. Pedal down with nothing held → pedal up → no callbacks.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: pedal cycle with no deferred notes — no callbacks");
        sustain_init(&s);
        sustain_set_pedal(&s, true, capture_release);
        reset_capture();
        sustain_set_pedal(&s, false, capture_release);
        TEST_ASSERT(s_released_count == 0, "empty pedal cycle: no callbacks");
        test_pass();
    }

    // -----------------------------------------------------------------------
    // 6. sustain_clear wipes pending: pedal up after clear fires no callbacks.
    // -----------------------------------------------------------------------
    {
        test_begin("sustain: clear wipes pending — no callbacks on subsequent pedal up");
        sustain_init(&s);
        sustain_set_pedal(&s, true, capture_release);
        sustain_note_on(&s, 60);
        sustain_note_off(&s, 60);
        // Clear before pedal-up
        sustain_clear(&s);
        reset_capture();
        sustain_set_pedal(&s, false, capture_release);
        TEST_ASSERT(s_released_count == 0, "after clear: no callbacks on pedal up");
        test_pass();
    }
}
