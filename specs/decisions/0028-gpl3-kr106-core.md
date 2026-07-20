# ADR 0028 — GPL-3.0 KR-106 core pivot

**Status:** accepted (2026-07-20)

## Context

The permissive-only implementation has produced a working instrument shell and a broad
Juno-style feature set, but recreating the character DSP block by block is taking too
long and still leaves the central question — whether the instrument sounds good — open.
Ultramaster KR-106 already implements the Juno signal path and is available under
GPL-3.0-only. The user has explicitly chosen speed and sonic quality over retaining a
permissive-only codebase.

## Decision

- Adopt **GPL-3.0-only** for the combined Tanmatsu Synth distribution and import the
  minimum useful DSP from the user-supplied `ultramaster_kr106` source tree.
- This ADR **supersedes ADR 0004** and the GPL exclusions in ADR 0026. Existing
  third-party components keep their own licenses and notices.
- Preserve the existing platform HAL, app/control/UI, parameter store, preset codec,
  voice allocator, MIDI/event plumbing, recording, and host/device test harness.
- Replace the Juno voice's hand-assembled oscillator/filter/envelope/HPF internals and
  the current chorus with KR-106-derived implementations behind the existing
  `SynthModel` / `IVoice` and master-effect seams.
- Port in vertical slices: first one playable voice through the existing engine, then
  parameter/preset mapping and polyphony, then KR-106 chorus and component variance.
- Pin the imported source revision (**Ultramaster KR-106 v2.5.13,
  `bc15caee5843ab238a25d0969e68d57db2b1615f`**), retain upstream copyright and GPL notices, identify
  modified files, and record every copied/adapted source file in the dependency ledger.

## What is retained

The prior work on the instrument shell is not discarded: the platform membrane,
real-time command queues, allocator, UI generated from the parameter table, MIDI,
presets/banks, SD recording, and host/device verification remain the fastest route to a
complete badge instrument. The stable `IVoice` boundary exists specifically to permit
this replacement.

## What becomes superseded

Once the KR-106 replacement passes host tests and device build/profile gates, remove the
now-unused hand-built Juno DSP wrappers, their implementation-specific tests, and any
DaisySP modules no longer referenced. Do not delete them before the replacement is green;
keeping a working A/B baseline makes the port faster and recoverable.

## Real-time constraints

GPL reuse changes licensing, not the device's hard constraints. The adapted render path
must remain allocation-free, non-blocking, finite/denormal-safe, block-based, and backed
by internal SRAM for hot per-voice state. Desktop/plugin framework code is not imported
into device layers.

## Consequences

- The repository can no longer be represented as MIT-only; public redistribution of the
  combined work must satisfy GPL-3.0 source and notice obligations.
- Sonic behavior may change deliberately toward KR-106. Existing preset IDs and UI
  controls should be mapped where semantics agree; incompatible extensions remain
  neutral or are retired through a separate data-format decision if necessary.
- The user-selected source tree is available at `../ultramaster_kr106`; import only from
  the pinned commit above and ignore unrelated uncommitted files in that source checkout.
