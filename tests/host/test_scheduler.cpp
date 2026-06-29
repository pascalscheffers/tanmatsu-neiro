/* tests/host/test_scheduler.cpp — unit tests for engine/scheduler.h */
#include <stdint.h>
#include <string.h>
#include "engine/scheduler.h"
#include "runner.h"

// ---------------------------------------------------------------------------
// Helper: record callbacks into a small fixed array.
// ---------------------------------------------------------------------------
struct CallRecord {
    uint8_t  pitch;
    uint8_t  velocity;
    uint8_t  type;
    uint32_t offset;
};

static CallRecord s_calls[128];
static int        s_call_count;

static void reset_calls() {
    s_call_count = 0;
    memset(s_calls, 0, sizeof(s_calls));
}

// ---------------------------------------------------------------------------
// Test 1: basic scheduling — events fire in the correct block, offsets correct.
// ---------------------------------------------------------------------------
static void test_basic_dispatch() {
    test_begin("scheduler: basic dispatch in correct blocks");

    Scheduler<64> sched;

    NoteCmd n100{NoteCmd::kNoteOn, 60, 100};
    NoteCmd n200{NoteCmd::kNoteOn, 62, 101};
    NoteCmd n300{NoteCmd::kNoteOn, 64, 102};

    sched.schedule(100, n100);
    sched.schedule(200, n200);
    sched.schedule(300, n300);

    TEST_ASSERT(sched.pending() == 3, "expected 3 pending after scheduling");

    // Block 0: [0, 64) — no events due (all >= 64)
    reset_calls();
    int fired = sched.dispatch_due(0, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 0, "block [0,64): expected 0 events");
    TEST_ASSERT(sched.pending() == 3, "block [0,64): still 3 pending");

    // Block 1: [64, 128) — event at 100 fires (100 < 128), offset = 100-64 = 36
    reset_calls();
    fired = sched.dispatch_due(64, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 1, "block [64,128): expected 1 event");
    TEST_ASSERT(s_call_count == 1, "block [64,128): 1 callback");
    TEST_ASSERT(s_calls[0].pitch == 60, "block [64,128): pitch 60");
    TEST_ASSERT(s_calls[0].offset == 36, "block [64,128): offset 36");
    TEST_ASSERT(sched.pending() == 2, "block [64,128): 2 remaining");

    // Block 2: [128, 192) — event at 200 is NOT due (200 >= 192)
    reset_calls();
    fired = sched.dispatch_due(128, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 0, "block [128,192): expected 0 events");
    TEST_ASSERT(sched.pending() == 2, "block [128,192): still 2 pending");

    // Block 3: [192, 256) — event at 200 fires (200 < 256), offset = 200-192 = 8
    reset_calls();
    fired = sched.dispatch_due(192, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 1, "block [192,256): expected 1 event");
    TEST_ASSERT(s_calls[0].pitch == 62, "block [192,256): pitch 62");
    TEST_ASSERT(s_calls[0].offset == 8, "block [192,256): offset 8");

    // Block 4: [256, 320) — event at 300 fires, offset = 300-256 = 44
    reset_calls();
    fired = sched.dispatch_due(256, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 1, "block [256,320): expected 1 event");
    TEST_ASSERT(s_calls[0].pitch == 64, "block [256,320): pitch 64");
    TEST_ASSERT(s_calls[0].offset == 44, "block [256,320): offset 44");
    TEST_ASSERT(sched.pending() == 0, "all events consumed");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 2: out-of-order scheduling fires in ascending sample_time order.
// ---------------------------------------------------------------------------
static void test_ordering() {
    test_begin("scheduler: ascending order when scheduled out of order");

    Scheduler<64> sched;

    // Schedule deliberately out of order: 300, 100, 200
    NoteCmd na{NoteCmd::kNoteOn, 70, 10};  // at 300
    NoteCmd nb{NoteCmd::kNoteOn, 71, 11};  // at 100
    NoteCmd nc{NoteCmd::kNoteOn, 72, 12};  // at 200

    sched.schedule(300, na);
    sched.schedule(100, nb);
    sched.schedule(200, nc);

    // All three fall in block [0, 400) — dispatch together.
    reset_calls();
    int fired = sched.dispatch_due(0, 400, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });

    TEST_ASSERT(fired == 3, "ordering: expected 3 events dispatched");
    TEST_ASSERT(s_calls[0].pitch == 71, "ordering: first should be pitch 71 (t=100)");
    TEST_ASSERT(s_calls[1].pitch == 72, "ordering: second should be pitch 72 (t=200)");
    TEST_ASSERT(s_calls[2].pitch == 70, "ordering: third should be pitch 70 (t=300)");

    // Offsets must also be in ascending order.
    TEST_ASSERT(s_calls[0].offset == 100, "ordering: offset[0] == 100");
    TEST_ASSERT(s_calls[1].offset == 200, "ordering: offset[1] == 200");
    TEST_ASSERT(s_calls[2].offset == 300, "ordering: offset[2] == 300");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 3: late events (sample_time <= now) dispatch at offset 0.
// ---------------------------------------------------------------------------
static void test_late_events() {
    test_begin("scheduler: late events dispatch at offset 0");

    Scheduler<64> sched;

    // Schedule event in the past: sample_time=10, now=100 (already late)
    NoteCmd n{NoteCmd::kNoteOn, 50, 80};
    sched.schedule(10, n);

    reset_calls();
    int fired = sched.dispatch_due(100, 64, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });

    TEST_ASSERT(fired == 1, "late: expected 1 event dispatched");
    TEST_ASSERT(s_calls[0].pitch == 50, "late: correct pitch");
    TEST_ASSERT(s_calls[0].offset == 0, "late: offset must be 0 (not negative)");
    TEST_ASSERT(sched.pending() == 0, "late: no remaining events");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 4: capacity — scheduling past Cap returns false; existing entries safe.
// ---------------------------------------------------------------------------
static void test_capacity() {
    test_begin("scheduler: capacity limit returns false, no corruption");

    Scheduler<4> sched;  // tiny cap for the test

    NoteCmd n{NoteCmd::kNoteOn, 60, 100};

    bool ok0 = sched.schedule(10, n);
    bool ok1 = sched.schedule(20, n);
    bool ok2 = sched.schedule(30, n);
    bool ok3 = sched.schedule(40, n);
    bool ok4 = sched.schedule(50, n);  // must fail — cap is 4

    TEST_ASSERT(ok0 && ok1 && ok2 && ok3, "cap: first 4 schedule calls succeed");
    TEST_ASSERT(!ok4,                      "cap: 5th schedule call returns false");
    TEST_ASSERT(sched.pending() == 4,      "cap: still 4 pending (not 5)");

    // Dispatch all 4 — should fire in order 10/20/30/40.
    reset_calls();
    int fired = sched.dispatch_due(0, 100, [](const NoteCmd& cmd, uint32_t offset) {
        if (s_call_count < 128) {
            s_calls[s_call_count++] = {cmd.pitch, cmd.velocity, cmd.type, offset};
        }
    });
    TEST_ASSERT(fired == 4, "cap: 4 events dispatched");
    TEST_ASSERT(sched.pending() == 0, "cap: 0 remaining after dispatch");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 5: clear() empties all pending events.
// ---------------------------------------------------------------------------
static void test_clear() {
    test_begin("scheduler: clear() empties pending");

    Scheduler<64> sched;

    NoteCmd n{NoteCmd::kNoteOn, 60, 100};
    sched.schedule(10, n);
    sched.schedule(20, n);
    sched.schedule(30, n);

    TEST_ASSERT(sched.pending() == 3, "clear: 3 pending before clear");

    sched.clear();

    TEST_ASSERT(sched.pending() == 0, "clear: 0 pending after clear");

    // dispatch_due should fire nothing.
    reset_calls();
    int fired = sched.dispatch_due(0, 100, [](const NoteCmd& cmd, uint32_t offset) {
        (void)cmd; (void)offset;
        s_call_count++;
    });
    TEST_ASSERT(fired == 0, "clear: dispatch after clear fires 0");

    test_pass();
}

// ---------------------------------------------------------------------------
// Test 6: dispatched events are removed; same window fires nothing twice.
// ---------------------------------------------------------------------------
static void test_removal() {
    test_begin("scheduler: dispatched events are removed");

    Scheduler<64> sched;

    NoteCmd n{NoteCmd::kNoteOn, 60, 100};
    sched.schedule(50, n);
    sched.schedule(75, n);

    // First dispatch over [0, 128): both fire.
    reset_calls();
    int fired1 = sched.dispatch_due(0, 128, [](const NoteCmd& cmd, uint32_t offset) {
        (void)cmd; (void)offset;
        s_call_count++;
    });
    TEST_ASSERT(fired1 == 2, "removal: first dispatch fires 2");
    TEST_ASSERT(sched.pending() == 0, "removal: 0 pending after first dispatch");

    // Second dispatch over the SAME window: fires nothing.
    reset_calls();
    int fired2 = sched.dispatch_due(0, 128, [](const NoteCmd& cmd, uint32_t offset) {
        (void)cmd; (void)offset;
        s_call_count++;
    });
    TEST_ASSERT(fired2 == 0, "removal: second dispatch fires 0");

    test_pass();
}

// ---------------------------------------------------------------------------
// Suite entry point — called from main.cpp
// ---------------------------------------------------------------------------
void test_scheduler_suite() {
    printf("--- scheduler ---\n");
    test_basic_dispatch();
    test_ordering();
    test_late_events();
    test_capacity();
    test_clear();
    test_removal();
}
