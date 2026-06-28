# Progress Log

Newest at the bottom. One entry per stage/session. Lean — link to specs, don't restate.

## 2026-06-27 — Project bootstrap
- Cloned `tanmatsu-template` into this repo; git is local-only (template kept as remote
  `upstream-template`, no push remote).
- Wrote `CLAUDE.md` (workflow, dedup/reuse policy, real-time audio rules) and initial
  specs `00`–`03`.
- Key findings: Tanmatsu audio is **output-only** (ES8156 stereo I2S DAC, no audio-in);
  USB-C = MIDI device, USB-A = MIDI host; ESP32-P4 has FPU + SIMD; 768 KB SRAM + 32 MB
  PSRAM. → synth is fully digital VA+hybrid.
- Chosen reuse anchor: **Mutable Instruments STM32 code (plaits/braids/stmlib), MIT.**
- Launched background agent to set up the ESP-IDF build env and report BSP audio/USB APIs.
- **Build env GREEN:** ESP-IDF v5.5.1 + RISC-V toolchain installed; `make build
  DEVICE=tanmatsu` succeeds. Baseline `application.bin` = 987 KB, 52% of the 2 MB app
  partition free. (First agent stalled on the toolchain download watchdog; finished the
  compile directly in a harness-tracked job.)
- **Audio API confirmed** (`bsp/audio.h`): I2S 16-bit stereo interleaved, default 44.1k
  (`bsp_audio_set_rate(48000)` to change), `bsp_audio_get_i2s_handle` →
  `i2s_channel_write`; hw volume + amp toggle. Codec = `es8156` component.
- **Decisions ratified** (ADRs 0001–0006 in `specs/decisions/`): VA/hybrid digital
  target · Juno-106 hybrid voice · 8 voices + unison · permissive-only vendoring · USB-A
  host MIDI first · v1 screen+keyboard only. Architecture spec promoted from draft.
- **Next:** Stage 0 — hello-audio: set rate 48k, get I2S handle, write a sine block loop,
  confirm clean output on speaker + headphones. Then Stage 1: vendor MI macro-osc + VA
  filter + ADSR for one voice with host-side DSP tests.

## 2026-06-27 — Full requirements interview → architecture plan
- Ran a 5-round requirements grill. Decisions ratified as ADRs 0007–0010 and three new
  specs (`04` platform/sim, `05` data model, `06` feature scope + roadmap):
  - **Host-first** dev on a thin **platform HAL** (5 seams: audio sink, display present,
    input, MIDI, storage); host stack = SDL2 + miniaudio + RtMidi; same UI in a window.
  - Synth = host for swappable **SynthModels** (`param_table` + `IVoice` factory), Juno-106
    first; **MPE-ready** voices; mono-timbral but split/layer-ready.
  - **Modulation matrix** core; Juno routings ship as the default patch.
  - **Sample-accurate clock** derived from the audio sample counter; pluggable source
    (internal+tap now, external MIDI-clock later); one event scheduler for MIDI/typing/arp/seq.
  - Features locked: play modes poly+mono/porta+unison+legato; FX chorus+delay+reverb;
    full arp; sequencer = step + real-time in one pattern model (param-locks); simple SMF
    player; full preset library (banks/browser/A·B/INIT/random·morph); full performance
    layer (macros/bend·mod/vel·AT/sustain·panic); reach goals = USB-audio-class out, WAV
    record, MPE (scales/microtuning left as a cheap hook only).
- MVP line = one poly Juno voice + chorus, played from musical typing.
- Naming palette drafted in `notes/naming.md` (aviation/Dutch instrument + chip/demoscene
  engine names; leads: **Fokker**, **Klang**). Awaiting Pascal's pick — not blocking.
- **Next:** build **Stage 0** (hello-audio + the HAL membrane on host + device).

## 2026-06-28 — Stage 0: hello audio + the membrane (ADR 0007 proven)
- Built the **platform HAL membrane**. New portable top-level layers compile for *both*
  targets from one source tree: `engine/synth.{h,c}` (220 Hz sine), `ui/ui.{h,c}` (PAX
  hello screen + sweep bar), `app/app.{h,c}` (shared init + main loop). Contract in
  `platform/platform.h` — 6 calls: init, framebuffer, present, audio_start/stop,
  poll_event, millis, sleep_ms. MIDI + storage seams deferred (no consumer yet).
- **Host backend** `platform/host/platform_host.c` = SDL2 + miniaudio; `main()` lives here.
  Build via `make host` → `build-host/tanmatsu-synth-host` (CMake in `host/CMakeLists.txt`,
  separate from IDF). **Verified: builds + runs clean on the Mac** (window + audio device
  init, no crash). Audible-sine / visible-window is the human check via `make host-run`.
