# Stage 13 — Original Juno-106 factory bank and fidelity pass

> **Status: implementation-ready plan.** Execute in order as fresh worker jobs. The user
> ratified the two decisions that would otherwise be gates on 2026-07-18: (1) when the
> existing Neiro Juno model conflicts with the original Juno-106 control/signal model,
> prefer the Juno-106 unless doing so would break a platform invariant; (2) GPL-3.0-derived
> factory data and implementation references are allowed. The preset wire format, stable
> parameter IDs, and factory-routing representation are not compatibility constraints yet.

## Goal

Ship all 128 original Juno-106 factory patches as Neiro's first factory bank, in original
slot order `A11`–`A88`, then `B11`–`B88`. Preserve the current 12 Neiro-designed patches
after that bank. A factory patch must decode from the original compact 18-byte payload at
load time; do not expand 128 patches into duplicated float arrays in flash.

This is also the minimum fidelity pass needed for those patches to mean what their source
data says:

- independent saw and pulse switches, including saw + pulse together;
- the fixed square sub-oscillator one octave below the DCO;
- Juno DCO range, PWM mode, DCO-LFO, VCF-LFO, VCF-envelope and shared-envelope semantics;
- the four-position Juno-106 HPF: bass boost, flat, about 236 Hz, about 754 Hz;
- one shared, free-running LFO (already ratified by ADR 0018);
- Chorus Off/I/II and the original VCA ENV/GATE switch.

Neiro extensions may remain where they do not distort imported patches. Original patches
must load extension-only state at neutral defaults: poly mode, unison 1, arp off, no glide,
no LFO2 routes, unity master gain, and no general-matrix routes unless a source control
cannot be expressed directly.

## Ratified source and licensing

- Upstream: `kayrockscreenprinting/ultramaster_kr106`, GPL-3.0, commit
  `bc15caee5843ab238a25d0969e68d57db2b1615f`.
- Import only `tools/preset-gen/Factory_Patches.pat`: 128 lines, each containing 16
  seven-bit slider bytes, two switch bytes, and a patch name.
- Expected source SHA-256:
  `f543f19833c5f2ef76485a1e3deb3caa7d75695b02b249a83121656da035fb7a`.
- KR-106's decoder, curve tables, and circuit notes may be consulted and selectively
  adapted, but do not vendor JUCE, plugin code, UI assets, or the KR-106 DSP wholesale.
- The imported dataset and any adapted implementation carry clear GPL-3.0-only notices.
  The combined distributed firmware is GPL-3.0-covered; existing original MIT files need
  not have their individual notices erased. WO-13a records the precise repository policy.

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
| 13a | ratify licensing, fidelity, and compatibility reset | medium | — |
| 13b | split the oversized preset module without behavior change | medium | 13a |
| 13c | faithful DCO controls, dual waveform mix, and square sub | high | 13a |
| 13d | direct Juno panel modulation semantics | high | 13c |
| 13e-i | pure four-position Juno HPF block | high | 13a |
| 13e-ii | place HPF in each Juno voice before the VCF | medium | 13e-i |
| 13f | clean in-memory patch object and disposable wire v3 | high | 13b–13d |
| 13g | vendor and deterministically generate the compact bank | medium | 13a |
| 13h | decode raw Juno records through calibrated curves | high | 13d–13g |
| 13i | expose 128 originals + 12 Neiro patches through one provider | high | 13f–13h |
| 13j | browser labels, documentation, exhaustive smoke verification | medium | 13i |
| 13k | host/device sonic calibration and final acceptance | high | 13j |

No additional Opus gates are open. A worker must still stop on an unlisted licensing,
persisted-data, CPU-budget, or material sonic choice not resolved by this document.

## WO-13a — Ratify the compatibility and licensing reset

**Touch list (5):** `specs/decisions/0026-juno106-factory-bank-and-fidelity.md`,
`specs/decisions/README.md`, `specs/02-synth-architecture.md`,
`specs/05-data-model.md`, `specs/MEMORY.md`.

**Read list (5):** this stage through Source record contract; ADR 0002 Decision; ADR 0004
Decision/Consequences; ADR 0009 Frozen shape; ADR 0020 Decision/Future follow-up.

**Reuse:** ADR 0018's shared LFO and the existing model/parameter-table architecture.

**Don't read:** implementation sources, vendor trees, other stage docs, or
`MEMORY-archive.md`.

**Implementation:** write ADR 0026 as the narrow superseding decision. It supersedes ADR
0002 only where the Juno model's mutually-exclusive hybrid oscillator conflicts with
independent Juno wave switches; ADR 0004 only for this pinned GPL dataset and derived
fidelity code; ADR 0009 only for hardwired Juno panel modulation and the pre-canon routing
wire shape; and ADR 0020 by changing the Juno sub to square. Record that current preset
versions/IDs may be replaced cleanly and old NVS user blobs may fail closed to the default
factory patch. Add the GPL component to spec 02's dependency ledger and update spec 05's
patch contract without pretending GPL applies to unrelated source files individually.

