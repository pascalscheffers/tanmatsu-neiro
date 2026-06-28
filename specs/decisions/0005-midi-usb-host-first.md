# ADR 0005 — USB-A host MIDI built first

**Status:** accepted (2026-06-27)

## Context
Two USB roles are both in scope: USB-A host (plug a hardware MIDI keyboard into the
badge) and USB-C device (badge appears as a MIDI device to a computer/DAW). Question is
ordering.

## Decision
Build **USB-A host MIDI first**: a plugged-in hardware MIDI controller plays the synth
standalone, no computer required. USB-C device mode and built-in QWERTY musical-typing
follow.

## Why
- It's the "this is a real instrument" moment — the most compelling first playable.
- Forces the note-input abstraction early (all sources normalize to one internal note
  event stream, `specs/03`), so adding device-mode and musical-typing later is additive.

## Consequences
- Needs a **USB-host MIDI class driver**. Verify what ESP-IDF / TinyUSB-host provides on
  the P4; a small driver may be required. (`CONFIG_USB_HOST_HUBS_SUPPORTED=y` is already
  set.) This risk is owned by the MIDI stage, not the audio/engine stages.
- Until the host driver exists, musical-typing is the dev/test note source so engine work
  isn't blocked on USB.
