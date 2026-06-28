# ADR 0006 — v1 UI is screen + QWERTY only

**Status:** accepted (2026-06-27) — decided by default; flag if you want otherwise.

## Context
The Tanmatsu exposes expansion headers (PMOD/SAO, Qwiic/I2C, 36-pin personality module)
that could carry physical knobs/encoders/CV. The question is whether v1 depends on any of
that or ships on the stock badge.

## Decision
**v1 targets the stock badge: 800×480 screen + QWERTY keyboard only.** No external knobs,
encoders, or CV. Live tweaking is the screen+keyboard parameter-page UI (`specs/03`).

## Why
- Everyone with a Tanmatsu can run it unmodified — widest reach, simplest bring-up.
- The parameter table + note-event abstraction mean physical controls can be added later
  as just another input source, without redesign.

## Consequences
- Live-tweak ergonomics must be genuinely good from keys alone — a real UX design task,
  not an afterthought.
- Expansion-header controls (encoders over I2C/PMOD) are a post-v1 enhancement, not a
  dependency.
