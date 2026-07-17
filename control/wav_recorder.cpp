// control/wav_recorder.cpp — recoverable PCM WAV drain on the storage worker.
#include "wav_recorder.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <atomic>
#include "engine/record_ring.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kSampleRate       = 48000;
constexpr uint32_t kBytesPerFrame    = 4;
constexpr uint32_t kMaxDataBytes     = UINT32_MAX - 36u;
constexpr uint64_t kCheckpointMillis = 1000;
constexpr size_t   kPathCapacity     = 512;
constexpr size_t   kDrainBatchBlocks = 8;
constexpr uint32_t kWorkerSleepMs    = 1;

FILE*                             s_file;
uint32_t                          s_data_bytes;
uint64_t                          s_checkpoint_millis;
std::atomic<bool>                 s_want_record{false};
std::atomic<bool>                 s_shutdown_requested{false};
std::atomic<bool>                 s_worker_started{false};
std::atomic<wav_recorder_state_t> s_state{WAV_RECORDER_IDLE};
std::atomic<wav_recorder_error_t> s_error{WAV_RECORDER_ERROR_NONE};

void put_le16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

void put_le32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

void make_header(uint8_t (&header)[44], uint32_t data_bytes) {
    memset(header, 0, sizeof(header));
    memcpy(header + 0, "RIFF", 4);
    put_le32(header + 4, 36u + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_le32(header + 16, 16);
    put_le16(header + 20, 1);
    put_le16(header + 22, 2);
    put_le32(header + 24, kSampleRate);
    put_le32(header + 28, kSampleRate * kBytesPerFrame);
    put_le16(header + 32, kBytesPerFrame);
    put_le16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put_le32(header + 40, data_bytes);
}

bool patch_header() {
    uint8_t field[4];
    put_le32(field, 36u + s_data_bytes);
    if (fseek(s_file, 4, SEEK_SET) != 0 || fwrite(field, 1, sizeof(field), s_file) != sizeof(field)) {
        return false;
    }
    put_le32(field, s_data_bytes);
    if (fseek(s_file, 40, SEEK_SET) != 0 || fwrite(field, 1, sizeof(field), s_file) != sizeof(field)) {
        return false;
    }
    return fseek(s_file, 0, SEEK_END) == 0;
}

bool checkpoint() {
    return patch_header() && fflush(s_file) == 0;
}

bool close_file() {
    if (s_file == nullptr) return true;
    FILE* file = s_file;
    s_file     = nullptr;
    return fclose(file) == 0;
}

bool drain_ring_batch(wav_recorder_error_t& error, bool& caught_up) {
    RecordBlock block;
    size_t      drained = 0;
    while (drained < kDrainBatchBlocks && record_ring_pop(block)) {
        const uint32_t bytes = (uint32_t)block.frame_count * kBytesPerFrame;
        if (bytes > kMaxDataBytes - s_data_bytes) {
            error = WAV_RECORDER_ERROR_SIZE_LIMIT;
            return false;
        }
        uint8_t pcm[kRecordBlockFrames * kBytesPerFrame];
        for (size_t i = 0; i < (size_t)block.frame_count * 2; ++i) {
            put_le16(pcm + i * 2, (uint16_t)block.samples[i]);
        }
        const size_t frames_written = fwrite(pcm, kBytesPerFrame, block.frame_count, s_file);
        s_data_bytes               += (uint32_t)frames_written * kBytesPerFrame;
        if (frames_written != block.frame_count) {
            error = WAV_RECORDER_ERROR_WRITE;
            return false;
        }
        ++drained;
    }
    caught_up = drained < kDrainBatchBlocks;
    return true;
}

void finish(wav_recorder_error_t error, bool drain) {
    record_ring_set_enabled(false);
    if (s_file != nullptr) {
        if (drain) {
            bool caught_up = false;
            while (!caught_up) {
                wav_recorder_error_t drain_error = WAV_RECORDER_ERROR_NONE;
                if (!drain_ring_batch(drain_error, caught_up)) {
                    if (error == WAV_RECORDER_ERROR_NONE) error = drain_error;
                    break;
                }
                if (!caught_up) platform_sleep_ms(kWorkerSleepMs);
            }
        }
        if (!checkpoint() && error == WAV_RECORDER_ERROR_NONE) {
            error = WAV_RECORDER_ERROR_WRITE;
        }
        if (!close_file() && error == WAV_RECORDER_ERROR_NONE) {
            error = WAV_RECORDER_ERROR_WRITE;
        }
    }
    s_error.store(error, std::memory_order_release);
    s_state.store(error == WAV_RECORDER_ERROR_NONE ? WAV_RECORDER_IDLE : WAV_RECORDER_ERROR, std::memory_order_release);
}

bool make_recording_path(char (&path)[kPathCapacity], wav_recorder_error_t& error) {
    const char* root = platform_sd_root();
    if (root == nullptr || root[0] == '\0') {
        error = WAV_RECORDER_ERROR_SD_UNAVAILABLE;
        return false;
    }
    char directory[kPathCapacity];
    if (snprintf(directory, sizeof(directory), "%s/recordings", root) >= (int)sizeof(directory)) {
        error = WAV_RECORDER_ERROR_DIRECTORY;
        return false;
    }
    if (mkdir(directory, 0777) != 0 && errno != EEXIST) {
        error = WAV_RECORDER_ERROR_DIRECTORY;
        return false;
    }
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        error = WAV_RECORDER_ERROR_DIRECTORY;
        return false;
    }
    uint8_t used[(10000 + 7) / 8] = {};
    errno                         = 0;
    while (const dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (strlen(name) == 11 && memcmp(name, "rec", 3) == 0 && memcmp(name + 7, ".wav", 4) == 0 && name[3] >= '0' &&
            name[3] <= '9' && name[4] >= '0' && name[4] <= '9' && name[5] >= '0' && name[5] <= '9' && name[6] >= '0' &&
            name[6] <= '9') {
            const int number  = (name[3] - '0') * 1000 + (name[4] - '0') * 100 + (name[5] - '0') * 10 + (name[6] - '0');
            used[number / 8] |= (uint8_t)(1u << (number % 8));
        }
    }
    const bool directory_ok = errno == 0;
    const bool close_ok     = closedir(dir) == 0;
    if (!directory_ok || !close_ok) {
        error = WAV_RECORDER_ERROR_DIRECTORY;
        return false;
    }
    for (int number = 1; number <= 9999; ++number) {
        if ((used[number / 8] & (uint8_t)(1u << (number % 8))) == 0) {
            if (snprintf(path, sizeof(path), "%s/rec%04d.wav", directory, number) < (int)sizeof(path)) {
                return true;
            }
            error = WAV_RECORDER_ERROR_DIRECTORY;
            return false;
        }
    }
    error = WAV_RECORDER_ERROR_NO_FILENAME;
    return false;
}

