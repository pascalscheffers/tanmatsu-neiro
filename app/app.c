// app.c — portable init + main loop.
//
// One implementation, two targets. Brings up the platform, configures the
// engine, starts the audio sink, then loops: drain input, redraw the UI,
// present, pace. Audio runs independently on the HAL's audio thread.
#include "app.h"
#include "control/keyboard.h"
#include "control/midi_router.h"
#include "platform.h"
#include "synth.h"
#include "ui.h"
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

// Input poll interval: ~1 ms so key presses aren't gated behind the full blit.
// Render interval: ~16 ms (~60 Hz), but only when the UI is dirty.
// To enable per-frame draw/present profiling: cmake -DSYNTH_PROFILE=ON
// (or, for the host build: cmake -B build-host -DSYNTH_PROFILE=ON)
// For the device build pass EXTRA_CFLAGS=-DSYNTH_PROFILE via idf.py or set it
// in your local CMakeLists component override:  make build EXTRA=-DSYNTH_PROFILE
// (EXTRA is not wired in the Makefile; see IDF_PARAMS in Makefile).
#define POLL_MS   1u
#define RENDER_MS 16u

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
    platform_present();
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

    // Initialise UI state: builds page list and default norm values from the
    // param table, sets preset_name to "INIT".
    UIState ui_state;
    ui_state_init(&ui_state);

    const platform_audio_config_t audio_cfg = {
        .sample_rate = SAMPLE_RATE,
        .block_size  = BLOCK_SIZE,
    };
    platform_audio_start(&audio_cfg, synth_render, NULL);

    pax_buf_t* fb      = platform_framebuffer();
    bool       running = true;

    // SINGLE-PRODUCER INVARIANT: all engine_note_on/off calls happen on this one
    // task (via keyboard_handle_event and midi_router_poll). The s_cmds SPSC ring
    // in engine/command_queue.h is single-producer by design — never move keyboard
    // or MIDI dispatch to a second task without redesigning that ring.
    uint64_t next_render = 0;

#ifdef SYNTH_PROFILE
    // Per-render profiling accumulators (draw + present, in cycles).
    uint64_t prof_draw_sum = 0, prof_draw_min = UINT64_MAX, prof_draw_max = 0;
    uint64_t prof_pres_sum = 0, prof_pres_min = UINT64_MAX, prof_pres_max = 0;
    int      prof_frames = 0;
#endif

    while (running) {
        // --- Input phase (~1 ms cadence) ---
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) running = false;
            keyboard_handle_event(&ev);
            if (ui_handle_event(&ui_state, &ev)) ui_state.dirty = true;
        }
        midi_router_poll();

        // --- Render phase (~16 ms cadence, dirty-gated) ---
        uint64_t now = platform_millis();
        if (now >= next_render) {
            ui_state.active_voices = engine_active_voices();
            ui_state.octave        = keyboard_octave();

            // Drive hold-to-repeat for F1/F2 shape buttons (WO-5).
            ui_tick(&ui_state, now);
            if (ui_state.held_dir != 0) ui_state.dirty = true;

            // Mark dirty if a displayed per-frame value changed since last paint.
            if (ui_state.active_voices != ui_state.last_drawn_voices || ui_state.octave != ui_state.last_drawn_octave) {
                ui_state.dirty = true;
            }

            if (fb && ui_state.dirty) {
#ifdef SYNTH_PROFILE
                uint64_t t0 = platform_cycles_now();
#endif
                ui_draw(fb, now, &ui_state);
#ifdef SYNTH_PROFILE
                uint64_t t1 = platform_cycles_now();
#endif
                platform_present();
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
                    printf("[PROFILE] draw  avg=%u min=%u max=%u us\n",
                           (unsigned)(prof_draw_sum / (uint64_t)prof_frames / div), (unsigned)(prof_draw_min / div),
                           (unsigned)(prof_draw_max / div));
                    printf("[PROFILE] pres  avg=%u min=%u max=%u us\n",
                           (unsigned)(prof_pres_sum / (uint64_t)prof_frames / div), (unsigned)(prof_pres_min / div),
                           (unsigned)(prof_pres_max / div));
                    prof_draw_sum = prof_pres_sum = 0;
                    prof_draw_min = prof_pres_min = UINT64_MAX;
                    prof_draw_max = prof_pres_max = 0;
                    prof_frames                   = 0;
                }
#endif
                ui_state.dirty             = false;
                ui_state.last_drawn_voices = ui_state.active_voices;
                ui_state.last_drawn_octave = ui_state.octave;
            }
            next_render = now + RENDER_MS;
        }

        platform_sleep_ms(POLL_MS);
    }

    platform_audio_stop();
    // ESC / window-close ends the loop; hand control back to the launcher (on
    // host this just exits the process). Without this the device app would sit
    // idle after the loop instead of returning home.
    platform_exit_to_launcher();
}
