# PAX rendering performance — the three levers

Findings from a read of vendored PAX v2.1.0 (`managed_components/robotman2412__pax-gfx/`,
pinned `607e004`) on 2026-06-28. PAX is a well-architected *software* rasterizer running
in its most conservative mode on the ESP32-P4: one core, no PPA, full-frame present.
None of that hurts the Stage 0 hello screen — it's headroom for the live-tweak UI
(Stage 2+). Three levers, in rough ROI order. **Profile before committing to any** —
CLAUDE.md "profile before optimizing" + the bench harness already exists (`make
bench-device`).

## Lever 1 — present only the dirty rectangle (our side; biggest, cheapest win)
- **Now:** `app.c:96-99` redraws and presents the **whole** 800×480×24bpp (1.15 MB)
  buffer every 16 ms (~60 Hz) regardless of what changed — ~69 MB/s of present
  bandwidth; the host path also does a per-pixel 24bpp→ARGB8888 convert each frame
  (`platform/host/`).
- **The data already exists.** PAX tracks a dirty bounding box in `pax_buf_t`
  (`pax_get_dirty` / `pax_mark_dirty*`, populated by every `pax_dispatch_*` in
  `core/src/pax_renderer.c`). Nothing reads it today.
- **Change:** in our present path (`platform/device` + `platform/host`), query
  `pax_get_dirty`, blit only that region, then clear the dirty bounds. Pure win, no PAX
  internals touched. A PAX-side partial-blit helper *may* be a small upstream nicety, but
  the core change is ours.
- **ROI:** highest. Most UI frames touch a tiny region (a moving bar, one value).

## Lever 2 — activate the multicore renderer (our side; ~1 call)
- **Now:** the static default engine is the **synchronous, single-core** software
  renderer (`pax_renderer.c:49-50`, `pax_render_engine_soft`). The dual-core async
  renderer **is compiled in** (`CONFIG_PAX_COMPILE_ASYNC_RENDERER` defaults `2`,
  `pax_config.h:56-70`) but only activates when something calls `pax_set_renderer(...)`.
  **Nothing in the synth, `platform/`, or `badge-bsp` does** → the second RISC-V core is
  idle for graphics.
- **Change:** one `pax_set_renderer(&pax_render_engine_softasync, …)` at UI init. Model:
  the draw task is pushed to **both** worker queues; each core renders alternating
  scanlines (odd/even); barrier at `pax_join()`. Not load-balanced per draw call — both
  cores walk every shape, speedup is from halving the rows.
- **Caveat:** the audio path owns a core and is sacred (CLAUDE.md RT rules). Verify the
  PAX worker task priorities/affinity don't contend with audio before enabling. Profile
  the actual win and the jitter cost.
- **ROI:** medium, low effort — but gated on the audio-core interaction.

## Lever 3 — wire the ESP32-P4 PPA / 2D-DMA (upstream PAX PR)
- **Now: dormant.** `CONFIG_PAX_COMPILE_ESP32P4_PPA_RENDERER` defaults **true** on P4
  (`pax_config.h:46-54`) and buffers are even 64-byte aligned for the PPA DMA engine
  (comment in `pax_gfx.c`), **but**: the `esp_driver_ppa` dependency and the esp32p4
  branch are **commented out** in the top-level `CMakeLists.txt`, and there is **no PPA
  renderer source file** in the tree. The config slot is a placeholder. So all
  fills/blits are CPU writes (or `memcpy`), not PPA/2D-DMA. The P4's marquee 2D
  accelerator is unused.
- **Change:** a real PPA render engine (rect fill, blit, format-convert via the PPA;
  framebuffer copies via 2D-DMA), selectable via the existing renderer seam. This is
  upstream-shaped work in someone else's codebase → per `07-upstream-contributions.md`,
  **discuss shape with Pascal/robotman2412 first, bring profile numbers, then PR.** The
  config slot + buffer alignment already being present suggests the author anticipated it.
- **ROI:** potentially large for blit/fill-heavy UI, but the most work and the only
  upstream contribution of the three.

## Also noted (not a lever, context)
- **Fixed-point default** (`CONFIG_PAX_USE_FIXED_POINT` true → 12.20 on RV32, no
  `__int128`) deliberately bypasses the P4 FPU for cross-platform determinism. Whether
  float would be faster in the rasterizer is a profile question, not a given.
- **Fonts** are baked C arrays (~240 KB across 5 fonts) but unreferenced ones are dropped
  by the linker (`CONFIG_PAX_COMPILE_FONT_INDEX` defaults false); the synth links only
  the font(s) it names. Flash is well-contained — leave it.
- **GUI layer** (`gui/`) is unused by the synth (`EXCLUDE_FROM_ALL` on host) — retained-
  mode, allocation-heavy; revisit only if we adopt it.

See also: `07-upstream-contributions.md` (PAX perf candidate, upstream policy),
ADR 0011 (optimize-device-host-adapts), ADR 0015 (spending-cpu-headroom).
