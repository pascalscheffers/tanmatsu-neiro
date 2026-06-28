# ADR 0016 — Master output stage: linear headroom + gentle soft-clip ceiling

**Status:** accepted (2026-06-28). Resolves the Stage 2 sonic gate *"soft-clip vs
linear headroom"* (deferred at Stage 2b; flagged in `stages/stage-2-param-model-ui.md`
carry-in and the `engine/synth.cpp` render comment). Companion to **ADR 0002** (Juno
voice) and **ADR 0015** (how we spend CPU).

## Context
On device the output **clips audibly at moderate polyphony** (MEMORY.md, 2026-06-28).
Diagnosed as a headroom/gain-staging issue, **not** integer mixing: the bus is float
throughout; only `to_i16` casts, and it **hard-clamps to ±1**. One Juno voice already
peaks ~1.05 pre-filter (osc 0.70 + sub 0.30 + noise 0.05), resonance adds transient
overshoot, and summed held voices exceed full scale despite the chorus's ×0.25 and the
×0.5 master gain. The hard clamp in `to_i16` is the source of the audible clip.

The question deferred to this gate: when a chord exceeds full scale, does the output
stage stay **fully linear** (just quieter, clip avoided by gain alone), add a **gentle
soft-clip** ceiling, or bake in **overt drive/saturation**? This changes how the synth
sounds, so it was Pascal's call (CLAUDE.md "When to Ask").

## Decision
**Headroom first, then a gentle soft-clip as the ceiling.** Two layers:

1. **Linear gain staging** keeps normal playing in the near-linear region. The existing
   `MASTER_GAIN` default (0.5, −6 dB) already places a realistic full-chord peak around
   ~1.0 pre-clip; ordinary 1–3-note playing sits well below the soft-clip knee and is
   **bit-transparent** (unity gain through the clipper).
2. A **cubic soft-clip** on the master bus (post-gain, pre-`to_i16`) catches peaks
   instead of hard-clamping. Unity slope at small signal, a soft knee, and a smooth
   approach to ±1 — odd-harmonic character only on transients, never a hard digital edge.

**Rejected:** *linear-only* (transparent but single notes too quiet, and a stray peak
still slams the `to_i16` clamp) and *baked-in drive* (colors every signal globally and
spends `tanhf` per sample — that belongs to a future, dialable `MASTER_DRIVE` patch
parameter, not a hardcoded master constant; see Consequences).

## The curve (what Sonnet implements)
A standard cubic soft-clip, chosen so **small-signal gain is exactly unity** (normal play
unaffected) and the curve reaches ±1 with zero slope at ±1.5:

```c
// dsp/saturate.h — pure, header-only, no vendor edit, no anti-denormal needed
// (no feedback path). Inline → IRAM-safe via the synth_render IRAM_ATTR (ADR 0013).
static inline float soft_clip(float x) {
    if (x >=  1.5f) return  1.0f;
    if (x <= -1.5f) return -1.0f;
    return x - x * x * x * (1.0f / 6.75f);   // x - x³/6.75
}
```

Properties: slope 1 at x=0 (transparent below the knee); soft saturation across
|x| ≈ 0.66…1.5; peak value exactly ±1 at x=±1.5 with matched (zero) slope, then a hard
clamp only beyond ±1.5 — which the gain staging keeps the signal away from in practice.
**Cheap:** three mults + a compare per sample, **no libm call** (unlike `tanhf`, ~140
cyc/smp and flat across `-O`, per Stage 0.5). Stereo master bus only.

Wire it in `engine/synth.cpp` step 6, applied to `left[i]`/`right[i]` **after**
`* gain`, before they leave the function. Update the stale `synth.cpp` gate comment to
cite this ADR.

## Consequences
- Fixes the clipping defect without coloring normal playing; the synth stays clean until
  it's genuinely pushed, then folds gracefully (CLAUDE.md "fail safe in the audio path").
- Leaves the door open for **overt analog grit as an explicit `MASTER_DRIVE` parameter**
  later (Stage 3 patch/mod work) — a pre-clip drive term feeding the same curve, defaulted
  low. The output ceiling and the character knob stay separate decisions.
- The soft-clip lands as a **reusable `dsp/saturate.h`** block (pure, host-testable),
  not inline magic — consistent with the `dsp/` wrapper convention.
- Negligible CPU; no new line item needed in the spec 02 cycles/block budget (a few
  flops per stereo sample, dwarfed by one voice).
