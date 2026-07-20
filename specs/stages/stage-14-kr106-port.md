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

## WO-14c-i — One shared J106 noise source

**Goal:** Replace the per-voice Daisy white-noise generators and linear gain with one
continuously running KR-106 Juno-106 noise source shared by all voices, matching the
hardware topology and panel taper.

### Touch list (only these eight paths)

1. `dsp/vendor/kr106/KR106Noise.h` (new; verbatim tracked source)
2. `dsp/vendor/kr106/README.md`
3. `engine/voice.h`
4. `engine/juno_voice.h`
5. `engine/juno_voice.cpp`
6. `engine/synth.cpp`
7. `tests/host/test_voice.cpp`
8. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Source `Source/DSP/KR106Noise.h` and only
   `Source/DSP/KR106Voice.h:dcoNoiseLevel_j106` at the pinned commit.
3. Target `engine/voice.h:IVoice` and `engine/juno_voice.{h,cpp}:JunoVoice`.
4. Target `engine/synth.cpp:{s_mono,synth_init,synth_render steps 3a/5}`.
5. Target `tests/host/test_voice.cpp` noise/zero-level/finite tests only.

### Reuse / implementation pins

- Vendor `KR106Noise.h` verbatim and append it to the vendor README provenance list.
- `synth.cpp` owns one `kr106::Noise` and one fixed `kMaxBlock` float buffer. In
  `synth_init`, reconstruct/reset the generator before `SetSampleRate` (that setter clears
  filter state but does not reset its seed). In every render, advance it once per frame
  unconditionally, including when no voice or noise parameter is active.
- Add `IVoice::set_noise_input(const float* samples, size_t n)` as an allocation-free
  block-input seam with a default no-op implementation so other voice/test doubles do not
  need edits. `JunoVoice` overrides it and keeps only the pointer/count until its immediate
  render call; null/short inputs fail silent.
- Remove the per-voice Daisy `WhiteNoise` include/member/init/process call. Inject the same
  shared buffer into every voice alongside LFO/expression before voice rendering.
- Clamp `NOISE_LEVEL + modulation` to `[0,1]`, apply the exact pinned J106 panel curve
  `d=x-0.0594; gain=0 when x<=0 else 1.0632*(sqrt(d*d+0.0146^2)+d)/2`, then multiply by
  `kr106::kNoiseAmpJ106` and the shared sample. The descriptor remains linear because it
  represents slider travel; taper lives in DSP.
- Preserve the all-sources-off VCF-floor mute contract from WO-14a. Do not add the separate
  analog-floor/popcorn control described in stale upstream comments (pinned code has none).

### Acceptance

- Tests prove: fresh reconstructed generators are sample-identical; one-shot vs split-block
  generation is identical; two equivalent voices fed the same block produce identical
  noise-only output; level 0 is silent; mid/full slider levels are monotonic and nonlinear;
  long FTZ-off rendering is finite and leaves no subnormal public state/output.
- No per-voice PRNG/noise generator remains. Render path has no allocation, logging,
  blocking, or new platform dependency.
- `make format`, `make host`, `make test`, `make build`, `make size`, membrane grep, and
  `git diff --check` pass. Record image/DIRAM/`sizeof(JunoVoice)` deltas in `MEMORY.md` and
  commit atomically.

### Split-if / stop conditions

- Stop if `kMaxBlock` cannot bound the callback, the pointer lifetime crosses a render
  call, the shared seam requires edits to another voice/test double, the device rejects
  upstream `M_PI`, or another target file is needed.
- Do not fall back to one KR noise generator per voice; that defeats the hardware topology
  and dedup goal.

## WO-14d — Fixed-buffer KR-106 BBD chorus

**Goal:** Replace DaisySP's master chorus with the calibrated KR-106 MN3009-style stereo
chorus while preserving fixed memory, the current master-chain order, and modes Off/I/II.

### Touch list (only these eight paths)

1. `dsp/vendor/kr106/BBDFilter.h` (new; verbatim tracked source)
2. `dsp/vendor/kr106/KR106AnalogNoise.h` (new; verbatim tracked source)
3. `dsp/kr106_chorus.h` (new; attributed source-derived embedded adaptation)
4. `dsp/vendor/kr106/README.md`
5. `engine/synth.cpp`
6. `tests/host/test_voice.cpp`
7. `main/linker_audio.lf`
8. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Source `Source/DSP/{KR106Chorus.h,BBDFilter.h,KR106AnalogNoise.h}` at the pinned commit.
3. Target `engine/synth.cpp:{s_chorus,synth_init,synth_render steps 4/6}`.
4. Target `main/linker_audio.lf:audio_dsp_iram,audio_libm_iram`.
5. Target `tests/host/test_voice.cpp` suite registration/footer only.

