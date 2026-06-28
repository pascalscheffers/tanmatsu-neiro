# Decision Records

Ratified design decisions, ADR-style, one per file, numbered in order. A decision here
overrides any "proposal" wording elsewhere in `specs/`. Keep each lean: context, the
decision, and the consequence. Supersede rather than rewrite history.

| # | Decision |
|---|---|
| [0001](0001-virtual-analog-target.md) | Fully digital virtual-analog/hybrid (no analog path on the hardware) |
| [0002](0002-juno106-hybrid-voice.md) | Juno-106 voice skeleton + hybrid macro-oscillator |
| [0003](0003-polyphony-8-unison.md) | 8 voices with unison (over 16 thin) |
| [0004](0004-permissive-licensing.md) | Permissive-only vendoring (MIT/BSD/Apache/CC0) |
| [0005](0005-midi-usb-host-first.md) | USB-A host MIDI built first |
| [0006](0006-v1-no-physical-controls.md) | v1 UI is screen + QWERTY only; no expansion controls yet |
| [0007](0007-host-first-platform-hal.md) | Host-first dev on a thin platform HAL (5 seams) |
| [0008](0008-engine-model-boundary.md) | Synth hosts swappable SynthModels; MPE-ready voices |
| [0009](0009-modulation-matrix.md) | Modulation is a matrix; Juno routings are a default patch |
| [0010](0010-timing-sample-clock.md) | Sample-accurate timing on a pluggable clock |
