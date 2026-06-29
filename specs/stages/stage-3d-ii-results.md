# Stage 3d-ii — Real-Voice CPU Benchmark Results

**Status:** ✅ **RATIFIED on hardware (2026-06-29, Opus 4.8)** — the 🛑 Stage 3d-ii CPU
gate is **closed**. Device run via AppFS (`make bench-device` + `make sniff`); P4 console
= `[1301]`. The full-featured Juno voice (PolyBLEP saw+sub+noise → SVF → 2×ADSR + 2×LFO +
16-slot mod matrix, Clean 106 routings active) fits **8 voices + unison + chorus** with
large headroom.

---

## Device configuration

| Field | Value |
|---|---|
| Chip | ESP32-P4 (rev v1.0) |
| CPU frequency | **360 MHz** |
| IDF version | v5.5.1 |
| Optimization | **-O2** (release/PERF — see journey below) |
| FTZ | OFF — RV32F has no FTZ bit (ADR 0012) |
| Block | 64 samples @ 48 000 Hz |
| Block period | **480 000 cyc / 1333.33 µs** |
| Budget ceilings | safe ≤70% = 336 000 cyc · hard ≤95% = 456 000 cyc |

---

## The optimization journey (why the numbers moved)

The first device bench of the *real* voice (vs the Stage 0.5 proxy) blew the budget. Four
fixes, each measured:

| Stage | Fix | Per-voice | Fixed overhead | 8 voices |
|---|---|---|---|---|
| Initial | device app was `-Og` (IDF debug) + per-sample `Svf::SetFreq` | ~131k | ~55k | **230%** |
| Build + filter | `-O2` release build; SVF cutoff updated at block rate (kills per-sample `sinf`+`powf`) | ~55k | ~53k | **103%** |
| Round A | per-voice LFOs advanced at **block rate** (one `sinf`/block, not 64) — they were only sampled per block anyway | ~27.5k | ~53k | — |
| Round B | param push to voices **change-gated** — only push params that moved this block (was 256 unconditional `set_param`/block recomputing filter+env transcendentals) | ~27.5k | **~22k** | **50.8%** |

Net per-voice improvement: **4.75×**. None of the four changed the sound (the LFO/param
values were already only consumed at block rate; `-O2` and block-rate filter are
transparent).

---

## Section 3 — real-voice load ramp (final, the gate)

Engine: real Juno voice, Clean 106 routings (ENV2→cutoff + LFO1→PWM) active. Timing:
`synth_render()` direct, 500 blocks after 200-block warmup.

| Voices | cyc/blk | % period | margin (cyc) | verdict |
|---|---|---|---|---|
| 1 | 51 051 | 10.6% | 428 949 | OK |
| 2 | 78 582 | 16.4% | 401 418 | OK |
| 3 | 106 142 | 22.1% | 373 858 | OK |
| 4 | 133 676 | 27.8% | 346 324 | OK |
| 5 | 161 244 | 33.6% | 318 756 | OK |
| 6 | 188 699 | 39.3% | 291 301 | OK |
| 7 | 216 269 | 45.1% | 263 731 | OK |
| **8** | **243 790** | **50.8%** | 236 210 | **OK** |
| **Worst case: U=8 + Chorus I (one note)** | **243 789** | **50.8%** | 236 211 | **OK** |

**Per-voice ≈ 27 540 cyc/blk; fixed intercept ≈ 23 500 cyc/blk.** Full 8-voice polyphony,
including the worst case (all 8 voices detuned on one note with chorus), sits at **50.8%** —
~19 percentage points under the 70% safe ceiling, leaving room for Stage 4 FX + UI jitter.

---

## Section 4 — fixed-overhead decomposition (idle render, 0 voices)

| Label | cyc/blk | % period |
|---|---|---|
| idle render (chorus off) | 22 346 | 4.66% |
| idle render (Chorus I) | 22 355 | 4.66% |
| → Chorus I cost | **9** | ~0.00% |
| idle render (Chorus II) | 22 358 | 4.66% |
| → Chorus II cost | **12** | ~0.00% |

The BBD chorus is effectively free. The remaining ~22k fixed cost is param-store smoother
advance + note/alloc bookkeeping + mono memset + soft-clip + stereo write — acceptable at
4.66%; not worth optimizing given the headroom.

---

## Section 5 — per-block DSP micro-bench (real wrappers, -O2)

| Block | cyc/blk | cyc/smp |
|---|---|---|
| dsp::Osc PolyBLEP saw | 4 615 | 72 |
| dsp::Filter SVF (block-rate freq) | 5 629 | 87 |
| dsp::Env ADSR | 2 125 | 33 |
| dsp::Lfo SINE (per-sample `sinf`) | 11 581 | 180 |
| dsp::Lfo TRI | 2 416 | 37 |
| WhiteNoise | 905 | 14 |
| ModMatrix eval (per block) | 221 | — |

> The `dsp::Lfo SINE` row still measures the per-sample `process()` in isolation (180
> cyc/smp). After Round A the **voice** no longer calls it per sample — it uses
> `process_block(n)` (one `sinf`/block), so this cost no longer appears in Section 3.
> DaisySP `Osc` (4.5×) and `Svf` (6×, double-sampled) remain heavier than hand-rolled
> equivalents — a future lever if more headroom is ever needed, but not required now.

---

## Verdict

**ADR 0003 (8 voices + unison) stands and is device-proven.** No polyphony or unison cap
needed. Stage 3 (Modulation + full Juno) is **complete**. Headroom for Stages 4–7 should be
planned against ~27.5k cyc/voice and ~22k fixed (≈49% of budget free at full 8-voice load).
