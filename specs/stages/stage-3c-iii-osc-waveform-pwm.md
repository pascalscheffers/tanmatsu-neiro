# Stage 3c-iii — Oscillator waveform switching + PWM wiring

**Status:** spec-complete / ready to dispatch  
**Executor:** fresh-context Sonnet worker (ADR 0017)  
**Protocol:** [stages/README.md](README.md)  
**Prerequisite stages:** 3c-i (param rows exist), 3b-i (mod matrix eval exists), 3b-ii (kPresetDestPwm in preset)

---

## Goal

The param rows `OSC_WAVEFORM` and `OSC_PWM` have existed since Stage 3c-i, and the "Clean 106"
factory preset already carries an `LFO1→PWM` routing (stored as the `kPresetDestPwm=0xFFFD`
sentinel). However none of this is audible: `dsp/osc.h` has no `set_waveform` or `set_pw` method,
`juno_voice.cpp` never applies the cached values, and `mod_matrix.h` has no `pwm_mod` accumulator.

This sub-stage completes the wiring end-to-end.

---

## Gate table

| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| ✅ Sub-osc stays saw | before dispatch | sonic / scope | `osc_sub_` stays SAW; see **ADR 0020**. |

No further gates for this sub-stage — all decisions pre-ratified below.

---

## Pre-ratified decisions (no gates required at runtime)

### 1. OSC_WAVEFORM enum mapping

| Value | Name | DaisySP constant |
|---|---|---|
| 0 | SAW | `daisysp::Oscillator::WAVE_POLYBLEP_SAW` |
| 1 | PULSE | `daisysp::Oscillator::WAVE_POLYBLEP_SQUARE` |
| 2 | TRI | `daisysp::Oscillator::WAVE_POLYBLEP_TRI` |

Applied to `osc_main_` only via a new `dsp::Osc::set_waveform(int wf)` method. Input is an `int`
(not the DaisySP enum) so the caller (JunoVoice) never sees the DaisySP type directly. Clamp: any
value outside [0, 2] → 0 (SAW). Default is 0 (SAW) — matches the existing table row default.

`dsp::Osc::set_waveform` maps through a local switch:
```
0 → osc_.SetWaveform(WAVE_POLYBLEP_SAW)
1 → osc_.SetWaveform(WAVE_POLYBLEP_SQUARE)
2 → osc_.SetWaveform(WAVE_POLYBLEP_TRI)
default → osc_.SetWaveform(WAVE_POLYBLEP_SAW)
```

`dsp::Osc::init()` already sets `WAVE_POLYBLEP_SAW`; no change needed there.

### 2. Sub-oscillator (osc_sub_) — stays SAW (ADR 0020)

`osc_sub_` is left at `WAVE_POLYBLEP_SAW` and is not exposed to `OSC_WAVEFORM`. Rationale: the
sub provides bass reinforcement regardless of waveform; the main oscillator carries the timbral
character. A future "sub square" stage is the accuracy follow-up. See ADR 0020 for full reasoning.

### 3. PWM wiring

#### 3a. dsp::Osc gains set_pw(float)

```cpp
void set_pw(float pw) { osc_.SetPw(pw); }
```

This delegates directly to `daisysp::Oscillator::SetPw(pw)`, which does `pw_ = fclamp(pw, 0.0f, 1.0f)`.
RT-safe: it is a cheap store with one clamp — no transcendentals, no allocation, no I/O.

**Note:** `SetPw` only affects `WAVE_POLYBLEP_SQUARE` (and `WAVE_SQUARE`). On other waveforms DaisySP
ignores `pw_`. This is correct behaviour — setting PW on a SAW or TRI is a no-op and costs nothing.

#### 3b. juno_voice.cpp — apply PW once per block

In `JunoVoice::render()`, **after** the mod-matrix eval and **before** the sample loop, add:

