// engine/factory_bank.h — accessor seam for the embedded Neiro factory JSON
// bank (WO-13-neiro-bank, ADR 0027 embed mechanism).
//
// The bank's bytes live in exactly one place: engine/banks/neiro_factory.json.
// Each build target resolves this accessor to those same bytes by a different
// mechanism (device: ESP-IDF EMBED_TXTFILES linker symbols in
// main/factory_bank_embed.cpp; host/test: a generated .cpp wrapping the file
// in a raw string literal, produced at CMake configure time — see
// host/CMakeLists.txt and tests/host/CMakeLists.txt). preset.cpp only ever
// calls this accessor; it never knows which mechanism backs it.
#pragma once

#include <cstddef>

// Returns a pointer to the embedded Neiro factory bank JSON (NUL-terminated)
// and, if `len_out` is non-null, writes its length (excluding the NUL).
const char* factory_bank_neiro_json(size_t* len_out);
