# Stage 4 — Timing, Arp, Sequencer, FX (campaign brief)

> **Status: pre-runbook.** This is the Opus-authored campaign map for Stage 4, written at the
> end of Stage 3 so the next session can drive it. It is NOT yet a set of Sonnet-executable
> work-orders — the **Kickoff gates** below must be resolved with Pascal first; each sub-stage's
> closed work-order is authored once its gates are settled (per `stages/README.md`, ADR 0017).
>
> Grounding: roadmap `specs/06`, **ADR 0010** (sample-accurate clock), **ADR 0015** (spend the
> CPU headroom on richness/FX first — fatter, not more voices). Seams: `specs/MAP.md`.

## Where we are entering Stage 4
- Stage 3 complete & ratified: full Juno voice, 16-slot mod matrix, 2 ENV / 2 LFO (LFO now a
  shared free-running engine pair, ADR 0018), play modes, unison. Device CPU at full load
  (8 voices + unison + chorus) = **50.8% of the 480k-cyc budget** (`stage-3d-ii-results.md`).
  Flash 893 KB / 2 MB (54% free), DIRAM 145 KB.
- The control→audio handoff already exists: `engine/command_queue.h` (`NoteCmd` ring) on the
  reusable `engine/spsc_ring.h`; params via `engine/param_store` (single write path). The
  master FX bus is `synth.cpp` step 6 (chorus → master gain → `soft_clip` → stereo out).

## Sub-stage decomposition (suggested order — confirm at kickoff)

**4a — Master clock + event scheduler (FOUNDATION, do first).** Implements ADR 0010: musical
clock derived from the audio sample counter; BPM → samples/tick at **96 PPQN**; pluggable clock
source (`internal` now, `external` MIDI-clock later as a new source — no consumer change);
transport (start/stop/continue) + tap tempo; an event scheduler that timestamps note/param
events in sample-time and dispatches them at (sub-)block boundaries through the existing ring.
*Seams:* new `engine/clock.{h,cpp}` + `engine/scheduler.{h,cpp}`; advance the clock by `frames`
each block in `synth_render` (same pattern as the shared LFO); params BPM + clock source +
transport. Host-testable in isolation. Negligible audio-thread cost.

**4b — Arpeggiator.** Consumes 4a's clock; turns held notes into timed note on/off events.
Modes (up/down/up-down/order/random), octave range, rate as clock divisions, gate length, swing,
latch. *Layer:* control (it generates note events, like `control/keyboard`). *Seams:* new
`control/arp.{h,cpp}`; param rows + a UI page (renders from the table automatically); interacts
with sustain/hold. Control-rate; negligible audio cost.

**4c — Pattern sequencer (LARGEST).** Step programming + real-time record + per-step param-locks,
played back through 4a's scheduler. *Seams:* a pattern **data model** (extends spec 05) +
`control/sequencer.{h,cpp}`; UI pages; preset/SD storage for patterns. Song-mode/chaining stays
**later** (roadmap). The data-model + storage format is the heavy decision here (gate G3).

**4d — FX bus: tempo-synced delay + reverb.** Insert after chorus in the `synth.cpp` master chain.
Delay reads the clock for sync divisions. **Reverb is the single largest legitimate budget
consumer (ADR 0015 #1) and is NOT in the Stage 0.5 proxy bench** — it carries a hard device-CPU
gate (G4). Reuse DaisySP where possible (e.g. `ReverbSc`) — confirm cost + license + vendor SHA.
*Seams:* new `dsp/` fx wrappers (delay line, reverb), `synth.cpp` master chain, params, UI,
preset. *Memory:* delay line + reverb tails likely **PSRAM** (spec 02 placement) — a 1 s stereo
delay ≈ 384 KB; size it deliberately.

*Order rationale:* 4a is a hard prerequisite for 4b, 4c, and delay-sync. After 4a, a reasonable
default is **4b (arp) → 4d (FX, the audible richness ADR 0015 prioritises) → 4c (sequencer, the
biggest, benefits from a mature clock+arp)** — but the order is a kickoff decision (G1).

## 🛑 Kickoff gates — resolve with Pascal BEFORE authoring work-orders
- **G1 — Sub-stage order & MVP slice.** Confirm 4a-first; choose arp-vs-FX-vs-seq order; decide
  whether to ship a thin playable slice of each early or complete each in turn.
- **G2 — Arp scope (sonic/feature).** Which modes ship; default rate/division set; swing range;
  latch behavior; how arp composes with sustain pedal and with the sequencer.
- **G3 — Sequencer data model + storage (architecture).** Steps/pattern, patterns count,
  per-step fields, **which params are lockable**, and the on-disk format — this extends the
  spec 05 preset/SD format and is preset-format-relevant (coordinate with `param_id.h`).
- **G4 — Reverb algorithm + device CPU profile (🛑 the real budget gate).** Pick the algorithm,
  then **profile its block cost on device** against the 70% ceiling before committing (ADR 0015).
  Delay is cheap; reverb is not. Also decide FX **chain order** (chorus→delay→reverb?) and delay
  sync divisions (sonic).
- **G5 — Display/UI render-path profile (deferred dependency).** ADR 0015 wants a Stage-0.5-style
  UI/PAX profiling pass before leaning on per-frame visual feedback; arp/seq pages add UI load.
  Not core to 4a–4d audio, but schedule it when the UI work in 4b/4c begins.

## Continuous (every sub-stage)
Track `make size`; keep host + device green; **profile before optimizing**; every FX/voice spend
lands as a measured row in the spec 02 cycles/block budget (ADR 0015), not a silent addition.

## First action at kickoff
Read this brief + `specs/06` + ADR 0010 + ADR 0015 + `stage-3d-ii-results.md`, run the G1–G4
gates with Pascal, then author the **4a** closed work-order (clock+scheduler) and dispatch a
fresh Sonnet worker. Everything downstream hangs off 4a.
