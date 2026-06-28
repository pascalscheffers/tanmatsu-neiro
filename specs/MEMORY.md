# Progress Log

Newest at the bottom. One entry per stage/session. Lean — link to specs, don't restate.

## 2026-06-27 — Project bootstrap
- Cloned `tanmatsu-template` into this repo; git is local-only (template kept as remote
  `upstream-template`, no push remote).
- Wrote `CLAUDE.md` (workflow, dedup/reuse policy, real-time audio rules) and initial
  specs `00`–`03`.
- Key findings: Tanmatsu audio is **output-only** (ES8156 stereo I2S DAC, no audio-in);
  USB-C = MIDI device, USB-A = MIDI host; ESP32-P4 has FPU + SIMD; 768 KB SRAM + 32 MB
  PSRAM. → synth is fully digital VA+hybrid.
- Chosen reuse anchor: **Mutable Instruments STM32 code (plaits/braids/stmlib), MIT.**
- Launched background agent to set up the ESP-IDF build env and report BSP audio/USB APIs.
- **Build env GREEN:** ESP-IDF v5.5.1 + RISC-V toolchain installed; `make build
  DEVICE=tanmatsu` succeeds. Baseline `application.bin` = 987 KB, 52% of the 2 MB app
  partition free. (First agent stalled on the toolchain download watchdog; finished the
  compile directly in a harness-tracked job.)
- **Audio API confirmed** (`bsp/audio.h`): I2S 16-bit stereo interleaved, default 44.1k
  (`bsp_audio_set_rate(48000)` to change), `bsp_audio_get_i2s_handle` →
  `i2s_channel_write`; hw volume + amp toggle. Codec = `es8156` component.
- **Decisions ratified** (ADRs 0001–0006 in `specs/decisions/`): VA/hybrid digital
  target · Juno-106 hybrid voice · 8 voices + unison · permissive-only vendoring · USB-A
  host MIDI first · v1 screen+keyboard only. Architecture spec promoted from draft.
- **Next:** Stage 0 — hello-audio: set rate 48k, get I2S handle, write a sine block loop,
  confirm clean output on speaker + headphones. Then Stage 1: vendor MI macro-osc + VA
  filter + ADSR for one voice with host-side DSP tests.
