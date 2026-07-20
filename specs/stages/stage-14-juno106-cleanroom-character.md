# Stage 14 — Juno-106 clean-room character pass

**Status: authored 2026-07-19. WO-CR-1b dispatchable once its analysis note lands; WO-CR-2
blocks on stage 13 WO-13h (which owns the control-curve note CR-2 consumes); WO-CR-3..8 need
their own analysis note before dispatch (see per-WO "Analysis gate").**

Stage 13 loads all 128 factory patches *correctly* (the data is faithful). This stage makes
them *sound* like a Juno-106: the analog-character DSP the patches assume — a 4-pole
self-oscillating VCF, real BBD chorus, calibrated control curves — that the current model
approximates or lacks. It is the "fidelity / character" companion to stage 13's "data"
work.

**This stage is bounded by one hard licensing rule.** Read the firewall below *before* any
work-order. It is why the stage exists as a documented, two-phase pipeline rather than "port
the good filter from KR-106."

---

## 🔒 The clean-room firewall (read first — this is the whole discipline)

Per memory `juno106-mit-licensing`, ADR 0004, and ADR 0026: **no permissively-licensed
Juno-106 clone exists.** Every good emulation (KR-106 GPLv3, Juno 6, 106.js) is GPL. We stay
MIT. Therefore this stage extracts *behaviour* from **documentation and primary hardware
references only**, never from GPL source. Two source classes, one hard line between them:

| ✅ ALLOWED input (behavioural reference) | ❌ FORBIDDEN input (taints MIT — never open) |
|---|---|
| KR-106 **published prose / design notes** (kayrock.org/kr106): the author's *written* descriptions of the circuits — TPT/IR3109 VCF w/ 2× oversampling, CD4013 sub, 2SC945 noise, 1982-vs-1984 calibration modes. | KR-106 **source code** — any `.cpp/.h/.js`, repo, gist, or pasted snippet. Reading it to reimplement **taints the MIT tree**. |
| Roland **Juno-106 Service Manual / schematics**, IR3109 / IR3R05 datasheets, BBD (MN3009 etc.) datasheets. | Any other GPL/AGPL Juno clone's **source** (Juno 6, 106.js, gruftie). |
| Independent **measurements** (frequency sweeps, scope captures) and standard DSP literature (bilinear transform, TPT/ZDF filters, BBD modelling papers). | GPL clone **generated output** presented as "reference" (curve tables, coefficient dumps) — ADR 0026 bans these too. |

> **Taint line, verbatim from memory:** KR-106's *prose/design notes* are a fine fidelity
> reference; reading its **source** to reimplement is what taints MIT — never do that.

**Firewall consequence for workers:** an implementation work-order in this stage carries a
`Don't-read: KR-106 anything (source, repo, gist), any GPL clone source, live web` line, and
its Read list contains **only** the Neiro analysis note (`specs/notes/juno106-*.md`) plus our
own headers. A worker never fetches primary sources itself — it implements from TARGET values
an analysis phase already distilled into a Neiro note. If a worker finds itself wanting to open
a filter's source "to check," that is the firewall tripping: **stop, return to Opus.**

---

## Two-phase pipeline (this is how each character bit is built)

Classic clean-room uses two teams; ours is stricter (we never read the source at all), so each
character bit is split into two phases that map onto our orchestrate-then-dispatch loop
(ADR 0017):

1. **Analysis (Opus-led / gated).** Read the ALLOWED sources for one character bit → distil a
   **behavioural spec into a Neiro note** (`specs/notes/juno106-<bit>.md`) with explicit
   **TARGET** values (corners, dB, ratios, curves) and **DERIVED** working, tagged exactly as
   `juno106-hpf-analysis.md` does. The note names *what the DSP must do in measurable terms*,
   citing only ALLOWED sources. **This phase never enters a worker prompt** — it is a sonic +
   licensing judgement (an Opus gate). Output = a committed note, no code.
2. **Implementation (Sonnet worker, closed work-order).** A fresh worker takes the note's
   TARGET table and implements the DSP + host tests from it, reusing MIT blocks
   (DaisySP/Mutable/`dsp/`) and clean-room math. Its Read list is the note + our headers; its
   Don't-read is the firewall line above. This is exactly the shape WO-13e-i (HPF) already ran
   — that job is the **worked template** for every implementation WO here.

**`juno106-hpf-analysis.md` is the exemplar of a phase-1 note; WO-13e-i is the exemplar of a
phase-2 worker.** When a work-order below says "produce the analysis note," it means "another
HPF-analysis-shaped note for this bit."

---

## Gate table (up front, per stages/README protocol)

| Gate | WO | Why Opus | Decision to resolve |
|---|---|---|---|
| VCF topology & order | CR-1 | sonic + CPU-budget | Replace 2-pole SVF with 4-pole 24 dB/oct self-oscillating ladder for the Juno model? CPU cost of 2× oversampling on 6 voices. |
| Each analysis note | CR-3..8 | sonic + licensing | The TARGET values themselves (they define the sound) and that every cited source is ALLOWED. Sign-off is an Opus gate. |
| Control-curve note | CR-2 | — | **Not owned here** — that note is stage 13 **WO-13h**'s deliverable + gate. CR-2 only consumes it; do not dispatch CR-2 until 13h's curve note is landed and signed. |
| Self-osc level compensation | CR-1 | sonic | How resonance→self-oscillation gain is normalised against master soft-clip (ADR 0016/0021). |

