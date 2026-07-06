# Stage 6 — Display foundation: WS3 dirty-rect blit + curve widget

> **Status: pre-runbook campaign brief.** Opus-authored map for the first post-Stage-5
> campaign, derived from `specs/FABLE-THOUGHTS.md` (§3c.1, §6a) and reordered per Pascal's
> WS3-first direction. NOT yet closed work-orders — the Kickoff gate below must be resolved,
> then each sub-stage's closed work-order is authored per `stages/README.md` (ADR 0017).
>
> **Execution unit:** one clean Sonnet 5 context per sub-stage; Pascal clears context between
> and prompts "do the next stage". Each sub-stage is sized to fit one context (README budget:
> ≤~8 files, ≤~5 read-sections, ~one feature).
>
> Grounding: `specs/FABLE-THOUGHTS.md` §3 (spike-flatten, not throughput) + §6 (the screen is
> a superpower, but every visual idea is a full-screen-blit audio risk until WS3 lands). Seams:
> `specs/MAP.md` (`platform/platform.h` present/framebuffer seam, `ui/ui.cpp`). The open
> poly-crackle MEMORY item names the 1.15 MB PSRAM blit as an audio-bandwidth lever.

## Why this stage is first

FABLE's own priority list buries WS3 at #2, but Pascal promoted it to the very front because it
is a **hard prerequisite for all of §6 UI** *and* one of the two known audio-side crackle levers
(the full-screen 1.15 MB PSRAM blit steals the audio core's memory bandwidth). Landing it first
converts every downstream visual feature (§6c instrument view in Stage 7, §6a curve headers +
§6d wayfinding in Stage 8, §6b PERFORM HUD in Stage 9) from "audio risk" to "free". Nothing
visual ships before it.

## Where we are entering Stage 6
- Stages 0.5–5 complete: full Juno voice + mod matrix, clock/arp/FX (Stage 4), MIDI I/O
  (Stage 5). The UI is nine text-list param pages + status strip, rendered from the param table
  (`ui/ui.cpp`, ADR 0008). The last UI overhaul (WO-1…WO-6, see `MEMORY.md`) gave us the
  9-page `PAGE_TABLE`, shape-button F1–F6, preset browser, and the F5 key-guide overlay.
- Display path: `ui/ui.cpp` draws with PAX into `platform_framebuffer()`, pushed by
  `platform_present()` (the 5-seam HAL, `platform/platform.h`). Today every frame blits the
  whole 800×480 buffer — that's the 1.15 MB PSRAM transfer to kill.

## Kickoff gate
- **G6 — WS3 dirty-region model + partial-present seam (architecture + CPU-budget). 🛑 OPEN.**
  Decision: does `platform_present()` gain a dirty-rectangle parameter (partial blit), or does
  the platform diff framebuffers itself? And what is the dirty-region API the UI exposes
  (per-widget invalidation vs a coalesced bounding box)? This changes the HAL seam *and* is the
  whole point of the audio-bandwidth win — Opus/Pascal ratify before 6a is authored. Recommendation:
  add an optional dirty-rect to the present seam (host can ignore it, device honours it) + a
  small `ui_invalidate(rect)` coalescer in `ui/`. Record as an ADR.

## Sub-stage decomposition (running order: 6a → 6b)

**6a — WS3 dirty-rect blit (FOUNDATION).** Replace the full-screen present with small dirty
regions over static chrome. UI tracks invalidated rectangles; `platform_present` blits only
those. *Seams:* `ui/ui.cpp` (+ maybe a small `ui/ui_dirty.*`), `platform/platform.h` present
seam, `platform/device/` + `platform/host/` present impls. *Acceptance:* no full-screen blit on
a param nudge / step tick; device A/B shows reduced PSRAM traffic; **crackle-under-redraw
regression clean** (`PROFILE=1`, redraw while an 8-note chord holds). *One-context fit:* likely,
but **split-if** the dirty-region tracking (ui) and the partial-blit path (both platform
backends) together blow the budget → `6a-i` (dirty-region tracking + `ui_invalidate` in `ui/`)
then `6a-ii` (partial-present in the HAL + both backends). **G6 gates authoring.**

**6b — `draw_curve` shared widget scaffold** (§6a widget only — no page wiring yet). A tiny PAX
polyline helper so later stages draw shapes, not numbers. *Seams:* new `ui/ui_widgets.{h,cpp}`:
`draw_curve(pax_buf, rect, const float* pts, int n, uint32_t accent)`. No page consumes it yet
(that's Stage 8 8b) — this is the pure widget + a host smoke render. *Acceptance:* `make host`/
`make build` green, widget renders a test polyline; membrane clean. *One-context fit:* easily
(S, one new file pair).

## Continuous (every sub-stage)
Track `make size`; host + device both green; **profile before/after** (WS3 is a measured PSRAM
+ block-time win, not an assumed one — record the numbers in `MEMORY.md` + the spec 02 budget).
Membrane clean (no `esp_`/`bsp_`/`SDL` above `platform/`).

## First action at kickoff
Read this brief + the open poly-crackle handoff in `MEMORY.md` + `platform/platform.h` present
seam + `ui/ui.cpp` draw/present path. Resolve **G6** with Pascal, ratify the dirty-rect ADR,
then author the **6a** closed work-order and dispatch a fresh Sonnet worker. 6b can follow
independently (no dependency on 6a's outcome).
