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
| 11i | preallocate a one-minute contiguous take | high | 11h device repro |
| 11j | measure and prime the first bulk SD write | medium | 11i device repro |
| 11k | run SDMMC at high-speed clock | medium | 11j throughput measurement |
| 11l | profile SD write ceiling and DMA stdio cache | high | 11k |
| 11m | use DMA-capable recorder staging | high | 11l device benchmark |
| 11n | isolate storage worker from core-0 starvation | medium | 11m device retest |
| 11o | separate fresh-media and WAV-alignment cost | medium | 11n device retest |
| 11p | align WAV PCM with RIFF padding | high | 11o device benchmark |

## 11p — Align WAV PCM with RIFF padding

**Status:** implemented; hardware retest pending.

**Repro:** the corrected 11o 2×2 benchmark measured fresh 128 KiB writes at offset 44 at
103 KiB/s and overwrite traffic there at 129 KiB/s, both below the recorder's required
187.5 KiB/s. The identical fresh workload at offset 4096 reached 681 KiB/s and its overwrite
reached 3,385 KiB/s. The recorder still starts PCM at byte 44, takes 238–260 ms per 32 KiB,
and overflows after three writes.

**Root cause:** the minimal 44-byte WAV layout permanently misaligns every bulk PCM write on
this FatFs/SD path. Fresh-media cost is not the blocker when the payload is sector-aligned.
ADR 0024 now ratifies a legal RIFF `JUNK` chunk that places the `data` payload at byte 4096.

**Touch list (6):** `control/wav_recorder.cpp`, `tests/host/test_wav_recorder.cpp`,
`specs/decisions/0024-sd-recording.md`, `specs/stages/stage-11-sd-recording.md`,
`specs/02-synth-architecture.md`, `specs/MEMORY.md`.

**Read list (5):** this 11p work-order; ADR 0024 §3; `control/wav_recorder.cpp:constants/
make_header/patch_header/finish/start`; `tests/host/test_wav_recorder.cpp:assert_header and WAV
suite`; `specs/02-synth-architecture.md:device memory/flash budget table`.

**Reuse:** the existing 32 KiB DMA-capable staging buffer, RIFF little-endian helpers, prime,
checkpoint/finalization paths, and all recorder tests. RIFF layout is fixed: 12-byte RIFF/WAVE,
24-byte `fmt ` chunk, `JUNK` header at byte 36 with 4,044 payload bytes, `data` header at byte
4088, and PCM at byte 4096. No new allocation, buffer, seam, codec, or sample-format change.

**Don't read:** DSP/voice sources, platform backends, SD profiler implementation, managed
components, unrelated tests, other stage docs, or `MEMORY-archive.md`.

**Implementation:** introduce named constants for the 4096-byte data offset and header field
locations; remove recorder magic offsets 44/40/36. Build the complete padded header in the
existing staging buffer after its pre-capture 32 KiB zero prime, then write exactly 4096 header
bytes. Set RIFF size to `file_size - 8`, emit `JUNK`/4044 and `data`/data-size at the ratified
offsets. Prime and every live PCM write begin at byte 4096. Preallocate exactly
`4096 + 60 * 192000` bytes; checkpoint seeks back to `4096 + committed`; every finalization
truncates there. Update host assertions to prove the JUNK and data chunk positions, zero padding,
exact payload ordering at 4096, corrected checkpoint fields/cursor, preallocated size, clean/error
final lengths, and a write failure beyond the padded header. Do not alter capture format,
staging/ring sizes, drop policy, task scheduling, or the 60-second limit. Retain the 11o
PROFILE benchmark until the hardware acceptance run is complete.

**Acceptance:** `make format`, `make test`, `make host`, `make build`, `make size`, and
`make PROFILE=1 build` pass; membrane grep and `git diff --check` are clean. Update the budget
table and append a tight MEMORY entry. Commit one atomic fix. Required hardware retest: record
for more than 10 seconds, stop cleanly, and capture prime/write/checkpoint/finish plus audio/I2S
PROFILE lines. Every steady 32 KiB write must stay below 170.7 ms, dropped blocks must remain
zero, audio overruns and I2S errors/short writes must remain zero, and the finalized WAV must
open with the correct duration in a standard player.

**Split-if:** the existing staging buffer cannot safely host the 4096-byte header, a standard WAV
reader rejects the JUNK-padded host artifact, or any recorder path still requires a non-aligned
PCM write. Stop without changing the sample format or enlarging the ring.

