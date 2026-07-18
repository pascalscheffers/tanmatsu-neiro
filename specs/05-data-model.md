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

## Patch (one sound) — JSON schema (ADR 0027, supersedes the earlier binary format below)
The patch/preset wire format is **JSON, parsed with cJSON** (ESP-IDF `json` component):
```json
{
  "name": "Clean 106",
  "params": { "<id-or-name>": <value>, "...": "..." },
  "routes": [ { "source": "LFO1", "dest": "PWM", "depth": 0.2, "curve": "LIN" } ]
}
```
- `params` keys are the stable `ParamDesc.id` (numeric or its canonical name — the loader
  accepts either so factory banks stay human-readable); missing keys fill from
  `ParamDesc.default`, so the format survives feature growth without a version field doing
  the work. Unknown keys are preserved where safe (round-tripped, not dropped) so a newer
  bank opened by older tooling doesn't lose data.
- `routes` is the modulation matrix (ADR 0009) — `{ source, dest, depth, curve }` — up to the
  fixed max-routings count; a Juno-model patch's *panel* modulation (ADR 0026) is not
  represented here, it is implied by the Juno-specific `params` (DCO-LFO depth, PWM amount,
  etc.) that the hardwired per-voice paths read directly.
- **INIT patch** is just a patch object with all-default `params` and empty `routes`.
  **A/B compare** holds two patch objects and swaps the active one. **Randomize/morph**
  generates or interpolates `params`/`routes` within each ParamDesc's range+curve.
- Macro→target assignments (ADR-perf) are a further top-level array once macros land;
  not yet frozen here.

### Superseded: binary preset format
Earlier drafts of this spec described a compact binary wire format (`model_id,
format_version`, a `values[]` array keyed by `ParamDesc.id`, byte-wise serialized
`routings[]`). **ADR 0027 supersedes this in favor of JSON.** Old NVS/preset blobs written
under the binary format may fail closed to the default factory patch on load — no migration
path is provided.

## Preset storage & browser
- Patches are small JSON files under the storage base path (`/sd/presets/...` on device, a
  local dir on host — ADR 0007). The 128 original Juno-106 patches and the Neiro factory
  bank ship as **JSON banks embedded in firmware flash** (`EMBED_TXTFILES`, ADR 0027); user
  banks are writable `.json` files on SD/AppFS.
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