### Reuse / implementation pins

- Vendor `BBDFilter.h` and `KR106AnalogNoise.h` verbatim. `KR106Chorus.h` cannot be
  vendored verbatim because `BBDLine::Init()` allocates a `std::vector`; derive
  `dsp/kr106_chorus.h` from it, retain GPL/authorship/source-pin attribution, and list exact
  modifications in the vendor README.
- Pin the adapted chorus to the product's 48 kHz rate. Replace each dynamic delay line with
  exactly `float[1024]` (or `std::array<float,1024>`), mask 1023: upstream requests
  `48,000 * 20 ms + 4 = 964`, rounded to 1024. Two lines = 8192 bytes. No vector, heap, or
  runtime size choice.
- Keep the adapted header under 500 lines by retaining executable behavior and calibration
  notes while removing stale/duplicated research prose. Preserve Hermite interpolation,
  BBD filters/saturation, leakage/click/floor/ripple models, mode calibrations, and 5 ms
  mode-change fade. Do not import plugin/UI code.
- Add software denormal/finite hygiene to adapted feedback/filter/ring/control states:
  `+1e-20f` or explicit epsilon snaps as appropriate; fail finite at stereo outputs.
- Replace `daisysp::Chorus s_chorus` with the adapted fixed chorus. Map `CHORUS_MODE`
  directly after clamp: 0=Off, 1=I, 2=II; upstream mode 3 remains unreachable.
  `CHORUS_RATE`, `CHORUS_DEPTH`, and `CHORUS_DELAY` remain stable persisted IDs but become
  explicit legacy no-ops because KR-106 modes use calibrated constants.
- Preserve the current CPU-saving off branch: process the KR chorus only while mode > 0.
  Mode I↔II transitions still use upstream's fade. The first engagement fills its delay
  naturally; do not pay full BBD/transcendental cost while bypassed.
- Preserve master order: summed mono → chorus → master/channel/unison gain → DC block →
  limiter → soft clip → record ring.
- Extend linker placement using the device map: map the `synth` object/audio leaves and exact
  single-precision `tanhf` archive member(s) plus dependencies into noflash IRAM/DRAM. Do not
  guess names; build, inspect the map, then add only linked members. Existing sin/exp leaves
  stay mapped.

### Acceptance

- Compile-time/static assertions prove two 1024-float buffers and bounded chorus size
  (expected about 8.5 KiB, hard stop above 10 KiB). No vector/allocation in the adapted DSP.
- Tests prove: bypass output path remains dry-equivalent at the synth branch; modes I/II are
  deterministic, finite, bounded, and stereo-decorrelated; fractional-delay onset is sane;
  I↔II switching has no discontinuity spike; ring wrap/Hermite boundaries remain valid; long
  FTZ-off silence and signal runs produce no NaN/Inf/subnormal observable state/output.
- `make format`, `make host`, `make test`, `make build`, `make size`, map audit, allocation/
  logging/blocking grep, and `git diff --check` pass. Record image, IRAM, DIRAM,
  `sizeof(kr106::Chorus)`, and the fact that on-device `PROFILE=1` timing remains a hardware
  follow-up if no badge is attached. Commit atomically.

### Split-if / stop conditions

- Stop if adapted behavior cannot fit under 500 lines/10 KiB, device audio leaves remain in
  flash, mode-on device build grows DIRAM instead of shrinking from Daisy's two 2400-float
  lines, output becomes unbounded/non-finite, another file is required, or source licensing/
  attribution is unclear.
- Do not remove calibrated analog behavior merely to hit a CPU estimate. If an attached
  device later exceeds 70% of the block period or reports `over>0`, return a CPU gate for a
  profiled optimization WO.

## WO-14f-i — Relocate HPF to the global master path

**Goal:** Correct the Juno-106 topology by moving the existing four-position HPF from every
voice to one processor after voice summing and before the KR-106 chorus. This slice changes
ownership/order only; WO-14f-ii replaces its approximate response with pinned KR behavior.

### Touch list (only these six paths)

1. `engine/juno_voice.h`
2. `engine/juno_voice.cpp`
3. `engine/synth.cpp`
4. `engine/param_desc.cpp`
5. `tests/host/test_voice.cpp`
6. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Target `engine/juno_voice.{h,cpp}` HPF member/init/param/render sites.
3. Target `engine/synth.cpp:{static DSP state,synth_init,changed-param loop,master loop}`.
4. Target `engine/param_desc.cpp:HPF_CUTOFF`.
5. Target `tests/host/test_voice.cpp` HPF integration/order tests only.

