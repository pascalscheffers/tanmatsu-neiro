# ADR 0003 — 8 voices with unison

**Status:** accepted (2026-06-27) · **rationale amended 2026-06-28** after Stage 0.5
on-device profiling (see *Amendment* below). The decision (8 + unison) stands; the *reason*
changed.

## Context
At ratification we assumed per-voice DSP cost traded directly against voice count on a fixed
~400 MHz budget — i.e. CPU was the binding constraint. Options weighed: 8 + unison, 16 thin,
or dynamic allocation.

## Decision
Target **8-voice polyphony** with optional **unison** (detuned voice stacking) rather
than 16 thinner voices. Voice pool is fixed-size and preallocated.

## Why
- Fatter sound: spare CPU goes to unison detune, the chorus, and a future reverb.
- Simpler, more predictable voice-stealing and hard-real-time guarantees than a dynamic
  allocator.
- 8 voices is ample for pads/bass/leads; revisit upward only after the first voice is
  profiled on hardware.

## Amendment (2026-06-28) — CPU is *not* the binding constraint at 8

Stage 0.5 measured the real device (P4 @ 360 MHz, block 64/48 k = 480 000 cyc/blk, -O2;
`stages/stage-0.5-results.md`). The proxy voice costs **~2 610 cyc/blk**, so **8 voices =
4.4% of the period**, 32 = 17.5%, **zero underruns**. Even a pessimistic 5–8× real-voice
scaling puts 8 voices at ~22–35% — well under the 70% ceiling, chorus + reverb included.

So the Context premise ("CPU trades directly against count") is false in the regime we ship
in. The honest reason to **hold at 8** is now **sonic, not CPU scarcity**:

- This is a Juno-character instrument — **fat unison voices + predictable stealing** beat raw
  note count. We *choose* 8 fat voices; we are not forced into them.
- The ~95% idle audio budget is better spent on **voice/FX richness** than on more notes —
  that allocation is its own decision (**ADR 0015**).

Guardrails so the choice stays reversible (Stage 1 code hygiene, not architecture):
one `kNumVoices` constant — never a literal `8`; voice pool stays genuinely state-only
(shared tables in PSRAM, no per-voice copies); allocator O(n) in voice count; **re-profile
with the real DaisySP voice before banking any higher number** — the proxy omits ladder
`tanh`, per-sample smoothing, multi-osc unison, and FX.

## Consequences
- Big sustained chords past 8 notes will steal voices — acceptable for this instrument.
- Unison reduces effective polyphony per note (a unison-4 patch ≈ 2 notes). The voice
  allocator must account for unison in its budget.
- Voice count is a tunable constant, not a hard architectural limit — profile, then tune.