Everything else (file layout, which test, internal state) is decide-with-default.

---

## Ordered work-orders

Priority = sonic payoff. VCF first (largest audible gap), then control curves (touches every
patch), then the quality passes. Each WO is one worker session inside the ≤~8-file /
≤~5-read budget.

---

### WO-CR-1 — IR3109 4-pole self-oscillating VCF (the big one)

**Split into 1a analysis + 1b implementation. 1a is an Opus gate.**

**Phase 1a — analysis note (Opus-led, gated).**
Produce `specs/notes/juno106-vcf-analysis.md` (HPF-note shape). Distil from ALLOWED sources:
- Topology: IR3109 = **4-pole 24 dB/oct low-pass OTA ladder**, TPT/ZDF form, KR-106 prose says
  **2× oversampled**. Confirm order, slope, self-oscillation behaviour.
- **TARGET**: resonance value at which the filter self-oscillates (sine at cutoff); cutoff→Hz
  mapping; resonance→Q mapping; the level-compensation law so a resonant sweep doesn't clip
  the master (ADR 0016/0021).
- **DERIVED**: coefficient/gain math for a ZDF 4-pole ladder at Fs=48000, whether 2× OS is
  needed on-device or a cheaper 1× topology hits the TARGET response within tolerance
  (CPU-budget gate — 6 voices × 4-pole × 2× OS is the cost to justify).
- 🛑 OPUS GATE — VCF topology & order, self-osc level comp (see gate table). Sonnet action on
  the *implementation* WO: STOP if the note is absent.

**Phase 1b — implementation (Sonnet worker).**
| Field | Value |
|---|---|
| **Touch list** | `dsp/juno_ladder.{h,cpp}` (new 4-pole ladder), `engine/juno_voice.{h,cpp}` (swap the Juno VCF from `dsp::Filter` SVF to the ladder), `tests/host/test_juno_ladder.cpp`, `engine/CMakeLists.txt`/test CMake as needed. |
| **Read list** | `specs/notes/juno106-vcf-analysis.md` (TARGET table); `dsp/filter.h` (the SVF wrapper being replaced + its denormal-injection style); `engine/juno_voice.cpp` VCF-application site; `dsp/vendor/daisysp` ladder (`Filters/ladder.*`) **only if the note says to reuse it**. |
| **Reuse** | Prefer DaisySP `LadderFilter` (MIT) if the note's TARGET response is met; else hand-roll ZDF ladder using standard bilinear/TPT math (NOT from any clone). Denormal injection per `dsp/filter.h` + ADR 0012. |
| **Don't-read** | KR-106 anything (source/repo/gist), any GPL clone source, live web, other stage docs. |
| **Acceptance** | `make host`+`make build`+`make test` green. New test asserts: −24 dB/oct slope past cutoff (±1 dB at 2× fc), self-oscillation at the note's TARGET resonance (sustained tone at cutoff into silence), corner within tolerance, output stays below master-limiter ceiling on a full-res sweep, denormal/NaN-clean. `make size` recorded. Membrane clean. |
| **Split-if** | 2× oversampling turns out mandatory *and* per-voice CPU blows the block budget → stop, return to Opus with the profile number (the CPU-budget gate). |

---

### WO-CR-2 — Runtime control-curve fidelity (consumes WO-13h's note)

> **Not a new note — depends on stage 13 WO-13h.** The control-curve *physics* note,
> `specs/notes/juno106-control-curves.md`, is **WO-13h's deliverable** (stage 13 §WO-13h touch
> list), authored on the *import* side: raw Juno byte `0..127` → decoded `PresetPatch` units.
> This WO does **not** re-author it. WO-CR-2 covers only the **runtime** layer WO-13h does not
> touch: whether the engine's `ParamDesc` normalized→DSP-unit mapping in `JUNO_PARAM_TABLE`
> reproduces the note's TARGET units live (exponential cutoff Hz, exponential ADSR seconds,
> etc.). If the generic `ParamDesc` curves already hit TARGET for every Juno row, this WO is a
> verification test only; where they don't, it fills the gap with Juno-specific curve fields.

**Prerequisite (hard):** `specs/notes/juno106-control-curves.md` exists and its curve TARGETs
are gate-signed — i.e. **WO-13h has landed** (or at minimum its curve note is committed and
Opus-approved). Do not dispatch CR-2 before then; without the note there is nothing to verify
against. This is the CR-2 analysis gate — it resolves in stage 13, not here.

