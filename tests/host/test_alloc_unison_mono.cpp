/* tests/host/test_alloc_unison_mono.cpp
 *
 * Regression tests for the mono+unison voice-cap fix (ADR / MEMORY 2026-06-30).
 *
 * Root cause: in the unison-mono path, each new note_on released the old group
 * via note_off() (leaving a release tail) and allocated fresh slots.  Rapid
 * retriggering piled tails up to the full 8-voice pool.
 *
 * Fix: reuse the current group's physical slots when the group is intact and its
 * size matches unison_count_.  No release tail, voice count stays ≤ unison_count_.
 *
 * These tests assert on allocator state directly (gate / pitch / unison_tag) —
 * no envelope/DSP behaviour is tested, keeping them fast and deterministic.
 *
 * Tests:
 *  A. Rapid retrigger — mono+legato, U=2: gate count never exceeds 2 after any
 *     number of consecutive note_ons on different pitches.
 *  B. Steal-back cap — hold A then B (2 gated), note_off B → exactly 2 gated,
 *     all tagged A (not 4).
 *  C. Regression — U=1 mono still works (1 gated).
 *  D. Regression — poly path unchanged (existing test_alloc_steal covers 8-voice
 *     pool; this just verifies poly with unison_count=1 after a mono session).
 *  E. Unison-count change — if unison_count changes between notes the new note
 *     ends with exactly the new count gated (fallback path).
 */

#include <stdio.h>
#include <string.h>
#include "engine/juno_model.h"
#include "engine/synth_config.h"
#include "engine/voice_alloc.h"
#include "runner.h"

static const float kSr = 48000.0f;

static int count_gated(const VoiceSlot* slots) {
    int n = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate) n++;
    return n;
}

static int count_gated_for_pitch(const VoiceSlot* slots, uint8_t pitch) {
    int n = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate && slots[i].pitch == pitch) n++;
    return n;
}

/* -------------------------------------------------------------------------
 * A. Rapid retrigger: mono+legato, U=2 — gate count never exceeds 2.
 * ---------------------------------------------------------------------- */
void test_unison_mono_rapid_retrigger() {
    printf("--- VoiceAlloc mono+unison cap ---\n");
    test_begin("mono+legato U=2: rapid retrigger never exceeds 2 gated voices");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kLegato);
    alloc.set_unison_count(2);
    alloc.set_unison_detune(20.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Press and hold A; expect 2 gated.
    alloc.note_on(60, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 2, "U=2 mono: exactly 2 gated after first note_on");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 60) == 2, "U=2 mono: both gated slots tagged with pitch 60");

    // Press B while A still held — this is the retrigger path.
    alloc.note_on(64, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 2, "U=2 mono: still exactly 2 gated after second note while first held");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 64) == 2, "U=2 mono: both gated slots now tagged with pitch 64");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 60) == 0,
                "U=2 mono: no gated slot still tagged with old pitch 60");

    // Smash several more notes in rapid succession.
    uint8_t pitches[] = {67, 69, 72, 60, 64, 67};
    for (int i = 0; i < (int)(sizeof(pitches) / sizeof(pitches[0])); i++) {
        alloc.note_on(pitches[i], 100, expr);
        int g = count_gated(alloc.slots());
        TEST_ASSERT(g == 2, "U=2 mono: gate count must stay exactly 2 after each rapid retrigger");
    }

    test_pass();
}

/* -------------------------------------------------------------------------
 * B. Steal-back cap: hold A then B (2 gated), note_off B → 2 gated tagged A.
 * ---------------------------------------------------------------------- */
void test_unison_mono_steal_back_cap() {
    test_begin("mono+legato U=2: steal-back yields exactly 2 gated (not 4)");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kLegato);
    alloc.set_unison_count(2);
    alloc.set_unison_detune(20.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Hold A (2 gated, tagged A).
    alloc.note_on(60, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 2, "steal-back: 2 gated after note_on A");

    // Hold B while A held (2 gated, tagged B via reuse).
    alloc.note_on(64, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 2, "steal-back: 2 gated after note_on B");

    // Release B — should steal back to A.
    alloc.note_off(64);
    int g = count_gated(alloc.slots());
    TEST_ASSERT(g == 2, "steal-back: exactly 2 gated after note_off B (not 4)");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 60) == 2,
                "steal-back: both gated slots tagged with A after steal-back");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 64) == 0,
                "steal-back: no gated slot still tagged B after steal-back");

    test_pass();
}

