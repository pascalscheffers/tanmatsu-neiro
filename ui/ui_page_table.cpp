// ui/ui_page_table.cpp — PAGE_TABLE, group_params, page_rows (Stage 5d WO-1).
//
// Extracted from ui.cpp so the page-search logic can be compiled without PAX
// (enabling host unit tests of ui_focus_param without the full drawing stack).
#include "ui_page_table.h"
#include "engine/param_desc.h"

// ---------------------------------------------------------------------------
// PAGE_TABLE (compile-time, WO-1)
// ---------------------------------------------------------------------------
// clang-format off
const PageDef PAGE_TABLE[] = {
    { "PRESET",  PAGE_PRESETS, {0xFF, 0xFF, 0xFF},                      0 },
    { "PERFORM", PAGE_PARAMS,  {GROUP_GLOBAL, GROUP_ARP,   0xFF},        2 },
    { "OSC",     PAGE_PARAMS,  {GROUP_OSC,    0xFF,        0xFF},        1 },
    { "FILTER",  PAGE_PARAMS,  {GROUP_FILTER, GROUP_HPF,   0xFF},        2 },
    { "AMP ENV", PAGE_PARAMS,  {GROUP_ENV,    0xFF,        0xFF},        1 },
    { "MOD ENV", PAGE_PARAMS,  {GROUP_ENV2,   0xFF,        0xFF},        1 },
    { "LFO",     PAGE_PARAMS,  {GROUP_LFO,    0xFF,        0xFF},        1 },
    { "FX",      PAGE_PARAMS,  {GROUP_FX,     0xFF,        0xFF},        1 },
    { "AMP",     PAGE_PARAMS,  {GROUP_AMP,    0xFF,        0xFF},        1 },
};
// clang-format on

const int kNumPages = (int)(sizeof(PAGE_TABLE) / sizeof(PAGE_TABLE[0]));

// ---------------------------------------------------------------------------
// group_params: collect all params for a group into out[], table order.
// ---------------------------------------------------------------------------
int group_params(uint8_t group, const ParamDesc** out, int max_out) {
    int n = 0;
    for (int i = 0; i < kJunoParamCount && n < max_out; i++) {
        if ((uint8_t)JUNO_PARAM_TABLE[i].group == group) {
            out[n++] = &JUNO_PARAM_TABLE[i];
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// page_rows: collect params for a page into out[] (all groups in order).
// ---------------------------------------------------------------------------
int page_rows(int page_index, const ParamDesc** out, int max_out) {
    if (page_index < 0 || page_index >= kNumPages) return 0;
    const PageDef& pd = PAGE_TABLE[page_index];
    if (pd.kind != PAGE_PARAMS) return 0;
    int n = 0;
    for (int g = 0; g < (int)pd.num_groups && n < max_out; g++) {
        n += group_params(pd.groups[g], out + n, max_out - n);
    }
    return n;
}
