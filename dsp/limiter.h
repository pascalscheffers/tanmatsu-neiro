// dsp/limiter.h — feed-forward, stereo-linked peak limiter (ADR 0021).
//
// Placed post-master-gain, pre-soft-clip in synth_render. Detects on
// max(|L|,|R|) and returns a shared gain-reduction coefficient applied to
// both channels, preserving the chorus stereo image.
//
// Fixed constants (no user knobs this round — mirrors how soft_clip shipped
// in ADR 0016; may be exposed as params later):
//   THRESH  0.92  — keeps steady-state in soft_clip's near-linear zone;
//                   leaves headroom for attack-miss transients before ±1.5.
//   attack  1.0 ms — ~48-sample catch at 48 kHz; no per-sample gain jumps.
//   release 120 ms — transparent character; no pumping on sustained chords.
//
// Process contract: call once per stereo frame with peak = max(|L|,|R|).
// Returns gr in (0,1]; multiply both channels by gr, then pass through
// soft_clip as the transient safety net. No libm in process() — only in
// init() (expf allowed at init, never in render path).
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
    float a_att_  = 0.0f;  // zeroed → no attack smoothing (instant catch)
    float a_rel_  = 0.0f;  // zeroed → no release smoothing (instant recover)
    float env_gr_ = 1.0f;  // unity gain reduction (pass-through)

    // init: store sample rate, compute one-pole coefficients (expf allowed here),
    // then reset. Call once at startup / sample-rate change; never in render.
    void init(float sample_rate) {
        sr_     = sample_rate;
        thresh_ = 0.92f;
        // One-pole convention: coef = 1 − exp(−1 / (t_sec × sr))
        a_att_  = 1.0f - expf(-1.0f / (0.001f * sr_));  // 1.0 ms attack
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
        // smaller target_gr ⇒ we need more reduction ⇒ attack (faster coef)
        float coef   = (target < env_gr_) ? a_att_ : a_rel_;
        env_gr_     += coef * (target - env_gr_);
        // Unity-snap: avoid denormal tail on recovery + clamp above 1
        if (env_gr_ > 1.0f - 1e-7f) env_gr_ = 1.0f;
        // Finite floor: must never go zero or negative
        if (env_gr_ < 1e-6f) env_gr_ = 1e-6f;
        return env_gr_;
    }
};

}  // namespace dsp
