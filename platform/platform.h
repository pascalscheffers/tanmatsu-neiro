// platform.h — the HAL membrane (ADR 0007).
//
// This is the ONLY contract that crosses the line between portable code
// (engine/ ui/ app/ dsp/ control/) and the OS/board world. Code above the
// membrane includes this header and nothing else platform-specific: no esp_*,
// no bsp/*, no SDL*, no miniaudio. Two implementations satisfy it —
// platform/device/ (ESP-IDF + badge-bsp) and platform/host/ (SDL2 + miniaudio).
//
// Stage 0 wires three of the five seams (audio sink, display present, input).
// Stage 5a adds the MIDI-in seam (platform_midi_read); the out path arrives
// with USB-C device mode (Stage 5d). Storage was wired in Stage 2d.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pax_gfx.h"  // pax_buf_t — PAX is portable C, builds on both targets

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Audio sink
// ---------------------------------------------------------------------------
// The HAL owns the audio thread and the real-time deadline. It pulls stereo
// float blocks from this callback; the engine just fills the buffers. Samples
// are in [-1, 1]; `n` is the frame count (block size). The callback runs on the
// audio context and MUST obey the real-time rules (CLAUDE.md): no alloc, no
// logging, no blocking.
typedef void (*platform_audio_render_fn)(float* left, float* right, size_t n, void* user);

typedef struct {
    uint32_t sample_rate;  // e.g. 48000
    size_t   block_size;   // frames per render callback, e.g. 64
} platform_audio_config_t;

// ---------------------------------------------------------------------------
// Input (canonical events)
// ---------------------------------------------------------------------------
typedef enum {
    PLATFORM_EV_NONE = 0,
    PLATFORM_EV_KEY,   // a key changed state; see `key` + `pressed` + `mods`
    PLATFORM_EV_QUIT,  // user asked to close the app (host window close, etc.)
} platform_event_type_t;

// Navigation key codes — above the ASCII range (> 0x7F), no conflict.
#define PLATFORM_KEY_UP    0x0100
#define PLATFORM_KEY_DOWN  0x0101
#define PLATFORM_KEY_LEFT  0x0102
#define PLATFORM_KEY_RIGHT 0x0103

// Shape-button key codes (the six buttons above the number row on the badge,
// left-to-right: F1=X, F2=triangle, F3=square, F4=circle, F5=three-lobe,
// F6=diamond). On device: BSP_INPUT_NAVIGATION_KEY_F1…F6. On host: SDLK_F1…F6.
#define PLATFORM_KEY_F1 0x0110
#define PLATFORM_KEY_F2 0x0111
#define PLATFORM_KEY_F3 0x0112
#define PLATFORM_KEY_F4 0x0113
#define PLATFORM_KEY_F5 0x0114
#define PLATFORM_KEY_F6 0x0115

// Modifier flags for PLATFORM_EV_KEY events.
#define PLATFORM_MOD_SHIFT (1u << 0)

typedef struct {
    platform_event_type_t type;
    int                   key;      // ASCII-ish code or PLATFORM_KEY_* for PLATFORM_EV_KEY
    bool                  pressed;  // true = down, false = up
    uint8_t               mods;     // bit flags: PLATFORM_MOD_SHIFT
} platform_event_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Bring up the board/window, NVS/storage roots, and the shared framebuffer.
// Returns false on a fatal init error.
bool platform_init(void);

// The shared UI framebuffer that portable ui/ code draws into. Owned by the
// platform; valid after platform_init() succeeds. NULL if the target has no
// display.
pax_buf_t* platform_framebuffer(void);

// Push the current framebuffer contents to the screen.
void platform_present(void);

// Start the audio sink, which begins calling `render` on its own thread/context.
// Returns false if audio could not be started.
bool platform_audio_start(const platform_audio_config_t* cfg, platform_audio_render_fn render, void* user);

// Stop the audio sink; no further render calls occur after this returns.
void platform_audio_stop(void);

// End this app and hand control back to the system shell: on device, reboot
// into the launcher (badge AppFS); on host, exit the process. Does not return.
void platform_exit_to_launcher(void);

// Non-blocking input poll. Returns true and fills *out when an event was
// available, false when the queue is empty.
bool platform_poll_event(platform_event_t* out);

// Wall-clock milliseconds since startup, for UI animation only. Musical time is
// derived from the audio sample counter, not from here (ADR 0010).
uint64_t platform_millis(void);

// ---------------------------------------------------------------------------
// Cycle counter (platform-specific; Stage 0.5 profiling seam)
// ---------------------------------------------------------------------------
// Monotonic CPU-cycle counter. On device: RISC-V mcycle CSR (32-bit hardware,
// returned as uint64_t for convenient difference arithmetic — callers must take
// differences within a few milliseconds to avoid 32-bit wrap). On host: nanosec-
// onds from CLOCK_MONOTONIC scaled to pseudo-1 GHz cycles (reference only; the
// device numbers are the budget).
uint64_t platform_cycles_now(void);

// Nominal CPU cycles per second. On device: the configured CPU clock frequency.
// On host: 1 000 000 000 (the 1 GHz pseudo-clock used by platform_cycles_now).
uint32_t platform_cycles_per_sec(void);

// Sleep/yield the calling (UI) thread for approximately `ms` milliseconds, so
// the main loop paces the display without busy-spinning. Not for the audio path.
void platform_sleep_ms(uint32_t ms);

// ---------------------------------------------------------------------------
// Storage (Stage 2d) — key/blob store
// ---------------------------------------------------------------------------
// Synchronous key/value blob store. On device: ESP-IDF NVS (keys ≤ 15 chars,
// blobs up to ~32 KB). On host: one file per key in ./presets/.
// Call only from the control/UI thread — never from the audio thread.

// Save `len` bytes from `data` under `key`. Returns 0 on success, -1 on error.
int platform_storage_save(const char* key, const void* data, size_t len);

// Load the blob for `key` into `buf` (at most `max_len` bytes).
// Returns bytes loaded on success, -1 if the key does not exist or on error.
int platform_storage_load(const char* key, void* buf, size_t max_len);

// ---------------------------------------------------------------------------
// MIDI input (Stage 5a) — in-only raw byte stream
// ---------------------------------------------------------------------------
// Non-blocking MIDI input. Copies up to max_len raw MIDI bytes into buf and
// returns the number copied (0 when nothing is pending). Raw stream (running
// status possible) — control/midi_in parses it. In-only for now; the out path
// arrives with USB-C device mode (Stage 5d). Call from the control/UI thread.
size_t platform_midi_read(uint8_t* buf, size_t max_len);

// ---------------------------------------------------------------------------
// Render task (input-latency fix) — off-thread drawing
// ---------------------------------------------------------------------------
// Start a background task that calls render_cb(ctx) at ~render_ms intervals so
// the caller's control loop is never blocked by drawing/presenting. On DEVICE
// this is a dedicated lower-priority core-0 task, and the CALLER's task is
// raised above the USB-host tasks so input is serviced promptly. Returns true
// if the task was started. On HOST (SDL must render on the main thread) it does
// nothing and returns false — the caller must then render inline.
bool platform_render_task_start(void (*render_cb)(void* ctx), void* ctx, uint32_t render_ms);

// Stop the render task started above (blocks until it has exited). No-op if none.
void platform_render_task_stop(void);

#ifdef __cplusplus
}
#endif