**Acceptance:** cross-references and decision index are correct; `git diff --check` is
clean; one atomic docs commit; tight MEMORY entry names WO-13b next.

**Split-if:** licensing review finds a linked dependency incompatible with GPL-3.0-only.
Stop with the exact dependency and license; do not weaken or omit notices.

## WO-13b — Split preset code before adding the bank

**Touch list (8):** `engine/preset.h`, `engine/preset.cpp`,
`engine/factory_neiro_presets.h`, `engine/factory_neiro_presets.cpp`,
`main/CMakeLists.txt`, `host/CMakeLists.txt`, `tests/host/CMakeLists.txt`,
`specs/MEMORY.md`.

**Read list (4):** this work-order; `engine/preset.cpp:factory bank and public factory
functions`; `engine/preset.h:factory API`; the three CMake source lists.

**Reuse:** the current 12 `FactoryPreset` definitions and the existing public factory API.

**Don't read:** voice/DSP sources, UI, KR-106 sources, unrelated tests, or other stages.

**Implementation:** move only the current Neiro-authored factory data and its named routing
tables into the new module. Keep names, order, default-by-name behavior, physical values and
routings bit-for-bit. `preset.cpp` remains the facade/codec; no format or sound change in
this job. Do not duplicate `FactoryPreset` definitions across modules. Keep each resulting
source below about 500 lines, splitting data-only include fragments if needed.

**Acceptance:** existing preset tests, `make format`, `make test`, `make host`, `make build`,
`make size`, membrane grep and `git diff --check` pass; factory count/names/values/routings
are unchanged; atomic refactor commit and MEMORY entry.

**Split-if:** moving the data requires a public API change or pushes either new source over
500 lines. Stop and return the smallest proposed private seam.

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

**Touch list (7):** `dsp/juno106_hpf.h`, `dsp/juno106_hpf.cpp`,
`tests/host/test_juno106_hpf.cpp`, `main/CMakeLists.txt`, `host/CMakeLists.txt`,
`tests/host/CMakeLists.txt`, `specs/MEMORY.md`.

**Read list (5):** this work-order; ADR 0026 HPF section; KR-106 pinned
`Source/DSP/KR106_HPF.h:J106 and BassBoostFilter`; KR-106 pinned
`Source/DSP/KR106_DSP.h:HPF`; `dsp/dcblock.h` denormal style.

**Reuse:** KR-106's circuit-derived targets: position 0 bass boost, position 1 flat,
position 2 one-pole 236 Hz HPF, position 3 one-pole 754 Hz HPF. Adapt only the minimum pure
DSP; retain GPL notice and upstream pin.

**Don't read:** other KR-106 files, voice/engine sources, DaisySP vendor, UI, or platform.

**Implementation:** create a fixed-state, allocation-free block with click-safe mode changes
and software denormal protection. Position 0 uses the pinned circuit-derived bass-boost
biquad; position 1 is unity apart from required numerical/DC hygiene; positions 2/3 are
first-order, not the existing two-pole SVF. Expose init, set-position and process only.

**Acceptance:** response tests prove the two cutoffs within tolerance, flat unity, expected
bass-boost shape, finite silence, bounded switch transient, and no denormals; all standard
builds/format/size/membrane checks pass; atomic GPL-attributed DSP commit and MEMORY.

**Split-if:** the upstream bass-boost implementation needs more than two additional state
elements per voice or its license provenance is ambiguous. Stop before substituting a shelf.

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

## WO-13f — In-memory patch object and disposable wire v3

**Touch list (5):** `engine/preset.h`, `engine/preset.cpp`,
`tests/host/test_preset.cpp`, `ui/ui_presets_state.cpp`, `specs/MEMORY.md`.

**Read list (5):** this work-order; `engine/preset.h:format/API`; `engine/preset.cpp:codec and
factory facade`; `ui/ui_presets_state.cpp:audition_factory/user`; `tests/host/test_preset.cpp`.

**Reuse:** fixed `PRESET_MAX_PARAMS`, `PRESET_MAX_ROUTINGS`, ID/value serialization, and
lock-free engine setters.

**Don't read:** DSP/voice code, platform backends, KR-106 sources, or unrelated tests.

**Implementation:** introduce one fixed-capacity `PresetPatch` value object containing name,
physical param entries and optional additive routes. Refactor factory and user load paths to
use it, eliminating parallel output-array plumbing at call sites. Define the simplest v3
field-by-field user blob around this object; v1/v2 compatibility is not required. Invalid or
old NVS data fails closed and boot continues with the named default factory patch. No heap.

**Acceptance:** round-trip, truncation, unknown-ID, invalid-old-version, route bounds, default
fallback and factory-audition tests pass; all standard verification passes; atomic refactor
commit and MEMORY.

