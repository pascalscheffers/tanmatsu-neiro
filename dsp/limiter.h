// dsp/limiter.h — feed-forward, stereo-linked peak limiter (ADR 0021).
//
// Placed post-master-gain, pre-soft-clip in synth_render. Detects on
// max(|L|,|R|) and returns a shared gain-reduction coefficient applied to
// both channels, preserving the chorus stereo image.
//
// Fixed constants (no user knobs this round — mirrors how soft_clip shipped
// in ADR 0016; may be exposed as params later):
//   THRESH  0.92  — keeps steady-state in soft_clip's near-linear zone.
//   attack  instantaneous (per-sample peak clamp) — snaps env_gr to target
//           on the same sample the transient arrives. This prevents velocity-
//           scaled transients from escaping into soft_clip's hard ±1.5 ceiling
//           and producing audible crackle on hard playing (confirmed on device:
//           clean at MASTER_GAIN=0.10, crackle on loud strikes under the old
//           1 ms one-pole attack). No look-ahead latency added.
//   release 120 ms — transparent character; no pumping on sustained chords.
//
// Process contract: call once per stereo frame with peak = max(|L|,|R|).
// Returns gr in (0,1]; multiply both channels by gr, then pass through
// soft_clip as the transient safety net. No libm in process() — only in
// init() (expf allowed at init, never in render path). IRAM-safe (inlines).
//
// Denormal strategy (ADR 0012): unity-snap on the recovery tail
// (where target−env decays geometrically toward denormal), finite floor,
// and NaN sanitization so a single upstream NaN cannot latch the envelope.
//
// // reserved for future look-ahead (ADR 0021)
#pragma once
#include <math.h>

namespace dsp {

class LimiterStereo {
public:
    // Safe defaults: pass-through if process() called before init().
    float sr_     = 48000.0f;
    float thresh_ = 0.92f;
    float a_rel_  = 0.0f;  // zeroed → no release smoothing (instant recover)
    float env_gr_ = 1.0f;  // unity gain reduction (pass-through)

    // init: store sample rate, compute release one-pole coefficient (expf allowed
    // here), then reset. Call once at startup / sample-rate change; never in render.
    void init(float sample_rate) {
        sr_     = sample_rate;
        thresh_ = 0.92f;
        // One-pole convention: coef = 1 − exp(−1 / (t_sec × sr))
        a_rel_  = 1.0f - expf(-1.0f / (0.120f * sr_));  // 120 ms release
        reset();
    }

    void reset() { env_gr_ = 1.0f; }

    // process: feed-forward gain-reduction envelope.
    // peak = max(|L·gain|, |R·gain|) for the current frame.
    // Returns gr in (0,1]. No libm, no allocation, IRAM-safe (inlines into caller).
    inline float process(float peak) {
        if (peak != peak) peak = 0.0f;  // NaN guard — limiter has memory
        float target = (peak > thresh_) ? (thresh_ / peak) : 1.0f;
        // Instant attack: when more reduction is needed, clamp this sample to
        // threshold immediately so no transient escapes into soft_clip's hard
        // ±1.5 ceiling. Smooth release (a_rel_) only, to avoid pumping on
        // sustained material.
        if (target < env_gr_) {
            env_gr_ = target;
        } else {
            env_gr_ += a_rel_ * (target - env_gr_);
        }
        // Unity-snap: avoid denormal tail on recovery + clamp above 1
        if (env_gr_ > 1.0f - 1e-7f) env_gr_ = 1.0f;
        // Finite floor: must never go zero or negative
        if (env_gr_ < 1e-6f) env_gr_ = 1e-6f;
        return env_gr_;
    }
};

}  // namespace dsp
