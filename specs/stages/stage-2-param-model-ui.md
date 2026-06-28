# Stage 2 — Parameter model + UI framework

**Status:** planned · **Executor:** Sonnet · **Protocol:** [stages/README.md](README.md)
**Source of truth:** [`specs/05-data-model.md`](../05-data-model.md) (ParamDesc, Patch, versioning).

## Goal
The **single declarative parameter table** that drives UI **and** MIDI **and** presets
(spec 05) — the central dedup mechanism (CLAUDE.md Prime Directive 2; protect it). A single
write path into the audio thread (lock-free ring + smoothing, no mutex — CLAUDE.md RT rules,
ADR 0013-safe). A hybrid panel + pages UI rendered *from the table*. Preset save/load (INIT +
a small factory bank) via the platform storage seam. No new sound — Stage 1's voice, now
fully parameterized and persistable.

## Gate table
| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| 🛑 Param-id namespace + preset format v1 | before 2d writes any file | data-format | Stable `uint16` ids grouped by section with gaps for growth; preset = `{model_id, format_version, [id→value], name}`; ratify in spec 05 before files exist |

Decide-with-default (no gate): per-param smoothing time defaults (use class defaults: fast
for filter/pitch, medium for levels); UI widget styling; ring buffer capacity.

## Sub-stages
| id | Deliverable | Ends when |
|---|---|---|
| 2a | `ParamDesc` table + param store (single-writer ring + smoothing) + host tests | `make test` green: range/curve/smoothing |
| 2b | Route Stage 1's hardcoded voice params through the table | same sound, now table-driven |
| 2c | UI pages rendered from the table (select + nudge + coarse, status strip) | live tweak visible + audible |
| 2d | Preset save/load + INIT + small factory bank (gated format) | round-trip verified host + device |

### 2a — the table + store
- Declare `ParamDesc` (spec 05: id, group, name/short_name, min/max/default, curve, unit,
  display_fmt, midi_cc, smoothing_ms, flags) and the **one** parameter table. Adding a param
  = one row, forever (non-negotiable, spec 02/05).
- **Param store**: `param_set(id, value, source)` applies the curve and pushes to a
  **single-writer lock-free ring**; the audio thread drains it and updates smoothed targets.
  One writer (UI/MIDI/control thread), one reader (audio). No locks, no allocation in the
  audio path. Smoothing per `smoothing_ms` to kill zipper noise.
- Host tests: curve/range correctness, smoothing converges to target in ~`smoothing_ms`,
  ring survives burst writes without tearing. FTZ-off.

### 2b — route the voice through the table
- Replace Stage 1's hardcoded constants with reads of smoothed param values by id (osc mix,
  sub level, noise level, cutoff, res, ADSR, chorus depth/rate). `IVoice::set_param(id,…)`
  is the path (ADR 0008). Verify the sound is unchanged from Stage 1 at default values.

### 2c — UI framework
- Render parameter **pages** from the table, grouped (OSC / FILTER / ENV / LFO / FX / MIX —
  spec 03). Row select; arrow / `,` / `.` nudge; Shift = coarse. Always-visible **status
  strip** (active voices, MIDI activity placeholder, CPU/block load from the Stage 0.5 stat,
  preset name). Fixed positions for muscle memory; no layout shifts (spec 03).
- Hybrid: a panel overview + drill-in edit pages. UI reads the table only — it must not
  know any specific model (ADR 0008).

### 2d — presets
- **Gate the format first** (above). Then implement save/load over the platform **storage
  seam** (SD on device, a file on host — add the seam to `platform.h` if not present;
  device uses BSP/SD, host uses stdio). Serialize **by param id** so the format is
  model-/order-independent (spec 05 versioning). Ship an **INIT** patch + a tiny **factory
  bank** (a few hand-set patches that exercise the range).
- Saving happens while audio may be playing → relies on ADR 0013 (render path in IRAM,
  PSRAM/DRAM data) so a flash/SD write can't glitch audio. Verify a save during playback
  does not drop out (on device).

## Acceptance
- Every MVP knob is exactly **one table row**; UI renders entirely from the table; no
  per-param special-casing anywhere (grep for hardcoded param handling — should be none).
- Tweak a param live → hear it, smoothly (no zipper). `make test` green for store/smoothing.
- Save a preset, change params, load it back → exact round-trip (host and device).
- `make host` + `make build` green; sizes recorded; membrane clean; preset save during
  playback does not glitch audio (ADR 0013 confirmed in practice).

## Hand-off to Stage 3
The table + store + UI are now the substrate. Stage 3 adds modulation **sources** and the
**matrix** as more table rows + a routing structure — it must not fork the param path.
