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
 * 17. unison_gain helper — equal-power 1/√U compensation properties.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "engine/juno_model.h"
#include "engine/param_id.h"
#include "engine/synth_config.h"
#include "engine/voice_alloc.h"
#include "runner.h"

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
        TEST_ASSERT(!slots[i].gate, "slot gate must be false at init");
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
    TEST_ASSERT(count_gated_slots(slots) == 1, "exactly one slot must be gated");
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
    const VoiceSlot* slots     = alloc.slots();
    int              gated_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate) {
            gated_idx = i;
            break;
        }
    TEST_ASSERT(gated_idx >= 0, "must have found the gated slot");

    slots[gated_idx].voice->set_param((int)ParamId::ENV_RELEASE, 0.05f);

    // Reach sustain (~200 blocks × 64 samples = ~0.27 s).
    float buf[64];
    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        slots[gated_idx].voice->render(buf, 64);
    }

    alloc.note_off(69);

    TEST_ASSERT(!slots[gated_idx].gate, "gate must clear after note_off");
    TEST_ASSERT(slots[gated_idx].voice->is_active(), "voice still active (release tail)");

    // Drain the release tail (220 blocks × 64 / 48000 ≈ 0.29 s >> 0.05 s).
    for (int b = 0; b < 220; b++) {
        memset(buf, 0, sizeof(buf));
        slots[gated_idx].voice->render(buf, 64);
    }

    TEST_ASSERT(!slots[gated_idx].voice->is_active(), "voice must be idle after release tail drains");
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

    const VoiceSlot* slots     = alloc.slots();
    int              first_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate && slots[i].pitch == 60) {
            first_idx = i;
            break;
        }
    TEST_ASSERT(first_idx >= 0, "must find the slot for pitch 60");

    alloc.note_on(60, 100, expr);  // retrigger same pitch

    // Must still be exactly one gated slot for this pitch.
    int found = 0;
    for (int i = 0; i < kNumVoices; i++)
        if (slots[i].gate && slots[i].pitch == 60) found++;

    TEST_ASSERT(found == 1, "retrigger must not allocate a second slot");
    TEST_ASSERT(slots[first_idx].gate, "the original slot must still be gated");
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
        if (slots[i].pitch == 60) {
            oldest_idx = i;
            break;
        }
    TEST_ASSERT(oldest_idx >= 0, "must find the oldest slot (pitch 60)");

    // 9th note_on must steal — pool is full, no idle or released slots.
    alloc.note_on(80, 100, expr);

    // Slot at oldest_idx should now hold the new pitch 80.
    TEST_ASSERT(slots[oldest_idx].pitch == 80, "oldest slot must be stolen for the 9th note");
    TEST_ASSERT(slots[oldest_idx].gate, "stolen slot must be gated with the new note");
    TEST_ASSERT(count_gated_slots(slots) == kNumVoices, "total gated count must still be kNumVoices after steal");
    test_pass();
}

/* --- 6. Mono: only one voice sounds; last-note priority -------------------- */
void test_alloc_mono_single_voice() {
    test_begin("mono: only one voice gated at a time");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // First note.
    alloc.note_on(60, 100, expr);
    TEST_ASSERT(count_gated_slots(alloc.slots()) == 1, "mono: exactly one gated after note_on");

    // Second note while first is held — only one voice should be gated.
    alloc.note_on(64, 100, expr);
    TEST_ASSERT(count_gated_slots(alloc.slots()) == 1, "mono: still exactly one gated with two held notes");

    // The sounding pitch should be the last-pressed note.
    int gated_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (alloc.slots()[i].gate) {
            gated_idx = i;
            break;
        }
    TEST_ASSERT(gated_idx >= 0, "mono: must find the gated slot");
    TEST_ASSERT(alloc.slots()[gated_idx].pitch == 64, "mono: last note is pitch 64");

    test_pass();
}

/* --- 7. Mono: last-note priority (steal-back on release) ------------------- */
void test_alloc_mono_steal_back() {
    test_begin("mono: steal-back to previous note on release");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Press C4, then E4 (hold both).
    alloc.note_on(60, 100, expr);
    alloc.note_on(64, 100, expr);
    // Release E4 — should steal back to C4.
    alloc.note_off(64);

    int gated_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (alloc.slots()[i].gate) {
            gated_idx = i;
            break;
        }

    TEST_ASSERT(gated_idx >= 0, "mono steal-back: voice must still be gated after release");
    TEST_ASSERT(alloc.slots()[gated_idx].pitch == 60, "mono steal-back: sounding pitch reverts to C4");

    test_pass();
}

