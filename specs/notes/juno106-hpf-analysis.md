# Juno-106 four-position HPF — derivation (WO-13e-i)

Pure-DSP block: `dsp/juno106_hpf.h` / `.cpp`. This note derives the per-position
difference-equation coefficients from the calibration targets in
[ADR 0026](../decisions/0026-juno106-factory-bank-and-fidelity.md#hpf-calibration-four-position-switch)
and records how they were checked. All values below are for **Fs = 48000 Hz**
(the project's audio sample rate — see e.g. `engine/juno_voice.h`'s
`sample_rate_ = 48000.0f` default).

Targets from ADR 0026 (tag: **TARGET**), everything else is this job's derivation
(tag: **DERIVED**).

## Common structure

All four positions are implemented as **one shared first-order (one-pole/one-zero)
Direct-Form-I biquad section**:

```
y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]
```

Only `(b0, b1, a1)` change per position; the same `x1`/`y1` state carries across a
position switch, so switching is a coefficient change on a running filter, not a
state reset — this is what makes it click-safe (bounded transient, no
discontinuity/blow-up; see test `position_switch_bounded_transient`).

## Positions 2 & 3 — first-order high-pass (bilinear transform)

**TARGET**: corner ≈ 225 Hz (position 2), ≈ 700 Hz (position 3), 6 dB/oct.

Analog prototype: `H(s) = s / (s + w0)`, `w0 = 2*pi*fc`.

Bilinear transform with frequency pre-warping (so the digital corner lands exactly
at `fc`, not the un-warped approximation): substitute
`s = K*(1-z^-1)/(1+z^-1)`, `K = w0 / tan(w0*T/2)`. Let `alpha = tan(pi*fc/Fs)`
(this is `tan(w0*T/2)`). Working through the substitution and normalizing by
`(K+w0)`:

```
b0 =  1 / (1 + alpha)
b1 = -1 / (1 + alpha)
a1 = (alpha - 1) / (alpha + 1)
```

**DERIVED** coefficients (Fs = 48000 Hz):

| Position | fc (TARGET) | alpha | b0 | b1 | a1 |
|---|---|---|---|---|---|
| 2 | 225 Hz | 0.01472623 | 0.98548646 | -0.98548646 | -0.97097293 |
| 3 | 700 Hz | 0.04582880 | 0.95616283 | -0.95616283 | -0.91232565 |

**DERIVED** magnitude response (computed from the coefficients above, standard
biquad frequency response `H(e^jΩ) = (b0 + b1*e^-jΩ)/(1 + a1*e^-jΩ)`,
`Ω = 2*pi*f/Fs`):

| f | Position 2 (225 Hz) | Position 3 (700 Hz) |
|---|---|---|
| 70 Hz  | -10.54 dB | -20.05 dB |
| 225 Hz | **-3.01 dB** (at corner, as expected for 1st-order) | -10.29 dB |
| 700 Hz | -0.43 dB | **-3.01 dB** (at corner) |
| 1 kHz  | -0.21 dB | -1.73 dB |
| 5 kHz  | -0.008 dB | -0.078 dB |
| 10 kHz | -0.0016 dB | -0.015 dB |

The exact -3.01 dB at each position's own corner is the expected property of a
first-order filter (prewarped bilinear transform preserves the analog corner
exactly). Test tolerance: within 0.5 dB of -3 dB at the corner frequency.

## Position 0 — low-shelf bass boost

**TARGET**: "+3 dB at 70 Hz, flat above the shelf" (a boost, not a cut).

**DERIVED design choice**: place the shelf's analog corner at `fc = 70 Hz` (the
same frequency as the probe point in the target), and solve for the low-frequency
asymptotic (DC) gain `G` such that the *analytic magnitude at the corner itself*
equals the target +3 dB — this pins the test probe exactly, and gives a
"flat above the shelf" high-frequency asymptote of unity (0 dB) by construction.

Analog prototype (first-order shelf, zero at `-G*w0`, pole at `-w0`):

```
H(s) = (s + G*w0) / (s + w0),  w0 = 2*pi*fc
```

At `s = j*w0` (i.e. `f = fc`, the corner): `|H(j*w0)| = sqrt(1 + G^2) / sqrt(2)`.
Setting this equal to the target linear gain `t = 10^(3/20) = 1.41254` and solving:

```
G = sqrt(2*t^2 - 1)
```

`G = 1.729313` → **+4.7575 dB** DC/low-frequency asymptote (this is *not* itself a
calibration target — it's the derived asymptote that makes the 70 Hz probe come out
to exactly +3 dB; DERIVED, not TARGET).

Same bilinear-transform machinery as the HPF case (`alpha = tan(pi*fc/Fs)`),
normalized by `(K+w0)`:

```
b0 = (1 + G*alpha) / (1 + alpha)
b1 = (G*alpha - 1) / (1 + alpha)
a1 = (alpha - 1) / (1 + alpha)
```

Note `a1` has the identical form to the HPF case (same pole location, `w0 = 2*pi*70`).

**DERIVED** coefficients (Fs = 48000 Hz, fc = 70 Hz, alpha = 0.00458149):

```
b0 =  1.00332613
b1 = -0.98755262
a1 = -0.99087875
```

**DERIVED** magnitude response:

| f | gain |
|---|---|
| 20 Hz  | +4.53 dB (approaching the 4.76 dB DC asymptote) |
| 70 Hz  | **+3.00 dB** (target, exact by construction) |
| 225 Hz | +0.70 dB |
| 700 Hz | +0.08 dB |
| 1 kHz  | +0.04 dB |
| 5 kHz  | +0.0016 dB (flat/unity — "flat above the shelf") |
| 10 kHz | +0.0003 dB |

Test tolerance: within 0.5 dB of +3 dB at 70 Hz; within 0.2 dB of 0 dB (unity) by
5 kHz.

## Position 1 — bypass / flat unity

**TARGET**: flat unity, but must still pass through the block's denormal/DC
hygiene (click-safe switching).

**DERIVED**: identity coefficients on the *same* shared biquad structure —
`b0 = 1, b1 = 0, a1 = 0` — so switching into/out of bypass is a coefficient change
on the running filter (state preserved), not a topology change, keeping it
click-safe. The tiny anti-denormal bias (see below) is still injected on every
`process()` call regardless of position.

## Denormal / numerical hygiene

Per ADR 0012 (RV32F has no hardware FTZ/DAZ), matching `dsp/dcblock.h` and
`dsp/filter.h`'s style: a `+1e-20f` bias is injected into the filter input before
each `process()` call. Because every position (including bypass) runs through the
same one-pole-feedback structure, this keeps `x1_`/`y1_` state off the denormal
floor in all four modes, including silence.

## Sanity checks performed

Numerically verified (Python, matching the exact formulas above) that:
- DC gain of the shelf design equals `G = 1.729313` (linear) as solved.
- High-frequency asymptote of the shelf design → 1.0 (unity, 0 dB) — confirms
  "flat above the shelf".
- Bypass (position 1) is exact unity at all probed frequencies.
- HPF corners land at exactly -3.01 dB at their own `fc`, confirming the
  prewarped bilinear transform is exact at the design frequency.

These numeric checks are re-verified in `tests/host/test_juno106_hpf.cpp` against
the actual C++ implementation (not just the derivation script).
