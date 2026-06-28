# ADR 0009 — Modulation is a matrix; Juno routings are a default patch

**Status:** accepted (2026-06-27)

## Context
We want authentic Juno behavior *and* deeper modulation later, across multiple engines,
without re-plumbing. Chosen: a small general matrix with Juno defaults layered on top.

## Decision
A single **modulation matrix** is core engine infrastructure (not per-model):

```
Routing = { source, dest_param_id, depth, curve }   // fixed-size array per patch
```

- **Sources:** LFO1/2, ENV1(amp)/ENV2(filter/mod), velocity, key-track, mod-wheel,
  pitch-bend, channel/poly aftertouch, MPE pressure & slide, macros (ADR-perf),
  sample-&-hold/random, and sequencer modulation lanes. Per-voice sources (envs, MPE)
  resolve per voice; global sources (wheel, macros) once per block.
- **Destinations:** any model parameter by `dest_param_id`, plus the audio-rate hot
  dests (pitch, PWM, cutoff, amp) handled specially for per-sample smoothness.
- **Rates:** pitch/PWM/cutoff/amp are audio-rate (block-smoothed or per-sample);
  everything else is control-rate (once per block). Keep the matrix evaluation O(routes).
- **Juno defaults:** the factory Juno patch ships pre-wired routings (LFO→pitch/PWM/cutoff
  by mod depth, ENV→cutoff/PWM, etc.) so it behaves like a 106 out of the box; users can
  add/replace routings.

## Consequences
- The matrix is bounded (fixed max routings per patch) → no allocation, predictable cost.
- A new engine inherits modulation for free — it only declares its parameters.
- The UI can start by exposing just the Juno-default knobs and reveal the full matrix
  later (ADR-modulation: "expose deeper over time"), with no engine change.
