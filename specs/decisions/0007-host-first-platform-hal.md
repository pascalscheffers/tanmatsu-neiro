# ADR 0007 — Host-first development on a thin platform HAL

**Status:** accepted (2026-06-27)

## Context
The Mac/Linux simulator is the **primary** dev target (device is the deploy target), and
on host we want the **same UI** in a window. The `dsp/` layer is already pure. We need
the rest of the app to run on both host and device from one codebase.

## Decision
Introduce a thin **platform HAL** (`platform/`) — the *only* layer that touches
OS/board APIs. Everything above it (`dsp/`, `engine/`, `control/`, `ui/`) is portable and
compiles on both targets. Two HAL implementations: `platform/device/` (ESP-IDF + badge-bsp)
and `platform/host/` (desktop).

The HAL is deliberately small — five seams:
1. **Audio sink** — owns the audio thread/clock and pulls stereo float blocks from a
   single `synth_render(float* L, float* R, size_t n)` callback. Device: I2S via
   `bsp_audio_*`. Host: **miniaudio** (MIT, header-only).
2. **Display present** — UI renders into a shared PAX framebuffer (`pax_buf_t`); the HAL
   presents it. Device: `bsp_display_blit`. Host: **SDL2** texture upload. (PAX is portable
   C and compiles on host — verify in Stage 0; tiny software-present fallback if not.)
3. **Input** — adapts raw input into one canonical `input_event`. Device: `bsp_input`
   queue. Host: SDL keyboard/mouse mapped to the same events.
4. **MIDI transport** — produces canonical MIDI events. Device: USB host/device. Host:
   **RtMidi**. (Musical-typing is a shared source above the HAL, on both.)
5. **Storage** — preset/pattern/sample IO via plain `fopen` over a configurable base path
   (`/sd` on device via VFS, a local dir on host). Minimal abstraction.

Time is **derived from the audio sample counter** (see ADR 0010), so timing is portable
by construction and needs no HAL clock beyond wall-clock for the UI.

Host build uses **CMake** (separate from the IDF build); both build the same upper layers.

## Consequences
- Up-front cost: the HAL interfaces + a host backend (SDL2 + miniaudio + RtMidi). Paid
  once, in Stage 0.
- Hard rule: nothing above `platform/` may `#include` ESP-IDF, badge-bsp, SDL, or
  miniaudio headers. The HAL is the membrane. CI/host build enforces it by simply not
  having those headers.
- Host backend libs are dev-host-only dependencies; they never ship to the device.
