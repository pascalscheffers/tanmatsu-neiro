// tests/host/test_record_ring.cpp — final-master PCM handoff tests.
#include <math.h>
#include "engine/record_ring.h"
#include "runner.h"

namespace {

void reset_ring() {
    record_ring_set_enabled(false);
    record_ring_clear();
    record_ring_reset_dropped_blocks();
}

void fill(float* left, float* right, size_t frames, float base) {
    for (size_t i = 0; i < frames; ++i) {
        left[i]  = base + (float)i / 1000.0f;
        right[i] = -(base + (float)i / 1000.0f);
    }
}

}  // namespace

void test_record_ring_suite() {
    printf("--- RecordRing (audio -> control SPSC) ---\n");

    {
        test_begin("ordered variable-size round trip");
        reset_ring();
        record_ring_set_enabled(true);
        const size_t sizes[] = {1, 3, kRecordBlockFrames};
        float        left[kRecordBlockFrames];
        float        right[kRecordBlockFrames];
        for (size_t n : sizes) {
            fill(left, right, n, (float)n / 100.0f);
            TEST_ASSERT(record_ring_publish(left, right, n), "publish");
        }
        for (size_t n : sizes) {
            RecordBlock out;
            TEST_ASSERT(record_ring_pop(out), "pop");
            TEST_ASSERT(out.frame_count == n, "frame count preserved");
            fill(left, right, n, (float)n / 100.0f);
            for (size_t i = 0; i < n; ++i) {
                TEST_ASSERT(out.samples[i * 2] == (int16_t)(left[i] * 32767.0f), "left order/data");
                TEST_ASSERT(out.samples[i * 2 + 1] == (int16_t)(right[i] * 32767.0f), "right order/data");
            }
        }
        RecordBlock out;
        TEST_ASSERT(!record_ring_pop(out), "empty after drain");
        test_pass();
    }

    {
        test_begin("PCM conversion finite/clamp/truncate edges");
        reset_ring();
        record_ring_set_enabled(true);
        float left[]  = {NAN, INFINITY, -INFINITY, 1.0f, -1.0f, 1.25f, -1.25f, 0.5f, -0.5f};
        float right[] = {-0.0f, 0.0f, NAN, -INFINITY, INFINITY, 0.99999f, -0.99999f, 0.1f, -0.1f};
        TEST_ASSERT(record_ring_publish(left, right, 9), "publish edges");
        RecordBlock out;
        TEST_ASSERT(record_ring_pop(out), "pop edges");
        const int16_t want_left[]  = {0, 0, 0, 32767, -32767, 32767, -32767, 16383, -16383};
        const int16_t want_right[] = {0, 0, 0, 0, 0, 32766, -32766, 3276, -3276};
        for (size_t i = 0; i < 9; ++i) {
            TEST_ASSERT(out.samples[i * 2] == want_left[i], "left conversion");
            TEST_ASSERT(out.samples[i * 2 + 1] == want_right[i], "right conversion");
        }
        test_pass();
    }

    {
        test_begin("disabled publish is a no-op");
        reset_ring();
        float left[1] = {0.5f}, right[1] = {-0.5f};
        TEST_ASSERT(record_ring_publish(left, right, 1), "disabled no-op succeeds");
        TEST_ASSERT(record_ring_publish(left, right, kRecordBlockFrames + 1), "disabled oversize is no-op");
        RecordBlock out;
        TEST_ASSERT(!record_ring_pop(out), "disabled ring stays empty");
        TEST_ASSERT(record_ring_dropped_blocks() == 0, "disabled no drops");
        test_pass();
    }

    {
        test_begin("capacity drops newest and oversize fails closed");
        reset_ring();
        record_ring_set_enabled(true);
        float left[kRecordBlockFrames]  = {};
        float right[kRecordBlockFrames] = {};
        for (size_t i = 0; i < 255; ++i) {
            left[0] = (float)i / 1000.0f;
            TEST_ASSERT(record_ring_publish(left, right, 1), "usable ring slot");
        }
        left[0] = 0.9f;
        TEST_ASSERT(!record_ring_publish(left, right, 1), "full drops newest");
        TEST_ASSERT(!record_ring_publish(left, right, kRecordBlockFrames + 1), "oversize rejected");
        TEST_ASSERT(record_ring_dropped_blocks() == 2, "both errors counted");

        RecordBlock out;
        for (size_t i = 0; i < 255; ++i) {
            TEST_ASSERT(record_ring_pop(out), "pop queued prefix");
            TEST_ASSERT(out.samples[0] == (int16_t)(((float)i / 1000.0f) * 32767.0f), "FIFO prefix intact");
        }
        TEST_ASSERT(!record_ring_pop(out), "drop-newest was not queued");
        record_ring_reset_dropped_blocks();
        TEST_ASSERT(record_ring_dropped_blocks() == 0, "counter reset");
        test_pass();
    }

    reset_ring();
}
