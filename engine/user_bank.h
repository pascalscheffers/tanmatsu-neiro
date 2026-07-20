// engine/user_bank.h — loader for JSON preset banks from SD/AppFS (WO-13-neiro-bank, ADR 0027).
//
// This module scans the `/presets/` directory on the SD card for `.json` files,
// parses them using `bank_json_parse`, and caches the resulting patches in a local pool.
// Control-path only; never call from the audio thread.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bank_json.h"

// Maximum number of user presets cached in internal memory.
static constexpr int kUserBankMaxPatches = 128;

// Returns the total number of user patches loaded from SD/AppFS.
int preset_user_count(void);

// Returns the name of the user patch at index `idx`.
const char* preset_user_name(int idx);

// Fill `ids_out`/`vals_out` with physical param values for user preset `idx`.
int preset_user_params(int idx, uint16_t* ids_out, float* vals_out, int max_count);

// Fill `routings_out` with the modulation routings for user preset `idx`.
int preset_user_routings(int idx, Routing* routings_out, int max_count);

// Force a re-scan of the SD `/presets/` directory and reload all JSON banks.
void preset_user_bank_reload(void);
