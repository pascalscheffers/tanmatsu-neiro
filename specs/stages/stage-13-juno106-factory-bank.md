# Stage 13 — Original Juno-106 factory bank and fidelity pass

> **Status: reworked 2026-07-18 (JSON banks + 6-voice). Implementation-ready through WO-13g-i;
> source gate before WO-13g-ii.** Execute in order as fresh worker jobs. The user ratified the
> original three decisions on 2026-07-18: (1) when the existing Neiro Juno model conflicts with
> the original Juno-106 control/signal model, prefer the Juno-106 unless doing so would break a
> platform invariant; (2) Neiro remains MIT, so no GPL-covered code, data, generated output, or
> adapted implementation enters the repository; (3) protocol constants are derived from primary
> documentation and waveform measurements, with GPL implementations restricted to post-freeze
> validation. The preset wire format, stable parameter IDs, and factory-routing representation
> are not compatibility constraints.
>
> **Rework decisions (2026-07-18, same day):**
> - **License: grant likely, proceed.** Build WO-13g-i now; only WO-13g-ii onward stay gated on
>   the redistribution grant.
> - **WO-13g-i is Opus-led.** The clean-room tape-transport reverse-engineering is R&D, not a
>   closed work-order: Opus derives the transport and authors `specs/notes/juno106-tape-format.md`
>   first, then dispatches a worker to implement the decoder against that note.
> - **Modern JSON bank format.** The preset/bank format is **JSON (parsed with cJSON, the ESP-IDF
>   `json` component — no new firmware dependency)**. *All* patches are JSON: the 128 originals and
>   the Neiro bank are JSON banks **embedded in firmware flash**; user banks are `.json` files on
>   **SD/AppFS**. The old compact-binary runtime pipeline (disposable wire v3 blob, `uint8_t[18]`
>   `.inc`, runtime record decoder, decode-on-request provider) is **replaced**: the tape is
>   decoded **once, offline**, into a JSON bank that ships embedded and is parsed at load. The
>   clean-room tape decoder (13g-i) and curve mapper (13h) survive as **offline build tooling that
>   emits JSON**, not runtime firmware code.
> - **The 12 Neiro factory patches are no longer hardcoded `FactoryPreset` data.** They are
>   re-homed as a **Neiro JSON bank** embedded in flash; a proper **user bank** is introduced.
> - **6-voice polyphony.** Drop `kNumVoices` 8→6 (authentic Juno-106, frees the budget the added
>   per-voice DSP needs). A PROFILE baseline job runs before WO-13c to give split-if a real number.
> - **ADRs:** WO-13a writes **ADR 0026** (Juno fidelity) and **ADR 0027** (JSON bank format,
>   embedded/SD storage, 6-voice).

## Goal

Ship all 128 original Juno-106 factory patches as Neiro's first factory bank, in original
slot order `A11`–`A88`, then `B11`–`B88`, followed by the current 12 Neiro-designed patches
re-homed as a Neiro bank. All patches ship as **JSON banks embedded in firmware flash**,
decoded from the original 18-byte Juno records **once, offline**, into JSON at build time — do
not carry a runtime record decoder or expand patches into duplicated float arrays in flash. The
12 Neiro patches move out of hardcoded `FactoryPreset` data into the Neiro JSON bank, and a
user bank (`.json` files on SD/AppFS) is introduced.

This is also the minimum fidelity pass needed for those patches to mean what their source
data says:

- independent saw and pulse switches, including saw + pulse together;
- the fixed square sub-oscillator one octave below the DCO;
- Juno DCO range, PWM mode, DCO-LFO, VCF-LFO, VCF-envelope and shared-envelope semantics;
- the four-position Juno-106 HPF: bass boost, flat, and two independently
  circuit-derived cut positions;
- one shared, free-running LFO (already ratified by ADR 0018);
- Chorus Off/I/II and the original VCA ENV/GATE switch.

Neiro extensions may remain where they do not distort imported patches. Original patches
must load extension-only state at neutral defaults: poly mode, unison 1, arp off, no glide,
no LFO2 routes, unity master gain, and no general-matrix routes unless a source control
cannot be expressed directly.

## MIT constraint and source gate

