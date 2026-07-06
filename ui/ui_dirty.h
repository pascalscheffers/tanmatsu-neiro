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
// Single-writer-per-field pattern (ui_invalidate from the control task,
// ui_dirty_take from the render task) via one volatile aligned 32-bit word —
// same rationale as UIState.change_seq (ui.h): atomic on RV32, and a race
// between a union write and a take-clear is benign (see app.c render_cb):
// the failure mode is "present more, never stale."
bool ui_dirty_take(int* y0, int* y1);

#ifdef __cplusplus
}
#endif
