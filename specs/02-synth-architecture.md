# Synth Architecture

Status: **ratified** (2026-06-27). The defining choices are locked in `specs/decisions/`
(ADRs 0001–0006); this document is the design they describe. Revisit by writing a new ADR.

Locked: VA/hybrid digital target (0001) · Juno-106 hybrid voice (0002) · 8 voices +
unison (0003) · permissive-only vendoring (0004) · USB-A host MIDI first (0005) · v1
screen+keyboard only (0006) · host-first + platform HAL (0007) · swappable SynthModels,
MPE-ready (0008) · modulation matrix (0009) · sample-accurate clock (0010).

**Companion specs:** platform/simulator → `04`; data model (param table, presets,
patterns) → `05`; full feature scope + staged roadmap → `06`. This file is the DSP/voice
design; those cover how it's hosted, stored, and sequenced.

## Design goals (from Pascal)
- Polyphonic, **fat bass**, **sparkling highs**.
- Analog modeling, **hybrid**: wavetable → VA filter, and/or analog+FM hybrid.
- **Live, easy parameter tweaking** from the badge UI.
- **USB MIDI** (be a device and/or host a controller).
- Memory-limited target → **dedup and reuse are essential**.

## Recommended sonic base: Juno-style poly skeleton, hybrid voice

**Recommendation: a Roland Juno-106-inspired voice architecture, with the single DCO
replaced by a hybrid macro-oscillator (wavetable + VA + 2-op FM).**

Why the Juno-106 as the skeleton:
- Simplest classic polysynth that still sounds *rich* → cheapest per-voice CPU → more
  polyphony on a fixed budget. AI-maintainable because the signal flow is short.
- **Fat bass** comes naturally: sub-oscillator + the famous Juno chorus.
- **Sparkling highs** come from the resonant filter + chorus, and (our addition) the FM
  mode for bell/glassy timbres.
- Enormous reference material and samples for validation.

The **hybrid twist**: where the Juno had one DCO + sub, we use one *macro-oscillator*
that can be a VA shape (saw/pulse/tri), a wavetable scan, or a 2-operator FM pair — plus
the sub-osc. This folds "analog osc", "wavetable osc", and "FM" into **one engine** (the
core dedup move) instead of three parallel oscillator types.

### Per-voice signal flow
```
  [Macro-osc]  mode ∈ {VA saw/pulse/tri, wavetable scan, 2-op FM}   ─┐
  [Sub-osc]    -1 oct square/sine, level                            ─┼─► [mix] ─► [VA filter
  [Noise]      white, level                                         ─┘            LP/BP/HP,
                                                                                  cutoff+res]
                                                                                       │
                                              [VCA] ◄── [Amp env ADSR]                 ▼
                                                                                  ► voice out
  Modulators per voice: 2× ADSR (amp, filter/mod), 1–2× LFO, velocity, key-track.
```

### Global / master bus
```
  Σ voices ─► [Chorus (Juno-style I/II)] ─► [optional reverb/delay] ─► [soft clip] ─► I2S DAC (stereo)
```
The chorus is doing a lot of the "Juno" character and the stereo width — high priority.

## Code reuse map (reuse before writing — see CLAUDE.md)

| Need | Reuse | License | Notes |
|---|---|---|---|
| Macro-oscillator (VA/wavetable/FM) | **Mutable Instruments `plaits` / `braids`** | MIT | Core engine. Vendor `dsp/vendor/mi/`. Ported to Daisy already → known-portable float DSP. |
| DSP utilities, filters, units, ringbuf | **MI `stmlib`** | MIT | SVF, one-pole, parameter smoothing, dsp helpers. |
| Audio out / codec / I2S | **`badge-bsp`** (ES8156) | (BSP) | Confirm API from build-env agent report. |
| USB MIDI device | **`esp_tinyusb`** 1.7.6 / TinyUSB MIDI | Apache-2.0 | USB-C device mode (Stage 5d). FS port (`CONFIG_TINYUSB_RHPORT_FS`). |
| USB MIDI host | **ESP-IDF `usb` Host Library** + vendored MIDIStreaming class driver | Apache-2.0 (lib) + **Unlicense/CC0** (driver) | USB-A host (Stage 5b). Driver adapted from esp-idf PR #12566 (`Wunderbaeumchen99817/esp-idf`, CC0); no first-party MIDI host component exists. P4-proven on hardware 2026-06-29. |
| UI rendering | **PAX graphics** | (lib) | All drawing. |
| FM engine (if we want full DX-grade FM) | **Dexed/MSFA** (msfa) | Apache-2.0 | Optional, heavier. Only if 2-op isn't enough. |

