# Progress Log

The **live** log: recent entries + open gates. Older history is in
[`MEMORY-archive.md`](MEMORY-archive.md). One entry per dispatched job; **append new entries
just above the "Open Opus gates" section** (which stays last). Lean — link to specs, don't
restate. When this passes ~200 lines, rotate older entries into the archive.


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
