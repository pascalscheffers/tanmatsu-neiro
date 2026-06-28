# Stage 2 — Parameter model + UI framework

**Status:** planned · **Executor:** Sonnet · **Protocol:** [stages/README.md](README.md)
**Source of truth:** [`specs/05-data-model.md`](../05-data-model.md) (ParamDesc, Patch, versioning).

## Session carry-in (from the Stage 1 wrap session, 2026-06-28)
Things that landed/were learned after this runbook was written — read before 2a:

- **Reuse the existing lock-free ring.** `engine/command_queue.h` already implements a
  single-producer/single-consumer ring (`CommandQueue<Cap>`, std::atomic acquire/release,
  power-of-two, host-tested). The 2a param store needs the *same* mechanism with a
  different payload — **generalise this ring (e.g. template on payload type), do not write a
  second one** (Prime Directive 2). Drain param updates in `synth_render` at the **same
  point** note commands are drained (top of block, before render). Note events already flow
  through this ring (control thread → audio thread); params join it.
- **Master gain is a Stage 2 deliverable and fixes a real defect.** On device the output
  **clips at moderate polyphony** — confirmed float-bus headroom, not integer mixing
  (one voice peaks ~1.05 pre-filter; summed held voices exceed ±1 despite the chorus's
  ×0.25). Add a **master-gain param to the MIX group** in 2b/2c. 🛑 *Soft-clip vs linear
  headroom is a sonic gate* — Pascal deferred it here deliberately; raise it as an OPUS GATE,
  don't pick one. See the `synth_render` header comment + the MEMORY entry.
- **Device input only maps the musical keys + ESC right now.** `platform/device/
  platform_device.c` translates scancodes for `a–;`, `z/x`, and ESC→QUIT only. The 2c UI
  (row select, `,`/`.` nudge, **Shift = coarse**) needs arrows + comma/dot + **shift-modifier
  tracking** added to the device input path. Host (SDL) already delivers these as keysyms, so
  test UI nav on host but **budget time to extend the device mapping** — it is not free.
- **Status strip (2c) gotchas:** `engine_active_voices()` is currently read from the UI
  thread (benign race, fine for a counter — but if you surface it, consider publishing an
  atomic snapshot from the audio thread). And the **live CPU/block load** the strip wants is
  *not* wired into the running synth yet — only the offline Stage 0.5 bench measured it; a
  small running cycle-count in the audio task is new work if you show it.
- **Storage seam does not exist yet** (deferred in Stage 0). 2d adds it to `platform.h` as
  planned — host = stdio file, device = BSP/SD. No surprise, just confirming.

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
| ✅ Master output: soft-clip vs linear headroom | resolved 2026-06-28 | sonic | **RATIFIED — [ADR 0016](../decisions/0016-master-output-soft-clip.md):** linear headroom + a gentle cubic soft-clip ceiling. Implemented as the first item of 2c (below); do **not** stop. |
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

**First, the resolved output-stage gate ([ADR 0016](../decisions/0016-master-output-soft-clip.md)).**
Small, self-contained; do it before the UI work:
- Add **`dsp/saturate.h`** — pure, header-only, no vendor edit. One inline `soft_clip(float)`:
  ```c
  static inline float soft_clip(float x) {
      if (x >=  1.5f) return  1.0f;
      if (x <= -1.5f) return -1.0f;
      return x - x * x * x * (1.0f / 6.75f);   // x - x³/6.75; unity slope at 0, ±1 at ±1.5
  }
  ```
- Apply it in `engine/synth.cpp` step 6 to `left[i]`/`right[i]` **after** `* gain`, before
  they leave `synth_render`. Update the stale `synth.cpp` gate comment to cite ADR 0016.
- Host test (`tests/host/`): transparent below the knee (`soft_clip(0.3) ≈ 0.3`), monotone,
  bounded to ±1 for large input, `soft_clip(±1.5) == ±1`. FTZ-off. No anti-denormal needed
  (no feedback path). No spec 02 budget row (a few flops/sample).
- Leave overt drive for later: ADR 0016 keeps a `MASTER_DRIVE` patch param as a Stage-3
  option — **do not** add a saturator/drive knob now.

Then the UI:
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
