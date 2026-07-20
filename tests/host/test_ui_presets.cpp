/* tests/host/test_ui_presets.cpp — WO-3: audition-with-revert UIState logic.
 *
 * Tests the snapshot → audition → revert and snapshot → audition → commit
 * state-machine round-trips implemented in ui/ui_presets.cpp.
 *
 * Engine calls (engine_set_param, engine_set_param_norm, engine_set_routings)
 * are provided as no-op stubs here so this test links without the full synth
 * engine and audio context.  platform_storage_load returns -1 (no user preset)
 * so the "User" row audition is a safe no-op as well.
 *
 * The tests exercise:
 *  1. ui_presets_snapshot() captures norms/name/idx into the snapshot fields
 *     and clears auditioning.
 *  2. After mutating norms[] (simulating audition), reverting via F3 restores
 *     the snapshot values in UIState.
 *  3. Confirming via F4 updates the snapshot to the current (auditioning) values,
 *     clears auditioning — no revert occurs on the next back press.
 *  4. Navigating away (Left/Right) while auditioning reverts before page-switch.
 */

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "engine/preset.h"
#include "platform/platform.h"
#include "runner.h"
#include "ui/ui.h"
#include "ui/ui_presets.h"

// ---------------------------------------------------------------------------
// Engine stubs — no-ops for test isolation.
// ---------------------------------------------------------------------------
extern "C" {
void  engine_set_param(uint16_t /*id*/, float /*value*/) {}
void  engine_set_param_norm(uint16_t /*id*/, float /*norm*/) {}
void  engine_set_routings(const struct Routing* /*r*/, int /*n*/) {}
void  engine_all_notes_off(void) {}
float engine_get_param(uint16_t /*id*/) {
    return 0.0f;
}
}  // extern "C"

// ---------------------------------------------------------------------------
// Platform storage stub — no user preset on disk.
// ---------------------------------------------------------------------------
extern "C" {
int platform_storage_load(const char* /*key*/, void* /*buf*/, size_t /*max*/) {
    return -1;
}
int platform_storage_save(const char* /*key*/, const void* /*data*/, size_t /*len*/) {
    return 0;
}
}  // extern "C"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a key-press event.
static platform_event_t key_press(int key) {
    platform_event_t ev{};
    ev.type    = PLATFORM_EV_KEY;
    ev.key     = key;
    ev.pressed = true;
    ev.mods    = 0;
    return ev;
}

// Initialise a UIState with preset index 0 ("INIT") and a recognisable norm pattern.
static void init_state(UIState* s) {
    memset(s, 0, sizeof(*s));
    s->page       = 0;  // PAGE_PRESETS
    s->preset_idx = 0;
    strncpy(s->preset_name, "INIT", sizeof(s->preset_name) - 1);
    // Fill norms with a recognisable pattern (id/128.0f).
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) {
        s->norms[i] = (float)i / (float)UI_NORM_TABLE_SIZE;
    }
    s->auditioning         = false;
    s->audition_preset_idx = 0;
    strncpy(s->audition_preset_name, "INIT", sizeof(s->audition_preset_name) - 1);
    memcpy(s->audition_snapshot, s->norms, sizeof(s->norms));
}

