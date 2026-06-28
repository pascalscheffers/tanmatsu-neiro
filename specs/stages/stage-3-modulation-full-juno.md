# Stage 3 — Modulation + full Juno

**Status:** planned · **Executor:** dispatched Sonnet worker (ADR 0017) · **Protocol:** [stages/README.md](README.md)
Each sub-stage below carries a **Work-order** block (the closed contract Opus hands the worker).
**Source of truth:** [ADR 0009](../decisions/0009-modulation-matrix.md) (matrix; Juno routings as a default patch).

## Goal
Modulation depth and the complete Juno voice: a **fixed modulation matrix** (ADR 0009) with
Juno default routings shipped as the default patch; **2 LFOs + 2 envelopes** per voice; the
**full Juno parameter set** exposed as table rows; and the **play modes**
(mono / portamento / unison / legato). Wavetable/FM macro-osc modes remain **Stage 7**.

## Gate table
| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| ✅ Mod-matrix shape | ~~before 3b-i~~ **RATIFIED 2026-06-28** | architecture + data-format | **16 slots**, `{source:u8, dest_param_id:u16, depth:f32 bipolar, curve:u8}`; audio-rate dests = pitch/PWM/cutoff/amp. Frozen in **ADR 0009 §Frozen shape**. |
| ✅ Juno default-patch voicing | ~~during 3b-ii~~ **RATIFIED 2026-06-28** | sonic | "Clean 106": `ENV2→cutoff +0.35`, `LFO1→PWM +0.20` (amp env hardwired). Frozen in **ADR 0009 §Default-patch voicing**. |
| 🛑 Unison cost > Stage 0.5 budget | end of 3d-ii (device) | CPU-budget | re-open budget gate; cap unison voices or reduce poly under unison |

Decide-with-default (no gate): LFO waveform set (tri/saw/square/S&H/sine); env curve
shapes; portamento time range; how mono retrigger/legato is detected in the allocator.

## Sub-stages
| id | Deliverable | Ends when |
|---|---|---|
| 3a | Mod sources: 2 ADSR + 2 LFO per voice, as table-integrated params | sources render + are tweakable |
| 3b-i | Fixed mod-matrix **engine** + host routing tests | a routing audibly moves its dest |
| 3b-ii | Juno default routing **patch** + preset format carries routings | default patch sounds Juno; routings round-trip |
| 3c-i | Full Juno param **set** exposed as table rows | every Juno control is a table row |
| 3c-ii | Complete **UI pages** so every control is reachable | all rows navigable on host + device |
| 3d-i | Play modes: **mono / portamento / legato** in the allocator | each plays correctly |
| 3d-ii | Play mode: **unison** (stack/detune) + on-device cost gate | unison plays; cost within budget |

