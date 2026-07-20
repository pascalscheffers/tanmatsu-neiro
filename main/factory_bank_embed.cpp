// main/factory_bank_embed.cpp — device impl of factory_bank_neiro_json() and
// juno106_factory_bank_json() (WO-13-neiro-bank, WO-13i). The bank bytes are
// embedded into flash by ESP-IDF's EMBED_TXTFILES (see main/CMakeLists.txt),
// which places each file's contents between two linker-generated symbols and
// NUL-terminates them for us (EMBED_TXTFILES, unlike EMBED_FILES, always
// appends a NUL byte).
#include "factory_bank.h"

extern "C" const char _binary_neiro_factory_json_start[];
extern "C" const char _binary_neiro_factory_json_end[];

extern "C" const char _binary_juno106_factory_json_start[];
extern "C" const char _binary_juno106_factory_json_end[];

const char* factory_bank_neiro_json(size_t* len_out) {
    const char* start = _binary_neiro_factory_json_start;
    const char* end   = _binary_neiro_factory_json_end;
    if (len_out) {
        // EMBED_TXTFILES appends a trailing NUL; exclude it from the length so
        // callers get the same "text length" as strlen() would report.
        size_t n = (size_t)(end - start);
        if (n > 0 && start[n - 1] == '\0') n--;
        *len_out = n;
    }
    return start;
}

const char* juno106_factory_bank_json(size_t* len_out) {
    const char* start = _binary_juno106_factory_json_start;
    const char* end   = _binary_juno106_factory_json_end;
    if (len_out) {
        size_t n = (size_t)(end - start);
        if (n > 0 && start[n - 1] == '\0') n--;
        *len_out = n;
    }
    return start;
}
