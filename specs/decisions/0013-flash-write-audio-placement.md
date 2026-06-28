# ADR 0013 — The audio render path survives flash writes (IRAM placement, not auto-suspend)

**Status:** accepted (2026-06-28)

## Context
On the ESP32-P4, a SPI flash write/erase **disables the flash cache** for the duration of
the operation. Any code or constant data that lives in flash and is touched while the cache
is down is unavailable — the result is a stalled task at best, a *"Cache disabled but cached
memory region accessed"* crash at worst. By default all normal code/rodata lives in flash.

Stage 0 has no bug today (nothing writes flash during playback). But the audio render path
(`synth_render`, `audio_task`, `to_i16`, and the DSP under them) currently lives in flash,
and the moment we **save a preset (Stage 2)** — or do **WAV record / SMF-from-SD
(Stage 5–6)** — while audio plays, the audio thread will drop out or crash. This decides
*where the hot audio code and its data live*, i.e. the memory map, so it must be settled
before Stage 2 builds the preset save path rather than discovered as a glitch.

Options considered (cleanest-first per Espressif's flash-concurrency guidance):
- **IRAM placement** — put the render path's *code* in internal IRAM and its *data* in
  internal DRAM (or PSRAM); it then executes through a cache-disable. Targeted; costs scarce
  internal RAM only for the hot path.
- **`CONFIG_SPIRAM_XIP_FROM_PSRAM`** — relocate flash `.text`/`.rodata` to PSRAM so the
  flash cache is never the bottleneck. Whole-program, simplest, but spends PSRAM space and
  bandwidth that we want for shared wavetables.
- **`CONFIG_SPI_FLASH_AUTO_SUSPEND`** — keep the cache reading during writes. Needs a flash
  chip that supports suspend, and Espressif explicitly calls it **unsuitable for
  high-real-time / frequent-ISR** workloads (strict suspend-to-resume timing). Wrong tool
  for an audio deadline.

Key nuance: **PSRAM is not flash.** A flash write does not take down PSRAM access, so shared
read-only wavetables/samples in PSRAM stay reachable during a preset save. Only *flash*-
resident code and rodata are the hazard.

## Decision
**The audio render path stays executable through a flash write by placing its hot code in
internal IRAM and the data/tables it touches in internal DRAM or PSRAM — never in flash
rodata. We do not rely on auto-suspend for the audio deadline.**

- Mark the render call chain `IRAM_ATTR`; keep it small and self-contained so the IRAM cost
  stays bounded (one of the reasons for the short Juno signal flow, ADR 0002).
- Any constant tables the render path reads go in DRAM or PSRAM, not flash `.rodata`
  (`DRAM_ATTR` / PSRAM allocation), so a cache-disable can't strand them.
- Per-voice state and working buffers: internal DRAM (already the Stage 0 placement of the
  audio scratch buffers). Shared wavetables/samples: PSRAM, read-only (ADR 0011, spec 02).
- I2S/any future DMA buffers: `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, word-aligned, with
  `esp_cache_msync` for coherence. Reserve internal RAM for DMA via
  `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`.
- Revisit toward `CONFIG_SPIRAM_XIP_FROM_PSRAM` only if IRAM gets tight and a profile shows
  PSRAM-XIP's bandwidth cost is acceptable. The choice is a *profiled* one (CLAUDE.md).

## Consequences
- The memory map becomes explicit and load-bearing: spec 02 carries a placement table and a
  running IRAM/DRAM/PSRAM budget tracked in `MEMORY.md` alongside `make size`.
- A modest, bounded IRAM cost for the render path. Watch it as voices/FX land; if IRAM
  pressure rises, that's the trigger to reconsider PSRAM-XIP.
- Preset save (Stage 2) and SD I/O (Stage 5–6) can run concurrently with audio without a
  dropout, by construction — the design constraint is recorded before the code exists.
- The exact `IRAM_ATTR` annotations land with the first real DSP (Stage 1), guided by this
  ADR; Stage 0's sine path needs no change beyond the placement policy.
