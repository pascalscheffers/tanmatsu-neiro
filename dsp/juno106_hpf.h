// dsp/juno106_hpf.h — attributed Juno-106 HPF extraction from KR-106.
//
// Adapted from Ultramaster KR-106 v2.5.13 (commit bc15cae), specifically
// Source/DSP/KR106_HPF.h::getJuno106HPFFreq/BassBoostFilter and
// Source/DSP/KR106_DSP.h::kr106::HPF. This target keeps only J106 behavior,
// uses float state for RV32F, and adds finite/denormal hygiene required by
// ADR 0012. Full provenance and adaptation notes are in
// dsp/vendor/kr106/README.md.
#pragma once

#include <math.h>

namespace dsp {

enum Juno106HpfPosition {
    JUNO106_HPF_BASS_BOOST = 0,
    JUNO106_HPF_BYPASS     = 1,
    JUNO106_HPF_236HZ      = 2,
    JUNO106_HPF_754HZ      = 3,

    // Compatibility names retained for the existing public API. The pinned
    // KR-106 response is 236/754 Hz, not the old approximate 225/700 Hz.
    JUNO106_HPF_225HZ = JUNO106_HPF_236HZ,
    JUNO106_HPF_700HZ = JUNO106_HPF_754HZ,
};

class Juno106Hpf {
public:
    void               init(float sample_rate);
    void               set_position(Juno106HpfPosition pos);
    Juno106HpfPosition position() const { return position_; }
    float              process(float in) {
        if (!isfinite(in)) in = 0.0f;

        float out = process_with(in, current_);
        if (crossfade_remaining_ > 0) {
            float previous = process_with(in, previous_);
            float t = static_cast<float>(crossfade_remaining_) / static_cast<float>(kCrossfadeSamples);
            out     = out * (1.0f - t) + previous * t;
            --crossfade_remaining_;
        }
        return isfinite(out) ? out : 0.0f;
    }

private:
    static constexpr float kDcBlockHz        = 0.35f;
    static constexpr int   kCrossfadeSamples = 64;
    static constexpr float kStateFloor       = 1.0e-20f;

    struct BassCoeffs {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    struct ProcessorState {
        float freq_hz = 0.0f;
        float cut_g   = 0.0f;
        float cut_s   = 0.0f;
        float dc_s    = 0.0f;
        float bass_z1 = 0.0f;
        float bass_z2 = 0.0f;
    };

    void compute_coeffs();

    float process_with(float in, ProcessorState& state) {
        float shaped  = state.freq_hz < 0.0f ? process_bass(in, state) : in;
        float blocked = process_dc(shaped, state);
        if (state.freq_hz <= 0.0f) return blocked;

        float v     = (blocked - state.cut_s) * state.cut_g / (1.0f + state.cut_g);
        float lp    = state.cut_s + v;
        state.cut_s = clean_state(lp + v);
        float out   = blocked - lp;
        return isfinite(out) ? out : 0.0f;
    }

    float process_bass(float in, ProcessorState& state) const {
        float out = bass_.b0 * in + state.bass_z1;
        if (!isfinite(out)) {
            state.bass_z1 = 0.0f;
            state.bass_z2 = 0.0f;
            return 0.0f;
        }
        state.bass_z1 = clean_state(bass_.b1 * in - bass_.a1 * out + state.bass_z2);
        state.bass_z2 = clean_state(bass_.b2 * in - bass_.a2 * out);
        return out;
    }

    float process_dc(float in, ProcessorState& state) const {
        float v    = (in - state.dc_s) * dc_g_ / (1.0f + dc_g_);
        float lp   = state.dc_s + v;
        state.dc_s = clean_state(lp + v);
        float out  = in - lp;
        return isfinite(out) ? out : 0.0f;
    }

    static float clean_state(float value) {
        if (!isfinite(value) || fabsf(value) < kStateFloor) return 0.0f;
        return value;
    }

    float              sample_rate_ = 48000.0f;
    float              dc_g_        = 0.0f;
    Juno106HpfPosition position_    = JUNO106_HPF_BYPASS;
    BassCoeffs         bass_;
    ProcessorState     current_;
    ProcessorState     previous_;
    int                crossfade_remaining_ = 0;
};

}  // namespace dsp