> **Context budget** ([protocol](README.md#keep-the-session-small--fit-one-sub-stage-without-compacting)):
> each sub-stage above is sized for **one no-compaction session**. The split is already baked
> in — `3b`/`3c`/`3d` were each two sessions' worth (engine vs voicing+format; param rows vs
> UI; single-voice modes vs unison+gate), so they're pre-divided. If any sub-stage *still* runs
> heavy, stop at the last green commit and hand off (don't compact). Read spec/ADR *sections*,
> not whole files; use the **Explore** agent for "where is…" searches so their output stays out
> of context.

### 3a — modulation sources
> **Work-order** — Touch: `dsp/lfo.h` (new, pure), `engine/juno_voice.{h,cpp}`,
> `engine/param_desc.cpp`, `engine/param_id.h`, `tests/host/test_mod_sources.cpp` (new).
> Read: ADR 0009 §sources; spec 05 §param rows; `MAP.md` (param table + `dsp/env.h`/`osc.h`
> wrap pattern). Reuse: `dsp/env.h` (`Adsr`), `dsp/osc.h` (`Oscillator` at sub-audio for the
> LFO), the `JUNO_PARAM_TABLE` row pattern. Don't-read: DaisySP source, other stage docs,
> `managed_components/`, UI. Split-if: a band-limited LFO beyond reusing `Oscillator` becomes
> its own sub-stage. Acceptance: sources render + are tweakable; `make test` green;
> `make host`/`make build` green; membrane clean.

- Per voice: a **second ADSR** (filter/mod env) alongside the amp env, and **2 LFOs**.
  DaisySP has no dedicated LFO — reuse the vendored `Oscillator` at sub-audio rate or a tiny
  hand-rolled LFO (waveforms above); it's small and low-risk, but keep it in `dsp/` and pure.
- All depths/rates/shapes are **param-table rows** (Stage 2 substrate), smoothed. No forking
  of the param path.

### 3b-i — the matrix engine
> **Work-order** — Touch: `engine/mod_matrix.{h,cpp}` (new), `engine/juno_voice.cpp` (apply
> mod to dests in the hot path), `tests/host/test_mod_matrix.cpp` (new). Read: ADR 0009 (full
> — small, it's the source of truth); `MAP.md` (`IVoice`, `juno_voice`); spec 02 budget row;
> ADR 0012 (denormal-safe). Reuse: smoothed values from `ParamStore`; the `IVoice::set_param`
> path. Don't-read: preset format (3b-ii), UI, vendor source. Split-if: keep to engine + tests
> — no default-patch voicing, no preset-format change this session. Acceptance: a routing moves
> its dest, zero depth = no effect, sums correct; `make test` green; builds green; membrane clean.
> **Begins only after Opus ratifies the matrix-shape gate.**

- 🛑 **Gate the matrix shape first** (above) — it shapes the audio inner loop *and* the
  preset format, so it's frozen before code/serialization depend on it. This is a hard stop;
  the session naturally begins after Opus ratifies the shape.
- Implement the **fixed matrix** (ADR 0009): a small array of `{source, dest, depth}` slots;
  sources = envs/LFOs/velocity/key-track/mod-wheel/expression; dests = cutoff, pitch, PWM,
  amp, LFO rate, etc. **Audio-rate** dests (cutoff/pitch/amp) are summed per-block in the
  voice's hot path; block-rate dests update once per block. Denormal-safe (ADR 0012).
- Host tests: a routing with depth d moves its dest by the expected amount; zero depth = no
  effect; matrix sums correctly with multiple sources on one dest.
- **Scope note:** engine + tests only. No default-patch voicing, no preset-format change yet
  (that's 3b-ii) — keeps this session to the inner loop and its math.

### 3b-ii — Juno default patch + preset format
> **Work-order** — Touch: `engine/preset.{cpp,h}` (carry routings), `engine/param_id.h`
> (format version), the default-patch/factory data, `tests/host/test_preset.cpp`. Read: spec 05
> §preset format + versioning; ADR 0009 §default routings; `MAP.md` (`preset.{h,cpp}`). Reuse:
> `preset_serialize`/`preset_parse` (serialize **by id**), the factory-bank infra from Stage 2d.
> Don't-read: matrix engine internals (done in 3b-i), UI, vendor. Split-if: if the format change
> is bigger than appending a routings block, split format-bump from voicing. Acceptance: default
> patch sounds Juno; a routing round-trips through save/load (host + device); builds + `make test`
> green; membrane clean. **Begins only after Opus ratifies the voicing gate.**

- 🛑 **Juno default-patch voicing gate** (above): dial the signature Juno routings/depths
  (env→VCA, env→cutoff, LFO→pitch/PWM). Ship them as the **default patch**.
- Extend the preset format to **carry routings** (the format-v1 gate from Stage 2 must already
  account for this — if it didn't, that's an ad-hoc data-format gate now). Verify a routing
  round-trips through save/load (host + device).

### 3c-i — full Juno param set
> **Work-order** — Touch: `engine/param_desc.cpp`, `engine/param_id.h`, `engine/juno_voice.cpp`
> (`set_param` handling for any new rows), `tests/host/test_params.cpp`. Read: spec 05 §param
> table; ADR 0002 (Juno control set); `MAP.md` (param table). Reuse: the `JUNO_PARAM_TABLE` row
> pattern + existing `JunoParam` handling — one row each, no special-casing. Don't-read: UI
> (3c-ii), vendor, matrix internals. Split-if: rows only — if this pulls UI work in, stop.
> Acceptance: every Juno control is exactly one row, settable via the store (host test); builds
> + `make test` green; membrane clean.

- Expose the complete Juno param set as table rows (osc range/waveform mix/PWM, sub level,
  noise, HPF, filter cutoff/res/env-depth/key-track, both envs, both LFOs, chorus I/II).
  Still no per-param special-casing — one row each. **No UI work this session** (that's 3c-ii);
  verify rows exist and are settable via the param store / a host test.

### 3c-ii — complete the UI pages
> **Work-order** — Touch: `ui/ui.cpp` (+`ui/ui.h` if needed); `platform/device/platform_device.c`
> *only if* new nav keys are required. Read: spec 03 §pages + nav; ADR 0008 (UI reads the table,
> knows no model); `MAP.md` (`ui.cpp`). Reuse: the param-page render + row-select/nudge/coarse
> nav from Stage 2c. Don't-read: engine internals beyond the table API, vendor. Split-if: UI only
> — no new params, no engine changes. Acceptance: every 3c-i row reachable + navigable on host
> *and* device; builds green; membrane clean.

- Complete the Stage 2 UI pages so **every** new row from 3c-i is reachable and navigable
  (host + device). UI reads the table only (ADR 0008) — no model knowledge. No new params
  here; this is purely making the existing rows reachable.

### 3d-i — play modes (single-voice)
> **Work-order** — Touch: `engine/voice_alloc.{h,cpp}`, `engine/param_desc.cpp` +
> `engine/param_id.h` (mode + portamento-time rows), `tests/host/test_voice_alloc.cpp`. Read:
> ADR 0008 (allocator), ADR 0002/0003 (modes), `MAP.md` (`voice_alloc`, `NoteExpression`).
> Reuse: `VoiceAlloc`, `NoteExpression`, the param store. Don't-read: unison (3d-ii), UI, vendor.
> Split-if: exclude unison — single-voice modes only. Acceptance: mono priority, audible glide,
> legato env handling each play correctly; builds + `make test` green; membrane clean.

- In the generic allocator (ADR 0008), add: **mono** (single voice, last/low priority),
  **portamento** (glide pitch), **legato/retrigger** (env behavior on overlapping notes).
  **poly** already exists. Unison is deliberately deferred to 3d-ii (it's the heavier,
  gated path).

### 3d-ii — unison + cost gate
> **Work-order** — Touch: `engine/voice_alloc.{h,cpp}` (unison stack/detune),
> `engine/param_desc.cpp` + `engine/param_id.h` (unison count/detune rows). Read: ADR 0003
> (unison/fatness), `stage-0.5-results.md` (CPU budget), `MAP.md` (`voice_alloc`). Reuse:
> `VoiceAlloc`, `kNumVoices`/pool from Stage 1c. Don't-read: vendor, UI. Split-if: if the device
> cost gate fires, cap unison / reduce poly per the gate — don't push past it. Acceptance: unison
> plays (fat, detuned); **on-device** cost within the ratified budget (recorded in MEMORY);
> builds + `make test` green; membrane clean. **Carries the 🛑 device cost gate.**

- **unison** (stack/detune N voices per note — ADR 0003's fatness path) in the allocator.
- 🛑 On device, measure unison cost against the Stage 0.5 budget; cap unison voices or reduce
  poly under unison if needed.

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
