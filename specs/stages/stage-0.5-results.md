# Stage 0.5 — CPU Benchmark Results

**Status:** pending hardware run · **Fill in:** after `make bench-device` (AppFS launch, no firmware replace) + `make monitor BENCH=1` serial capture

---

## Device configuration

| Field | Value |
|---|---|
| Chip | ESP32-P4 |
| CPU frequency | ___ MHz (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) |
| IDF version | v5.5.1 |
| Optimization | -O2 (IDF default) |
| FTZ (flush-to-zero) | OFF — RV32F has no FTZ bit (ADR 0012) |
| Block size | 64 samples |
| Sample rate | 48 000 Hz |
| Block period | ___ cycles / ___ µs |

---

## Kernel cost table (device)

> Baseline subtract: ___ cyc/blk (___ cyc/smp)

| Kernel | cyc/blk | cyc/smp | µs/blk | % period |
|---|---|---|---|---|
| baseline (empty loop) | | | | |
| sinf (per sample) | | | | |
| expf (per sample) | | | | |
| SVF 2-pole | | | | |
| biquad TDF-II | | | | |
| Moog ladder 4-pole | | | | |
| PolyBLEP saw | | | | |
| memcpy 64×4 B | | | | |

_Host reference numbers (pseudo-1 GHz, not the budget):_

| Kernel | cyc/blk (host) | µs/blk (host) |
|---|---|---|
| baseline | | |
| sinf | | |
| expf | | |
| SVF 2-pole | | |
| biquad TDF-II | | |
| Moog ladder 4-pole | | |
| PolyBLEP saw | | |
| memcpy | | |

---

## Load ramp (device)

Block period = ___ cycles (100%)
70% ceiling  = ___ cycles

| voices | cyc/blk | % period | margin cyc | verdict |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 4 | | | | |
| 8 | | | | |
| 12 | | | | |
| 16 | | | | |
| … | | | | |

Safe voice ceiling (≤ 70%): **___ voices**

Underruns observed: ___ (should be 0 below the ceiling)
Audio task stack high-water: ___ bytes

---

## Summary

_One paragraph: the proposed per-voice budget in cycles, whether 8 voices + unison
fits at ≤ 70% period with headroom for chorus + reverb, and whether ADR 0003 stands
or needs amending. Written after hardware numbers are captured._

---

## 🛑 OPUS GATE — CPU budget & polyphony

Raised after filling the table above.

- **Why Opus:** CPU-budget / architecture (touches ADR 0003, sizes all of Stage 1)
- **Decision:** What is the per-voice cycle budget? Does 8 voices + unison fit with
  headroom for chorus + a future reverb? Keep, raise, or lower the voice count?
- **Recommendation:** keep ADR 0003 (8 + unison) if the fake-voice ramp clears 8 voices
  at ≤ 70% period with room to spare; otherwise amend ADR 0003 to the measured ceiling.
- **Sonnet action:** STOP — record results, raise this in MEMORY.md "Open Opus gates",
  ask the user to switch to Opus before Stage 1.
