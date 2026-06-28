// tests/host/test_command_queue.cpp — lock-free note-command ring tests.
// Single-threaded correctness (FIFO order, full/empty boundaries). The atomic
// ordering itself is exercised on-device; here we pin down the queue semantics.
#include "engine/command_queue.h"
#include "runner.h"

static NoteCmd on(uint8_t pitch) { return NoteCmd{NoteCmd::kNoteOn, pitch, 100}; }

void test_command_queue_suite() {
    printf("--- CommandQueue (lock-free SPSC) ---\n");

    {
        test_begin("empty: pop returns false");
        CommandQueue<4> q;
        NoteCmd out;
        TEST_ASSERT(!q.pop(out), "pop on empty queue must fail");
        test_pass();
    }

    {
        test_begin("FIFO: commands pop in push order");
        CommandQueue<8> q;
        for (uint8_t p = 60; p < 65; p++) TEST_ASSERT(q.push(on(p)), "push");
        for (uint8_t p = 60; p < 65; p++) {
            NoteCmd out;
            TEST_ASSERT(q.pop(out), "pop");
            TEST_ASSERT(out.pitch == p, "FIFO order preserved");
        }
        NoteCmd out;
        TEST_ASSERT(!q.pop(out), "queue empty after draining");
        test_pass();
    }

    {
        test_begin("full: holds Cap-1, drops overflow");
        CommandQueue<4> q;  // 3 usable slots
        TEST_ASSERT(q.push(on(1)), "slot 1");
        TEST_ASSERT(q.push(on(2)), "slot 2");
        TEST_ASSERT(q.push(on(3)), "slot 3");
        TEST_ASSERT(!q.push(on(4)), "4th push must fail (full)");
        test_pass();
    }

    {
        test_begin("wraparound: push/pop past Cap stays FIFO");
        CommandQueue<4> q;
        // Cycle well past capacity to exercise index wrap.
        for (int i = 0; i < 20; i++) {
            TEST_ASSERT(q.push(on((uint8_t)i)), "push");
            NoteCmd out;
            TEST_ASSERT(q.pop(out), "pop");
            TEST_ASSERT(out.pitch == (uint8_t)i, "value survives wrap");
        }
        test_pass();
    }
}
