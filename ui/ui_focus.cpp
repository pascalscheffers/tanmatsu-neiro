// ui/ui_focus.cpp — CC-driven UI focus (work-order: Launchkey follow).
//
// Separated from ui.cpp so it can be compiled without PAX for host unit tests.
// Depends only on: ui_page_table (PAGE_TABLE, page_rows, kNumPages), ui.h
// (UIState), engine/param_desc.h, and engine/synth.h (engine_set_param_norm).
// No PAX drawing calls.
#include <string.h>
#include "engine/param_desc.h"
#include "engine/synth.h"
#include "ui.h"
#include "ui_page_table.h"

static inline float focus_clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// ---------------------------------------------------------------------------
// ui_focus_param — jump the UI to whichever page/row displays param_id and
// update the norm shadow so the value bar tracks the CC position live.
//
// If an audition is in progress on the PRESET page it is cleared first (same
// revert path as navigating away while auditioning in ui_presets_handle_event).
// ---------------------------------------------------------------------------
extern "C" bool ui_focus_param(UIState* s, uint16_t param_id, float norm) {
    for (int page = 0; page < kNumPages; page++) {
        const ParamDesc* rows[24];
        int              n = page_rows(page, rows, 24);
        for (int r = 0; r < n; r++) {
            if (rows[r]->id == param_id) {
                // Resolve any in-progress preset audition before switching page.
                if (s->auditioning) {
                    // Restore norms from snapshot, push to engine, clear flag.
                    for (int j = 0; j < kJunoParamCount; j++) {
                        uint16_t id = JUNO_PARAM_TABLE[j].id;
                        if (id < UI_NORM_TABLE_SIZE) {
                            s->norms[id] = s->audition_snapshot[id];
                            engine_set_param_norm(id, s->norms[id]);
                        }
                    }
                    strncpy(s->preset_name, s->audition_preset_name, sizeof(s->preset_name) - 1);
                    s->preset_name[sizeof(s->preset_name) - 1] = '\0';
                    s->preset_idx                              = s->audition_preset_idx;
                    s->auditioning                             = false;
                }
                s->page = page;
                s->row  = r;
                if (param_id < UI_NORM_TABLE_SIZE) {
                    s->norms[param_id] = focus_clamp01(norm);
                }
                return true;
            }
        }
    }
    return false;
}
