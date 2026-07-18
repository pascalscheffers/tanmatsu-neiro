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
- User-facing volume moves from 0–100 in 5-point steps, with an immediate step on press, a 250 ms hold
  delay, then one step every 150 ms while held. Logical percent follows a square-law listening taper
  (`gain = norm²`) rather than mapping linearly into the codec's dB-coded register. Representative
  relative attenuation is −6.2 dB at 70%, −12.0 dB at 50%, and −24.1 dB at 25%.
- The platform and UI expose 0–100, where user-facing 100 maps to 90% codec volume. The
  90% codec ceiling preserves ADR 0021's measured-safe device landing; the rejected codec
  100% setting overloaded the downstream analog output.
- The platform HAL exposes canonical volume-up/down keys plus codec-volume get/set functions.
  The host maps its media-volume keys and applies the same attenuation in its audio sink.
- The AMP page shows a read-only Volume bar before Master Gain. It is live session chrome,
  not a synth parameter: arrow/F1/F2 navigation remains over the declarative parameter table,
  while the dedicated side buttons update the bar and its 0–100 value.
- Volume is app state outside presets and the parameter table. The app-owned storage domain
  persists it under key `volume` as an exact one-byte `uint8_t` logical-percent payload. On
  device this is the existing `neiro_synth` NVS namespace; the host uses the same storage seam.
- Startup accepts only an exact one-byte payload in the inclusive range 0–100. Missing,
  malformed, or out-of-range data, and a platform apply failure, all fall back safely to the
  existing logical 100 default (codec 90%).
- Successful changes mark the value dirty. Hold-repeat remains control-thread-only and does
  not write storage per step: release of either volume key saves the last successfully applied
  logical value, and normal shutdown retries once if the value is still dirty. Dirty state is
  cleared only after a successful save; no-ops at 0 or 100 do not mark it.
- Speaker-amplifier enable and headphone auto-routing remain unchanged.

## Consequences

Physical loudness is independent of patch tone, limiter drive, and MIDI-file automation.
Codec I2C and persistence writes stay off the real-time thread. Release batching limits NVS
wear without changing the control assignment or audio architecture.
