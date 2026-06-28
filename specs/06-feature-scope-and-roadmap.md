# Feature Scope & Roadmap

What we're building and in what order, derived from the ratified decisions (ADRs 0001–
0010) and the requirements interview. "Simple now, extensible without a rewrite."

## Feature matrix
Legend: **MVP** = first playable · **v1** = the "complete instrument" · **later** =
designed-for, built afterward.

| Area | MVP | v1 | later |
|---|---|---|---|
| Voice | Juno voice (osc+sub+noise→VA filter→amp env), 8-voice poly | full Juno param set; mono+porta, unison, legato/retrigger | more `SynthModel`s (Jupiter, FM, **wavetable**), split/layer |
| Oscillator | VA waveforms (saw/pulse/tri) in MI macro-osc | + 2-op FM mode | + SD wavetable scanning; samples (maybe) |
| Modulation | amp env + 1 LFO, Juno-default routings | 2 envs, 2 LFOs, full mod matrix UI | more sources; deeper matrix |
| FX (master) | chorus | + delay (tempo-synced), reverb | drive/saturation (if wanted), more |
| Control | musical typing | USB-A host MIDI, pitch/mod, velocity+AT, sustain/hold/panic | USB-C MIDI device, **MPE**, MIDI-learn |
| Timing | — | internal clock + tap, sample-accurate scheduler | external MIDI-clock in/out, song-position |
| Arp | — | full arp (modes, octaves, sync, gate, swing, latch) | — |
| Sequencer | — | pattern: step program + real-time record, param-locks | song mode / pattern chaining; piano-roll edit polish |
| MIDI files | — | simple SMF player (type 0/1) → current patch | import to internal sequencer |
| Presets | INIT + load/save | factory + user banks, categories/browser, A/B, randomize/morph | WiFi/web sharing (maybe) |
| UI | one minimal PAX page | hybrid panel overview + edit pages, status strip, LED feedback | per-engine panels; themes |
| Audio out | speaker + headphone (I2S) | hardware volume, amp/HP handling | **USB audio-class out**, **WAV record to SD** |
| Simulator | sine on host+device via HAL | full UI + RtMidi + miniaudio on host | offline-render test suite expansion |

Out of scope unless revisited: multitimbral (>2 parts), scales/microtuning (cheap tuning
hook left in the pitch path), sample/rompler engine, on-device audio input (no hardware).

## Staged roadmap
Each stage: spec → (plan) → implement → verify (host + device, host-side DSP tests) →
commit → memorize. 5–15 files each (CLAUDE.md). Stages 0.5–3 have detailed,
Sonnet-executable **runbooks** in [`stages/`](stages/) (Opus-authored, with 🛑 escalation
gates — ADR 0014); later stages are re-planned with Opus when reached.

- **Stage 0 — Hello audio + the membrane.** `platform/` HAL (audio sink, present, input,
  storage stubs); host backend (SDL2+miniaudio) and device backend (I2S); `synth_render`
  plays a sine on both; PAX renders+presents on both. *Proves ADR 0007.*
- **Stage 0.5 — On-device profiling & CPU budget.** A `make bench` harness times synthetic
  DSP proxies on real P4 silicon and measures the I2S deadline margin at 64@48k → an
  empirical cycles/block budget + max-voice envelope that sizes Stage 1. *Profile before
  optimizing (CLAUDE.md); grounds ADR 0003.* Runbook: `stages/stage-0.5-profiling.md`.
- **Stage 1 — One voice (MVP).** `SynthModel`/`IVoice` boundary (ADR 0008); Juno voice via
  DaisySP macro-osc (VA) + VA filter + ADSR; mode-agnostic 8-voice allocator; master chorus;
  musical-typing; a few params on a minimal page. Host DSP tests (spectra/aliasing/env).
- **Stage 2 — Parameter model + UI framework.** Param table + single write path + ring +
  smoothing (ADR-data); hybrid panel+pages UI; presets save/load + INIT + factory bank.
- **Stage 3 — Modulation + full Juno.** Mod matrix (ADR 0009), 2 LFOs / 2 envs, full Juno
  param set + default routings; play modes (mono/porta/unison/legato).
- **Stage 4 — Timing, arp, sequencer, FX.** Sample-accurate scheduler + internal clock/tap
  (ADR 0010); full arp; pattern model (step+realtime, param-locks); delay + reverb.
- **Stage 5 — MIDI I/O.** USB-A host MIDI (the "real instrument" moment, ADR 0005);
  USB-C MIDI device; MPE-aware expression; SMF player from SD.
- **Stage 6 — Library + capture + polish.** Preset browser/tags, A/B, randomize/morph;
  WAV record; USB audio-class out; factory sound design; A/B vs reference Juno samples.
- **Stage 7+ — Second engine.** Add the wavetable (and/or FM) `SynthModel` — the proof
  that the boundary (ADR 0008) holds and nothing above it had to change.

## Continuous (every stage)
- Track `make size` (flash/RAM budget) and keep a running tally in `specs/MEMORY.md`.
- Keep host + device green. Profile before optimizing (CLAUDE.md).
