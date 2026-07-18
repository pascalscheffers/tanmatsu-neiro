# ADR 0026 — Juno-106 factory-bank fidelity pass

**Status:** accepted (2026-07-18)

## Context
Stage 13 ships all 128 original Juno-106 factory patches as Neiro's first factory bank.
For those patches to mean what their source data says, the Juno model needs independent
saw/pulse switches (with both together), a square sub-oscillator, and Juno-authentic
panel modulation and HPF semantics — some of which conflict with earlier, pre-Juno-data
decisions made before real patch data existed. This ADR is a **narrow, superseding**
decision: it changes only what conflicts with faithfully loading the original 128
patches, and only where named below.

## Decision
- **Supersedes ADR 0002 only for the mutually-exclusive hybrid oscillator mode switch.**
  ADR 0002's macro-oscillator (VA saw/pulse/tri **or** wavetable scan **or** 2-op FM,
  one mode at a time) stays as the engine's oscillator core for non-Juno models and for
  Juno's wavetable/FM modes. For the Juno-106 model specifically, saw and pulse become
  **independent switches** that can both be on at once (as the source byte-16 bits
  encode), not one mutually-exclusive VA sub-mode. Everything else in ADR 0002 (one
  oscillator engine, sub-osc + noise, one filter, Juno-style master chorus) stands.
- **Reaffirms ADR 0004 (permissive-only vendoring)** without change, and makes explicit
  for this stage: no GPL-derived bank data, decoder implementation, generated bank
  output, curve tables, or code comments may enter the repository at any point (see the
  stage's MIT constraint and source gate). A GPL implementation may only be run
  post-freeze as an external pass/fail oracle, never linked, invoked by the build, or
  shipped.
- **Supersedes ADR 0009 only for two points:** (1) the Juno-106 model wires its **panel
  modulation directly** (DCO-LFO, VCF-LFO, VCF-ENV, PWM-LFO/manual, VCA ENV/GATE) as
  hardwired per-voice signal paths matching the original panel, not as general-matrix
  `Routing` slots; and (2) the pre-canon default routing wire shape recorded in ADR
  0009's "Default-patch voicing" and "Frozen shape" sections is not a compatibility
  constraint for the original 128 patches — those patches carry their modulation state
  in the 18-byte source record's hardwired fields, not in matrix routings. The general
  modulation matrix itself remains core engine infrastructure for non-Juno-panel
  destinations and for Neiro-original patches/extensions.
- **Supersedes ADR 0020 by changing the Juno sub-oscillator to a fixed square wave**
  one octave below the DCO, matching the real Juno-106 (ADR 0020's own "Future
  follow-up" anticipated exactly this change). `SUB_WAVEFORM` remains fixed (no user
  param row) — only the fixed shape changes from saw to square.
- Neiro extensions (poly modes beyond unison-1, arp, glide, LFO2 routes, general-matrix
  routes, extra master gain staging) may remain available, but original patches must
  load them at neutral defaults so an original patch sounds like the source data, not
  like an accidental Neiro-extension patch.

## Consequences
- `JunoVoice`'s oscillator gains two independent wave-enable bits instead of one
  `OSC_WAVEFORM` enum select, for Juno only; other models/modes keep ADR 0002's
  single-select macro-oscillator.
- Juno panel modulation is implemented as dedicated per-voice signal paths, in addition
  to (not instead of) the general modulation matrix; non-Juno models are unaffected.
- The Juno sub-oscillator becomes `WAVE_POLYBLEP_SQUARE`; ADR 0020 is superseded, not
  deleted (its context/rationale for the interim saw choice stands as history).
- No third-party bank data, decoder code, or generated output derived from GPL sources
  may be committed at any stage of this work, including as "cleaned up" or "reference"
  material; violating this blocks the commit regardless of how close to done the work is.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