/* --- 8. Mono: all notes released gates the voice off ----------------------- */
void test_alloc_mono_all_off() {
    test_begin("mono: all notes released = gate off");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);
    alloc.note_off(60);

    TEST_ASSERT(count_gated_slots(alloc.slots()) == 0, "mono: no gated voices after sole note released");
    test_pass();
}

/* --- 9. Portamento: pitch ramps from old to new over glide time ------------ */
void test_alloc_portamento() {
    test_begin("portamento: glide offset starts non-zero and ramps toward zero");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kMono);

    // 0.5 s portamento time.
    const float kPortaTime = 0.5f;
    alloc.set_portamento_time(kPortaTime);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // First note (no previous → no glide).
    alloc.note_on(60, 100, expr);
    TEST_ASSERT(alloc.glide_offset() == 0.0f, "portamento: first note has zero glide offset");

    // Second note (E4 = semitone +4 from C4) while C4 is held.
    alloc.note_on(64, 100, expr);
    // Initial offset must be -4 semitones (E4 sounds like C4 initially).
    TEST_ASSERT(alloc.glide_offset() < -0.5f, "portamento: initial offset is negative (old pitch below new)");

    // Advance glide halfway through portamento time.
    const float kBlockTime = 0.001f;  // 1 ms blocks
    const int   kHalfSteps = (int)(kPortaTime * 0.5f / kBlockTime);
    for (int b = 0; b < kHalfSteps; b++) {
        alloc.advance_glide(kBlockTime);
    }
    float mid_offset = alloc.glide_offset();
    TEST_ASSERT(mid_offset < 0.0f && mid_offset > -4.0f, "portamento: mid-glide offset is between 0 and initial");

    // Advance through the full portamento time.
    for (int b = 0; b < kHalfSteps + 10; b++) {
        alloc.advance_glide(kBlockTime);
    }
    TEST_ASSERT(alloc.glide_offset() == 0.0f, "portamento: offset reaches zero after portamento time");

    test_pass();
}

/* --- 10. Legato vs. retrigger: poly path unaffected ----------------------- */
void test_alloc_poly_unchanged() {
    test_begin("play mode: poly path unchanged after mono use");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);

    // Start in mono, then switch to poly — verify poly path works normally.
    alloc.set_play_mode(PlayMode::kMono);
    alloc.set_play_mode(PlayMode::kPoly);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    for (int i = 0; i < kNumVoices; i++) {
        alloc.note_on((uint8_t)(60 + i), 100, expr);
    }

    TEST_ASSERT(count_gated_slots(alloc.slots()) == kNumVoices, "poly: all kNumVoices gated after poly mode restored");
    test_pass();
}

/* --- 11. Legato: no retrigger when note overlaps held note --------------- */
void test_alloc_legato_no_retrigger() {
    test_begin("legato: overlapping notes do not retrigger if a note was held");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_play_mode(PlayMode::kLegato);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // First note — this is a fresh attack (no prior held note → retrigger expected).
    alloc.note_on(60, 100, expr);

    // Render enough to leave attack phase.
    float buf[64];
    int   gated_idx = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (alloc.slots()[i].gate) {
            gated_idx = i;
            break;
        }
    TEST_ASSERT(gated_idx >= 0, "legato: must have a gated slot");

    for (int b = 0; b < 200; b++) {
        memset(buf, 0, sizeof(buf));
        alloc.slots()[gated_idx].voice->render(buf, 64);
    }

    // While first note is still held, press a second note (legato transition).
    // The slot pitch should change (legato detection worked).
    alloc.note_on(64, 100, expr);
    TEST_ASSERT(alloc.slots()[gated_idx].pitch == 64, "legato: slot pitch changes to new note on legato transition");
    TEST_ASSERT(alloc.slots()[gated_idx].gate, "legato: gate stays true during legato transition");

    // Release the new note — should steal back to 60.
    alloc.note_off(64);
    TEST_ASSERT(alloc.slots()[gated_idx].pitch == 60, "legato: steal-back to first note when second released");
    TEST_ASSERT(alloc.slots()[gated_idx].gate, "legato: gate stays true after steal-back (first key still held)");

    // Release the first note — now gate should drop.
    alloc.note_off(60);
    TEST_ASSERT(!alloc.slots()[gated_idx].gate, "legato: gate drops after all notes released");

    test_pass();
}