void start() {
    wav_recorder_error_t error = WAV_RECORDER_ERROR_NONE;
    if (!platform_sd_available()) {
        s_error.store(WAV_RECORDER_ERROR_SD_UNAVAILABLE, std::memory_order_release);
        s_state.store(WAV_RECORDER_ERROR, std::memory_order_release);
        return;
    }
    char path[kPathCapacity];
    if (!make_recording_path(path, error)) {
        s_error.store(error, std::memory_order_release);
        s_state.store(WAV_RECORDER_ERROR, std::memory_order_release);
        return;
    }
    s_file = fopen(path, "wb");
    if (s_file == nullptr) {
        s_error.store(WAV_RECORDER_ERROR_OPEN, std::memory_order_release);
        s_state.store(WAV_RECORDER_ERROR, std::memory_order_release);
        return;
    }
    uint8_t header[44];
    make_header(header, 0);
    if (fwrite(header, 1, sizeof(header), s_file) != sizeof(header) || fflush(s_file) != 0) {
        finish(WAV_RECORDER_ERROR_WRITE, false);
        return;
    }
    s_data_bytes        = 0;
    s_checkpoint_millis = platform_millis();
    s_error.store(WAV_RECORDER_ERROR_NONE, std::memory_order_release);
    s_state.store(WAV_RECORDER_RECORDING, std::memory_order_release);
    record_ring_set_enabled(false);
    record_ring_clear();
    record_ring_reset_dropped_blocks();
    record_ring_set_enabled(true);
}

