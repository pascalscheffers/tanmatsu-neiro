# ADR 0024 — SD-card WAV recording: mounted storage + block-ring capture

**Status:** accepted (2026-07-16). Companion to ADR 0007 (HAL membrane), ADR 0013
(render path in IRAM), ADR 0015 (measure audio cost), and ADR 0021 (performance state is
not preset state).

## Context

The synth has no SD write path. Persistent storage is NVS only
(`platform_storage_save/load`, Stage 2d), which is appropriate for small preset blobs but
not audio. The `SYNTH_PROFILE` RAM tap currently dumps base64 over serial; real captures
lose about 6% of those lines, so the resulting clicks are transport artifacts. A binary SD
capture is the next trustworthy diagnostic and also the first slice of the roadmap's WAV
recording feature.

Tanmatsu has a 4-bit SDMMC microSD slot on slot 0. The checked-in BSP pins are CLK 43,
CMD 44, D0–D3 39–42 (`tanmatsu_hardware.h`). FATFS is configured for three volumes and is
already a component dependency, but the application never mounts a card. The installed
`nicolaielectronics/esp-hosted-tanmatsu` 2.12.3 component contains the closest vetted P4
mount sequence in
`examples/host_sdcard_with_hosted/main/sd_card_functions.c`: slot 0, GPIO-matrix pins,
4-bit width, internal pull-ups, and on-chip SD I/O LDO channel 4.

The feature must ship in normal builds, capture the final master output (after DC block,
limiter, and soft clip), and never allocate, block, log, or touch FATFS on the audio thread.
One row on the PERFORM page controls it. Recording state must never be stored in a patch.

## Decision

### 1. Mount the card behind a reusable platform seam

`platform/platform.h` gains:

- `bool platform_sd_available(void);`
- `const char* platform_sd_root(void);` — `"/sd"` on device and `"./sd"` on host.

The device backend attempts one best-effort mount during `platform_init()`, before audio
starts. It uses `esp_vfs_fat_sdmmc_mount`, `SDMMC_HOST_SLOT_0`, the BSP pin constants,
4-bit mode, internal pull-ups, no auto-format, and on-chip LDO channel 4. Mount failure is
logged and does not prevent the synth from starting. The host backend creates `./sd` and
reports it available. Portable code may then use libc directory/file operations beneath
the returned root; no ESP-IDF filesystem types cross the membrane.

The first version is boot-time mount only: inserting or replacing a card requires an app
restart. Hot-plug/remount and safe eject are separate work because they require coordination
with open recordings and the BSP card-detect event.

`main/CMakeLists.txt` adds `esp_driver_sdmmc`; `fatfs` is already present. The implementation
must cite the installed 2.12.3 example above at the copied mount sequence rather than claim
an unverified upstream commit.

### 2. Cross the real-time boundary once per audio block

The handoff is split by layer:

- `engine/record_ring.{h,cpp}` owns a fixed SPSC block ring. The audio thread converts and
  publishes one completed stereo block; the storage worker pops blocks. It contains no file
  I/O and no platform headers.
- `control/wav_recorder.{h,cpp}` owns directories, filenames, WAV headers, and `FILE*`. A
  dedicated low-priority storage worker is the sole ring consumer and the only thread that
  performs FATFS/libc file operations. The control thread only publishes the requested state
  and reads atomic recorder status, so a slow or wedged card cannot freeze input or UI work.

Reuse `engine/spsc_ring.h`; clarify its comments so producer/consumer roles are generic.
Do not add per-sample atomics. A ring item carries a frame count and holds up to the configured
64 stereo frames as interleaved signed 16-bit PCM. The device backend already renders the
configured 64-frame block directly; the host backend must split a larger miniaudio callback
into chunks of at most 64 before calling the engine. A future backend that submits more than
64 frames to the recorder fails closed as a dropped block rather than truncating or growing
the real-time allocation. A 256-item ring holds 255 full blocks, about 16,320 frames / 340 ms
and roughly 64 KiB of internal DRAM. (The earlier 32,768-frame/128-KiB sketch was about
683 ms, not 341 ms.) The audio path performs one relaxed enabled check per block, converts
with the same finite/clamp/truncate rule as the device DAC, then makes one SPSC publish.