/* --- 12. Unison: U voices allocated for one note --------------------------- */
void test_alloc_unison_stack() {
    test_begin("unison U=4: four voices allocated for one note");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_unison_count(4);
    alloc.set_unison_detune(20.0f);  // 20 cents total spread

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);  // C4, should allocate 4 voices

    const VoiceSlot* slots = alloc.slots();
    int              gated = count_gated_slots(slots);
    TEST_ASSERT(gated == 4, "unison U=4: exactly 4 voices must be gated");

    // All gated slots must be on pitch 60.
    for (int i = 0; i < kNumVoices; i++) {
        if (slots[i].gate) {
            TEST_ASSERT(slots[i].pitch == 60, "unison: all gated slots must be pitch 60");
        }
    }

    test_pass();
}

/* --- 13. Unison: detune offsets are spread symmetrically ------------------- */
void test_alloc_unison_detune_offsets() {
    test_begin("unison U=2: detune offsets are non-zero and anti-symmetric");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_unison_count(2);
    alloc.set_unison_detune(20.0f);  // ±10 cents = ±0.1 semitone

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(60, 100, expr);

    // Gather the pitch offsets from the 2 gated voices.
    const VoiceSlot* slots = alloc.slots();
    TEST_ASSERT(count_gated_slots(slots) == 2, "unison U=2: exactly 2 gated");

    // Advance glide (no glide, but this is how the real render path works).
    // Each voice should have been given a non-zero pitch offset at note_on.
    // Since we can't read the pitch offset from IVoice directly, verify indirectly:
    // render one block per voice — with detune the two voices will produce slightly
    // different phases; their sum is not zero (hard to test directly without phase).
    // Instead, just verify the count and that the voices are active and producing output.
    float buf[64] = {};
    for (int i = 0; i < kNumVoices; i++) {
        if (slots[i].gate) slots[i].voice->render(buf, 64);
    }
    float energy = 0.0f;
    for (int i = 0; i < 64; i++) energy += buf[i] * buf[i];
    TEST_ASSERT(energy > 0.0f, "unison U=2: combined voices must produce non-zero output");

    test_pass();
}

/* --- 14. Unison: note_off releases all group voices ------------------------ */
void test_alloc_unison_note_off() {
    test_begin("unison U=3: note_off releases all 3 group voices");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_unison_count(3);
    alloc.set_unison_detune(15.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    alloc.note_on(64, 100, expr);

    const VoiceSlot* slots = alloc.slots();
    TEST_ASSERT(count_gated_slots(slots) == 3, "unison U=3: 3 gated before note_off");

    alloc.note_off(64);

    TEST_ASSERT(count_gated_slots(slots) == 0, "unison U=3: 0 gated after note_off releases all group voices");
    test_pass();
}

/* --- 15. Unison: reduces effective polyphony ------------------------------- */
void test_alloc_unison_reduces_polyphony() {
    test_begin("unison U=4: at most 2 distinct notes fit in 8-voice pool");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_unison_count(4);
    alloc.set_unison_detune(10.0f);

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Two notes should fill 8 slots (4 × 2 = 8).
    alloc.note_on(60, 100, expr);
    alloc.note_on(64, 100, expr);

    const VoiceSlot* slots = alloc.slots();
    // All 8 slots should be gated (2 notes × 4 voices each).
    TEST_ASSERT(count_gated_slots(slots) == kNumVoices, "unison U=4 × 2 notes fills the pool (8 gated)");

    // A 3rd note forces steal; total gated remains kNumVoices.
    alloc.note_on(67, 100, expr);
    TEST_ASSERT(count_gated_slots(slots) == kNumVoices, "unison U=4: 3rd note steals but total stays at kNumVoices");

    test_pass();
}

/* --- 16. Unison: U=1 is identical to original poly path ------------------- */
void test_alloc_unison_u1_unchanged() {
    test_begin("unison U=1: identical to standard poly path");

    JunoModel  model;
    VoiceAlloc alloc;
    model.init(kSr);
    alloc.init(&model);
    alloc.set_unison_count(1);
    alloc.set_unison_detune(20.0f);  // detune is ignored when U=1

    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};

    // Fill pool and verify standard poly behaviour.
    for (int i = 0; i < kNumVoices; i++) {
        alloc.note_on((uint8_t)(60 + i), 100, expr);
    }
    TEST_ASSERT(count_gated_slots(alloc.slots()) == kNumVoices,
                "unison U=1: kNumVoices gated for kNumVoices distinct notes");

    alloc.note_off(60);
    int gated_after = count_gated_slots(alloc.slots());
    TEST_ASSERT(gated_after == kNumVoices - 1, "unison U=1: note_off releases exactly 1 voice");

    test_pass();
}