void storage_worker(void*) {
    while (true) {
        const bool want_record = s_want_record.load(std::memory_order_acquire);
        const auto state       = s_state.load(std::memory_order_acquire);

        if (state == WAV_RECORDER_ERROR) {
            if (!want_record) {
                s_error.store(WAV_RECORDER_ERROR_NONE, std::memory_order_release);
                s_state.store(WAV_RECORDER_IDLE, std::memory_order_release);
            }
        } else if (state == WAV_RECORDER_IDLE) {
            if (want_record) start();
        } else if (!want_record) {
            finish(WAV_RECORDER_ERROR_NONE, true);
        } else if (!platform_sd_available()) {
            finish(WAV_RECORDER_ERROR_SD_UNAVAILABLE, false);
        } else if (record_ring_dropped_blocks() != 0) {
            finish(WAV_RECORDER_ERROR_RING_OVERFLOW, true);
        } else {
            wav_recorder_error_t error     = WAV_RECORDER_ERROR_NONE;
            bool                 caught_up = false;
            if (!drain_ring_batch(error, caught_up)) {
                finish(error, false);
            } else {
                const uint64_t now = platform_millis();
                if (now - s_checkpoint_millis >= kCheckpointMillis) {
                    if (!checkpoint()) {
                        finish(WAV_RECORDER_ERROR_WRITE, false);
                    } else {
                        s_checkpoint_millis = now;
                    }
                }
            }
        }

        if (s_shutdown_requested.load(std::memory_order_acquire)) {
            s_want_record.store(false, std::memory_order_release);
            if (s_state.load(std::memory_order_acquire) == WAV_RECORDER_RECORDING) {
                finish(WAV_RECORDER_ERROR_NONE, true);
            }
            return;
        }
        platform_sleep_ms(kWorkerSleepMs);
    }
}

}  // namespace

extern "C" bool wav_recorder_init(void) {
    if (s_worker_started.load(std::memory_order_acquire)) {
        return !s_shutdown_requested.load(std::memory_order_acquire);
    }
    s_want_record.store(false, std::memory_order_relaxed);
    s_shutdown_requested.store(false, std::memory_order_relaxed);
    s_error.store(WAV_RECORDER_ERROR_NONE, std::memory_order_relaxed);
    s_state.store(WAV_RECORDER_IDLE, std::memory_order_relaxed);
    if (platform_storage_worker_start(storage_worker, nullptr)) {
        s_worker_started.store(true, std::memory_order_release);
        return true;
    }
    s_error.store(WAV_RECORDER_ERROR_WORKER_START, std::memory_order_release);
    s_state.store(WAV_RECORDER_ERROR, std::memory_order_release);
    return false;
}

extern "C" bool wav_recorder_shutdown(void) {
    if (!s_worker_started.load(std::memory_order_acquire)) return true;
    s_want_record.store(false, std::memory_order_release);
    s_shutdown_requested.store(true, std::memory_order_release);
    if (!platform_storage_worker_stop()) return false;
    s_worker_started.store(false, std::memory_order_release);
    return true;
}

extern "C" void wav_recorder_service(bool want_record) {
    s_want_record.store(want_record, std::memory_order_release);
}

extern "C" wav_recorder_state_t wav_recorder_state(void) {
    return s_state.load(std::memory_order_acquire);
}

extern "C" wav_recorder_error_t wav_recorder_error(void) {
    return s_error.load(std::memory_order_acquire);
}
