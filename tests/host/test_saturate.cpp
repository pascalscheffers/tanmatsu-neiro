// test_saturate.cpp — host tests for dsp/saturate.h (ADR 0016).
#include <math.h>
#include "dsp/saturate.h"
#include "runner.h"

static void test_saturate_transparent(void) {
    test_begin("soft_clip: transparent near zero");
    float x = 0.3f;
    float y = soft_clip(x);
    // y = x - x^3/6.75; for x=0.3: y ≈ 0.296. Within 1.5% of linear.
    TEST_ASSERT(fabsf(y - x) < 0.015f, "clip error > 1.5% at x=0.3");
    TEST_ASSERT(soft_clip(0.0f) == 0.0f, "soft_clip(0) must be 0");
    test_pass();
}

static void test_saturate_monotone(void) {
    test_begin("soft_clip: monotone (non-decreasing)");
    float prev = soft_clip(-2.0f);
    for (int i = -200; i <= 200; i++) {
        float xi = (float)i * 0.01f;
        float yi = soft_clip(xi);
        TEST_ASSERT(yi >= prev - 1e-6f, "soft_clip is not monotone");
        prev = yi;
    }
    test_pass();
}

static void test_saturate_bounded(void) {
    test_begin("soft_clip: output bounded to [-1, 1] for |x| >= 1.5");
    TEST_ASSERT(soft_clip(1.5f) == 1.0f, "soft_clip(1.5) must be 1.0");
    TEST_ASSERT(soft_clip(-1.5f) == -1.0f, "soft_clip(-1.5) must be -1.0");
    TEST_ASSERT(soft_clip(10.0f) == 1.0f, "soft_clip(10) must be 1.0");
    TEST_ASSERT(soft_clip(-10.0f) == -1.0f, "soft_clip(-10) must be -1.0");
    test_pass();
}

static void test_saturate_odd_symmetry(void) {
    test_begin("soft_clip: odd symmetry — soft_clip(-x) == -soft_clip(x)");
    float vals[] = {0.0f, 0.1f, 0.5f, 0.9f, 1.2f, 1.5f, 2.0f};
    for (int i = 0; i < 7; i++) {
        float x  = vals[i];
        float yp = soft_clip(x);
        float yn = soft_clip(-x);
        TEST_ASSERT(fabsf(yn + yp) < 1e-6f, "odd symmetry violated");
    }
    test_pass();
}

void test_saturate_suite(void) {
    printf("--- dsp/saturate.h ---\n");
    test_saturate_transparent();
    test_saturate_monotone();
    test_saturate_bounded();
    test_saturate_odd_symmetry();
}
