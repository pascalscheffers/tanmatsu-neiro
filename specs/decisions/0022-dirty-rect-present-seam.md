# ADR 0022 — Dirty-region present seam: full-width scanline band

**Status:** accepted (2026-07-06). Resolves the **G6 kickoff gate** of
[Stage 6](../stages/stage-6-display-foundation.md) (WS3 dirty-rect blit). Companion to
**ADR 0007** (the HAL membrane / `platform/platform.h`), **ADR 0011** (the device gets the
optimal representation, the host pays the conversion tax), and **ADR 0015** (how we spend the
audio-block budget). Grounds the open poly-crackle handoff in `specs/MEMORY.md`.

## Context

Today every UI repaint pushes the **entire** 800×480 framebuffer to the panel:
`platform_present()` calls `bsp_display_blit(0, 0, s_h_res, s_v_res, …)` — a ~768 KB–1.15 MB
DMA **read out of PSRAM**. The open poly-crackle handoff names that full-screen blit as one of
two audio-side crackle levers: the PSRAM→panel transfer steals memory bandwidth from the audio
core, so a redraw during a note-on burst pushes per-block render time past its deadline
(`over` spikes). WS3 (this stage) exists to make a param nudge / step tick move kilobytes, not a
megabyte. That requires a **partial present**, which changes the HAL seam — hence the gate.

The gate posed two coupled questions: (1) what shape does the dirty region take / how does the
present seam express it, and (2) how much of the draw path changes. Pascal delegated the call to
Opus (2026-07-06); this ADR is the ratification.

## Decision

### 1. Dirty-region model — a full-width horizontal scanline band `[y0, y1)`

The UI coalesces everything that changed since the last present into a **single band**: a
`[y0, y1)` scanline range spanning the full 800-px width. The present seam takes that band; a
full-screen present is just the band `[0, V_RES)`.

Why a band and not an arbitrary rect or a platform-side diff:

- **Zero-copy on device.** `bsp_display_blit` on tanmatsu forwards straight to
  `esp_lcd_panel_draw_bitmap`, which wants a **packed** source bitmap. A full-width band of the
  800-wide framebuffer is *already contiguous in memory*, so the band present is a pure pointer
  offset — no scratch buffer, no per-row stride copy:
  `bsp_display_blit(0, y0, W, y1, (uint8_t*)pixels + (size_t)y0 * W * bytes_per_px)`.
- An **arbitrary `(x,y,w,h)` rect** is sub-width → its rows are *not* contiguous in an 800-wide
  buffer → the device would have to pack rows into a scratch buffer every present, for marginal
  gain given our layout. Rejected.
- A **platform-side framebuffer diff** (keep a shadow copy, diff each present, blit changed
  spans) needs no UI change, but costs a second ~768 KB PSRAM framebuffer *and* a full-buffer
  scan every present — it *adds* the very PSRAM traffic WS3 exists to cut. Rejected.

Our UI is vertically stacked chrome — tab strip (top), param rows (middle), status bar (bottom)
— so a coalesced band is the natural, cheapest, and correct shape. When two far-apart regions
change in the same frame (e.g. a top tab and the bottom status), they coalesce into one taller
band; that is still far less than a full-screen blit, and correctness is unconditional.

**UI API** (new, small — `ui/ui_dirty.{h,cpp}`, or inline in `ui.cpp` if it stays <~40 lines):

- `void ui_invalidate(int y0, int y1)` — union into the pending band (`min y0` / `max y1`).
- `bool ui_dirty_take(int* y0, int* y1)` — return the coalesced band and clear it; `false` if
  nothing is pending.
- `void ui_invalidate_all(void)` — mark the full screen `[0, V_RES)`.

**Present seam** (`platform/platform.h`): `void platform_present(void)` →
`void platform_present(int y0, int y1)`. It is an internal HAL seam with exactly two callers, so
both backends and all call sites (`app/app.c`, the device `bench_screen`) change in the same
commit — no compat shim. Host honours the band by converting only rows `[y0, y1)` to ARGB and
`SDL_UpdateTexture`-ing that sub-rect (ADR 0011 — the host may legitimately convert the band and
present; it just must not read outside it). Both backends clamp `0 ≤ y0 < y1 ≤ V_RES`.

### 2. Draw model — keep the full redraw; blit only the dirty band

`ui_draw` **keeps** its full-clear + full-repaint of the framebuffer. 6a changes only the
**blit** (the PSRAM→panel DMA — the named lever), narrowing it to the dirty band. Because
nothing outside the changed widget differs from the previous frame, the panel's un-blitted
regions already show the correct pixels, so a band blit is visually exact — **zero stale-pixel
risk**. This is the right altitude for a foundation stage: the framebuffer is always fully
correct, and the audio-bandwidth win (the whole point) lands with a minimal, low-risk change.

Incremental draw — dropping the full clear and repainting only invalidated widgets — would also
cut the draw-side (core-0 PSRAM *write*) traffic, but every mutation path would have to invalidate
precisely or leak stale pixels: a much larger, error-prone `ui.cpp` refactor. It is deferred to a
later, **profiled** optimization, taken only if draw CPU turns out to be a measured lever after
the blit is fixed.

## Consequences

- The HAL present seam gains a `(y0, y1)` band; the full-screen present is `platform_present(0,
  V_RES)`. Two backends + two call sites move together — a one-commit seam change.
- The common case (nudge one param, tick one step) blits one row-band instead of the whole
  screen — the intended PSRAM-bandwidth reduction. This is a **measured** win: 6a records
  before/after blit size + block-time `over` count in `MEMORY.md` and the spec-02 budget (ADR
  0015's "every spend measured" rule); it is not assumed.
- Every UI mutation must invalidate its band (or `ui_invalidate_all` as the safe fallback for
  rare whole-screen events: first frame, page/tab change, key-guide overlay, preset-page
  enter/exit). A missed *band* invalidate shows as a stale sub-region, not a crash — and the
  full-redraw model means the framebuffer content is still correct, only the transfer was skipped.
- **Draw-side** PSRAM-write traffic is unchanged this round; incremental draw remains available
  as a future optimization behind the same `ui_invalidate` API without a seam change.
- `SYNTH_PROFILE` (brackets draw vs present) and `SYNTH_FREEZE_DISPLAY` continue to work
  unchanged; the profiler now measures the band present.
