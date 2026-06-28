/* tests/host/test_alloc.cpp
 *
 * Host tests for the Stage 1c voice allocator (VoiceAlloc + JunoModel).
 *
 * 1. Init — pool populated: kNumVoices slots, all gates down, all voices idle.
 * 2. note_on → one voice becomes active; gate is set.
 * 3. note_off → gate clears; voice still active (release tail), then idles.
 * 4. Retrigger — same pitch re-uses the same slot, not a new one.
 * 5. Polyphony + steal — 9 simultaneous note_ons fill all 8 slots; the 9th
 *    steals the oldest gated voice.
 */

#include "runner.h"
#include "engine/voice_alloc.h"
#include "engine/juno_model.h"
#include "engine/synth_config.h"
#include <stdio.h>
#include <string.h>

static const float kSr = 48000.0f;

static int count_active_slots(const VoiceSlot* slots) {
    int n = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].voice->is_active()) n++;
    return n;
}

static int count_gated_slots(const VoiceSlot* slots) {
    int n = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate) n++;
    return n;
}

/* --- 1. Init ---------------------------------------------------------------- */
void test_alloc_init() {
    printf("--- VoiceAlloc ---\n");
    test_begin("init: kNumVoices slots allocated, all idle");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    const VoiceSlot* slots = alloc.slots();
    for (int i = 0; i < kNumVoices; i++) {
        TEST_ASSERT(slots[i].voice != nullptr, "slot voice pointer must be non-null");
        TEST_ASSERT(!slots[i].gate,            "slot gate must be false at init");
    }
    TEST_ASSERT(count_active_slots(slots) == 0, "no voice should be active at init");
    test_pass();
}

/* --- 2. note_on activates a voice ------------------------------------------ */
void test_alloc_note_on() {
    test_begin("note_on: one slot becomes active");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(69, 100, expr);  // A4

    const VoiceSlot* slots = alloc.slots();
    TEST_ASSERT(count_gated_slots(slots) == 1,  "exactly one slot must be gated");
    TEST_ASSERT(count_active_slots(slots) >= 1, "at least one slot must be active");

    // Render one block to let the voice start producing output.
    float buf[64] = {};
    for (int i = 0; i < kNumVoices; i++) {
        if (slots[i].gate) slots[i].voice->render(buf, 64);
    }
    float sum = 0.0f;
    for (int i = 0; i < 64; i++) sum += buf[i] * buf[i];
    TEST_ASSERT(sum > 0.0f, "active voice must produce non-zero output");
    test_pass();
}

/* --- 3. note_off clears gate; voice eventually idles ----------------------- */
void test_alloc_note_off() {
    test_begin("note_off: gate clears; voice idles after release");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    // Short release so the test runs quickly.
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(69, 100, expr);

    // Set a short release via set_param so the tail drains fast.
    const VoiceSlot* slots = alloc.slots();
    int gated_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate) { gated_idx = i; break; }
    TEST_ASSERT(gated_idx >= 0, "must have found the gated slot");

    // JUNO_PARAM_ENV_RELEASE = 9 (from JunoParam enum)
    slots[gated_idx].voice->set_param(9, 0.05f);

    // Reach sustain (~200 blocks × 64 samples = ~0.27 s).
    float buf[64];
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        slots[gated_idx].voice->render(buf, 64);
    }

    alloc.note_off(69);

    TEST_ASSERT(!slots[gated_idx].gate,          "gate must clear after note_off");
    TEST_ASSERT(slots[gated_idx].voice->is_active(), "voice still active (release tail)");

    // Drain the release tail (220 blocks × 64 / 48000 ≈ 0.29 s >> 0.05 s).
    for (int b = 0; b < 220; b++) {
        memset(buf, 0, sizeof(buf));
        slots[gated_idx].voice->render(buf, 64);
    }

    TEST_ASSERT(!slots[gated_idx].voice->is_active(),
                "voice must be idle after release tail drains");
    test_pass();
}

/* --- 4. Retrigger re-uses the same slot ------------------------------------ */
void test_alloc_retrigger() {
    test_begin("retrigger: same pitch reuses its slot");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);  // C4

    const VoiceSlot* slots = alloc.slots();
    int first_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate && slots[i].pitch == 60) { first_idx = i; break; }
    TEST_ASSERT(first_idx >= 0, "must find the slot for pitch 60");

    alloc.note_on(60, 100, expr);  // retrigger same pitch

    // Must still be exactly one gated slot for this pitch.
    int found = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate && slots[i].pitch == 60) found++;

    TEST_ASSERT(found == 1,              "retrigger must not allocate a second slot");
    TEST_ASSERT(slots[first_idx].gate,   "the original slot must still be gated");
    test_pass();
}

/* --- 5. Polyphony + voice steal -------------------------------------------- */
void test_alloc_steal() {
    test_begin("steal: 9th note_on reclaims oldest gated voice");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Fill all 8 slots with distinct pitches.
    for (int i = 0; i < kNumVoices; i++) {
        alloc.note_on((uint8_t)(60 + i), 100, expr);
    }

    const VoiceSlot* slots = alloc.slots();
    TEST_ASSERT(count_gated_slots(slots) == kNumVoices, "all slots must be gated");

    // Find which slot has pitch 60 (the oldest — timestamp 1).
    int oldest_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].pitch == 60) { oldest_idx = i; break; }
    TEST_ASSERT(oldest_idx >= 0, "must find the oldest slot (pitch 60)");

    // 9th note_on must steal — pool is full, no idle or released slots.
    alloc.note_on(80, 100, expr);

    // Slot at oldest_idx should now hold the new pitch 80.
    TEST_ASSERT(slots[oldest_idx].pitch == 80,
                "oldest slot must be stolen for the 9th note");
    TEST_ASSERT(slots[oldest_idx].gate,
                "stolen slot must be gated with the new note");
    TEST_ASSERT(count_gated_slots(slots) == kNumVoices,
                "total gated count must still be kNumVoices after steal");
    test_pass();
}

void test_alloc_suite() {
    test_alloc_init();
    test_alloc_note_on();
    test_alloc_note_off();
    test_alloc_retrigger();
    test_alloc_steal();
}
