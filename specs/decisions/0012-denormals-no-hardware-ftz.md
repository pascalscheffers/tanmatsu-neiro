# ADR 0012 — Denormals are killed in software; the P4 has no hardware flush-to-zero

**Status:** accepted (2026-06-28)

## Context
`CLAUDE.md`'s Real-Time rule #7 originally read "Flush-to-zero (FTZ) where available."
On the ESP32-P4 it is **not** available. Each HP core implements the RISC-V `RV32F`
single-precision extension, and the RISC-V F/D floating-point extensions define **no
FTZ/DAZ control bit** — there is no `fcsr` flag to flush subnormals. Subnormals are handled
in hardware per IEEE-754, which is implementation-defined and, on real-time DSP, typically
a multi-cycle penalty per operation exactly in the loops that run every sample.

This bites *this* synth in two ways:
- **Feedback paths decay into the subnormal range.** IIR filters (the VA filter, ADR 0002)
  and especially the chorus / delay / reverb (Stage 4) settle toward tiny floats during
  silence/release. Unchecked, that is a per-sample cost spike right where the budget is
  tightest — a self-inflicted deadline miss.
- **It is a host↔device divergence trap.** x86 (the Mac dev host) and ARM Cortex-M7 (the
  **Daisy** platform the vendored Mutable code was ported to) *have* FTZ and usually run
  with it on, so a denormal bug is silent and free there. On the P4 it is a glitch or a
  blown block. A clean host test could hide a device-only failure — undercutting the
  host-first model (ADR 0007) unless we account for it.

## Decision
**Denormal suppression is mandatory and explicit in software on every filter / feedback /
recursive DSP block. We do not rely on an FPU FTZ mode, because the P4 has none.**

- Each recursive block guards its state with an explicit anti-denormal idiom — add a tiny
  DC offset / `+ 1.0e-20f` to the feedback term, or clamp small magnitudes to zero — applied
  where state is updated, not as an afterthought. Treat it as a required idiom, not an
  optional optimization.
- **The host offline-render test harness runs with FTZ *disabled*** (e.g. x86
  `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF)` + `_MM_SET_DENORMALS_ZERO_MODE`) so host
  tests reproduce P4 denormal behavior. This is the FTZ-aware corollary of ADR 0011
  (test the device's reality, not the host's convenience).
- When vendoring MI `stmlib` / `plaits`, **audit each block for a denormal guard.** Note
  which already inject one and which silently rely on the host/Daisy FPU; add guards to the
  latter as a marked local edit.

## Consequences
- A handful of extra adds/clamps in hot loops — negligible cost, and far cheaper than a
  subnormal stall. The output sanitize already added at the int16 conversion
  (`to_i16`, NaN/Inf→0) is the last line of this same defense.
- Host DSP tests get slightly slower (no FTZ) but become *protective* of the device.
- Updates `CLAUDE.md` RT rule #7 from "where available" to "mandatory in software."
- Pairs with ADR 0011: another case where the device's constraints, not the host's
  comforts, define the shared code's behavior.
