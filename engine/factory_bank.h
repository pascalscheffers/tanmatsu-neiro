// engine/factory_bank.h — accessor seam for the embedded factory JSON banks
// (WO-13-neiro-bank, WO-13i; ADR 0027 embed mechanism).
//
// Each bank's bytes live in exactly one place: engine/banks/neiro_factory.json
// and engine/banks/juno106_factory.json respectively. Each build target
// resolves these accessors to those same bytes by a different mechanism
// (device: ESP-IDF EMBED_TXTFILES linker symbols in
// main/factory_bank_embed.cpp; host/test: a generated .cpp wrapping both
// files in raw string literals, produced at CMake configure time — see
// host/CMakeLists.txt and tests/host/CMakeLists.txt). preset.cpp only ever
// calls these accessors; it never knows which mechanism backs them.
#pragma once

#include <cstddef>

// Returns a pointer to the embedded Neiro factory bank JSON (NUL-terminated)
// and, if `len_out` is non-null, writes its length (excluding the NUL).
const char* factory_bank_neiro_json(size_t* len_out);

// Returns a pointer to the embedded 128-patch original Juno-106 factory bank
// JSON (NUL-terminated) and, if `len_out` is non-null, writes its length
// (excluding the NUL).
const char* juno106_factory_bank_json(size_t* len_out);
