// synth.c — Stage 0 sine engine.
//
// "Hello audio": a single steady sine, low amplitude, identical on both
// channels. Its only job is to prove that synth_render is driven correctly by
// the HAL audio sink on host and device from the same code. Real DSP (the Juno
// macro-osc voice) replaces this in Stage 1.
#include "synth.h"
#include <math.h>

#define TWO_PI 6.28318530717958647692f

// Stage 0 tone: A3 (220 Hz), gentle level to be obviously audible but not harsh.
#define TONE_HZ        220.0f
#define TONE_AMPLITUDE 0.20f

static float s_phase     = 0.0f;  // current oscillator phase, radians [0, 2pi)
static float s_phase_inc = 0.0f;  // radians per sample at the configured rate

void synth_init(uint32_t sample_rate) {
    s_phase     = 0.0f;
    s_phase_inc = TWO_PI * TONE_HZ / (float)sample_rate;
}

void synth_render(float* left, float* right, size_t n, void* user) {
    (void)user;
    for (size_t i = 0; i < n; i++) {
        float s  = TONE_AMPLITUDE * sinf(s_phase);
        left[i]  = s;
        right[i] = s;

        s_phase += s_phase_inc;
        if (s_phase >= TWO_PI) {
            s_phase -= TWO_PI;
        }
    }
}
