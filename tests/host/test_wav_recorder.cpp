// tests/host/test_wav_recorder.cpp — recoverable SD WAV writer tests.
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include "control/wav_recorder.h"
#include "engine/record_ring.h"
#include "runner.h"

namespace {

std::string           s_root;
std::atomic<bool>     s_available;
std::atomic<uint64_t> s_millis;
std::atomic<bool>     s_stall_sd;
std::atomic<bool>     s_sd_call_stalled;
std::atomic<bool>     s_worker_done{true};
bool                  s_worker_start_fails;
std::thread           s_worker;

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
    TEST_ASSERT(wav_recorder_shutdown(), "stop previous worker");
    record_ring_set_enabled(false);
    record_ring_clear();
    record_ring_reset_dropped_blocks();
    char path[160];
    snprintf(path, sizeof(path), "/tmp/tanmatsu-wav-%ld-%s-XXXXXX", (long)getpid(), suffix);
    TEST_ASSERT(mkdtemp(path) != nullptr, "make temporary SD root");
    s_root               = path;
    s_available          = true;
    s_millis             = 0;
    s_stall_sd           = false;
    s_sd_call_stalled    = false;
    s_worker_start_fails = false;
    TEST_ASSERT(wav_recorder_init(), "start recorder worker");
}

template <typename Predicate>
void wait_until(Predicate predicate, const char* message) {
    for (int i = 0; i < 2000; ++i) {
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT(false, message);
}

void wait_for_state(wav_recorder_state_t state, const char* message) {
    wait_until([state]() { return wav_recorder_state() == state; }, message);
}

void start_recording() {
    wav_recorder_service(true);
    wait_for_state(WAV_RECORDER_RECORDING, "recording started");
}

void stop_recording() {
    wav_recorder_service(false);
    wait_for_state(WAV_RECORDER_IDLE, "recording stopped");
}

void finish_test() {
    TEST_ASSERT(wav_recorder_shutdown(), "stop recorder worker");
    remove_tree(s_root);
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
    if (s_stall_sd.load()) {
        s_sd_call_stalled.store(true);
        while (s_stall_sd.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return s_available.load();
}

extern "C" const char* platform_sd_root(void) {
    return s_root.c_str();
}

extern "C" uint64_t platform_millis(void) {
    return s_millis.load();
}

extern "C" void platform_sleep_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

extern "C" bool platform_storage_worker_start(void (*storage_cb)(void*), void* ctx) {
    if (s_worker_start_fails || s_worker.joinable()) return false;
    s_worker_done = false;
    s_worker      = std::thread([storage_cb, ctx]() {
        storage_cb(ctx);
        s_worker_done = true;
    });
    return true;
}

extern "C" bool platform_storage_worker_stop(void) {
    if (!s_worker.joinable()) return s_worker_done.load();
    for (int i = 0; i < 100 && !s_worker_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!s_worker_done.load()) return false;
    s_worker.join();
    return true;
}

void test_wav_recorder_suite() {
    printf("--- WavRecorder (async recoverable SD PCM writer) ---\n");

    {
        test_begin("worker start failure is visible");
        TEST_ASSERT(wav_recorder_shutdown(), "no previous worker");
        s_worker_start_fails = true;
        TEST_ASSERT(!wav_recorder_init(), "worker start fails");
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_ERROR, "start failure enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_WORKER_START, "start failure reason");
        TEST_ASSERT(wav_recorder_shutdown(), "failed start shutdown is harmless");
        s_worker_start_fails = false;
        test_pass();
    }

    {
        test_begin("stalled SD cannot block control service or state reads");
        reset("stalled");
        s_stall_sd = true;
        wav_recorder_service(true);
        wait_until([]() { return s_sd_call_stalled.load(); }, "worker entered stalled SD call");
        const auto begin = std::chrono::steady_clock::now();
        wav_recorder_service(false);
        (void)wav_recorder_state();
        (void)wav_recorder_error();
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        TEST_ASSERT(elapsed < std::chrono::milliseconds(10), "control snapshots return promptly");
        const auto stop_begin = std::chrono::steady_clock::now();
        TEST_ASSERT(!wav_recorder_shutdown(), "stalled worker reports bounded stop timeout");
        const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;
        TEST_ASSERT(stop_elapsed < std::chrono::milliseconds(250), "stop timeout remains bounded");
        s_stall_sd = false;
        wait_until([]() { return s_worker_done.load(); }, "released worker exits");
        TEST_ASSERT(wav_recorder_shutdown(), "released worker joins");
        remove_tree(s_root);
        test_pass();
    }

    {
        test_begin("exact header, payload, clean stop, no overwrite");
        reset("clean");
        const std::string recordings = s_root + "/recordings";
        TEST_ASSERT(mkdir(recordings.c_str(), 0777) == 0, "precreate recordings");
        const std::string first = recordings + "/rec0001.wav";
        FILE*             old   = fopen(first.c_str(), "wb");
        TEST_ASSERT(old != nullptr && fwrite("old", 1, 3, old) == 3, "precreate old take");
        fclose(old);

        start_recording();
        s_stall_sd = true;
        wait_until([]() { return s_sd_call_stalled.load(); }, "worker stalled before packed batch");
        for (size_t block = 0; block < 15; ++block) {
            publish(kRecordBlockFrames, 0.01f + (float)block / 100.0f);
        }
        publish(3, 0.25f);
        s_stall_sd = false;
        stop_recording();
        TEST_ASSERT(wav_recorder_state() == WAV_RECORDER_IDLE, "clean stop is idle");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_NONE, "clean stop has no error");

        const std::vector<uint8_t> bytes       = read_file(recordings + "/rec0002.wav");
        constexpr size_t           full_frames = 15 * kRecordBlockFrames;
        assert_header(bytes, (full_frames + 3) * 4);
        for (size_t frame = 0; frame < full_frames + 3; ++frame) {
            const size_t block       = frame / kRecordBlockFrames;
            const size_t block_frame = frame % kRecordBlockFrames;
            const float  base        = block < 15 ? 0.01f + (float)block / 100.0f : 0.25f;
            const auto   left        = (int16_t)((base + (float)block_frame / 1000.0f) * 32767.0f);
            const auto   right       = (int16_t)(-(base + (float)block_frame / 1000.0f) * 32767.0f);
            const size_t offset      = 44 + frame * 4;
            TEST_ASSERT(bytes[offset] == (uint8_t)left && bytes[offset + 1] == (uint8_t)((uint16_t)left >> 8),
                        "packed left PCM is ordered and exact");
            TEST_ASSERT(bytes[offset + 2] == (uint8_t)right && bytes[offset + 3] == (uint8_t)((uint16_t)right >> 8),
                        "packed right PCM is ordered and exact");
        }
        TEST_ASSERT(read_file(first).size() == 3, "existing take untouched");
        finish_test();
        test_pass();
    }

    {
        test_begin("one-second checkpoint patches recoverable sizes");
        reset("checkpoint");
        start_recording();
        publish(4);
        s_millis = 999;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        s_millis = 1000;
        wait_until(
            []() {
                const auto bytes = read_file(s_root + "/recordings/rec0001.wav");
                return bytes.size() == 60 && le32(bytes, 40) == 16;
            },
            "checkpoint patches sizes");
        const std::vector<uint8_t> bytes = read_file(s_root + "/recordings/rec0001.wav");
        assert_header(bytes, 16);
        stop_recording();
        finish_test();
        test_pass();
    }

    {
        test_begin("overflow stops and finalizes queued prefix");
        reset("overflow");
        start_recording();
        s_stall_sd = true;
        wait_until([]() { return s_sd_call_stalled.load(); }, "worker stalled before overflow");
        float left[1] = {}, right[1] = {};
        for (size_t i = 0; i < 255; ++i) {
            TEST_ASSERT(record_ring_publish(left, right, 1), "fill ring");
        }
        TEST_ASSERT(!record_ring_publish(left, right, 1), "overflow ring");
        s_stall_sd = false;
        wait_for_state(WAV_RECORDER_ERROR, "overflow enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_RING_OVERFLOW, "overflow reason");
        assert_header(read_file(s_root + "/recordings/rec0001.wav"), 255 * 4);
        wav_recorder_service(false);
        wait_for_state(WAV_RECORDER_IDLE, "release clears error state");
        finish_test();
        test_pass();
    }

    {
        test_begin("card loss stops with visible error");
        reset("card-loss");
        start_recording();
        s_stall_sd = true;
        wait_until([]() { return s_sd_call_stalled.load(); }, "worker stalled before card loss");
        publish(2);
        s_available = false;
        s_stall_sd  = false;
        wait_for_state(WAV_RECORDER_ERROR, "card loss enters error");
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_SD_UNAVAILABLE, "card loss reason");
        assert_header(read_file(s_root + "/recordings/rec0001.wav"), 0);
        wav_recorder_service(false);
        wait_for_state(WAV_RECORDER_IDLE, "release clears card error");
        finish_test();
        test_pass();
    }

    {
        test_begin("real filesystem write failure is latched");
        reset("write-failure");
        start_recording();
        s_stall_sd = true;
        wait_until([]() { return s_sd_call_stalled.load(); }, "worker stalled before write failure");
        publish(2);
        struct rlimit old_limit;
        TEST_ASSERT(getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "read file-size limit");
        struct rlimit limit     = old_limit;
        limit.rlim_cur          = 44;
        void (*old_signal)(int) = signal(SIGXFSZ, SIG_IGN);
        TEST_ASSERT(setrlimit(RLIMIT_FSIZE, &limit) == 0, "set file-size limit");
        s_millis   = 1000;
        s_stall_sd = false;
        wait_for_state(WAV_RECORDER_ERROR, "write failure enters error");
        TEST_ASSERT(setrlimit(RLIMIT_FSIZE, &old_limit) == 0, "restore file-size limit");
        signal(SIGXFSZ, old_signal);
        TEST_ASSERT(wav_recorder_error() == WAV_RECORDER_ERROR_WRITE, "write failure reason");
        wav_recorder_service(false);
        wait_for_state(WAV_RECORDER_IDLE, "release clears write error");
        finish_test();
        test_pass();
    }

    record_ring_set_enabled(false);
}
