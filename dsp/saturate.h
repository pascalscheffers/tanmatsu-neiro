// dsp/saturate.h — gentle cubic soft-clip (ADR 0016).
//
// Applied post-master-gain in synth_render. Pass-through for |x| near zero;
// unity slope at 0; output bounded to ±1 for |x| >= 1.5.
// Formula: x − x³/6.75, hard-clamped at ±1.5 → ±1.
// Cheap (no libm), IRAM-safe. No feedback path so no anti-denormal needed.
#pragma once

static inline float soft_clip(float x) {
    if (x >= 1.5f) return 1.0f;
    if (x <= -1.5f) return -1.0f;
    return x - x * x * x * (1.0f / 6.75f);
}
