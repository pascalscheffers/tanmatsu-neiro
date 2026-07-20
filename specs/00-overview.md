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
- **Reuse-first, dedup-first.** Memory is the binding constraint. The Juno core is being
  replaced with the minimum useful GPL-3.0 Ultramaster KR-106 DSP, while one parameter
  table continues to drive UI, MIDI, and presets. (`specs/02`, ADR 0028)
- **Plays like an instrument.** USB-MIDI host (plug in a keyboard) and device (DAW), plus
  built-in musical typing, with fast on-screen live tweaking. (`specs/03`)

## Spec map
- `01-hardware.md` — what the Tanmatsu actually offers (audio, USB, compute, memory).
- `02-synth-architecture.md` — DSP/voice design, engine boundary, reuse map, RT/memory.
- `03-control-ui.md` — note input, performance/arp/sequencer/MIDI-file, live-tweak UI.
- `04-platform-and-simulator.md` — the host/device platform HAL (5 seams) and host stack.
- `05-data-model.md` — parameter table, patches/presets, pattern/sequence model (dedup).
- `06-feature-scope-and-roadmap.md` — MVP/v1/later feature matrix + staged roadmap.
- `07-upstream-contributions.md` — what we fix upstream (PAX/badge-bsp) vs work around.
- `08-embedded-practices.md` — on-target measurement, CI-without-hardware, golden tests, safety nets.
- `decisions/` — ratified design decisions (ADR-style), one per file.
- `stages/` — Opus-authored, Sonnet-executable stage runbooks (0.5–3) + execution protocol.
- `notes/` — working notes (e.g. `naming.md`).
- `MEMORY.md` — running progress log (read at session start).

## Current status (2026-07-20)
- Host and ESP32-P4 builds, tests, platform HAL, six-voice allocator, parameter-driven
  UI/MIDI/presets, factory banks, and SD recording are working.
- **ADR 0028 pivots the combined project to GPL-3.0-only** and replaces the hand-built
  Juno DSP/chorus with a minimal Ultramaster KR-106 port behind the existing voice seam.
- **Next:** audit and pin the user-supplied KR-106 tree, then land one playable KR-106
  voice before removing any superseded DSP.

## Roadmap
Full staged roadmap + feature matrix in `06-feature-scope-and-roadmap.md`. In brief:
Stage 0 hello-audio + platform HAL → Stage 0.5 on-device profiling → Stage 1 one-voice MVP → Stage 2 param model + UI →
Stage 3 modulation + full Juno → Stage 4 timing/arp/sequencer/FX → Stage 5 MIDI I/O →
Stage 6 library/capture/polish → Stage 7+ second engine (proves the boundary).
