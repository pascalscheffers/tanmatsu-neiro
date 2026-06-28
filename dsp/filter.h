// dsp/filter.h — thin wrapper over DaisySP Svf (state-variable filter).
// Adds anti-denormal injection per ADR 0012: RISC-V RV32F has no hardware
// flush-to-zero; a tiny DC bias on the input prevents state denormals from
// accumulating in the SVF feedback paths.
#pragma once
#include "Filters/svf.h"

namespace dsp {

enum FilterMode { FILTER_LP = 0, FILTER_BP, FILTER_HP };

class Filter {
public:
    void init(float sample_rate) { svf_.Init(sample_rate); }
    void set_freq(float hz)       { svf_.SetFreq(hz); }
    void set_res(float res)       { svf_.SetRes(res); }
    void set_mode(FilterMode m)   { mode_ = m; }

    // ADR 0012: 1e-18f is inaudible but keeps SVF states above the
    // denormal threshold on hardware without FTZ.
    void process(float in) { svf_.Process(in + 1e-18f); }

    // Mode-selected output; call after process().
    float output() {
        switch (mode_) {
            case FILTER_LP: return svf_.Low();
            case FILTER_BP: return svf_.Band();
            case FILTER_HP: return svf_.High();
        }
        return svf_.Low();
    }

private:
    daisysp::Svf svf_;
    FilterMode   mode_ = FILTER_LP;
};

} // namespace dsp
