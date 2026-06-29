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
