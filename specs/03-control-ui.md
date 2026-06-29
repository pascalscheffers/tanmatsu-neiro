# Control & UI

How the player makes notes and tweaks sound. Driven entirely by the parameter table
(`specs/02`) so UI and MIDI never hardcode parameter knowledge.

## Note input sources (any/all → one internal note event stream)
1. **USB-MIDI host (USB-A):** plug in a hardware MIDI keyboard/controller. Primary
   "real instrument" path. Class driver implemented; hardware-verified as of Stage 5b.
2. **USB-MIDI device (USB-C):** play from a computer/DAW. TinyUSB Full-Speed MIDI device;
   hardware-verified as of Stage 5d.
3. **QWERTY keyboard:** built-in "musical typing" (rows = piano keys) so the badge makes
   sound standalone with nothing plugged in. See layout below.
4. *(Later, maybe)* BLE-MIDI over the C6 radio; expansion-header controls.

All sources normalize to one `note_event` (note on/off, velocity, channel) consumed by
the engine — sources are interchangeable and added without touching the engine. Expression
(pitch-bend, mod wheel, aftertouch, sustain CC64, panic) is also handled in Stage 5c.

## Musical-typing key layout (GarageBand-style)

```
   W E     T Y U     O P            ← black keys (sharps)
  A S D F G H J K L  ;              ← white keys: C D E F G A B C D E
```

`Z` / `X` shift the octave down / up (default octave 4, range 1–7).

## UI page model — 9 fixed pages

Pages are defined in a compile-time `PAGE_TABLE[9]` (not runtime-derived) in `ui/ui.cpp`
(`WO-1`, 2026-06-29). Left/Right arrows cycle pages; Up/Down select the row within a page.

| # | Page title | Contents |
|---|---|---|
| 1 | PRESET | Preset browser (scrollable list, audition-with-revert) |
| 2 | PERFORM | Clock tempo (GROUP_GLOBAL) + Arp controls (GROUP_ARP) |
| 3 | OSC | Oscillator parameters (GROUP_OSC) |
| 4 | FILTER | VCF parameters (GROUP_FILTER) + HPF parameters (GROUP_HPF) |
| 5 | AMP ENV | Amplitude envelope ADSR (GROUP_ENV) |
| 6 | MOD ENV | Modulation envelope ADSR (GROUP_ENV2) |
| 7 | LFO | LFO 1 & 2 parameters (GROUP_LFO) |
| 8 | FX | Chorus and effects (GROUP_FX) |
| 9 | AMP | Master level, play mode, portamento, unison (GROUP_AMP) |

Multi-group pages (PERFORM, FILTER) display a **section sub-header** strip (dim background +
magenta accent bar) before each group's rows — purely decorative; row-index math is
unchanged.

## Shape buttons (F1–F6, badge physical buttons)

Left-to-right on the badge face: X · triangle · square · circle · three-lobe · diamond.
All six buttons deliver press **and** release edge events via the platform layer.

| Button | Symbol | Key (host) | Action |
|---|---|---|---|
| F1 | ✕ (X) | F1 | Nudge selected value **down** |
| F2 | △ (triangle) | F2 | Nudge selected value **up** |
| F3 | □ (square) | F3 | **Back** to the PRESET page (page 1); on the PRESET page, cancel an in-progress audition (revert) |
| F4 | ○ (circle) | F4 | **Load / confirm** — commit the highlighted preset on the PRESET page; no-op on param pages |
| F5 | ☘ (three-lobe) | F5 | Toggle the **musical-typing key-guide overlay** (works on any page) |
| F6 | ◇ (diamond) | F6 | **Save** current sound to the user preset slot |

### Hold-to-repeat nudge (F1 / F2)

Implemented in `ui_tick()`, called each frame from `app.c`:

- **Initial delay:** 250 ms before repeat begins.
- **Continuous params:** ramp rate 0.15 norm/s → 0.50 norm/s over a 500 ms ease-in window.
  Full range (0→1) takes ~2 s at peak speed.
- **Stepped params:** one step every 150 ms, no acceleration. Sounds click through one
  discrete option at a time (waveform, play mode, arp mode, etc.).
- Release of F1 or F2 stops repeat immediately. Navigating to a different row also cancels.

## Preset page — audition with revert

The PRESET page (page 1, home) is a special-purpose page, not a param-page:

- **Browse:** ↑/↓ scrolls the list and immediately **auditions** the highlighted preset
  (loads params + routings into the engine live). The first ↑/↓ motion also saves a
  snapshot of the current patch.
- **Confirm (○ or Enter):** commits the highlighted preset; refreshes the snapshot so
  pressing □ after confirming no longer reverts.
- **Revert (□ or Esc):** restores the pre-browse patch from the snapshot and clears
  `auditioning` state.
- **Navigate away (← / →):** reverts first (if auditioning), then passes the page-switch
  through to the navigation handler.

