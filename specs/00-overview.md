# Tanmatsu Synth — Overview

A polyphonic **analog-modeling / hybrid synthesizer** that runs on the **Tanmatsu** badge
(Nicolai Electronics / badge.team, ESP32-P4). Built on `tanmatsu-template` + ESP-IDF.

## Vision (one line)
A pocketable hybrid polysynth with **fat bass and sparkling highs** — a classic
virtual-analog poly skeleton, extended with wavetable and FM, playable live from a USB
MIDI keyboard or a DAW, tweakable on the badge's screen + keyboard.

## The pitch in three facts
- **Fully digital VA + hybrid.** No analog signal path on the Tanmatsu (output is a
  stereo I2S DAC; there's no audio input). "Analog modeling" = DSP that emulates analog.
  Hybrid = **wavetable + VA filters + FM**, all in software. (`specs/01`)
- **Reuse-first, dedup-first.** Memory is the binding constraint. We build on MIT-licensed
  embedded synth DSP (Mutable Instruments) and a single parameter table that drives UI,
  MIDI, and presets. (`specs/02`, `CLAUDE.md`)
- **Plays like an instrument.** USB-MIDI host (plug in a keyboard) and device (DAW), plus
  built-in musical typing, with fast on-screen live tweaking. (`specs/03`)

## Spec map
- `01-hardware.md` — what the Tanmatsu actually offers (audio, USB, compute, memory).
- `02-synth-architecture.md` — DSP/voice design, reuse map, real-time/memory plan.
- `03-control-ui.md` — MIDI input, live-tweak UI, presets.
- `decisions/` — ratified design decisions (ADR-style), one per file.
- `MEMORY.md` — running progress log (read at session start).

## Current status (2026-06-27)
- Repo cloned from `tanmatsu-template`; local-only git (no push remote).
- `CLAUDE.md` + initial specs written.
- **Build environment** being set up by a background agent (`make prepare` → ESP-IDF
  v5.5.1 + RISC-V toolchain; first `make build DEVICE=tanmatsu`; BSP audio/USB API report).
- **Awaiting Pascal's answers** to the five open questions in `02-synth-architecture.md`
  (sonic base, polyphony, MIDI priority, license stance, expansion scope) before
  ratifying architecture into `decisions/`.

## Near-term roadmap (provisional, after decisions land)
1. **Stage 0 — Hello audio.** BSP audio up; play a sine through the DAC at target Fs/block.
   Confirms I2S, latency, and the real-time scaffold. (depends on build-env report)
2. **Stage 1 — One voice.** Vendor MI macro-osc + VA filter + ADSR; one monophonic voice
   from the parameter table; host-side DSP tests.
3. **Stage 2 — Polyphony + chorus.** Voice pool, allocator, Juno-style chorus on master.
4. **Stage 3 — MIDI in.** USB-MIDI device first (or host — see Q3); musical typing.
5. **Stage 4 — Live UI.** Parameter pages, status strip, presets on SD.
6. **Stage 5 — Sound design.** Factory presets; A/B vs reference; tune fat/sparkle.
