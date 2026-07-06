// ui_dirty.cpp — dirty-region coalescer impl (see ui_dirty.h).
#include "ui_dirty.h"
#include <stdint.h>

// Packed band: hi 16 bits = y0, lo 16 bits = y1. Empty sentinel is y0=0xFFFF,
// y1=0 (y0 >= y1 reads as "nothing pending"). volatile + naturally aligned
// 32-bit word: single instruction load/store on RV32, so a union write racing
// a take-clear either wins outright or loses outright — never a torn value.
#define EMPTY_BAND 0xFFFF0000u

static volatile uint32_t s_band = EMPTY_BAND;

extern "C" void ui_invalidate(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y0 > 0xFFFF) y0 = 0xFFFF;
    if (y1 < 0) y1 = 0;
    if (y1 > 0xFFFF) y1 = 0xFFFF;

    uint32_t cur = s_band;
    uint32_t cy0 = cur >> 16;
    uint32_t cy1 = cur & 0xFFFFu;
    uint32_t ny0 = (uint32_t)y0 < cy0 ? (uint32_t)y0 : cy0;
    uint32_t ny1 = (uint32_t)y1 > cy1 ? (uint32_t)y1 : cy1;
    s_band       = (ny0 << 16) | ny1;
}

extern "C" void ui_invalidate_all(void) {
    s_band = (0u << 16) | 0xFFFFu;
}

extern "C" bool ui_dirty_take(int* y0, int* y1) {
    uint32_t cur = s_band;
    s_band       = EMPTY_BAND;

    uint32_t hi = cur >> 16;
    uint32_t lo = cur & 0xFFFFu;
    if (hi >= lo) return false;  // nothing pending

    *y0 = (int)hi;
    *y1 = (int)lo;
    return true;
}