Factory presets are listed first; a "User" row at the bottom refers to the NVS-stored user
preset. The committed preset is indicated with a cyan dot; an audition-in-progress behind a
different committed preset shows a magenta dot.

## Key-guide overlay (F5)

Pressing F5 on any page toggles a centred 680×260 dark panel drawn on top of all other
content. It shows:

- A two-row QWERTY key grid (naturals + accidentals with correct gap slots for E–F, B–C).
- Each cell: key letter + note name at the current octave (e.g. "C4", "F#4").
- Footer: current octave number + `Z`/`X`/`F5` hints.

Note names are derived at draw time from `keyboard_semitone_for_key()` (a thin public
accessor around the existing `key_to_semitone()` table) + `keyboard_octave()`. No table
is duplicated.

## Status strip (bottom of screen, always visible)

Drawn by `draw_status()` in `ui/ui.cpp`:

- 8 voice-activity dots (bright = active).
- Current octave number.
- Loaded preset name.
- Key hints: `<>pg  ^v row  F1/F2 nudge  F3 back  F6 save  ESC`.
- Thin magenta→cyan gradient rule above the strip (synthwave motif).

## Keyboard shortcuts (host / badge QWERTY)

| Key | Action |
|---|---|
| `←` / `→` | Cycle pages |
| `↑` / `↓` | Select row |
| `[` / `]` | Previous / next factory preset |
| `=` | Save user preset |
| `Esc` | Quit to launcher |
| `A`–`;` + `W E T Y U O P` | Musical typing (note input) |
| `Z` / `X` | Octave down / up |

Note: the `,` / `.` nudge keys from the original draft were **retired** in WO-5 (2026-06-29)
and replaced by the F1/F2 shape buttons with hold-to-repeat. `[`, `]`, and `=` remain.

## MIDI expression (Stage 5c)

- **Pitch bend:** ±2 semitones (constant `kPitchBendRangeSemis` in `synth_config.h`).
  Applied per-voice every block; mod-matrix sources also include pitch bend.
- **Mod wheel (CC1):** routed as a mod-matrix source (`ModSource::MOD_WHEEL`).
  LFO1_DEPTH no longer binds CC1 (unbound in Stage 5c-iii to avoid double-bind).
- **Aftertouch (channel pressure):** routed as `ModSource::AFTERTOUCH`.
- **Sustain pedal (CC64):** handled by `control/sustain.{h,c}` — defers note-offs while
  the pedal is down; flushes all held pitches on the down→up edge.
- **Panic (CC120 / CC123):** calls `engine_all_notes_off()` + `sustain_clear()`.
- **Generic CC → param:** `engine_cc_to_param()` scans `JUNO_PARAM_TABLE` for a matching
  `midi_cc` field and calls `engine_set_param_norm()`.

## CC auto-focus (controller follow, 2026-06-29)

When a generic CC moves a mapped param (via `engine_cc_to_param` → `engine_set_param_norm`),
`midi_router_poll` stashes the param id + norm in module-static state. The control loop in
`app.c` reads this with `midi_router_take_param_focus` immediately after each `midi_router_poll`
call and passes the result to `ui_focus_param`. The UI jumps to that param's page/row and
updates the norm shadow so the value bar tracks the knob live.

- **Always on** — no toggle. Every mapped CC automatically retargets the screen.
- **No focus for hardwired CCs** — mod wheel (CC1), sustain (CC64), and panic (CC120/123) are
  handled before the generic-CC path and do not trigger a page jump (mod wheel is not a table
  param; sustain/panic are global actions with no page to jump to).
- **Audition guard** — if the PRESET page is currently auditioning a preview, `ui_focus_param`
  reverts the audition before switching page (same revert path as navigating away manually).
- Implementation: `control/midi_router.{h,c}` (focus state + getter), `ui/ui_focus.cpp`
  (PAX-free; compiled separately to allow host unit tests), `app/app.c` (wire-up).

## Presets (data detail in `specs/05`)
- **Storage:** NVS under key `"user"` for the single user slot. Factory presets are compiled
  in as `const` arrays in `engine/preset.cpp`.
- **Format:** v2 blob — magic "TNMT" header, param pairs (id:u16 + value:f32), then a
  routings block (`count:u16` + N × 8-byte routing records).
- **At boot:** loads the default factory preset by name ("Solo Lead") and applies params +
  routings. If a user preset exists in NVS, it overrides the factory default.

## Still deferred
- MIDI-learn (assignable CC) — easy on top of the param table; schedule with a later stage.
- HPF DSP wiring — `HPF_CUTOFF` is a navigable row but the SVF block behind it is still
  inert (needs a second `dsp::Filter` in `JunoVoice`; its own sub-stage).
- Tap tempo in the UI (the engine clock already supports `engine_tap_tempo()`; no button
  assigned yet).
