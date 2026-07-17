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
| 11f-i | storage-worker platform seam + SD diagnostics | medium | 11e device freeze repro |
| 11f-ii | non-blocking recorder requests + worker I/O | high | 11f-i |
| 11g | aggregate SD writes to sustain capture | high | 11f-ii device repro |
| 11h | retain PCM until a true bulk write | high | 11g device repro |

## 11h — Retain PCM until a true bulk write

**Repro:** after 11g, six device recordings still stop with `REC:DROP` after
0.383–0.440 s (287–330 complete 64-frame blocks), essentially the original 340 ms ring
limit. The nominal 16-block/4 KiB batch did not materially improve capture.

**Root cause:** the storage worker wakes every 1 ms while the 48 kHz/64-frame producer
publishes only every 1.333 ms. `drain_ring_batch()` writes whatever it finds immediately,
so steady state normally issues one 256-byte `fwrite`; the 4 KiB buffer is only a maximum,
not persistent aggregation. This leaves the original ~750 tiny writes/s intact. Recorder
memory traffic is below ~1 MiB/s including float reads and ring copies, so memory bandwidth
is not the observed limit. The card's allocation accounting indicates 32 KiB FAT clusters.

**Touch list (4):** `control/wav_recorder.cpp`, `tests/host/test_wav_recorder.cpp`,
`specs/MEMORY.md`, `specs/stages/stage-11-sd-recording.md`.

**Read list (4):** this 11h work-order; `control/wav_recorder.cpp:drain_ring_batch/checkpoint/finish/start`;
`engine/record_ring.h:RecordBlock`; `tests/host/test_wav_recorder.cpp:multi-block/checkpoint/overflow tests`.

**Reuse:** existing `RecordBlock`, RIFF accounting, one-second checkpoint, fixed ring,
drop-newest failure policy, and storage worker. No allocation and no new filesystem seam.

**Don't read:** DSP/voice sources, platform backends, managed components, other stage docs,
or `MEMORY-archive.md`.

**Implementation:** replace the per-call maximum batch with a persistent 32 KiB static PCM
staging buffer (128 full audio blocks). Worker passes pop available ring blocks into the
staging buffer but do not call `fwrite` until it is full. A full buffer is written once and
then reused. Before the one-second RIFF checkpoint, and during clean/error finalization after
the ring is drained, flush any short staged tail before patching the header. Reset staging on
start. Preflight RIFF capacity using committed plus staged bytes; on a partial write, add only
the committed prefix to `s_data_bytes`, discard the unwritten staging tail, preserve the first
meaningful error, and finalize the valid prefix. Keep each steady-state pass bounded to at
most the staging capacity plus one filesystem call, followed by the existing cooperative
yield. Do not enlarge or move the audio ring.

**Acceptance:** add a regression that publishes at least one complete 32 KiB staging unit
and proves it reaches the file before stop/checkpoint, while the existing distinguishable
multi-block short-tail test proves final tail flushing and exact ordering. Clean stop,
checkpoint, overflow, card-loss, and write-failure tests remain green. `make format`,
`make test`, `make host`, `make build`, and membrane grep pass; record device image/DIRAM
sizes. Commit one atomic fix and append a tight `MEMORY.md` entry. Required hardware retest:
record for >10 s, stop, and inspect the finalized WAV.

**Split-if:** the additional 32 KiB static buffer does not fit the device DIRAM budget, or
correct tail/error handling requires a public seam change. Stop with the measured conflict;
do not move the audio producer ring to PSRAM, enlarge it, or change the drop policy.

## 11g — Aggregate SD writes to sustain real-time capture

**Repro:** on device, every recording stops with `REC:DROP` after roughly 0.38–0.47 s.
The finalized card files contain 287, 288, 351, and 319 complete 64-frame blocks. The SPSC
ring has 255 usable slots (~340 ms), so the file lengths fingerprint a full ring plus only a
few completed writer batches; the writer is not sustaining the producer's 750 blocks/s.

