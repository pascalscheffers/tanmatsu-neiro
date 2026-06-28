# Stage 0.5 — CPU Benchmark Results

**Status:** ✅ captured on hardware (2026-06-28) · device run via AppFS (`make bench-device`)
+ `make sniff`. Raw capture: `build/tanmatsu-bench/console.log` (P4 console = `[1301]`).

---

## Device configuration

| Field | Value |
|---|---|
| Chip | ESP32-P4 (rev v1.0) |
| CPU frequency | **360 MHz** (cpu_start: `cpu freq: 360000000 Hz`) |
| IDF version | v5.5.1 |
| Optimization | `-Og` (IDF default for this build; see note) |
| FTZ (flush-to-zero) | OFF — RV32F has no FTZ bit (ADR 0012) |
| Block size | 64 samples |
| Sample rate | 48 000 Hz |
| Block period | **480 000 cycles / 1333.33 µs** |

> **Note — optimization level.** This image built at `-Og` (the compile line carried `-Og`),
> not `-O2`. Real DSP at `-O2`/`-O3` will be **faster** than these numbers, so every figure
> below is a conservative (pessimistic) bound on the budget. Worth a confirming `-O2` re-run
> before Stage 1 tuning, but it only widens the headroom.

---

## Kernel cost table (device, 360 MHz)

> Baseline subtract: **470 cyc/blk (7 cyc/smp)** — empty loop over the 64-frame buffer.

| Kernel | cyc/blk | cyc/smp | µs/blk | % period |
|---|---|---|---|---|
| baseline (empty loop) | 470 | 7 | 1.31 | 0.1% |
| sinf (per sample) | 1 829 | 28 | 5.08 | 0.4% |
| expf (per sample) | 8 827 | 137 | 24.52 | 1.8% |
| SVF 2-pole | 1 833 | 28 | 5.09 | 0.4% |
| biquad TDF-II | 2 407 | 37 | 6.69 | 0.5% |
| Moog ladder 4-pole | 3 363 | 52 | 9.34 | 0.7% |
| PolyBLEP saw | 2 319 | 36 | 6.44 | 0.5% |
| memcpy 64×4 B | 221 | 3 | 0.61 | 0.0% |

Reads: filters are cheap (SVF ≈ 28, ladder ≈ 52 cyc/smp). **`expf` is the standout cost**
(137 cyc/smp) — relevant because exponential envelopes/LFOs lean on it; prefer per-block or
LUT-based env shaping over per-sample `expf`. Host reference numbers (pseudo-1 GHz,
orientation only) are in `make bench` output — not the budget, so not reproduced here.

---

## Load ramp (device) — fake voice = PolyBLEP saw + SVF LP + env, via the audio callback

Block period = **480 000 cycles** (100%) · 70% ceiling = **336 000 cycles**

| voices | cyc/blk | % period | margin cyc | verdict |
|---|---|---|---|---|
| 1 | 4 132 | 0.9% | 475 868 | OK |
| 2 | 7 781 | 1.6% | 472 219 | OK |
| 4 | 15 090 | 3.1% | 464 910 | OK |
| 8 | 29 682 | 6.2% | 450 318 | OK |
| 12 | 44 302 | 9.2% | 435 698 | OK |
| 16 | 58 908 | 12.3% | 421 092 | OK |
| 24 | 88 074 | 18.3% | 391 926 | OK |
| 32 | 117 309 | 24.4% | 362 691 | OK |

**Marginal per-voice cost ≈ 3 650 cyc/blk (~57 cyc/smp)** — dead linear across the sweep
(+~480 cyc/blk fixed buffer-clear overhead). The ramp **never reached the 70% ceiling**: at
the harness max of 32 voices it sits at **24.4%**.

Safe voice ceiling (≤ 70%): **> 32 proxy voices** (ramp maxed at 32 / 24.4%); linear
extrapolation → ~**90 proxy voices** at 70%.

Underruns observed: **0** (every step `OK`; audio ran clean through the sweep).
Audio task stack high-water: _not captured (add to bench if we ever run tight)._

---

## Summary

On real P4 silicon at **360 MHz**, one 64-frame block is **480 000 cycles**. The proxy
voice (band-limited saw + 2-pole filter + envelope) costs **~3 650 cyc/blk (~57 cyc/smp)**,
so **8 voices = 6.2% of the period** and even **32 voices = 24.4%**, with no underruns. The
heaviest primitive is `expf` (137 cyc/smp); filters are cheap (28–52 cyc/smp).

The proxy is deliberately lighter than a real Stage 1 DaisySP voice (it omits per-sample
param smoothing, ladder `tanh`, multi-osc unison, and FX). But the margin is enormous: even
assuming a **real voice costs 5–8× the proxy** (~18–29 k cyc/blk), 8 voices land at
**~30–48% of the period** — still under the 70% ceiling, leaving room for chorus + a future
reverb + UI/MIDI jitter. **ADR 0003 (8 voices + unison) stands with comfortable headroom**;
nothing forces a reduction, and the data would even support raising polyphony later if
desired (an architecture change — defer to a deliberate decision, don't auto-bump).

Per-voice budget for Stage 1 sizing: aim for **≤ ~30 000 cyc/blk per voice (~470 cyc/smp)**
to keep 8 voices ≤ 50% period; that's ~8× the proxy, a generous envelope for DaisySP blocks.

---

## 🛑→✅ OPUS GATE — CPU budget & polyphony — **RATIFIED 2026-06-28 (Opus 4.8)**

- **Decision:** **ADR 0003 stands** — 8 voices + unison, block 64 @ 48 kHz. Measured 8-voice
  proxy load is 6.2% of the period; conservative 5–8× real-voice scaling keeps it ≤ ~48%,
  inside the 70% ceiling with room for chorus + reverb.
- **Per-voice budget for Stage 1:** target ≤ ~30 000 cyc/blk (~470 cyc/smp). Avoid
  per-sample `expf` (137 cyc/smp) in envelopes/LFOs — block-rate or LUT.
- **Open follow-ups (non-blocking):** (1) confirm with an `-O2` build — only widens headroom;
  (2) raising polyphony beyond 8 is *supported by the data* but is an architecture change —
  bring it as an explicit decision if wanted, don't auto-change ADR 0003.
- **Next:** proceed to **Stage 1** (one-voice MVP) against the per-voice budget above.
