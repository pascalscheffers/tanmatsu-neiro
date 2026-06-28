# Stage 3 — Modulation + full Juno

**Status:** planned · **Executor:** Sonnet · **Protocol:** [stages/README.md](README.md)
**Source of truth:** [ADR 0009](../decisions/0009-modulation-matrix.md) (matrix; Juno routings as a default patch).

## Goal
Modulation depth and the complete Juno voice: a **fixed modulation matrix** (ADR 0009) with
Juno default routings shipped as the default patch; **2 LFOs + 2 envelopes** per voice; the
**full Juno parameter set** exposed as table rows; and the **play modes**
(mono / portamento / unison / legato). Wavetable/FM macro-osc modes remain **Stage 7**.

## Gate table
| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| 🛑 Mod-matrix shape | before 3b | architecture + data-format | Fixed N slots (start N≈8), each `{source, dest, depth}`; mark which dests are **audio-rate** (cutoff, pitch, amp) vs block-rate; freeze before it enters the preset format |
| 🛑 Juno default-patch voicing | during 3b | sonic | Dial the signature Juno default routings/depths (env→VCA, env→cutoff, LFO→pitch/PWM); A/B against reference if available |
| 🛑 Unison cost > Stage 0.5 budget | end of 3d (device) | CPU-budget | re-open budget gate; cap unison voices or reduce poly under unison |

Decide-with-default (no gate): LFO waveform set (tri/saw/square/S&H/sine); env curve
shapes; portamento time range; how mono retrigger/legato is detected in the allocator.

## Sub-stages
| id | Deliverable | Ends when |
|---|---|---|
| 3a | Mod sources: 2 ADSR + 2 LFO per voice, as table-integrated params | sources render + are tweakable |
| 3b | Fixed mod-matrix engine + Juno default routing patch + host routing tests | a routing audibly moves its dest |
| 3c | Full Juno param set exposed; UI pages complete | all Juno controls are table rows |
| 3d | Play modes (mono / porta / unison / legato) in the allocator | each mode plays correctly |

### 3a — modulation sources
- Per voice: a **second ADSR** (filter/mod env) alongside the amp env, and **2 LFOs**.
  DaisySP has no dedicated LFO — reuse the vendored `Oscillator` at sub-audio rate or a tiny
  hand-rolled LFO (waveforms above); it's small and low-risk, but keep it in `dsp/` and pure.
- All depths/rates/shapes are **param-table rows** (Stage 2 substrate), smoothed. No forking
  of the param path.

### 3b — the matrix
- 🛑 **Gate the matrix shape first** (above) — it shapes the audio inner loop *and* the
  preset format, so it's frozen before code/serialization depend on it.
- Implement the **fixed matrix** (ADR 0009): a small array of `{source, dest, depth}` slots;
  sources = envs/LFOs/velocity/key-track/mod-wheel/expression; dests = cutoff, pitch, PWM,
  amp, LFO rate, etc. **Audio-rate** dests (cutoff/pitch/amp) are summed per-block in the
  voice's hot path; block-rate dests update once per block. Denormal-safe (ADR 0012).
- Ship **Juno default routings** as the default patch (🛑 voicing gate). Extend the preset
  format to carry routings (the format-v1 gate from Stage 2 must already account for this —
  if it didn't, that's an ad-hoc data-format gate now).
- Host tests: a routing with depth d moves its dest by the expected amount; zero depth = no
  effect; matrix sums correctly with multiple sources on one dest.

### 3c — full Juno surface
- Expose the complete Juno param set as table rows (osc range/waveform mix/PWM, sub level,
  noise, HPF, filter cutoff/res/env-depth/key-track, both envs, both LFOs, chorus I/II).
  Complete the UI pages (Stage 2 framework) so every control is reachable. Still no
  per-param special-casing.

### 3d — play modes
- In the generic allocator (ADR 0008): **poly** (have it), **mono** (single voice, last/low
  priority), **portamento** (glide pitch), **unison** (stack/detune N voices per note —
  ADR 0003's fatness path), **legato/retrigger** (env behavior on overlapping notes).
- 🛑 On device, measure unison cost against the Stage 0.5 budget; cap if needed.

## Acceptance
- Move an LFO→cutoff (or env→pitch) routing → the dest audibly tracks it; Juno default patch
  sounds recognizably Juno. All Juno params are table rows; UI reaches them all.
- Each play mode behaves (mono priority, audible glide, fat unison, legato env handling).
- Preset round-trip now includes routings + mod params. `make test` green (routing math);
  `make host` + `make build` green; CPU within the ratified budget (recorded); membrane
  clean.

## Hand-off
Stages 4–7 (timing/arp/seq/FX, MIDI I/O, library/capture, second engine) are not yet
detailed — re-plan them with Opus when Stage 3 lands, informed by what Stages 0.5–3 taught
us about real CPU headroom and the preset format.
