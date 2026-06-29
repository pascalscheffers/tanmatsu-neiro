# ADR 0019 — Note generators (arp, sequencer) run engine-side on the audio thread

**Status:** accepted (2026-06-29)

## Context

Stage 4a placed the musical clock (`engine/clock.h`) and the event scheduler
(`engine/scheduler.h`) on the **audio thread** — both are advanced once per block from
`synth_render`, and the clock is derived from the audio sample counter (ADR 0010) so it
cannot drift against the sound it triggers. The Stage 4 campaign brief had tentatively
pencilled the arpeggiator into the **control layer** (alongside `control/keyboard`), using a
look-ahead scheduling window. With the clock and scheduler now audio-thread resident, that
placement is the weaker option.

## Decision

The note generators — the **arpeggiator** (4b) and the **pattern sequencer** (4c), and any
future MIDI-file player — are **pure `engine/` modules driven once per block from
`synth_render`**, not control-layer modules.

- They **observe note on/off** as those events are drained from the existing `NoteCmd` ring at
  the top of the block (one place sees all note input), maintaining their own held-note state.
- They **read the clock with zero staleness** (same-thread) and **push timed note events
  straight into the 4a scheduler** (`engine/scheduler.h`), which fires them sample-accurately.
- They are **pure** in the RT sense: fixed-size state (held-note arrays sized at compile time),
  no allocation, no logging, no blocking — they obey the same audio-path rules as the voices.
- Configuration (mode, rate, gate, swing, octaves, latch, on/off) arrives through the **single
  parameter table** like every other tweakable; the generators read the smoothed/stepped values
  each block.

## Consequences

- **Fully sample-accurate** with no look-ahead-window tuning and no control-thread timer/cadence
  to add — the jitter the sample-clock (ADR 0010) exists to remove is not reintroduced.
- The arp/seq **step logic is trivial** (advance a counter, pick the next held note, push to a
  ring), so the added audio-thread cost is negligible — measured as a row in the spec 02
  cycles/block budget when 4b/4c land (ADR 0015 discipline), not assumed.
- This is a deliberate **deviation from the brief's "control layer" note**; the brief +
  `specs/MAP.md` are updated to point at the engine-side seam.
- The architecture rule (dependencies point downward; the audio thread is sacred) still holds:
  the generators are *engine* components, below `control/`, and stay allocation/log/lock-free.
  What changes is only *where the brief assumed they'd live*, not the layering invariant.
- The same seam serves both 4b and 4c — the sequencer is "a generator with a stored pattern
  instead of a live held-note arpeggiation"; both feed the scheduler identically.
</content>
</invoke>
