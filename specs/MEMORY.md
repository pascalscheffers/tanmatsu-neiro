# Progress Log

The **live** log: recent entries + open gates. Older history is in
[`MEMORY-archive.md`](MEMORY-archive.md). One entry per dispatched job; **append new entries
just above the "Open Opus gates" section** (which stays last). Lean — link to specs, don't
restate. When this passes ~200 lines, rotate older entries into the archive.


## 2026-07-16 — Master-bus DC blocker (waveform-asymmetry fix, COMPLETE)

Acted on the "add output DC-blocker" lead from the tap.wav forensics below (rendered wave
bottom-heavy, mean DC −0.224, envelope-proportional). Commit `ef5233d`.

- **`dsp/dcblock.h`** (new): thin wrap of `daisysp::DcBlock` (reuse, Prime Directive 1) —
  leaky-differentiator HPF, corner ~1.6 Hz (`gain = 1 − 10/Fs`). Adds `+1e-20f` denormal
  guard on input (ADR 0012: P4 has no FTZ; one-pole feedback decays toward denormal on
  silence). Bias is itself removed by the blocker, never reaches output.
- **`engine/synth.cpp`**: two per-channel statics `s_dc_l/s_dc_r`, `init` in `synth_init`,
  `process` **post-gain, pre-limiter** in both chorus branches. Order is deliberate — block
  *before* limiter peak-detect + `soft_clip` so both see a symmetric wave (recovers headroom,
  kills asymmetric clip). Chorus-off path uses `s_dc_l` for both channels.
- **`tests/host/test_dcblock.cpp`** (new, 4 cases): DC offset → mean→0; 1 kHz passband unity;
  −0.224-offset 500 Hz sine re-centred (settle ~10τ before measuring); silence stays finite.
- Added `dcblock.cpp` to all three builds (main/host/tests CMake).

`make test` ✅ (+4 cases) `make host` ✅ `make build` ✅ `make format` ✅.
Flash 0x112ba0 (46% free), DIRAM 174 KB.

**NEXT:** (1) on-device / line-out listen — confirm DC-block reduces crackle + no bass loss;
(2) still open: chase *why* render is DC-biased (osc waveshape / sub / filter) — blocker is a
guard, not the root cause. See [[crackle-voice-leak-open]].

## 2026-07-16 — tap.wav analyzed: crackle-in-tap is CAPTURE ARTIFACT; waveform ASYMMETRY is the real lead

Analyzed `tap.wav` (on-device SPACE-freeze dump) vs line recordings (`gain50.wav`,
`untitled.wav`) + `sniff.log`. Two separate signatures — do not conflate.

