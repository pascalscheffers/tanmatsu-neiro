// ui.h — portable UI rendering (Stage 2c: param-table-driven pages).
//
// Draws into a PAX framebuffer the platform owns. PAX is portable C, so this
// exact code runs on host and device; only presenting the buffer differs.
// The UI reads the parameter table + current param values from the engine;
// it must not know about any specific voice model (ADR 0008).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pax_gfx.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// Normalised param-value shadow array, indexed by param ID (0..127).
#define UI_NORM_TABLE_SIZE 128

// Persistent UI state — owned by app.c, passed by pointer to all ui_ calls.
typedef struct {
    int         page;                        // selected page index (0..num_pages-1)
    int         row;                         // selected row within the page
    int         active_voices;               // set by app each frame
    int         octave;                      // set by app each frame
    const char* preset_name;                 // placeholder until Stage 2d
    float       norms[UI_NORM_TABLE_SIZE];   // normalised [0,1] shadow per param ID
    // Internal — do not modify directly:
    int         num_pages;
    uint8_t     page_groups[8];  // group enum value for each page slot
} UIState;

// Initialise UIState from the param table: build the page list, compute
// normalised defaults from param table defaults, set preset_name to "INIT".
void ui_state_init(UIState* s);

// Feed a platform event into the UI. Consumes: arrow keys (row/page navigate),
// comma/dot (fine nudge), Shift+comma/dot (coarse nudge). Returns true if the
// event was consumed and should not be forwarded to other handlers.
bool ui_handle_event(UIState* s, const platform_event_t* ev);

// Render one frame of the Stage 2c parameter-page UI into fb.
void ui_draw(pax_buf_t* fb, uint64_t millis, const UIState* s);

#ifdef __cplusplus
}
#endif
