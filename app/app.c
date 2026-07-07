// app.c — portable init + main loop.
//
// One implementation, two targets. Brings up the platform, configures the
// engine, starts the audio sink, then loops: drain input, redraw the UI,
// present, pace. Audio runs independently on the HAL's audio thread.
//
// Rendering runs on a separate lower-priority task (device) or inline on the
// main thread (host, where SDL requires main-thread rendering). The control
// loop is never blocked by the ~1.15 MB display blit, so the next note-on
// arrives within ~1 ms rather than waiting behind a full frame.
#include "app.h"
#include "control/keyboard.h"
#include "control/midi_router.h"
#include "platform.h"
#include "synth.h"
#include "ui.h"
#include "ui_dirty.h"
#ifdef SYNTH_BENCH
#include "bench.h"
#endif
#ifdef SYNTH_BENCH_INTERACTIVE
#include <stdio.h>
#include "pax_fonts.h"
#include "pax_text.h"
#endif
#ifdef SYNTH_PROFILE
#include <stdio.h>
#endif

// Audio format (spec 02): 64-frame blocks at 48 kHz.
#define SAMPLE_RATE 48000u
#define BLOCK_SIZE  64u

// Control-loop poll interval (~1 ms) and render interval (~16 ms / ~60 Hz).
// To enable per-render draw/present profiling: cmake -DSYNTH_PROFILE=ON
// (or, for the host build: cmake -B build-host -DSYNTH_PROFILE=ON)
// For the device build pass EXTRA_CFLAGS=-DSYNTH_PROFILE via idf.py or set it
// in your local CMakeLists component override:  make build EXTRA=-DSYNTH_PROFILE
// (EXTRA is not wired in the Makefile; see IDF_PARAMS in Makefile).
#define POLL_MS   1u
#define RENDER_MS 16u

// Voice-meter debounce (Stage 8 diag, 2026-07-07). The active-voice count churns
// rapidly while smashing a chord; blitting the status band on every change starves
// core-1 audio's PSRAM access -> block-budget overrun -> crackle (device A/B: with
// the meter blit suppressed, over=0/max~633us; with it live, over=15/max=2743us).
// Only commit a meter redraw once the count has held steady this long, so a smash
// produces no mid-play blits and the meter still settles imperceptibly at rest.
#define VOICE_METER_STABLE_MS 100u

#ifdef SYNTH_BENCH_INTERACTIVE
// Device bench is interactive: the badge console (USB-Serial-JTAG) and badgelink
// (USB-OTG) can't share the USB-C, so an AppFS-launched app starts with its
// console detached. We draw a prompt and wait for a key, giving time to switch
// the badge to debug/USB mode and attach `make sniff`/monitor before any output.
static void bench_screen(const char* l1, const char* l2) {
    pax_buf_t* fb = platform_framebuffer();
    if (fb == NULL) return;
    pax_background(fb, 0xFF101018);
    pax_draw_text(fb, 0xFFFFFFFF, pax_font_sky_mono, 24, 12, 12, "Tanmatsu Synth - CPU bench");
    pax_draw_text(fb, 0xFF30C0FF, pax_font_sky_mono, 20, 12, 56, l1);
    if (l2) pax_draw_text(fb, 0xFF7A7A8A, pax_font_sky_mono, 16, 12, 88, l2);
    platform_present(0, (int)pax_buf_get_height(fb));
}

// Block until the user presses a key on the badge (or asks to quit).
static void bench_wait_for_key(void) {
    bench_screen("Press any key to start", "Attach the console first (debug USB mode).");
    for (;;) {
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) platform_exit_to_launcher();
            if (ev.type == PLATFORM_EV_KEY && ev.pressed) return;
        }
        platform_sleep_ms(16);
    }
}
#endif

// ---------------------------------------------------------------------------
// Render callback — called by the platform render task (device) or inline
// from the control loop (host). Reads UIState written by the control loop and
// repaints only when change_seq has advanced. Owns the framebuffer + present
// exclusively — the control loop must never call ui_draw or platform_present.
// ---------------------------------------------------------------------------
static uint32_t s_last_drawn_seq = 0;