- **tap.wav "crackle" = dropped `[TAP]` serial lines zero-filled** (the `cb85741` design,
  working as intended). Fingerprint: signal snaps to **exact 0.0** in ~8-sample runs,
  **periodic ~145 samples**, ~212 events in 0.68 s, full-amplitude on both sides of each
  hole (`+0.000 → −0.779`). No fades, no rail flat-tops. tap.wav playback DOES crackle
  (Pascal confirmed) but those clicks are the **zero-holes**, not the DAC path. sniff.log
  shows the same USB-serial-JTAG link mangling PROFILE lines mid-word, so ~% of `[TAP]`
  lines still drop. **⇒ tap.wav cannot be used for the render-vs-line-out diff while drops
  persist.** To get a trustworthy on-device tap: **binary dump to SD**, or CRC/len-check
  each serial line (reject, don't zero-fill). Recapture-over-serial won't fix it.
- **Waveform ASYMMETRY is REAL** (lives in the non-zero = genuine render samples). tap.wav
  is bottom-heavy: mean **DC −0.224**, envelope-proportional (loud win −0.43 @ peak 0.80,
  quiet win −0.06 @ peak 0.25), swings ≈ ±0.57 around a *moving* negative center. Hardware
  designer flags the asymmetry as **probably not good**. No DC-blocker on the output path.
  **New lead** — investigate why the rendered wave is asymmetric (osc shape? unipolar env/
  mod? missing bipolar centering?) and add an **output DC-blocker**; asymmetry may itself be
  the signal-domain defect (biased wave hits one rail first under gain).
- **Line recordings = the honest crackle evidence**, distinct signature: positive **full-
  scale clip +0.99997**, non-periodic (gain50 79 steps, untitled 346; gain24 clean, peak
  0.21). Matches earlier step-discontinuity finding.
- **sniff.log profiler CLEAN** during capture: `audio avg=89–95µs budget=1333 over=0/750`,
  voices=1, no overruns in 3054 lines. No CPU starvation.

**NEXT:** (1) trustworthy tap = binary-to-SD or line-checksummed serial; (2) chase waveform
asymmetry + add output DC-blocker; (3) keep line-out clip (+FS) as the crackle ground truth.
Do NOT re-apply instant-attack limiter. See [[crackle-voice-leak-open]].

## 2026-07-16 — Manual tap freeze WORKS on device; drop-robust dump (crackle split, IN PROGRESS)

Goal: the decisive crackle split — is the glitch in the **rendered buffer** (DSP) or only
**after** it (codec/i2s/analog)? The auto step-trigger (`>0.6`) never fires, so the tap needed a
human-in-the-loop freeze. Now shipped and confirmed firing on hardware.

- **Manual freeze / re-arm** (`600d11a` feat, `1918a98` fix): SPACE (PROFILE builds only) freezes
  the RAM tap; tiny post-trigger tail ⇒ the frozen ring is ~all **pre-keypress** history (682 ms,
  ring doubled to 128 KiB). Auto re-arm after each dump = repeat captures, no reboot. **Bug found +
  fixed:** device `scancode_to_key()` (`platform_device.c`) is a whitelist of musical/UI keys only —
  SPACE fell through to 0 and the event became `NONE` before app.c; added `BSP_INPUT_SCANCODE_SPACE
  → ' '`. Worker had checked keyboard.c/ui.cpp but not the device scancode layer.
- **Starve counter ripped** (`a5ef58b`): underrun hypothesis dead (frozen-display run), counter was
  a broken heuristic (const ~550 at idle). `platform_audio_profile_read` back to 4 out-params.
- **Drop-robust dump** (`cb85741`): first real capture dropped ~6% of `[TAP]` serial lines; the old
  decoder concatenated chunks in order so a **mid-stream drop shifted every later sample** and killed
  alignment. Now each data line is **offset-stamped** (`[TAP] d <off> <b64>`); `tap2wav.py` places
  each chunk at its byte offset and **zero-fills holes at the correct position** (reports gap
  frame-ranges) — alignment survives drops. Dump now sleeps every 16 lines (was 32) to cut the rate.
  `tap2wav --selftest` gains a mid-stream-drop alignment assertion. ✅

**Verify:** `make PROFILE=1 build` ✅ (DIRAM ~306 KB/53%, ~264 KB free) `tap2wav --selftest` ✅.
Freeze confirmed firing on device (Pascal got a `tap.wav`; that capture predates the offset fix so
it's misaligned — recapture with `cb85741`).

**NEXT (Pascal, decisive):** `make install run PROFILE=1` → `make sniff` → play till crackle →
**tap SPACE** → wait `[TAP] end` → `python3 tools/tap2wav.py sniff.log -o tap.wav`, while recording
line-out simultaneously. Diff rendered vs line-out: **rendered clean + line-out crackles ⇒
codec/i2s/analog** (never audited); **rendered also crackles ⇒ DSP glitch at clean timing**, localize
in code. See memory note [[crackle-voice-leak-open]].

## 2026-07-10 — Stage 8: SYNTH_PROFILE audio RAM tap (crackle forensics ground truth)

Added a one-shot RAM tap of the rendered stereo output, `SYNTH_PROFILE`-only, to settle whether
the smash-crackle is amplitude clipping at render time or a DMA/codec-side fault — pure
instrumentation per the "instrument, don't guess" rule (see memory note on RT-spike diagnosis).

- **`engine/synth.cpp`**: `s_tap_ring[16384*2]` int16 (64 KiB, interleaved L,R, ~341 ms @ 48 kHz)
  captures the exact post-`soft_clip` signal (both the stereo-chorus and mono step-6 paths) via
  `tap_capture()` — branch-light (two clamp+scale conversions, one masked index increment, one
  compare), no allocation/locks/calls. Trigger: peak fed to the limiter (`postgain`/`fabsf(m)`)
  exceeds `kTapTriggerLevel = 1.2f` while unarmed → latch, capture 8192 more frames (half the
  ring), freeze via `std::atomic<bool> s_tap_frozen` (release store). One-shot per boot; reboot
  re-arms. `engine_tap_frozen()`/`engine_tap_data()` (new, `extern "C"`) are the reader API,
  stubbed to false/zeros/NULL when `SYNTH_PROFILE` is off (mirrors `engine_profile_read`).
- **`engine/synth.h`**: reader declarations + the dump-order contract, documented inline.
  `engine_tap_data()` takes **three** out-params (`out_frames`, `out_trig_frame`,
  `out_start_offset`), not the two in the original sketch — a strict oldest→newest single-pointer
  read would need a second full-ring copy on the audio thread, which the RT rules forbid for a
  diagnostic buffer this size. Instead the reader returns the raw physical ring plus
  `out_start_offset` (physical index of the oldest frame) and `out_trig_frame` as a **logical**
  index already relative to oldest; the caller walks two spans
  (`[start_offset..frames-1]` then `[0..start_offset-1]`) to unroll — done at dump time in
  `app.c`, off the audio thread, so it costs nothing in the RT path.
- **`app/app.c`**: inside the existing 1 s `SYNTH_PROFILE` readout, once `engine_tap_frozen()`
  first goes true, dumps once: `[TAP] hdr sr=48000 ch=2 fmt=s16le frames=16384 trig_frame=<N>`,
  then `[TAP] d <base64-of-48-bytes>` lines (hand-rolled base64 + CRC-32, no ESP-IDF ROM calls —
  app.c is shared host+device), `platform_sleep_ms(2)` every 32 lines (portable stand-in for the
  work order's `vTaskDelay`), then `[TAP] end crc32=<hex>`. Guarded to print once per boot.
- **`tools/tap2wav.py`** (new, stdlib-only): parses a `make sniff` capture for the `[TAP]` lines
  anywhere in the stream, base64-decodes, CRC-verifies (warns, doesn't hard-fail — serial can drop
  lines), writes a 48 kHz/stereo/16-bit WAV, prints the trigger frame + its ms offset.
  `--selftest` round-trips a synthetic capture (mis-aligned chunk size on purpose) end to end.

**Runbook:** `make PROFILE=1 build install run`, `make sniff`, smash keys (MASTER_GAIN ~0.5) until
the crackle is audible, wait for `[TAP] end`, then `python3 tools/tap2wav.py sniff.log -o tap.wav`.
**Interpretation:** flattened/saturated onsets in `tap.wav` → the crackle is amplitude clipping at
render time (revisit the limiter/gain-staging fix per the crackle memory note); a clean tap despite
an audibly crackling take → the fault is downstream of render (DMA/codec/I2S side), redirect the
hunt there.

**Verify:** `make host` ✅ `make test` 207/207 ✅ `make build` ✅ (174,006 B DIRAM, 30.19%, +70 B
noise vs. prior 173,936 B baseline) `make PROFILE=1 build` ✅ (240,880 B DIRAM, 41.79%, ~335 KB
free — no overflow) `make format` ✅. `tap2wav.py --selftest` ✅.

**Decisions made that the work-order didn't spell out:** the 3-out-param reader signature (above,
rationale documented in `synth.h`); `platform_sleep_ms` instead of raw `vTaskDelay` for host/device
portability; hand-rolled CRC-32/base64 in `app.c` instead of ESP-IDF ROM functions (same
portability reason). Scope stayed additive-only in `engine/synth.cpp` — the pre-existing
uncommitted Stage 8 note-on refractory diag hunks in that file and in `engine/synth_config.h` were
left untouched (staged separately, out of scope for this commit).

## 2026-07-07 — 6a.1: rotated-panel band-present fix implemented (COMPLETE, resolves the handoff below)

Implemented the closed work-order from ADR 0023 (see the handoff entry directly below for the
full RC1–RC3 + lost-union diagnosis).

- **`platform/device/platform_device.c`**: `platform_present(y0,y1)` now clamps to logical height
  `s_h_res` (not a hardcoded 480), maps the logical band to a raw column range
  `X0=s_h_res-y1, X1=s_h_res-y0`, and either (a) full zero-copy blits
  `bsp_display_blit(0,0,s_h_res,s_v_res,pix)` when the band width `w ≥ BAND_PACK_THRESHOLD`
  (240 logical px — the pack-vs-full traffic breakeven, `w = s_h_res/2`) or the scratch alloc
  failed, or (b) packs the raw column window row-by-row into one of two alternating PSRAM
  scratch buffers and blits the sub-window. Scratch buffers (`240 * s_v_res * s_fb_bpp` = 576 KB
  each, ~1.15 MB total) are `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`'d once in `platform_init`
  right after display params are known; a failed alloc logs `ESP_LOGE` and permanently falls back
  to full-blit-only present (display stays usable, just slower) rather than leaving it dead.
  Every `bsp_display_blit` call site now carries the RC2 end-exclusive-coordinate comment.
- **`ui/ui_dirty.cpp`**: `s_band` changed from a plain `volatile uint32_t` to `std::atomic<uint32_t>`
  (the file is `.cpp`, so C++ `<atomic>` was used instead of C11 `<stdatomic.h>` — `_Atomic` in a
  `.cpp` compiled by g++ doesn't accept `stdatomic.h`'s macros). `ui_dirty_take` does one
  `exchange(EMPTY_BAND)` (take + clear indivisibly); `ui_invalidate` does a `compare_exchange_weak`
  union loop; `ui_invalidate_all` is a single plain `store` (safe without a CAS: `[0,0xFFFF)` is
  already a superset of anything a racing union could produce). Closes the lost-union race named
  in ADR 0023's "Secondary" root cause.
- **`app/app.c`**: `render_cb`'s comment above `ui_dirty_take` corrected — no longer claims a lost
  union "gets picked up on the next bump" (that was the bug); now states the race is closed via
  `std::atomic`. No logic change (`platform_present(0, pax_buf_get_height(fb))` on the first frame
  was already correct under the logical-coordinate contract).
- **Host backend**: unchanged, confirmed still builds/tests (already logical coords).
- **`specs/02-synth-architecture.md`**: budget-table row added for 6a.1 (+1.15 MB PSRAM scratch,
  flash 0x1128c0/46% free).

**Verify:** `make host` ✅ `make test` (183/183 incl. voice-alloc/arp/MIDI/limiter suites) ✅
`make build` ✅ `make format` ✅. Membrane clean (`ui_dirty.{h,cpp}` and `app/app.c` have no
esp_/bsp_ includes). Flash 0x1128c0 (≈1,124 KB, 46% partition free) vs. prior 0x112700 — the
scratch buffers are PSRAM (not flash), so the flash delta is just the new pack/blit logic.

**Decisions made that the work-order didn't spell out:**
- `ui_invalidate_all` uses a plain `atomic_store`, not a CAS loop, since `[0,0xFFFF)` unions to
  itself with any input — documented inline so a future reader doesn't "fix" it into an
  unnecessary CAS.
- `ui_dirty.cpp`/`.h` doc comments updated from "C11 atomics" to "`std::atomic`" for accuracy
  (the module is C++ even though its public API is `extern "C"`).

**Still open:** on-device visual verification is Pascal's step (ADR 0023 acceptance: full startup
paint; per-press row updates on amp-page up/down; aligned preset-page separators while scrolling;
watch for a 1-px sliver/mirror off-by-one at a band edge in the `X0/X1` mapping) — plus the
before/after `PROFILE` present numbers this ADR was written to fix, still unmeasured (ADR 0015
rule).

## 2026-07-07 — 6a display corruption diagnosed → ADR 0023 + 6a.1 work-order (HANDOFF)

Pascal reported on-device corruption after 6a (partial startup paint; up/down updates only a
mid-screen chunk; preset-page separators misalign; left/right fixes all). Full analysis + decided
fix: **[ADR 0023](decisions/0023-rotated-panel-band-present.md)**; closed work-order: **6a.1** in
[stage-6](stages/stage-6-display-foundation.md). Key facts:

- **Change detection was NOT the bug** — `ui_dirty` coalescing and all app.c invalidate sites are
  correct (one minor lost-union race, fixed in 6a.1).
- **RC1:** panel is portrait-native 480×800 under 270° PAX rotation → UI logical y = raw
  framebuffer **column** (`480−y`), so the "row band" blit painted a vertical strip. ADR 0022's
  zero-copy premise was false on this hardware.
- **RC2:** `bsp_display_blit` header says width/height but the tanmatsu impl forwards to
  `esp_lcd_panel_draw_bitmap`'s **end-exclusive** coords. d00623e ("pass band height") was a
  misdiagnosis that made small bands blit *nothing* — revert its interpretation. **Upstream
  badge-bsp doc/impl mismatch — flag/patch per spec 07.**
- **RC3:** first-frame full present used logical height (480) of 800 raw rows.
- **Fix (6a.1):** present seam speaks logical coords; device maps band→raw columns, packs into
  one of two 576 KB PSRAM scratch buffers (DMA2D is async) and blits the sub-window; bands
  ≥ 240 px fall back to full zero-copy blit (traffic breakeven).
- **State:** ~~docs committed; 6a.1 dispatch is next (Sonnet · high). Then re-measure PROFILE.~~
  **6a.1 implemented — see the entry directly above.** On-device verification + PROFILE
  re-measurement still pending (Pascal).

## 2026-06-29 — UI overhaul (WO-1…WO-6) COMPLETE — capstone

Six work-orders delivered a complete UI/interaction overhaul (all committed, all tests green):

- **WO-1** (`ui/ui.cpp`): compile-time `PAGE_TABLE[9]` replaces the runtime page-build loop.
  9 fixed pages: PRESET · PERFORM · OSC · FILTER · AMP ENV · MOD ENV · LFO · FX · AMP.
- **WO-4** (`platform/platform.h` + both platform backends): `PLATFORM_KEY_F1–F6` constants
  (0x0110–0x0115) wired to badge shape buttons (X/triangle/square/circle/three-lobe/diamond)
  and to SDL F1–F6 on host.
- **WO-2** (`ui/ui.cpp`): tightened layout (`ROW_H` 56→43, fonts scaled down); section
  sub-headers on PERFORM (CLOCK + ARP) and FILTER (VCF + HPF); synthwave restyle (neon
  cyan/magenta palette, gradient rules).
- **WO-3** (`ui/ui_presets_state.cpp/h` + `ui/ui_presets.cpp/h`, new files): preset browser
  page with lazy snapshot + live audition on ↑/↓; ○/Enter commits, □/Esc reverts.
- **WO-5** (`ui/ui.cpp`): F1–F6 actions wired; hold-to-repeat nudge on F1/F2 (250 ms initial
  delay, 0.15→0.50 norm/s ramp, 500 ms ease-in; stepped params 150 ms/step). Comma/dot nudge
  keys retired.
- **WO-6** (`ui/ui_overlay.cpp/h`, new; `control/keyboard.c/h`): F5 toggles a musical-typing
  key-guide overlay (QWERTY grid + note names + octave hint) on any page.

**Final control model (now in `specs/03-control-ui.md`):**
- F1 = nudge down, F2 = nudge up (hold-to-ramp), F3 = back, F4 = load/confirm, F5 = key-guide toggle, F6 = save.
- Preset page: browse auditions live; revert on back/navigate-away; commit on F4/Enter.
- 9 fixed pages in compile-time table; multi-group pages show sub-headers.
- `make test` ✅ (175/175) `make host` ✅ `make build` ✅ membrane clean throughout.
  Final flash: 0x110780 ≈ 1,114 KB (47% partition free). DIRAM 156 KB.

**Open debt (non-blocking):** HPF DSP wiring; `kPresetDestPwm` sentinel; tap tempo UI button;
Stage 4d FX (delay + ReverbSc). Next campaign: per Opus's judgment.

## 2026-06-29 — WO-1: explicit page table for UI (COMPLETE)

- **`ui/ui.cpp`**: replaced the runtime-built `page_groups[]` loop with a `static const PageDef PAGE_TABLE[9]` (compile-time, C++). New types: `PageKind` enum (`PAGE_PRESETS`/`PAGE_PARAMS`), `PageDef` struct (title, kind, groups[3], num_groups). New `page_rows(page_index, out, max_out)` helper concatenates `group_params()` results across all groups listed for a page.
- **Page order:** PRESET (empty, kind=PAGE_PRESETS) · PERFORM (GROUP_GLOBAL + GROUP_ARP) · OSC · FILTER (GROUP_FILTER + GROUP_HPF) · AMP ENV · MOD ENV · LFO · FX · AMP.
- `draw_tabs` labels from `PAGE_TABLE[i].title` (not `group_name`). `draw_rows`/`ui_handle_event` use `page_rows()`; `rows[]` buffers widened from 16 → 24 to safely absorb merged pages. `kNumPages=9` replaces `s->num_pages` in nav wrap math.
- **`ui/ui.h`**: `UIState` loses `page_groups[16]` and `num_pages`; comment updated. `group_name()` kept for future section sub-headers.
- `ui_state_init`: dynamic page-building loop removed; page=0/row=0 from `memset`.
- `make host` ✅ `make build` ✅ `make test` ✅ (169/169) `make format` ✅ membrane clean.
- **Next:** WO-2 (section sub-headers) or WO-3 (preset list page render).

## 2026-06-29 — WO-4: shape buttons (F1–F6) plumbed through platform layer (COMPLETE)

- **`platform/platform.h`**: added `PLATFORM_KEY_F1=0x0110 … PLATFORM_KEY_F6=0x0115` (contiguous
  block after the arrow keys). Brief comment: badge shape buttons left→right (X/triangle/square/circle/
  three-lobe/diamond).
- **`platform/device/platform_device.c`**: six new `case` arms in the `INPUT_EVENT_TYPE_NAVIGATION`
  switch map `BSP_INPUT_NAVIGATION_KEY_F1..F6 → PLATFORM_KEY_F1..F6`. Both press and release edges
  are delivered because `nav->state` (bool, same field already used for arrow keys) carries the
  press/release state — `out->pressed = nav->state`. This is the same BSP mechanism in use for UP/DOWN/LEFT/RIGHT.
- **`platform/host/platform_host.c`**: six new `case` arms in the SDL `SDL_KEYDOWN/SDL_KEYUP` switch
  map `SDLK_F1..F6 → PLATFORM_KEY_F1..F6`. Auto-repeat is already filtered for non-nav keys by the
  existing `e.key.repeat` guard, so only real down/up edges reach these cases.
- No existing key meanings changed. No handler consumes these yet (WO-5 wires actions).
- `make host` ✅ `make build` ✅ `make format` ✅ membrane clean.
- **Next:** WO-5 — wire shape button actions in the UI (F1–F6 → page-jump / preset-select / etc.).

## 2026-06-29 — WO-2: tighten layout, section sub-headers, synthwave restyle (COMPLETE)

- **`ROW_H` 56 → 43** (9 rows fit in the 402px content area; no scroll needed for any current page).
  `FONT_MD` 18→16, `FONT_SM` 14→12, `BAR_H` 14→10.
- **`HEADER_H = 18`** section sub-headers on multi-group pages: PERFORM (CLOCK + ARP) and
  FILTER (VCF + HPF) each draw a dim-bg strip + magenta left accent bar before each group's rows.
  Single-group pages (OSC, LFO, AMP ENV, etc.) unchanged. Selection semantics untouched —
  headers are purely draw-time; `page_rows()` / row-index math is unmodified.
- **`DrawItem` list** (new struct, `ITEM_HEADER` / `ITEM_ROW`): `build_items()` assembles the
  interleaved header+row sequence from `PAGE_TABLE` each frame. `items_height()` computes the
  true pixel height (headers + rows) for centering/scroll math. Scroll fallback still works for
  hypothetical future overflow: centres the selected row in the content area, clamps to edges.
- **Synthwave restyle** (stacked rects only — no per-pixel loops):
  - `COL_ACCENT2 = #FF2D9Du` (neon magenta) added.
  - `lerp_col()` inline helper and `draw_gradient_bar()` (3-segment cyan→mid→magenta).
  - Value bars: full accent brightness when selected, dimmed when not.
  - Active tab: cyan fill + 3px magenta underline.
  - Tab strip + status bar: 3-segment cyan→magenta 1px gradient rule.
  - `group_name()` labels updated: GROUP_FILTER→"VCF", GROUP_GLOBAL→"CLOCK", GROUP_ARP→"ARP".
- `make host` ✅ `make build` ✅ `make test` ✅ (169/169) `make format` ✅.
  Flash delta: **+1,388 bytes** (total 1,110,452 bytes, 47% partition free). `ui/ui.cpp` is 609 lines
  (acceptable per WO-2 scope note; split deferred to later WOs).
- **Next:** WO-3 (preset list page render) or WO-5 (shape button actions), per Opus.

## 2026-06-29 — WO-3: preset list page with audition-with-revert (COMPLETE)

- **`ui/ui.h`**: `UIState` gains 4 new fields: `auditioning` (bool), `audition_snapshot[128]` (norms copy),
  `audition_preset_name[33]`, `audition_preset_idx`. No existing fields moved/removed.
- **`ui/ui_presets_state.cpp` / `ui_presets_state.h`** (new, no PAX dep): pure state machine —
  `ui_presets_snapshot()` captures norms+name+idx into the audition fields, clears `auditioning`;
  `ui_presets_handle_event()` dispatches: Up/Down auditions on first move (snapshot captured lazily);
  F4/Enter confirms (snapshot refreshed, auditioning cleared); F3/Esc reverts (snapshot restored to engine
  via `engine_set_param_norm` + UIState); Left/Right reverts then returns false (pass-through to page switch).
- **`ui/ui_presets.cpp` / `ui_presets.h`** (new, PAX): drawing — scrollable list of factory presets +
  "User" row; selection arrow; cyan dot = committed preset, magenta dot = committed-behind-audition;
  "AUDITIONING" badge on highlighted row; alternating row stripe; hint strip at bottom.
  Scroll logic: centred on cursor, clamped to content bounds, identical to `draw_rows`.
- **`ui/ui.cpp`**: `#include "ui_presets.h"` added; `ui_state_init` calls `ui_presets_snapshot()` after
  boot-preset load and positions cursor to the active preset row; `ui_handle_event` delegates page-0 events
  to `ui_presets_handle_event` before the param-page switch/nudge path; `ui_draw` calls `ui_presets_draw`
  on page-0, `draw_rows` otherwise.
- **`tests/host/test_ui_presets.cpp`** (new): 6 tests with no-op engine stubs + no-storage stub:
  snapshot captures state, revert restores norms/name/idx, confirm updates snapshot (no revert on back),
  Esc alias, Enter alias, Left/Right reverts + returns false.
- CMakeLists updated: `host/` + `main/` get `ui_presets.cpp` + `ui_presets_state.cpp`;
  `tests/host/` gets `ui_presets_state.cpp` + `test_ui_presets.cpp` + PAX include path.
- `make test` ✅ (175/175) `make host` ✅ `make format` ✅ membrane clean.
  `ui.cpp` is 638 lines (was 609 before WO-3; split of new functionality done into two new files).
- **Next:** WO-5 (shape button actions / F-button overlay), per Opus.

## 2026-06-29 — WO-5: shape-button actions + hold-to-repeat nudge (COMPLETE)

- **F-button map on PAGE_PARAMS pages:**
  - F1 (X): nudge selected param DOWN (fine step = 0.01 norm / 1 stepped unit); begin hold-repeat.
  - F2 (triangle): nudge UP; begin hold-repeat.
  - F3 (square): back to PRESET page (s->page=0, s->row=0, snapshot called).
  - F4 (circle): no-op (consume/ignore).
  - F5 (three-lobe): left untouched (return false — reserved for key-guide overlay).
  - F6 (diamond): save user preset (same logic as `=` key, which still works).
- **Hold-to-repeat model (`ui_tick`, called from `app.c` each frame before `ui_draw`):**
  - Initial delay: 250 ms.
  - Continuous params: ramp rate 0.15 norm/s → 0.50 norm/s over 500 ms easing window.
    Full 0→1 traverse ~2 s at full speed.
  - Stepped params: one step every 150 ms, no acceleration.
  - Release of F1/F2 stops repeat immediately; row/page change also clears held_dir.
- **New `UIState` fields:** `held_dir` (0/±1), `held_row`, `held_since_ms`, `last_step_ms`, `repeat_accum`.
- **Refactors:** `nudge_selected(s, dir, coarse)` helper extracted; `nudge_selected_norm(s, delta)` for tick;
  `save_user_preset(s)` helper; comma/dot key handlers removed (retired). `=` key delegates to `save_user_preset`.
- **`ui_handle_event` restructured:** F1/F2 release now observed before the press-only guard (not dropped).
  Row-change navigation calls `hold_stop()` to cancel any active repeat.
- **Status hint strip:** `"<>pg  ^v row  F1/F2 nudge  F3 back  F6 save  ESC"`.
- No unit tests added (manual verification per WO spec — repeat model verified via manual `make host` run).
- `make test` ✅ (169/169) `make host` ✅ `make build` ✅ `make format` ✅ membrane clean.
  Binary: **0x110260 = 1,114,112 bytes (47% partition free)**; DIRAM 156 KB (26.99%).
- **Next:** per Opus — Stage 4d (FX), WO-6 (key-guide overlay), or other.

## 2026-06-29 — WO-6: on-demand piano key-guide overlay (F5 three-lobe button) (COMPLETE)

- **`control/keyboard.h/.c`**: new `keyboard_semitone_for_key(int key) → int` — thin public
  wrapper around the existing `static key_to_semitone()`. The UI derives note names from
  this accessor + `keyboard_octave()` with no table duplication (Prime Directive 2).
- **`ui/ui.h`**: `bool show_keyguide` added to `UIState` (zero-initialized by `memset` in
  `ui_state_init` — defaults to off).
- **`ui/ui.cpp`**: F5 (PLATFORM_KEY_F5) handled **early on PRESS**, before any page-kind
  branching, toggling `s->show_keyguide` and returning `true` (consumed). Old placeholder
  `return false` stub in the PAGE_PARAMS F5 case removed. `ui_overlay_draw_keyguide()` called
  last in `ui_draw()` behind the `show_keyguide` guard (on top of all other content).
- **`ui/ui_overlay.h` + `ui/ui_overlay.cpp`** (new, 174 lines): `ui_overlay_draw_keyguide()`
  draws a centred 680×260 dark panel with neon-cyan border, title "MUSICAL TYPING", a
  10-column two-row QWERTY key grid (naturals row / accidentals row with correct E–F and B–C
  gap slots), and a footer with current octave and Z/X/F5 hints. Each cell shows the key
  letter (uppercased) + note name (e.g. "C#4") computed via the new accessor. Black-key cells
  are distinctly darker than white-key cells. Rects + PAX text only — no per-pixel work.
  No engine/platform/synth deps (membrane clean).
- **`host/CMakeLists.txt` + `main/CMakeLists.txt`**: `ui_overlay.cpp` registered (mirrors
  how `ui_presets.cpp` is listed).
- `make host` ✅ `make build` ✅ `make test` ✅ (153/153) `make format` ✅ membrane clean.
  Flash delta: **+1,312 bytes** (0x110260 → 0x110780; 47% partition free). DIRAM unchanged.
- **Next:** per Opus — Stage 4d (FX: tempo-synced delay + DaisySP ReverbSc) or other.

## 2026-06-29 — WO-8: vector badge-button shape icons in hint strips (COMPLETE)

- **`ui/ui_icons.h` / `ui/ui_icons.cpp`** (new, ~65 lines total): `UiIconShape` enum (CROSS/TRIANGLE/SQUARE/CIRCLE/TRILOBE/DIAMOND = F1..F6); `ui_icon_draw(fb, shape, cx, cy, size, color)` draws the corresponding outline shape using PAX primitives. Geometry: `r = size*0.45`, `h = size*0.40`; CROSS = two `pax_draw_line` diagonals; TRIANGLE = `pax_outline_tri` apex-up; SQUARE = `pax_outline_rect`; CIRCLE = `pax_outline_circle`; DIAMOND = four `pax_draw_line` cardinal-point rhombus; TRILOBE = three `pax_outline_circle` arranged trefoil (lr=0.26*size, top lobe dy=0.20, bottom lobes dx=0.22/dy=0.14).
- **`ui/ui.cpp` `draw_status`**: hint strip replaced with mixed icon+text layout, advancing x via `pax_draw_text` return value and `ui_icon_width`. Left portion stays as text (`"<>pg  ^v row  "`), then △ ✕ "nudge", □ "back", ☘ "keys", ◇ "save", "ESC". Icon cy = text_y + FONT_SM*0.5 (vertically centred).
- **`ui/ui_presets.cpp`**: hint strip replaced with "^v browse" + ○ "load" + □ "back" + "<> page"; dot bug fixed — U+25CF glyph (tofu on device) replaced with `pax_simple_circle` (radius FONT_MD*0.18, position matching the old glyph column).
- **`ui/ui_overlay.cpp`**: footer "F5 = close" → "Z/X = oct down/up" (text, unchanged) + ☘ icon + "close".
- **`host/CMakeLists.txt` + `main/CMakeLists.txt`**: `ui_icons.cpp` registered in both build descriptions.
- `make host` ✅ `make test` ✅ (All tests passed) `make format` ✅ `make build` ✅ membrane clean.
  Flash: DIRAM 156744 B (27.19%); total image 1,121,194 B. Commit: cd0a654.
- **Next:** per Opus — Stage 4d (FX), or other.

## 2026-06-29 — Launchkey 37 mapping: mod-wheel → filter + 8 pots → fun params (COMPLETE)

- **Mod wheel → cutoff** (`engine/juno_voice.cpp` render only): added hardwired additive term
  `+ p_mod_wheel_ * kModWheelCutoffRange` (8 kHz) beside the existing VCF_ENV/KEY/LFO panel mods.
  NOT a mod-matrix route — the matrix treats `cutoff_mod` as raw Hz, so a ±1 depth adds ~1 Hz
  (inaudible); the matrix ENV2→cutoff routes in presets are cosmetic no-ops (real filter env is
  `VCF_ENV_DEPTH`×8000). Additive: wheel up brightens, wheel 0 = patch unchanged, every patch.
  Mod wheel stays available as a `MOD_WHEEL` matrix source too. No preset/matrix change.
- **8 pots → params** (`engine/param_desc.cpp` `midi_cc` edits): Launchkey pots send CC 21–28.
  21→FILTER_CUTOFF, 22→FILTER_RES, 23→VCF_ENV_DEPTH, 24→VCF_LFO_DEPTH, 25→CHORUS_DEPTH,
  26→UNISON_DETUNE, 27→LFO1_RATE, 28→ENV_RELEASE. GM CCs 74/71/93/72 freed (controller only
  sends 21–28; per-controller remap is MIDI-learn's job, deferred). Routed via existing
  `engine_cc_to_param`→`engine_set_param_norm` (5c-iii) — no router change.
- **Tests** (`tests/host/test_mod_sources.cpp`): updated `test_engine_cc_to_param` (CC21→cutoff,
  CC22→res, CC74→0); added `test_mod_wheel_hardwired_cutoff` (wheel up brightens, no routing).
- `make test` ✅ (all pass) `make host` ✅ `make build` ✅ `make format` ✅. Image **1,121,584 B**.
- **Hardware check (Pascal):** mod wheel opens filter on any patch; pots 1–8 sweep the params above.

## 2026-06-29 — Input latency fix: decouple poll from render + dirty-gate (COMPLETE)

**Bug:** `app_run` polled input once per 16 ms frame, after the full-screen `ui_draw` +
`platform_present()` (~1.15 MB blit). A key press waited behind the whole render cycle.

**Fix (commits 4879169 + fa7b9e5):**
- `sdkconfigs/tanmatsu`: `CONFIG_FREERTOS_HZ=1000` (was 100). `vTaskDelay(1)` now
  resolves to ~1 ms, not ~10 ms. Only the canonical defaults fragment was changed.
- `app/app.c`: loop restructured — input + MIDI polled every ~1 ms (`POLL_MS=1`);
  render runs every ~16 ms (`RENDER_MS=16`) but **only when `ui_state.dirty`** is set.
  `ui_handle_event` return value, `held_dir`, and per-frame `active_voices`/`octave`
  changes all set `dirty`. `next_render` timestamp replaces the unconditional sleep.
- `ui/ui.h` / `ui/ui.cpp`: `UIState` gains `dirty`, `last_drawn_voices`,
  `last_drawn_octave`. `ui_state_init` sets `dirty=true`, sentinels force first paint.
- SYNTH_PROFILE guard (off by default) wraps `ui_draw` + `platform_present` with cycle
  counters; logs avg/min/max μs every 120 rendered frames via `printf`.
- SINGLE-PRODUCER comment in `app_run` documents SPSC ring invariant.

`make test` ✅ (153/153) `make host` ✅ (build-only; SDL window) `make build` ✅
Image: 1,121,312 B (−272 B vs. WO-8; 47% partition free). DIRAM 156,742 B unchanged.

**What's next:** Stage 4d FX (tempo-synced delay + ReverbSc). If device profiling
(enable with `-DSYNTH_PROFILE`) shows the full-frame blit still costly during voice-
meter animation, Stage 2B partial-rect blitting is the natural follow-on.

## 2026-06-29 — Render task: UI drawing moved off the control loop (COMPLETE)

**Root cause:** `bsp_display_blit` (~1.15 MB) blocked the input-poll task, so the
second note after idle waited a full render cycle (~16 ms) before being processed.

**Fix (commit 9eccdd1):**
- `platform/platform.h`: new `platform_render_task_start(cb, ctx, ms)` / `_stop()` seam.
- `platform/device/platform_device.c`: dedicated core-0 render task at RENDER_PRIO 2;
  calling (control) task raised to CONTROL_PRIO 5 so input polling wins over USB-host
  tasks (CLASS_PRIO 3 / DAEMON_PRIO 2) and the render task (same priority 2).
  Priority ordering: control 5 > usbh_midi 3 > usbh_daemon 2 = render 2; audio
  (core 1, configMAX-2) untouched.
- `platform/host/platform_host.c`: stubs return false; host renders inline (SDL
  requires main thread).
- `ui/ui.h` / `ui/ui.cpp`: `dirty` bool replaced with `volatile uint32_t change_seq`
  (single-writer control, single-reader render; atomic on RV32 per alignment).
  Initialized to 1 so first frame draws. `last_drawn_voices/octave` → `last_voices/last_octave`.
- `app/app.c`: render_cb (file-scope, owns framebuffer + present) only repaints when
  `change_seq` advanced. Control loop bumps `change_seq` after every visible state write.
  `has_render_task=false` path renders inline for host.
- Accepted one-frame UIState visual tear (fixed-size arrays, no freed pointers — benign).

`make test` ✅  `make host` ✅  `make build` ✅  `make format` ✅
Image: 1,121,828 B (+244 B; 46% free). DIRAM: 156,770 B (+26 B). Task stack: 8 KB heap.

**What's next:** Hardware latency verification (Pascal: play rapidly, confirm second note
is no longer gated by the blit). If display smoothness suffers under heavy MIDI load,
or to cut core-0 overhead further, Stage 2B partial-rect blitting is the next lever.
Stage 4d FX (tempo-synced delay + DaisySP ReverbSc) is the main open campaign.

## 2026-06-29 — Launchkey follow: CC-driven UI focus + README MIDI section (COMPLETE)

- **`control/midi_router.c/.h`**: module-static `s_focus_id/norm/pending`; stashed in the
  generic-CC branch whenever `engine_cc_to_param` returns a non-zero id. New getter
  `midi_router_take_param_focus(out_id, out_norm)` — one-shot read-and-clear.
  Mod wheel (CC1), sustain (CC64), and panic (CC120/123) explicitly excluded from focus.
- **`ui/ui_page_table.cpp/h`** (new): PAGE_TABLE, PageDef/PageKind enum, `group_params`,
  `page_rows`, `kNumPages` extracted from `ui.cpp` into a PAX-free unit so tests can compile
  the page-search logic without the drawing stack.
- **`ui/ui_focus.cpp`** (new, PAX-free): `ui_focus_param` — loops pages/rows, on match
  reverts any active audition (inline revert, same path as manual navigate-away), sets
  `s->page/row`, updates `s->norms[id] = clamp01(norm)`, returns true. Unknown id → false,
  no state change.
- **`ui/ui.cpp`**: removed duplicated PAGE_TABLE/group_params/page_rows (now in
  `ui_page_table.cpp`); added `#include "ui_page_table.h"`.
- **`app/app.c`**: right after `midi_router_poll()`, calls `midi_router_take_param_focus`;
  if true and `ui_focus_param` returns true, bumps `change_seq` to trigger a repaint.
- **`tests/host/test_ui_focus.cpp`** (new, 2 tests): FILTER_CUTOFF → FILTER page (index 3),
  correct row, norm=0.5; unknown id returns false, page/row unchanged.
  Wired into `tests/host/CMakeLists.txt` + `main.cpp`.
- **`README.md`**: "MIDI control (USB)" subsection with CC table + Launchkey note; added
  "Controller follow" bullet to "What it can do".
- **`specs/03-control-ui.md`**: "CC auto-focus" section after MIDI expression.
- `make test` ✅ (all pass, +2 new) `make host` ✅ `make build` ✅ `make format` ✅.
  Flash: **1,122,268 B** (+440 B vs. prior; 46% partition free). DIRAM: 156,782 B (27.2%).
- **What's next:** Stage 4d FX (tempo-synced delay + DaisySP ReverbSc), or HPF DSP wiring.

## 2026-06-29 — ADR 0021 Part 1: CC7 attenuation-only channel volume (COMPLETE)

**What changed:** CC7 (MIDI channel volume) was routing to `MASTER_GAIN`'s 0–2× range,
causing CC7=127 to be a 4× boost — the opposite of MIDI spec. Dense MIDI playback landed
continuously in the soft-clip saturation zone.

**Fix (4 files):**
- `engine/synth.cpp`: added `static std::atomic<float> s_channel_vol{1.0f}`; reset to 1.0 in
  `synth_init` and in the panic block (so panic can never latch the session quiet); output gain
  becomes `MASTER_GAIN × channel_vol × unison_gain(U)`.
- `engine/synth.h`: declared `engine_set_channel_volume(float vol)` alongside other expression setters.
- `control/midi_router.c`: CC7 special-cased before the generic-CC fallthrough; applies
  `vol = norm * norm` square-law taper (GM/DLS convention: 127→1.0, 64→0.25=−12 dB, 0→silence)
  and calls `engine_set_channel_volume(vol)`. CC1/64/120/123 handling untouched.
- `engine/param_desc.cpp`: `MASTER_GAIN` CC field changed from 7 to 0xFF; `VCA_LEVEL` already
  had 0xFF (dead shadow — no change needed). `MASTER_GAIN` is now a manual headroom knob only.
- `README.md`: CC7 row updated from "Master gain" to accurate channel-volume description.

**Key decisions:** CC7 is performance state, not a preset value — routed via expression atomics,
not the param table (per ADR 0021). Square-law taper `norm²`. No test needed updating (existing
`test_engine_cc_to_param` checked CC21/22/74; no CC7→param assertion existed).

**Results:** `make test` ✅ (all pass) `make host` ✅ `make build` ✅ (image 0x1121c0 B, 46% free).

**What's next:** ADR 0021 Part 2 — master-bus peak limiter (`dsp/LimiterStereo`, feed-forward
stereo-linked, THRESH=0.92, 1 ms attack, 120 ms release). See ADR 0021 §2.

