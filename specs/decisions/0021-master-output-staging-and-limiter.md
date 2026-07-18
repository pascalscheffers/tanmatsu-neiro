# ADR 0021 — Master output: fix CC7 gain staging + add a master-bus limiter

**Status:** accepted (2026-06-29), output landing amended (2026-07-18). **Amends ADR 0016** (master-output soft-clip): the
soft-clip is retained but **demoted** from "ceiling" to *transient safety net* behind a
real limiter. Companion to **ADR 0015** (how we spend CPU) and **ADR 0012** (denormals,
no hardware FTZ).

## 2026-07-18 amendment — unity DSP gain, louder codec

After the I2S framing crackle was fixed, an on-device A/B recorded the same playing
session at master gains 0.5 and 2.0. The 2.0 region was pleasantly loud but deliberately
hot: PROFILE measured pre-limiter peaks 2.35–2.47, minimum gain reduction 0.49–0.51
(about 6 dB), and final peaks 1.00. The SD master measured −7.4 LUFS and only six
integer-rail samples, so the limiter/soft-clip chain was working, but raising DSP gain
further would buy compression rather than clean output level.

Pascal chose the cleaner staging:

- `MASTER_GAIN` defaults to **1.0 (unity)** and every factory preset lands at unity.
  The 0.0–2.0 range remains available; 2.0 is an intentional hot/limited setting.
- Device codec volume rises from **80% to 88%**. The badge BSP maps this to ES8156
  register 158 rather than 144, approximately +7 dB at the DAC volume stage while
  remaining well below the BSP's 100% setting.
- The PROFILE I2S snapshot reports the actual device-backend codec setting instead of
  duplicating a constant in `app.c`.

This separates roles: master gain is a meaningful DSP trim/drive control; codec volume
sets physical device loudness. Existing user-saved presets retain their serialized gain.
The first device follow-up is a new SD take plus speaker/headphone listening at the new
landing; SD analysis verifies limiter use, while only listening/line capture can reveal
downstream analog distortion.

## Context

Field report: on device, dense polyphony clips harshly, and it is **worst when a MIDI
file drives volume via CC7** ("set it >1.0 and it sounds terrible for many notes").
ADR 0016 assumed normal play sits below the soft-clip knee and only transients fold.
The report shows two staging faults that put the signal *continuously* in the
saturating region:

1. **CC7 boosts 4×, the opposite of every other instrument.** `control/midi_router.c`
   scales an incoming CC to norm 0..1, then `MASTER_GAIN`'s `0.0–2.0` `CURVE_LIN` range
   (`engine/param_desc.cpp`) maps CC7=127 → **gain 2.0**. A typical MIDI file (CC7≈100)
   already yields ~1.57×, *before* any note summing. (`VCA_LEVEL` also declared CC7 but
   was dead — `cc_to_param` returns `MASTER_GAIN` first.) Per the MIDI spec, channel
   volume (CC7) is an **attenuation** fader: −∞ up to unity (0 dB) at 127 — it must
   never boost above unity.
2. **The only ceiling is static.** `dsp/saturate.h` (ADR 0016) is a *memoryless* cubic
   waveshaper. With 8 voices each peaking ~1.05 and summing linearly, a sustained chord
   sits deep in the saturating region, so the curve distorts the whole waveform instead
   of catching the occasional peak.

How commercial instruments stay clean — and why you never *hear* their limiter — is
three layers: **(1) headroom** so a full chord lands below full scale; **(2) CC7 is
attenuation-only**; **(3) a transparent limiter** as the last net that engages only on
genuine peaks because (1)–(2) keep the signal out of it. We had none of these working.
This changes how the synth sounds/plays, so the call was Pascal's (CLAUDE.md "When to
Ask"); decision recorded below.

## Decision

Fix all three layers.

### 1. CC7 becomes MIDI channel volume — attenuation-only, dB taper
MIDI channel volume is **transient performance state, not a patch setting** (real synths
do not store CC7 in a preset). So it is **not** a param-table entry: route it through the
existing channel-wide MIDI expression mechanism (the single-writer control / single-reader
audio atomics that already carry mod-wheel / pitch-bend / aftertouch in `engine/synth.cpp`).

- `control/midi_router.c` special-cases **CC7** (beside the existing CC1/64/120/123
  cases) and applies a **square-law taper** — the GM/DLS convention — to the 0..1 norm:
  `vol = norm * norm`. So 127 → 1.0 (0 dB), 64 → 0.25 (−12 dB midpoint), 0 → silence.
- `engine/synth.{h,cpp}` add `engine_set_channel_volume(float)` writing a
  `std::atomic<float> s_channel_vol{1.0f}` (default unity), reset to 1.0 in `synth_init`
  and on the panic/all-notes-off path so it can never latch a session quiet.
