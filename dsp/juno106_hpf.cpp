// dsp/juno106_hpf.cpp — coefficient derivations for dsp/juno106_hpf.h.
//
// Both derivations use the prewarped bilinear transform of a first-order
// analog prototype; the algebra and magnitude-response verification are in
// specs/notes/juno106-hpf-analysis.md — this file is the direct
// implementation of the closed-form results there, nothing more.
#include "dsp/juno106_hpf.h"
#include <math.h>

namespace dsp {

void Juno106Hpf::hpf_coeffs(float fc) {
    // Analog prototype H(s) = s / (s + w0), w0 = 2*pi*fc, prewarped bilinear
    // transform (alpha = tan(w0*T/2) = tan(pi*fc/Fs)):
    //   b0 =  1 / (1 + alpha)
    //   b1 = -1 / (1 + alpha)
    //   a1 = (alpha - 1) / (alpha + 1)
    float alpha = tanf((float)M_PI * fc / sample_rate_);
    float inv   = 1.0f / (1.0f + alpha);
    b0_         = inv;
    b1_         = -inv;
    a1_         = (alpha - 1.0f) / (alpha + 1.0f);
}

void Juno106Hpf::shelf_coeffs(float fc, float gain_db_at_fc) {
    // Analog prototype H(s) = (s + G*w0) / (s + w0), w0 = 2*pi*fc. G is
    // solved so the analytic magnitude AT the corner (f = fc) equals the
    // target gain: |H(j*w0)| = sqrt(1+G^2)/sqrt(2) = target_linear
    //   => G = sqrt(2*target_linear^2 - 1)
    // Then the same prewarped bilinear transform as hpf_coeffs():
    //   b0 = (1 + G*alpha) / (1 + alpha)
    //   b1 = (G*alpha - 1) / (1 + alpha)
    //   a1 = (alpha - 1) / (1 + alpha)
    float target_linear = powf(10.0f, gain_db_at_fc / 20.0f);
    float g             = sqrtf(2.0f * target_linear * target_linear - 1.0f);
    float alpha         = tanf((float)M_PI * fc / sample_rate_);
    float inv           = 1.0f / (1.0f + alpha);
    b0_                 = (1.0f + g * alpha) * inv;
    b1_                 = (g * alpha - 1.0f) * inv;
    a1_                 = (alpha - 1.0f) * inv;
}

}  // namespace dsp