## 2026-06-29 — ADR 0021 Part 2: master-bus peak limiter (COMPLETE)

ADR 0021 is now **fully implemented** (Parts 1+2 done).

**What shipped:**
- `dsp/limiter.h` (new, header-only): `dsp::LimiterStereo` — feed-forward, stereo-linked
  peak limiter. Constants: THRESH=0.92, attack 1.0 ms (`a_att ≈ 0.0202 @ 48k`), release
  120 ms (`a_rel ≈ 0.000174 @ 48k`). No libm in `process()`; `expf` in `init()` only.
  Denormal strategy: unity-snap on recovery tail, finite floor (1e-6), NaN input guard.
  Default-safe if `process()` called before `init()` (member initializers).
- `engine/synth.cpp`: `s_limiter` added beside `s_chorus`; initialized in `synth_init`;
  inserted in the per-frame loop (step 6) post-gain, pre-`soft_clip`, in **both** chorus-on
  and chorus-off branches. `s_limiter.process()` called once per frame (continuous envelope).
  Gain pipeline is now: gain → peak limiter (GR) → soft_clip (transient net) → output.
- `tests/host/test_limiter.cpp` (new): `test_limiter_suite()`, 9 test cases covering
  below-threshold transparency, sustained over-threshold convergence, attack timing (5 ms
  catch), net safety (soft_clip(peak×gr) ≤ 1.0), release asymmetry (>>1k samples),
  threshold boundary sweep, NaN/huge-peak robustness, and CC7-staged scenario.