## 11o — Separate fresh-media and WAV-alignment cost

**Status:** implemented; hardware retest pending.

**Repro:** 11n is active (`storage worker started core=1 priority=1`) but the recorder still
takes 238–260 ms per 32 KiB and overflows after 98,304 committed bytes. The 11l benchmark's
first pass over its newly allocated 128 KiB extent took 1,124 ms (113 KiB/s), while every later
case reopened and overwrote that same extent in 24–67 ms. It therefore did not establish a
5.5 MiB/s fresh-write ceiling. It also wrote at offset 0, while WAV PCM starts at offset 44 and
forces a partial-sector path around otherwise bulk-aligned transfers.

**Root cause:** the earlier benchmark compared aligned overwrite traffic with unaligned fresh
recording traffic, so its scheduling and DMA-buffer conclusions were based on a false ceiling.
The current device evidence proves sustained recorder throughput is below 187.5 KiB/s, but does
not yet distinguish fresh-media latency from the 44-byte payload offset. Measure that 2×2 split
before changing the RIFF layout or physically initializing an entire take.

**Touch list (3):** `platform/device/sd_profile.c`, `specs/MEMORY.md`, this file.

**Read list (3):** this 11o work-order; `platform/device/sd_profile.c:sd_profile_run/
benchmark_case`; `sniff.log:SD setup/sdbench/storage worker/record write`.

**Reuse:** `esp_vfs_fat_create_contiguous_file`, the existing 32 KiB internal DMA-capable
source buffer, `fopen`/`fseek`/`fwrite`/`fflush`, and PROFILE-only boot invocation. No recorder,
task, ring, filesystem configuration, or public seam change.

**Don't read:** recorder implementation, DSP/voice sources, host backend, managed components,
other stage docs, tests, or `MEMORY-archive.md`.

**Implementation:** replace the misleading multi-chunk/cache benchmark with two simultaneously
allocated temporary extents so neither case can reuse the other's sectors. One extent reserves
128 KiB of payload beginning at offset 44; the other reserves the same payload beginning at
offset 4096, representing a legal sector-aligned padded RIFF layout. For each extent, measure
exactly 128 KiB as four 32 KiB `fwrite` calls plus `fflush`, first on the newly allocated extent
and then after reopen/seek as an overwrite. Keep the same explicit DMA-capable source. Log
`phase=fresh|overwrite`, `offset=44|4096`, chunk, bytes, elapsed milliseconds, and KiB/s. Create
and retain both distinct files before either fresh pass; clean up both files and all allocations
on every error path while preserving the first error. Remove the unused stdio-cache allocation
and old chunk-size matrix. Do not modify the live recorder based on the result in this job.

**Acceptance:** `make format`, `make test`, `make host`, `make build`, and
`make PROFILE=1 build` pass; membrane grep is clean. Commit one atomic diagnostic correction and
append a tight `MEMORY.md` entry that marks 11n's root-cause claim falsified. Required hardware
retest: capture all four new `sdbench` lines plus one recorder attempt. If fresh offset 4096 is
above 187.5 KiB/s while fresh offset 44 is below it, author the padded-RIFF fix. If both fresh
cases are below the producer, choose between whole-take physical initialization and visible card
rejection; do not enlarge the finite ring.

**Split-if:** two distinct contiguous temporary extents cannot be kept allocated concurrently,
or the benchmark cannot guarantee the fresh passes precede their corresponding overwrites. Stop
without making a recorder change.

## 11n — Isolate storage worker from core-0 starvation

**Repro:** with 11m's explicitly DMA-capable recorder staging, the pre-capture prime still takes
242 ms and live 32 KiB writes take 237–256 ms, overflowing after the second write. The same card,
four-bit 40 MHz bus, FatFs path, transfer size, and DMA-capable source write 32 KiB in about 24 ms
in the 11l boot benchmark. Audio remains below budget. The boot benchmark runs before render,
control, and USB-host tasks start; the storage worker is later pinned to core 0 at priority 1,
below control 5, MIDI class 3, render 2, and USB daemon 2.

**Root cause:** the DMA and filesystem paths can exceed the required throughput by a wide margin;
the remaining measured difference is core-0 scheduling. Wall-clock `fwrite` time grows by roughly
10x only after higher-priority core-0 tasks exist. Move the low-priority storage worker to core 1,
where the audio task at `configMAX_PRIORITIES-2` always preempts it, rather than raising storage
above latency-sensitive MIDI/render work on core 0.

