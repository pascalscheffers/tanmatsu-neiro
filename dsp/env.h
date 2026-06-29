// dsp/env.h — thin wrapper over DaisySP Adsr.
// reset() re-initialises to IDLE (no click, no release tail).
#pragma once
#include "Control/adsr.h"

namespace dsp {

class Env {
public:
    void init(float sample_rate) {
        sr_ = sample_rate;
        adsr_.Init(sample_rate);
    }

    void set_attack(float s) { adsr_.SetAttackTime(s); }
    void set_decay(float s) { adsr_.SetDecayTime(s); }
    void set_sustain(float v) { adsr_.SetSustainLevel(v); }
    void set_release(float s) { adsr_.SetReleaseTime(s); }

    // gate=true during hold, false on note_off; returns envelope level 0-1.
    float process(bool gate) { return adsr_.Process(gate); }

    // True when envelope is in IDLE (before first note or after release).
    bool is_idle() const { return !adsr_.IsRunning(); }

    // Re-init to IDLE — instant silence, no release tail. Used for voice steal.
    void reset() { adsr_.Init(sr_); }

private:
    daisysp::Adsr adsr_;
    float         sr_ = 48000.0f;
};

}  // namespace dsp
