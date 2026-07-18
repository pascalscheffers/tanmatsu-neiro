// tests/host/test_note_cap.cpp — direct note-on spacing regression.
//
// synth_render()'s direct-path drain loop (engine/synth.cpp) is not itself
// host-testable in isolation (it lives inside the IRAM render function, wired
// to the real voice allocator/arp/scheduler). Per the work order, this test
// instead exercises the two building blocks the cap is built from:
//   1. SpscRing::peek() — non-destructive lookahead, used to decide whether
//      the *next* queued command is a kNoteOn before committing to pop it.
//   2. A drain helper that mirrors the break-and-leave admission logic
//      in synth.cpp's arp_on==false branch, byte-for-byte, over the same
//      CommandQueue<Cap> type. If synth.cpp's loop and this helper ever
//      diverge, that's a signal to keep them in sync.
#include <vector>
#include "engine/command_queue.h"
#include "engine/synth_config.h"
#include "runner.h"

namespace {

// Mirrors engine/synth.cpp's arp_on==false drain loop: starts at most one
// note every kNoteOnStartIntervalBlocks render blocks. Returns counts applied
// in this block.
struct DrainResult {
    int note_ons  = 0;
    int note_offs = 0;
};

template <size_t Cap>
DrainResult drain_capped(CommandQueue<Cap>& q, int& cooldown, std::vector<uint8_t>& on_log,
                         std::vector<uint8_t>& off_log) {
    if (cooldown > 0) cooldown--;
    DrainResult result;
    NoteCmd     cmd;
    while (true) {
        NoteCmd next;
        if (q.peek(next) && next.type == NoteCmd::kNoteOn && (result.note_ons > 0 || cooldown > 0)) {
            break;
        }
        if (!q.pop(cmd)) break;
        if (cmd.type == NoteCmd::kNoteOn) {
            on_log.push_back(cmd.pitch);
            result.note_ons++;
            cooldown = kNoteOnStartIntervalBlocks;
        } else {
            off_log.push_back(cmd.pitch);
            result.note_offs++;
        }
    }
    return result;
}

NoteCmd on(uint8_t pitch) {
    return NoteCmd{NoteCmd::kNoteOn, pitch, 100};
}

NoteCmd off(uint8_t pitch) {
    return NoteCmd{NoteCmd::kNoteOff, pitch, 0};
}

}  // namespace

void test_note_cap_suite() {
    printf("--- Direct note-on spacing (Stage 8) ---\n");

    {
        test_begin("peek: empty ring returns false, doesn't touch out");
        CommandQueue<16> q;
        NoteCmd          out{99, 99, 99};
        TEST_ASSERT(!q.peek(out), "peek on empty must fail");
        test_pass();
    }

    {
        test_begin("peek: non-destructive, repeated peek matches next pop");
        CommandQueue<16> q;
        q.push(on(60));
        q.push(on(61));
        NoteCmd a, b, popped;
        TEST_ASSERT(q.peek(a), "peek 1");
        TEST_ASSERT(q.peek(b), "peek 2 (idempotent)");
        TEST_ASSERT(a.pitch == 60 && b.pitch == 60, "peek doesn't advance");
        TEST_ASSERT(q.pop(popped), "pop after peek");
        TEST_ASSERT(popped.pitch == 60, "pop returns what was peeked");
        NoteCmd c;
        TEST_ASSERT(q.peek(c) && c.pitch == 61, "peek now sees next element");
        test_pass();
    }

    {
        test_begin("chord: 8 starts spaced kNoteOnStartIntervalBlocks apart, none dropped");
        CommandQueue<16> q;
        for (uint8_t p = 60; p < 68; p++) TEST_ASSERT(q.push(on(p)), "push note-on");

        std::vector<uint8_t> on_log, off_log;
        int                  cooldown = 0;
        int                  blocks   = 0;
        int                  starts[8];
        while ((int)on_log.size() < 8) {
            DrainResult r = drain_capped(q, cooldown, on_log, off_log);
            TEST_ASSERT(r.note_ons <= 1, "at most one start per block");
            if (r.note_ons == 1) starts[on_log.size() - 1] = blocks;
            blocks++;
            TEST_ASSERT(blocks < 100, "sanity: must terminate");
        }
        TEST_ASSERT((int)on_log.size() == 8, "all 8 note-ons eventually admitted");
        for (uint8_t p = 60; p < 68; p++) {
            bool found = false;
            for (uint8_t seen : on_log) found |= (seen == p);
            TEST_ASSERT(found, "each pitch admitted exactly once");
        }
        for (int i = 1; i < 8; i++) {
            TEST_ASSERT(starts[i] - starts[i - 1] == kNoteOnStartIntervalBlocks, "start interval is exact");
        }
        TEST_ASSERT(starts[7] == 7 * kNoteOnStartIntervalBlocks, "eighth note starts after 7 intervals");
        test_pass();
    }

    {
        test_begin("burst: note-ons + note-offs interleaved, nothing lost");
        CommandQueue<32> q;
        // 8 note-ons interleaved with 4 note-offs behind the cap.
        for (uint8_t p = 60; p < 68; p++) {
            TEST_ASSERT(q.push(on(p)), "push note-on");
            if (p % 2 == 0) TEST_ASSERT(q.push(off((uint8_t)(p - 1))), "push note-off");
        }

        std::vector<uint8_t> on_log, off_log;
        int                  cooldown = 0;
        int                  blocks   = 0;
        while ((int)on_log.size() < 8 || (int)off_log.size() < 4) {
            DrainResult r = drain_capped(q, cooldown, on_log, off_log);
            TEST_ASSERT(r.note_ons <= 1, "at most one start per block");
            blocks++;
            TEST_ASSERT(blocks < 100, "sanity: must terminate");
        }
        TEST_ASSERT((int)on_log.size() == 8, "all note-ons consumed");
        TEST_ASSERT((int)off_log.size() == 4, "all note-offs consumed");
        test_pass();
    }
}
