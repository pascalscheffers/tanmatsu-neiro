// engine/spsc_ring.h — generic lock-free single-producer/single-consumer ring.
//
// The original CommandQueue was note-event-specific. This is the same algorithm
// templated on the payload type T so the param store (Stage 2a) can reuse it
// without a second implementation (Prime Directive 2). See command_queue.h for
// the NoteCmd-specific backward-compat shim.
//
// Properties:
//   - Wait-free on both sides (no CAS, no spin).
//   - Holds Cap-1 items (one slot stays empty to distinguish full from empty).
//   - Cap must be a power of two >= 2.
//   - T must be trivially copyable (plain word stores in push/pop).
//   - Lives in DRAM/.bss so it is reachable during a flash write (ADR 0013).
#pragma once

#include <atomic>
#include <cstddef>

template <typename T, size_t Cap>
class SpscRing {
    static_assert(Cap >= 2 && (Cap & (Cap - 1)) == 0, "Cap must be a power of two >= 2");

public:
    // Producer (control thread). Returns false if full — command dropped.
    bool push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Cap - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buf_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (audio thread). Returns false if empty.
    bool pop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = buf_[tail];
        tail_.store((tail + 1) & (Cap - 1), std::memory_order_release);
        return true;
    }

    // Consumer (audio thread). Non-destructive: copies the element the next
    // pop() would return into out, without advancing tail_. Returns false if
    // empty. Safe to call from the same single consumer as pop() (no extra
    // synchronization needed — same thread, same ordering as pop()).
    bool peek(T& out) const {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = buf_[tail];
        return true;
    }

private:
    T                   buf_[Cap];
    std::atomic<size_t> head_{0};  // written by producer only
    std::atomic<size_t> tail_{0};  // written by consumer only
};
