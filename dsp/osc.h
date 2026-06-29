// dsp/osc.h — thin wrapper over DaisySP Oscillator.
// Default waveform: PolyBLEP sawtooth. Provides set_note() for MIDI pitch.
// Keep dsp/vendor/ un-edited; wrap here.
#pragma once
#include <stdint.h>
#include "Synthesis/oscillator.h"
#include "Utility/dsp.h"

namespace dsp {

class Osc {
public:
    void init(float sample_rate) {
        osc_.Init(sample_rate);
        osc_.SetAmp(1.0f);
        osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    }

    void set_freq(float hz) { osc_.SetFreq(hz); }
    void set_amp(float a) { osc_.SetAmp(a); }
    void reset() { osc_.Reset(); }

    // MIDI note 0-127 → Hz via DaisySP mtof.
    void set_note(uint8_t note) { osc_.SetFreq(daisysp::mtof((float)note)); }

    float process() { return osc_.Process(); }

private:
    daisysp::Oscillator osc_;
};

}  // namespace dsp
