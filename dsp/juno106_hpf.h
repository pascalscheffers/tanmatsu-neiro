// dsp/juno106_hpf.h — Juno-106 four-position HPF switch (pure DSP block).
//
// Models the Juno-106 front-panel HPF switch's four positions as one shared
// first-order (one-pole/one-zero) Direct-Form-I biquad section; only the
// coefficients change per position, so a position switch is a coefficient
// change on a *running* filter (state preserved), not a topology change —
// this keeps switching click-safe (bounded transient, no discontinuity).
//
// Coefficients are derived (not vendored/borrowed) from the ADR 0026 HPF
// calibration targets; full derivation + magnitude-response tables in
// specs/notes/juno106-hpf-analysis.md.
//
// ADR 0012: RV32F has no hardware flush-to-zero. A tiny +1e-20f bias is
// injected into the filter input on every process() call (all positions,
// including bypass) to keep the feedback state off the denormal floor.
#pragma once

namespace dsp {

enum Juno106HpfPosition {
    JUNO106_HPF_BASS_BOOST = 0,  // low-shelf, ~+3 dB at 70 Hz, flat above
    JUNO106_HPF_BYPASS     = 1,  // flat unity
    JUNO106_HPF_225HZ      = 2,  // first-order HPF, corner ~225 Hz
    JUNO106_HPF_700HZ      = 3,  // first-order HPF, corner ~700 Hz
};

class Juno106Hpf {
public:
    void init(float sample_rate) {
        sample_rate_ = sample_rate;
        x1_          = 0.0f;
        y1_          = 0.0f;
        set_position(JUNO106_HPF_BYPASS);
    }

    // Recomputes coefficients for the given position at the current sample
    // rate. State (x1_, y1_) is left untouched so switching positions on a
    // running filter is a coefficient change, not a reset.
    void set_position(Juno106HpfPosition pos) {
        position_ = pos;
        compute_coeffs(pos);
    }

    Juno106HpfPosition position() const { return position_; }

    // Single-sample process (block callers loop this — matches dsp/filter.h
    // and dsp/dcblock.h's per-sample style).
    float process(float in) {
        // ADR 0012 anti-denormal bias: inhabits every position, including
        // bypass, so state never settles into a denormal on silence.
        float x0 = in + 1e-20f;
        float y0 = b0_ * x0 + b1_ * x1_ - a1_ * y1_;
        x1_      = x0;
        y1_      = y0;
        return y0;
    }

private:
    void compute_coeffs(Juno106HpfPosition pos) {
        switch (pos) {
            case JUNO106_HPF_BYPASS:
                b0_ = 1.0f;
                b1_ = 0.0f;
                a1_ = 0.0f;
                break;
            case JUNO106_HPF_225HZ:
                hpf_coeffs(225.0f);
                break;
            case JUNO106_HPF_700HZ:
                hpf_coeffs(700.0f);
                break;
            case JUNO106_HPF_BASS_BOOST:
            default:
                shelf_coeffs(70.0f, 3.0f);
                break;
        }
    }

    // First-order high-pass, prewarped bilinear transform. See
    // specs/notes/juno106-hpf-analysis.md for the full derivation.
    void hpf_coeffs(float fc);

    // First-order low-shelf bass boost, corner == probe frequency; DC/low-freq
    // asymptotic gain solved so the analytic magnitude AT fc hits gain_db_at_fc
    // exactly. See specs/notes/juno106-hpf-analysis.md.
    void shelf_coeffs(float fc, float gain_db_at_fc);

    float              sample_rate_ = 48000.0f;
    Juno106HpfPosition position_    = JUNO106_HPF_BYPASS;

    // Direct-Form-I coefficients: y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1].
    float b0_ = 1.0f;
    float b1_ = 0.0f;
    float a1_ = 0.0f;

    // Filter state, shared across all positions.
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

}  // namespace dsp
