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

    // Waveform select: 0=SAW, 1=PULSE, 2=TRI. Out-of-range → SAW.
    void set_waveform(int wf) {
        switch (wf) {
            case 0:
                osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
                break;
            case 1:
                osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
                break;
            case 2:
                osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_TRI);
                break;
            default:
                osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
                break;
        }
    }

    // Pulse width [0, 1]. Only affects WAVE_POLYBLEP_SQUARE; no-op on other waveforms.
    void set_pw(float pw) { osc_.SetPw(pw); }

    float process() { return osc_.Process(); }

private:
    daisysp::Oscillator osc_;
};

}  // namespace dsp