/* -------------------------------------------------------------------------
 * C. Regression: U=1 mono still works (1 gated, no pile-up).
 * ---------------------------------------------------------------------- */
void test_unison_mono_u1_regression() {
    test_begin("mono U=1: single voice, no pile-up (regression)");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);
    alloc.set_unison_count(1);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    alloc.note_on(60, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 1, "U=1 mono: exactly 1 gated after note_on");

    alloc.note_on(64, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 1, "U=1 mono: still 1 gated after second note");

    alloc.note_on(67, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 1, "U=1 mono: still 1 gated after third note");

    test_pass();
}

/* -------------------------------------------------------------------------
 * D. Regression: poly path unchanged after mono+unison session.
 * ---------------------------------------------------------------------- */
void test_unison_mono_poly_regression() {
    test_begin("poly path unchanged after mono+unison session (regression)");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    // Run a mono+unison session first.
    alloc.set_play_mode(PlayMode::kLegato);
    alloc.set_unison_count(2);
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);
    alloc.note_on(64, 100, expr);
    alloc.note_off(64);
    alloc.note_off(60);

    // Switch to poly — should behave normally.
    alloc.set_play_mode(PlayMode::kPoly);
    alloc.set_unison_count(1);

    // Four distinct notes: expect 4 gated.
    for (int i = 0; i < 4; i++) {
        alloc.note_on((uint8_t)(60 + i), 100, expr);
    }
    TEST_ASSERT(count_gated(alloc.slots()) == 4, "poly: 4 distinct notes = 4 gated after mono session");

    test_pass();
}

/* -------------------------------------------------------------------------
 * E. Unison-count change: new count is honoured via fallback path.
 * ---------------------------------------------------------------------- */
void test_unison_mono_count_change() {
    test_begin("mono U=2→3: unison count change allocates correct new count");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);
    alloc.set_unison_count(2);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // First note with U=2.
    alloc.note_on(60, 100, expr);
    TEST_ASSERT(count_gated(alloc.slots()) == 2, "U=2: 2 gated for first note");

    // Change unison count to 3, then trigger a new note.
    alloc.set_unison_count(3);
    alloc.note_on(64, 100, expr);

    // Group size changed (old=2, new=3) → fallback path → exactly 3 gated.
    TEST_ASSERT(count_gated(alloc.slots()) == 3,
                "U=2→3: after count change new note ends with exactly 3 gated (fallback)");
    TEST_ASSERT(count_gated_for_pitch(alloc.slots(), 64) == 3, "U=2→3: all 3 gated slots tagged with new pitch");

    test_pass();
}

/* -------------------------------------------------------------------------
 * F. Play-mode change silences all voices (Fix A regression).
 *
 * Root cause (commit 9caa5bf follow-up): set_play_mode() only cleared tracking
 * state (mono_slot_, stack, tags) but never called voice->reset().  Any voice
 * that was gate==true before the mode switch stayed gated and kept rendering,
 * now unreachable by note_off — the "stuck tone".
 *
 * Fix: set_play_mode() calls reset_all() on a real change.
 * Assertion: after switching from mono to poly, every slot has gate==false and
 * is_active()==false.  Also checks mono_slot_ reset via the white-box accessor.
 * ---------------------------------------------------------------------- */
