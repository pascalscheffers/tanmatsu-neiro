# Juno-106 parameter set — what each control does, and open research

Working reference for the Juno-106 model fidelity work (stage 13). Two halves:
**(A)** the real Juno-106 panel/patch parameters and what they do, mapped to our
`ParamId` rows and the 18-byte source record; **(B)** the open research list —
things we do *not* yet know well enough to call calibrated.

Source record byte order (Roland tone params, 16 continuous + 2 switch bytes) is the
one pinned in `stage-13-juno106-factory-bank.md` (Source record contract). Neiro IDs
from `engine/param_id.h`.

## A. The parameter set (known function)

### LFO section (one LFO, delayed triangle, free-running — ADR 0018)
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 0 | LFO Rate | LFO speed | `LFO1_RATE` |
| 1 | LFO Delay Time | fade-in delay after note-on before LFO reaches full depth | `LFO1_DELAY` |

Real Juno LFO is a single triangle; depth to each destination is set at that
destination (DCO / VCF), not a global LFO-depth knob. Delay = auto-fade envelope on
the LFO amount.

### DCO section (one oscillator + sub + noise)
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 2 | DCO LFO | LFO→DCO pitch depth (vibrato) | `DCO_LFO_DEPTH` |
| 3 | DCO PWM | pulse-width amount | `OSC_PWM` |
| 4 | DCO Noise | white-noise mix level | `NOISE_LEVEL` |
| 15 | DCO Sub | sub-oscillator level | `SUB_LEVEL` |
| 16 b0-2 | Range | 16'/8'/4' octave (−12/0/+12 semis) | `OSC_RANGE` |
| 16 b3 | Pulse | pulse wave on/off | `OSC_PULSE_ON` |
| 16 b4 | Saw | saw wave on/off | `OSC_SAW_ON` |
| 17 b0 | PWM Man/LFO | PWM source: manual pulse-width vs LFO-swept | `PWM_MODE` |

Notes: saw + pulse are **independent** switches (both can be on) — ADR 0026. Sub is a
**fixed square** one octave below DCO — ADR 0026 (superseded ADR 0020's saw). No DCO
level knob on real Juno; osc is full-level, balanced against sub/noise. Our
`OSC_LEVEL` is a Neiro extension; originals load it at unity.

### HPF (four-position switch, before VCF)
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 17 b3-4 (inverted) | HPF | 4-pos high-pass / bass-boost | `HPF_CUTOFF` (0-3) |

Position map (ADR 0026 / `juno106-hpf-analysis.md`): 0 = +3 dB bass boost @70 Hz,
1 = flat bypass, 2 = HPF ~225 Hz, 3 = HPF ~700 Hz. Decode: `pos = 3 - ((sw2>>3)&3)`.

### VCF section
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 5 | VCF Freq | cutoff | `FILTER_CUTOFF` |
| 6 | VCF Res | resonance | `FILTER_RES` |
| 7 | VCF ENV | envelope→cutoff depth | `VCF_ENV_DEPTH` |
| 8 | VCF LFO | LFO→cutoff depth | `VCF_LFO_DEPTH` |
| 9 | VCF KYBD | keyboard-follow amount | `VCF_KEY_TRACK` |
| 17 b1 | VCF Env polarity | positive / inverted env→cutoff | `VCF_ENV_POLARITY` |

Real Juno VCF = IR3109, **4-pole 24 dB/oct low-pass**, self-oscillates at high res.
Filter mode is LP only on the real panel (our `FILTER_MODE` LP/BP/HP is a Neiro
extension; originals load LP).

### ENV section (ONE shared ADSR)
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 11 | Attack | A | `ENV_ATTACK` / `ENV2_ATTACK` |
| 12 | Decay | D | `ENV_DECAY` / `ENV2_DECAY` |
| 13 | Sustain | S | `ENV_SUSTAIN` / `ENV2_SUSTAIN` |
| 14 | Release | R | `ENV_RELEASE` / `ENV2_RELEASE` |

Real Juno has **one** ADSR feeding both VCF and VCA. On import we duplicate A/D/S/R
into both Neiro envelopes (ENV1=amp, ENV2=filter) so one Juno env drives both; Neiro
may edit them apart afterward (extension).

### VCA / output
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 10 | VCA Level | patch output level | `VCA_LEVEL` |
| 17 b2 | VCA Env/Gate | VCA driven by ADSR vs raw gate | `VCA_GATE_MODE` |

### Chorus (master bus BBD)
| Src byte | Juno control | Function | Neiro ID |
|---|---|---|---|
| 16 b5 (inv) + b6 | Chorus Off/I/II | BBD chorus off / rate-I / rate-II | `CHORUS_MODE` (0/1/2) |

`CHORUS_RATE/DEPTH/DELAY` exist as Neiro rows but the real switch is just Off/I/II —
originals select a mode, not free rate/depth.

## B. Still to research (open)

1. **Control curves (biggest gap).** Byte `0..127` → real units for every continuous
   param: LFO rate (Hz) and delay (s), DCO/VCF LFO depths, VCF cutoff (Hz) and res,
   VCF env depth, key-follow slope, ADSR times (s), VCA/sub/noise levels. WO-13h's
   `juno106-control-curves.md` not yet written; need Roland-doc + measured hardware
   calibration, not a borrowed table. Curves are almost certainly non-linear
   (exponential env times, exponential cutoff).
2. **VCF fidelity.** Real = 4-pole 24 dB/oct self-oscillating IR3109. Confirm what our
   voice VCF actually is (SVF 2-pole?) and whether a 24 dB/oct + self-oscillation model
   is needed for authentic sound. Resonance→self-osc threshold + level compensation.
3. **HPF real-hardware sweep.** Current position frequencies (70/225/700 Hz) are our
   derived *targets*, not measured. ADR 0026: real sweeps later supersede. Measure
   actual corners + bass-boost shape from hardware.
4. **Chorus I / II characteristics.** BBD stage: exact mod rate, depth, delay time per
   mode; I vs II difference; whether it's mono→stereo (it is on real HW) and our
   stereo handling. Noise/highpass of the BBD path.
5. **LFO shape + delay law.** Confirm triangle-only; the delay/fade curve shape and
   time range; free-run vs key-sync behavior across the poly board.
6. **PWM neutral + range.** Manual mode pulse-width mapping (byte→duty %), and LFO-mode
   sweep depth around the 50% center. What duty the real panel min/max reach.
7. **Key-follow curve.** Amount → cutoff tracking (0 = none, full = 100%/oct?); is the
   pivot note fixed, and is the curve linear in the control.
8. **Analog character / imperfections.** DCO drift/tuning instability, per-voice
   detune, VCF cutoff spread across the 6 voice cards — how much (if any) to model for
   the "fat" character vs a clean digital voice.
9. **Sub + noise levels.** Real sub square level range relative to DCO; noise color
   (white?) and level scaling.
10. **Voice-level gain staging.** How VCA Level, patch loudness, and master combine so
    imported patches sit at authentic relative levels without clipping the master
    soft-clip (ADR 0016).

Resolve 1–2 before final calibration (WO-13h / WO-13k); 3–4 are quality passes that can
follow with real hardware access.
