# ADR 0020 — Sub-oscillator stays sawtooth for Stage 3c-iii

**Status:** accepted — 2026-06-29  
**Context:** Stage 3c-iii wires OSC_WAVEFORM (SAW/PULSE/TRI) and PWM to the MAIN oscillator
(`osc_main_`). The question arose: should `osc_sub_` also switch waveform, or stay saw?

## Decision

`osc_sub_` stays WAVE_POLYBLEP_SAW for this sub-stage and the foreseeable future.

## Rationale

The real Juno-106 sub-oscillator is a fixed square wave one octave below the DCO — it adds low-end
body. Our sub is currently a PolyBLEP saw, which is already not bit-accurate. Switching it to
square now would require a second `set_waveform` call and a second class of behavior to document and
test, for an incremental sonic gain that is masked by the primary waveform change on the main
oscillator.

The *character* of Stage 3c-iii is the main oscillator gaining variable pulse width and triangle —
that carries the chiptune and bright-pulse timbres intended by this sub-stage. The sub's role is
bass reinforcement regardless of waveform; SAW already does that acceptably.

## Future follow-up (not a gate)

A dedicated "sub-osc square" sub-stage would change `osc_sub_` to `WAVE_POLYBLEP_SQUARE` and
potentially expose a `SUB_WAVEFORM` param row. This is a one-line code change plus a test. It is
closer to a real Juno-106 sub, but that is a sonic/accuracy improvement, not a correctness fix.
Record it when planning Stage 7 (second engine / accuracy pass).

## Scope

This decision applies only to the `JunoVoice` sub-oscillator. The main oscillator (`osc_main_`)
fully supports SAW, PULSE (variable duty via `set_pw`), and TRI as specified in Stage 3c-iii.

## Consequences

- Stage 3c-iii code worker only modifies `osc_main_` waveform and `set_pw` behaviour.
- No `SUB_WAVEFORM` param row is needed.
- The SUB_LEVEL mix knob continues to work for all three main waveforms (the sub adds body
  regardless of its own wave shape).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
