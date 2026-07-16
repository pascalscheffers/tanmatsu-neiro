// engine/record_ring.h — final-master PCM handoff (audio -> control thread).
#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t kRecordBlockFrames = 64;

struct RecordBlock {
    uint16_t frame_count;
    int16_t  samples[kRecordBlockFrames * 2];  // interleaved L, R
};

// Audio-thread producer. Disabled capture is a successful no-op. Enabled
// publishes outside 1..kRecordBlockFrames fail closed and count as drops.
bool record_ring_publish(const float* left, const float* right, size_t frame_count);

// Control-thread API. clear() and pop() must only be called by the one consumer.
void     record_ring_set_enabled(bool enabled);
bool     record_ring_enabled();
bool     record_ring_pop(RecordBlock& out);
void     record_ring_clear();
uint32_t record_ring_dropped_blocks();
void     record_ring_reset_dropped_blocks();
