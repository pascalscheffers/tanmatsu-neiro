# Stage 11 — SD mount and WAV recording

> **Status: implementation-ready plan.** Architecture and failure policy are ratified in
> [ADR 0024](../decisions/0024-sd-recording.md). Execute in order as five fresh worker jobs.
> Each work-order stays within the `stages/README.md` budget. Workers commit only on green and
> append a tight MEMORY entry; the orchestrator updates the next work-order if a discovered
> signature differs from the pinned source.

## Running order

| Work-order | Deliverable | Effort | Depends on |
|---|---|---|---|
| 11a | reusable SD mount seam | medium | ADR 0024 |
| 11b | block SPSC audio capture | high | 11a only for final integration, code is otherwise independent |
| 11c | preset-safe table/UI toggle | medium | none |
| 11d | portable recoverable WAV writer | high | 11a–11b |
| 11e | app/UI integration and device verification | high | 11a–11d |

## 11a — Mount SD behind the platform membrane

**Touch list (5):** `platform/platform.h`, `platform/device/platform_device.c`,
`platform/host/platform_host.c`, `main/CMakeLists.txt`, `specs/MAP.md`.

**Read list (4):** ADR 0024 §1; `platform/platform.h:Storage`; both backend
`platform_init` functions; installed esp-hosted 2.12.3
`examples/host_sdcard_with_hosted/main/sd_card_functions.c:sd_card_mount` plus
`badgeteam__badge-bsp/targets/tanmatsu/tanmatsu_hardware.h:SD card slot`.

**Reuse:** ESP-IDF `esp_vfs_fat_sdmmc_mount`; BSP `BSP_SDCARD_*`; existing backend logging
and host directory style.

**Don't read:** other managed components, DaisySP, UI/engine sources, MEMORY archive.

**Implementation:** add `platform_sd_available()` / `platform_sd_root()`. Device mounts once
at `/sd` before returning from `platform_init`, with slot 0, 4-bit BSP pins, pull-ups, LDO 4,
no format, and best-effort failure. Retain card/power handles for application lifetime. Host
ensures `./sd` is a directory and reports failure if it cannot create/use it. Add the SDMMC
driver dependency.

**Acceptance:** `make host`, `make test`, and `make build` green; host launch creates `./sd`;
device-without-card remains usable; no ESP symbols above `platform/`; no formatting on mount
failure.

**Split-if:** the installed IDF API cannot use LDO 4 with the BSP wiring, or Wi-Fi already
owns slot 0. Stop and return an architecture/hardware gate with the exact conflict.

## 11b — Publish final master audio by block

**Touch list (8):** `engine/record_ring.h`, `engine/record_ring.cpp`, `engine/spsc_ring.h`,
`engine/synth.cpp`, `host/CMakeLists.txt`, `main/CMakeLists.txt`,
`tests/host/CMakeLists.txt`, `tests/host/test_record_ring.cpp`.

**Read list (5):** ADR 0024 §2; `engine/spsc_ring.h:SpscRing`; `engine/synth.cpp:master
output loop`; `platform/device/platform_device.c:to_i16`; `engine/synth_config.h`.

**Reuse:** `SpscRing<T, Cap>` and the device DAC finite/clamp/truncate conversion semantics.

**Don't read:** FATFS/SD sources, UI, preset code, miniaudio header, DaisySP vendor code.

**Implementation:** a 256-item SPSC ring of fixed 64-frame stereo PCM blocks, one enabled
atomic, and a dropped-block counter. Audio API accepts the completed left/right block and
publishes once; control API enables/disables, drains, and reads/reset errors. Call capture once
after `synth_render`'s master loop, outside its chorus branch. Keep all producer operations
bounded and IRAM/cache-safe per ADR 0013.

**Acceptance:** ordered round-trip, exact conversion edge cases (NaN/Inf/clamps/truncation),
disabled no-op, capacity/drop-newest, and counter reset tests; `make host`, `make test`,
`make build` green; membrane clean. `SYNTH_PROFILE` and normal builds both link.

**Split-if:** `synth_render` can legally receive a block larger than 64 frames in any shipping
backend. Stop and revise the item shape without allocating or looping over multiple publishes.

## 11c — Add the non-preset Record row

**Touch list (6):** `engine/param_id.h`, `engine/param_desc.h`, `engine/param_desc.cpp`,
`engine/preset.cpp`, `engine/preset.h`, `tests/host/test_preset.cpp`.

