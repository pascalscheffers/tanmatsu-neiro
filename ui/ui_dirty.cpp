// ui_dirty.cpp — dirty-region coalescer impl (see ui_dirty.h).
#include "ui_dirty.h"
#include <stdint.h>
#include <atomic>

// Packed band: hi 16 bits = y0, lo 16 bits = y1. Empty sentinel is y0=0xFFFF,
// y1=0 (y0 >= y1 reads as "nothing pending"). A plain volatile 32-bit word
// used to back this (single instruction load/store on RV32) but that only
// prevents torn reads/writes, not a lost update: a control-task
// ui_invalidate union and a render-task ui_dirty_take can interleave as
// read-cur / read-cur / write-take / write-union, silently dropping the
// take's clear (benign) or the union's band (not benign — see ui_dirty_take
// below). C++ std::atomic with explicit exchange/CAS closes that race.
#define EMPTY_BAND 0xFFFF0000u

static std::atomic<uint32_t> s_band{EMPTY_BAND};

extern "C" void ui_invalidate(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y0 > 0xFFFF) y0 = 0xFFFF;
    if (y1 < 0) y1 = 0;
    if (y1 > 0xFFFF) y1 = 0xFFFF;

    // CAS loop: union the new [y0,y1) into whatever the band currently is,
    // retrying if another writer (or ui_dirty_take's exchange) changed it
    // out from under us between the load and the store.
    uint32_t cur = s_band.load();
    for (;;) {
        uint32_t cy0  = cur >> 16;
        uint32_t cy1  = cur & 0xFFFFu;
        uint32_t ny0  = (uint32_t)y0 < cy0 ? (uint32_t)y0 : cy0;
        uint32_t ny1  = (uint32_t)y1 > cy1 ? (uint32_t)y1 : cy1;
        uint32_t next = (ny0 << 16) | ny1;
        if (s_band.compare_exchange_weak(cur, next)) break;
        // cur was updated to the current value by the failed CAS; loop and
        // recompute the union against it.
    }
}

extern "C" void ui_invalidate_all(void) {
    // No union needed — [0, 0xFFFF) already covers any pending band, so a
    // plain store is safe: it can only ever widen what's pending, never
    // narrow it, regardless of what a concurrent ui_invalidate races in.
    s_band.store((0u << 16) | 0xFFFFu);
}

extern "C" bool ui_dirty_take(int* y0, int* y1) {
    // Atomic exchange: whatever band is present at this instant is taken in
    // full and the slot is reset to empty in the same indivisible step, so a
    // concurrent ui_invalidate union either lands entirely before this
    // exchange (and gets taken now) or entirely after it (and is picked up
    // next frame) — never lost in between.
    uint32_t cur = s_band.exchange(EMPTY_BAND);

    uint32_t hi = cur >> 16;
    uint32_t lo = cur & 0xFFFFu;
    if (hi >= lo) return false;  // nothing pending

    *y0 = (int)hi;
    *y1 = (int)lo;
    return true;
}
