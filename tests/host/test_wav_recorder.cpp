// tests/host/test_wav_recorder.cpp — recoverable SD WAV writer tests.
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "control/wav_recorder.h"
#include "engine/record_ring.h"
#include "runner.h"

namespace {

std::string s_root;
bool        s_available;
uint64_t    s_millis;

uint32_t le32(const std::vector<uint8_t>& bytes, size_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) | ((uint32_t)bytes[offset + 2] << 16) |
           ((uint32_t)bytes[offset + 3] << 24);
}

std::vector<uint8_t> read_file(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    TEST_ASSERT(file != nullptr, "open output");
    TEST_ASSERT(fseek(file, 0, SEEK_END) == 0, "seek output end");
    const long size = ftell(file);
    TEST_ASSERT(size >= 0 && fseek(file, 0, SEEK_SET) == 0, "measure output");
    std::vector<uint8_t> bytes((size_t)size);
    TEST_ASSERT(bytes.empty() || fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read output");
    fclose(file);
    return bytes;
}

void remove_tree(const std::string& root) {
    const std::string recordings = root + "/recordings";
    if (DIR* dir = opendir(recordings.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            if (entry->d_name[0] != '.') {
                unlink((recordings + "/" + entry->d_name).c_str());
            }
        }
        closedir(dir);
        rmdir(recordings.c_str());
    }
    rmdir(root.c_str());
}

void reset(const char* suffix) {
    wav_recorder_service(false);
    record_ring_set_enabled(false);
    record_ring_clear();
    record_ring_reset_dropped_blocks();
    char path[160];
    snprintf(path, sizeof(path), "/tmp/tanmatsu-wav-%ld-%s-XXXXXX", (long)getpid(), suffix);
    TEST_ASSERT(mkdtemp(path) != nullptr, "make temporary SD root");
    s_root      = path;
    s_available = true;
    s_millis    = 0;
}

void publish(size_t frames, float base = 0.1f) {
    float left[kRecordBlockFrames];
    float right[kRecordBlockFrames];
    for (size_t i = 0; i < frames; ++i) {
        left[i]  = base + (float)i / 1000.0f;
        right[i] = -(base + (float)i / 1000.0f);
    }
    TEST_ASSERT(record_ring_publish(left, right, frames), "publish synthetic block");
}

void assert_header(const std::vector<uint8_t>& bytes, uint32_t data_bytes) {
    TEST_ASSERT(bytes.size() == 44u + data_bytes, "file length");
    TEST_ASSERT(memcmp(bytes.data(), "RIFF", 4) == 0, "RIFF id");
    TEST_ASSERT(le32(bytes, 4) == 36u + data_bytes, "RIFF size");
    TEST_ASSERT(memcmp(bytes.data() + 8, "WAVEfmt ", 8) == 0, "WAVE/fmt ids");
    TEST_ASSERT(le32(bytes, 16) == 16, "PCM fmt size");
    TEST_ASSERT(bytes[20] == 1 && bytes[21] == 0, "PCM format");
    TEST_ASSERT(bytes[22] == 2 && bytes[23] == 0, "stereo");
    TEST_ASSERT(le32(bytes, 24) == 48000, "sample rate");
    TEST_ASSERT(le32(bytes, 28) == 192000, "byte rate");
    TEST_ASSERT(bytes[32] == 4 && bytes[33] == 0, "block align");
    TEST_ASSERT(bytes[34] == 16 && bytes[35] == 0, "sample bits");
    TEST_ASSERT(memcmp(bytes.data() + 36, "data", 4) == 0, "data id");
    TEST_ASSERT(le32(bytes, 40) == data_bytes, "data size");
}

}  // namespace

extern "C" bool platform_sd_available(void) {
    return s_available;
}

extern "C" const char* platform_sd_root(void) {
    return s_root.c_str();
}

extern "C" uint64_t platform_millis(void) {
    return s_millis;
}

