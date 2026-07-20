# Feature Scope & Roadmap

What we're building and in what order, derived from the ratified decisions (ADRs 0001–
0010) and the requirements interview. "Simple now, extensible without a rewrite."

## Feature matrix
Legend: **MVP** = first playable · **v1** = the "complete instrument" · **later** =
designed-for, built afterward.

| Area | MVP | v1 | later |
|---|---|---|---|
| Voice | 8-voice KR-106 Juno voice (coherent DCO/sub + shared noise → nonlinear J106 VCF → firmware ADSR/measured VCA) | full Juno param set; mono+porta, unison, legato/retrigger | more `SynthModel`s (Jupiter, FM, **wavetable**), split/layer |
| Oscillator | KR-106 phase-coherent saw/pulse/sub | independent saw/pulse, PWM, range and DCO LFO controls | separate FM/wavetable `SynthModel`; SD wavetable scanning; samples (maybe) |
| Modulation | amp env + 1 LFO, Juno-default routings | 2 envs, 2 LFOs, full mod matrix UI | more sources; deeper matrix |
| FX (master) | global KR J106 HPF + fixed KR BBD chorus modes I/II | + delay (tempo-synced), reverb | drive/saturation (if wanted), more |
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
- **Stage 1 — One voice (MVP, historical implementation).** `SynthModel`/`IVoice` boundary
  (ADR 0008); mode-agnostic 8-voice allocator; musical typing; a few params on a minimal
  page. Its original Daisy voice/chorus implementation was superseded by the Stage 14
  KR-106 port: coherent DCO/sub, nonlinear J106 VCF, firmware ADSR/measured VCA, shared
  noise, global KR HPF, and fixed BBD chorus.
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

Compatibility note: `FILTER_MODE` and `CHORUS_RATE`/`CHORUS_DEPTH`/`CHORUS_DELAY` remain
stored legacy no-ops. The J106 filter is low-pass only and KR chorus modes own fixed
calibration; remapping or removing those IDs requires a separate UI/preset-compatibility
decision.

## Continuous (every stage)
- Track `make size` (flash/RAM budget) and keep a running tally in `specs/MEMORY.md`.
- Keep host + device green. Profile before optimizing (CLAUDE.md).
