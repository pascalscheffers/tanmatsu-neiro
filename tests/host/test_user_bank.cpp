/* tests/host/test_user_bank.cpp — SD-card user preset bank loader (engine/user_bank).
 *
 * platform_sd_root()/platform_sd_available() are process-wide extern "C" symbols
 * defined once, by test_wav_recorder.cpp's stub. This suite reads back whatever
 * root that stub currently reports and creates a "presets" directory under it,
 * rather than defining its own conflicting stub. */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include "engine/param_id.h"
#include "engine/user_bank.h"
#include "platform.h"
#include "runner.h"

static std::string preset_dir(void) {
    return std::string(platform_sd_root()) + "/presets";
}

static void write_file(const char* name, const char* json) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", preset_dir().c_str(), name);
    FILE* f = fopen(path, "wb");
    fwrite(json, 1, strlen(json), f);
    fclose(f);
}

static void setup_preset_dir(void) {
    mkdir(platform_sd_root(), 0755);
    mkdir(preset_dir().c_str(), 0755);
}

static void test_user_bank_loads_json_files(void) {
    test_begin("user bank: loads patches from .json files on SD");
    setup_preset_dir();
    write_file("mine.json", R"([{ "name": "MyBass", "params": { "Osc Level": 0.75 } }])");
    write_file("notjson.txt", "not a bank");
    preset_user_bank_reload();

    TEST_ASSERT(preset_user_count() == 1, "expected 1 patch (non-.json file must be skipped)");
    TEST_ASSERT(strcmp(preset_user_name(0), "MyBass") == 0, "patch name must be 'MyBass'");

    uint16_t ids[8];
    float    vals[8];
    int      n = preset_user_params(0, ids, vals, 8);
    TEST_ASSERT(n == 1, "expected 1 param");
    TEST_ASSERT(ids[0] == ParamId::OSC_LEVEL, "param id must resolve to OSC_LEVEL");
    TEST_ASSERT(vals[0] == 0.75f, "physical value must be exact");
    test_pass();
}

static void test_user_bank_out_of_range(void) {
    test_begin("user bank: out-of-range index is safe");
    preset_user_bank_reload();
    setup_preset_dir();
    write_file("mine.json", R"([{ "name": "MyBass", "params": { "Osc Level": 0.75 } }])");
    preset_user_bank_reload();

    TEST_ASSERT(strcmp(preset_user_name(-1), "") == 0, "negative index must return empty name");
    TEST_ASSERT(strcmp(preset_user_name(99), "") == 0, "too-large index must return empty name");

    uint16_t ids[8];
    float    vals[8];
    TEST_ASSERT(preset_user_params(-1, ids, vals, 8) == -1, "negative index params must return -1");
    TEST_ASSERT(preset_user_params(99, ids, vals, 8) == -1, "too-large index params must return -1");

    Routing routes[8];
    TEST_ASSERT(preset_user_routings(-1, routes, 8) == -1, "negative index routings must return -1");
    test_pass();
}

static void test_user_bank_reload_picks_up_changes(void) {
    test_begin("user bank: reload re-scans the preset directory");
    setup_preset_dir();
    write_file("mine.json", R"([{ "name": "MyBass", "params": { "Osc Level": 0.75 } }])");
    preset_user_bank_reload();
    TEST_ASSERT(preset_user_count() == 1, "expected 1 patch before adding a second file");

    write_file("second.json", R"([{ "name": "MyLead", "params": { "Osc Level": 0.5 } }])");
    preset_user_bank_reload();
    TEST_ASSERT(preset_user_count() == 2, "expected 2 patches after reload picks up new file");
    test_pass();
}

static void cleanup_preset_dir(void) {
    std::string dir = preset_dir();
    DIR*        d   = opendir(dir.c_str());
    if (!d) return;
    while (const dirent* entry = readdir(d)) {
        if (entry->d_name[0] != '.') {
            unlink((dir + "/" + entry->d_name).c_str());
        }
    }
    closedir(d);
    rmdir(dir.c_str());
}

void test_user_bank_suite(void) {
    test_user_bank_loads_json_files();
    test_user_bank_out_of_range();
    test_user_bank_reload_picks_up_changes();
    cleanup_preset_dir();
    preset_user_bank_reload();
}
