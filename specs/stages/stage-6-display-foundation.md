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
- **G6 — WS3 dirty-region model + partial-present seam (architecture + CPU-budget). ✅ RESOLVED
  (2026-07-06, [ADR 0022](../decisions/0022-dirty-rect-present-seam.md)).** Ratified: the present
  seam becomes `platform_present(int y0, int y1)` carrying a **full-width horizontal scanline
  band** (zero-copy on device — a band of the 800-wide framebuffer is contiguous; host converts
  only those rows). The UI exposes a coalesced-band API (`ui_invalidate(y0,y1)` /
  `ui_dirty_take` / `ui_invalidate_all`), **not** per-widget rects. Draw model: keep the full
  redraw, blit only the band (zero stale-pixel risk; incremental draw deferred). 6a is now
  authorable.

## Sub-stage decomposition (running order: 6a → 6b)

**Dispatch (ADR 0017 tier+effort grid):**

| Sub-stage | Effort | Worker | Gate |
|---|---|---|---|
| 6a WS3 dirty-rect blit | M | **Sonnet · high** (audio-bandwidth + present-timing correctness — a miss is audible) | G6 |
| 6b `draw_curve` widget | S | **Sonnet · low** (pure, mechanical widget) | — |


**6a — WS3 dirty-rect blit (FOUNDATION). ✅ DONE (2026-07-06).** Replaced the full-screen present
with a coalesced full-width scanline-band present per ADR 0022. Landed in one context (no split
needed): `ui/ui_dirty.{h,cpp}` (new coalescer), `ui/ui.cpp`/`ui.h` chrome-band accessors,
`app/app.c` invalidation wiring at all five `change_seq` sites + band-present in `render_cb`,
`platform/platform.h` seam (`platform_present(int y0,int y1)`), both backends (device: zero-copy
row-offset blit; host: converts + updates only the band). `make host`/`make build`/`make test`
green; membrane clean; flash 0x112700 (near-neutral, as expected — see MEMORY.md). On-device
PSRAM/block-time A/B (the crackle-under-redraw regression check) is Pascal's verification step
(host has no PSRAM lever to measure).

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