**Root cause:** `drain_ring_batch()` converts and calls `fwrite` once per 64-frame block, so
48 kHz stereo capture becomes 750 tiny 256-byte stdio/FATFS writes per second. The existing
eight-block batch bounds scheduling time but does not aggregate filesystem I/O. On the tested
card, the low-priority storage worker falls behind and the deliberate drop-newest policy
finalizes the valid prefix.

**Touch list (4):** `control/wav_recorder.cpp`, `tests/host/test_wav_recorder.cpp`,
`specs/MEMORY.md`, `specs/stages/stage-11-sd-recording.md`.

**Read list (4):** this 11g work-order; `control/wav_recorder.cpp:drain_ring_batch/finish`;
`engine/record_ring.h:RecordBlock`; `tests/host/test_wav_recorder.cpp:clean-stop and overflow tests`.

**Reuse:** existing `RecordBlock`, fixed-batch/yield policy, RIFF size accounting, and
drop-newest error behavior. Use one fixed storage-worker buffer; no allocation and no new
filesystem abstraction.

**Don't read:** DSP/voice sources, platform backends, managed components, other stage docs,
or `MEMORY-archive.md`.

**Implementation:** make one 4 KiB `fwrite` for up to sixteen full 64-frame blocks instead
of one 256-byte `fwrite` per block. Pack variable-length blocks contiguously, preflight the
entire batch against the RIFF size limit, and update `s_data_bytes` by bytes actually written.
Preserve partial-write detection, final draining, checkpointing, the bounded batch yield,
and the audio-side ring unchanged. Keep the 4 KiB buffer in static storage rather than the
8 KiB FreeRTOS worker stack. Extend the host test to publish multiple distinguishable blocks
(including a short final block) and verify the exact contiguous PCM payload and header size.

**Acceptance:** the multi-block regression proves ordered, byte-exact packing with a short
tail; clean stop, overflow, checkpoint, card-loss, and write-failure tests remain green;
`make host`, `make test`, `make build`, and `make format` pass; membrane clean. Commit one
atomic fix and append a tight `MEMORY.md` entry including build/size results and the required
on-device retest (`REC` for >10 s, then inspect the WAV).

**Split-if:** one 4 KiB filesystem write still cannot be expressed without changing the
public recorder/ring seam, or device static/stack memory no longer fits. Stop with the exact
measured conflict; do not enlarge the audio ring or change the drop policy.

## 11f — Keep blocking SD work off the control loop

**Repro:** on device, setting the table-driven Record row to On freezes the app; the sniff
log has no recorder or SD setup diagnostics around the failure.

**Root cause:** all FATFS operations run synchronously in the control loop, and
`control/wav_recorder.cpp:drain_ring` drains until the live SPSC ring is empty. The audio
producer can refill a block every 1.33 ms while a small FATFS write is in progress, so the
consumer is not guaranteed to observe empty and `wav_recorder_service()` may never return.
Even a drain cap would still allow one wedged `fwrite`/`fflush` to freeze input and UI work.
Start/finish failures also lose useful libc error context, while the device mount logs only
terminal failures.

### 11f-i — Storage worker seam and mount diagnostics

**Touch list (4):** `platform/platform.h`, `platform/device/platform_device.c`,
`platform/host/platform_host.c`, `specs/MAP.md`.

**Read list (3):** ADR 0024 §2–3; `platform/platform.h:Render task`;
both backends' `platform_render_task_start/stop` implementations and the device
`mount_sd_card` function.

**Reuse:** the existing render-task lifecycle pattern, FreeRTOS task APIs on device, pthread
or C++ thread support already linked by the host target, and ESP-IDF `ESP_LOG*` plus
`esp_err_to_name`.

**Implementation:** add a portable dedicated storage-worker start/stop seam. Device runs it
on core 0 below render/control priority with a FATFS-safe fixed stack; host runs the same
callback on a background thread. Start/stop return explicit success/failure and stop must be
bounded rather than wait forever for a stuck callback. Add device logs immediately before and
after SD power creation, slot configuration, mount, and successful card discovery; failure
logs include operation, numeric code, and `esp_err_to_name`. Do not log from the audio thread.