**Implementation (Sonnet worker).**
| Field | Value |
|---|---|
| **Touch list** | `engine/param_desc.cpp` (Juno curve fields in `JUNO_PARAM_TABLE`, only where generic curve misses TARGET), a small `dsp/curve.h` if a shared exp/piecewise helper is warranted, `tests/host/test_juno_curves.cpp`. |
| **Read list** | `specs/notes/juno106-control-curves.md` (13h's TARGET laws — the source of truth); `engine/param_desc.h` (`ParamDesc` curve field); spec 05 param-table section; `engine/param_id.h`. |
| **Reuse** | The existing `ParamDesc` curve mechanism + WO-13h's already-authored conversion functions — reuse, don't re-derive. Table data + at most one helper, not new engine wiring. |
| **Don't-read** | KR-106 anything, GPL clone source, live web; the WO-13h *implementation* (`juno106_patch.*`) — read its **note**, not its decoder. |
| **Acceptance** | Builds/tests green. Test asserts each continuous Juno param's normalized 0/0.5/1 maps to the note's TARGET DSP unit within tolerance; load a known factory patch and assert live cutoff Hz / env seconds match TARGET. Membrane clean. `make size`. |
| **Split-if** | > ~8 params need bespoke (non-shared) runtime curves → split by section (LFO+DCO / VCF / ENV+VCA). |

---

### WO-CR-3 — BBD chorus I / II character

**Analysis gate first** → `specs/notes/juno106-chorus-analysis.md`: TARGET mod rate/depth/delay
per mode (I vs II), mono→stereo spread (real HW is stereo), BBD noise/HPF colour. ALLOWED:
Roland schematic + BBD datasheet + measurement.
**Impl worker:** shape DaisySP `ChorusEngine` (already vendored) to the two TARGET modes; wire
`CHORUS_MODE` 0/1/2 to off / I / II; stereo out preserved through `dsp/limiter.h` (which already
keeps the chorus stereo image). Touch: `engine/synth.cpp` chorus block + a thin
`dsp/juno_chorus.h` config, test. Don't-read: firewall line.

---

### WO-CR-4 — LFO shape + delay law

**Analysis gate** → note: confirm **triangle-only**, the delay/fade curve shape + time range,
free-run vs key-sync across the poly board (ADR 0018 is free-running — confirm against HW).
**Impl:** adjust `dsp/lfo.h` shape/delay to TARGET; test the fade envelope. Small WO.

---

### WO-CR-5 — PWM neutral point + range

**Analysis gate** → note: manual-mode byte→duty-% mapping, LFO-mode sweep depth around 50 %
centre, panel min/max duty reached. **Impl:** map `OSC_PWM` + `PWM_MODE` to TARGET duty in
`juno_voice.cpp` (the `[0.05,0.95]` clamp site in MAP.md line 51); test. Small WO.

---

### WO-CR-6 — Key-follow curve

**Analysis gate** → note: amount→cutoff-tracking law (0=none, full=?%/oct), pivot note, linearity.
**Impl:** implement `VCF_KEY_TRACK` law in `juno_voice.cpp`; test tracking slope. Small WO.

---

### WO-CR-7 — Sub + noise level/colour

**Analysis gate** → note: CD4013 sub square level range vs DCO; 2SC945 **noise colour** (confirm
white) + level scaling. **Impl:** calibrate `SUB_LEVEL`/`NOISE_LEVEL` scaling + noise generator
colour in `juno_voice.cpp`; test relative levels. Small WO. (Sub *shape* is already fixed square
— ADR 0026; this is levels/colour only.)

---

### WO-CR-8 — Analog character & 1982-vs-1984 calibration (quality pass, last)

**Analysis gate** → note: KR-106 prose documents **1982 vs 1984 calibration modes** — capture
what differs (VCF cutoff spread across the 6 voice cards, per-voice detune, DCO drift). TARGET:
how much (if any) spread/drift to model for "fat" vs clean digital voice; whether to expose a
1982/1984 switch. 🛑 sonic gate — this changes the character deliberately.
**Impl:** per-voice detune/cutoff-offset seeding in `voice_alloc`/`juno_voice`; test bounded
spread. Lowest priority — do after 1–7 land.

---

## Sequencing & dependencies

- **CR-1 (VCF)** and **CR-2 (curves)** are the two that change how every patch sounds — do them
  first, in that order (CR-2's cutoff curve depends on CR-1's cutoff→Hz TARGET being fixed).
  **CR-2 additionally blocks on stage 13 WO-13h** (it owns the `juno106-control-curves.md`
  note CR-2 verifies against) — CR-1 can proceed independently.
- **CR-3..7** are independent quality passes, any order, after CR-1/2.
- **CR-8** is last (it layers imperfection on an otherwise-correct voice).
- Each WO's **analysis note is committed before its implementation WO is dispatched** — that
  commit *is* the gate resolution. Update `specs/MEMORY.md` per note and per impl WO.

## Definition of done (stage)

All 128 factory patches audibly read as Juno-106: 4-pole self-oscillating VCF, calibrated
non-linear curves, authentic chorus I/II, triangle LFO with correct delay, correct PWM/key-follow/
sub/noise levels. Every character bit traces to a committed `juno106-*.md` note citing only
ALLOWED sources. Zero KR-106 source (or any GPL clone source) ever entered the tree — verifiable
because no such file exists and every note's citations are ALLOWED-class.