- **Device backend** `platform/device/platform_device.c` = BSP display/input + a pinned
  (core 1, prio MAX-2) FreeRTOS audio task: render→int16 interleave→`i2s_channel_write`
  (blocking DMA = the deadline); all buffers preallocated (RT rules). `main/main.c` is now
  a 3-line shim → `app_run()`; `main/CMakeLists.txt` pulls the portable sources via `../`.
  **Verified: `make build DEVICE=tanmatsu` clean. app size 0xe4820 ≈ 936 KB, 55% partition
  free** (was 987 KB baseline). On-hardware audio check pending a board.
- **Membrane holds by construction:** `grep` for esp_/bsp/SDL/miniaudio above the line is
  clean; the host build literally lacks those headers.
- Findings: PAX builds host-side unmodified except two glibc-isms — needed compat shims
  `host/compat/{endian.h,malloc.h}` (Apple-only include path; PAX's ESP includes are
  `#ifdef ESP_PLATFORM`-guarded). PAX gui target excluded on host (EXCLUDE_FROM_ALL; pulls
  ESP deps, unused in Stage 0). Correction to the hardware note: `bsp_audio_set_rate` only
  reconfigs the I2S clock (needs the channel *disabled*), it does not tear down/recreate —
  so audio_start does disable→set_rate→enable to honor 48 kHz.
- Block size **64 @ 48 kHz**, stereo float `[-1,1]`. `clang-format` lives at
  `/opt/homebrew/opt/llvm/bin` (not on PATH; `make format` fails without it).
- **Post-Stage-0 review** against ADRs that landed mid-stage (0011, spec 07):
  - **ADR 0011 (device-optimal, host adapts):** *audio* already compliant (engine emits
    planar float; device int16 is intrinsic I2S; host interleaves). *Display* was not —
    fixed: the host now renders into a **device-native** `pax_buf` (24_888RGB) and converts
    to the SDL ARGB texture in `platform_present` via `pax_get_index_conv` (host pays the
    tax). Bonus: the sim now shows the device's exact color depth (spec 04 1:1 goal).
  - **Spec 07 (upstream-first):** logged the confirmed PAX host-build findings (two glibc
    headers + unguarded esp includes in gui) and corrected the `bsp_audio_set_rate` note.
    Compat shims marked `TODO(upstream)`. **Flagged for Pascal** — PAX portability is the
    ideal first upstream PR (author = robotman2412).
- **Next:** Stage 1 — the SynthModel/IVoice boundary (ADR 0008) + Juno voice (MI macro-osc
  + VA filter + ADSR), 8-voice allocator, master chorus, musical typing, host DSP tests.

## 2026-06-28 — Embedded-practices research → ADRs 0012/0013 + spec 08
- Researched embedded/hardware-team practices vs the general-purpose-machine assumptions
  baked into the early specs. Two findings high-impact enough to ratify; rest = workflow.
- **ADR 0012 — no hardware FTZ on the P4.** RISC-V `RV32F` has no flush-to-zero bit (x86
  host *and* the ARM/Daisy port of MI code do). → denormal suppression is **mandatory in
  software** in every filter/feedback block; host offline-render tests run **FTZ-off** to
  reproduce device behavior (corollary of ADR 0011). Fixed CLAUDE.md RT rule #7 (was
  "flush-to-zero where available" — false on this chip). Audit MI stmlib guards on vendor.
- **ADR 0013 — flash writes stall the audio path.** A flash write/erase disables the flash
  cache on the P4; the render path lives in flash today, so a Stage 2 preset save (or SD
  WAV/SMF later) would glitch/crash audio. Decision: place the render chain in **IRAM**
  (`IRAM_ATTR`), its tables in DRAM/PSRAM (never flash `.rodata`); PSRAM stays reachable
  during a flash write so shared wavetables are safe. Rejected auto-suspend (unsuitable for
  real-time). `IRAM_ATTR` annotations land with Stage 1 DSP. Spec 02 now carries a memory-
  **placement table** + a running IRAM/DRAM/PSRAM **budget** (Stage 0 row seeded).
- **Spec 08 (new) — embedded practices**, hobby-sized: on-target audio stats (cycle %/
  underruns/stack HWM, land Stage 1), CI-without-hardware (host+device build, format, host
  DSP tests), golden-file render regression tests (FTZ-off), coredump+WDT+stack-overflow
  safety nets, `dependencies.lock`. The big wins (pure core + dual build) are already done.
- **Stage 0 code fixes applied + device build re-verified clean** (0xe4880 ≈ 936 KB, 55%
  free): `to_i16` now guards NaN/Inf→0 before the DAC; `s_audio_run` is `_Atomic`
  (cross-core flag). `platform/device/platform_device.c` only — membrane intact.
