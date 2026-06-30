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