```cpp
// PWM: apply once per block (block-rate, ~750 Hz @ 64/48k — ample for a slow LFO sweep).
// Clamp [0.05, 0.95] to avoid degenerate silent/full-duty pulse at the extremes.
float pw = p_osc_pwm_ + mout.pwm_mod;
if (pw < 0.05f) pw = 0.05f;
if (pw > 0.95f) pw = 0.95f;
osc_main_.set_pw(pw);
```

**Clamp rationale:** `SetPw` delegates to DaisySP's `fclamp(pw, 0, 1)` internally, but we apply a
tighter [0.05, 0.95] range at the JunoVoice level. At pw=0 or pw=1 the PolyBLEP square becomes a
degenerate all-positive or all-negative pulse with zero width — effectively silent (or a DC
offset). The 5%/95% guard keeps the waveform musically useful at all times, even at maximum LFO
depth. This is the same guard used on hardware analog synths with PWM to prevent the oscillator
from "disappearing".

**Block-rate justification:** The PW is applied once per block (same as cutoff, per the block-rate
optimization in Stage 3c-i fix). At 64-sample blocks and 48 kHz sample rate the update rate is
750 Hz — far above any musically meaningful LFO rate (even the fastest Juno LFO sweep is under
20 Hz). No audible stairstepping; the PolyBLEP algorithm transitions smoothly between cycles.

#### 3c. OSC_WAVEFORM application

In `JunoVoice::render()`, apply waveform once per block before the sample loop:

```cpp
// Waveform: apply once per block (waveform switch is not audio-rate; block-rate is fine).
osc_main_.set_waveform(p_osc_waveform_);
```