static void render_cb(void* arg) {
    UIState*   s  = (UIState*)arg;
    pax_buf_t* fb = platform_framebuffer();
    if (!fb) return;
#ifdef SYNTH_FREEZE_DISPLAY
    // Freeze: let the first frame paint normally, then never repaint again.
    // This removes display-blit / memory-bus pressure so an audio crackle test
    // with PROFILE=1 isolates compute cost from core-0 contention.
    static bool painted_once = false;
    if (painted_once) return;
#endif
    uint32_t seq = s->change_seq;         // volatile read
    if (seq == s_last_drawn_seq) return;  // nothing changed — skip draw + blit
#ifdef SYNTH_PROFILE
    static uint64_t prof_draw_sum = 0, prof_draw_min = UINT64_MAX, prof_draw_max = 0;
    static uint64_t prof_pres_sum = 0, prof_pres_min = UINT64_MAX, prof_pres_max = 0;
    static int      prof_frames = 0;
    uint64_t        t0          = platform_cycles_now();
#endif
    ui_draw(fb, platform_millis(), s);
#ifdef SYNTH_PROFILE
    uint64_t t1 = platform_cycles_now();
#endif
    // ADR 0022: ui_draw always fully repaints the framebuffer, so it is
    // always correct; only the blit is narrowed. The failure mode of the
    // whole dirty-band scheme is therefore "present more," never "present
    // stale": an empty band here (nothing invalidated since the last take)
    // falls back to a full present. The ui_dirty_take/ui_invalidate race that
    // used to risk a silently dropped union is closed with std::atomic (ADR
    // 0023) — a union either lands fully before or fully after a concurrent
    // take, never lost in between.
    // The very first present must be full-screen: the panel/SDL texture starts
    // with undefined contents, and the frame-1 status reconciliation (voices +
    // octave, above) only invalidates the narrow status band — so an incidental
    // partial band would leave the freshly-drawn content area never blitted.
    // s_last_drawn_seq is still 0 only on this first paint (change_seq starts at
    // 1); ui_dirty_take still runs first to clear the pending band either way.
    int  py0, py1;
    bool have_band = ui_dirty_take(&py0, &py1);
    if (!have_band || s_last_drawn_seq == 0) {
        py0 = 0;
        py1 = (int)pax_buf_get_height(fb);
    }
    platform_present(py0, py1);
#ifdef SYNTH_PROFILE
    uint64_t t2       = platform_cycles_now();
    uint64_t cyc_draw = t1 - t0;
    uint64_t cyc_pres = t2 - t1;
    prof_draw_sum    += cyc_draw;
    prof_pres_sum    += cyc_pres;
    if (cyc_draw < prof_draw_min) prof_draw_min = cyc_draw;
    if (cyc_draw > prof_draw_max) prof_draw_max = cyc_draw;
    if (cyc_pres < prof_pres_min) prof_pres_min = cyc_pres;
    if (cyc_pres > prof_pres_max) prof_pres_max = cyc_pres;
    prof_frames++;
    if (prof_frames >= 120) {
        uint32_t hz  = platform_cycles_per_sec();
        uint32_t div = hz / 1000000u;
        if (div == 0) div = 1;
        printf("[PROFILE] draw  avg=%u min=%u max=%u us\n", (unsigned)(prof_draw_sum / (uint64_t)prof_frames / div),
               (unsigned)(prof_draw_min / div), (unsigned)(prof_draw_max / div));
        printf("[PROFILE] pres  avg=%u min=%u max=%u us\n", (unsigned)(prof_pres_sum / (uint64_t)prof_frames / div),
               (unsigned)(prof_pres_min / div), (unsigned)(prof_pres_max / div));
        prof_draw_sum = prof_pres_sum = 0;
        prof_draw_min = prof_pres_min = UINT64_MAX;
        prof_draw_max = prof_pres_max = 0;
        prof_frames                   = 0;
    }
#endif
    s_last_drawn_seq = seq;
#ifdef SYNTH_FREEZE_DISPLAY
    painted_once = true;
#endif
}