**Acceptance:** both backends compile, worker start failure is representable, device task
priority cannot starve render/control/USB, `make host`, `make test`, `make build`, and
`make format` pass; membrane clean.

**Split-if:** the host needs a new link dependency beyond its existing system thread support.

### 11f-ii — Non-blocking recorder control seam

**Touch list (6):** `control/wav_recorder.h`, `control/wav_recorder.cpp`, `app/app.c`,
`tests/host/test_wav_recorder.cpp`, `specs/MEMORY.md`, `specs/MAP.md`.

**Read list (4):** ADR 0024 §2–3; `control/wav_recorder.cpp:drain_ring/start/finish`;
`tests/host/test_wav_recorder.cpp:test_wav_recorder_suite`;
`app/app.c:recorder service and shutdown`.

**Reuse:** the 11f-i storage worker, existing drop-newest ring/error policy,
`wav_recorder_error_t`, and C++ atomics.

**Don't read:** DSP/voice sources, UI implementation, managed components, MEMORY archive,
or unrelated platform code.

**Implementation:** initialize the storage worker explicitly from the app. Make
`wav_recorder_service` only atomically publish `want_record`; the worker remains the sole ring
consumer and owns every directory/`FILE*` call and all state transitions. State/error getters
are atomic snapshots. A worker-start failure is visible and forces Record Off. Shutdown
requests a clean finalize but waits only for the bounded platform stop contract. Preserve the
first meaningful write/finalization error and check `readdir`, `closedir`, `fflush`, and
`fclose` failures where they can change correctness. The worker drains only a fixed batch
before yielding, so it cannot starve lower-priority FreeRTOS idle/watchdog tasks; the audio
producer always remains wait-free.

**Acceptance:** host tests use a stalled fake storage callback to prove control-side service
and state reads return while the worker is blocked; start/stop transitions, overflow,
card-loss, delayed flush/close errors, header/checkpoint, and payload behavior remain covered.
`make host`, `make test`, `make build`, and `make format` pass; membrane clean.

**Split-if:** deterministic failure injection requires production-only filesystem hooks, or
safe shutdown requires forcibly deleting a task while it owns a `FILE*`. Stop and return the
exact conflict; do not add either behavior silently.

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

**Touch list (9):** `engine/record_ring.h`, `engine/record_ring.cpp`, `engine/spsc_ring.h`,
`engine/synth.cpp`, `platform/host/platform_host.c`, `host/CMakeLists.txt`,
`main/CMakeLists.txt`, `tests/host/CMakeLists.txt`, `tests/host/test_record_ring.cpp`.

**Read list (5):** ADR 0024 §2; `engine/spsc_ring.h:SpscRing`; `engine/synth.cpp:master
output loop`; `platform/device/platform_device.c:to_i16`; `engine/synth_config.h`.

**Reuse:** `SpscRing<T, Cap>` and the device DAC finite/clamp/truncate conversion semantics.

**Don't read:** FATFS/SD sources, UI, preset code, miniaudio header, DaisySP vendor code.

**Implementation:** a 256-item SPSC ring of stereo PCM blocks carrying a frame count and up to
64 frames, one enabled atomic, and a dropped-block counter. Audio API accepts the completed
left/right block and publishes once; an input over 64 frames fails closed and increments the
counter. Control API enables/disables, drains, and reads/reset errors. Call capture once after
`synth_render`'s master loop, outside its chorus branch. Cap host render calls at 64 frames when
splitting a larger miniaudio callback; the device's configured path is already exactly 64. Keep
all producer operations bounded and IRAM/cache-safe per ADR 0013.

**Acceptance:** ordered round-trip, exact conversion edge cases (NaN/Inf/clamps/truncation),
disabled no-op, capacity/drop-newest, and counter reset tests; `make host`, `make test`,
`make build` green; membrane clean. `SYNTH_PROFILE` and normal builds both link.

**Split-if:** a shipping backend cannot be made to call the engine in chunks of at most 64
frames without buffering or allocating on its audio path. Stop and return the exact conflict.

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
