// tests/host/test_audio_volume.cpp — listening-volume taper regression tests.
#include <math.h>
#include "platform/audio_volume.h"
#include "runner.h"

static void test_volume_gain_curve(void) {
    test_begin("volume: logical percent uses square-law gain");
    TEST_ASSERT(platform_volume_gain(0) == 0.0f, "zero must be silent");
    TEST_ASSERT(fabsf(platform_volume_gain(25) - 0.0625f) < 1e-6f, "25% must map to 0.0625 gain");
    TEST_ASSERT(fabsf(platform_volume_gain(50) - 0.25f) < 1e-6f, "50% must map to 0.25 gain");
    TEST_ASSERT(fabsf(platform_volume_gain(70) - 0.49f) < 1e-6f, "70% must map to 0.49 gain");
    TEST_ASSERT(platform_volume_gain(100) == 1.0f, "100% must be unity");
    TEST_ASSERT(platform_volume_gain(101) == 1.0f, "out-of-range input must clamp");
    test_pass();
}

static void test_codec_curve(void) {
    test_begin("volume: codec mapping follows square-law taper");
    TEST_ASSERT(platform_volume_codec_pct(0) == 0.0f, "zero must select maximum codec attenuation");
    TEST_ASSERT(fabsf(platform_volume_codec_pct(25) - 63.24f) < 0.02f, "25% codec mapping drifted");
    TEST_ASSERT(fabsf(platform_volume_codec_pct(50) - 76.62f) < 0.02f, "50% codec mapping drifted");
    TEST_ASSERT(fabsf(platform_volume_codec_pct(70) - 83.12f) < 0.02f, "70% codec mapping drifted");
    TEST_ASSERT(platform_volume_codec_pct(100) == 90.0f, "100% must retain the safe codec ceiling");

    float previous = platform_volume_codec_pct(0);
    for (uint32_t pct = 1; pct <= 100; ++pct) {
        float current = platform_volume_codec_pct(pct);
        TEST_ASSERT(current >= previous, "codec curve must be monotonic");
        previous = current;
    }
    test_pass();
}

void test_audio_volume_suite(void) {
    printf("--- platform/audio_volume ---\n");
    test_volume_gain_curve();
    test_codec_curve();
}