- The output gain in `synth_render` becomes `MASTER_GAIN · channel_vol · unison_gain(U)`.
- `engine/param_desc.cpp` removes CC7 (→ `0xFF`) from both `MASTER_GAIN` and `VCA_LEVEL`.
  `MASTER_GAIN` stays a **manual** headroom/output-trim knob (range `0.0–2.0` kept,
  default 1.0 = unity per the 2026-07-18 amendment); it is no longer reachable by MIDI. This also clears the dead
  CC7→`VCA_LEVEL` shadow.

A MIDI file's volume automation now only ever *attenuates* (unity max), exactly like
other GM instruments — the signal stays in the linear region.

### 2. Master-bus peak limiter (the transparent net)
A **feed-forward, stereo-linked** peak limiter on the master bus, **post-gain,
pre-soft-clip**. It detects on `max(|L|,|R|)` and applies one shared gain-reduction
coefficient to both channels (preserves the chorus stereo image), giving smooth
*gain reduction* instead of constant waveshaping. CPU is not the constraint (ADR 0015:
8 voices ≈ 4.4% of budget), so a per-sample one-pole limiter is affordable.

New pure module `dsp/limiter.h` (header-only, like `dsp/filter.h`), class
`dsp::LimiterStereo` — `init(float sr)`, `reset()`, `float process(float peak)`:

```
peak      = max(|L·gain|,|R·gain|)
if (peak != peak) peak = 0                        // NaN guard (limiter has memory)
target_gr = (peak > THRESH) ? THRESH/peak : 1.0
coef      = (target_gr < env_gr) ? a_att : a_rel  // smaller target ⇒ attack
env_gr   += coef * (target_gr - env_gr)
if (env_gr > 1 - 1e-7) env_gr = 1.0               // unity-snap (denormal-proof tail + clamp)
if (env_gr < 1e-6)     env_gr = 1e-6              // finite floor
return env_gr                                     // multiply both channels, then soft_clip
```

**Fixed constants** (no user knobs this round — mirrors how soft_clip shipped in 0016;
may be exposed as params later):

| Symbol | Value | Why |
|---|---|---|
| `THRESH` | 0.92 | Keeps limited steady-state in soft_clip's near-linear zone (`soft_clip(0.92)≈0.80`); headroom for attack-miss transients before the ±1.5 hard clamp. |
| attack | 1.0 ms | `a_att = 1 − exp(−1/(0.001·sr)) ≈ 0.0202`. ~48-sample catch; no per-sample gain jumps. |
| release | 120 ms | `a_rel = 1 − exp(−1/(0.120·sr)) ≈ 0.000174`. **Transparent** character — no pumping on sustained chords, quick loudness recovery. |

`exp` is computed once in `init()` from the sample rate — **never** in the render path.
The coefficient formula matches the project's one-pole convention (`param_store.cpp`).
No `logf`/`expf`/`tanhf` per sample; branch-light. **No look-ahead** in this version —
it would add 1–2 ms of instrument latency; the 1 ms attack overshoot is exactly what the
retained soft-clip catches. The module leaves room for a look-ahead field later without
an API change.

**Layering:** limiter (smooth gain reduction) → `soft_clip` (retained, catches the brief
attack-miss residual) → `to_i16` (absolute NaN/±1 backstop, unchanged).

## Denormals (ADR 0012)

Unlike `soft_clip`, the limiter **has a feedback path** (`env_gr_`), so it must not use
the `1e-18` bias trick (that would skew the gain). Instead: a **unity-snap** on the
recovery tail (where `target−env` decays geometrically toward denormal), a **finite
floor**, and **input NaN sanitization** (a single upstream NaN must not latch the
limiter's memory forever). The host/float path has no `to_i16` clamp, so the limiter's
own NaN guard is the only protection there. Host DSP tests run with FTZ disabled.

## Consequences

- A MIDI file's CC7 volume now behaves like every other GM instrument (attenuation,
  unity max), so playback is clean — the direct fix for the reported defect.
- Dense chords fold gracefully (smooth gain reduction) instead of buzzing; normal /
  low-polyphony play stays **bit-transparent** (limiter `gr == 1.0` below threshold,
  soft-clip unity below its knee).
- `soft_clip`'s role narrows to a transient safety net; ADR 0016's curve is unchanged.
- Channel volume is **not** serialized into presets (it is performance state) — a
  deliberate departure from "one parameter table drives everything," because CC7 is a
  real-time controller, not a patch value.
- A dialable limiter character / release control, and the `MASTER_DRIVE` grit parameter
  from ADR 0016's Consequences, remain future work.
- Negligible CPU: a handful of flops per stereo sample, no libm in render — add one
  small limiter line item to the spec 02 cycles/block budget per ADR 0015's
  "every spend measured" rule.