> If a sonic feature needs GPL code (Surge, ZynAddSubFX, Vital), **stop and ask** — it's
> a licensing decision for `specs/decisions/`. We default to the MIT path above.

## Layering (mirrors CLAUDE.md)
```
ui/ ─► control/ ─► engine/ ─► dsp/ (pure, vendored MI here)
                                   ┊
              everything above ────┴──── platform/  (HAL: device=BSP/I2S/USB/SD, host=SDL2/miniaudio/RtMidi)
```
- `dsp/` is pure and host-testable. No IDF includes. This is where MI code is wrapped.
- `engine/` hosts swappable **SynthModels** (ADR 0008): the model-agnostic **voice
  allocator** (poly/mono/unison/legato), **modulation matrix** (ADR 0009), **master FX
  bus** (chorus→delay→reverb), parameter store, and the sample-accurate **event scheduler**
  (ADR 0010). Owns the fixed voice pool; voices hold state only (shared tables in PSRAM).
- A **SynthModel** = a parameter table + an `IVoice` factory. Juno-106 is the first; the
  per-voice signal flow above *is* the Juno model's voice. New engines add a table + voice,
  nothing else.
- `control/` = note sources (MIDI in, musical-typing, arp, sequencer, MIDI-file) all
  normalized to one event stream into the scheduler; preset/pattern load-save.
- `platform/` is the **only** layer touching OS/board APIs — the HAL membrane (ADR 0007,
  spec `04`). Device and host impls behind one contract.

## The parameter table (central dedup mechanism)
A single declarative table defines every tweakable once:
```
{ id, group, name, short_name, min, max, default, curve, unit,
  midi_cc, smoothing_ms, target (where in the engine it writes) }
```
- **UI** renders pages/controls from it. **MIDI** maps CC→param through it. **Presets**
  serialize the set of values. **Smoothing** (zipper-noise removal) is declared, not
  re-implemented per param.
- Adding a parameter = one row. This is non-negotiable; protect it from special-casing.

## Memory & real-time plan
- **Block size:** start at **64 samples @ 48 kHz** (~1.33 ms latency/block). Tune later.
- **Voice pool:** fixed, preallocated. Voice = state only; **wavetables are shared
  read-only** (one copy in PSRAM, every voice indexes it).
- **Cores:** audio render pinned to one core; UI/MIDI/SD on the other.
- **No malloc/log/block in the audio path** (see CLAUDE.md Real-Time Audio Rules).
- **Denormals:** no hardware FTZ on the P4 — suppress in software in every feedback block
  (ADR 0012).
- Track `make size` after every stage; keep a running RAM/flash budget below.

### Memory placement (ADR 0013 — survives a flash-cache-disable)
A flash write/erase disables the flash cache on the P4; anything in flash that the audio
path touches mid-write stalls or crashes it. So placement is load-bearing, not advisory:

| What | Where | Why |
|---|---|---|
| Audio render code (render chain) | **internal IRAM** (`IRAM_ATTR`) | executes through a flash cache-disable (preset save, SD I/O) |
| Constant tables the render path reads | **DRAM or PSRAM**, never flash `.rodata` | a cache-disable must not strand them |
| Per-voice state + working buffers | **internal DRAM** | hot, touched every block; fast, no cache risk |
| Shared wavetables / samples | **PSRAM, read-only** | bulk; PSRAM stays reachable during a *flash* write |
| UI framebuffers | **PSRAM** | large, UI-core cadence |
| I2S / future DMA buffers | **internal, DMA-capable, word-aligned** | `MALLOC_CAP_DMA\|MALLOC_CAP_INTERNAL`; `esp_cache_msync` for coherence |

### Running memory budget (update each stage, alongside `make size`)
The **cycles/block** column is seeded by Stage 0.5 profiling (`stages/stage-0.5-profiling.md`)
and tracked per stage thereafter against the ratified per-voice budget.