**Touch list (3):** `platform/device/platform_device.c`, `specs/MEMORY.md`, this file.

**Read list (2):** this 11n work-order;
`platform/device/platform_device.c:audio task/render priority/storage worker`.

**Reuse:** the existing pinned FreeRTOS storage task, `STORAGE_PRIO=1`, audio task core 1 and high
priority, and PROFILE logs. No new task, seam, queue, or buffer.

**Don't read:** recorder implementation, DSP/voice sources, SD profiler, host backend, managed
components, other stage docs, tests, or `MEMORY-archive.md`.

**Implementation:** pin the existing storage task to core 1 instead of core 0. Keep priority 1,
stack, lifecycle, and callback unchanged. Update the priority/affinity comment to document that
audio always preempts storage and that storage no longer competes with control/render/USB on
core 0. In PROFILE builds, log the storage worker's configured core and priority once when it
starts. Do not change audio affinity/priority, storage priority, SD settings, buffering, or
recorder policy.

**Acceptance:** `make format`, `make test`, `make host`, `make build`, and
`make PROFILE=1 build` pass; membrane grep is clean. Commit one atomic fix and append a tight
`MEMORY.md` entry. Required hardware retest: confirm the worker log reports core 1 / priority 1,
record for >10 s, and capture `record write`, checkpoint, finish, audio, and I2S PROFILE lines.
Writes must average below 170.7 ms without audio overruns or new I2S errors/short writes.

**Split-if:** the target is configured unicore or core 1 is unavailable. Stop without raising
storage priority on core 0.

## 11m — Use DMA-capable recorder staging

**Repro:** the 11l device benchmark negotiated four-bit SDMMC at 40 MHz and wrote a contiguous
128 KiB test extent at 5,463 KiB/s with 32 KiB transfers from an explicitly DMA-capable source.
The live recorder still takes 234–246 ms per 32 KiB (about 136–139 KiB/s), over 39 times slower
than the unloaded ceiling and below the required 187.5 KiB/s. Audio remains below budget. The
benchmark source comes from `heap_caps_malloc(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)`; the recorder
source is a static `.bss` array whose SDMMC DMA capability is not explicit.

**Root cause:** the filesystem, card, bus width, and clock can comfortably sustain the stream.
The remaining controlled difference is the live write source buffer. Make recorder staging
explicitly suitable for device SD DMA before changing scheduling or adding another queue. The
11l A/B also shows a DMA stdio cache adds no throughput at 32 KiB when the source is already
DMA-capable, so do not add a second cache buffer yet.

**Touch list (8):** `platform/platform.h`, `platform/device/platform_device.c`,
`platform/host/platform_host.c`, `control/wav_recorder.cpp`, `tests/host/test_wav_recorder.cpp`,
`specs/MAP.md`, `specs/MEMORY.md`, this file.

