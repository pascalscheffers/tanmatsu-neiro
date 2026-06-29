// dsp/lfo.h — simple phase-accumulator LFO for sub-audio modulation.
//
// Pure, portable: no ESP-IDF, no I/O, no globals (CLAUDE.md dsp/ rules).
// Anti-denormal: +1e-20f DC offset on triangle/sine output per ADR 0012
// (RV32F has no hardware flush-to-zero).
//
// Waveforms: SINE, TRI(angle), SAW, SQUARE, S&H (sample-and-hold).
// All outputs are in [−1, +1] range.
//
// Usage:
//   dsp::Lfo lfo;
//   lfo.init(48000.0f);
//   lfo.set_rate(1.5f);          // Hz
//   lfo.set_waveform(dsp::LfoWave::SINE);
//   float v = lfo.process();     // returns one sample
//   lfo.reset();                 // resets phase to 0

#pragma once
#include <cmath>
#include <cstdlib>  // rand

namespace dsp {

enum class LfoWave : int {
    SINE   = 0,
    TRI    = 1,
    SAW    = 2,   // upward ramp 0..+1..-1..0
    SQUARE = 4,   // +1 for first half, -1 for second half
    SH     = 5,   // sample-and-hold: new random value each cycle
};

class Lfo {
public:
    void init(float sample_rate) {
        sr_       = sample_rate;
        phase_    = 0.0f;
        phase_inc_= 0.0f;
        sh_value_ = 0.0f;
        wave_     = LfoWave::SINE;
    }

    // Set rate in Hz (0 = frozen).
    void set_rate(float hz) {
        if (hz < 0.0f) hz = 0.0f;
        phase_inc_ = hz / sr_;
    }

    void set_waveform(LfoWave w) { wave_ = w; }

    // Advance one sample and return the output in [−1, +1].
    float process() {
        float out = compute(phase_);

        // Advance phase.
        float prev = phase_;
        phase_ += phase_inc_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            // S&H: latch a new random value at cycle start.
            if (wave_ == LfoWave::SH) {
                // rand() is not in the hot audio path (S&H fires once per LFO
                // cycle which at typical rates is once every thousands of samples),
                // but it is technically non-deterministic. Acceptable for an LFO.
                sh_value_ = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            }
        }
        (void)prev;

        return out;
    }

    // Advance the LFO by n samples in one step and return the block-end output.
    // Equivalent (to sub-sample phase precision) to calling process() n times and
    // using the final value — but computes the waveform (e.g. sinf) only ONCE per
    // block. Use this from block-rate callers (e.g. juno_voice render()).
    float process_block(uint32_t n) {
        phase_ += phase_inc_ * (float)n;
        // Wrap; re-latch S&H on each cycle boundary crossed (rare at sub-audio rates).
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            if (wave_ == LfoWave::SH) {
                sh_value_ = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            }
        }
        return compute(phase_);
    }

    // Reset phase accumulator to 0 (re-sync).
    void reset() {
        phase_    = 0.0f;
        sh_value_ = 0.0f;
    }

private:
    float   sr_       = 48000.0f;
    float   phase_    = 0.0f;   // normalised [0, 1)
    float   phase_inc_= 0.0f;   // Hz / sample_rate
    float   sh_value_ = 0.0f;   // last latched S&H value
    LfoWave wave_     = LfoWave::SINE;

    float compute(float ph) const {
        // ph ∈ [0, 1); all outputs ∈ [−1, +1].
        // Anti-denormal offset (+1e-20f) is added to continuous waveforms so
        // the output never underflows to a denormal on the P4 (no HW FTZ).
        switch (wave_) {
            case LfoWave::SINE: {
                // sinf is fine at sub-audio rate; not hot enough to matter.
                float v = sinf(ph * 6.2831853f);   // 2π
                return v + 1e-20f;
            }
            case LfoWave::TRI: {
                // 0→1: ph∈[0,0.5) → 0→+1; ph∈[0.5,1) → +1→-1
                float v = (ph < 0.5f)
                    ? (4.0f * ph - 1.0f)
                    : (3.0f - 4.0f * ph);
                return v + 1e-20f;
            }
            case LfoWave::SAW: {
                // Upward ramp: [0,1) → [-1, +1)
                float v = ph * 2.0f - 1.0f;
                return v + 1e-20f;
            }
            case LfoWave::SQUARE:
                // No anti-denormal needed: hard ±1 never denormalizes.
                return (ph < 0.5f) ? 1.0f : -1.0f;
            case LfoWave::SH:
                // Value latched at cycle start; held until next cycle.
                return sh_value_;
            default:
                return 0.0f;
        }
    }
};

} // namespace dsp
