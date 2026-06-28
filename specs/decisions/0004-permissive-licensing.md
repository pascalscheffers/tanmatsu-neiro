# ADR 0004 — Permissive-only vendoring

**Status:** accepted (2026-06-27)

## Context
DSP can be reused from permissive sources (Mutable Instruments — MIT) or copyleft ones
(Surge, ZynAddSubFX, Vital — GPL). The latter is richer but relicenses anything it
touches.

## Decision
Vendor **only permissively-licensed code: MIT / BSD / Apache-2.0 / CC0.** No GPL/LGPL/
AGPL without a new ADR explicitly superseding this one for a specific component. Our own
code is **MIT**.

## Why
- Keeps the whole project cleanly MIT-able, matching the badge.team / template ethos
  (template is CC0).
- The Mutable Instruments path (plaits/braids/stmlib, MIT) already covers the core
  oscillator + filter + DSP-utility needs, so permissive is not a real constraint here.

## Consequences
- Primary DSP source: **Mutable Instruments STM32 code**, vendored under `dsp/vendor/mi/`,
  pinned to a specific upstream commit, local edits minimized and marked.
- If a future feature truly needs GPL DSP, stop and write an ADR — it's a project-wide
  license change, not a casual import.
- Record every vendored component in the dependency ledger (`specs/02`).