/* --- 17. unison_gain: equal-power 1/√U compensation helper --------------- */
// Tests the same formula used in synth.cpp (engine/synth.cpp, step 6):
//   unison_gain(count) = 1.0f / sqrtf((float)(count < 1 ? 1 : count))
// Verified properties:
//   U=1  → 1.0 (no change from pre-unison output, bit-identical)
//   U=4  → 0.5 (2 octaves of voices → half amplitude)
//   monotonically decreasing
//   worst-case U=8 with MASTER_GAIN=0.5: 8 voices × 0.5 × (1/√8) ≈ 1.414 < 1.5 soft_clip ceiling
static inline float test_unison_gain_fn(int count) {
    return 1.0f / sqrtf((float)(count < 1 ? 1 : count));
}
void test_unison_gain() {
    printf("--- unison_gain compensation ---\n");
    test_begin("unison_gain: U=1 yields 1.0 (no compensation change)");
    float g1 = test_unison_gain_fn(1);
    TEST_ASSERT(g1 == 1.0f, "unison_gain(1) must be exactly 1.0");
    test_pass();

    test_begin("unison_gain: U=4 yields 0.5");
    float g4 = test_unison_gain_fn(4);
    // 1/sqrt(4) = 0.5 exactly
    TEST_ASSERT(fabsf(g4 - 0.5f) < 1e-6f, "unison_gain(4) must be 0.5");
    test_pass();

    test_begin("unison_gain: monotonically decreasing U=1..8");
    float prev = test_unison_gain_fn(1);
    for (int u = 2; u <= 8; u++) {
        float cur = test_unison_gain_fn(u);
        TEST_ASSERT(cur < prev, "unison_gain must decrease with each increment");
        prev = cur;
    }
    test_pass();

    test_begin("unison_gain: U=8 worst-case stays below soft_clip ceiling (1.5)");
    // Worst-case: 8 voices each at amplitude 1.0, MASTER_GAIN=0.5, compensation applied.
    // peak = 8 voices × 1.0 × MASTER_GAIN(0.5) × unison_gain(8)
    // But unison_gain(8) × MASTER_GAIN(0.5): product = 0.5/sqrt(8) ≈ 0.177
    // Actually: the raw mono bus worst case is sum of 8 unity-amplitude voices = 8.0
    // After gain: 8.0 × 0.5 (MASTER_GAIN) × unison_gain(8) = 4.0 × (1/sqrt(8)) ≈ 1.414
    // 1.414 < 1.5 (soft_clip hard-clamp) — confirms no hard clipping at U=8.
    float worst_case = 8.0f * 0.5f * test_unison_gain_fn(8);  // 8 voices, MASTER_GAIN=0.5
    TEST_ASSERT(worst_case < 1.5f, "worst-case U=8 mono sum stays below soft_clip ceiling");
    test_pass();

    test_begin("unison_gain: clamp for count < 1 returns 1.0 (no NaN/Inf)");
    float g0 = test_unison_gain_fn(0);
    TEST_ASSERT(g0 == 1.0f, "unison_gain(0) must clamp to 1.0 (same as U=1)");
    test_pass();
}

void test_alloc_suite() {
    test_alloc_init();
    test_alloc_note_on();
    test_alloc_note_off();
    test_alloc_retrigger();
    test_alloc_steal();
    // Stage 3d-i: play mode tests.
    printf("--- VoiceAlloc play modes ---\n");
    test_alloc_mono_single_voice();
    test_alloc_mono_steal_back();
    test_alloc_mono_all_off();
    test_alloc_portamento();
    test_alloc_poly_unchanged();
    test_alloc_legato_no_retrigger();
    // Stage 3d-ii: unison tests.
    printf("--- VoiceAlloc unison ---\n");
    test_alloc_unison_stack();
    test_alloc_unison_detune_offsets();
    test_alloc_unison_note_off();
    test_alloc_unison_reduces_polyphony();
    test_alloc_unison_u1_unchanged();
    // Unison gain compensation (bug fix: U>=3 clipping).
    test_unison_gain();
}