`synth_render` submits `left/right` once after the complete master loop, so both chorus
branches have exactly one capture site. Full-ring behavior is drop-newest, never wait: an
atomic dropped-block counter increments and audio continues. The writer treats any drop as
a recording error, finalizes the valid prefix, and stops rather than silently producing a
deceptive discontinuous take.

### 3. Write recoverable PCM WAV files on a storage worker

`wav_recorder_service(bool want_record)` runs every control-loop iteration but is non-blocking:
it atomically publishes the desired state and snapshots worker-owned state/error. The storage
worker performs the following transitions:

- rising edge: require mounted SD, create `<sd-root>/recordings`, discard stale ring data,
  choose the next unused `rec0001.wav` … `rec9999.wav`, write a 44-byte PCM header, then
  enable the audio producer;
- steady state: pop and `fwrite` all queued blocks;
- falling edge: disable the producer first, drain the committed remainder, patch RIFF/data
  sizes, flush, and close;
- write error, card loss, ring overflow, or RIFF 32-bit size exhaustion: disable, finalize
  the valid prefix if possible, close, report an error, and force the UI toggle off.

Each worker pass drains a bounded block batch and then yields. This prevents the priority-1
storage task from remaining permanently runnable and starving FreeRTOS idle/watchdog work when
the producer and filesystem happen to run at similar rates.

Format is little-endian RIFF/WAVE, 48 kHz, stereo, signed 16-bit PCM: 192,000 bytes/second.
Files live in a dedicated directory so future samples/presets do not mix with recordings.
No existing file is overwritten; exhausting `rec9999.wav` fails visibly.

Once per second while recording, after draining, the storage worker checkpoints the header
sizes and calls `fflush`. A clean stop patches them again. Sudden power loss can therefore
lose or leave unindexed at most the final checkpoint interval instead of leaving an all-zero
length header. This is not a transactional filesystem guarantee; it is bounded damage with
ordinary FATFS/libc.

### 4. The table-driven toggle is session state, not patch state

Add `ParamId::RECORD`, a stepped 0/1 `GROUP_GLOBAL` row named `Record`, and
`FLAG_NO_PRESET = 1 << 3`. It appears automatically on the PERFORM page through the existing
table/page machinery. The app reads the UI shadow/current value to request writer state; the
writer's actual state remains authoritative on failures.

`preset_serialize` excludes `FLAG_NO_PRESET` rows from both its count and entries.
`preset_parse` also filters flagged known IDs, so a crafted or future blob cannot start
recording. Old preset blobs remain valid, the wire version stays v2, and no existing ID is
renumbered. Loading/auditioning a preset leaves the current recording session unchanged.

The UI resets the row to Off and invalidates the PERFORM content band when start/write/drop
fails. A concise status/error indication is required; serial logging alone is not sufficient
for a handheld recorder.

## Consequences

- The mount seam is reusable by sample banks, SD presets, SMF playback, and a direct binary
  diagnostic tap; recorder directory policy stays out of `platform/`.
- Normal-build internal DRAM rises by about 64 KiB, not 128 KiB. PROFILE builds temporarily
  carry both this ring and the diagnostic tap; `make size` must confirm the combined budget.
- The enabled recording path adds float-to-int16 conversion plus one block publication.
  Measure PROFILE audio average/max before and during recording and record the delta per
  ADR 0015; do not infer it from host timing.
- FATFS stalls affect only the low-priority storage worker. A stall longer than roughly 340 ms
  fills the ring; the audio producer drops newest without waiting, and the worker reports a
  visible stopped/error recording when the filesystem call returns. Input, UI, and the audio
  deadline remain independent of card latency.
- First-pass limitations are explicit: boot-time card discovery, PCM16 only, no safe-eject,
  no recording recovery beyond periodic header checkpoints, and a 4 GiB RIFF ceiling.

Implementation is decomposed into closed work-orders in
[`stage-11-sd-recording.md`](../stages/stage-11-sd-recording.md).