- `tests/host/main.cpp` + `tests/host/CMakeLists.txt`: suite registered alongside test_saturate.
- `specs/02-synth-architecture.md`: limiter line item added to the cycles/block budget table.

**Results:** `make test` ✅ (all pass, 9 new limiter cases) `make host` ✅ `make build` ✅
  `make format` ✅. Image: 0x112370 B (46% partition free).

**Note on release test:** the 120 ms one-pole release requires ~6.3 τ (≈36 k samples) to
recover from strong gain reduction to 0.999; test window is 60 k samples (generous).
The release-vs-attack asymmetry assertion (release > 1000 samples >> attack ~240 samples) confirms
the asymmetry is present and correct.

See [ADR 0021](decisions/0021-master-output-staging-and-limiter.md) for full rationale.

## 2026-06-30 — Device crackle diagnostics: audio-block profiler + display freeze (COMPLETE)

Added two build-flag-gated diagnostic modes to isolate the Solo Lead patch crackle
under heavy key-smashing (suspected RT deadline miss). No shipping behavior change —
all new runtime code is under `#ifdef`.

**SYNTH_PROFILE** (`make build PROFILE=1` / `cmake -DSYNTH_PROFILE=ON`):
- `platform/device/platform_device.c`: wraps `s_render()` with `esp_cpu_get_cycle_count()`;
  accumulates sum/max/over-budget counters (budget 480 000 cyc = 360 MHz × 64 / 48000).
  Measures render compute only — `i2s_channel_write` DMA-wait is excluded by design.
- `platform/platform.h`: `platform_audio_profile_read(avg, max, over, count)` declared
  unconditionally; returns zeros when SYNTH_PROFILE is off (shipping safe).
- `platform/host/platform_host.c`: stub always returns zeros.
- `app/app.c`: ~1 s cadence readout in the main `while (running)` loop.
  Console output: `[PROFILE] audio avg=X max=Y over=Z/N us-budget=1333`

**SYNTH_FREEZE_DISPLAY** (`make build FREEZE_DISPLAY=1` / `-DSYNTH_FREEZE_DISPLAY=ON`):
- `app/app.c` `render_cb`: first frame paints normally, every subsequent call returns
  immediately. Removes display-blit (~1.15 MB) / memory-bus pressure from core 0 so
  PROFILE=1 tests can isolate audio compute from blit contention.

**2×2 test matrix** (smash and hold keys on Solo Lead while watching the console):
1. `make build PROFILE=1` — baseline: both suspects active (audio compute + display blit).
2. `make build PROFILE=1 FREEZE_DISPLAY=1` — display frozen: only audio compute.
   If `over` drops to 0 → blit contention is the culprit. If `over` stays high → compute.

`make host` ✅ `make test` ✅ (all pass) `make build` ✅ `make build PROFILE=1` ✅
`make build PROFILE=1 FREEZE_DISPLAY=1` ✅ `make format` ✅. Commit: 1f4e39d.

## 2026-06-30 — Instant-attack limiter: fix transient overshoot crackle (COMPLETE)

**Bug:** Hard/loud playing produced audible crackle; soft playing was clean; `MASTER_GAIN=0.10`
was clean at any velocity. Root cause: the 1 ms one-pole attack in `dsp/limiter.h` left
`env_gr ≈ 1.0` for ~48 samples after the first over-threshold transient, so
`peak × gr ≈ 2.0` sailed into `dsp/saturate.h soft_clip`'s hard ±1.5 ceiling — producing a
hard clip that scaled with velocity exactly as observed. Device audio profiler showed `over=0`
deadline misses, confirming purely an amplitude/transient issue.

**Fix (`dsp/limiter.h`):** Replaced the one-pole attack with an instantaneous snap —
`env_gr = target` immediately when more reduction is needed. Release (120 ms one-pole) is
unchanged. `a_att_` member and its `expf()` computation in `init()` removed (now unused).
No change to `engine/synth.cpp` required; the call site is unaffected.