**Split-if:** changing the public API forces edits outside the touch list. Stop with callers
enumerated; the orchestrator will issue a bounded caller-migration job.

## WO-13g — Vendor and generate the compact source bank

**Touch list (8):** `third_party/kr106/Factory_Patches.pat`,
`third_party/kr106/LICENSE`, `third_party/kr106/README.md`,
`tools/gen_juno106_factory.py`, `engine/factory_juno106_data.inc`,
`tests/host/test_juno106_factory_source.cpp`, `tests/host/CMakeLists.txt`,
`specs/MEMORY.md`.

**Read list (4):** this work-order; pinned upstream `tools/preset-gen/Factory_Patches.pat`;
pinned upstream `LICENSE`; `tests/host/CMakeLists.txt:test registration style`.

**Reuse:** Python standard library only and the source record contract above.

**Don't read:** KR-106 generator/plugin/DSP sources, engine preset code, UI, or platform.

**Implementation:** vendor the exact source file and license with pin/hash/provenance README.
Write a deterministic generator that rejects anything except exactly 128 records, the exact
`A11..A88,B11..B88` slot sequence, 18 seven-bit bytes per row, unique bounded names, and the
expected source SHA-256. Generate a compact include containing names plus `uint8_t[18]`
records; no floats and no generator dependency at firmware runtime. `--check` must prove the
checked-in output is current.

**Acceptance:** source hash matches; generator self-test and `--check` pass; generated data
is exactly 2,304 parameter bytes plus names/struct overhead; standard builds/tests/format/
size checks pass once the test is registered; atomic vendor commit and MEMORY.

**Split-if:** upstream at the pinned commit does not match the hash or exact 128-slot shape.
Stop; do not silently use `Source/KR106_Presets_JUCE.h` or a newer commit.

## WO-13h — Decode Juno records with calibrated curves

**Touch list (7):** `engine/juno106_patch.h`, `engine/juno106_patch.cpp`,
`tests/host/test_juno106_patch.cpp`, `main/CMakeLists.txt`, `host/CMakeLists.txt`,
`tests/host/CMakeLists.txt`, `specs/MEMORY.md`.

**Read list (5):** this work-order and Source record contract;
`engine/preset.h:PresetPatch`; `engine/param_desc.cpp:Juno rows`; pinned KR-106
`tools/preset-gen/gen_presets.py:read_pat_patches`; pinned KR-106 parameter-curve helpers
named by ADR 0026.

**Reuse:** parameter descriptors for ordinary normalized mapping, shared-envelope duplication,
direct Juno panel params from 13c/13d, and fixed-capacity `PresetPatch`.

**Don't read:** generated JUCE/iPlug preset headers, KR-106 voice/filter/chorus code, UI,
platform, or unrelated tests.

**Implementation:** decode one raw record into a complete neutral-initialized `PresetPatch`.
Use small named conversion functions for the Juno-106 slider curves; do not scatter `/127`
or magic times/frequencies. At minimum calibrate LFO rate/delay, DCO/VCF modulation depths,
VCF cutoff, ADSR times, and VCA level from the pinned reference. Map all switches exactly.
Duplicate A/D/S/R to both envelopes. Set master gain to unity and emit zero matrix routes.

**Acceptance:** exhaustive tests cover all 128 records and every switch combination present;
selected patches assert exact decoded controls; all values are within their descriptor ranges;
no record produces NaN/Inf; decoder allocates nothing; all standard verification passes;
atomic GPL-attributed decoder commit and MEMORY.

**Split-if:** faithful VCF/envelope conversion requires replacing the DSP algorithm rather
than mapping its controls. Stop with the mismatch and an isolated follow-up proposal; do not
copy the whole KR-106 voice engine into this job.

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
Original names are slot-qualified (`A11 Brass`, etc.) so duplicates stay unambiguous. Decode
only the selected original patch on request. Delegate later indices to the Neiro provider.
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
and the pinned KR-106 factory MIDI/A-B procedure as a listening reference only.

**Don't read:** additional KR-106 implementation, UI, platform internals, managed components,
or unrelated stages.

**Implementation:** add an offline renderer that walks all 128 originals with a fixed note/
chord probe. Automated tests require finite output, bounded peak/DC, actual signal when the
patch sources demand it, silence only when the decoded source legitimately has no audible
generator, and deterministic re-render. Render a small named calibration set spanning brass,
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
  12 existing Neiro patches and User.
- Original data remains compact, source-pinned, hash-verified, reproducibly generated and
  properly licensed.
- Every source byte/switch has a documented mapping and exhaustive decode coverage.
- Saw+pulse, square sub, direct panel modulation, shared ADSR mapping and four HPF positions
  are live DSP behavior, not inert metadata.
- No runtime allocation/blocking/logging enters the audio path; eight voices remain within
  measured CPU and internal-RAM budgets.
- Host/test/device builds are green, README/specs are true, and final device audition finds
  no bank-wide mapping defect.