- Protocol reference: Roland's `JUNO-106 Owner's Manual`, MIDI Implementation section,
  including the APR message `F0 41 30 0n pp [16 sliders] [sw1] [sw2] F7`.
- The protocol and control meanings may be independently implemented from Roland's
  published documentation. Do not copy a third-party decoder merely because it implements
  the same format.
- Numerical protocol facts and interface constants may be used only when their derivation is
  recorded from Roland documentation or independently measured from the tape waveform.
  Do not copy a GPL implementation's identifiers, comments, tables, control flow, selection
  or arrangement of constants, or implementation-tuning values. Decoder thresholds, filter
  lengths and tolerances are our own measured choices, not borrowed heuristics.
- Legal basis for that boundary: [EU Directive 2009/24/EC](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=celex%3A32009L0024)
  Article 1(2) excludes the ideas and principles underlying program elements and interfaces
  from software copyright, and Article 5(3) permits a lawful user to observe, study and test a
  program to determine those ideas and principles. This plan still uses the narrower clean-room
  rule above; it is an engineering provenance policy, not a claim that arbitrary snippets are
  safe to copy.
- The 128-patch payload must come from a source with explicit MIT/CC0/public-domain terms,
  an explicit redistribution grant, or a documented legal determination that an
  independently captured hardware dump is uncopyrightable parameter data. "Free download"
  and "available on GitHub" are not sufficient licenses.
- KR-106 may identify questions worth independently measuring, but workers must not read or
  use its GPL source, bank file, generated headers, curve tables, circuit-derived constants,
  or code comments. No translation or clean-up of GPL code is permitted.
- A GPL implementation may be run only after the clean-room decoder is frozen, as an external
  validation oracle. Its source is outside every worker read list; its decoded bytes are not
  source input or committed output. Oracle comparison reports only pass/fail, record count and
  independently computed hashes. It is never linked, invoked by the build, or shipped. The GNU
  GPL FAQ says program output is not generally covered unless it copies protected program
  content ([GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#WhatCaseIsOutputGPL));
  Neiro deliberately applies the stricter no-GPL-generated-bank policy anyway.
- Until the source criterion above is met and its bytes, hash, and provenance are pinned in
  this document, WO-13g-ii and later are blocked. WO-13a–13g-i remain executable.
- Preserve Neiro's MIT license. Any proposed source whose terms would cover the combined
  firmware is rejected rather than accepted through a license change.

## Candidate tape evidence (not yet licensed source)

Edy Hinzen's [Juno-106 Connection](http://www.hinzen.de/midi/juno-106/) identifies these two
hardware `TAPE SAVE` recordings. They are useful local evidence and clean-room acceptance
inputs, but the page gives no explicit redistribution license. Do not commit either WAV or
decoded records until the provenance gate is resolved.

| Local file | Site label | PCM shape | Duration | SHA-256 |
|---|---|---|---:|---|
| `junot020.wav` | factory bank A | mono unsigned PCM8, 11,025 Hz | 4.755 s | `0c5d2e93dc98a88ebc66920aa8b1ff805aefeb17771219b9e7b4b06f6b8b8bc3` |
| `junot040.wav` | factory bank B | mono unsigned PCM8, 11,025 Hz | 4.792 s | `542b2c62242ded92d7f0957574cf64c3a9b279a24363df9aaddbb0c9dc35b4d9` |

Both candidates have independently been shown to contain 64 checksum-valid records with
valid header, byte framing and tail, yielding exactly 128 records in `A11`–`A88`, then
`B11`–`B88` order. Treat that only as the expected clean-room result: the implementation must
derive the transport from the waveform and published Juno record shape, then reproduce the
result without copying the validating implementation or its output.

Source hierarchy for all work below:

1. Roland owner/service documentation for control meanings and APR record shape.
2. The two original tape waveforms for independently measured transport timing, framing and
   checksum behavior.
3. Our checked-in analysis notes and synthetic tests for implementation choices.
4. A third-party/GPL decoder only as post-freeze pass/fail validation, never as design input.

## Source record contract

The 18 source bytes are authoritative and remain preserved byte-for-byte:

| byte | Juno-106 control |
|---:|---|
| 0–1 | LFO rate, LFO delay |
| 2–4 | DCO LFO depth, PWM amount, noise level |
| 5–9 | VCF cutoff, resonance, ENV depth, LFO depth, key tracking |
| 10–15 | VCA level, ADSR A/D/S/R, sub level |
| 16 | range bits 0–2, pulse bit 3, saw bit 4, chorus-off bit 5 inverted, chorus I/II bit 6 |
| 17 | PWM manual bit 0, VCF ENV inverse bit 1, VCA gate bit 2, inverted HPF bits 3–4 |

Decode range as 16' = -12 semitones, 8' = 0, 4' = +12. Decode HPF as
`position = 3 - ((sw2 >> 3) & 3)`. Byte 16's chorus representation yields exactly Off,
I, or II. Duplicate the source ADSR into Neiro's amp and modulation envelope state so the
single Juno envelope drives both destinations. All source bytes are 0–127.

## Running order

| Work-order | Deliverable | Effort | Depends on |
|---|---|---|---|
| 13a | ratify licensing, fidelity, JSON-bank format, and 6-voice reset (ADR 0026 + 0027) | medium | — |
| 13-baseline | measure 8-voice PROFILE worst block + `sizeof(JunoVoice)`, then set `kNumVoices=6` | low | 13a |
| 13c | faithful DCO controls, dual waveform mix, and square sub | high | 13a, 13-baseline |
| 13d | direct Juno panel modulation semantics | high | 13c |
| 13e-i | pure four-position Juno HPF block | high | 13a |
| 13e-ii | place HPF in each Juno voice before the VCF | medium | 13e-i |
| 13-fmt | `PresetPatch` value object + JSON bank codec (cJSON), replacing the binary format | high | 13a |
| 13-neiro-bank | serialize the 12 Neiro patches to an embedded JSON bank; drop hardcoded data | medium | 13-fmt, 13c–13e |
| 13g-i | **Opus-led**: document then decode the tape transport (offline tool) | high | 13a |
| 13g-ii | vendor licensed WAVs; offline-generate the 128-patch JSON bank | medium | 13g-i + source gate |
| 13h | offline: decode raw Juno records through calibrated curves into JSON | high | 13d, 13-fmt, 13g-ii |
| 13i | expose 128 originals + Neiro bank + user banks through one provider | high | 13-neiro-bank, 13h |
| 13j | browser labels, documentation, exhaustive smoke verification | medium | 13i |
| 13k | host/device sonic calibration and final acceptance at 6 voices | high | 13j |

The permissive factory-bank provenance gate below is open; no other Opus gates are open. A
worker must still stop on an unlisted licensing, persisted-data, CPU-budget, or material sonic
choice not resolved by this document.

## WO-13a — Ratify the compatibility and licensing reset

**Touch list (7):** `specs/decisions/0026-juno106-factory-bank-and-fidelity.md`,
`specs/decisions/0027-json-bank-format-and-6-voice.md`,
`specs/decisions/README.md`, `specs/02-synth-architecture.md`,
`specs/05-data-model.md`, `specs/09-build-and-run.md`, `specs/MEMORY.md`.

**Read list (5):** this stage through Source record contract; ADR 0002 Decision; ADR 0004
Decision/Consequences; ADR 0009 Frozen shape; ADR 0020 Decision/Future follow-up.

**Reuse:** ADR 0018's shared LFO and the existing model/parameter-table architecture.

**Don't read:** implementation sources, vendor trees, other stage docs, or
`MEMORY-archive.md`.

**Implementation:** write **ADR 0026** as the narrow superseding decision. It supersedes ADR
0002 only where the Juno model's mutually-exclusive hybrid oscillator conflicts with
independent Juno wave switches; reaffirms ADR 0004 and explicitly forbids GPL-derived bank
data or implementation; supersedes ADR 0009 only for hardwired Juno panel modulation and
the pre-canon routing wire shape; and supersedes ADR 0020 by changing the Juno sub to square.

Write **ADR 0027** for the bank/format reset: the preset/bank format is **JSON parsed with
cJSON (ESP-IDF `json` component — no new firmware dependency)**; all patches are JSON; the 128
originals and the Neiro bank ship as JSON banks **embedded in flash** (via `EMBED_TXTFILES`),
user banks are `.json` files on **SD/AppFS**; the 128 originals are decoded from the 18-byte
Juno records **once, offline** into JSON (no runtime record decoder); the 12 Neiro patches move
out of hardcoded `FactoryPreset` data into the Neiro JSON bank; polyphony drops to **6 voices**.
It supersedes spec 05's binary preset format. Record that old NVS/preset blobs may fail closed
to the default factory patch. Update spec 02's dependency/source ledger with the MIT source gate
and cJSON row, spec 05's patch contract (JSON schema), and note the 6-voice PROFILE step in
spec 09.

**Acceptance:** cross-references and both decision-index rows (0026, 0027) are correct;
`git diff --check` is clean; one atomic docs commit; tight MEMORY entry names WO-13-baseline next.

**Split-if:** any planned source or implementation would impose copyleft or lacks clear
redistribution terms. Stop with the exact dependency and license; do not weaken the MIT
constraint or omit notices.

## WO-13-baseline — PROFILE baseline, then drop to 6 voices

**Touch list (4):** `engine/synth_config.h`, `engine/param_desc.cpp` (UNISON docs referencing
`kNumVoices = 8`), `specs/MEMORY.md`, and any test asserting an 8-voice pool.

**Read list (3):** this work-order; `engine/synth_config.h:kNumVoices/kNoteOnStartIntervalBlocks`;
`specs/09-build-and-run.md:PROFILE worst-block procedure`.

**Reuse:** the existing `PROFILE=1` worst-block instrumentation and `make size`.

**Don't read:** DSP internals, KR-106, UI, unrelated stages.

**Implementation:** first record the **current 8-voice** PROFILE worst render block,
`sizeof(JunoVoice)`, and audio-side DIRAM in MEMORY as the split-if reference for WO-13c/13e.
Take the reading with the display quiescent (the display-blit ipc-collapse, [[ipc-collapse-rt-spikes]],
otherwise masks it). Then set `kNumVoices = 6` (authentic Juno-106); revisit
`kNoteOnStartIntervalBlocks` for the smaller pool and update the UNISON doc comments. This is a
config + docs change; no DSP algorithm change.

**Acceptance:** baseline numbers recorded; `kNumVoices == 6`; `make format`/`make test`/
`make host`/`make build`/`make size` and membrane grep pass; UNISON and note-on-interval
comments are true; atomic commit and MEMORY entry naming WO-13c next.

**Split-if:** dropping to 6 voices needs a change outside the touch list (e.g. a fixed-8
assumption in the allocator or a preset). Stop with the exact site.

## WO-13-fmt and WO-13-neiro-bank — JSON bank format (replaces the old WO-13b/13f)

The old "split the preset module" (13b) and "disposable wire v3 blob" (13f) work-orders are
**superseded** by the JSON-bank rework. Two jobs replace them; both are authored in full before
dispatch, against ADR 0027:

- **WO-13-fmt** — introduce a fixed-capacity `PresetPatch` value object and a **JSON bank codec
  on cJSON** (`engine/bank_json.{h,cpp}`), with `preset.cpp` as the JSON facade. Bank schema: a
  JSON array of patches `{ "name": str, "params": { id-or-name: value, ... }, "routes": [ ... ] }`.
  Reuse fixed `PRESET_MAX_PARAMS`/`PRESET_MAX_ROUTINGS`; parse off the audio thread; no heap in
  the audio path. Device uses the ESP-IDF `json` component; the host build gets a small cJSON
  shim. Fail closed on malformed/unknown-ID/truncated JSON.
- **WO-13-neiro-bank** — serialize the current 12 Neiro patches to an embedded JSON bank
  (`EMBED_TXTFILES`) and **delete the hardcoded `FactoryPreset` data**. Boot default resolves by
  name from a bank (preserve INIT semantics). Add user-bank load from a `.json` file on SD/AppFS.

Full touch/read/reuse/acceptance/split-if for these two are finalized when the fidelity front
half (13c–13e) lands, since the param IDs they serialize change there.

## WO-13c — Faithful DCO wave switches and sub oscillator

**Touch list (8):** `engine/param_id.h`, `engine/param_desc.cpp`,
`engine/juno_voice.h`, `engine/juno_voice.cpp`, `dsp/osc.h`,
`tests/host/test_osc_waveform.cpp`, `tests/host/test_params.cpp`,
`specs/MEMORY.md`.

**Read list (5):** this work-order; ADR 0026 oscillator section; `engine/juno_voice.h:oscillator
state`; `engine/juno_voice.cpp:init/set_param/render oscillator mix`; `dsp/osc.h:Osc`.

**Reuse:** `dsp::Osc`, the fixed voice pool, parameter table, block rendering, and existing
denormal/finite guards.

**Don't read:** DaisySP internals, preset implementation, UI code, KR-106 DSP, or platform.

**Implementation:** replace the exclusive `OSC_WAVEFORM` behavior in the Juno model with
independent preset-eligible `OSC_SAW_ON` and `OSC_PULSE_ON` stepped parameters. Retire the old
ID without reusing it. Maintain one phase-coherent saw oscillator and one pulse oscillator;
both may sound simultaneously. Keep `OSC_PWM` as the pulse-width/amount control pending
WO-13d. Constrain DCO range to 16'/8'/4'. Change the sub oscillator to a fixed 50% PolyBLEP
square at one octave below the DCO. Remove triangle from this model; it belongs in a future
non-Juno `SynthModel`. Use named fixed mix gains and test saw-only, pulse-only, both, neither,
range, square-sub spectrum, finite output, and no allocations in render.

**Acceptance:** both-wave output is demonstrably the sum of two active sources without
phase reset on parameter changes; sub is square; all tests/builds/format/size/membrane checks
pass; no RT-rule violation; atomic feature commit and MEMORY entry.

**Split-if:** the extra oscillator makes the eight-voice PROFILE worst block exceed the audio
budget or causes internal-RAM pressure. Stop with measured cycle/DIRAM deltas; do not replace
it with a hand-written oscillator without a new decision.

## WO-13d — Direct Juno panel modulation semantics

**Touch list (7):** `engine/param_id.h`, `engine/param_desc.cpp`,
`engine/juno_voice.h`, `engine/juno_voice.cpp`, `engine/mod_matrix.h`,
`tests/host/test_mod_sources.cpp`, `specs/MEMORY.md`.

**Read list (5):** this work-order; ADR 0026 modulation section;
`engine/juno_voice.cpp:set_param/render modulation`; `engine/mod_matrix.h:virtual destinations
and ModOutputs`; ADR 0018 Decision.

**Reuse:** the injected shared LFO value, per-note LFO delay fade, both envelopes, and the
general matrix as an additive extension.

**Don't read:** `synth.cpp`, preset code, KR-106 DSP, UI, DaisySP internals, or platform.

**Implementation:** add preset-eligible Juno controls for `DCO_LFO_DEPTH` and `PWM_MODE`
(LFO/Manual), and make the existing VCF ENV/LFO/key controls authoritative direct panel
paths. In LFO PWM mode, interpret `OSC_PWM` as modulation amount around the hardware-neutral
center; in Manual mode, interpret it as fixed pulse width. Apply DCO-LFO directly to pitch.
The original single ADSR is represented by equal ENV1/ENV2 values on import, while Neiro may
still edit them separately afterward. Remove the INIT patch's unconditional LFO→PWM and
ENV2→cutoff matrix defaults once their panel paths would double-apply; matrix routes remain
optional additive modulation. Name any retained virtual destinations generically.

**Acceptance:** direct depths at zero are neutral; maximum depths are bounded; PWM modes are
audibly/distinctly tested; no factory patch requires a route merely to express an original
panel control; all builds/tests/format/size/membrane checks pass; atomic commit and MEMORY.

**Split-if:** shared LFO rate/depth ownership cannot be changed without `synth.cpp`. Stop and
split only that ownership wiring into a follow-up, keeping this work-order's contract.

## WO-13e-i — Pure Juno-106 four-position HPF

**Touch list (8):** `dsp/juno106_hpf.h`, `dsp/juno106_hpf.cpp`,
`tests/host/test_juno106_hpf.cpp`, `main/CMakeLists.txt`, `host/CMakeLists.txt`,
`tests/host/CMakeLists.txt`, `specs/notes/juno106-hpf-analysis.md`,
`specs/MEMORY.md`.

**Read list (5):** this work-order; ADR 0026 HPF section; the Roland Juno-106 service
schematic HPF section pinned by ADR 0026; `dsp/dcblock.h` denormal style;
`dsp/filter.h` pure-DSP conventions.

**Reuse:** the published component topology, standard RC/biquad equations, and existing
permissive pure-DSP conventions. Independently calculate and record the two cut frequencies
and bass-boost transfer function before coding. Copy no third-party implementation or
precomputed coefficient table.

**Don't read:** KR-106 or other GPL implementations, voice/engine sources, DaisySP vendor,
UI, or platform.

**Implementation:** first record the schematic nodes, component values, equations and
calculated responses in the analysis note. Then create a fixed-state, allocation-free block
with click-safe mode changes and software denormal protection. Position 0 uses the
independently derived bass-boost biquad; position 1 is unity apart from required numerical/DC
hygiene; positions 2/3 are first-order, not the existing two-pole SVF. Expose init,
set-position and process only.

**Acceptance:** response tests prove the two cutoffs within tolerance, flat unity, expected
bass-boost shape, finite silence, bounded switch transient, and no denormals; all standard
builds/format/size/membrane checks pass; atomic MIT DSP commit and MEMORY.

**Split-if:** the schematic is unavailable/ambiguous, the derivation cannot be reproduced
from the note, or the filter needs more than two additional state elements per voice. Stop
before borrowing an external implementation or substituting an arbitrary shelf.

## WO-13e-ii — Wire HPF before the VCF

**Touch list (5):** `engine/param_desc.cpp`, `engine/juno_voice.h`,
`engine/juno_voice.cpp`, `tests/host/test_voice.cpp`, `specs/MEMORY.md`.

**Read list (4):** this work-order; `engine/juno_voice.h:filter state`;
`engine/juno_voice.cpp:init/set_param/render signal order`; `dsp/juno106_hpf.h:public API`.

**Reuse:** `HPF_CUTOFF`'s existing ID, renamed/displayed as a four-step position, and the
new pure HPF block.

**Don't read:** preset/UI implementation, KR-106 sources, other DSP, or platform.

**Implementation:** make `HPF_CUTOFF` stepped 0–3 and place one HPF instance per voice after
osc/sub/noise mixing and before the VCF. Remove the stale continuous/inert comments. Do not
place this on the master bus. Verify every position through the voice path.

**Acceptance:** signal order and four responses are tested; mode changes remain bounded;
all builds/tests/format/size/membrane checks pass; atomic commit and MEMORY.

**Split-if:** voice-state growth threatens internal SRAM or HPF processing materially breaks
the measured audio budget. Stop with `sizeof(JunoVoice)`, DIRAM and PROFILE deltas.

## WO-13f — SUPERSEDED (folded into WO-13-fmt / WO-13-neiro-bank)

The disposable binary wire v3 blob is replaced by the JSON bank format (ADR 0027). The
`PresetPatch` value object and the codec now live in **WO-13-fmt**, and the user-blob / boot-
default behavior in **WO-13-neiro-bank** (JSON, fail-closed to the named default). See those
sections above; the `ui/ui_presets_state.cpp` audition wiring stays in WO-13j.

## WO-13g-i — Clean-room tape transport decoder

**Ownership: Opus-led.** The transport reverse-engineering is R&D, not a closed job. Opus
derives conditioning/symbol-decision/pilot/framing/checksum from the two candidate waveforms and
writes `specs/notes/juno106-tape-format.md` (every constant tagged Roland-fact or our
measurement) *first*; only then is a worker dispatched to implement the decoder against that
frozen note. The worker touch/read lists below apply to that implementation job.

**Touch list (4):** `tools/decode_juno106_tape.py`,
`tests/tools/test_decode_juno106_tape.py`, `specs/notes/juno106-tape-format.md`,
`specs/MEMORY.md`.

**Read list (5):** this work-order and Candidate tape evidence; Roland Owner's Manual
`Tape Interface` and `MIDI Implementation: APR`; the two local candidate WAV headers and
waveforms; `tools/sniff-console.py:argparse/main CLI style`; `specs/08-embedded-practices.md:golden
tests`.

**Reuse:** Python standard library `wave`, `struct`, `hashlib` and `argparse`; the Source
record contract above; local candidate hashes as identity checks. This is an offline authoring
tool only and adds no firmware dependency.

**Don't read:** KR-106, the Java librarian, or any other third-party tape decoder, source,
decompilation, constants, comments, decoded bytes or generated bank; engine/DSP/UI sources;
unrelated WAVs. Do not search the web for decoder implementations.

**Implementation:** analyze the candidate waveforms first and record a reproducible transport
description: conditioning, symbol decision, pilot/sync acquisition, bit order, byte framing,
record boundary, checksum and tail. For every numeric constant, record whether it is a Roland
protocol fact or our own waveform measurement, including the measurement method and tolerance.
Then implement a streaming, sample-rate-aware decoder which emits only canonical 18-byte
records plus bank/slot metadata. It must tolerate DC offset and moderate gain/sample-rate
changes without encoding assumptions about these exact PCM8 containers. Keep checksum and
framing validation mandatory; never guess or repair payload bytes silently.

Tests use a small independently constructed synthetic symbol fixture and mutations for bad
pilot, invalid symbol length, byte framing, truncation and checksum. The uncommitted local
candidate WAVs are an additional acceptance run, not test fixtures. Freeze the implementation
and commit it before any GPL oracle is run. After freeze, an orchestrator may compare only
record count and whole-bank SHA-256 against an external decoder; do not feed its records back
into implementation or tests.

**Acceptance:** each exact candidate hash decodes deterministically to 64 records; combined
slots are exactly `A11`–`A88`,`B11`–`B88`; all records are 18 bytes and seven-bit clean;
header, framing, checksum and tail validation pass; resampled/gain/DC-offset variants decode
identically; every corrupt synthetic case fails closed with a precise error. Stdlib-only tests,
`git diff --check`, `make test`, `make host` and `make build` pass; one atomic MIT tooling/docs
commit and tight MEMORY entry. Record the two independently decoded whole-bank hashes in the
analysis note only after this acceptance is green.

**Split-if:** the transport cannot be derived reproducibly from the two waveforms and Roland
record shape, or a proposed constant is known only from GPL code. Stop with the missing fact;
do not inspect or translate a third-party decoder to fill the gap.

## 🛑 OPUS GATE — Permissive factory-bank provenance

**Why Opus:** licensing/data provenance.

**Decision:** establish an explicit MIT/CC0/public-domain redistribution grant for the exact
Hinzen `junot020.wav` and `junot040.wav` captures (hashes above), obtain equivalent independent
hardware captures with such a grant, or record a documented legal determination covering
redistribution of the exact waveforms and decoded parameter records. Pin the grant/source and
SHA-256 here before WO-13g-ii is dispatched.

**Recommendation:** first ask Edy Hinzen for a CC0 or MIT grant covering the two WAVs and their
decoded 128 parameter records. Otherwise obtain a new Juno-106 hardware/tape capture under an
explicit grant. Do not treat public download access as a license, and do not substitute the
GPL KR-106 bank or GPL-decoder output.

**Sonnet action:** STOP. WO-13a–13g-i are not blocked; WO-13g-ii onward are blocked.

> **JSON-rework note (ADR 0027) — applies to WO-13g-ii, 13h, 13i below.** These sections were
> written for the compact-binary runtime pipeline and are superseded in representation, not
> intent: the tape is decoded **once, offline**, through the calibrated curves (13h) into a
> **JSON bank** (`third_party/juno106-factory/bank.json`-style, embedded via `EMBED_TXTFILES`),
> not a `uint8_t[18]` `.inc` with a runtime decoder. So `engine/factory_juno106_data.inc` becomes
> an embedded JSON bank; `engine/juno106_patch.*` becomes offline tooling (a Python step in
> `tools/`) that emits `PresetPatch`-shaped JSON; the runtime path is a single cJSON parse. The
> unified provider (13i) exposes the originals JSON bank, the Neiro JSON bank, and user banks. The
> clean-room decode + curve provenance + hash/checksum discipline is unchanged. Final
> touch/read/acceptance are settled at dispatch, after the source gate resolves and 13-fmt lands.

## WO-13g-ii — Vendor licensed tapes and generate the compact bank

**Touch list (8):** `third_party/juno106-factory/bank-a.wav`,
`third_party/juno106-factory/bank-b.wav`,
`third_party/juno106-factory/LICENSE`, `third_party/juno106-factory/SOURCE.md`,
`engine/factory_juno106_data.inc`,
`tests/host/test_juno106_factory_source.cpp`, `tests/host/CMakeLists.txt`,
`specs/MEMORY.md`.

**Read list (5):** this work-order and resolved source gate; the ratified WAVs and license;
`tools/decode_juno106_tape.py:CLI/output contract`; WO-13g-i's independently decoded bank
hashes; `tests/host/CMakeLists.txt:test registration style`.

**Reuse:** the frozen clean-room tape decoder, Python standard library, source record contract,
candidate identity hashes and resolved redistribution grant.

**Don't read:** KR-106 or any other GPL bank/decoder/generator/plugin/DSP source or decoded
output, engine preset code, UI, or platform.

**Implementation:** vendor the exact two licensed WAVs, grant/license, and a SOURCE note with
the site/capture origin, both SHA-256 values, authorization text/date, PCM facts and the
clean-room decoder version. Extend the decoder's existing CLI only if needed to generate one
deterministic compact include from bank A then B; any such edit must be added to this touch
list before dispatch. Reject any hash mismatch, decode warning, checksum error, non-64 bank,
non-18-byte record or non-seven-bit value. Map records to canonical labels
`A11..A88,B11..B88`; do not import descriptive names unless separately granted. The include
contains only slot labels and `uint8_t[18]` records—no floats or runtime decoder. `--check`
must prove the checked-in output is current from both WAVs.

**Acceptance:** source hashes and resolved grant match; decoding reproduces WO-13g-i's frozen
whole-bank hashes; `--check` passes; generated data is exactly 2,304 parameter bytes plus
names/struct overhead; source test deliberately rejects a payload-symbol corruption that
breaks framing/checksum; standard builds/tests/format/size checks pass; atomic vendor commit and
MEMORY.

**Split-if:** the ratified source does not match its hash/authorization, the frozen decoder,
or exact 2×64-record shape. Stop; do not silently substitute GPL output or a merely
downloadable bank.

## WO-13h — Decode Juno records with calibrated curves

**Touch list (8):** `engine/juno106_patch.h`, `engine/juno106_patch.cpp`,
`tests/host/test_juno106_patch.cpp`, `main/CMakeLists.txt`, `host/CMakeLists.txt`,
`tests/host/CMakeLists.txt`, `specs/notes/juno106-control-curves.md`,
`specs/MEMORY.md`.

**Read list (5):** this work-order and Source record contract;
`engine/preset.h:PresetPatch`; `engine/param_desc.cpp:Juno rows`; Roland Owner's Manual
`MIDI Implementation`; Roland's published Juno-106 control ranges pinned by ADR 0026.

**Reuse:** parameter descriptors for ordinary normalized mapping, shared-envelope duplication,
direct Juno panel params from 13c/13d, and fixed-capacity `PresetPatch`.

**Don't read:** KR-106 or other GPL decoders/curves/generated headers/DSP, UI, platform, or
unrelated tests.

**Implementation:** decode one raw record into a complete neutral-initialized `PresetPatch`.
Use small named conversion functions for the Juno-106 slider curves; do not scatter `/127`
or magic times/frequencies. Independently document each mapping and its published range or
measurement basis in the control-curves note. At minimum cover LFO rate/delay, DCO/VCF
modulation depths, VCF cutoff, ADSR times, and VCA level. Where no authoritative curve is
available, use the simplest monotonic mapping through Neiro's parameter descriptor and mark
it for hardware calibration rather than borrowing a third-party table. Map all switches
exactly. Duplicate A/D/S/R to both envelopes. Set master gain to unity and emit zero matrix
routes.

**Acceptance:** exhaustive tests cover all 128 records and every switch combination present;
selected patches assert exact decoded controls; all values are within their descriptor ranges;
no record produces NaN/Inf; decoder allocates nothing; all standard verification passes;
atomic MIT decoder commit and MEMORY.

**Split-if:** faithful VCF/envelope conversion requires replacing the DSP algorithm rather
than mapping its controls. Stop with the mismatch and an isolated follow-up proposal; do not
copy any third-party voice engine into this job.

## WO-13i — Unified factory provider

**Touch list (8):** `engine/factory_juno106.h`, `engine/factory_juno106.cpp`,
`engine/preset.cpp`, `tests/host/test_preset.cpp`, `main/CMakeLists.txt`,
`host/CMakeLists.txt`, `tests/host/CMakeLists.txt`, `specs/MEMORY.md`.

**Read list (5):** this work-order; `engine/preset.cpp:factory facade/default`;
`engine/factory_neiro_presets.h:private provider`; `engine/juno106_patch.h:decoder`;
`engine/factory_juno106_data.inc:generated shape`.

**Reuse:** `PresetPatch`, the compact data, decoder, existing default-by-name behavior, and
the current Neiro bank provider.

**Don't read:** DSP/voice code, UI drawing/state, KR-106 sources, or platform.

**Implementation:** expose 140 factory entries: original 128 first, current 12 afterward.
Original entries use canonical slot labels (`A11`, etc.); add descriptive names only if the
resolved source grant explicitly covers them. Decode only the selected original patch on
request. Delegate later indices to the Neiro provider.
Keep boot default resolution by name; preserve the current default unless ADR 0026 names a
different one. Do not cache 128 expanded patches or allocate.

**Acceptance:** count 140; exact first/last original slots and all 12 legacy names/order;
every entry returns a valid `PresetPatch`; repeated decode is deterministic; all standard
verification passes; flash delta is recorded; atomic feature commit and MEMORY.

**Split-if:** provider integration requires modifying UI or storage. Stop after the provider
is green; those callers belong to WO-13j.

## WO-13j — Browser integration, docs, and exhaustive smoke tests

**Touch list (6):** `ui/ui_presets.cpp`, `ui/ui_presets_state.cpp`,
`tests/host/test_ui_presets.cpp`, `README.md`, `specs/03-control-ui.md`,
`specs/MEMORY.md`.

**Read list (5):** this work-order; `ui/ui_presets.cpp:list drawing`;
`ui/ui_presets_state.cpp:browse/audition/revert`; `tests/host/test_ui_presets.cpp`;
`README.md:What it can do/Playing it`.

**Reuse:** the existing virtual scroll, factory-count API, audition snapshot/revert, and
table-driven parameter pages.

**Don't read:** engine/DSP implementation, KR-106 sources, platform backends, or other UI.

**Implementation:** ensure a 141-row list (140 factory + User) scrolls and wraps correctly.
Show the source slot already embedded in original names; do not add a parallel bank database.
Retain current Neiro patches visibly after B88. Update user docs to say Original 128 plus
Neiro bank and document the new independent wave/PWM/HPF controls. Add a loop that auditions,
reverts, and confirms boundary rows and representative middle rows without overflow.

**Acceptance:** A11, A88, B11, B88, first Neiro patch, last Neiro patch and User are reachable;
audition/revert/confirm work at every boundary; no fixed two-digit index buffer truncation;
all standard verification passes; atomic integration/docs commit and MEMORY.

**Split-if:** acceptable navigation needs a new grid/category browser. Keep the existing list
correct first and return the grid as a separate UX proposal.

## WO-13k — Calibration and final acceptance

**Touch list (5):** `tests/host/test_juno106_factory_audio.cpp`,
`tests/host/CMakeLists.txt`, `tools/render_juno106_factory.py`,
`specs/02-synth-architecture.md`, `specs/MEMORY.md`.

**Read list (5):** this work-order; `tests/host:test synth render helpers`;
`engine/factory_juno106.h:provider`; `engine/synth.h:render/note API`;
`specs/09-build-and-run.md:device listen/profile procedure`.

**Reuse:** host render path, existing finite/peak/DC test helpers, `make size`, PROFILE build,
and independently captured hardware or permissively licensed reference recordings.

**Don't read:** KR-106 or other GPL implementation/data, UI, platform internals, managed
components, or unrelated stages.

**Implementation:** add an offline renderer that walks all 128 originals with a fixed note/
chord probe. Automated tests require finite output, bounded peak/DC, actual signal when the
patch sources demand it, silence only when the decoded source legitimately has no audible
generator, and deterministic re-render. Select a small slot-identified calibration set,
using independently documented patch charts or reference recordings, that spans brass,
strings, EP, bass, noise/FX, saw+pulse, PWM, negative VCF ENV, all HPF positions and both
chorus modes. Record parameter and broad spectral/envelope comparisons; do not use brittle
sample-exact comparison against another synth.

**Acceptance:** `make format`, `make test`, `make host`, `make build`, `make size`, and
`make PROFILE=1 build` pass; membrane grep and `git diff --check` clean. Device acceptance:
scroll A11→B88, audition at least the calibration set on headphones, confirm no clicks/stuck
notes/NaNs, and capture eight-voice PROFILE worst blocks below budget. Record normal/PROFILE
flash, DIRAM, `sizeof(JunoVoice)`, and worst render time. Stage closes only after the 128-record
audit is green and the device listening pass has no systematic mapping error.

**Split-if:** a whole class is systematically wrong because Neiro's VCF or envelope algorithm
cannot realize the decoded control. Stop with one rendered/source comparison and author a
focused fidelity stage; do not hand-tune 128 individual patches to hide a model error.

## Definition of done

- The browser exposes all original 128 patches in canonical A/B slot order, followed by the
  12 existing Neiro patches (now a JSON bank) and user banks.
- All patches ship as JSON banks (cJSON): the 128 originals + Neiro bank embedded in flash, user
  banks as `.json` on SD/AppFS. The originals are source-pinned, hash-verified, reproducibly
  decoded offline into JSON, and licensed under MIT-compatible terms documented in the repository.
- Polyphony is 6 voices; the eight-voice PROFILE baseline and the six-voice worst block are both
  recorded.
- Every source byte/switch has a documented mapping and exhaustive decode coverage.
- Saw+pulse, square sub, direct panel modulation, shared ADSR mapping and four HPF positions
  are live DSP behavior, not inert metadata.
- No runtime allocation/blocking/logging enters the audio path; six voices remain within
  measured CPU and internal-RAM budgets.
- Host/test/device builds are green, README/specs are true, and final device audition finds
  no bank-wide mapping defect.
