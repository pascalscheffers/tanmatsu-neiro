# ADR 0002 — Juno-106 voice skeleton + hybrid macro-oscillator

**Status:** accepted (2026-06-27)

## Context
We need a polyphonic VA poly with fat bass and sparkling highs, on a tight CPU/memory
budget, that's simple enough to stay AI-maintainable. Candidates weighed: Juno-106,
Jupiter-8 (2 VCO), Prophet-5 (2 VCO + SSM), FM-forward (Dexed/MSFA).

## Decision
Use the **Roland Juno-106 voice architecture as the skeleton**: one oscillator + sub-osc
+ noise → one VA filter (LP/BP/HP, cutoff+res) → VCA, with 2× ADSR + 1–2× LFO per voice,
and a **Juno-style chorus on the master bus**. Replace the single DCO with **one hybrid
macro-oscillator** whose mode is VA (saw/pulse/tri) **or** wavetable scan **or** 2-op FM.

Full signal flow in `specs/02-synth-architecture.md`.

## Why
- Shortest signal path of the candidates → cheapest per-voice CPU → most polyphony, and
  the easiest to reason about file-by-file.
- Fat bass = sub-osc + unison + chorus; sparkle = resonant filter + chorus + FM mode.
- Folding VA/wavetable/FM into **one** oscillator engine is the central dedup move
  (one engine, not three). Built on Mutable Instruments `plaits`/`braids` (ADR 0004).

## Consequences
- Not the thickest possible raw oscillator section (that was Jupiter/Prophet) — we buy
  thickness back with unison + chorus instead of a second full VCO per voice.
- FM is 2-operator (within the macro-osc), not full DX-grade 6-op, unless revisited.
