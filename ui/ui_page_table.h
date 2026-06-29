// ui/ui_page_table.h — internal header: PAGE_TABLE, PageDef, PageKind, page_rows.
//
// Shared by ui.cpp (draw path) and ui_focus.cpp (CC-driven focus, testable
// without PAX). Do not include from outside the ui/ layer.
#pragma once

#include <stdint.h>
#include "engine/param_desc.h"

// ---------------------------------------------------------------------------
// PageKind / PageDef
// ---------------------------------------------------------------------------
enum PageKind : uint8_t {
    PAGE_PRESETS = 0,  // no param rows — rendered by ui_presets_draw (WO-3)
    PAGE_PARAMS  = 1,  // one or more ParamGroup columns concatenated
};

struct PageDef {
    const char* title;
    PageKind    kind;
    uint8_t     groups[3];  // ParamGroup values; unused slots are 0xFF
    uint8_t     num_groups;
};

// Defined in ui_page_table.cpp.
extern const PageDef PAGE_TABLE[];
extern const int     kNumPages;

// Collect all params for a group into out[], table order. Returns count.
// Defined in ui_page_table.cpp.
int group_params(uint8_t group, const ParamDesc** out, int max_out);

// Collect params for a page into out[] (all groups in order). Returns count.
// For PAGE_PRESETS returns 0.  max_out should be at least 24.
// Defined in ui_page_table.cpp.
int page_rows(int page_index, const ParamDesc** out, int max_out);
