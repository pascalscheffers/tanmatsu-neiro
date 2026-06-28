# ADR 0001 — Fully digital virtual-analog / hybrid target

**Status:** accepted (2026-06-27)

## Context
The Tanmatsu has no audio input and no analog signal path — output is a stereo I2S DAC
(ES8156) only (`specs/01-hardware.md`). There is nothing to physically patch or sample.

## Decision
The synth is a **fully digital virtual-analog (VA) + hybrid** instrument. "Analog
modeling" means DSP that *emulates* analog oscillators and filters. The "hybrid" is
**wavetable + VA filter models + 2-op FM**, all in software, out through the stereo DAC.

## Consequences
- No real-analog or external-CV signal path is in scope (see ADR 0006 for expansion).
- All "warmth/character" must come from DSP modeling, saturation, and the chorus.
- The whole `dsp/` layer is pure and host-testable — a genuine advantage.