void test_wav_recorder_suite() {
    printf("--- WavRecorder (recoverable SD PCM writer) ---\n");

    {
        test_begin("exact header, payload, clean stop, no overwrite");
        reset("clean");
        const std::string recordings = s_root + "/recordings";
        TEST_ASSERT(mkdir(recordings.c_str(), 0777) == 0, "precreate recordings");
        const std::string first = recordings + "/rec0001.wav";
        FILE*             old   = fopen(first.c_str(), "wb");
        TEST_ASSERT(old != nullptr && fwrite("old", 1, 3, old) == 3, "precreate old take");
        fclose(old);

        wav_recorder_service(true);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_RECORDING, "recording started");
        publish(3, 0.25f);
        wav_recorder_service(true);
        wav_recorder_service(false);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_IDLE, "clean stop is idle");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_NONE, "clean stop has no error");

        const std::vector<uint8_t> bytes = read_file(recordings + "/rec0002.wav");
        assert_header(bytes, 12);
        TEST_ASSERT(bytes[44] == 255 && bytes[45] == 31, "left PCM little endian");
        TEST_ASSERT(bytes[46] == 1 && bytes[47] == 224, "right PCM little endian");
        TEST_ASSERT(read_file(first).size() == 3, "existing take untouched");
        remove_tree(s_root);
        test_pass();
    }

    {
        test_begin("one-second checkpoint patches recoverable sizes");
        reset("checkpoint");
        wav_recorder_service(true);
        publish(4);
        s_millis = 999;
        wav_recorder_service(true);
        s_millis = 1000;
        wav_recorder_service(true);
        const std::vector<uint8_t> bytes = read_file(s_root + "/recordings/rec0001.wav");
        assert_header(bytes, 16);
        wav_recorder_service(false);
        remove_tree(s_root);
        test_pass();
    }

    {
        test_begin("overflow stops and finalizes queued prefix");
        reset("overflow");
        wav_recorder_service(true);
        float left[1] = {}, right[1] = {};
        for (size_t i = 0; i < 255; ++i) {
            TEST_ASSERT(record_ring_publish(left, right, 1), "fill ring");
        }
        TEST_ASSERT(!record_ring_publish(left, right, 1), "overflow ring");
        wav_recorder_service(true);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_ERROR, "overflow enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_RING_OVERFLOW, "overflow reason");
        assert_header(read_file(s_root + "/recordings/rec0001.wav"), 255 * 4);
        wav_recorder_service(false);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_IDLE, "release clears error state");
        remove_tree(s_root);
        test_pass();
    }

    {
        test_begin("card loss stops with visible error");
        reset("card-loss");
        wav_recorder_service(true);
        publish(2);
        s_available = false;
        wav_recorder_service(true);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_ERROR, "card loss enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_SD_UNAVAILABLE, "card loss reason");
        assert_header(read_file(s_root + "/recordings/rec0001.wav"), 0);
        wav_recorder_service(false);
        remove_tree(s_root);
        test_pass();
    }

    {
        test_begin("real filesystem write failure is latched");
        reset("write-failure");
        wav_recorder_service(true);
        publish(2);
        struct rlimit old_limit;
        TEST_ASSERT(getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "read file-size limit");
        struct rlimit limit     = old_limit;
        limit.rlim_cur          = 44;
        void (*old_signal)(int) = signal(SIGXFSZ, SIG_IGN);
        TEST_ASSERT(setrlimit(RLIMIT_FSIZE, &limit) == 0, "set file-size limit");
        wav_recorder_service(true);
        s_millis = 1000;
        wav_recorder_service(true);
        TEST_ASSERT(setrlimit(RLIMIT_FSIZE, &old_limit) == 0, "restore file-size limit");
        signal(SIGXFSZ, old_signal);
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_ERROR, "write failure enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_WRITE, "write failure reason");
        wav_recorder_service(false);
        remove_tree(s_root);
        test_pass();
    }

    record_ring_set_enabled(false);
}
