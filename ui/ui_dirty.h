// ui_dirty.h — dirty-region coalescer (ADR 0022 / Stage 6a WS3).
//
// Portable: no esp_/bsp_/SDL/pax_ — lives above the platform membrane (ADR
// 0007) same as the rest of ui/. Tracks a single pending full-width scanline
// band [y0, y1) that the control loop unions into and the render path drains
// once per frame, so platform_present() can blit only the rows that changed
// instead of the whole 800x480 framebuffer.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Union [y0, y1) into the pending dirty band. Safe to call from the control
// loop any number of times per frame; bands coalesce (min y0, max y1).
void ui_invalidate(int y0, int y1);

// Mark the whole screen dirty (page/tab change, key-guide toggle, first
// frame, or any other whole-screen event). The render side clamps y1 to the
// real framebuffer height.
void ui_invalidate_all(void);

// Return the coalesced pending band in *y0/*y1 and clear it. Returns false
// (leaving *y0/*y1 untouched) if nothing is pending.
//
// ui_invalidate runs on the control task, ui_dirty_take on the render task.
// Backed by a std::atomic<uint32_t> word: ui_dirty_take does an atomic exchange
// (take + clear in one indivisible step) and ui_invalidate does a
// compare-exchange union loop, so a union and a take can never interleave
// into a lost update (ADR 0023) — a union either lands fully before or
// fully after a concurrent take.
bool ui_dirty_take(int* y0, int* y1);

#ifdef __cplusplus
}
#endif
