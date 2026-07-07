# ADR 0023 — Band present on a rotated panel: pack-and-blit with full-blit fallback

**Status:** accepted (2026-07-07). **Amends ADR 0022** (dirty-rect present seam), whose
zero-copy premise turned out to be false on the Tanmatsu hardware. Companion to ADR 0011
(device gets the optimal representation; conversions live in the backend).

## Context — why the 6a present corrupted the screen

The 6a dirty-band present (commits 5b32e3a, fa6ebc1, d00623e) produced on-device
corruption: startup painted only part of the screen; up/down navigation updated only a
mid-screen chunk; the preset page misaligned its row separators while scrolling;
left/right page changes (full-screen invalidate) fixed everything. Investigation
(2026-07-07) found the change-detection side (`ui/ui_dirty.cpp`, `app/app.c` invalidate
sites) **correct**; three device-present bugs stacked up:

### RC1 — coordinate-space mismatch (the architectural one)

The ST7701 panel is **portrait-native 480×800**
(`nicolaielectronics__mipi_dsi_abstraction/dsi_panel_nicolaielectronics_st7701.c`:
`H_RES 480`, `V_RES 800`); the BSP default rotation is 270°, so PAX draws the 800×480
landscape UI through orientation `PAX_O_ROT_CW`. PAX's transform (`pax_orientation.c`,
`pax_orient_ccw3_vec2f`) maps **logical (x, y) → raw (480 − y, x)**. Therefore:

- UI logical **y** (what `ui_invalidate` tracks) = raw framebuffer **column** `480 − y`;
- UI logical **x** = raw framebuffer **row**.

`platform_present()` treated the logical y-band as raw *rows* and blitted
`fb + y0*480*bpp`. A raw-row band is physically a **vertical strip** of the visible
landscape screen (at horizontal position ≈ x∈[y0,y1)) — with positionally correct content,
since `ui_draw` fully repaints, which is why the artifact read as "only a middle chunk
updates". ADR 0022's "a full-width band is contiguous → zero-copy pointer offset" premise
is **false on this hardware**: a logical scanline band is a set of raw *columns*, which
are not contiguous.

### RC2 — `bsp_display_blit` argument semantics (upstream doc/impl mismatch)

badge-bsp's `bsp/display.h` documents `bsp_display_blit(x, y, width, height, buffer)`,
but the Tanmatsu implementation (`targets/tanmatsu/badge_bsp_display.c`) forwards all
four straight to `esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, buf)`
— **end-exclusive coordinates**. Commit d00623e trusted the header and passed the band
*height* as the 4th arg, making things worse: whenever `y1−y0 < y0` the DPI driver's copy
loop (`for (y = y_start; y < y_end)`) runs zero iterations and **nothing is blitted**
(the stale selection arrow / status strip). The original 5b32e3a call (`y1` as 4th arg)
was accidentally right. Existing badge apps never hit this because full-screen blits have
`x = y = 0`, where width/height coincide with x_end/y_end. **Upstream flag for Pascal:**
header doc vs tanmatsu impl disagree — candidate for an upstream badge-bsp fix
(spec 07 policy); until then our call site documents the end-coordinate semantics.

### RC3 — first-frame "full" present used logical height

`render_cb`'s forced first full present passed `pax_buf_get_height(fb)` = **480**
(PAX returns logical, orientation-swapped dims), but the raw framebuffer has 800 rows —
only 60 % of the panel was blitted at startup.

### Secondary — `ui_dirty_take` lost-union race

`ui_dirty_take`'s read-then-clear can race a control-task `ui_invalidate` union and drop
a band permanently: the framebuffer stays correct (full repaint) but that panel region is
never re-blitted — the app.c comment claiming a lost union "gets picked up on the next
bump" is wrong. Fix with C11 `atomic_exchange` (take) + CAS loop (union).

## Decision

### 1. The present seam speaks **logical UI coordinates**

`platform_present(y0, y1)` takes logical UI scanlines `[0, 480)`. Each backend maps to
its native layout (ADR 0011: conversions live behind the membrane). The host backend is
already logical — unchanged. All `ui_invalidate` call sites stay as-is. Both backends
clamp the band to the **logical** height, which also fixes RC3: `platform_present(0,
pax_buf_get_height(fb))` is now a legal full-logical band.

### 2. Device: hybrid pack-and-blit, with a full-blit fallback at the breakeven width

In `platform/device/platform_device.c`:

1. **Map** the logical band to a raw column range: `X0 = 480 − y1`, `X1 = 480 − y0`
   (clamped to `[0, s_h_res)`; verify the off-by-one visually on device).
2. **Full-blit fallback:** if `y1 − y0 ≥ 240`, blit the whole framebuffer zero-copy:
   `bsp_display_blit(0, 0, s_h_res, s_v_res, pax_pixels)`. *Breakeven math:* the pack
   path moves `4·w·800·bpp` bytes (pack read + pack write + draw-bitmap read + panel-fb
   write); the full path moves `2·480·800·bpp` (read + write). Equal at `w = 240` — half
   the logical height.
3. **Pack path** otherwise: copy the raw column window into a scratch buffer — for each
   raw row `r` in `[0, 800)`:
   `memcpy(scratch + r*w*bpp, pix + (r*480 + X0)*bpp, w*bpp)` — then
   `bsp_display_blit(X0, 0, X1, s_v_res, scratch)`. Note the **end-coordinate**
   semantics (RC2): args 3/4 are x_end/y_end, not width/height.
4. **Scratch buffers: two**, each `240·800·bpp` (= 576 KB at RGB888), heap-allocated from
   PSRAM once at display init, alternated per present. Double-buffered because the DSI
   abstraction sets `use_dma2d = true`, making `draw_bitmap` **async**: the flush
   semaphore inside `bsp_display_blit` serializes the *next blit*, not our repacking of
   a buffer the in-flight DMA2D may still be reading.

### 3. Rejected / deferred alternatives

- **Track logical-x extents instead** (zero-copy: logical x = raw rows, contiguous):
  UI rows span nearly the full width, so a row change degenerates to a full blit. Rejected.
- **Full dirty rect (x and y)**: would narrow the pack window further (pack only raw rows
  `[x0, x1)`), behind the same `ui_invalidate` API. Deferred — take it only if a profile
  says the band pack is still a measured cost.
- **Draw directly into the panel's internal DPI framebuffer** (blit degenerates to a
  cache `msync`): zero copies, but the full clear+repaint would tear against the 60 Hz
  scanout with `num_fbs = 1`. Not for this stage.

## Consequences

- The corruption modes disappear: the blitted region is the region that changed, on the
  axis it changed on; the first frame paints the full panel.
- A one-param-row nudge (~32 logical px) moves ≈ `4·32·800·3` ≈ **300 KB** of PSRAM
  traffic instead of ≈ 2.3 MB (full-frame read+write) — the PSRAM-bandwidth lever 6a was
  built for, now pointed at the right axis. Still a **measured** win: record before/after
  `PROFILE` present numbers in `MEMORY.md` (ADR 0015 rule).
- +1.15 MB PSRAM (two scratch buffers), allocated once at init — nowhere near the budget
  ceiling; note it in the spec-02 memory table.
- ADR 0022 remains the decision of record for the *seam shape* (coalesced y-band, full
  redraw + narrowed blit); only its zero-copy device mechanism and its "raw rows" reading
  of the band are superseded by this ADR.
