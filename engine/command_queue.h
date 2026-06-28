// engine/command_queue.h — lock-free note-command ring (control → audio).
//
// The control thread (UI/MIDI, core 0) and the audio thread (render, core 1)
// must never touch voice state at the same time. Instead of a mutex (forbidden
// in the audio path — CLAUDE.md RT rule #2), note events cross the boundary
// through this single-producer/single-consumer lock-free ring: the control
// thread push()es commands; synth_render() drains them with pop() at the top of
// the block, so voice allocation is mutated only on the audio thread.
//
// Pure C++ (std::atomic only) so it is host-testable and reusable. The struct
// lives in DRAM/.bss (never flash .rodata) so it stays reachable during a flash
// write (ADR 0013); pop() inlines into the IRAM render path with no libcalls.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// One control event destined for the voice allocator. Kept tiny + trivially
// copyable so push/pop are plain word stores.
struct NoteCmd {
    enum Type : uint8_t { kNoteOn = 0, kNoteOff = 1 };
    uint8_t type;
    uint8_t pitch;
    uint8_t velocity;  // unused for note_off
};

// Single-producer (control), single-consumer (audio) lock-free ring. Holds up
// to Cap-1 commands (one slot stays empty to tell full from empty). Cap must be
// a power of two. No locks, no allocation, wait-free on both sides.
template <size_t Cap>
class CommandQueue {
    static_assert(Cap >= 2 && (Cap & (Cap - 1)) == 0,
                  "Cap must be a power of two >= 2");

public:
    // Producer side (control thread). False = full → command dropped. At UI/MIDI
    // event rates against ~750 audio blocks/s this never fills in practice.
    bool push(const NoteCmd& cmd) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Cap - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buf_[head] = cmd;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side (audio thread). False = empty.
    bool pop(NoteCmd& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = buf_[tail];
        tail_.store((tail + 1) & (Cap - 1), std::memory_order_release);
        return true;
    }

private:
    NoteCmd             buf_[Cap];
    std::atomic<size_t> head_{0};  // written by producer only
    std::atomic<size_t> tail_{0};  // written by consumer only
};
