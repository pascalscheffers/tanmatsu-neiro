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
| [0011](0011-optimize-device-host-adapts.md) | Optimize base code for the P4; the host simulator pays the conversion tax |
| [0012](0012-denormals-no-hardware-ftz.md) | Denormals killed in software (P4 has no hardware FTZ); host tests run FTZ-off |
| [0013](0013-flash-write-audio-placement.md) | Audio render path placed in IRAM so flash writes (presets/SD) don't glitch it |
| [0014](0014-model-tier-workflow.md) | Opus plans (source-pinned runbooks); Sonnet executes; 🛑 gates hard-stop to escalate |
| [0015](0015-spending-cpu-headroom.md) | Spend the Stage 0.5 CPU headroom on richness (reverb, oversampling) over raw voice count; profile the video path before waveform animation |
| [0016](0016-master-output-soft-clip.md) | Master output stage = linear headroom + a gentle cubic soft-clip ceiling (no baked-in drive; overt grit is a future `MASTER_DRIVE` param) |
| [0017](0017-orchestrator-worker-methodology.md) | Default build methodology: Opus orchestrates, fresh-context Sonnet workers execute closed work-orders; gates return to Opus (amends 0014) |
| [0025](0025-side-buttons-control-codec-volume.md) | Dedicated side buttons control session-level codec volume (0–90%), independent of patch gain and MIDI CC7 |
