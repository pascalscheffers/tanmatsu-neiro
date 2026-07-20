// engine/user_bank.cpp — loader for JSON preset banks from SD/AppFS (WO-13-neiro-bank, ADR 0027).
#include "user_bank.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

static PresetPatch g_user[kUserBankMaxPatches];
static int         g_user_count = -1;  // -1 = not yet loaded

static void ensure_user_bank_loaded(void) {
    if (g_user_count >= 0) return;

    const char* root = platform_sd_root();
    if (!root) {
        g_user_count = 0;
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/presets", root);

    DIR* dir = opendir(path);
    if (!dir) {
        g_user_count = 0;
        return;
    }

    int            n_total = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && n_total < kUserBankMaxPatches) {
        // Only process .json files
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".json") != 0) continue;

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);

        FILE* f = fopen(file_path, "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (len <= 0 || len > 65536) {  // Sanity limit 64KB per bank file
            fclose(f);
            continue;
        }

        char* buf = (char*)malloc(len + 1);
        if (!buf) {
            fclose(f);
            continue;
        }

        size_t read_n = fread(buf, 1, len, f);
        buf[read_n]   = '\0';
        fclose(f);

        int slots_left = kUserBankMaxPatches - n_total;
        int added      = bank_json_parse(buf, read_n, &g_user[n_total], slots_left);
        if (added > 0) {
            n_total += added;
        }

        free(buf);
    }
    closedir(dir);
    g_user_count = n_total;
}

int preset_user_count(void) {
    ensure_user_bank_loaded();
    return g_user_count < 0 ? 0 : g_user_count;
}

const char* preset_user_name(int idx) {
    ensure_user_bank_loaded();
    if (idx < 0 || idx >= preset_user_count()) return "";
    return g_user[idx].name;
}

int preset_user_params(int idx, uint16_t* ids_out, float* vals_out, int max_count) {
    ensure_user_bank_loaded();
    if (idx < 0 || idx >= preset_user_count()) return -1;
    const PresetPatch& p = g_user[idx];
    int                n = (p.count < max_count) ? p.count : max_count;
    for (int i = 0; i < n; i++) {
        ids_out[i]  = p.ids[i];
        vals_out[i] = p.vals[i];
    }
    return n;
}

int preset_user_routings(int idx, Routing* routings_out, int max_count) {
    ensure_user_bank_loaded();
    if (idx < 0 || idx >= preset_user_count()) return -1;
    const PresetPatch& p = g_user[idx];
    if (p.route_count == 0) return 0;
    int n = (p.route_count < max_count) ? p.route_count : max_count;
    for (int i = 0; i < n; i++) {
        routings_out[i] = p.routes[i];
    }
    return n;
}

void preset_user_bank_reload(void) {
    g_user_count = -1;
}
