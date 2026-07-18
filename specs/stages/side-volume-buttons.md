# Work-order — Tanmatsu side-volume buttons

## Touch list

- `platform/platform.h`
- `platform/device/platform_device.c`
- `platform/host/platform_host.c`
- `app/app.c`
- `README.md`
- `specs/02-synth-architecture.md`
- `specs/MEMORY.md`

## Read list

1. `specs/decisions/0025-side-buttons-control-codec-volume.md` — complete decision
2. `platform/platform.h` — input key constants and audio lifecycle declarations
3. `platform/device/platform_device.c` — `kCodecVolumePct`, `platform_audio_start`,
   `platform_poll_event`, and `platform_audio_i2s_profile_read`
4. `platform/host/platform_host.c` — audio callback, `platform_poll_event`, audio lifecycle
5. `app/app.c` — main event loop and control-tick section

## Reuse

- BSP `BSP_INPUT_NAVIGATION_KEY_VOLUME_UP/DOWN` events and `bsp_audio_set_volume()`.
- Existing `platform_millis()` and the F1/F2 repeat timing convention (250 ms / 150 ms).
- Host audio-sink conversion rule from ADR 0011; do not alter synth rendering.

## Don't read

Do not open DaisySP, other managed-component sources, unrelated stage documents, or other
engine/DSP files. The hardware and BSP behaviour are already resolved by ADR 0025.

## Implementation

1. Add `PLATFORM_KEY_VOLUME_UP/DOWN` and control-thread-only
   `platform_audio_volume_get/set` declarations. `set` clamps to 0–90 and reports success.
2. Device: replace the fixed codec-volume constant with session state initialized to 90;
   set it at audio start; map both BSP navigation events; perform BSP I2C writes only from
   `platform_audio_volume_set`; report the live value in PROFILE output.
3. Host: map SDL media-volume keys; maintain the same 0–90 state; apply `pct / 90.0` at the
   sink after `s_render`, using an atomic value because the miniaudio callback is concurrent.
4. App: on press, change 5 points immediately; after 250 ms held, repeat every 150 ms.
   Release stops repeat. Never route these events through UI/keyboard note handling.
5. Document the controls, update the spec-02 budget row if size changes materially, and append
   a lean MEMORY entry.

## Acceptance

- Device side keys map on both press and release and clamp at 0/90.
- Codec writes occur on the control thread; no allocation, I2C, lock, or logging enters render.
- Host media keys provide behavioural parity without changing engine output.
- `make format`, `make host`, `make test`, and `make build` pass.
- Membrane grep remains clean.
- Commit one atomic implementation change and return only a concise summary plus size result.

## Split-if

Stop before editing if the implementation needs persistence, a UI overlay, a BSP patch, or any
file outside the touch list. Those are separate work-orders.