**Tests (`tests/host/test_limiter.cpp`):**
- Added `test_limiter_attack`: first over-threshold sample is clamped to threshold
  (`peak × gr ≤ thresh + 1e-4`) and `soft_clip(peak × gr) < 0.95`. **Failed pre-fix, passes post-fix.**
- Added `test_limiter_no_transient_overshoot`: worst-case (env at unity, loud frame
  immediately) — same assertions. **Failed pre-fix, passes post-fix.**
- Adapted existing attack-timing case: now asserts instant 1-sample catch (not "within 5 ms").
- Adapted net-safety case: comment updated to reflect instant attack.
- Adapted release case: asymmetry comment updated to "attack = 1 sample, release >> 1000 samples."

**Docs:** ADR 0021 §2 limiter table updated — attack changed from "1.0 ms" to
"instantaneous (per-sample peak clamp)" with rationale paragraph.

**Results:** `make test` ✅ (all pass, 2 new limiter cases) `make host` ✅ `make build` ✅
`make format` ✅. Image: 0x112830 B (46% partition free; unchanged — `a_att_` removal
offset the new code). Commit: 72b0d2f.

**Note:** The device CPU-spike (mono release-tail pile-up causing periodic loudness dips)
is a **separate open item** still under investigation — unrelated to this crackle fix.

## 2026-06-30 — Signal-magnitude probe for master-chain crackle diagnosis (COMPLETE)

Added a `SYNTH_PROFILE`-gated signal-magnitude probe to `engine/synth.cpp` `synth_render`
step 6 (the per-sample loop). Four volatile float accumulators track peaks through the chain:

- **mono** (`s_pk_mono`): peak of the raw voice-sum — what voices are contributing before gain.
- **postg** (`s_pk_postgain`): peak fed into the limiter (post-gain). The key crackle indicator.
- **gr** (`s_min_gr`): worst (lowest) limiter gain-reduction factor. 1.0 = limiter idle.
- **out** (`s_pk_out`): peak output after `soft_clip`. Should stay ≤ 1.0; above that = hard clip.

`engine_profile_read(pk_mono, pk_postgain, min_gr, pk_out)` (declared unconditionally in
`engine/synth.h`; returns zeros when `SYNTH_PROFILE` off) snapshots+resets the accumulators.
`app/app.c` calls it in the ~1 s PROFILE readout and prints:
`[PROFILE] sig  mono=X.XX postg=X.XX gr=X.XX out=X.XX`

To use: `make build PROFILE=1 && make install && make run` then `make sniff`.

**Diagnosis guide:**
- `postg` 0.6–0.9 with `gr ≈ 1.0` → headroom problem (soft_clip cubic zone; raise MASTER_GAIN
  or tune voices down). Distortion is from soft_clip operating in its non-linear region.
- `postg > 0.92` with `gr < 1.0` → limiter active (working as designed). Check `out` ≤ 1.0.
- `out > 1.0` → soft_clip ceiling breached (postg×gr > 1.5); hard clip. Should not happen with
  the instant-attack limiter in place (see ADR 0021 / limiter fix commit 72b0d2f).
- All zeros → no audio rendered during the interval (silence); expected between notes.

All probe runtime code is under `#ifdef SYNTH_PROFILE` — zero overhead in the shipping image.
Commit: 983a7b3.

## 2026-06-30 — Mono+unison voice-cap: slot reuse in note_on_mono / note_off_mono (COMPLETE)

**Bug:** On the Solo Lead patch (legato, U=2, ENV_RELEASE=0.20 s), rapid retriggering drove
the voice meter to 8 and spiked render cost. Root cause: the unison-mono `note_on_mono` path
released the old group via `note_off()` (leaving a 200 ms release tail still rendering + active)
and allocated fresh slots. Smashing faster than release time stacked tails up to the full pool.

**Fix (`engine/voice_alloc.cpp`):** In both `note_on_mono` and `note_off_mono` (the `unison_count_ > 1`
branches), check whether the current group is intact (gated slots tagged with old pitch, count ==
`unison_count_`). If yes, **reuse those exact slots** — retag `unison_tag_`, update `pitch`/`timestamp`,
call `voice->note_on()` to retrigger. No `note_off()` on the old group → no release tail. Fall back
to the existing release+allocate path when the group is absent (first note) or its size doesn't match
the current `unison_count_` (unison param changed between notes).

**Invariant established:** mono live voices ≤ `unison_count_` at all times. Solo Lead voice meter
stays at 2 regardless of retrigger rate. Render cost proportional to `unison_count_`, not the pool.

**Tests (`tests/host/test_alloc_unison_mono.cpp`, 5 cases):** rapid retrigger cap, steal-back cap
(4→2), U=1 mono regression, poly regression, unison-count-change fallback. All green.

`make test` ✅ `make host` ✅ `make build` ✅ `make format` ✅ image 0x112540 B (46% free). Commit: 73496d3.

## 2026-06-30 — ROOT CAUSE of the "crackle": preset apply truncated at 32 params (FIXED)

The multi-day crackle hunt ended here. **Preset apply used 32-entry id/val buffers**
(`ui.cpp` boot-load/user-restore/preset-switch) but every factory preset carries **49**
params. PLAY_MODE, UNISON_COUNT, CHORUS_MODE, PORTAMENTO, and all ARP_* sit past index 32 →
**silently dropped → loaded at param-table defaults.** "Solo Lead" (mono+legato+unison-2)
booted **poly with default unison** → 8 voices on an 8-key smash → `soft_clip` driven into its
nonlinear zone (grit) **plus** 8-voice display-blit contention (underruns) = the crackle.

Diagnosis path (all the prior diagnostic work paid off): signal probe showed `gr=1.00`
(limiter never engaged) with `postg`=0.6–0.9 (soft_clip nonlinear) and `mono`≈5 (8 voices);
audio probe showed `over`=12–18, `max`≈3000 µs with live display (contention) vs `over`=0,
`max`=843 µs frozen. Pascal manually set PLAY_MODE=2 → `mono`=1.08, `postg`=0.2, `over`=0,
crackle gone — proving the patch was running poly.

**Fix (commit 9caa5bf):** new `PRESET_MAX_PARAMS = 96` (≥ kJunoParamCount=49, blob-format
headroom) in `engine/preset.h`; all apply buffers sized to it — `ui.cpp` (3 sites) +
`ui_presets_state.cpp` (audition path, which already used 64 — why *browsing* a preset sounded
right but *booting* didn't). Fixes ALL presets, not just Solo Lead (chorus/arp/unison/play-mode
were defaulted everywhere). `make host` ✅ `make test` ✅.

**Also (commit 0e5d402):** build-flag determinism — `PROFILE`/`FREEZE_DISPLAY` now passed as
explicit `-DVAR=0/1` so an omitted flag clears the stale CMakeCache entry (they share the
default build dir, unlike BENCH/USBHOST_DEBUG which fork dirs). No more `make clean` needed to
drop a diagnostic flag.