**Read list (4):** ADR 0024 §4; the named parameter table/ID files; `preset_serialize` and
`preset_parse`; existing preset round-trip tests.

**Reuse:** `GROUP_GLOBAL`, `CURVE_STEPPED`, table-driven PERFORM page, stable-ID convention.

**Don't read:** renderer/DSP, platform backends, managed components, unrelated tests.

**Implementation:** allocate the next stable global/session ID without renumbering; add
`FLAG_NO_PRESET`; add Record default-Off row; compute serialized count from eligible rows and
skip the row; filter flagged known IDs during parse. Update format comments but keep v2.

**Acceptance:** Record appears through existing page collection; new serialization omits it
from count/data; a synthetic v2 blob containing it parses without returning it; old v1/v2 and
unknown-ID behavior remains green; all three builds/tests green; no format bump.

**Split-if:** adding the row makes `PRESET_BLOB_MAX` or a fixed UI row limit insufficient.
Stop with measured sizes; do not silently enlarge a persisted format or redesign the page.

## 11d — Drain to a recoverable WAV

**Touch list (6):** `control/wav_recorder.h`, `control/wav_recorder.cpp`,
`host/CMakeLists.txt`, `main/CMakeLists.txt`, `tests/host/CMakeLists.txt`,
`tests/host/test_wav_recorder.cpp`.

**Read list (4):** ADR 0024 §3; `engine/record_ring.h` public seam from 11b;
`platform/platform.h:SD`; existing host test conventions.

**Reuse:** libc `opendir/readdir/fopen/fwrite/fseek/fflush`; platform SD root/availability;
the 11b ring consumer API.

**Don't read:** app/UI, device SD driver implementation, synth/preset internals, miniaudio header,
DaisySP vendor code.

**Implementation:** implement the state machine and exact failure/checkpoint policy from ADR
0024. Expose idle/recording/error state and a compact error enum for the app; do not add UI or
app wiring in this work-order. Tests may provide platform seam stubs and a temporary SD root;
production code still discovers the root only through `platform_sd_root()`.

**Acceptance:** host tests validate exact 44-byte header fields and little-endian sizes,
incrementing/no-overwrite names, clean-stop finalization, periodic checkpoint, overflow/write
failure state, and a WAV whose payload matches synthetic ring blocks. `make host`, `make test`,
`make build`, and `make format` green; membrane clean.

**Split-if:** portable failure injection needs production-only hooks or the writer exceeds one
focused source/header pair. Stop and return the exact testability issue; do not leak host APIs
into production code.

## 11e — Wire the toggle, status, and verification

**Touch list (7):** `app/app.c`, `ui/ui.h`, `ui/ui.cpp`, `README.md`,
`specs/02-synth-architecture.md`, `specs/MEMORY.md`, `specs/MAP.md`.

**Read list (5):** ADR 0024 §4 and Consequences; `control/wav_recorder.h` public seam from 11d;
`app/app.c:control loop`; `ui/ui.h:UIState`; `ui/ui.cpp:status strip drawing` and dirty-band
helpers.

**Reuse:** `UIState.norms[ParamId::RECORD]` as requested state, `engine_set_param_norm`, existing
content/status invalidation, and recorder state/error enums. Do not use `engine_get_param` for
control logic.

**Don't read:** recorder implementation, device SD implementation, synth/preset internals,
managed components, miniaudio/DaisySP vendor code.

**Implementation:** service the recorder every control-loop iteration. On failure, force Record
Off in both the UI shadow and engine param and invalidate the content band. Add compact REC/error
feedback to the existing status strip and invalidate it only on recorder-state changes. Update
README playing/features text, the MAP from planned to implemented seams, and the measured budget.

**Acceptance:** `make host`, `make test`, `make build`, `make format`, and `make size` green;
membrane clean. Host manual: toggle on, play, toggle off, open
`sd/recordings/rec0001.wav`, confirm stereo/48k/PCM16/duration and no tail corruption.

**Device acceptance (Pascal):** card present at boot; record at least 30 seconds while playing
8 voices and navigating UI; stop and verify the file on a computer. With `PROFILE=1`, record
audio avg/max/over before and during recording; zero overruns required. Remove/disable the card
in a second run and confirm recording stops visibly while synth audio continues. Record flash,
internal DRAM, and PROFILE delta in spec 02 + MEMORY.

**Split-if:** status feedback requires a new cross-thread UI state machine beyond one compact
status field. Split status UI into a follow-up before implementing rather than widening this
work-order.
