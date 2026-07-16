// engine/record_ring.cpp — bounded final-master PCM handoff (ADR 0024).
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

#include <math.h>
#include <atomic>
#include "record_ring.h"
#include "spsc_ring.h"

namespace {

static constexpr size_t kRecordRingCapacity = 256;

static SpscRing<RecordBlock, kRecordRingCapacity> s_blocks;
static std::atomic<bool>                          s_enabled{false};
static std::atomic<uint32_t>                      s_dropped_blocks{0};

IRAM_ATTR int16_t to_pcm16(float value) {
    // Match platform/device/platform_device.c::to_i16 exactly: non-finite
    // becomes silence, finite input clamps to [-1, 1], then truncates.
    if (!isfinite(value)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    return (int16_t)(value * 32767.0f);
}

}  // namespace

IRAM_ATTR bool record_ring_publish(const float* left, const float* right, size_t frame_count) {
    if (!s_enabled.load(std::memory_order_relaxed)) return true;

    if (frame_count == 0 || frame_count > kRecordBlockFrames) {
        s_dropped_blocks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    RecordBlock block;
    block.frame_count = (uint16_t)frame_count;
    for (size_t i = 0; i < frame_count; ++i) {
        block.samples[i * 2]     = to_pcm16(left[i]);
        block.samples[i * 2 + 1] = to_pcm16(right[i]);
    }
    if (!s_blocks.push(block)) {
        s_dropped_blocks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void record_ring_set_enabled(bool enabled) {
    s_enabled.store(enabled, std::memory_order_relaxed);
}

bool record_ring_enabled() {
    return s_enabled.load(std::memory_order_relaxed);
}

bool record_ring_pop(RecordBlock& out) {
    return s_blocks.pop(out);
}

void record_ring_clear() {
    RecordBlock discarded;
    while (s_blocks.pop(discarded)) {
    }
}

uint32_t record_ring_dropped_blocks() {
    return s_dropped_blocks.load(std::memory_order_relaxed);
}

void record_ring_reset_dropped_blocks() {
    (void)s_dropped_blocks.exchange(0, std::memory_order_relaxed);
}
