# ADR 0010 — Sample-accurate timing on a pluggable clock

**Status:** accepted (2026-06-27)

## Context
Arp, sequencer (step + real-time record), tempo-synced delay, and MIDI-file playback all
need a shared, accurate musical clock. We chose internal clock + tap tempo now, with
external MIDI-clock sync deferred (but not designed out).

## Decision
The **master musical clock is derived from the audio sample counter** — the only clock
that can't drift against the audio it triggers. BPM → samples-per-tick at a fixed
internal resolution (**96 PPQN**). Everything musical is scheduled in sample-time.

- **Event scheduler:** all note/param events (MIDI in, musical-typing, arp, sequencer,
  MIDI-file player) are timestamped in sample-time and dispatched at (sub-)block
  boundaries into the audio engine via the lock-free control→audio ring.
- **Clock is a pluggable source:** `internal` (default, driven by sample count + BPM) or
  `external` (MIDI clock — future). Tap tempo and song transport (start/stop/continue)
  act on whichever source is active. Adding MIDI-clock-in later = a new source feeding the
  same scheduler; no consumer changes.
- **Consumers** (arp, sequencer, delay-time) read the clock; they never keep their own.

## Consequences
- Tight, jitter-free sync between sound and sequencing for free.
- External sync, song-position pointer, and MIDI-clock-out slot in later as source/sink
  adapters without touching consumers (honors the deferred-but-not-designed-out choice).
- Block size sets worst-case event timing granularity; sub-block dispatch is available if
  a profile says it's needed (start block = 64 @ 48 kHz).
