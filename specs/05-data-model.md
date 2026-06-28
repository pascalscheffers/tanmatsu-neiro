# Data Model (the central dedup mechanism)

One declarative description of every tweakable, reused by UI, MIDI, presets, modulation,
and sequencing. Get this right and features stop costing edits in four places.

## Parameter descriptor
Each parameter is one row, owned by the active `SynthModel` (ADR 0008):
```
ParamDesc {
  id          : stable numeric id (never reused; presets reference it)
  group       : OSC / SUB / FILTER / ENV / LFO / FX / AMP / GLOBAL …
  name        : "Filter Cutoff"     short_name : "Cutoff"
  min,max,default
  curve       : LIN | EXP | LOG | STEPPED       unit : Hz, %, dB, semitones, …
  display_fmt : how to render the value
  midi_cc     : default CC (remappable; MIDI-learn later)
  smoothing_ms: zipper-noise removal time (0 = none)
  flags       : AUDIO_RATE, PER_VOICE, MOD_DEST, …
}
```
- **UI** renders pages/controls from the table. **MIDI** maps CC→param through it.
  **Presets** serialize values keyed by `id`. **Smoothing** is declared, not re-coded.
- Adding a parameter = one row. This is non-negotiable; protect it from special-casing.

## Parameter store & the single write path
All value changes — UI, MIDI CC, mod matrix, sequencer param-locks, preset load — go
through one `param_set(id, value, source)` that applies the curve and feeds the audio
thread via the lock-free ring (ADR 0007). Audio-rate params are block-smoothed; the audio
context only ever *reads* a resolved value array. Single-writer discipline, no locks.

## Patch (one sound)
```
Patch {
  model_id, format_version
  values[]          : value per ParamDesc.id
  routings[]        : modulation matrix (ADR 0009), fixed max count
  macros[]          : macro→target(s) assignments (ADR-perf)
  meta              : name, category/tags, author, init-flag
}
```
- **Versioned.** A loader migrates older `format_version`s forward (defaults fill new
  params) so the format survives feature growth. Unknown params are preserved where safe.
- **INIT patch** is just a patch with defaults. **A/B compare** holds two patches and
  swaps the active one. **Randomize/morph** generates or interpolates `values[]`/`routings[]`
  within each ParamDesc's range+curve.

## Preset storage & browser
- Patches are small files under the storage base path (`/sd/presets/...` on device, a
  local dir on host — ADR 0007). Factory bank ships read-only (baked in / on SD); user
  banks are writable.
- Browser indexes by category/tag for search/filter. Index is rebuilt from files (or a
  small cached manifest); no database.

## Sequence / pattern model (step + real-time, one structure — ADR 0010)
```
Pattern {
  length (steps/bars), ppqn = 96
  events[] : timestamped in ticks — NoteOn/NoteOff (pitch,vel), ParamLock(id,value), …
}
Song { ordered [ {pattern_ref, repeats} ] }
```
- **Step programming** writes quantized events on the grid; **real-time record** captures
  incoming events at their tick (optionally quantized). Same `events[]` either way — that
  is how "both, layered" is one model, not two.
- **Per-step param locks** are just `ParamLock` events — they reuse the parameter id space,
  so any parameter is automatable for free.
- Playback emits events into the scheduler (ADR 0010); the arp and MIDI-file player are
  other producers into the same scheduler. Stored versioned, like patches.

## Why this shape
- The `id` space is the lingua franca: UI, MIDI, mod dests, and param-locks all speak it.
- Engines stay swappable because none of the above names a parameter directly — they all
  go through the active model's table (ADR 0008).
