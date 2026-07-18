# Work-order — AMP-page listening-volume control

## Touch list

- `platform/platform.h`
- `platform/device/platform_device.c`
- `platform/host/platform_host.c`
- `app/app.c`
- `ui/ui.h`
- `ui/ui.cpp`
- `specs/MEMORY.md`

## Read list

1. `specs/decisions/0025-side-buttons-control-codec-volume.md` — complete amended decision
2. `platform/platform.h` — `platform_audio_volume_get/set` contract
3. `platform/device/platform_device.c` — codec-volume state, getter/setter, audio start, profile snapshot
4. `platform/host/platform_host.c` — sink-volume state, audio callback, getter/setter
5. `app/app.c` + `ui/ui.{h,cpp}` — `adjust_volume`, volume events/repeat, `UIState`, `build_items`, `draw_rows`

## Reuse

- Existing side-button events and repeat path in `app/app.c`.
- Existing `DrawItem`/row renderer and gradient bar in `ui/ui.cpp`.
- Existing dirty content-band invalidation and `UIState.change_seq` coordination.

## Don't read

Do not open engine/DSP code, DaisySP, managed-component sources, unrelated stage documents,
or launcher sources. The shared-NVS question is already resolved outside this work-order;
persistence is explicitly out of scope.

## Implementation

1. Change the portable audio-volume contract to logical 0–100. Device maps logical 100 to
   the safe codec ceiling 90% (rounded integer mapping); host applies logical `pct / 100.0`.
   Startup/getter state is 100. Keep PROFILE's `codec_volume_pct` reporting the actual mapped
   codec setting (90 at logical 100).
2. Make side-button steps operate in logical 5-point increments over 0–100.
3. Add the logical listening-volume value to `UIState`, initialized from the platform getter.
   After every successful press/repeat setter call, update it; if it changed, bump
   `change_seq` and invalidate the content band so the render task paints it.
4. On the AMP page only, prepend a read-only `Volume` row before `Master Gain`. Reuse the
   ordinary row styling and gradient, show `0`–`100` plus `%`, and fill by value/100.
   It is not selectable and must not alter `page_rows`, parameter IDs, presets, MIDI, arrow
   navigation, F1/F2 nudging, or CC auto-focus row indices.
5. Append a lean MEMORY entry with verification and size.

## Acceptance

- AMP draws `Volume 100 %` before `Master Gain` at startup; button changes repaint it live.
- Logical 0/100 maps to device codec 0/90 and host sink gain 0.0/1.0; values clamp.
- Side buttons step 0, 5, …, 100 with the existing hold timing.
- Volume remains session-only and absent from parameter/preset/MIDI/NVS data.
- `make format`, `make host`, `make test`, `make build`, and `make size` pass.
- Membrane grep remains clean.
- Commit one atomic implementation change and return only a concise summary plus size result.

## Split-if

Stop before editing if this needs persistence, selectable-row semantics, a new parameter ID,
a BSP patch, or any file outside the touch list.
