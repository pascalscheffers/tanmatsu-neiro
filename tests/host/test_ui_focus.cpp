/* tests/host/test_ui_focus.cpp — ui_focus_param: CC-driven page/row jump.
 *
 * Tests that ui_focus_param:
 *  1. Finds the correct page and row for a mapped param, updates s->norms[],
 *     and returns true.
 *  2. Returns false (and leaves page/row unchanged) for a param not on any page.
 *
 * Compiles ui_focus.cpp (PAX-free) + ui_page_table.cpp directly; no ui.cpp
 * or PAX needed. Engine calls are stubbed as no-ops.
 */

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "engine/param_desc.h"
#include "engine/param_id.h"
#include "runner.h"
#include "ui/ui.h"
#include "ui/ui_page_table.h"

// Engine stubs are provided by test_ui_presets.cpp (same link unit).

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a minimal UIState without calling ui_state_init (which touches storage
// and the full engine). Zero-init is sufficient: page=0 (PRESET), row=0.
static void make_state(UIState* s) {
    memset(s, 0, sizeof(*s));
    // Fill norms with a sentinel so we can detect changes.
    for (int i = 0; i < UI_NORM_TABLE_SIZE; i++) {
        s->norms[i] = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Test 1: FILTER_CUTOFF (CC21) is on the FILTER page (index 3), not page 0.
//         ui_focus_param must jump there, select the correct row, and set norm.
// ---------------------------------------------------------------------------
static void test_focus_filter_cutoff(void) {
    test_begin("ui_focus_param: FILTER_CUTOFF lands on FILTER page with correct norm");

    UIState s;
    make_state(&s);

    uint16_t id   = ParamId::FILTER_CUTOFF;
    float    norm = 0.5f;
    bool     ok   = ui_focus_param(&s, id, norm);

    TEST_ASSERT(ok, "ui_focus_param should return true for FILTER_CUTOFF");

    // FILTER page is index 3 in PAGE_TABLE (PRESET=0 PERFORM=1 OSC=2 FILTER=3).
    TEST_ASSERT(s.page != 0, "page should not stay on PRESET (0)");
    TEST_ASSERT(s.page == 3, "page should be FILTER (index 3)");

    // The selected row must be the one whose id is FILTER_CUTOFF.
    const ParamDesc* rows[24];
    int              n = page_rows(s.page, rows, 24);
    TEST_ASSERT(n > 0, "FILTER page must have rows");
    TEST_ASSERT(s.row >= 0 && s.row < n, "row must be in range");
    TEST_ASSERT(rows[s.row]->id == id, "selected row must be FILTER_CUTOFF");

    // Norm shadow must be updated.
    TEST_ASSERT(fabsf(s.norms[id] - norm) < 1e-6f, "norms[FILTER_CUTOFF] must equal 0.5");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 2: A param id that is not displayed on any page returns false and
//         leaves page and row unchanged.
// ---------------------------------------------------------------------------
static void test_focus_unknown_id(void) {
    test_begin("ui_focus_param: unknown id returns false, page/row unchanged");

    UIState s;
    make_state(&s);
    s.page = 2;  // OSC page
    s.row  = 1;

    // 0xFFFE is well outside all known param IDs.
    bool ok = ui_focus_param(&s, 0xFFFEu, 0.7f);

    TEST_ASSERT(!ok, "ui_focus_param should return false for unknown id");
    TEST_ASSERT(s.page == 2, "page must remain unchanged");
    TEST_ASSERT(s.row == 1, "row must remain unchanged");

    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry
// ---------------------------------------------------------------------------
void test_ui_focus_suite(void) {
    printf("--- ui_focus_param: CC-driven page/row focus ---\n");
    test_focus_filter_cutoff();
    test_focus_unknown_id();
}
