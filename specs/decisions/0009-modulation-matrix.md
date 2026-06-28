# ADR 0009 — Modulation is a matrix; Juno routings are a default patch

**Status:** accepted (2026-06-27)

## Context
We want authentic Juno behavior *and* deeper modulation later, across multiple engines,
without re-plumbing. Chosen: a small general matrix with Juno defaults layered on top.

## Decision
A single **modulation matrix** is core engine infrastructure (not per-model):

```
Routing = { source, dest_param_id, depth, curve }   // fixed-size array per patch
```

- **Sources:** LFO1/2, ENV1(amp)/ENV2(filter/mod), velocity, key-track, mod-wheel,
  pitch-bend, channel/poly aftertouch, MPE pressure & slide, macros (ADR-perf),
  sample-&-hold/random, and sequencer modulation lanes. Per-voice sources (envs, MPE)
  resolve per voice; global sources (wheel, macros) once per block.
- **Destinations:** any model parameter by `dest_param_id`, plus the audio-rate hot
  dests (pitch, PWM, cutoff, amp) handled specially for per-sample smoothness.
- **Rates:** pitch/PWM/cutoff/amp are audio-rate (block-smoothed or per-sample);
  everything else is control-rate (once per block). Keep the matrix evaluation O(routes).
- **Juno defaults:** the factory Juno patch ships pre-wired routings (LFO→pitch/PWM/cutoff
  by mod depth, ENV→cutoff/PWM, etc.) so it behaves like a 106 out of the box; users can
  add/replace routings.

## Default-patch voicing (RATIFIED 2026-06-28, Opus 4.8 — sonic gate, Stage 3b-ii)
The factory Juno default ("Clean 106"): minimal but recognizably 106. The amp envelope is
**already hardwired** in `JunoVoice` (since Stage 1), so the matrix default is *additive*.

| source | dest   | depth  | curve |
|--------|--------|--------|-------|
| ENV2   | cutoff | +0.35  | LIN   |
| LFO1   | PWM    | +0.20  | LIN   |

(2 of 16 slots active; LFO1 = triangle, moderate rate.) Vibrato / extra motion is left for the
user / mod-wheel — not in the default. Depths are normalized (scale the dest's natural range);
exact feel is tunable on device without re-ratifying (it's a default-data tweak, not a shape
change). This is the **INIT/factory default**; the other factory presets may layer more.

## Frozen shape (RATIFIED 2026-06-28, Opus 4.8 — gate "Mod-matrix shape", before Stage 3b-i)
The concrete, preset-format-bearing shape. Frozen here because it sizes the audio inner loop
*and* the persisted preset bytes; do not change without an ADR amendment + format bump.

- **Fixed array of 16 `Routing` slots per patch** (no allocation; bounded, predictable cost).
  16 leaves ~11 free above the ~5 the Juno default patch pre-wires (ADR 0009 defaults).
- **Routing record** (serialize field-by-field, byte-wise like the existing preset format —
  no struct memcpy, avoids alignment/padding UB):
  ```
  struct Routing {
    uint8_t  source;         // ModSource id (0 = NONE → slot inactive)
    uint16_t dest_param_id;  // any ParamId from JUNO_PARAM_TABLE
    float    depth;          // bipolar [-1, +1], scales the dest's natural range
    uint8_t  curve;          // ModCurve id, 0 = LIN (default)
  };  // 8 bytes incl. padding; serialized as 1+2+4+1 = 8 bytes
  ```
  A slot with `source == NONE` **or** `depth == 0` is inactive and skipped in eval.
- **Source ids** (stable, assign in `mod_matrix.h`): LFO1, LFO2, ENV1(amp), ENV2(mod),
  velocity, key-track, mod-wheel, pitch-bend, aftertouch (+ MPE/macros/S&H/seq reserved for
  later — leave id gaps). Per-voice sources (envs, per-voice LFO, velocity, key-track) resolve
  per voice; global sources (wheel, bend, macros) resolve once per block.
- **Audio-rate dests** — pitch, PWM, cutoff, amp — are summed per block and block-smoothed (or
  per-sample) for smoothness. **All other dests are control-rate** (evaluated once per block).
- Eval is **O(active routes)** and **denormal-safe** (ADR 0012). `curve` is a small LUT/shape
  applied to `depth × source` before summing into the dest.
- **Preset (Stage 3b-ii):** append a routings block = `count:u16` + `count × Routing`,
  field-by-field; bump the format version. Unknown source/curve ids skipped forward-compat.

## Consequences
- The matrix is bounded (fixed max routings per patch) → no allocation, predictable cost.
- A new engine inherits modulation for free — it only declares its parameters.
- The UI can start by exposing just the Juno-default knobs and reveal the full matrix
  later (ADR-modulation: "expose deeper over time"), with no engine change.