**Read list (5):** this 11m work-order; `platform/platform.h:SD card`;
`control/wav_recorder.cpp:staging globals/flush_staging/wav_recorder_init/shutdown`;
both platform backends' `platform_sd_preallocate`; `tests/host/test_wav_recorder.cpp:platform
fakes and suite lifecycle`.

**Reuse:** device `heap_caps_malloc` with `MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL`, host `malloc/free`,
the existing 32 KiB staging capacity and recorder lifecycle. Allocation remains off the audio
thread and occurs once at recorder initialization.

**Don't read:** DSP/voice sources, SD profiler implementation, managed components, other stage
docs, unrelated tests, or `MEMORY-archive.md`.

**Implementation:** add portable `platform_sd_alloc_io_buffer(size)` and
`platform_sd_free_io_buffer(ptr)` functions. Device returns internal DMA-capable memory; host
uses ordinary heap memory. Document that this is storage-worker memory, never audio-thread
allocation. Replace the static recorder staging array with a pointer allocated once before the
storage worker starts. If allocation fails, expose the existing WORKER_START error and do not
start capture. Free only after the worker has stopped successfully; if bounded stop fails, retain
the buffer because the worker may still reference it. Re-init after a successful shutdown must
allocate and work again. Do not add `setvbuf`, change the 32 KiB size, change write cadence, or
touch the audio ring. Extend host fakes/tests to cover allocation failure and successful
shutdown/re-init without leaking or double-freeing.

**Acceptance:** `make format`, `make test`, `make host`, `make build`, and
`make PROFILE=1 build` pass; membrane grep is clean. Commit one atomic fix and append a tight
`MEMORY.md` entry with normal/PROFILE size changes. Required hardware retest: record for >10 s
and capture all `record write`, checkpoint, and finish events. A 32 KiB live write must fall
below 170.7 ms to sustain PCM; compare it with the prior 234–246 ms range.

**Split-if:** allocation/free cannot be expressed without exposing ESP heap capabilities above
`platform/`, or the recorder lifecycle can free while a failed-to-stop worker is live. Stop with
the exact conflict rather than adding a global device-only include to control code.

## 11l — Profile SD write ceiling and DMA stdio cache

**Repro:** at the original 20 MHz mount, live 32 KiB writes sustain only about 138 KiB/s
(236–237 ms each), below the recorder's required 187.5 KiB/s. A proposed upstream helper wraps
`fopen` with an internal `MALLOC_CAP_DMA` buffer installed through `setvbuf`, but that code does
not itself enable peripheral DMA. The checked-in Tanmatsu BSP declares a four-bit SDMMC bus
(`BSP_SDCARD_WIDTH=4`, D0–D3 on GPIO 39–42), while the physical-link width is disputed and must
be reported from the initialized host rather than assumed.

**Root cause:** the current live timing mixes card/bus throughput, stdio/FatFS overhead, and
core-0 scheduling. Before another recorder architecture change, measure the unloaded filesystem
ceiling across transfer sizes and A/B the upstream DMA-capable stdio-cache technique.

**Touch list (6):** `platform/device/sd_profile.h`, `platform/device/sd_profile.c`,
`platform/device/platform_device.c`, `main/CMakeLists.txt`, `specs/MEMORY.md`, this file.

**Read list (5):** this 11l work-order; `platform/device/platform_device.c:mount_sd_card`;
`main/CMakeLists.txt:idf_component_register`; the user-supplied `asp_fastopen` sample in this
work-order; `sdkconfig_tanmatsu:FAT Filesystem support`.

**Reuse:** `esp_vfs_fat_create_contiguous_file`, `heap_caps_malloc` with
`MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL`, `setvbuf`, `esp_timer_get_time`, and the mounted `/sd`
filesystem. Keep the live recorder and its per-write profiler unchanged.

**Don't read:** recorder implementation, DSP/voice sources, host backend, managed components,
other stage docs, tests, or `MEMORY-archive.md`.

**Implementation:** add a device-only `sd_profile_run(root)` module called once immediately
after a successful mount, compiled to a no-op unless `SYNTH_PROFILE` is defined. It creates and
contiguously preallocates a temporary 128 KiB file beneath the SD root, allocates a 32 KiB
DMA-capable internal source buffer, and benchmarks exactly 128 KiB of `fwrite`+`fflush` traffic
at 4, 8, 16, and 32 KiB transfer sizes. Run each size twice using a freshly opened stream:
(a) libc's default buffering; (b) a separate 32 KiB internal DMA-capable buffer installed with
`setvbuf` before any stream I/O, matching the supplied fast-open technique. Log mode, chunk,
bytes, elapsed milliseconds, and integer KiB/s; also log whether both allocated buffers are
DMA-capable. Close between cases, preserve the first error, remove the temporary file, and free
both buffers on every path. A benchmark failure logs and returns without making SD unavailable.
Do not run this diagnostic in normal builds. Extend the successful mount log with the actual
host bus width from `sdmmc_host_get_slot_width` as well as negotiated frequency. Do not change
the live recorder, task priorities, ring/staging capacity, sample format, or filesystem config.

**Acceptance:** `make format`, `make test`, `make host`, normal `make build`, and
`make PROFILE=1 build` pass; membrane grep is clean. Commit one atomic diagnostic feature and
append a tight `MEMORY.md` entry. Required hardware retest: capture boot `SD setup` and all
`sdbench` lines, then make one live recording and capture `record write` lines. Compare the best
unloaded KiB/s with the live rate and the required 187.5 KiB/s. Retain a DMA stdio cache in the
recorder only in a later work-order if the A/B result materially improves throughput.

**Split-if:** the contiguous helper cannot safely reuse a named temporary file, `setvbuf` cannot
be called before I/O on a fresh stream, or cleanup requires a new public platform seam. Stop
without changing the recorder.

## 11k — Run SDMMC at high-speed clock

**Repro:** after 11j, every full 32 KiB write takes 236–237 ms, including after the priming
write. That is about 138 KiB/s sustained versus 187.5 KiB/s required by 48 kHz stereo PCM16;
the second live write reports 36 dropped blocks. Audio remains below budget. The mounted host
uses `SDMMC_HOST_DEFAULT()`, whose ESP-IDF 5.5.1 definition selects the 20 MHz default clock.

**Root cause:** recording failure is now a sustained SD throughput deficit, not a cold-write,
allocation, buffer-capacity, or audio-CPU problem. ESP-IDF's own SD-card FATFS performance setup
selects `SDMMC_FREQ_HIGHSPEED` (40 MHz). Test that single bus-speed variable before changing task
priority or buffering architecture.

**Touch list (3):** `platform/device/platform_device.c`, `specs/MEMORY.md`, this file.

**Read list (3):** this 11k work-order; `platform/device/platform_device.c:mount_sd_card`;
`esp-idf/components/esp_driver_sdmmc/include/driver/sdmmc_default_configs.h:SDMMC_HOST_DEFAULT`.

**Reuse:** `SDMMC_FREQ_HIGHSPEED`, the existing mount sequence, and the existing successful-card
log. No new seam, task, buffer, or dependency.

**Don't read:** recorder implementation, DSP/voice sources, other platform code, managed
components, other stage docs, tests, or `MEMORY-archive.md`.

**Implementation:** set `host.max_freq_khz = SDMMC_FREQ_HIGHSPEED` before mount. Extend the
successful card-discovery log with the card's negotiated `max_freq_khz` so the sniff log proves
whether high-speed mode was accepted. Do not change bus width, allocation unit, storage priority,
staging size, ring size, or recorder policy.

**Acceptance:** `make format`, `make test`, `make host`, and `make PROFILE=1 build` pass;
membrane grep is clean. Commit one atomic fix and append a tight `MEMORY.md` entry. Required
hardware retest: confirm the boot log reports 40000 kHz, record for >10 s, and capture all
`record write`, checkpoint, and finish events. Full-write latency must average below 170.7 ms
to sustain the 187.5 KiB/s stream; retain the setting only if there are no mount/write errors.

**Split-if:** the installed ESP-IDF lacks `SDMMC_FREQ_HIGHSPEED` or the mounted card structure
does not expose negotiated `max_freq_khz`. Stop without adding a private-IDF dependency.

## 11j — Measure and prime the first bulk SD write

**Repro:** after 11i reports successful contiguous preallocation, recording still stops on
the first 32 KiB staging flush. The exact prefix is 32,768 committed bytes plus 65,280 bytes
drained from the 255 usable ring slots, for a finalized 98,048-byte file. Audio remains below
budget. Therefore the first bulk `fwrite` blocks for longer than the ring's ~340 ms reserve;
buffer capacity and audio CPU are not the current unknowns.

**Root cause:** contiguous FAT allocation removes live cluster-chain allocation, but it does
not prove that the card's first physical bulk write is latency-safe. The current profiler has
no duration around `fwrite`, so it cannot distinguish a one-time cold write/erase stall from
repeated slow 32 KiB transfers.

**Touch list (3):** `control/wav_recorder.cpp`, `specs/MEMORY.md`, this file.

**Read list (3):** this 11j work-order; `control/wav_recorder.cpp:flush_staging/start`;
`platform/platform.h:platform_millis`.

**Reuse:** the existing 32 KiB staging buffer, `platform_millis`, preallocated stream,
PROFILE logging, and current header/cursor accounting. No new seam or allocation.

**Don't read:** DSP/voice sources, platform backend implementations, managed components,
other stage docs, tests, or `MEMORY-archive.md`.

**Implementation:** in PROFILE builds, time every `flush_staging()` filesystem call and log
requested bytes, committed bytes, elapsed milliseconds, cumulative committed bytes, and the
current dropped-block count after it returns. During `start()`, after opening the preallocated
file and before writing the real zero-length header or enabling capture, write exactly one full
32 KiB zero-filled staging buffer at byte 44, flush it, log its duration with a distinct
`record prime` event, then seek back to byte 0. Treat any short write, flush, or seek failure as
the existing WRITE error. This is pre-capture housekeeping: do not add the priming bytes to
`s_data_bytes`, and reset staging state before RECORDING. The final file remains truncated to
the genuine captured prefix. Keep non-PROFILE behavior identical except for the priming write.

**Acceptance:** `make format`, `make test`, `make host`, and `make PROFILE=1 build` pass;
membrane grep is clean. Commit one atomic diagnostic fix and append a tight `MEMORY.md` entry.
Required hardware retest: record until either >10 s or failure and capture all `record prime`,
`record write`, checkpoint, and finish events. If priming is slow but later writes sustain,
retain it; if later writes also exceed reserve, stop and use the timings to choose the next
architecture change rather than enlarging another buffer blindly.

**Split-if:** the priming write changes or extends the portable public seam, or the existing
staging buffer cannot be safely reused before capture. Stop without implementing.

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

## 11i — Preallocate a one-minute contiguous take

**Repro:** after 11h, the first 32 KiB filesystem write commits, then blocks for longer than the
255-block audio ring's ~340 ms reserve. The error prefix is exact: 32,768 committed bytes plus
65,280 drained ring bytes gives the final 98,048-byte valid prefix. Header creation and flush
already complete before capture starts, so header-only housekeeping does not prevent the stall.

**Root cause:** FAT cluster allocation still occurs in the live recording path. The storage
worker cannot drain the ring while blocked in `fwrite`, so any allocation/write stall longer than
the ring reserve causes the deliberate drop-newest error. ESP-IDF 5.5 provides
`esp_vfs_fat_create_contiguous_file(..., alloc_now=true)`, a vetted wrapper over FatFs
`f_expand`, for allocating the cluster chain before opening the stdio stream.

**Touch list (8):** `platform/platform.h`, `platform/device/platform_device.c`,
`platform/host/platform_host.c`, `control/wav_recorder.cpp`,
`tests/host/test_wav_recorder.cpp`, `specs/MAP.md`, `specs/MEMORY.md`, this file.

**Read list (5):** this 11i work-order; ADR 0024 §2–3; `control/wav_recorder.cpp:start/finish/
patch_header/drain_ring_batch`; `platform/platform.h:SD card`; both platform backends'
`platform_sd_available/platform_sd_root` implementations.

**Reuse:** ESP-IDF's `esp_vfs_fat_create_contiguous_file` with `alloc_now=true`, the existing
storage-worker start transition, `WAV_RECORDER_ERROR_WRITE`, libc `truncate`, and the current WAV
header/checkpoint/finalization machinery. Do not add a second writer or enlarge the audio ring.

**Don't read:** DSP/voice sources, managed-component sources, other stage docs,
`MEMORY-archive.md`, or unrelated platform code.

**Implementation:** add a path-based portable platform preallocation seam. Device creates an
empty contiguous file of exactly `44 + 60 * 192000 = 11,520,044` bytes using the ESP-IDF helper;
host creates the same logical extent with a sparse `ftruncate` so tests stay cheap. On failure,
remove any partial file and report the existing write error. In `start()`, complete path choice,
preallocation, reopen in update mode, zero-length WAV-header write+flush, and seek to byte 44
before clearing the ring, publishing RECORDING, or enabling capture. Cap accepted PCM at exactly
11,520,000 data bytes so the live path never extends the file. Header checkpointing must restore
the data cursor to `44 + committed_bytes`, not the preallocated physical end. Every finalization
path closes and truncates the file to `44 + committed_bytes`; preserve the first meaningful error.
PROFILE logs must distinguish preallocation begin/success/failure. Do not add an arming enum: the
existing IDLE snapshot is authoritative until all housekeeping succeeds.

**Acceptance:** a host regression stalls preallocation and proves state remains IDLE and the
audio ring remains disabled; successful start observes the preallocated logical extent before
RECORDING; clean stop and checkpoint tests prove exact header/payload and no zero tail; a forced
preallocation failure enters visible WRITE error without enabling capture or leaving a file.
Existing overflow, card-loss, and write-failure tests remain green. `make format`, `make test`,
`make host`, `make build`, `make size`, and membrane grep pass. Commit atomically and append a
tight MEMORY entry. Required device retest: arm Record, note arming latency, record for 10–60 s,
stop, inspect exact WAV duration/size, and capture PROFILE recorder events.

**Split-if:** the ESP-IDF contiguous-file helper is unavailable to the device target, host sparse
preallocation is not portable within the current build, or final truncation needs a new public
filesystem-handle abstraction. Stop with the exact conflict rather than falling back to writing
zeros or allocating during capture.

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
