# Stage 1 — One voice (MVP)

**Status:** planned · **Executor:** Sonnet · **Protocol:** [stages/README.md](README.md)
**Blocked by:** Stage 0.5 CPU-budget gate (the per-voice budget sizes this stage).

## Goal
The first **playable** synth: the `SynthModel`/`IVoice` boundary (ADR 0008), one Juno voice
(ADR 0002: osc + sub + noise → VA filter → ADSR VCA), a model-agnostic 8-voice allocator
(ADR 0003), a master chorus, musical-typing note input, and a handful of params on a minimal
PAX page. Stands up the **host DSP test harness** (spec 08). Render chain placed in **IRAM**
(ADR 0013). Replaces Stage 0's sine.

## Reuse — pinned DSP source (Opus-cleared, ADR 0004 / ADR 0014)
**DaisySP** (`electro-smith/DaisySP`, **MIT**), pure portable float C++ → satisfies `dsp/`
purity (ADR 0007). Vendor a thin slice under `dsp/vendor/daisysp/`, **wrap don't edit**.
Record the exact upstream commit hash in `MEMORY.md` at vendor time, and add a row to the
dependency ledger in `specs/02`. Modules used this stage (confirmed present):
- `Source/Synthesis/oscillator.{h,cpp}` — PolyBLEP saw/square/tri (the VA macro-osc mode + the sub).
- `Source/Filters/svf.{h,cpp}` **and** `Source/Filters/moogladder.{h,cpp}` — filter (pick is gated).
- `Source/Control/adsr.{h,cpp}` — amp envelope / VCA.
- `Source/Noise/whitenoise.h` — noise source.
- `Source/Effects/chorus.{h,cpp}` — master chorus starting point.
- `Source/Utility/*` (dsputils, dcblock) as needed.

MI `plaits`/`stmlib` (wavetable/FM macro-osc modes) is **Stage 7**, not now. The `IVoice`
osc is shaped as a macro-osc but only its VA mode is implemented this stage.

## Gate table
| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| 🛑 Filter model: SVF vs MoogLadder | start of 1b | sonic | **SVF** — gives the LP/BP/HP multimode ADR 0002 specifies; keep MoogLadder vendored to A/B for Juno character later |
| 🛑 Per-voice cost > Stage 0.5 budget | end of 1c (on device) | CPU-budget | re-open the budget/polyphony gate with the measured real-voice cost |

Decide-with-default (no gate): voice-steal policy = **oldest-released, then oldest**;
C/C++ seam = keep `synth_render` `extern "C"`, implement engine/dsp in C++; `NoteExpression`
is a minimal struct now (bend/pressure/slide fields, channel-filled), MPE wiring is Stage 5.

## Sub-stages
| id | Deliverable | Ends when |
|---|---|---|
| 1a | Vendor DaisySP slice + `tests/host/` runner (`make test`) + one oscillator aliasing/spectrum test | `make test` green; osc passes |
| 1b | `dsp/` wrappers + `SynthModel`/`IVoice` + one `JunoVoice`; host env/filter tests | voice renders a note in a host test |
| 1c | Engine voice allocator (8) + master chorus + wire `synth_render` → engine | polyphonic render replaces the sine; device cost measured |
| 1d | Musical-typing input + minimal UI page (active voices) | play from the keyboard on host; device green |

### 1a — vendor + test harness
- Vendor the DaisySP slice into `dsp/vendor/daisysp/`; add its sources to **both** builds
  (`host/CMakeLists.txt` and `main/CMakeLists.txt`). Pin + record the commit in `MEMORY.md`.
- Stand up `tests/host/` — a tiny no-framework runner (assert helpers + a `main`) and a
  `make test` target that builds & runs it on the Mac (pure `dsp/`, no platform). FTZ-off
  (ADR 0012). This is the spec 08 host-DSP-test deliverable; keep it dependency-light.
- First test: render a DaisySP PolyBLEP saw at a few pitches, FFT (or a simple Goertzel
  bank), assert aliased energy above Nyquist-image threshold is below a floor. Proves the
  vendored osc + the harness together.

### 1b — the voice
- `dsp/` wrappers expose our blocks over the vendored code (thin, named per our convention:
  `osc_`, `filter_`, `env_`), keeping `dsp/vendor/` un-edited.
- Define the `SynthModel`/`IVoice` interface (ADR 0008 shape) in `engine/`. Implement one
  **`JunoVoice`**: macro-osc (VA mode) + sub-osc (−1 oct) + white noise → mix → filter
  (the gated pick) → ADSR-driven VCA. Allocation-free, block-based `render()` that **adds**
  into the buffer (ADR 0008). Software denormal suppression in the filter/feedback paths
  (ADR 0012).
- Host tests: ADSR shape (attack/decay/sustain/release timing), filter sweep response
  (cutoff/res do the expected thing), voice silent after release + `reset()`.

### 1c — polyphony + master FX + wiring
- Generic **voice allocator** (model-agnostic, reads `make_voice()`): fixed pool of 8
  (ADR 0003), `note_on`/`note_off`, steal policy (default above). Unison hooks may stub
  here; full unison is Stage 3.
- **Master chorus** (DaisySP Chorus) on the summed voice bus — the Juno character (ADR 0002).
- Re-point `synth_render` from the Stage 0 sine to `engine_render` (sum voices → chorus →
  out). Mark the render call chain `IRAM_ATTR`; constant tables → DRAM/PSRAM, not flash
  rodata (ADR 0013). Update the spec 02 placement budget (first real IRAM row).
- **On device**, measure the real per-voice cost (reuse the Stage 0.5 cycle seam) and check
  it against the ratified budget. If it exceeds → 🛑 re-open the budget gate.

### 1d — play it
- Musical-typing: map QWERTY rows to a chromatic keyboard via `platform_poll_event`
  (existing seam) → `note_on`/`note_off`; an octave shift + basic velocity. Lives in a new
  `control/` layer (never touches the audio path directly — mutates via the engine's note
  API). See spec 03.
- Minimal PAX page (extend `ui/`): title, active-voice count/indicator, current octave.
  No param editing yet (that's Stage 2). Keep the fixed-position layout principle (spec 03).
- Verify by ear on host (`make host-run`, play the keyboard); device build green.

## Acceptance
- Press keys → up to 8 simultaneous Juno-ish notes, audible on host; release frees voices.
- `make test` green (aliasing floor + envelope/filter behavior); `make host` + `make build`
  green; device image within the flash/RAM budget (record `make size` in `MEMORY.md`).
- Render chain is `IRAM_ATTR`; membrane grep clean; `dsp/` is pure (no ESP-IDF/I/O/globals).
- Per-voice cost recorded against the Stage 0.5 budget.

## Hand-off to Stage 2
Stage 1 voice params are **hardcoded** constants. Stage 2 introduces the param table and
routes them through it — do **not** build a bespoke param path here; leave clean constants
that Stage 2 can lift into table rows.
