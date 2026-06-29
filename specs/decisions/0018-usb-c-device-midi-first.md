# ADR 0018 — USB-C device MIDI before USB-A host MIDI

**Status:** accepted (2026-06-29) · **Amends:** [ADR 0005](0005-midi-usb-host-first.md)

## Context
ADR 0005 chose to build **USB-A host MIDI first** (plug a hardware controller into the badge)
for the "real instrument" moment, accepting that it needs a USB-host MIDI class driver of
unknown availability on the ESP32-P4 — a risk explicitly owned by the MIDI stage.

Stage-5 kickoff research (2026-06-29; forums + Espressif docs) sized that risk concretely:

- **USB-C device MIDI** (badge → computer/DAW) has a **first-party ESP-IDF example**
  (`peripherals/usb/device/tusb_midi`) built on **TinyUSB, which we already vendor**. No driver
  to write; class-compliant in/out.
- **USB-A host MIDI** has **no mainline ESP-IDF managed component**. The closest reference
  (esp-idf PR #12566) is ESP32-S2/S3-only and unmerged; the `ESP32_Host_MIDI` library is
  Arduino-flavored. It is a custom USB-host class driver we would own — the real risk.

## Decision
Build the device-USB transports in **risk-ascending** order: **USB-C device mode first**
(sub-stage 5d), **USB-A host mode later** (5b). This amends ADR 0005's ordering only.

The "all sources normalize to one internal note-event stream" principle from ADR 0005 is
**unchanged and reaffirmed** — and is in fact what makes the reordering free: the foundation
(5a: HAL seam + RtMidi host backend + `control/midi_in` normalization) is transport-agnostic,
so which device transport lands first changes nothing above the membrane.

## Why
- Ship a real, low-risk MIDI path first (vetted TinyUSB example) instead of front-loading the
  one unverified driver.
- USB-A host remains on the roadmap as the "real instrument" moment; it is sequenced after the
  proven paths so its spike (ADR 0005 §Consequences, gate G1) doesn't block earlier MIDI value.

## Consequences
- Sub-stage order becomes: 5a (foundation, host RtMidi) → 5c (expression/CC) → 5d (USB-C device)
  → 5b (USB-A host, driver spike) → 5e (SMF player). USB-A host is still committed, just later.
- The USB-host class-driver risk and the G1 feasibility spike still belong to this stage; they
  are simply no longer the first thing built.
