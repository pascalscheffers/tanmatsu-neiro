// test_dcblock.cpp — host tests for dsp/dcblock.h (master-bus DC blocker).
// Verifies: DC is removed (output mean → 0), passband is ~unity, an offset
// sine is re-centred, and the feedback state stays finite on silence.
#include <math.h>
#include "dsp/dcblock.h"
#include "runner.h"

static const float kFs = 48000.0f;

// Removing a constant offset: after the filter settles, output mean ≈ 0.
static void test_dcblock_removes_dc(void) {
    test_begin("dcblock: constant offset → output mean ~0");
    dsp::DcBlock dc;
    dc.init(kFs);
    // Settle, then measure mean over a window.
    for (int i = 0; i < 48000; i++) dc.process(0.7f);
    double sum = 0.0;
    for (int i = 0; i < 48000; i++) sum += dc.process(0.7f);
    float mean = (float)(sum / 48000.0);
    TEST_ASSERT(fabsf(mean) < 1e-3f, "DC not removed (mean not ~0)");
    test_pass();
}

// A mid-frequency sine (well above the ~1.6 Hz corner) passes near unity gain.
static void test_dcblock_passband_unity(void) {
    test_begin("dcblock: 1 kHz sine passes ~unity");
    dsp::DcBlock dc;
    dc.init(kFs);
    const float w = 2.0f * (float)M_PI * 1000.0f / kFs;
    // Settle transient.
    for (int i = 0; i < 4800; i++) dc.process(sinf(w * (float)i));
    float peak = 0.0f;
    for (int i = 4800; i < 4800 + 4800; i++) {
        float y = dc.process(sinf(w * (float)i));
        if (fabsf(y) > peak) peak = fabsf(y);
    }
    TEST_ASSERT(peak > 0.99f && peak < 1.01f, "passband gain not ~unity");
    test_pass();
}

// Sine + DC offset in → the recovered sine is centred (mean ~0, amplitude kept).
static void test_dcblock_recenters_offset_sine(void) {
    test_begin("dcblock: offset sine → re-centred");
    dsp::DcBlock dc;
    dc.init(kFs);
    const float w      = 2.0f * (float)M_PI * 500.0f / kFs;
    const float offset = -0.224f;  // the bottom-heavy bias from crackle forensics
    // Settle ~10 time constants (corner ~1.6 Hz, tau ~0.1 s) so the offset-step
    // transient has fully decayed before measuring the mean.
    for (int i = 0; i < 48000; i++) dc.process(sinf(w * (float)i) + offset);
    double sum = 0.0;
    float  pk  = 0.0f;
    for (int i = 48000; i < 48000 + 9600; i++) {
        float y = dc.process(sinf(w * (float)i) + offset);
        sum    += y;
        if (fabsf(y) > pk) pk = fabsf(y);
    }
    float mean = (float)(sum / 9600.0);
    TEST_ASSERT(fabsf(mean) < 1e-3f, "offset sine not re-centred");
    TEST_ASSERT(pk > 0.98f && pk < 1.02f, "sine amplitude altered");
    test_pass();
}

// Silence must not produce NaN/Inf (feedback state stays finite; denormal guard).
static void test_dcblock_silence_finite(void) {
    test_begin("dcblock: silence stays finite");
    dsp::DcBlock dc;
    dc.init(kFs);
    dc.process(1.0f);  // kick the feedback, then decay to silence
    float y = 0.0f;
    for (int i = 0; i < 200000; i++) y = dc.process(0.0f);
    TEST_ASSERT(isfinite(y), "DC blocker produced non-finite output on silence");
    test_pass();
}

void test_dcblock_suite(void) {
    printf("--- dsp/dcblock.h ---\n");
    test_dcblock_removes_dc();
    test_dcblock_passband_unity();
    test_dcblock_recenters_offset_sine();
    test_dcblock_silence_finite();
}