// ---------------------------------------------------------------------------
// Test 1: snapshot captures current state and clears auditioning.
// ---------------------------------------------------------------------------
static void test_snapshot_captures_state(void) {
    test_begin("snapshot captures norms/name/idx; clears auditioning");

    UIState s;
    init_state(&s);
    // Simulate being mid-audition.
    s.auditioning = true;
    s.preset_idx  = 2;
    s.norms[5]    = 0.75f;
    strncpy(s.preset_name, "Brass", sizeof(s.preset_name) - 1);

    ui_presets_snapshot(&s);

    TEST_ASSERT(!s.auditioning, "auditioning should be false after snapshot");
    TEST_ASSERT(s.audition_preset_idx == 2, "snapshot should capture preset_idx=2");
    TEST_ASSERT(strcmp(s.audition_preset_name, "Brass") == 0, "snapshot should capture name");
    TEST_ASSERT(fabsf(s.audition_snapshot[5] - 0.75f) < 1e-6f, "snapshot should capture norms[5]");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 2: snapshot → audition (norms change) → F3 revert → norms restored.
// ---------------------------------------------------------------------------
static void test_revert_restores_snapshot(void) {
    test_begin("snapshot → audition norms change → F3 revert → norms restored");

    UIState s;
    init_state(&s);

    // Take snapshot at known state: norms all = 0.1f, name="PRE", idx=1.
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.1f;
    s.preset_idx = 1;
    strncpy(s.preset_name, "PRE", sizeof(s.preset_name) - 1);
    ui_presets_snapshot(&s);

    // Simulate audition: change norms[], name, idx.
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.9f;
    s.preset_idx  = 3;
    s.auditioning = true;
    strncpy(s.preset_name, "DURING", sizeof(s.preset_name) - 1);

    // Send F3 (back/revert).
    platform_event_t ev       = key_press(PLATFORM_KEY_F3);
    bool             consumed = ui_presets_handle_event(&s, &ev);

    TEST_ASSERT(consumed, "F3 should be consumed on preset page");
    TEST_ASSERT(!s.auditioning, "auditioning should be false after revert");
    TEST_ASSERT(strcmp(s.preset_name, "PRE") == 0, "preset_name should be restored");
    TEST_ASSERT(s.preset_idx == 1, "preset_idx should be restored");

    // Verify a sample of norms — the revert iterates the param table and restores
    // each param's norm from the snapshot via engine_set_param_norm (stubbed).
    // UIState.norms[] should be restored from audition_snapshot[].
    bool norms_restored = true;
    for (int j = 0; j < kJunoParamCount; j++) {
        uint16_t id = JUNO_PARAM_TABLE[j].id;
        if (id < UI_NORM_TABLE_SIZE) {
            if (fabsf(s.norms[id] - s.audition_snapshot[id]) > 1e-5f) {
                norms_restored = false;
                break;
            }
        }
    }
    TEST_ASSERT(norms_restored, "norms[] should be restored from audition_snapshot[]");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 3: snapshot → audition → F4 confirm → snapshot updated, no revert.
// ---------------------------------------------------------------------------
static void test_confirm_updates_snapshot(void) {
    test_begin("snapshot → audition → F4 confirm → snapshot updated, no revert");

    UIState s;
    init_state(&s);

    // Take snapshot at known state.
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.2f;
    s.preset_idx = 0;
    strncpy(s.preset_name, "ORIG", sizeof(s.preset_name) - 1);
    ui_presets_snapshot(&s);

    // Simulate audition to a different preset.
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.8f;
    s.preset_idx  = 2;
    s.auditioning = true;
    strncpy(s.preset_name, "NEW", sizeof(s.preset_name) - 1);

    // Send F4 (confirm).
    platform_event_t ev       = key_press(PLATFORM_KEY_F4);
    bool             consumed = ui_presets_handle_event(&s, &ev);

    TEST_ASSERT(consumed, "F4 should be consumed on preset page");
    TEST_ASSERT(!s.auditioning, "auditioning should be false after confirm");
    // Snapshot should now reflect the new patch.
    TEST_ASSERT(strcmp(s.audition_preset_name, "NEW") == 0, "snapshot name should be updated after confirm");
    TEST_ASSERT(s.audition_preset_idx == 2, "snapshot idx should be updated after confirm");

    // A subsequent F3 press should be a no-op (auditioning=false → revert is skipped).
    ev = key_press(PLATFORM_KEY_F3);
    ui_presets_handle_event(&s, &ev);
    TEST_ASSERT(strcmp(s.preset_name, "NEW") == 0, "post-confirm F3 should not revert (already committed)");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 4: Esc alias reverts same as F3.
// ---------------------------------------------------------------------------
static void test_esc_alias_reverts(void) {
    test_begin("Esc alias reverts same as F3");

    UIState s;
    init_state(&s);

    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.3f;
    s.preset_idx = 0;
    strncpy(s.preset_name, "BASE", sizeof(s.preset_name) - 1);
    ui_presets_snapshot(&s);

    s.auditioning = true;
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.7f;
    s.preset_idx = 1;
    strncpy(s.preset_name, "TEMP", sizeof(s.preset_name) - 1);

    platform_event_t ev       = key_press('\x1B');  // Esc
    bool             consumed = ui_presets_handle_event(&s, &ev);

    TEST_ASSERT(consumed, "Esc should be consumed on preset page");
    TEST_ASSERT(!s.auditioning, "auditioning should be false after Esc revert");
    TEST_ASSERT(strcmp(s.preset_name, "BASE") == 0, "preset_name should be restored after Esc");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 5: Enter alias confirms same as F4.
// ---------------------------------------------------------------------------
static void test_enter_alias_confirms(void) {
    test_begin("Enter alias confirms same as F4");

    UIState s;
    init_state(&s);

    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.4f;
    s.preset_idx = 0;
    strncpy(s.preset_name, "ORIG2", sizeof(s.preset_name) - 1);
    ui_presets_snapshot(&s);

    s.auditioning = true;
    s.preset_idx  = 5;
    strncpy(s.preset_name, "CONFIRMED", sizeof(s.preset_name) - 1);

    platform_event_t ev       = key_press('\n');  // Enter
    bool             consumed = ui_presets_handle_event(&s, &ev);

    TEST_ASSERT(consumed, "Enter should be consumed on preset page");
    TEST_ASSERT(!s.auditioning, "auditioning should be false after Enter confirm");
    TEST_ASSERT(s.audition_preset_idx == 5, "snapshot should be updated with confirmed idx");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 6: Left/Right returns false (pass-through) but reverts audition.
// ---------------------------------------------------------------------------
static void test_page_nav_reverts_and_passes_through(void) {
    test_begin("Left/Right while auditioning: reverts and returns false (pass-through)");

    UIState s;
    init_state(&s);

    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) s.norms[i] = 0.5f;
    s.preset_idx = 0;
    strncpy(s.preset_name, "SNAP", sizeof(s.preset_name) - 1);
    ui_presets_snapshot(&s);

    s.auditioning = true;
    s.preset_idx  = 2;
    strncpy(s.preset_name, "AUDITIONING", sizeof(s.preset_name) - 1);

    platform_event_t ev       = key_press(PLATFORM_KEY_LEFT);
    bool             consumed = ui_presets_handle_event(&s, &ev);

    TEST_ASSERT(!consumed, "Left should NOT be consumed (pass-through to page switch)");
    TEST_ASSERT(!s.auditioning, "auditioning should be cleared after Left");
    TEST_ASSERT(strcmp(s.preset_name, "SNAP") == 0, "preset_name should be reverted");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 7: WO-13j — the 141-row list (140 factory + User) reaches every
// boundary and a representative sample of middle rows without overflow.
// Navigation goes through ui_presets_handle_event() only; the test never
// assigns UIState::row to skip across the bank.
// ---------------------------------------------------------------------------
static void press_preset_key(UIState* s, int key) {
    platform_event_t ev = key_press(key);
    ui_presets_handle_event(s, &ev);
}

static void browse_down_to(UIState* s, int target, int total) {
    int moves = 0;
    while (s->row != target && moves <= total) {
        press_preset_key(s, PLATFORM_KEY_DOWN);
        moves++;
    }
    TEST_ASSERT(s->row == target, "browse should reach the requested row before wrapping twice");
    TEST_ASSERT(moves <= total, "browse should not overflow or loop past one complete list span");
}

static void reaudition_current_row(UIState* s) {
    press_preset_key(s, PLATFORM_KEY_UP);
    press_preset_key(s, PLATFORM_KEY_DOWN);
}

static void test_boundary_and_middle_rows_reachable(void) {
    test_begin("140-row factory span + User: boundaries and middle rows reachable, no overflow");

    const int factory_n = preset_factory_count();
    TEST_ASSERT(factory_n == 140, "factory count should be 140 (128 Juno-106 + 12 Neiro)");
    const int user_row = factory_n;
    const int n        = factory_n + 1;

    // Slot-order sanity: A11 .. A88 .. B11 .. B88 (canonical Juno-106 order).
    TEST_ASSERT(strcmp(preset_factory_name(0), "A11") == 0, "row 0 should be A11");
    TEST_ASSERT(strncmp(preset_factory_name(63), "A88", 3) == 0, "row 63 should be A88 (last A-group slot)");
    TEST_ASSERT(strcmp(preset_factory_name(64), "B11") == 0, "row 64 should be B11 (first B-group slot)");
    TEST_ASSERT(strncmp(preset_factory_name(127), "B88", 3) == 0, "row 127 should be B88 (last original slot)");
    // Neiro bank follows immediately after the 128 originals.
    TEST_ASSERT(strcmp(preset_factory_name(128), "INIT") == 0, "row 128 should be the first Neiro patch");
    TEST_ASSERT(strcmp(preset_factory_name(139), "Chip Noise") == 0, "row 139 should be the last Neiro patch");

    UIState s;
    init_state(&s);

    // Prove both list-end wraps before walking the ordered boundary sample.
    press_preset_key(&s, PLATFORM_KEY_UP);
    TEST_ASSERT(s.row == user_row, "Up from A11 should wrap to User");
    press_preset_key(&s, PLATFORM_KEY_DOWN);
    TEST_ASSERT(s.row == 0, "Down from User should wrap to A11");
    press_preset_key(&s, PLATFORM_KEY_F3);

    // Canonical boundaries plus representative middle rows. Each factory row
    // gets two independent transactions: audition -> revert, then audition ->
    // confirm. The final User row exercises the same commands safely with the
    // test's intentional "no saved user preset" storage stub.
    const int rows_to_visit[] = {0, 32, 63, 64, 95, 127, 128, 133, 139, user_row};
    for (size_t i = 0; i < sizeof(rows_to_visit) / sizeof(rows_to_visit[0]); i++) {
        int target = rows_to_visit[i];
        if (s.row == target) reaudition_current_row(&s);
        browse_down_to(&s, target, n);

        bool is_user_row = (target == user_row);
        if (!is_user_row) {
            TEST_ASSERT(s.auditioning, "browsing to a factory row should start an audition");
            TEST_ASSERT(strcmp(s.preset_name, preset_factory_name(target)) == 0,
                        "audition at a factory row should load that row's name into UIState");
            TEST_ASSERT(s.preset_idx == target, "audition should retain the full factory row index");
        }
        TEST_ASSERT(s.row == target, "row should stay exactly at the requested index (no wrap/overflow)");

        int  before_revert_idx = s.audition_preset_idx;
        char before_revert_name[PRESET_NAME_LEN + 1];
        strncpy(before_revert_name, s.audition_preset_name, sizeof(before_revert_name));
        before_revert_name[sizeof(before_revert_name) - 1] = '\0';
        press_preset_key(&s, PLATFORM_KEY_F3);
        TEST_ASSERT(!s.auditioning, "revert should clear auditioning at every sampled row");
        TEST_ASSERT(s.preset_idx == before_revert_idx, "revert should restore the previously-confirmed index");
        TEST_ASSERT(strcmp(s.preset_name, before_revert_name) == 0,
                    "revert should restore the previously-confirmed name");

        reaudition_current_row(&s);
        TEST_ASSERT(s.row == target, "re-audition should return to the same full-width row index");
        press_preset_key(&s, PLATFORM_KEY_F4);
        TEST_ASSERT(!s.auditioning, "confirm should clear auditioning at every sampled row");
        if (!is_user_row) {
            TEST_ASSERT(s.preset_idx == target, "confirm should retain the selected factory row index");
            TEST_ASSERT(s.audition_preset_idx == target, "confirm should snapshot the selected factory row index");
            TEST_ASSERT(strcmp(s.audition_preset_name, preset_factory_name(target)) == 0,
                        "confirm should snapshot the selected factory name");
        }
    }

    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry point
// ---------------------------------------------------------------------------

void test_ui_presets_suite(void) {
    printf("--- WO-3: preset page audition-with-revert ---\n");
    test_snapshot_captures_state();
    test_revert_restores_snapshot();
    test_confirm_updates_snapshot();
    test_esc_alias_reverts();
    test_enter_alias_confirms();
    test_page_nav_reverts_and_passes_through();
    printf("--- WO-13j: 140-row factory span browser reachability ---\n");
    test_boundary_and_middle_rows_reachable();
}
