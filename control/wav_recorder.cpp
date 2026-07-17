// control/wav_recorder.cpp — recoverable PCM WAV drain on the control thread.
#include "wav_recorder.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "engine/record_ring.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kSampleRate       = 48000;
constexpr uint32_t kBytesPerFrame    = 4;
constexpr uint32_t kMaxDataBytes     = UINT32_MAX - 36u;
constexpr uint64_t kCheckpointMillis = 1000;
constexpr size_t   kPathCapacity     = 512;

FILE*                s_file;
uint32_t             s_data_bytes;
uint64_t             s_checkpoint_millis;
wav_recorder_state_t s_state = WAV_RECORDER_IDLE;
wav_recorder_error_t s_error = WAV_RECORDER_ERROR_NONE;

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

void close_file() {
    if (s_file != nullptr) {
        fclose(s_file);
        s_file = nullptr;
    }
}

bool drain_ring(wav_recorder_error_t& error) {
    RecordBlock block;
    while (record_ring_pop(block)) {
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
    }
    return true;
}

void finish(wav_recorder_error_t error, bool drain) {
    record_ring_set_enabled(false);
    if (s_file != nullptr) {
        wav_recorder_error_t drain_error = WAV_RECORDER_ERROR_NONE;
        if (drain && !drain_ring(drain_error) && error == WAV_RECORDER_ERROR_NONE) {
            error = drain_error;
        }
        if (!checkpoint() && error == WAV_RECORDER_ERROR_NONE) {
            error = WAV_RECORDER_ERROR_WRITE;
        }
        close_file();
    }
    s_error = error;
    s_state = error == WAV_RECORDER_ERROR_NONE ? WAV_RECORDER_IDLE : WAV_RECORDER_ERROR;
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
    while (const dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (strlen(name) == 11 && memcmp(name, "rec", 3) == 0 && memcmp(name + 7, ".wav", 4) == 0 && name[3] >= '0' &&
            name[3] <= '9' && name[4] >= '0' && name[4] <= '9' && name[5] >= '0' && name[5] <= '9' && name[6] >= '0' &&
            name[6] <= '9') {
            const int number  = (name[3] - '0') * 1000 + (name[4] - '0') * 100 + (name[5] - '0') * 10 + (name[6] - '0');
            used[number / 8] |= (uint8_t)(1u << (number % 8));
        }
    }
    closedir(dir);
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
        s_state = WAV_RECORDER_ERROR;
        s_error = WAV_RECORDER_ERROR_SD_UNAVAILABLE;
        return;
    }
    char path[kPathCapacity];
    if (!make_recording_path(path, error)) {
        s_state = WAV_RECORDER_ERROR;
        s_error = error;
        return;
    }
    s_file = fopen(path, "wb");
    if (s_file == nullptr) {
        s_state = WAV_RECORDER_ERROR;
        s_error = WAV_RECORDER_ERROR_OPEN;
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
    s_error             = WAV_RECORDER_ERROR_NONE;
    s_state             = WAV_RECORDER_RECORDING;
    record_ring_set_enabled(false);
    record_ring_clear();
    record_ring_reset_dropped_blocks();
    record_ring_set_enabled(true);
}

}  // namespace

extern "C" void wav_recorder_service(bool want_record) {
    if (s_state == WAV_RECORDER_ERROR) {
        if (!want_record) {
            s_error = WAV_RECORDER_ERROR_NONE;
            s_state = WAV_RECORDER_IDLE;
        }
        return;
    }
    if (s_state == WAV_RECORDER_IDLE) {
        if (want_record) {
            start();
        }
        return;
    }
    if (!want_record) {
        finish(WAV_RECORDER_ERROR_NONE, true);
        return;
    }
    if (!platform_sd_available()) {
        finish(WAV_RECORDER_ERROR_SD_UNAVAILABLE, false);
        return;
    }
    if (record_ring_dropped_blocks() != 0) {
        finish(WAV_RECORDER_ERROR_RING_OVERFLOW, true);
        return;
    }
    wav_recorder_error_t error = WAV_RECORDER_ERROR_NONE;
    if (!drain_ring(error)) {
        finish(error, false);
        return;
    }
    const uint64_t now = platform_millis();
    if (now - s_checkpoint_millis >= kCheckpointMillis) {
        if (!checkpoint()) {
            finish(WAV_RECORDER_ERROR_WRITE, false);
            return;
        }
        s_checkpoint_millis = now;
    }
}

extern "C" wav_recorder_state_t wav_recorder_state(void) {
    return s_state;
}

extern "C" wav_recorder_error_t wav_recorder_error(void) {
    return s_error;
}
