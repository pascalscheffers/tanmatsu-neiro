# ADR 0003 — 8 voices with unison

**Status:** accepted (2026-06-27)

## Context
Per-voice DSP cost trades directly against voice count on a fixed ~400 MHz budget.
Options: 8 + unison, 16 thin, or dynamic allocation.

## Decision
Target **8-voice polyphony** with optional **unison** (detuned voice stacking) rather
than 16 thinner voices. Voice pool is fixed-size and preallocated.

## Why
- Fatter sound: spare CPU goes to unison detune, the chorus, and a future reverb.
- Simpler, more predictable voice-stealing and hard-real-time guarantees than a dynamic
  allocator.
- 8 voices is ample for pads/bass/leads; revisit upward only after the first voice is
  profiled on hardware.

## Consequences
- Big sustained chords past 8 notes will steal voices — acceptable for this instrument.
- Unison reduces effective polyphony per note (a unison-4 patch ≈ 2 notes). The voice
  allocator must account for unison in its budget.
- Voice count is a tunable constant, not a hard architectural limit — profile, then tune.
