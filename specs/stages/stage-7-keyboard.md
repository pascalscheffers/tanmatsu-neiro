# Stage 7 — Keyboard campaign (§1) + instrument view (§6c)

> **Status: pre-runbook campaign brief.** Opus-authored map from `specs/FABLE-THOUGHTS.md` §1
> (keyboard as a button-grid instrument) + §6c (F5 overlay → instrument view). NOT yet closed
> work-orders — resolve the Kickoff gates, then author each sub-stage's closed work-order per
> `stages/README.md` (ADR 0017).
>
> **Execution unit:** one clean Sonnet 5 context per sub-stage. Each is sized to fit one context.
>
> **Depends on Stage 6:** 7c (instrument view) needs WS3 (6a) landed. 7a/7b are pure control-side
> and do not — but ship 7c alongside 7b so the new layout is self-teaching (FABLE §1a UI hint:
> "UI is half of each feature").
>
> Grounding: `specs/FABLE-THOUGHTS.md` §1a/§1b/§1c/§6c. Seams: `control/keyboard.{c,h}`
> (`key_to_semitone`, public `keyboard_semitone_for_key`), `ui/ui_overlay.{cpp,h}` (F5 overlay,
> WO-6), `engine/param_desc.{h,cpp}` + `engine/param_id.h` (the one param table), engine voice
> state for held-note highlighting. The QWERTY grid is a **true rectangle** (no row stagger) —
> scancode→(row,col) is exact.

## The reframe (why this stage matters)
The Tanmatsu QWERTY is not a bad piano; it's a good button-grid instrument. An unstaggered
rectangle is exactly the geometry of the LinnStrument/Push **fourths grid** — every chord and
interval is the same finger-shape in every key. This transforms what the instrument *is*, and
the screen (7c) is what makes the new layouts learnable in one session.

## Where we are entering Stage 7
- `control/keyboard.c` maps keys → semitones via `key_to_semitone()` (public
  `keyboard_semitone_for_key()`), consumed by the F5 overlay (`ui/ui_overlay.cpp`, WO-6) with no
  table duplication. **This single seam is the whole campaign's lever** — generalize it to a
  layout-fn pointer and everything downstream (overlay included) follows.
- Params live in one declarative table (`param_desc.{h,cpp}`, `param_id.h`); adding a stepped
  param = one row + a stable id. Presets serialize by id (`preset.cpp`), so layout/scale params
  persist for free.
- WS3 (Stage 6) has landed, so overlay redraws are dirty-rect, not full-screen.

## Kickoff gates
- **G7a — Layout set + default (sonic/UX). 🛑 OPEN.** Which layouts ship in v1 (Piano / Fourths
  grid / Scale-locked / Chord-row) and which is the **default** `KEYB_LAYOUT`? Recommendation:
  ship all four as stepped options, default **Piano** (no surprise for existing users), Fourths
  grid as the flagship alternative. `ROW_INTERVAL` options +3/+4/+5/+7, default +5 (fourths).
- **G7b — Scale/key-center param ids + preset format (data-format). 🛑 OPEN at 7d.** `SCALE`,
  `KEY_CENTER` are new persisted ids — confirm id block + preset compatibility before 7d.

## Sub-stage decomposition (running order: 7a → 7b → 7c → 7d → 7e)

**7a — Layout dispatch refactor (FOUNDATION, do first).** Turn `key_to_semitone` into a
layout-fn dispatch + a static scancode→(row,col) table. *Seams:* `control/keyboard.{c,h}`;
`KeyPos{row,col}` + `layout_fn` pointer; default layout = today's piano fill (behaviour
unchanged). Host SDL side is already positional (`SDL_Scancode`) — bonus: musical typing stops
breaking on non-US keyboards. *Acceptance:* identical notes to today with piano layout; overlay
still renders. *One-context fit:* yes (one file pair, mechanical).

**7b — Fourths grid layout (§1a, flagship).** Add `KEYB_LAYOUT` (Piano/Grid) + `ROW_INTERVAL`
stepped params; `grid_semitone(p) = p.col + p.row * row_interval`. *Seams:* `keyboard.c` layout
fn, two param rows (`param_desc`/`param_id`), factory defaults. **G7a gates the default.**
*Acceptance:* Grid layout gives fourths tuning; ~2+ octaves across 4 rows; piano unchanged when
selected. *One-context fit:* yes.

**7c — Instrument-view overlay (§6c).** Grow the F5 overlay into a grid-aware live map: every
cell = key cap + note name; held keys light (from engine voice state); root accented in every
octave; chord-name readout ("C#m7") when ≥3 notes held (control-side pitch-class-set lookup).
*Seams:* `ui/ui_overlay.{cpp,h}` renders from the **active layout fn** (not a hardcoded grid);
read held notes via `engine_*`/voice state; a small chord-name table. **Needs WS3 (6a).**
*Acceptance:* overlay reflects the active layout, held-key highlight tracks playing, chord name
correct for common triads/7ths. *One-context fit:* M — **split-if** the chord-name lookup + the
grid render blow budget → `7c-i` (grid render from layout fn + held highlight) / `7c-ii`
(chord-name readout).

**7d — Scale-locked layout + key center (§1b).** Bottom rows = scale degrees (7/octave, "can't
hit a wrong note"). `SCALE` (major/minor/dorian/mixo/penta/blues…) + `KEY_CENTER` (0–11) stepped
params; `scale_layout_semitone()`; overlay shows degree numbers + names, colored by degree.
*Seams:* `keyboard.c` (+ a 12-entry table per scale), two params, overlay degree coloring, status
strip KEY/SCALE badge. **G7b gates the param/preset format.** *Acceptance:* diatonic playing in
the set key; presets carry key+scale. *One-context fit:* yes (S; reuses 7a seam).

**7e — Stradella chord row (§1c).** Top row → diatonic triads of the current KEY_CENTER/SCALE
(I ii iii IV V vi vii°, spice chords further right). One press = `engine_note_on ×3–4`
(control-side loop; audio thread untouched); release = matching note_offs (track per-key note
lists). Overlay labels the row with live roman numerals + concrete names. **Depends on 7d.**
*Acceptance:* one-finger diatonic chords; correct release; with ARP+LATCH = one-finger
accompaniment. *One-context fit:* yes (S once 7d exists).

## Continuous (every sub-stage)
`make size`; host + device green; membrane clean (`control/` and `ui/` never touch the audio
path — chord triggers go through `engine_note_on/off`, the existing public API). Update the
README key table / musical-typing layout when a user-visible layout ships (CLAUDE.md rule).

## First action at kickoff
Read this brief + `control/keyboard.{c,h}` (`key_to_semitone`) + `ui/ui_overlay.cpp` +
`param_desc`/`param_id` conventions. Resolve **G7a**, author the **7a** refactor work-order,
dispatch. 7b/7d/7e are parallel-safe on the 7a seam; 7c ships with 7b.
