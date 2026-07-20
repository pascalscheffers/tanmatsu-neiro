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

- **WO-14b:** port KR-106 ADSR/VCA timing and J106 cutoff/DAC mapping.
- **WO-14c:** shared KR-106 analog noise and per-voice variance.
- **WO-14d:** KR-106 BBD chorus behind the master-effect seam, with fixed preallocation.
- **WO-14e:** delete now-unreferenced hand-built Juno/Daisy DSP and implementation-specific
  tests; shrink the dependency ledger/build only after 14a–d are green.
