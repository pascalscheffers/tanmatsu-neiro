# Stage 0.5 — CPU Benchmark Results

**Status:** ✅ captured on hardware (2026-06-28) · device run via AppFS (`make bench-device`)
+ `make sniff`. Two builds captured: **-O2** (primary; shipping-representative, commit
`8b0e09a`) and **-Og** (IDF debug default, first run). Raw capture:
`build/tanmatsu-bench/console.log` (P4 console = `[1301]`).

---

## Device configuration

| Field | Value |
|---|---|
| Chip | ESP32-P4 (rev v1.0) |
| CPU frequency | **360 MHz** (cpu_start: `cpu freq: 360000000 Hz`) |
| IDF version | v5.5.1 |
| Optimization | **-O2** (primary) · -Og also captured for comparison |
| FTZ (flush-to-zero) | OFF — RV32F has no FTZ bit (ADR 0012) |
| Block size | 64 samples |
| Sample rate | 48 000 Hz |
| Block period | **480 000 cycles / 1333.33 µs** |

---

## Kernel cost table (device, 360 MHz, **-O2**)

> Baseline subtract: **198 cyc/blk (3 cyc/smp)** at -O2 (was 470 at -Og).

| Kernel | cyc/blk (-O2) | cyc/smp (-O2) | µs/blk | % period | -Og cyc/blk | -O2 speedup |
|---|---|---|---|---|---|---|
| baseline (empty loop) | 198 | 3 | 0.55 | 0.0% | 470 | 2.4× |
| sinf (per sample) | 1 675 | 26 | 4.65 | 0.3% | 1 829 | 1.1× (flat) |
| expf (per sample) | 9 252 | 144 | 25.70 | 1.9% | 8 827 | ~1.0× (flat) |
| SVF 2-pole | 938 | 14 | 2.61 | 0.2% | 1 833 | 2.0× |
| biquad TDF-II | 867 | 13 | 2.41 | 0.2% | 2 407 | 2.8× |
| Moog ladder 4-pole | 1 570 | 24 | 4.36 | 0.3% | 3 363 | 2.1× |
| PolyBLEP saw | 1 026 | 16 | 2.85 | 0.2% | 2 319 | 2.3× |
| memcpy 64×4 B | 204 | 3 | 0.57 | 0.0% | 221 | 1.1× (flat) |

Reads: our hand-written DSP kernels gain **2–2.8×** at -O2 (filters, oscillator). The libm
transcendentals **`sinf`/`expf` don't move** — they're prebuilt in the toolchain, so no app
optimization touches them. **`expf` is the dominant primitive at either level (~144 cyc/smp)**
and stays that way → keep exponential env/LFO shaping at block rate or via a LUT, regardless
of `-O`. Host reference numbers (pseudo-1 GHz, orientation only) live in `make bench` output.

---

## Load ramp (device, **-O2**) — fake voice = PolyBLEP saw + SVF LP + env, via the audio callback

Block period = **480 000 cycles** (100%) · 70% ceiling = **336 000 cycles**

| voices | cyc/blk (-O2) | % period | margin cyc | verdict | -Og cyc/blk | -Og % |
|---|---|---|---|---|---|---|
| 1 | 2 998 | 0.6% | 477 002 | OK | 4 132 | 0.9% |
| 2 | 5 613 | 1.2% | 474 387 | OK | 7 781 | 1.6% |
| 4 | 10 833 | 2.3% | 469 167 | OK | 15 090 | 3.1% |
| 8 | 21 266 | 4.4% | 458 734 | OK | 29 682 | 6.2% |
| 12 | 31 700 | 6.6% | 448 300 | OK | 44 302 | 9.2% |
| 16 | 42 129 | 8.8% | 437 871 | OK | 58 908 | 12.3% |
| 24 | 62 984 | 13.1% | 417 016 | OK | 88 074 | 18.3% |
| 32 | 83 850 | 17.5% | 396 150 | OK | 117 309 | 24.4% |

**Marginal per-voice cost ≈ 2 610 cyc/blk (~41 cyc/smp)** at -O2 — dead linear, ~1.4× cheaper
than -Og's ~3 650 (+~390 cyc/blk fixed buffer-clear overhead). The ramp **never reached the
70% ceiling**: at the harness max of 32 voices it sits at **17.5%** (-Og: 24.4%).

Safe voice ceiling (≤ 70%): **> 32 proxy voices** (ramp maxed at 32 / 17.5%); linear
extrapolation → ~**128 proxy voices** at 70% (-Og: ~90).

Underruns observed: **0** at both -O2 and -Og (every step `OK`; audio ran clean).
Audio task stack high-water: _not captured (add to bench if we ever run tight)._

---

## Summary

On real P4 silicon at **360 MHz**, one 64-frame block is **480 000 cycles**. At **-O2** (what
the synth will ship at) the proxy voice (band-limited saw + 2-pole filter + envelope) costs
**~2 610 cyc/blk (~41 cyc/smp)**, so **8 voices = 4.4% of the period** and even **32 voices =
17.5%**, no underruns. `-O2` gives our DSP kernels a clean 2–2.8× over the `-Og` debug build;
the only primitive that doesn't benefit is `expf` (prebuilt libm, ~144 cyc/smp at both).

The proxy is deliberately lighter than a real Stage 1 DaisySP voice (it omits per-sample
param smoothing, ladder `tanh`, multi-osc unison, and FX). But the margin is huge: even
assuming a **real voice costs 5–8× the proxy** (~13–21 k cyc/blk at -O2), 8 voices land at
**~22–35% of the period** — comfortably under the 70% ceiling, with room for chorus + a
future reverb + UI/MIDI jitter. **ADR 0003 (8 voices + unison) stands with wide headroom.**

Per-voice budget for Stage 1 sizing: aim for **≤ ~30 000 cyc/blk per voice (~470 cyc/smp)** to
keep 8 voices ≤ 50% period — ~11× the -O2 proxy, a generous envelope for real DaisySP blocks.

---

## 🛑→✅ OPUS GATE — CPU budget & polyphony — **RATIFIED 2026-06-28 (Opus 4.8)**

- **Decision:** **ADR 0003 stands** — 8 voices + unison, block 64 @ 48 kHz. Measured 8-voice
  proxy load is 4.4% of the period (-O2); conservative 5–8× real-voice scaling keeps it
  ≤ ~35%, well inside the 70% ceiling with room for chorus + reverb.
- **Per-voice budget for Stage 1:** target ≤ ~30 000 cyc/blk (~470 cyc/smp). Keep `expf`
  out of the per-sample path (~144 cyc/smp at any `-O`) — block-rate or LUT.
- **Confirmed:** the `-O2` follow-up is done — it widened headroom (8 voices 6.2% → 4.4%) and
  showed the libm transcendentals don't optimize, validating the `expf` guidance.
- **Open follow-up (non-blocking):** raising polyphony beyond 8 is *supported by the data* but
  is an architecture change — bring it as an explicit decision if wanted; don't auto-change
  ADR 0003. (Separately: the shipping image is still `-Og`; moving the whole project to a PERF
  build is its own easy change when we want the speed.)
- **Next:** proceed to **Stage 1** (one-voice MVP) against the per-voice budget above.
