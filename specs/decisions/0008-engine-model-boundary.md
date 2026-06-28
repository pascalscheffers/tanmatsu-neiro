# ADR 0008 — The synth is a host for swappable SynthModels

**Status:** accepted (2026-06-27)

## Context
We ship a Juno-106 first but must add other engines (Jupiter, FM, wavetable) later
*without a rewrite* (the "multi-engine ready" choice). We must also stay **MPE-ready**
(per-note expression) since retrofitting that into voice allocation is painful.

## Decision
Define a **`SynthModel`** boundary. Everything generic is built once *around* it:

```
SynthModel = {
  param_table   : the model's parameter descriptors (drives UI/MIDI/presets — ADR-data)
  make_voice()  : factory for one IVoice (the per-note DSP graph)
  fx_defaults   : suggested master-FX setup
  meta          : id, name, version
}

IVoice (per-note synthesis, the swappable part):
  note_on(pitch, velocity, NoteExpression)   // expression carries MPE: bend/pressure/slide
  note_off(); reset()
  set_param(id, value)                        // smoothed upstream
  render(float* buf, size_t n)                // adds its output; pulls shared mod values
```

Generic, model-agnostic, written once:
- **Voice allocator** (poly/mono/unison/legato — ADR play-modes), voice stealing.
- **Modulation matrix** (ADR 0009), **master FX bus** (chorus/delay/reverb).
- **Parameter store + smoothing**, **preset (de)serialization**, **sequencer/arp/MIDI**,
  **UI** — all read the active model's `param_table`, never hardcode a model.

The Juno-106 is the first `SynthModel`. Adding an engine = a new `param_table` + `IVoice`,
nothing else.

## MPE / per-note expression
`note_on` takes a `NoteExpression {bend, pressure, slide/timbre, ...}` updated per-note
during the note's life; the mod matrix exposes these as sources. Voices are allocated
per-note (MPE allocates a voice per channel). v1 fills expression from channel-wide
controls; MPE just feeds the same fields per-note — no allocator change.

## Consequences
- A voice never owns shared tables; wavetables live once in PSRAM, indexed by voices.
- `IVoice::render` must be allocation-free and block-based (CLAUDE.md RT rules).
- Slight indirection cost (one virtual call per voice per block) — negligible vs DSP, and
  measured before any concern.
- Mono-timbral for v1 (ADR-timbrality): one active SynthModel + one voice pool. A second
  "part" (split/layer) later = a second model instance + pool; the boundary already allows
  it.