void test_play_mode_change_silences_voices() {
    test_begin("set_play_mode: all voices silenced on real mode change (no orphans)");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    // Start in mono+legato with U=2 so multiple gated slots are live.
    alloc.set_play_mode(PlayMode::kMono);
    alloc.set_unison_count(2);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);  // gate 2 slots

    // Confirm precondition: ≥1 slot is gated.
    int before = count_gated(alloc.slots());
    TEST_ASSERT(before >= 1, "play_mode_change: precondition — at least 1 slot gated before switch");

    // Switch to poly — this is the mode change that previously left orphans.
    alloc.set_play_mode(PlayMode::kPoly);

    // Post-condition A: every slot must have gate == false.
    for (int i = 0; i < kNumVoices; i++) {
        TEST_ASSERT(!alloc.slots()[i].gate, "play_mode_change: slot gate must be false after mode switch");
    }

    // Post-condition B: every voice must be inactive (envelope done / reset).
    for (int i = 0; i < kNumVoices; i++) {
        TEST_ASSERT(!alloc.slots()[i].voice->is_active(), "play_mode_change: voice must be inactive after mode switch");
    }

    // Post-condition C: total gated count is zero.
    TEST_ASSERT(count_gated(alloc.slots()) == 0, "play_mode_change: 0 gated slots after mode switch");

    // Post-condition D: mono_slot_ reset (glide_offset via white-box accessor).
    TEST_ASSERT(alloc.glide_offset() == 0.0f, "play_mode_change: glide_offset reset after mode switch");

    test_pass();
}

/* -------------------------------------------------------------------------
 * G. Buried-note release must not leak voices (the mono+unison stuck-note bug).
 *
 * Scenario 1 (the actual leak):
 *   on 60 → slots {0,1} tagged 60, stack [60]
 *   on 64 → reuse slots {0,1} as 64, stack [60,64]
 *   off 60 (buried — was reused) → should only pop stack; sounding group (64) untouched
 *   off 64 (sounding) → final note off; all voices must be released
 *
 * Before the fix, off 60 triggered steal-back (prev_pitch=64, cur_tag=64 — same
 * group!), gated the 64 group off, retriggered it without restoring bookkeeping,
 * so off 64 found nothing gated and voices leaked forever.
 *
 * Scenario 2 (sanity — release in played order):
 *   on 60, on 64, off 64 (sounding), off 60 (now sounding) → also 0 gated, 0 active.
 * ---------------------------------------------------------------------- */
void test_unison_mono_release_all_no_leak() {
    test_begin("mono+legato U=2: buried-note release must not leak voices");

    // --- Scenario 1: release buried note first ---
    {
        JunoModel  model;
        VoiceAlloc alloc;
        model.init(kSr);
        alloc.init(&model);
        alloc.set_play_mode(PlayMode::kLegato);
        alloc.set_unison_count(2);

        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

        alloc.note_on(60, 100, expr);  // stack [60], slots tagged 60
        alloc.note_on(64, 100, expr);  // stack [60,64], slots reused → tagged 64
        alloc.note_off(60);            // buried note: only pops stack; voices unchanged
        alloc.note_off(64);            // last note: all voices must be released

        int gated = count_gated(alloc.slots());
        TEST_ASSERT(gated == 0, "buried-note scenario1: 0 gated after full release");
        for (int i = 0; i < kNumVoices; i++) {
            TEST_ASSERT(!alloc.slots()[i].voice->is_active(),
                        "buried-note scenario1: every voice inactive after full release");
        }
    }

    // --- Scenario 2: release in played order (sanity check) ---
    {
        JunoModel  model;
        VoiceAlloc alloc;
        model.init(kSr);
        alloc.init(&model);
        alloc.set_play_mode(PlayMode::kLegato);
        alloc.set_unison_count(2);

        NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

        alloc.note_on(60, 100, expr);
        alloc.note_on(64, 100, expr);
        alloc.note_off(64);  // sounding note off → steal-back to 60
        alloc.note_off(60);  // now sounding note off → all released

        int gated = count_gated(alloc.slots());
        TEST_ASSERT(gated == 0, "buried-note scenario2: 0 gated after full release in order");
        for (int i = 0; i < kNumVoices; i++) {
            TEST_ASSERT(!alloc.slots()[i].voice->is_active(),
                        "buried-note scenario2: every voice inactive after full release in order");
        }
    }

    test_pass();
}

void test_alloc_unison_mono_suite() {
    test_unison_mono_rapid_retrigger();
    test_unison_mono_steal_back_cap();
    test_unison_mono_u1_regression();
    test_unison_mono_poly_regression();
    test_unison_mono_count_change();
    test_play_mode_change_silences_voices();
    test_unison_mono_release_all_no_leak();
}
