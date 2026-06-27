# Synth Architecture (DRAFT — pending grill answers)

Status: **proposal**. Sonic base, polyphony, and a few other choices are open questions
(end of this file). Everything here is the *recommended* design; it gets ratified into
`specs/decisions/` once Pascal confirms.

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
| USB MIDI device | **`esp_tinyusb` / TinyUSB MIDI** | MIT/Apache | USB-C device mode. |
| USB MIDI host | IDF USB host + MIDI class driver | (TBD) | USB-A host; may need a small driver. Verify. |
| UI rendering | **PAX graphics** | (lib) | All drawing. |
| FM engine (if we want full DX-grade FM) | **Dexed/MSFA** (msfa) | Apache-2.0 | Optional, heavier. Only if 2-op isn't enough. |

> If a sonic feature needs GPL code (Surge, ZynAddSubFX, Vital), **stop and ask** — it's
> a licensing decision for `specs/decisions/`. We default to the MIT path above.

## Layering (mirrors CLAUDE.md)
```
ui/ ─► control/ ─► engine/ ─► dsp/ (pure, vendored MI here) ─► platform/ (BSP/I2S/USB/SD)
```
- `dsp/` is pure and host-testable. No IDF includes. This is where MI code is wrapped.
- `engine/` = voice allocator + mod matrix + master FX. Owns the fixed voice pool.
- `platform/` = the only place that talks to ESP-IDF/BSP hardware.

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
- **Placement:** per-voice state + working buffers in **internal SRAM**; wavetable/sample
  banks + UI framebuffers in **PSRAM**.
- **Cores:** audio render pinned to one core; UI/MIDI/SD on the other.
- **No malloc/log/block in the audio path** (see CLAUDE.md Real-Time Audio Rules).
- Track `make size` after every stage; keep a running RAM/flash budget here.

## Polyphony (open — see below)
Per-voice cost dominates the budget. Recommendation: **8 voices** with optional **unison**
(stack/detune for fatness) rather than 16 thin voices — fatter sound, simpler stealing,
comfortable CPU headroom for the chorus and a future reverb. Revisit after the first
voice is profiled on hardware.

## Validation strategy
- `dsp/` blocks get **host-side unit tests** (pure C/C++, compiles on the Mac): spectra,
  aliasing floor, filter response, envelope shape. Acceptance criteria per spec.
- On-device: A/B against reference Juno-106 samples for character.

## Dependency ledger (fill in as we vendor)
| Component | Version/commit | License | Why |
|---|---|---|---|
| esp-idf | v5.5.1 | Apache-2.0 | platform |
| badge-bsp | ^0.9.9 | (verify) | board support |
| pax-gfx | ^2.1.0 | (verify) | UI |
| tanmatsu-wifi | ^1.1.2 | (verify) | radio (optional; may drop) |
| MI eurorack (plaits/stmlib) | (pin on vendor) | MIT | oscillator/DSP |

---

## OPEN QUESTIONS (the grill) — answers ratify into `specs/decisions/`
1. **Sonic base / character.** Juno-106 skeleton (recommended) vs Jupiter-8 (2 VCO,
   fatter, costlier) vs Prophet-5 (2 VCO, SSM filter) vs FM-forward hybrid.
2. **Polyphony target.** 8 + unison (recommended) vs 16 thin vs dynamic.
3. **MIDI role priority.** Device (USB-C, play from DAW) + host (USB-A, plug a keyboard)
   — both eventually; which first?
4. **Vendoring license stance.** Permissive-only (MIT/BSD/Apache — keeps the clean reuse
   path) vs GPL allowed too (unlocks Surge/Zyn/Vital but infects our license).
5. **Expansion/CV in scope?** Physical knobs/encoders/CV via PMOD/Qwiic later, or
   screen+keyboard only for v1?