This replaces the existing "cached only" comment in `set_param`. The call is cheap (one switch +
one store in DaisySP's `SetWaveform`).

### 4. New mod destination: kModDestPwm in mod_matrix.h

The `kPresetDestPwm=0xFFFD` sentinel currently lives as a `static constexpr` in `preset.cpp`
only. Promote it to `mod_matrix.h` parallel to `kModDestPitch=0xFFFE`, and add `pwm_mod` to
`ModOutputs`.

#### 4a. mod_matrix.h changes

Add after the `kModDestPitch` line:

```cpp
// kModDestPwm: virtual destination for pulse-width modulation.
// Unit: bipolar offset added to OSC_PWM base value (range [-1, +1]).
// JunoVoice render() reads mod_out.pwm_mod and adds it to p_osc_pwm_ before
// clamping to [0.05, 0.95] and calling osc_main_.set_pw().
static constexpr uint16_t kModDestPwm = 0xFFFDu;
```

Add `pwm_mod` to `ModOutputs`:

```cpp
struct ModOutputs {
    // Audio-rate (pitch, cutoff, amp):
    float pitch_semi = 0.0f;
    float cutoff_mod = 0.0f;
    float amp_mod    = 0.0f;

    // Control-rate:
    float res_mod   = 0.0f;
    float osc_sub   = 0.0f;
    float osc_noise = 0.0f;
    float pwm_mod   = 0.0f;  // NEW: offset for OSC_PWM (kModDestPwm)
};
```

#### 4b. mod_matrix.cpp — seed pwm_mod and add branch in eval()

In `ModMatrix::eval()`:

1. Seed the new accumulator in the init block:
   ```cpp
   out.pwm_mod = 1e-20f;  // denormal guard (ADR 0012)
   ```

2. Add a branch in the `dest` dispatch (after the `kModDestPitch` branch):
   ```cpp
   } else if (dest == kModDestPwm) {
       out.pwm_mod += shaped;
   ```

**Note:** `kModDestPwm` is control-rate (same as `res_mod` / `osc_sub` / `osc_noise`). PWM
updating once per block at 750 Hz is indistinguishable from sample-rate on any musical LFO sweep.

#### 4c. preset.cpp — remove the local sentinel, use kModDestPwm

Remove the `static constexpr uint16_t kPresetDestPwm = 0xFFFDu;` from `preset.cpp` and replace
its use with `kModDestPwm` from `mod_matrix.h`. The numeric value is identical (0xFFFD) so all
previously serialized presets remain valid — no format bump needed.

```cpp
// Before (to remove):
static constexpr uint16_t kPresetDestPwm = 0xFFFDu;

// After (use existing include):
// mod_matrix.h already #include'd; use kModDestPwm directly.
{(uint8_t)ModSource::LFO1, kModDestPwm, +0.20f, (uint8_t)ModCurve::LIN},
```

### 5. Chiptune feasibility note

With Stage 3c-iii landed, the Tanmatsu synth achieves convincing chiptune character:

- **Pulse wave** (WAVE_POLYBLEP_SQUARE, variable duty via PWM) — the NES / SID signature tone.
- **Triangle** (WAVE_POLYBLEP_TRI) — smooth, flute-like; the NES 2A03 triangle channel approximated.
- **Noise source** — the existing `daisysp::WhiteNoise` (already present since Stage 1).
- **Arp** (Stage 4b, fully landed) — all modes including note-order and random; fast rates (1/32
  @ 120 BPM = 60 Hz) give authentic chiptune arpeggios.
- **Mono + legato** (Stage 3d-i) — single-voice portamento for lead lines.
- **Fast envelopes** (ENV_ATTACK=0, ENV_DECAY short, ENV_SUSTAIN=0) — percussive chip hits.
- **VCA_GATE_MODE=1** (hard gate: instant on/off, no envelope taper) — authentic square-wave VCA,
  matching the NES's instantaneous channel enable/disable.

**Honest caveats:**
- PolyBLEP oscillators are band-limited. They are *cleaner* than a real NES 2A03 — no aliasing
  aliases, no harmonic staircasing. The character is "chip-inspired", not bit-exact.
- DaisySP's `WAVE_POLYBLEP_TRI` is a smooth sinusoidal triangle. The real NES triangle is a
  stepped 4-bit approximation (16-level). The stepped quality is audible on the NES; ours is
  smoother.
- One DCO per voice. A real NES 2A03 has 2 pulse channels + 1 triangle + 1 noise running
  simultaneously (4 simultaneous timbres). On Tanmatsu, you approximate this via polyphony (up to
  8 voices) and the arp — with the right preset and arp rate this is convincing for solo chip leads
  and bass, but is not the same as simultaneous multi-channel NES audio.

**Verdict:** convincing for performance and composition, not bit-exact emulation. A dedicated NES
emulator (2A03 register-exact) would be Stage 7 territory.

---

## Work-order (code worker contract)

### Touch list (≤ 8 files)

1. `dsp/osc.h` — add `set_waveform(int)` and `set_pw(float)` methods
2. `engine/mod_matrix.h` — add `kModDestPwm = 0xFFFDu`; add `pwm_mod` to `ModOutputs`
3. `engine/mod_matrix.cpp` — seed `out.pwm_mod = 1e-20f`; add `kModDestPwm` branch in `eval()`
4. `engine/juno_voice.cpp` — apply `set_waveform` + `set_pw` once per block in `render()`
5. `engine/preset.cpp` — remove `kPresetDestPwm`; replace with `kModDestPwm`
6. `tests/host/test_osc_waveform.cpp` (new) — waveform + PWM tests (see acceptance below)

Budget: 6 files touched, within the ≤ 8-file rule.

### Read list (≤ 5 sections)

1. `dsp/osc.h` (entire — 32 lines)
2. `dsp/vendor/daisysp/Source/Synthesis/oscillator.h` lines 25–120 (WAVE_* enum + SetWaveform/SetPw)
3. `engine/juno_voice.cpp` lines 208–356 (render())
4. `engine/mod_matrix.h` (entire — 144 lines)
5. `engine/mod_matrix.cpp` lines 76–117 (eval())

### Reuse

- `daisysp::Oscillator::SetWaveform(uint8_t)` and `SetPw(float)` — already in the vendor; just expose.
- `kModDestPitch=0xFFFE` pattern in `mod_matrix.h` — parallel pattern for `kModDestPwm=0xFFFD`.
- `ModOutputs` denormal seeding pattern (`out.pitch_semi = 1e-20f` etc.) — replicate for `pwm_mod`.
- `mout.cutoff_mod` application pattern in `render()` — replicate for `mout.pwm_mod`.

### Don't-read

- `dsp/vendor/daisysp/Source/Synthesis/oscillator.cpp` — unnecessary.
- `engine/preset.cpp` beyond the `kPresetDestPwm` + `k_clean_106_routings` lines — unnecessary.
- Stage 4/5 docs, UI code, platform code — out of scope.

### Split-if

If making `set_waveform` or `set_pw` audio-rate (per-sample) is tempting — don't. Block-rate is
the spec (750 Hz bandwidth is ample). If this pulls in a second test file or any UI change, stop
and return the split to Opus.

### Acceptance criteria

All of the following must pass before commit:

1. `make host` green — no compile errors in `dsp/osc.h`, `engine/mod_matrix.{h,cpp}`,
   `engine/juno_voice.cpp`, `engine/preset.cpp`.
2. `make build` green — device target compiles.
3. `make test` green — all existing tests still pass + the new tests below.
4. Membrane grep clean — `dsp/osc.h` contains no `esp_`/`bsp_`/`SDL`/`miniaudio` symbols.

**New tests** (`tests/host/test_osc_waveform.cpp`):

- `test_osc_waveform_saw`: `set_waveform(0)` on a fresh `dsp::Osc` → SAW output (non-silent,
  sawtooth character). Verify via RMS > 0 over one cycle.
- `test_osc_waveform_pulse`: `set_waveform(1)` → PULSE output non-silent. Verify RMS > 0.
- `test_osc_waveform_tri`: `set_waveform(2)` → TRI output non-silent. Verify RMS > 0.
- `test_osc_waveform_clamp`: `set_waveform(-1)` and `set_waveform(99)` both clamp to SAW
  (produce non-silent output identical to waveform 0).
- `test_osc_pw_affects_output`: `set_waveform(1)` + `set_pw(0.2f)` and `set_pw(0.8f)` produce
  different RMS values (asymmetric pulse vs symmetric).
- `test_osc_pw_clamp`: `set_pw(0.0f)` and `set_pw(1.0f)` do not crash; output is non-NaN/Inf.
- `test_modoutputs_pwm_mod_seeded`: construct a `ModMatrix` with no routes; `eval()` returns
  `pwm_mod` close to 0 (within 1e-15, confirming the 1e-20f denormal guard, not exact 0).
- `test_modmatrix_pwm_route`: set one route `{LFO1, kModDestPwm, 0.5f, LIN}` with `lfo1=1.0f` →
  `eval().pwm_mod` ≈ 0.5f (within 1e-5).
- `test_preset_no_kPresetDestPwm`: verify `kPresetDestPwm` is no longer defined in `preset.cpp`
  (compile-time: the symbol `kPresetDestPwm` must not exist in the preset TU).

---

## Rationale summary (for the commit message)

The three pieces of dead code — waveform select, PWM, and LFO→PWM routing — were all spec'd in
3b-ii/3c-i but held off because `dsp/osc.h` had no seam. Wiring them now:

1. Adds 2 methods to `dsp::Osc` (delegates to DaisySP — zero new DSP code).
2. Promotes `kModDestPwm` from a file-local sentinel to a shared constant.
3. Adds `pwm_mod` to `ModOutputs` (one float + one branch in eval).
4. Applies waveform + pw in `render()` — 3 lines before the sample loop.

Net result: the "Clean 106" LFO→PWM routing that has been serialized in every factory preset since
Stage 3b-ii is finally audible.

---

## Hand-off

After this sub-stage:
- OSC_WAVEFORM and OSC_PWM are fully functional for the main oscillator.
- The LFO→PWM "Clean 106" routing works end-to-end.
- Tracked open items remain: HPF DSP wiring (inert row needs a 2nd `dsp::Filter` in JunoVoice).
- **Next campaign:** Stage 4d (FX: tempo-synced delay + DaisySP ReverbSc) or resume Stage 5c
  (expression/CC map), per Opus's judgment.
