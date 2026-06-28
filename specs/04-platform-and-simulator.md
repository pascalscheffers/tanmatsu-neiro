# Platform & Simulator (host-first)

How the same code runs on the Mac/Linux desktop (primary dev) and the Tanmatsu (deploy).
Decision: ADR 0007. This doc is the concrete shape of the HAL.

## The membrane
```
  ui/  control/  engine/  dsp/          ← portable, no OS/board headers, builds everywhere
  ───────────────────────────────────  ← the membrane: only platform/ crosses it
  platform/
    platform.h            (the 5 HAL interfaces — the contract)
    device/  ESP-IDF + badge-bsp impls
    host/    SDL2 + miniaudio + RtMidi impls
```
**Hard rule:** nothing above `platform/` includes `esp_*`, `bsp/*`, `SDL*`, `miniaudio`,
or `RtMidi`. If it does, it's a layering bug. The host build literally lacks those headers
above the membrane, so violations don't compile.

## The five seams (see ADR 0007 for rationale)
| Seam | Contract | Device impl | Host impl |
|---|---|---|---|
| Audio sink | owns audio thread; pulls `synth_render(L,R,n)` | I2S `bsp_audio_*`, int16 stereo | miniaudio callback |
| Display present | present a shared `pax_buf_t` | `bsp_display_blit` | SDL2 texture upload |
| Input | emit canonical `input_event` | `bsp_input` queue | SDL key/mouse → mapped |
| MIDI transport | emit/consume canonical MIDI | USB host (A) / device (C) | RtMidi |
| Storage | `fopen` over a base path | `/sd` (FATFS via VFS) | local dir |

- **Render contract:** `void synth_render(float* L, float* R, size_t n)` — the single
  audio entry point. The HAL owns the thread and the deadline; the engine just fills
  buffers. Float in the engine; the device sink converts to int16 (ADR 0001 / hardware).
- **No clock seam:** musical time comes from the sample counter (ADR 0010). The HAL only
  exposes wall-clock for UI animation.

## Cost — the HAL is link-time, not a per-sample layer (zero-overhead by construction)
The HAL must not eat the P4 real-time budget. It doesn't, *if built this way*:
- **Only the audio-sink seam is on the deadline, and it's crossed once per block** (~1.33
  ms @ 64/48k), never per sample. All per-sample DSP lives above the HAL in portable
  `engine/`/`dsp/` — the same compiled code on both targets. The other four seams run on
  the non-audio core at frame/event rate.
- **Implement seams as link-time-selected free functions** (`platform_*`), one concrete
  impl linked per build (build system picks `device/` or `host/`). **No vtables, no
  fn-pointer structs, no `if (target)` in the path.** The `synth_render` call is then a
  direct, inlinable call; LTO erases the boundary entirely.
- The only intrinsic per-block cost on device is **float→int16 convert + clamp + copy to
  the I2S DMA buffer** — output work under any design, not abstraction overhead. Keep it a
  tight, vectorizable loop; P4 SIMD only if a profile demands it.
- **Don't conflate with the `IVoice`/`SynthModel` boundary (ADR 0008):** that one *is* a
  runtime interface, but coarse — ~1 indirect call per voice per block (≈8/block),
  negligible. HAL = compile/link-time; SynthModel = per-block; **neither is per-sample.**

## Host tech stack (dev-host only, never shipped to device)
- **SDL2** — window, present, keyboard/mouse. Cross-platform (mac/linux/win).
- **miniaudio** — audio out. Single-header, MIT, zero-dep.
- **RtMidi** — real MIDI gear in/out on the desktop.
- **CMake** — host build (`build-host/`), separate from the IDF build. Same upper-layer
  sources compiled for the host.

## PAX on the host
The UI draws with PAX into an in-RAM `pax_buf_t`; only *presenting* it differs per target.
PAX is portable C with no IDF dependency, so we compile the PAX sources for host and blit
its framebuffer to an SDL texture. (Validate in Stage 0. Fallback if PAX won't build
host-side: a ~100-line software presenter over the same `pax_buf_t` pixel format — small,
and it keeps `ui/` identical either way.)

## Input mapping on host (musical typing + UI)
The computer keyboard drives both the musical-typing note source and the badge-style
navigation, mapped to match the Tanmatsu layout so UI/UX iteration on the Mac transfers
1:1 to the device. Real expressive play uses a MIDI controller via RtMidi.

## Threading / real-time
- **Audio** renders on a dedicated high-priority context: a pinned FreeRTOS task on the
  P4 (one core for audio, the other for UI/MIDI/SD), the miniaudio callback thread on host.
- **Control → audio** communication is a **lock-free SPSC command ring** (events + param
  changes) plus double-buffered/atomic param values. The audio context never locks, logs,
  or allocates (CLAUDE.md RT rules).
- Determinism: an **offline render mode** (run `synth_render` faster-than-real-time to a
  WAV) gives reproducible host DSP tests independent of audio-device timing.

## Acceptance (Stage 0 proves the membrane)
- A sine plays through `synth_render` on **both** host (miniaudio, in an SDL window) and
  device (I2S), from the same engine code, with only `platform/{host,device}/` differing.
- PAX renders text/shapes into the framebuffer and presents on both.
