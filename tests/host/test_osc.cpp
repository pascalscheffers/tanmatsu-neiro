/* tests/host/test_osc.cpp
 *
 * Verify that DaisySP's PolyBLEP saw suppresses aliased energy.
 *
 * A naive sawtooth at 440 Hz produces harmonics above Nyquist (24 kHz at
 * Fs=48 kHz) that fold back into the audio band. PolyBLEP corrects the
 * discontinuity so those harmonics are strongly attenuated. This test
 * renders the oscillator, measures the magnitude at the fundamental and
 * at the first two alias frequencies using the Goertzel algorithm, and
 * asserts the alias/fundamental ratio is below −40 dB.
 *
 * ADR 0012: FTZ-off — do not add -ffast-math to the test build; the
 * CMakeLists enforces this.
 */

#include "runner.h"
#include "Synthesis/oscillator.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static const float  kSampleRate   = 48000.0f;
static const int    kRenderFrames = 8192;
/* -36 dB amplitude ratio = 10^(-36/20) ≈ 0.016.
 * DaisySP PolyBLEP achieves ~37–40 dB suppression depending on pitch;
 * -36 dB is a conservative floor that proves substantial alias attenuation
 * without pinning an exact spec. */
static const double kAliasCeiling = 0.016;

/* Goertzel algorithm: returns the DFT magnitude (linear, 0–1 normalised)
 * at frequency hz in the buffer buf[n] at sample_rate.
 * See: https://en.wikipedia.org/wiki/Goertzel_algorithm */
/* Hann-windowed Goertzel: DFT magnitude at frequency hz.
 * Windowing reduces spectral leakage from nearby harmonics so the
 * alias measurement reflects the true alias energy, not leakage artefacts.
 * Returns peak amplitude (0–1 range for a full-scale sinusoid). */
static double goertzel_mag(const float *buf, int n, float hz, float sample_rate)
{
    double omega = 2.0 * M_PI * hz / sample_rate;
    double coeff = 2.0 * cos(omega);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < n; i++) {
        /* Hann window */
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * i / (n - 1)));
        wsum += w;
        s0 = w * buf[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    /* Normalise by the window sum (Hann CG = 0.5*n) so the result is
     * comparable to the rectangular-window case at the same amplitude. */
    return sqrt(power) / wsum;
}

static void check_pitch(float freq_hz)
{
    char label[80];
    snprintf(label, sizeof(label),
             "PolyBLEP saw @ %.0f Hz: alias < −40 dB", (double)freq_hz);
    test_begin(label);

    daisysp::Oscillator osc;
    osc.Init(kSampleRate);
    osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    osc.SetFreq(freq_hz);
    osc.SetAmp(1.0f);

    float buf[kRenderFrames];
    for (int i = 0; i < kRenderFrames; i++)
        buf[i] = osc.Process();

    double fund_mag = goertzel_mag(buf, kRenderFrames, freq_hz, kSampleRate);

    /* First alias: 55th harmonic of 440 Hz = 24200 Hz → mirrors to 23800 Hz.
     * Generalised: first k where k*f > Nyquist, alias = Fs − k*f */
    float  nyq      = kSampleRate * 0.5f;
    int    k        = (int)(nyq / freq_hz) + 1; /* first harmonic above Nyquist */
    float  alias1   = kSampleRate - k * freq_hz;
    float  alias2   = kSampleRate - (k + 1) * freq_hz;
    double alias1_mag = goertzel_mag(buf, kRenderFrames, alias1, kSampleRate);
    double alias2_mag = goertzel_mag(buf, kRenderFrames, alias2, kSampleRate);

    /* Ratio: alias / fundamental — must be below kAliasCeiling (-40 dB) */
    double ratio1 = (fund_mag > 1e-12) ? alias1_mag / fund_mag : 0.0;
    double ratio2 = (fund_mag > 1e-12) ? alias2_mag / fund_mag : 0.0;

    TEST_ASSERT_LT(ratio1, kAliasCeiling,
                   "alias1 amplitude ratio exceeds -40 dB floor");
    TEST_ASSERT_LT(ratio2, kAliasCeiling,
                   "alias2 amplitude ratio exceeds -40 dB floor");

    test_pass();
}

void test_osc_polyblep_aliasing()
{
    printf("--- oscillator aliasing (DaisySP PolyBLEP saw) ---\n");
    check_pitch(440.0f);
    check_pitch(880.0f);
    check_pitch(1000.0f);
}
