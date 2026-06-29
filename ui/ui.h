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
// Pages are defined by the static PAGE_TABLE in ui.cpp (explicit order,
// multi-group pages supported). kNumPages is the compile-time page count.
typedef struct {
    int   page;                       // selected page index (0..kNumPages-1)
    int   row;                        // selected row within the page
    int   active_voices;              // set by app each frame
    int   octave;                     // set by app each frame
    char  preset_name[33];            // displayed in the status strip
    int   preset_idx;                 // factory index (0-based) or -1 for user preset
    float norms[UI_NORM_TABLE_SIZE];  // normalised [0,1] shadow per param ID

    // Audition-with-revert state (WO-3, preset browser page).
    // Snapshot is captured when entering page 0; restored on back/navigate-away
    // if auditioning is still true (user never confirmed with F4/Enter).
    bool  auditioning;                            // true while previewing an unconfirmed preset
    float audition_snapshot[UI_NORM_TABLE_SIZE];  // pre-audition norms copy
    char  audition_preset_name[33];               // pre-audition preset_name copy
    int   audition_preset_idx;                    // pre-audition preset_idx copy

    // Key-guide overlay toggle (WO-6, F5 three-lobe button).
    bool show_keyguide;  // true = draw the musical-typing overlay on top of any page

    // Hold-to-repeat state (WO-5, F1/F2 shape buttons).
    int      held_dir;       // 0=none, -1=F1 (down), +1=F2 (up)
    int      held_row;       // row index when hold started (detect row change)
    uint64_t held_since_ms;  // timestamp when hold began
    uint64_t last_step_ms;   // timestamp of most recent repeat step
    float    repeat_accum;   // fractional norm accumulator for continuous params

    // Render-task coordination (input-latency fix, app.c loop).
    // change_seq is bumped by the control loop on every visible change; the
    // render task (or inline render on host) redraws when it differs from the
    // last value it drew.  Single-writer (control), single-reader (render) —
    // a volatile aligned 32-bit word is atomic on RV32.
    volatile uint32_t change_seq;   // monotonic counter; init to 1 so first frame draws
    int               last_voices;  // active_voices at the most recent change_seq bump
    int               last_octave;  // octave at the most recent change_seq bump
} UIState;

// Initialise UIState: compute normalised defaults from the param table,
// load the boot preset, set preset_name to "INIT". Page order is fixed by
// the PAGE_TABLE in ui.cpp; no dynamic page construction.
void ui_state_init(UIState* s);

// Feed a platform event into the UI. Consumes: arrow keys (row/page navigate),
// F1/F2 (nudge down/up), F3 (back to preset page), F6 (save user preset),
// Shift+comma/dot (coarse nudge). Returns true if the event was consumed and
// should not be forwarded to other handlers.
bool ui_handle_event(UIState* s, const platform_event_t* ev);

// Per-frame tick: drives hold-to-repeat for F1/F2 on parameter pages.
// Call once per frame BEFORE ui_draw(), passing the current millisecond count.
void ui_tick(UIState* s, uint64_t now_ms);

// Render one frame of the Stage 2c parameter-page UI into fb.
void ui_draw(pax_buf_t* fb, uint64_t millis, const UIState* s);

#ifdef __cplusplus
}
#endif
