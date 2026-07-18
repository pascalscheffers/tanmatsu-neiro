# ADR 0025 — Side buttons control codec volume

**Status:** accepted (2026-07-18). Companion to ADR 0021 (master-output staging).

## Context

Tanmatsu has dedicated volume-up and volume-down side buttons. The BSP already emits
press/release navigation events for both. The speaker's FM8002A amplifier exposes only
enable/disable; the ES8156 codec exposes runtime hardware volume and feeds both the speaker
and headphone paths.

`MASTER_GAIN` is a patch parameter and intentional drive/trim control. MIDI CC7 is transient
attenuation-only performance state. Using either as the listening-volume control would change
the synth's gain staging and limiter behaviour.

## Decision

- The side buttons adjust ES8156 codec volume on the control thread, never in the audio path.
- Volume moves in 5-percentage-point steps, with an immediate step on press, a 250 ms hold
  delay, then one step every 150 ms while held.
- The range is 0–90%. The 90% ceiling preserves ADR 0021's measured-safe device landing;
  the rejected 100% setting overloaded the downstream analog output.
- The platform HAL exposes canonical volume-up/down keys plus codec-volume get/set functions.
  The host maps its media-volume keys and applies the same attenuation in its audio sink.
- Volume is session state in this slice: startup is 90%, with no preset serialization, NVS
  persistence, or parameter-table row.
- Speaker-amplifier enable and headphone auto-routing remain unchanged.

## Consequences

Physical loudness is independent of patch tone, limiter drive, and MIDI-file automation.
Codec I2C writes stay off the real-time thread. A volume overlay and wear-safe persistence
can be added later without changing the control assignment or audio architecture.