**Open/low-pri follow-ups:** revert the instant-attack limiter (it's a clipper, dormant now);
optional headroom for genuinely-poly loud patches; WS3 dirty-rect blit (Stage 2B) still cuts
the display-contention underruns that affect poly chords + frees budget for FX. Diagnostics
(`SYNTH_PROFILE` audio + signal probes, `SYNTH_FREEZE_DISPLAY`) remain in-tree behind flags.

## 2026-06-30 — Orphaned-voice regression: set_play_mode + preset-apply cleanup (COMPLETE)

**Regression (follow-up to 9caa5bf):** Preset apply now loads ALL 49 params, so PLAY_MODE
actually changes on preset switch. `set_play_mode()` only cleared tracking state (`mono_slot_`,
stack, `unison_tag_`) but never silenced gated voices — any voice that was `gate==true` before
the mode switch became an orphan: untracked, `note_off` unreachable. Result: a sustained
~600 Hz stuck tone after the first patch switch, and voice-pool saturation (mute until restart)
after repeated switches.

**Fix A (`engine/voice_alloc.cpp`):** `set_play_mode()` now calls `reset_all()` on a real mode
change instead of the partial tracking-only clear. `reset_all()` is already RT-safe (no alloc,
no log, no blocking). Redundant partial-clear lines removed.

**Fix B (`ui/ui.cpp`, `ui/ui_presets_state.cpp`):** `engine_all_notes_off()` added at the top
of `ui_apply_params` (boot-load and `[`/`]` switch paths) and `apply_params` (audition/browse
path). Clears all voices + arp before any patch load. `engine_all_notes_off` is lock-free
(atomic flag); consumed by the audio thread at the top of the next block, before freshly-queued
params drain — correct ordering guaranteed.

**Test (`tests/host/test_alloc_unison_mono.cpp`, case F):** note_on in mono+unison then
`set_play_mode(kPoly)` — asserts every slot has `gate==false` and `is_active()==false`.
`test_ui_presets.cpp`: added `engine_all_notes_off` no-op stub so test build links.

`make test` ✅ `make host` ✅ `make build` ✅ `make format` ✅. Commit: 30fa253.

## 2026-06-30 — 🔎 CRACKLE INVESTIGATION — STATE & HANDOFF (OPEN)

Long crackle hunt. Big bugs fixed; a **residual remains** and the right move is a fresh-context
session. Read this before resuming.

**Current symptom (still open):** on device, an **intermittent "sticky" ~600 Hz sine** + **slight
crackle**, on **ALL patches** (not Solo-Lead-specific), **present even with the display frozen**
(`FREEZE_DISPLAY=1`). Character is now milder/shorter than at the start (the gross causes are gone).

**Fixed this session (all committed, tests green):**
- **Preset 32-param truncation** (`9caa5bf`) — apply buffers were `[32]`; presets carry 49 params,
  so PLAY_MODE/UNISON/CHORUS_MODE/ARP (idx 33-49) were dropped → every patch booted at table
  defaults (poly). New `PRESET_MAX_PARAMS=96` (`engine/preset.h`) used at all apply sites. **This
  was the dominant crackle cause** (Solo Lead ran poly → 8 voices → soft_clip grit).
- **Voice orphan/mute on mode change** (`30fa253`) — `set_play_mode` cleared tracking but didn't
  silence gated voices → orphaned stuck voice (a sine) + accumulation to mute; now calls
  `reset_all()` on change, and `engine_all_notes_off()` runs on every preset apply (ui.cpp +
  ui_presets_state.cpp). Fixed the *mute* and the *original* sine — but a sine still recurs, so
  there is ANOTHER stuck-voice path.
- **Instant-attack limiter reverted** (`44a227b`) — it was effectively a hard clipper at threshold
  (flat-tops transients); restored the ADR-0021 1 ms-attack limiter. Dormant for Solo Lead anyway.
- **Build-flag determinism** (`0e5d402`), **mono+unison voice cap** (`73496d3`).

**RULED OUT (don't re-chase):**
- *Display-blit contention* — frozen display STILL crackles. (It does add deadline misses at high
  poly voice counts — real, but not this residual.)
- *soft_clip headroom* for Solo Lead — with PLAY_MODE loading, it's mono 2 voices: signal probe
  showed `mono≈1.1, postg≈0.2, gr=1.00, over=0`, clean numbers. (Headroom may still matter for
  genuinely-poly loud chords — open, low-pri.)
- *MIDI note-on vel-0 not treated as note-off* — parser DOES convert it (`control/midi_in.c:134`).
- *Instant-attack limiter* — reverted.

**CONFIRMED 2026-06-30 (Pascal):** after smashing then releasing ALL keys on Solo Lead, the voice
meter **stays at 2** (= `unison_count`). So it's a **voice leak**: most notes release fine, but the
**last note's mono+unison group never gets released** — it stays gated → the stuck ~600 Hz sine,
and 2 dead voices drone/contend → the residual crackle. Stuck count == unison_count, persistent.

**Prime suspect — `note_off_mono` (mono+unison) final-release path, `engine/voice_alloc.cpp`.**
Likely introduced/exposed by WS4 (`73496d3`, mono slot-reuse). WS4's tests assert the *cap*
("≤ 2 gated", "steal-back yields 2") but **never assert that releasing all keys returns to 0
active voices** — so a "final note-off leaves the group gated" leak passes them. Trace: the
`note_off_mono` "no more held notes" branch (releases group by `tag = slots_[mono_slot_].pitch`)
and how the WS4 reuse path in `note_on_mono` sets `mono_slot_` / `unison_tag_` — a desync between
`mono_slot_.pitch`, the gated group's tag, and the mono note-stack would make the final note-off
match nothing. Also check the steal-back branch. **Add a regression test: play → release all →
assert 0 gated AND 0 active voices** (the missing assertion).

Lower-probability fallbacks if the above isn't it: `CommandQueue<64>` (`s_cmds`) dropping a
note-off on overflow under heavy smashing; poly steal / note-off pitch→slot desync; denormal SVF
ring (P4 no FTZ, ADR 0012). But the clean "stuck at exactly unison_count" points hard at the
mono+unison release path.

**Diagnostics in-tree (behind flags; shipping image unaffected):**
- `SYNTH_PROFILE`: audio-block cycle profiler + signal probe (`[PROFILE] sig mono/postg/gr/out`
  via `engine_profile_read`) + `[PROFILE] audio avg/max/over`.
- `SYNTH_FREEZE_DISPLAY`: paint once then freeze (isolates display contention).
- Run: `make PROFILE=1 USBHOST_DEBUG=1 [FREEZE_DISPLAY=1] build install run` + `make sniff`.
  (`USBHOST_DEBUG` keeps the USB-C console alive + routes the controller via USB-A; note its
  per-MIDI-event `ESP_LOGI` is itself core-0 load — quiet it if it pollutes a measurement.)

**Open follow-ups (non-blocking):** WS3 dirty-rect blit (Stage 2B — display is genuinely slow +
cuts poly-chord contention underruns + frees budget for FX); optional poly headroom.

## 2026-06-30 — Buried-note off in mono+unison must not leak voices (COMPLETE — resolves OPEN crackle handoff)

**Bug (the residual sticky ~600 Hz sine):** On mono+legato U=2, on A → on B → off A (buried) → off B left two voices gated forever. Root cause in `note_off_mono`: `stack_remove(A)` ran, but `s.pitch` (the sounding slot) was B, not A. The steal-back branch fired with `prev_pitch=B` and `cur_tag=B` — the same group — so it gated B off, retriggered it without restoring `pitch`/`gate`/`unison_tag_`, then off B found nothing gated. Leak == `unison_count` voices stuck indefinitely.

**Fix (commit 29d89d1, `engine/voice_alloc.cpp`):**
- PRIMARY: Guard at top of `note_off_mono` (after `mono_slot_` and `s` are obtained): `if (s.pitch != pitch) return;` — a buried note only ran `stack_remove`, the sounding group is untouched.
- DEFENSIVE: In the steal-back reuse retrigger loop (`prev_group_count == unison_count_` branch), explicitly restore `sv.pitch = prev_pitch`, `sv.gate = true`, `unison_tag_[idx] = prev_pitch` alongside the existing timestamp + note_on calls.

**Regression test (case G, `tests/host/test_alloc_unison_mono.cpp`):** Two scenarios — buried-first then sounding release order, and sounding-first order — both assert `count_gated == 0` AND every `voice->is_active() == false` after full release.

`make test` ✅ (all pass, new case G + existing A–F green) `make host` ✅ `make build` ✅ `make format` ✅.
Image: 0x112540 B (46% partition free, DIRAM delta ~0). Commit: 29d89d1.

**Resolves:** the OPEN crackle voice-leak handoff (2026-06-30 investigation log). Pending Pascal's device verification (play → release all keys → voice meter should return to 0).

## 2026-06-30 — 🔎 POLY CRACKLE = CPU DEADLINE MISS (not amplitude) — HANDOFF (OPEN)

The mono+unison **voice leak is FIXED** (29d89d1, above). A **separate poly crackle remains
OPEN**, and the device profiler has now **falsified the amplitude hypothesis** — it is a **CPU
deadline miss (audio-block underrun)**, not transient overshoot/clipping.

**Symptom (Pascal, Juno EP, poly):** crackle scales with velocity × polyphony — 4 notes, or 3
hard, or 8 moderate.

**Profile numbers (device, `PROFILE=1 USBHOST_DEBUG=1`, budget = 1333 µs/block):**
- `sig` chain is CLEAN at loudest: `mono=3.95 postg=0.84 gr=1.00 out=0.76`. `postg` never crosses
  the 0.92 knee, `gr` stays **1.00** (limiter never engages), `out` peaks 0.76. **No clip, no
  limiting** → NOT a headroom problem. **Look-ahead limiter is the WRONG fix — do not build it.**
- `audio` breaks: idle `avg=80 max=85 over=0`; smashing → `avg=331 max=1774 over=9` …
  `avg=699 max=2262 over=16` … `avg=690 max=2547 over=21/750`. **max ≈ 1.9× budget, 9–21
  blocks/sec over.** Those overruns ARE the crackle.

**Decomposition:** baseline full-poly `avg≈690 µs ≈ 52%` matches the ratified 8-voice cost (fine).
The killer is **per-block spikes to ~2× budget** riding on top, clustered around the MIDI note-on
bursts in the log AND the voice-meter animating the ~1.15 MB full-screen blit during play.

**Two prime spike sources (one measurement away from being settled):**
1. **Display-blit contention** — core-0 blit (framebuffer in PSRAM) starves core-1 audio's PSRAM
   wavetable fetches. This is the "real at high poly" contention the earlier handoff flagged.
   Fix = **WS3 dirty-rect blit (Stage 2B)** — already the planned lever; also frees FX budget.
2. **Note-on burst / cold-wavetable voice activation** — a chord's worth of note-ons draining in
   one audio block (alloc scan + `JunoVoice::note_on` + first cold PSRAM wavetable touch per newly
   active voice). Fix = optimize that path.
   (`USBHOST_DEBUG` per-event `ESP_LOGI` also adds core-0 load — quiet it for clean measurement.)

**NEXT MEASUREMENT (requested from Pascal):** `make PROFILE=1 FREEZE_DISPLAY=1 USBHOST_DEBUG=1
build install run` + `make sniff`, smash 8 hard on Juno EP, read `[PROFILE] audio`:
- `over` → ~0  ⇒ **display-blit contention** is it ⇒ implement WS3 dirty-rect blit (Stage 2B).
- `over` stays high ⇒ **note-on/activation compute spike** ⇒ profile & optimize `JunoVoice::note_on`
  / voice wake path (consider warming wavetable access, cheaper note_on).

Lesson (RT rule 6): amplitude intuition was wrong; the signal probe + cycle profiler settled it.

## 2026-07-06 — Stage 6a: WS3 dirty-rect present (COMPLETE, resolves G6/ADR 0022)

Replaced the full-screen `platform_present()` with a coalesced full-width scanline-band present.
New `ui/ui_dirty.{h,cpp}`: `ui_invalidate(y0,y1)` / `ui_invalidate_all()` / `ui_dirty_take()`, one
packed `volatile uint32_t` band word (hi16=y0, lo16=y1). `platform_present(void)` →
`platform_present(int y0,int y1)`; device backend does a zero-copy row-offset blit (captures
`s_fb_bpp` from the chosen panel format at init — was previously implicit); host converts +
`SDL_UpdateTexture`s only the band rows. `app.c` wires invalidation at all five `change_seq`
sites: nav/nudge/page/keyguide and MIDI auto-focus → full-screen on page/keyguide change, else
content band (`ui_band_content`); voice-meter + octave → status band (`ui_band_status`, the
note-on/off hot path); held-dir repeat → content band. `render_cb` takes the band with a
full-present fallback when nothing is pending (first frame, or a raced/missed invalidate).

**Three-band mapping:** whole-screen (page/keyguide change) / content `[40,442)` / status
`[442,480)` (`ui/ui.cpp` `CONTENT_Y`/`SCREEN_H`/`STATUS_H`).

**Safety property:** `ui_draw` still fully repaints every frame, so the framebuffer is always
correct — only the blit is narrowed. Failure mode is "present more, never stale": an empty/raced
band falls back to full-screen; a lost union is a superset next bump. This makes the scheme
race-tolerant across the control/render task split.

`make host`/`make test`/`make build` all green, membrane clean. `make size`: flash 0x112700
(≈1097 KB, 46% partition free) — near-neutral as expected (present-side change only). Stage 6a
marked done in `specs/stages/stage-6-display-foundation.md`; row added to the `specs/02` budget
table. **Not yet measured:** on-device PSRAM traffic / block-time `over` count before vs after —
that A/B (`PROFILE=1`, 8-note chord held during redraw) is Pascal's device-verification step;
host has no PSRAM lever to observe. If `over` drops toward 0, that closes the WS3 half of the
open poly-crackle handoff (2026-06-30, above); 6b (`draw_curve` widget) can proceed independently.

## 2026-07-07 — WO-8a: note-on admission cap (COMPLETE, resolves G8a)

Fixed the poly-crackle root cause (see 2026-07-06 handoff, above): a chord landing 4-8 note-ons
in one audio block spiked render time past the 1333us block budget (allocator scan + `note_on()`
init x N), starving the blocking I2S DMA — NOT amplitude/headroom (`gr=1.00` during crackle,
falsified). Fix: `engine/synth_config.h` adds `kMaxNoteOnsPerBlock = 2` (2/block spreads an
8-note chord over 4 blocks ~5.3ms, below strum perception; note-offs stay uncapped). `engine/
spsc_ring.h` gets `SpscRing::peek()` — non-destructive lookahead, purely additive, used by both
the note queue and (available to) the param store. `engine/synth.cpp`'s direct-path drain
(arp_on==false) now uses break-and-leave: once the cap is hit, `peek` the next command; if it's
a `kNoteOn`, stop draining and leave it + everything behind it (including note-offs) queued for
the next block (~1.33ms max extra latency on a trailing note-off, nothing ever dropped). Arp
path (arp_on==true) is untouched — drains everything into `s_arp`, no cap, as before.

New test `tests/host/test_note_cap.cpp` (registered in `CMakeLists.txt`/`main.cpp`): since the
real drain lives inside the IRAM `synth_render` and isn't host-testable in isolation, it exercises
(a) `SpscRing::peek()` correctness (empty, non-destructive, tracks next pop) and (b) a drain
helper mirroring synth.cpp's break-and-leave logic byte-for-byte against the same
`CommandQueue<Cap>` type — 8-note chord spreads over exactly ceil(8/2)=4 drains with none
dropped/duplicated, and a note-on/note-off interleaved burst loses nothing.

`make host`/`make test` green (211/211 — 4 new), `make build` green, `make format` clean,
membrane grep clean (no `esp_`/`bsp_`/`SDL` above `platform/`). `make size`: flash image
1,124,306 bytes total (DIRAM 27.39% used) — near-neutral (control-path change only).

**Remaining verification (Pascal, on-device):** PROFILE A/B — smash 5-8 keys on the Juno EP
patch, watch the `over` (block-time-exceeded) counter before vs after. If `over` drops toward 0
this closes the poly-crackle handoff; if it doesn't, the next lever is Stage 8d (block size
64→128, `-funsafe-math` — each device-bench-gated, G8d) or a deeper per-voice `note_on()` cost
profile.

## 2026-07-07 — Stage 8 diag: per-region CPU sub-timers + voice-meter A/B (COMPLETE, Phase-1)

Key-smash crackle persists **after** Stage 8a. New device profile: idle `audio avg=78 max=80
over=0`; smash `avg=780 max=2743 over=15/750`, `sig gr=1.00` (limiter never engages — amplitude
falsified again). Two stacked CPU problems: sustained `avg=780` = the 8-voice steady render
floor (58% of the 1333us budget); transient `max=2743` (2× budget) = the underruns that crackle.
Stage 8a only spreads note-on cost — can't lower the floor or the spikes. Whole-block profiler
can't say WHERE the cycles go, so Pascal chose **diagnose-first** (PD6) before any Phase-2 fix.

- **`engine/synth.cpp` + `synth.h`**: `SYNTH_PROFILE`-only per-region cycle sub-timers via the
  portable `platform_cycles_now()` seam — `t_drain` (steps 1..1b), `t_voices` (step 5 voice-sum),
  `t_master` (step 6 chain). New `engine_profile_read_cpu(drain,voices,master)` snapshots+resets
  per-block averages (mirrors the amplitude-probe reset pattern). The t1..t2 gap (param push/LFO/
  chorus setup) is unmeasured → shows as `audio_avg − drain − voices − master`.
- **`app/app.c`**: prints `[PROFILE] cpu drain=.. voices=.. master=.. us-per-block`. Also, under
  `SYNTH_PROFILE`, the voice-count meter is routed to `printf("[PROFILE] voices=%d")` **instead**
  of the status-band `ui_invalidate` blit — an A/B to rule out blit contention on voice churn.
  Octave indicator left live (separate change path).
- All new code behind `#ifdef SYNTH_PROFILE` → shipping image byte-unchanged. `make host` +
  `make test` green (211/211), `make PROFILE=1 build` links clean (46% flash free). Commit 8e5c24b.

**RESOLVED 2026-07-07 (device A/B, commit d54500e).** The A/B settled it: with the voice-meter
blit suppressed (routed to printf), smashing to 6 voices gave `over=0 max~633us`; with the blit
live the same smash gave `over=15 max=2743us`. So the poly-crackle spikes are **status-band blit
contention on voice-count churn**, NOT the audio floor. Sub-timers confirm the floor is fine:
6 voices = `voices=539us + master=68us ≈ avg=621us` (< 1333 budget), `drain=0` → Stage 8a
note-on cost is a non-issue and **Stage 8d is not needed**. ~90us/voice → 8 voices stays under
budget.

**Attempted fix (d54500e) — FALSIFIED 2026-07-07.** Added a voice-meter debounce (`app/app.c`,
`VOICE_METER_STABLE_MS=100ms`, commit d54500e). It did NOT stop the crackle: a fresh device run
still shows `over=18 max=2676us` while smashing to 8 voices. Pascal also confirms a **frozen
display (SYNTH_FREEZE_DISPLAY) still crackles** — so it is **NOT blit contention**. The Phase-1
A/B `over=0` was a red herring (that run peaked at 6 voices and didn't hit the pattern). The
debounce is kept (harmless; fewer redundant blits) but is not the fix.

**Re-diagnosis (open).** Audio profile: `avg≈646 max=2676` (2× budget), `over=7..18`, but the
per-region AVG sub-timers sum to the avg (`drain≈0 voices≈560 master≈70`) — i.e. a *typical*
block is fine; the crackle is **one rare block per window** the avg can't localize. 2676us
(~960k cyc) is far too big for note-on transcendentals → smells like a **stall or preemption**,
not steady compute. `voices≈90us/voice`, floor stays under budget even at 8 → not the floor.

**Next diag (commit 3b3af48):** added per-region **MAX** (not just avg) + a 4th `setup` region
(steps 2–4) so the four regions now tile the whole block. `cpu` line is now
`drain=avg/max setup=avg/max voices=avg/max master=avg/max`. **Pascal, on-device:** `make
PROFILE=1 build install run` + `make sniff`, smash 5–8 keys, read the over>0 window's `cpu`
line. Which region's MAX ≈ the whole-block `audio max`? drain→note-on/steal cold code;
voices→per-voice render stall (likely DaisySP flash I-cache, vendor .cpp still in flash per
synth.cpp header); setup→param/arp; none match / all-modest→preemption (needs mcycle-vs-minstret
split next). That picks the real Phase-2 lever.

## 2026-07-09 — Stage 8 diag: voices-region minstret + worst-block snapshot (COMPLETE)

Region already localized: last device paste's per-region MAX (from 3b3af48) shows the `voices`
region MAX ≈ whole-block `audio` MAX in every over>0 window (e.g. `voices=748/2390`,
`audio max=2773`); setup/drain/master stay small. So step 5 (voice-sum) IS the spike region.
Unknown = the **mechanism**. This job adds the discriminator so the next single device run
settles stall-vs-preemption-vs-compute-vs-hot-voice without more guessing (Pascal: "stop
guessing").

- **`platform_instret_now()`** seam (`platform.h` + device + host): device reads RISC-V
  `minstret` CSR (0xB02) low 32 bits via inline asm (esp_cpu.h has no wrapper); `platform_init`
  clears `mcountinhibit` (CSR 0x320) bits 0+2 once under `#ifdef SYNTH_PROFILE` so cycle+instret
  actually count. Host stub returns 0 (IPC is device-only). Diff-within-a-block convention like
  `platform_cycles_now`.
- **`synth.cpp`/`.h`**: read instret at the voices-region boundaries (t2/t3) → `d_voices_instret`;
  per-voice cycle timing inside the loop tracks the single worst voice's `render()` cost + slot
  index + active count (all `#ifdef SYNTH_PROFILE`; shipping loop byte-unchanged). New **worst-block
  snapshot**: keyed on largest `d_voices` in the read-window, freezes {voices_cyc, voices_instret,
  active, vmax_cyc, vmax_idx, drain/setup/master_cyc}. `EngineCpuProfile` extended;
  `engine_profile_read_cpu` hands them back + resets the key each window.
- **`app/app.c`**: new line `[PROFILE] worst voices=..us instret=.. ipc=X.YY active=.. vmax=..us@vN
  | drain/setup/master`. ipc = instret/cycles ×100 (integer, no float).

All new runtime code behind `#ifdef SYNTH_PROFILE` → shipping image unaffected. `make host` +
`make test` (207 pass) ✅ `make build PROFILE=1` links (CSR asm compiles) ✅ `make build` ✅
`make format` ✅ membrane clean. Shipping `application.bin` 0x112990 (46% free).

**Pascal, on-device (the decisive run):** `make PROFILE=1 build install run` + `make sniff`,
smash 5–8 keys on **Juno EP**, find an `over>0` window, read its `[PROFILE] worst` line. Decision
tree:
- **`ipc` low** (≪ a quiet-window block's ipc) with `instret` ~flat → **memory/cache stall**. Then:
  `vmax@vN` ≈ `voices` (one voice = the whole region) → **one cold voice** (PSRAM wavetable /
  cache miss on activation) → prefetch/warm the wavetable. `vmax` small vs `voices` (cost spread
  across voices) → **global stall = flash I-cache XIP** (DaisySP/voice vendor .cpp still in flash)
  → move hot voice/vendor code to IRAM.
- **`instret` high with `active` low** (the silent `voices≈0 → 2257µs` block is exactly this test)
  → **preemption** by another task (USB host / render / IDLE) → chase task priority + core-1
  affinity of the audio task.
- **`instret` high scaling with `active`, `ipc` normal** → **genuine compute** in voice render →
  profile `JunoVoice::render` for a hot path (denormal SVF? branch?).

Whichever fires picks the Phase-2 fix. Commit: (this change).

## 2026-07-09 — Stage 8: poly crackle ROOT CAUSE = flash-XIP I-cache stall → DSP to IRAM (FIX, device-verify pending)

The worst-block profiler (above) settled the poly smash-crackle mechanism. Device readout while
smashing 6–8 on Juno EP:
- `worst voices=2307us instret=179173 ipc=0.21 active=8 vmax=407us@v0` (over>0 blocks)
- vs post-release clean block `voices=619us instret=155624 ipc=0.69 active=7 vmax=93us@v5`

**Same instret, 3.7× the cycles → ipc collapses 0.69→0.21 (~5 cyc/instr = pipeline stalled on
memory fetch).** instret scales with `active` (~22k/voice) so it's real per-voice work, NOT
preemption (which would show high instret at low active — never seen). Cost is **uniform across
all voices** (`vmax` only ~1.4× the per-voice average, not one dominant cold voice) → an
instruction-side stall, not a data/wavetable stall. Confirmed structurally: the per-sample hot
calls — `dsp::Osc::process`→DaisySP `Oscillator::Process` (×2), `dsp::Filter`→`Svf::Process`,
`dsp::Env`→`Adsr::Process` (×2) — all resolve to **out-of-line vendor .cpp in flash**.
`JunoVoice::render` is `IRAM_ATTR` but that places only its OWN code; the DaisySP leaves it calls
stayed in flash, executed via XIP I-cache which thrashes under 8-voice load. (Also explains why it
survived FREEZE_DISPLAY — not core-0 contention.)

This is an **incomplete implementation of ADR 0013** (which requires the whole render call chain,
"the DSP under them", in IRAM — for flash-write survival too; the flash-resident callees were also
a latent preset-save-during-play fault).

**Fix (this commit):** new `main/linker_audio.lf` maps `oscillator`/`svf`/`adsr` objects to IRAM
via the ESP-IDF `noflash` scheme (`.text→iram`, `.rodata→dram`); registered with `LDFRAGMENTS` in
`main/CMakeLists.txt`. **No vendor-source edits.** Map confirms the functions now live at 0x4ff…
(P4 internal RAM); flash `.text` for those objs is 0-size. Cost: **DIRAM +2448 B** (157878→160326,
27.8%, 416 KB free) — ESP32-P4 unified DIRAM has ample room. `chorus.cpp` left in flash (master
region is well under budget). `make build` links (no IRAM overflow), `make host`/`make test` (207)
green, `make format` clean. Flash image 1,008,660 B.

**Pascal, device-verify (self-confirming):** `make PROFILE=1 build install run` + `make sniff`,
smash 5–8 on Juno EP. Expect the `[PROFILE] worst` **`ipc` to climb from ~0.21 toward ~0.6+** and
`voices`/`audio max` to drop below the 1333µs budget with **`over`→~0**. If ipc recovers but `over`
still spikes, next candidates: add `chorus.cpp` + `mtof`/`whitenoise` to the fragment, or the L2
cache config. If ipc stays ~0.21, the stall is elsewhere (data-side) — re-open.

## 2026-07-10 — Stage 8 (final): whole sound-gen path in IRAM (completes ADR 0013)

The prior fix (01dd861) only IRAM'd the three per-voice DaisySP leaves. ELF map still showed
`daisysp::Chorus::Process`/`ChorusEngine::Process` (master chain), `ModMatrix::eval` (block-rate),
`voice_alloc.cpp` (block-rate drain) and every libm transcendental the audio path calls
(`sinf cosf powf expf logf log2f sqrtf fmodf` + kernels/helpers — used by `Svf::SetFreq`, `mtof`,
Chorus's LFO, `dsp/lfo.h`, `dsp/limiter.h`) still resolving to flash (`0x4002…`/`0x4009…`). Any of
these executing flash-XIP can stall core 1 mid-block when a core-0 present/blit burst contends the
shared MSPI — the mechanism behind the poly onset-crackle.

**Fix:** extended `main/linker_audio.lf` — no CMakeLists change needed (LDFRAGMENTS already wired,
libm.a resolves as a normal archive in the link):
- `libmain.a` mapping (existing `oscillator`/`svf`/`adsr`) gained `chorus`, `mod_matrix`,
  `voice_alloc` (whole-object `noflash`; no `IRAM_ATTR` added to the .cpp files — the mapping
  already covers them, and also pulls their `.rodata` to DRAM).
- New `libm.a` mapping: discovered by building once, grepping `application.map` for every
  math symbol still at a `0x4009…` address, then reading its owning archive member (real prefix
  is `libm_a-`, not the guessed `lib_a-`). Final set: `sf_sin sf_cos kf_sin kf_cos ef_rem_pio2
  kf_rem_pio2 wf_pow ef_pow wf_exp ef_exp wf_log ef_log wf_log2 wf_sqrt ef_sqrt wf_fmod ef_fmod
  sf_scalbn sf_finite`. Double-precision siblings (`pow`/`cos`/`sqrt`/… used by ui.cpp/pax-gfx/
  badge-bsp) stayed in flash — not audio-path. `exp2f` isn't linked at all (unused). `fabsf` is a
  hardware instruction (RV32F `fsgnjx`), no mapping needed.

**Map proof:** `Chorus::Process`, `ChorusEngine::Process`, `ModMatrix::eval`, all of
`voice_alloc.cpp`'s symbols, and every listed libm symbol (incl. `__ieee754_*`/`__kernel_*`
helpers) now resolve to `0x4ff1….` (P4 internal SRAM). `ChorusEngine::ProcessLfo` has no standalone
address — it's been inlined into `Process` (also IRAM). Grep for the flash ranges
(`0x4002…`/`0x4009…`) against the target symbol list returns empty.

**Cost:** DIRAM 160326 → 173936 B (+13,610 B; 27.8%→30.17%, 402,528 B / ~393 KB free — ample per
Pascal's directive). `make build` links clean (no IRAM/DIRAM overflow). `make host` and `make test`
(host target doesn't apply the fragment — device-only) both green. `make format` clean (fragment
isn't a clang-format target; no other files touched).

**Pascal, device-verify (self-confirming):** `make PROFILE=1 build install run` + `make sniff`,
smash 6–8 notes on Juno EP. Expect `[PROFILE] worst` **`ipc` 0.22 → ~0.6+**, `voices`/`master`
under the 1333 µs block budget, **`over` → ~0**. If `ipc` recovers but `over` still spikes, next
suspects are `.rodata` const tables still in flash (data-side) or L2 cache config — re-open, don't
patch blindly.

## 2026-07-16 — Crackle diag: split hypothesis (a) DMA underrun vs (b) DSP-glitch (COMPLETE)

Amplitude/clipping theory is FALSIFIED (direct-line recording: single-sample step
discontinuities up to 12.5× RMS slew, zero int16 clamp hits, zero flat-tops — physically not
clipping). Two live hypotheses remain; this stage builds one device run that discriminates them
(commit `c6e96ca`).

- **`engine/synth.cpp`**: the audio RAM tap's trigger predicate changed from "postgain peak >
  `kTapTriggerLevel` (1.2)" to "sample-to-sample step `fmaxf(fabsf(l-prev_l), fabsf(r-prev_r))` >
  `kTapStepThreshold` (0.6f)". New audio-thread-only statics `s_tap_prev_l`/`s_tap_prev_r`.
  `kTapTriggerLevel` removed (dead). Ring/freeze/reader API/app.c dump/`tap2wav.py` unchanged.
  Still fully `SYNTH_PROFILE`-gated.
- **`platform/device/platform_device.c`**: new i2s write-timing starve counter. The main
  blocking `i2s_channel_write` at steady state waits ~one block period for the DMA queue
  (back-pressure = the RT deadline); if a prior render overran, the queue drained and the write
  returns almost instantly. `s_i2s_starve` increments when the wrapped write completes in under
  `kStarveThresholdCyc` (`kAudioBlockBudgetCyc / 2`). Only the main write is wrapped, not the
  silence-flush write. `platform_audio_profile_read()` gained a trailing `uint32_t* out_starve`
  out-param (device: real count; host stub: always 0) — `platform.h` and `app.c`'s 1 s readout
  updated to match (`starve=%u` appended to the `[PROFILE] audio` line).

**Runbook to discriminate a vs b:** `make PROFILE=1 build install run`, `make sniff`, smash keys
(~MASTER_GAIN 0.5) until crackle is audible, wait for `[TAP] end`, then
`python3 tools/tap2wav.py sniff.log -o tap.wav`. Read the `starve=` field on the `[PROFILE] audio`
line and whether the tap froze (a nonzero small `starve` count right at boot/t≈0 is expected
warm-up noise, not a signal).

- Tap froze on a step **and** `starve==0` ⇒ **(b) DSP-domain glitch** (voice-steal / unsmoothed
  param jump / envelope snap) — look at voice allocator steal path and mod-matrix param
  smoothing next.
- `starve>0` (mid-session, not just at boot) **and** tap never froze ⇒ **(a) DMA underrun** —
  look at render budget overruns / block-time spikes next.
- Both ⇒ compound (an overrun triggers both effects).

**Verify:** `make host` ✅, `make test` ✅ (207 assertions, 0 fail), `make build` ✅ — shipping
DIRAM 173,936 B / 30.17% (baseline 174,006 B / 30.19%, effectively unchanged — both new
instruments are fully `SYNTH_PROFILE`-gated, no shipping-image growth), `make PROFILE=1 build` ✅
links, DIRAM 240,904 B / 41.79% (dominated by the pre-existing 64 KiB tap ring). `make format` ✅
(style-only diff, verified rebuild byte-identical). Membrane clean: no new `esp_`/`bsp_` leakage,
`platform/device/platform_device.c` stays C with no engine/dsp includes added.

## 2026-07-16 — Crackle diag RESULT: display + timing/underrun RULED OUT (device run, FROZEN)

Ran the a-vs-b instrument (entry above) on device, both live-display and `FREEZE_DISPLAY=1`.
**Both hypotheses the run was built to test are eliminated. Display is ruled out — definitively,
Nth time — and this time with the cleanest evidence yet.**

**Live-display run:** on note-on/off *bursts* render blew budget — `max` 1871–2490 µs (budget
1333), `over` 7–16/750/s, and `ipc` collapsed 0.72 → 0.23 → 0.08 at *constant* instret (~177 k),
i.e. a memory STALL not extra compute. Overruns correlated with the `voices=N` transition lines
(note events), not voice count (steady 8-voice = 759 µs, `over=0`). This *looked* like blit
contention → underrun.

**FROZEN-display run (`FREEZE_DISPLAY=1`) — the falsifier:** every overrun and IPC collapse
**vanished**. All load levels: `over=0/750`, `max ≤ 769 µs`, `ipc` flat 0.71–0.72, 8-voice steady
760 µs. **And Pascal confirms it STILL crackles with display frozen.** Therefore:
- The live-run overruns / IPC-collapse **are** core-0 display-blit bus contention — REAL, but a
  **red herring for the crackle**. (Worth a perf fix eventually — dirty-rect/Stage-2B — but NOT
  the crackle.) Do not re-open display for the crackle. (Ruled out ~10×; stop.)
- **(a) DMA underrun is DEAD:** frozen `over=0` ⇒ render never overran ⇒ DMA never starved ⇒ no
  codec-repeat step. Crackle persists anyway.
- Crackle occurs under **clean timing**: `over=0`, `max=769 µs`, `ipc=0.72`, `gr=1.00` (limiter
  never engages), `out=0.73` (nowhere near ±1.0 — no clip), signal probe healthy (mono 3.1,
  postg 0.81). Load-independent of the RT deadline.

**The tap never froze** (step-discontinuity trigger `> 0.6f`) in either run — so either the
rendered buffer has no single-sample step ≥ 0.6, or the threshold is too high, or the tap path
itself is suspect. Note the tension with the earlier `gain50.wav` line capture that *did* show
12.5×-RMS steps: that capture is downstream of render (codec/analog), so a clean rendered tap +
crackling line-out would localise the fault to the **codec/i2s/DAC hardware path**, not DSP.

**Also: the i2s starve counter (commit `c6e96ca`) is BROKEN** — reads `starve≈550` constant at
idle (0 voices, silence, `over=0`). The "write returns fast ⇒ starve" heuristic false-positives
because a healthy DMA ring with depth legitimately returns most writes fast. The `starve` field
is meaningless; ignore it. Rip out or replace with an inter-write wall-clock gap detector.

**NEXT (not display, not timing):** get the RENDERED-buffer signal during a frozen-display
crackle and diff it against a simultaneous line-out recording. Make the tap capturable on demand
(manual/keypress freeze, or free-run + dump) since the step trigger doesn't fire. If
tap.wav (render) is clean while the line-out crackles ⇒ fault is post-render: **codec / i2s config
(bit depth, MCLK ratio, sample-rate slip) / analog** — a path never seriously audited. If tap.wav
*also* crackles ⇒ it IS in DSP at clean timing (aliasing / quantization / a small periodic
discontinuity below 0.6), localise in code. See memory note [[crackle-voice-leak-open]].

## 2026-07-16 — Tap gets manual freeze + re-arm; starve counter ripped out

Follow-on to the RESULT entry above: the tap only ever froze on the auto step-discontinuity
trigger (`> 0.6f`), which is proven not to fire for this glitch, so the rendered-buffer capture
needed to close out the codec-vs-DSP split was unreachable. Fixed.

**Manual freeze design:** two new reader-side controls in `engine/synth.h`/`.cpp`,
`engine_tap_freeze_now()` and `engine_tap_rearm()` (both no-op off `SYNTH_PROFILE`). Cross-thread
signalling is two `std::atomic<bool>` request flags (`s_tap_freeze_req`, `s_tap_rearm_req`) —
the control thread only sets a flag, all ring/position mutation stays on the audio thread
(single-writer contract intact). A manual freeze latches a short `kTapManualPostFrames = 64`
(~1.3 ms) post-trigger tail instead of the auto path's half-ring tail, so the frozen ring is
almost entirely *pre*-keypress history — what a human reacting to an audible glitch needs.
Re-arm resets `s_tap_write_pos`/`s_tap_trig_pos`/`s_tap_post_remaining`/`s_tap_prev_l`/`s_tap_prev_r`
then clears `s_tap_frozen` last (release), so the tap can be captured again with no reboot.
Ring doubled `kTapFrames` 16384 → 32768 (128 KiB, ~683 ms @ 48 kHz) to buy more pre-trigger
context now that freeze is keypress-timed rather than glitch-timed; `kTapPostTrigFrames` stays
half-ring for the (still-intact) auto path.

`app/app.c`: under `SYNTH_PROFILE` only, SPACE (ASCII 32) key-down in the input-drain loop calls
`engine_tap_freeze_now()` and prints `[TAP] freeze requested`, intercepted with `continue;` before
keyboard/UI dispatch. Confirmed SPACE is unused by musical typing (`control/keyboard.c` only maps
`a,w,s,e,d,f,t,g,y,h,u,j,k,o,l,p,;`) and unused by `ui/ui.cpp` (only F1/F2/F5, nav keys, `[`/`]`).
The existing tap-dump site now calls `engine_tap_rearm()` after `tap_dump()` — no reboot needed
between captures.

**Starve counter ripped out** (flagged BROKEN in the entry above — false-positived at idle):
`kStarveThresholdCyc`, `s_i2s_starve`, the `wr_cyc_start`/`wr_dc` timing block, and the rationale
comment are gone from `platform/device/platform_device.c`; `platform_audio_profile_read` is back
to 4 out-params (`platform/platform.h`, `platform/host/platform_host.c`, `app/app.c` all updated
to match).

**Verify:** `make host` ✅, `make test` ✅ (207/207, 0 fail, unchanged), `make build` ✅ (shipping
DIRAM unaffected — new code is `SYNTH_PROFILE`-gated), `make PROFILE=1 build` ✅ links, DIRAM
306,050 B / 53.09% (baseline 240,880 B / 41.79%, +65,170 B ≈ the expected +64 KiB ring-doubling,
~264 KB still free — no overflow), `make format` ✅ (cosmetic realignment only), membrane clean
(no new `esp_`/`bsp_` leakage above the HAL). `python3 tools/tap2wav.py --selftest` ✅ — the
`[TAP]` dump format contract is untouched.

**Runbook (for Pascal):** `make PROFILE=1 build install run`, `make sniff`, play until the
crackle is audible, **immediately tap SPACE**, wait for `[TAP] end`, then
`python3 tools/tap2wav.py sniff.log -o tap.wav`. Re-arm is automatic after each dump — tap SPACE
again for another capture, no reboot. Interpretation: diff `tap.wav` (rendered) against a
simultaneous line-out recording — **rendered clean + line-out crackles → codec/i2s/analog fault;
rendered also crackles → DSP-domain glitch at clean timing.**

## Open Opus gates
Sonnet appends a 🛑 gate here when a runbook step needs Opus (see `specs/stages/README.md`).
Opus clears the entry when the gate is resolved.

*(none open — Stage 3d-ii CPU gate cleared 2026-06-29; see archive and `stage-3d-ii-results.md`.)*

✅ Stage 3d-ii (unison / voice CPU cost) — **RATIFIED 2026-06-29 (Opus 4.8)**. Device bench:
  8 voices + worst-case unison+chorus = 50.8% of the 480k-cyc budget (per-voice ~27.5k, fixed
  ~22k) after four transparent perf fixes (-O2 build, block-rate SVF cutoff, block-rate LFO,
  change-gated param push). ADR 0003 stands; no cap needed. Numbers: `stage-3d-ii-results.md`.

✅ Stage 3 — Juno default-patch voicing — **RATIFIED 2026-06-28 (Opus 4.8)**
  Sonic gate during 3b-ii. Pascal chose **"Clean 106"**: matrix default routings =
  `ENV2→cutoff +0.35 LIN` and `LFO1→PWM +0.20 LIN`. Frozen in **ADR 0009 §Default-patch
  voicing**.

✅ Stage 3 — Mod-matrix shape — **RATIFIED 2026-06-28 (Opus 4.8)**
  16 fixed routing slots/patch, record = `{source:u8, dest_param_id:u16, depth:f32, curve:u8}`.
  Frozen in **ADR 0009 §Frozen shape**.

✅ Stage 2 — Master output: soft-clip vs linear headroom — **RATIFIED 2026-06-28 (Opus 4.8)**
  Linear headroom + gentle cubic soft-clip ceiling → **ADR 0016**.

✅ Stage 0.5d — CPU budget & polyphony — **RATIFIED 2026-06-28 (Opus 4.8)**
  Device: ESP32-P4 @ 360 MHz, block 64/48k = 480 000 cyc/blk. **ADR 0003 (8 + unison) stands**.
  Numbers + reasoning: `specs/stages/stage-0.5-results.md`.
