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

## Current status (2026-06-28)
- Repo cloned from `tanmatsu-template`; local-only git (no push remote).
- `CLAUDE.md` + specs `00`–`06` written; ADRs 0001–0010 ratified.
- **Build env GREEN** on both targets: ESP-IDF v5.5.1 device build + a desktop CMake host
  build (SDL2 + miniaudio).
- **Stage 0 DONE** — the platform HAL membrane (ADR 0007). One engine (`synth_render`)
  plays a sine and renders a PAX screen on host (verified live on the Mac) and device
  (compile-verified, 936 KB / 55% partition free); only `platform/{host,device}/` differs.
  See `MEMORY.md`.
- **Workflow:** Stages 0.5–3 are written up as source-pinned runbooks in `stages/` for
  **Sonnet** to execute, escalating to **Opus** at 🛑 gates (ADR 0014). DSP pinned to
  **DaisySP** (MIT) for the MVP voice.
- **Next:** Stage 0.5 — on-device profiling (real CPU budget), then Stage 1 (one Juno voice).

## Roadmap
Full staged roadmap + feature matrix in `06-feature-scope-and-roadmap.md`. In brief:
Stage 0 hello-audio + platform HAL → Stage 0.5 on-device profiling → Stage 1 one-voice MVP → Stage 2 param model + UI →
Stage 3 modulation + full Juno → Stage 4 timing/arp/sequencer/FX → Stage 5 MIDI I/O →
Stage 6 library/capture/polish → Stage 7+ second engine (proves the boundary).