| Stage | Flash (app) | Internal IRAM | Internal DRAM | PSRAM | Cycles/block | Notes |
|---|---|---|---|---|---|---|
| 0 | 936 KB (55% free) | — | audio scratch (`s_left/right/interleaved`) | framebuffer | — | sine engine; no IRAM placement yet |
| 0.5 | (bench build only) | — | — | — | **480 000 cyc/blk @ 360 MHz** | measured on device (-O2); proxy voice ~2 610 cyc/blk; 8 voices = 4.4% period (`stages/stage-0.5-results.md`) |
| ADR 0021 limiter | — | — | `s_limiter` (5 floats, ~20 B DRAM) | — | **~10 cyc/blk** (est.) | master-bus peak limiter; ~5 flops/frame × 64 frames; no libm in render path; inlines into IRAM via header-only `dsp/limiter.h` |
| 6a WS3 dirty-rect present | ≈1097 KB / 0x112700 (46% partition free) | — | +1 `uint32_t` dirty-band word (`ui_dirty.cpp`) | framebuffer (unchanged) | present-side only | ADR 0022: `platform_present(y0,y1)` blits only the dirty scanline band instead of the full 800×480 framebuffer; near-neutral flash/RAM (one word + a few functions) — the win is PSRAM-bandwidth on the *device*, not measured on host; on-device A/B (`PROFILE=1`, chord held during redraw) is Pascal's verification step |

## Polyphony — 8 voices + unison (ADR 0003, rationale amended)
**8 voices** with optional **unison** (stack/detune for fatness) rather than 16 thin voices.
Stage 0.5 showed CPU does *not* bind at 8 (4.4% of the period; ~95% idle) — so the reason to
hold at 8 is **sonic** (fat Juno voices + predictable stealing), not cycles. Voice count is a
tunable constant (one `kNumVoices`, never a literal `8`; pool stays state-only); re-profile
the real voice before raising it. What the idle budget is spent on — reverb, 2× oversampling,
waveform animation — over raw count: **ADR 0015**.

## Validation strategy
- `dsp/` blocks get **host-side unit tests** (pure C/C++, compiles on the Mac): spectra,
  aliasing floor, filter response, envelope shape. Acceptance criteria per spec.
- On-device: A/B against reference Juno-106 samples for character.

## Dependency ledger (fill in as we vendor)
| Component | Version/commit | License | Why |
|---|---|---|---|
| esp-idf | v5.5.1 | Apache-2.0 | platform |
| badge-bsp | ^0.9.9 | (verify) | board support |
| pax-gfx | ^2.1.0 | MIT | UI (also built host-side via its non-ESP CMake path) |
| tanmatsu-wifi | ^1.1.2 | (verify) | radio (optional; may drop) |
| DaisySP (osc/svf/ladder/adsr/noise/chorus) | `599511b` (master, 2025-05-29) | MIT | **primary MVP DSP** — pure float blocks (ADR 0014); vendor at Stage 1a |
| MI eurorack (plaits/stmlib) | (pin on vendor, Stage 7) | MIT | hybrid macro-osc: wavetable/FM modes (later) |
| miniaudio | 0.11.22 | MIT-0 / public-domain | **host-only** audio sink; vendored `platform/host/miniaudio.h` |
| SDL2 | system (brew `sdl2`) | Zlib | **host-only** window/present/input; never shipped to device |
| RtMidi | system (brew `rtmidi`) | MIT-style (permissive) | **host-only** MIDI input via CoreMIDI/ALSA; never shipped to device (gate G6, Stage 5a) |

---

## Resolved (was "the grill") — see `specs/decisions/`
All five bootstrap questions are ratified: sonic base = Juno-106 hybrid (ADR 0002);
polyphony = 8 + unison (0003); MIDI = USB-A host first (0005); licensing = permissive-only
(0004); expansion/CV = out of scope for v1, screen+keyboard only (0006).

## Open / deferred (not blocking)
- Block size (start 64 @ 48 kHz) and final voice count — settle by profiling on hardware.
- USB-host MIDI class driver availability on the P4 — investigate at the MIDI stage (0005).
- Reverb/delay on the master bus after the chorus — nice-to-have, budget permitting (now a
  priority spend for the headroom, ADR 0015 — profile its real block cost on landing).
- Musical-typing key layout and the live-tweak key map — design when PAX UI work starts.
- **Display/UI render-path benchmark** (waveform-animation feasibility, ADR 0015) — a
  Stage-0.5-style bench for PAX draw + present cost; run when UI work begins (~Stage 2),
  before the scope feature is promised. The cost is video/PSRAM-bandwidth, not audio cycles.
