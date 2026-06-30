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

void test_alloc_unison_mono_suite() {
    test_unison_mono_rapid_retrigger();
    test_unison_mono_steal_back_cap();
    test_unison_mono_u1_regression();
    test_unison_mono_poly_regression();
    test_unison_mono_count_change();
}