### Implementation pins

- Remove `Juno106Hpf` ownership, init, `HPF_CUTOFF` handling, and processing from
  `JunoVoice`; feed its oscillator/sub/noise mix directly into the KR VCF.
- Own one `dsp::Juno106Hpf s_hpf` in `synth.cpp`; initialize it at the engine sample rate.
  Route changed `ParamId::HPF_CUTOFF` values to it after clamp `[0,3]`.
- Process each `s_mono[i]` through `s_hpf` exactly once before both the KR chorus-on path
  and dry path. Final order becomes voice sum → global HPF → KR chorus → gain → DC block →
  limiter → soft clip → recorder.
- Keep the stable param ID/range/default; remove `FLAG_PER_VOICE` and document global
  post-sum ownership. Do not change response values yet.
- Remove only tests whose premise is per-voice/pre-VCF placement; do not weaken standalone
  HPF response/switch/finite coverage.

### Acceptance

- Source-order audit proves one global HPF call before both chorus branches and none in
  `JunoVoice`. `sizeof(JunoVoice)` drops by the old HPF member size.
- `make format`, `make host`, `make test`, `make build`, `make size`, membrane grep, and
  `git diff --check` pass. Record image/DIRAM/voice-size deltas and commit atomically.

### Split-if

- Stop if master processing needs allocation, persisted values must change, HPF cannot run
  before both branches, another file is required, or any non-HPF voice behavior changes.

## WO-14f-ii — Replace HPF response with pinned KR-106 behavior

**Goal:** Replace the old hand-derived HPF internals with the pinned KR-106 J106 global HPF:
positions bass/flat/236 Hz/754 Hz, common 0.35 Hz AC coupling, and click-safe 64-sample mode
crossfade.

### Touch list (only these seven paths)

1. `dsp/juno106_hpf.h`
2. `dsp/juno106_hpf.cpp`
3. `tests/host/test_juno106_hpf.cpp`
4. `dsp/vendor/kr106/README.md`
5. `specs/02-synth-architecture.md`
6. `specs/MAP.md`
7. `specs/MEMORY.md`

### Read list

1. This work-order.
2. Source `Source/DSP/KR106_HPF.h:{getJuno106HPFFreq,BassBoostFilter}` and only
   `Source/DSP/KR106_DSP.h:HPF` at the pinned commit.
3. Target `dsp/juno106_hpf.{h,cpp}:Juno106Hpf`.
4. Target `tests/host/test_juno106_hpf.cpp`.
5. Vendor README provenance format and `specs/02` HPF signal-flow text only.

### Implementation pins

- Keep existing target paths/API/build registrations, but replace internals with an
  attributed J106-only extraction/adaptation. Record exact source sections/modifications in
  vendor README. No J6/J60/model/plugin/orchestrator code.
- Preserve upstream positions: 0=circuit bass filter; 1=flat; 2=236 Hz; 3=754 Hz. Every
  position includes the 0.35 Hz AC-coupling blocker. Mode changes snapshot state and
  crossfade old/new processors over exactly 64 samples.
- Preserve upstream bass circuit constants and response. At 48 kHz target approximately:
  +10.10 dB @20 Hz, +7.42 dB @70 Hz, +5.86 dB @103 Hz, +1.41 dB @5 kHz; low/high shelf
  difference about 9.1 dB. Retire the old +3 dB @70 Hz target.
- Adapt upstream `double` bass states to `float` for RV32F. Sanitize non-finite input before
  feedback; apply software anti-denormal hygiene to bass, DC, current and previous cut-filter
  states. Coefficient/libm work stays outside per-sample processing.

### Acceptance

- Tests cover positions 2/3 at -3.01 dB near 236/754 Hz; common 0.35 Hz blocker; bass
  response at 20/70/103/5000 Hz; deterministic split vs contiguous processing; exact
  64-sample and rapid repeated switching; NaN/Inf input recovery; all modes bounded; long
  FTZ-off silence with observable output normal-or-zero.
- `make format`, `make host`, `make test`, `make build`, `make size`, membrane grep, and
  `git diff --check` pass. Record HPF size/image/DIRAM and commit atomically.

### Split-if

- Stop if float state misses response tolerance, any mode is non-finite/unbounded, exact
  crossfade semantics require another file, or adaptation exceeds the existing module's
  focused responsibility/500-line limit.
