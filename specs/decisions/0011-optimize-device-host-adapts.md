# ADR 0011 — Optimize for the device; the simulator pays the conversion tax

**Status:** accepted (2026-06-27)

## Context
Host-first dev (ADR 0007) runs the same code on the constrained P4 and on a desktop that's
orders of magnitude more capable. Whenever a representation choice — buffer format, pixel
layout, data alignment, endianness, fixed vs float — favors one side, we must say which
side it optimizes for.

## Decision
**The shared/base code uses whatever representation is most performant on the P4. Any
adaptation to the host's preferred format happens in `platform/host/`, on the host.** The
device never spends cycles reshaping data for the simulator's convenience — the simulator
spends them, because it has them to spare.

Applies now and as it comes up:
- **Audio buffers** — the engine emits the P4-optimal format/layout; the host sink converts
  to what miniaudio wants. (Device float→int16 for I2S is intrinsic output, not a host tax.)
- **Display** — the UI renders in the panel's native pixel/color format and framebuffer
  layout; `platform/host/` converts that buffer to the SDL texture format.
- **Data at rest** — wavetables/samples/presets stored in P4-optimal layout (alignment,
  PSRAM-friendly, fixed-point where it wins on P4); the host reads the same bytes and adapts.
- **Endianness / packing** — device-native; the host swaps/unpacks if needed.

## Consequences
- The host backend carries conversion code the device doesn't. Fine — invisible at desktop
  speed, and it keeps the device path lean.
- "Most performant on P4" is a *profiled* claim for hot paths (CLAUDE.md), not a guess.
- Don't let host convenience leak a representation choice back into the shared layer. If a
  format debate arises, the P4 wins and the host adapts.
- Pairs with ADR 0007's "HAL is link-time, not per-sample": the host's conversion work
  lives in its HAL impl, never above the membrane.
