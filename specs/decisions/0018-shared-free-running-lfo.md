# ADR 0018 — Shared free-running LFO; per-note delay fade-in stays per-voice

**Status:** accepted (2026-06-29)

## Context

Device-confirmed bug (INIT patch, UNISON=8): after cycling the 8-voice pool, a single
repeated note had ~10 s of phasing artifacts and did not sound identical each time.

Root cause: `JunoVoice::note_on()` reset the LFO *delay* counters but not LFO *phase*.
`lfo*.reset()` ran only on voice steal; idle voices early-returned from `render()`, so
per-voice LFO phase **froze at its last value**. A reused voice resumed LFO→PWM/cutoff
modulation from a history-dependent frozen phase. At UNISON=8 all 8 voices carried
different frozen phases → inter-voice beating and non-determinism across notes.

Two fixes are possible:
1. Reset LFO phase on `note_on()` (retrigger). Deterministic but changes LFO sync
   behaviour on held chords — LFO restarts every note.
2. Move LFO to the engine as a single shared, free-running oscillator (Juno-106
   authentic: all voices wobble in lock-step). Deterministic, cheaper, and eliminates
   the stale-phase class of bug structurally.

Pascal chose option 2.

## Decision

**One shared `dsp::Lfo` pair (`s_lfo1`, `s_lfo2`) lives in `engine/synth.cpp`.**
The engine advances both LFOs unconditionally once per block and injects the block-end
raw values into every voice via the new `IVoice::set_lfo_inputs(float, float)` method.

**Per-note delay fade-in stays per-voice.** The Juno-106's LFO delay (slow fade-in of
modulation after key-on) is a per-note effect, not a global one. Each `JunoVoice` still
maintains its own `lfo*_delay_pos_` counter, reset on `note_on()`, and applies the
resulting `0→1` scale to the injected raw value before writing `lfo*_value_`.

## Rationale

- **Determinism.** All voices receive the same `l1 = s_lfo1.process_block(n)` every
  block. There is no per-voice phase accumulator to diverge, freeze, or become stale.
- **Authentic Juno-106 behaviour.** The 106's LFO is a global chip-level oscillator;
  all voices receive the same waveshape simultaneously.
- **Cheaper.** One `sinf` (or waveform eval) per block instead of up to 8.
- **Structural fix.** Eliminating per-voice LFO phase removes the entire class of
  stale-phase bugs, not just the specific UNISON=8 repro. No per-note `reset()` call
  is needed, and the idle early-return in `render()` becomes safe again.

## Consequences

- `JunoVoice` no longer owns `dsp::Lfo` instances. Rate and shape parameters
  (`LFO1_RATE`, `LFO1_SHAPE`, `LFO2_RATE`, `LFO2_SHAPE`) are now consumed by
  `synth_render`'s change-gated param loop and applied to `s_lfo1`/`s_lfo2`; the voice
  `set_param()` switch ignores those ids (they fall through to `default: break`).
- `IVoice` gains a new pure-virtual method `set_lfo_inputs(float, float)`. Any future
  second voice model must implement it (the engine calls it unconditionally each block).
- Host tests for LFO behaviour at the voice level are reworked to drive the voice via
  `set_lfo_inputs()` rather than assuming the voice owns the oscillator. Pure `dsp::Lfo`
  unit tests are unchanged.
- LFO phase is no longer reset on note retrigger. If a future feature needs per-note
  LFO sync (e.g. an LFO-sync parameter), it should be implemented as an optional
  `s_lfo1.reset()` call in the note-on path of `synth_render`, not by reintroducing
  per-voice LFO objects.

## Cross-references

- ADR 0009 (mod matrix): `lfo1_value()`/`lfo2_value()` remain the inputs to the matrix;
  only how they are computed changes.
- ADR 0012 (denormals): `dsp::Lfo` already carries the `+1e-20f` anti-denormal offset
  on continuous waveforms — no change needed.
- ADR 0017 (worker methodology): this fix was dispatched as a closed work-order to a
  fresh-context Sonnet worker; Pascal ratified the design decision before dispatch.
