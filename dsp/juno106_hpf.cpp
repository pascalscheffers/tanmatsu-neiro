// dsp/juno106_hpf.cpp — J106-only adaptation of the pinned KR-106 HPF.
#include "dsp/juno106_hpf.h"
#include <math.h>

namespace dsp {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float position_frequency(Juno106HpfPosition position) {
    switch (position) {
        case JUNO106_HPF_BASS_BOOST:
            return -1.0f;
        case JUNO106_HPF_236HZ:
            return 236.0f;
        case JUNO106_HPF_754HZ:
            return 754.0f;
        case JUNO106_HPF_BYPASS:
        default:
            return 0.0f;
    }
}

Juno106HpfPosition clamp_position(Juno106HpfPosition position) {
    int value = static_cast<int>(position);
    if (value < 0) value = 0;
    if (value > 3) value = 3;
    return static_cast<Juno106HpfPosition>(value);
}

}  // namespace

void Juno106Hpf::init(float sample_rate) {
    sample_rate_         = isfinite(sample_rate) && sample_rate > 1.0f ? sample_rate : 48000.0f;
    position_            = JUNO106_HPF_BYPASS;
    current_             = {};
    previous_            = {};
    crossfade_remaining_ = 0;
    compute_coeffs();
}

void Juno106Hpf::set_position(Juno106HpfPosition position) {
    position            = clamp_position(position);
    float new_frequency = position_frequency(position);
    if (new_frequency != current_.freq_hz) {
        // KR-106 semantics: a repeated switch snapshots the live current
        // processor, replacing any older crossfade snapshot.
        previous_            = current_;
        crossfade_remaining_ = kCrossfadeSamples;
        current_.freq_hz     = new_frequency;
        if (new_frequency > 0.0f) {
            float normalized = new_frequency / (sample_rate_ * 0.5f);
            if (normalized < 0.001f) normalized = 0.001f;
            if (normalized > 0.9f) normalized = 0.9f;
            current_.cut_g = tanf(normalized * kPi * 0.5f);
        } else {
            current_.cut_g = 0.0f;
        }
    }
    position_ = position;
}

void Juno106Hpf::compute_coeffs() {
    // Common 10 uF / 44.9 kOhm pre-HPF AC coupling pole.
    float dc_normalized = kDcBlockHz / (sample_rate_ * 0.5f);
    if (dc_normalized < 0.0f) dc_normalized = 0.0f;
    if (dc_normalized > 0.9f) dc_normalized = 0.9f;
    dc_g_ = tanf(dc_normalized * kPi * 0.5f);

    // Exact KR-106 J106 bass circuit constants.
    constexpr float kR1  = 47.0e3f;
    constexpr float kC1  = 0.047e-6f;
    constexpr float kCa  = 0.01e-6f;
    constexpr float kRg  = 10.0e3f;
    constexpr float kRf  = 100.0e3f;
    constexpr float kCf  = 0.022e-6f;
    constexpr float kR43 = 47.0e3f;
    constexpr float kR44 = 220.0e3f;
    constexpr float kR45 = 47.0e3f;

    const float tau_1z = kR1 * kC1;
    const float tau_1p = kR1 * (kC1 + kCa);
    const float tau_2z = (kRg * kRf / (kRg + kRf)) * kCf;
    const float tau_2p = kRf * kCf;
    const float alpha  = kR45 / kR44;
    const float direct = kR45 / kR43;
    const float ag     = alpha * (1.0f + kRf / kRg);

    const float d0 = 1.0f;
    const float d1 = tau_1p + tau_2p;
    const float d2 = tau_1p * tau_2p;
    const float n0 = direct * d0 + ag;
    const float n1 = direct * d1 + ag * (tau_1z + tau_2z);
    const float n2 = direct * d2 + ag * (tau_1z * tau_2z);
    const float k  = 2.0f * sample_rate_;
    const float k2 = k * k;
    const float a0 = d0 + d1 * k + d2 * k2;

    bass_.b0 = (n0 + n1 * k + n2 * k2) / a0;
    bass_.b1 = 2.0f * (n0 - n2 * k2) / a0;
    bass_.b2 = (n0 - n1 * k + n2 * k2) / a0;
    bass_.a1 = 2.0f * (d0 - d2 * k2) / a0;
    bass_.a2 = (d0 - d1 * k + d2 * k2) / a0;
}

}  // namespace dsp
