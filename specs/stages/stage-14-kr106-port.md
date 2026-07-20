# Stage 14 — Ultramaster KR-106 DSP port

ADR 0028 authorizes this GPL-3.0-only pivot. Source is the sibling checkout
`../ultramaster_kr106`, tag `v2.5.13`, commit
`bc15caee5843ab238a25d0969e68d57db2b1615f`. Ignore its unrelated working-tree changes;
copy only tracked content at that commit.

The port lands as green vertical slices behind the existing `IVoice` and master-effect
seams. Do not delete the old DSP until its replacement is built, tested, and profiled.

## WO-14a — Phase-coherent DCO/sub + optimized J106 VCF

**Goal:** Make the existing `JunoVoice` audibly use KR-106's phase-coherent DCO/sub and
optimized Juno-106 IR3109/BA662 VCF at 1x, without importing plugin/desktop infrastructure.

### Touch list (only these eight paths)

1. `LICENSE` (new; exact upstream GPL v3 license text)
2. `dsp/vendor/kr106/KR106Oscillators.h` (new; verbatim tracked source)
3. `dsp/vendor/kr106/KR106VCF_OPTIMIZED.h` (new; verbatim tracked source)
4. `dsp/vendor/kr106/README.md` (new; provenance, exact source paths/commit, unmodified status)
5. `engine/juno_voice.h`
6. `engine/juno_voice.cpp`
7. `tests/host/test_voice.cpp`
8. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Source `Source/DSP/KR106Oscillators.h` and `Source/DSP/KR106VCF_OPTIMIZED.h` at the
   pinned commit; source `LICENSE` only for the exact license copy.
3. Target `engine/juno_voice.h:JunoVoice` and `engine/juno_voice.cpp:{init,note_on,
   reset,set_param,render,is_active}`.
4. Target `tests/host/test_voice.cpp` existing oscillator/filter/finite-output tests only.
5. `specs/decisions/0028-gpl3-kr106-core.md:Real-time constraints`.

### Reuse / implementation pins

- Vendor the two KR-106 headers verbatim. Adapt only in `JunoVoice`; do not edit vendored
  code.
- Replace the three `dsp::Osc` members with one `kr106::Oscillators`, preserving the
  upstream shared phase accumulator and CD4013-style sub toggle. Initialize it for J106:
  `mPulseInvert=true`, J106 saw/pulse/sub amplitudes, `Init(sample_rate)`.
- Preserve independent target levels by assigning the public KR-106 saw/pulse amplitudes
  from `cur_amp` before `Process`; pass `AudioTaper(eff_sub)` for the sub level. Keep the
  current external noise and HPF in this slice.
- Replace `dsp::Filter` with `kr106::VCF`: `SetSampleRate`, `SetOversample(1)`,
  `mJ106Res=true`, and `Reset` at init/reset. Convert cutoff Hz to normalized Nyquist
  frequency and call `UpdateCoeffs` once per block; call `TrackInputEnv` then
  `ProcessSample` per sample.
- Juno is low-pass only. `FILTER_MODE` becomes a no-op in this model; remove obsolete
  mode-specific test expectations rather than retaining a parallel filter.
- Preserve all existing note, envelope, modulation, VCA, HPF, expression, and
  `IVoice` behavior not explicitly replaced above.
- Do not import `KR106Voice.h`, `KR106_DSP.h`, JUCE, wavetable/resampler code, KR106
  allocation/voice management, presets, LFO, ADSR/VCA, noise, chorus, or UI.

### Acceptance

- Existing saw-only, pulse-only, saw+pulse, sub, note/release, modulation, bounded-output,
  and finite-output voice coverage is green after adapting implementation-specific tests.
- Add a regression proving saw+pulse share a coherent oscillator (combined rendering is
  deterministic after reset and differs from either waveform alone) and a high-resonance
  finite/bounded filter sweep.
- `make format`, `make host`, `make test`, `make build`, and `make size` pass.
- Render path has no allocation, logging, blocking, JUCE/plugin dependency, or new platform
  dependency. `git diff --check` clean.
- Record code-size/DIRAM change and `sizeof(JunoVoice)` in `MEMORY.md`; commit atomically.

### Split-if / stop conditions

- Stop before editing if either source header at the pinned commit differs from the local
  file, or its license/copyright status is unclear.
