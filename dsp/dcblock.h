// dsp/dcblock.h — thin wrapper over DaisySP DcBlock (leaky differentiator HPF).
// Removes the DC / very-low-frequency component of a signal; corner ~1.6 Hz
// (gain = 1 - 10/Fs), so it re-centres an offset waveform without touching bass.
// Keep dsp/vendor/ un-edited; wrap here.
//
// ADR 0012: the P4 has no hardware flush-to-zero, and DcBlock's one-pole feedback
// (gain_ * output_) decays toward a denormal as the signal goes silent. Inject a
// tiny DC bias into the input so the feedback state never underflows to a denormal.
#pragma once
#include "Utility/dcblock.h"

namespace dsp {

class DcBlock {
public:
    void init(float sample_rate) { dc_.Init(sample_rate); }

    // Anti-denormal guard (+1e-20f) per ADR 0012 — the tiny bias is itself
    // removed by the DC blocker, so it never reaches the output.
    float process(float in) { return dc_.Process(in + 1e-20f); }

private:
    daisysp::DcBlock dc_;
};

}  // namespace dsp