void app_run(void) {
    if (!platform_init()) {
        return;
    }

#ifdef SYNTH_BENCH
    // Profiling mode (Stage 0.5): run the kernel table + load ramp, then stop.
    // Never enters the normal UI loop. The bench binary is built separately
    // (make bench / BENCH=1); the shipping image never defines SYNTH_BENCH.
#ifdef SYNTH_BENCH_INTERACTIVE
    bench_wait_for_key();
    bench_screen("Running...", "Watch the console for the result table.");
#endif
    bench_run(SAMPLE_RATE, BLOCK_SIZE);
#ifdef SYNTH_BENCH_INTERACTIVE
    bench_screen("Done.", "Returning to launcher...");
    fflush(stdout);
    platform_sleep_ms(2500);  // let the final console lines drain before reboot
    platform_exit_to_launcher();
#endif
    return;
#endif

    keyboard_init();
    midi_router_init();
    synth_init(SAMPLE_RATE, BLOCK_SIZE);

    // Initialise UI state: computes normalised defaults from the param table,
    // loads the boot preset, sets preset_name to "INIT".
    UIState ui_state;
    ui_state_init(&ui_state);

    const platform_audio_config_t audio_cfg = {
        .sample_rate = SAMPLE_RATE,
        .block_size  = BLOCK_SIZE,
    };
    platform_audio_start(&audio_cfg, synth_render, NULL);

    // Start a dedicated render task on device (core 0, RENDER_PRIO 2); on host
    // SDL requires main-thread rendering so this returns false and we render
    // inline in the control loop below.
    bool     has_render_task = platform_render_task_start(render_cb, &ui_state, RENDER_MS);
    bool     running         = true;
    uint64_t next_ctrl       = 0;
#ifdef SYNTH_PROFILE
    uint64_t next_prof = 0;
#endif

    // SINGLE-PRODUCER INVARIANT: all engine_note_on/off calls happen on this one
    // task (via keyboard_handle_event and midi_router_poll). The s_cmds SPSC ring
    // in engine/command_queue.h is single-producer by design — never move keyboard
    // or MIDI dispatch to a second task without redesigning that ring.
    while (running) {
        // --- Input phase (~1 ms cadence) ---
        // Drain the full event queue before sleeping so no event waits an extra
        // POLL_MS tick.  All state writes happen here; change_seq is bumped
        // AFTER the state is committed so the render task sees a consistent snapshot.
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) running = false;
            keyboard_handle_event(&ev);
            int  prev_page     = ui_state.page;
            bool prev_keyguide = ui_state.show_keyguide;
            if (ui_handle_event(&ui_state, &ev)) {
                ui_state.change_seq++;
                if (ui_state.page != prev_page || ui_state.show_keyguide != prev_keyguide) {
                    ui_invalidate_all();
                } else {
                    int a, b;
                    ui_band_content(&a, &b);
                    ui_invalidate(a, b);
                }
            }
        }
        midi_router_poll();
        {
            uint16_t fid;
            float    fnorm;
            if (midi_router_take_param_focus(&fid, &fnorm)) {
                int prev_page = ui_state.page;
                if (ui_focus_param(&ui_state, fid, fnorm)) {
                    ui_state.change_seq++;
                    if (ui_state.page != prev_page) {
                        ui_invalidate_all();
                    } else {
                        int a, b;
                        ui_band_content(&a, &b);
                        ui_invalidate(a, b);
                    }
                }
            }
        }

        // --- Control tick + inline render (when no render task) ---
        uint64_t now = platform_millis();
        if (now >= next_ctrl) {
            int v                  = engine_active_voices();
            int o                  = keyboard_octave();
            // Keep the drawn value fresh; the status-band blit is debounced so a
            // chord smash (rapid voice-count churn) doesn't starve the audio task.
            ui_state.active_voices = v;
            if (v != ui_state.last_voices) {
                static int      s_vpend       = -1;
                static uint64_t s_vpend_since = 0;
                if (v != s_vpend) {  // new candidate -> (re)start the debounce window
                    s_vpend       = v;
                    s_vpend_since = now;
                }
                if (now - s_vpend_since >= VOICE_METER_STABLE_MS) {
                    ui_state.last_voices = v;
                    ui_state.change_seq++;
                    int a, b;
                    ui_band_status(&a, &b);
                    ui_invalidate(a, b);
#ifdef SYNTH_PROFILE
                    printf("[PROFILE] voices=%d\n", v);
#endif
                }
            }
            if (o != ui_state.last_octave) {
                ui_state.octave      = o;
                ui_state.last_octave = o;
                ui_state.change_seq++;
                int a, b;
                ui_band_status(&a, &b);
                ui_invalidate(a, b);
            } else {
                ui_state.octave = o;
            }

            // Drive hold-to-repeat for F1/F2 shape buttons (WO-5). State writes
            // happen inside ui_tick; bump change_seq afterwards if a repeat fired.
            ui_tick(&ui_state, now);
            if (ui_state.held_dir != 0) {
                ui_state.change_seq++;
                int a, b;
                ui_band_content(&a, &b);
                ui_invalidate(a, b);
            }

            // On host there is no render task; draw inline on the main thread.
            if (!has_render_task) render_cb(&ui_state);

            next_ctrl = now + RENDER_MS;
        }

#ifdef SYNTH_PROFILE
        // Audio-block cycle readout + signal-magnitude probe — ~1 s cadence.
        if (now >= next_prof) {
            uint32_t avg_cyc, max_cyc, over, count;
            platform_audio_profile_read(&avg_cyc, &max_cyc, &over, &count);
            uint32_t hz  = platform_cycles_per_sec();
            uint32_t div = hz / 1000000u;
            if (div == 0) div = 1;
            printf("[PROFILE] audio avg=%u max=%u over=%u/%u us-budget=1333\n", (unsigned)(avg_cyc / div),
                   (unsigned)(max_cyc / div), (unsigned)over, (unsigned)count);
            float pk_mono, pk_postgain, min_gr, pk_out;
            engine_profile_read(&pk_mono, &pk_postgain, &min_gr, &pk_out);
            printf("[PROFILE] sig  mono=%.2f postg=%.2f gr=%.2f out=%.2f\n", (double)pk_mono, (double)pk_postgain,
                   (double)min_gr, (double)pk_out);
            // Per-region CPU split (avg us/block): where the smash-crackle cost lives.
            uint32_t drain_cyc, voices_cyc, master_cyc;
            engine_profile_read_cpu(&drain_cyc, &voices_cyc, &master_cyc);
            printf("[PROFILE] cpu  drain=%u voices=%u master=%u us-per-block\n", (unsigned)(drain_cyc / div),
                   (unsigned)(voices_cyc / div), (unsigned)(master_cyc / div));
            next_prof = now + 1000u;
        }
#endif
        platform_sleep_ms(POLL_MS);
    }

    platform_render_task_stop();
    platform_audio_stop();
    // ESC / window-close ends the loop; hand control back to the launcher (on
    // host this just exits the process). Without this the device app would sit
    // idle after the loop instead of returning home.
    platform_exit_to_launcher();
}