- Stop and report if 1x VCF cannot remain finite under the existing cutoff/resonance ranges,
  if the device link cannot resolve its math calls, or if another target file is required.
- Do not solve those by importing `KR106Voice.h` or a second allocator; that is a new WO.

## Planned follow-ups

- **WO-14b:** port KR-106 ADSR/VCA timing.
- **WO-14c:** shared KR-106 analog noise and per-voice variance.
- **WO-14d:** KR-106 BBD chorus behind the master-effect seam, with fixed preallocation.
- **WO-14e:** delete now-unreferenced hand-built Juno/Daisy DSP and implementation-specific
  tests; shrink the dependency ledger/build only after 14a–d are green.

## WO-14b — Firmware ADSR + measured J106 VCA

**Goal:** Replace only the amp envelope and its linear VCA multiply with KR-106's
firmware-timed Juno-106 ADSR and measured VCA transfer curve. Keep ENV2 and all control/UI
parameter IDs intact.

### Touch list (only these seven paths)

1. `dsp/vendor/kr106/KR106ADSR.h` (new; verbatim tracked source)
2. `dsp/vendor/kr106/KR106VCA.h` (new; verbatim tracked source)
3. `dsp/vendor/kr106/README.md`
4. `engine/juno_voice.h`
5. `engine/juno_voice.cpp`
6. `tests/host/test_voice.cpp`
7. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Source `Source/DSP/KR106ADSR.h` and `Source/DSP/KR106VCA.h` at the pinned commit.
3. Target `engine/juno_voice.h:JunoVoice` and `engine/juno_voice.cpp:{init,note_on,
   note_off,reset,set_param,render,is_active}`.
4. Target `tests/host/test_voice.cpp` envelope/release/reset/VCA tests only.
5. `dsp/vendor/kr106/README.md` provenance format.

### Reuse / implementation pins

- Vendor both headers verbatim and append them to the vendor README's imported-file list.
- Replace only amp `dsp::Env env_` with `kr106::ADSR amp_env_`; keep `env2_` unchanged.
  Configure `mModel=kJ106`, sample rate, A/D/S/R before first note.
- `ENV_ATTACK`, `ENV_DECAY`, and `ENV_RELEASE` remain physical seconds at the public seam.
  Map each to its nearest KR-106 slider/index timing. Use fixed, read-only 128-entry timing
  tables generated offline from the pinned `AttackMs`/`DecRelMs` helpers and a bounded
  nearest-index lookup; do not run `DecRelMs`'s simulation or libm in the audio path.
  Document table provenance beside the tables. `ENV_SUSTAIN` maps directly to
  `SetSustain(clamp(value,0,1))`.
- Always advance `amp_env_.Process()` once per rendered sample. Envelope VCA mode uses
  `kr106::VCAGainJ106(clamp(env,0,1))`; gate mode uses the ADSR's smoothed `mGateEnv`.
  Apply existing velocity and `VCA_LEVEL` afterward so current control contracts remain.
- `note_on`/`note_off` call the KR envelope edges. `reset` must force a fully finished,
  silent state while preserving configured A/D/S/R; `is_active`/early-exit use
  `GetBusy()` rather than Daisy's idle API.
- Do not replace ENV2, LFOs, VCF cutoff mapping, noise, HPF, chorus, allocator, or params.

### Acceptance

- Add coverage for J106's quantized/tick-timed attack, decay-to-sustain, release-to-idle,
  smooth gate-mode edges, nonlinear measured VCA curve, reset silence, finite output, and
  shortest/longest public A/D/R values.
- Existing note/voice/modulation/oscillator/VCF tests remain green after removing only
  Daisy-ADSR-specific expectations.
- `make format`, `make host`, `make test`, `make build`, `make size`, RT membrane grep, and
  `git diff --check` pass. Record image/DIRAM/`sizeof(JunoVoice)` deltas in `MEMORY.md` and
  commit atomically.

### Split-if / stop conditions

- Stop if exact timing tables would exceed 2 KiB total, public A/D/R ranges cannot map
  monotonically, device math/link behavior changes, another target file is required, or
  the new envelope cannot reach idle in bounded time at the maximum release.
- Do not import `KR106Voice.h` or change persisted parameter semantics to escape a gate.
