# ADR 0015 — Spending the CPU headroom (richness over raw voice count)

**Status:** accepted (2026-06-28) — direction set; the per-feature spend is profiled and
ratified as each feature lands. Companion to **ADR 0003** (which holds voice count at 8).

## Context
Stage 0.5 on-device profiling (`stages/stage-0.5-results.md`, -O2, P4 @ 360 MHz) showed the
audio budget is **massively under-spent**: 8 proxy voices = 4.4% of the 480 000-cyc block,
32 = 17.5%, zero underruns; linear headroom to ~128 proxy voices at the 70% ceiling. Memory
is not the constraint either — voices are state-only with shared PSRAM tables (spec 02). For
the VA MVP, we run with **~95% of the audio period idle**.

That inverts the question the early specs asked. It is no longer "what can we afford?" but
"what is the idle budget best spent on?" Leaving it unstated risks a future session either
(a) reflexively cranking polyphony, or (b) "optimizing" against a CPU constraint that no
longer binds. This ADR records the intended *direction* of spend; ADR 0003 keeps the voice
count.

The caveat that keeps this honest: the Stage 0.5 proxy is deliberately light (no ladder
`tanh`, no per-sample smoothing, no multi-osc unison, **no FX bus at all**). A real DaisySP
voice is ~5–8× the proxy, and effects — reverb especially — can each cost more than several
voices. The headroom is real but the bench measured the cheap part. Every spend below is
**gated on a real profile**, not on the proxy extrapolation.

## Decision
Spend the headroom on **per-voice and per-patch richness first, raw voice count last.** This
is a Juno-character instrument: fatter beats more-notes. Priority order:

1. **FX bus depth — a proper reverb** after the chorus (spec 02 master bus, currently a
   nice-to-have). Likely the single largest legitimate consumer of the budget and the most
   audible. Profile its real block cost before committing; it is not in the proxy.
2. **Per-voice quality** — affordable now that cycles are cheap:
   - **2× oversampling of the voice** (or just the nonlinear ladder stage) to chase the
     stated *"sparkling highs"* goal by pushing aliasing above the audible band. Oversampling
     a band-limited saw + ladder is a textbook headroom sink and directly serves the sound.
   - Fatter/denser **unison**, richer filter modes.
3. **Visual feedback — waveform / scope animation on the badge screen** (see below). Serves
   playability and the demoscene/instrument feel; small CPU, but a **display/PSRAM-bandwidth
   and UI-core** cost that is *entirely unmeasured* today.
4. **Raw voice count (12/16)** — only if a real-voice profile shows comfortable margin *and*
   a musical need appears. Last resort, not first reflex. Gated by ADR 0003's guardrails
   (single `kNumVoices` constant, state-only pool, O(n) allocator).

No single feature is pre-authorized to consume the budget. Each lands against a measured
block cost and the 70% period ceiling, tracked in the spec 02 cycles/block budget row.

**Confirmed sonic balance (Pascal, 2026-06-28): fatter sound, fewer voices.** This is the
ratified default, not just a starting lean — spend goes to per-voice/FX richness first. The
balance is not a one-way door: voice-count-vs-richness **variants are a config concern, not a
rewrite** — a compile-time knob now (the single `kNumVoices` constant, ADR 0003), and a
candidate **runtime** setting later (e.g. a "16 thin / 8 fat" performance mode), once the
real-voice profile bounds what each variant costs. Design the voice pool and allocator so the
count is read from config, not baked — which the ADR 0003 guardrails already require.

## Waveform animation — feasible, but profile the *video* path, not the audio path
The likely cost of an oscilloscope/waveform view is **not** audio-thread cycles (copying a
ring of samples out is trivial and lock-free). It is on the **UI core**: PAX draw time for a
per-frame polyline, framebuffer churn in PSRAM, and present/blit bandwidth — none of which
Stage 0.5 touched (the bench has no UI). So before relying on it:

- Add a **display/UI profiling pass** (a Stage 0.5-style harness for the render→present
  path): measure PAX frame time + present cost for a full-width waveform at the target FPS,
  on device, at the real framebuffer depth (24_888 RGB, ADR 0011).
- Decide the **sample tap**: the audio thread publishes a down-sampled snapshot into a
  lock-free single-writer ring (no audio-path drawing, no locks — CLAUDE.md RT rules); the UI
  reads it at its own cadence. Decimate to ~screen-width points; never draw per-sample.
- Watch **PSRAM bandwidth**: framebuffers live in PSRAM (spec 02 placement table); a
  per-frame full-screen redraw competes with everything else on the PSRAM bus. Dirty-rect or
  a dedicated scope strip, not a full repaint, if the profile is tight.

Expectation: cheap, as the user suspects — but it is a separate budget (video, not audio) and
gets its own measurement before we lean on it.

## Consequences
- ADR 0003 holds at 8 voices; this ADR documents *why the spare cycles don't go to voice 9*.
- A new profiling task is implied: **display/UI render-path benchmark** (waveform animation
  feasibility), analogous to Stage 0.5's audio bench — schedule it when UI work begins
  (around Stage 2's param/UI pages), before the scope feature is promised.
- Reverb and oversampling become explicit, profiled line items in the spec 02 cycles/block
  budget as they land — not silent additions.
- The default posture for "we have spare CPU" is now **recorded**: richness first, count
  last, every spend measured. Future sessions inherit the reasoning, not just the numbers.
