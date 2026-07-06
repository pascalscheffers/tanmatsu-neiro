# Stage 8 — P4 efficiency (§3) + UI polish (§6a/§6d)

> **Status: pre-runbook campaign brief.** Opus-authored map from `specs/FABLE-THOUGHTS.md` §3
> (fix the spikes before chasing throughput) + §6a (draw the curve, not the number) + §6d
> (navigation & wayfinding) + §5 scope/headphone grab-bag. NOT yet closed work-orders — resolve
> gates, then author each per `stages/README.md` (ADR 0017).
>
> **Execution unit:** one clean Sonnet 5 context per sub-stage.
>
> **Depends on Stage 6:** 8b (curve headers) needs 6b's `draw_curve` widget; the scope page needs
> WS3 (6a). 8a is pure engine and independent.
>
> Grounding: `specs/FABLE-THOUGHTS.md` §3a/§3b/§3c, §6a/§6d, §5. Seams: `engine/synth.cpp`
> (command drain at top of block), `engine/synth_config.h` (block size, caps), `engine/command_queue.h`
> (`CommandQueue<NoteCmd>`), `ui/ui.cpp` (`PAGE_TABLE`, `draw_tabs`, `draw_rows`), `ui/ui_widgets.*`
> (from 6b). The open poly-crackle MEMORY item: crackle is **per-block spikes** (note-on bursts +
> blit contention), not average load (8 voices ≈ 52%).

## The reframe (why this stage)
Average CPU is fine; the crackle is per-block **spikes**. The highest-value work is
spike-flatteners (8a), not inner-loop rewrites. §3b is an explicit **won't-do list** — do not let
a fresh worker re-derive dead ends. The UI half of this stage is the cheap "feels like a real
instrument" polish that WS3 (Stage 6) just made free.

## Where we are entering Stage 8
- WS3 dirty-rect blit landed (Stage 6) → redraws are cheap; the `draw_curve` widget exists (6b).
- Command handoff: `engine/synth.cpp` drains the `CommandQueue<NoteCmd>` at the top of each block,
  then runs the allocator + FX. Block size + `kNumVoices` live in `engine/synth_config.h`.
- UI: 9-page `PAGE_TABLE` in `ui/ui.cpp`, F1–F6 shape buttons, hold-to-repeat nudge.

## Kickoff gates
- **G8d — Profile-gated build/CPU changes (CPU-budget/sonic). 🛑 OPEN at 8d.** Block size
  64→128, `-funsafe-math`, silent-voice early-out each need a **device A/B bench** (`make bench`),
  not faith. `-funsafe-math` must **not** enable `-ffinite-math-only` (breaks NaN guards, ADR 0012)
  — use `-funsafe-math-optimizations -fno-math-errno` or keep NaN checks as integer-bit tests.
  Each is a per-change commit gate on measured numbers.
- **G8a — Accent latency (sonic, minor). 🛑 confirm at 8a.** Capping note-ons at 2/block spreads a
  chord over ≤4 blocks (≤~5 ms, below strum perception). Confirm the cap value; note-offs stay
  uncapped (dropping them sticks voices).

## Sub-stage decomposition (running order: 8a → 8b → 8c → 8d)

**8a — Note-on admission cap (§3a — directly targets the open poly-crackle item).** In
`synth.cpp`'s command drain, admit at most `kMaxNoteOnsPerBlock` (=2) note-ons per block; leave
the rest in the queue for next block. Note-offs unlimited. *Seams:* `engine/synth.cpp` drain
loop, `engine/synth_config.h` constant. *Acceptance:* an 8-note chord produces **no over-budget
block** (`PROFILE=1`); regression test; audio unchanged perceptually. *One-context fit:* easily
(S). **This is the highest-value item in the stage — could be pulled ahead if crackle is urgent.**

**8b — Curve-header page graphics (§6a).** Wire 6b's `draw_curve` into page headers, redrawn on
param-dirty only (dirty-rect): AMP/MOD ENV (ADSR curve, edited segment highlighted), FILTER
(analytic SVF magnitude response, ~64 log points, control-side — no FFT), LFO (one cycle + live
phase dot), OSC (single-cycle DCO-mix sketch). *Seams:* `ui/ui.cpp` page headers, `ui/ui_widgets.*`,
control-side curve precompute. *Acceptance:* each of the four pages shows its shape; redraw only
on change. *One-context fit:* M — **split-if** four page headers exceed budget → `8b-i` (ENV +
FILTER) / `8b-ii` (LFO + OSC).

**8c — Wayfinding pack (§6d — four independent acceptance boxes).** (1) Page tab strip (9
abbreviated tabs, current lit — makes CC auto-focus jumps legible); (2) stepped params as carousel
(`… SAW [PULSE] SUB …`); (3) big transient value readout on F1/F2 nudge, fade ~500 ms; (4)
contextual footer (per-page key hints). *Seams:* `ui/ui.cpp` (`draw_tabs`, `draw_rows`, status
strip). *Acceptance:* all four boxes independently verifiable. *One-context fit:* S–M — **split-if**
needed → `8c-i` (tab strip + footer) / `8c-ii` (carousel + transient readout).

**8d — Profile-gated cheap wins (§3c.2–4).** Each device-bench-gated, each its own commit: block
size 64→128 (halves per-block fixed overhead, +1.3 ms latency still <5 ms), silent-voice early-out
(verify voice loop skips `!is_active()` before touching state — 2-line win if not), `-funsafe-math`
on `dsp/`+`engine/` only (with the NaN-guard caveat above). **G8d gates each.** *Acceptance:* each
change shows a measured `make bench` win or is reverted; NaN guards intact. *One-context fit:* S
each — treat as up to three tiny work-orders (`8d-i/ii/iii`) if bench turnaround is slow.

### §3b — WON'T-DO (record explicitly, do not re-derive)
- **PIE SIMD for the float voice loop** — the P4's 128-bit PIE is *integer* SIMD; there is no
  4×f32 vector unit. (PIE *does* pay only at the float→int16 interleave + int16 delay copies, via
  **esp-dsp**, Apache-2.0 — a future reuse-first note, not this stage.)
- **Look-ahead limiter / headroom work** — falsified by the signal probe (gr=1.00 during crackle).
- **Fixed-point voice DSP** — the FPU is good (~27.5k cyc/voice); revisit only if a profile fingers
  a specific block.

## Grab-bag folded in (§5, optional this stage — schedule if a slot allows)
- **Scope page (§5):** a UI page plotting the last render block (waveform) or small esp-dsp FFT.
  Needs WS3 (6a). S–M. Author as `8e` only if the stage has headroom.
- **Headphone-detect duck (§5):** auto-duck MASTER_GAIN into speaker-safe range when the amp is on,
  restore on headphone insert (BSP). S. `8f` if slotted, else moves to Stage 9.

## Continuous (every sub-stage)
`make size`; host + device green; **profile before optimizing** — every §3 change lands as a
measured row in the spec 02 cycles/block budget (ADR 0015), never a silent addition. Membrane
clean.

## First action at kickoff
Read this brief + the poly-crackle handoff in `MEMORY.md` + `synth.cpp` drain loop + `synth_config.h`.
Author **8a** first (biggest crackle win, no gate blocking), dispatch. Resolve **G8d** before 8d.
