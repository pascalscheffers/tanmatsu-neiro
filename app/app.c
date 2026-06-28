// app.c — portable init + main loop.
//
// One implementation, two targets. Brings up the platform, configures the
// engine, starts the audio sink, then loops: drain input, redraw the UI,
// present, pace. Audio runs independently on the HAL's audio thread.
#include "app.h"
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

// Audio format for Stage 0 (spec 02): 64-frame blocks at 48 kHz.
#define SAMPLE_RATE 48000u
#define BLOCK_SIZE  64u

// ~60 Hz UI cadence; the audio thread is decoupled from this.
#define FRAME_MS 16u

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

    synth_init(SAMPLE_RATE);

    const platform_audio_config_t audio_cfg = {
        .sample_rate = SAMPLE_RATE,
        .block_size  = BLOCK_SIZE,
    };
    platform_audio_start(&audio_cfg, synth_render, NULL);

    pax_buf_t* fb      = platform_framebuffer();
    bool       running = true;
    while (running) {
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) {
                running = false;
            }
        }

        if (fb) {
            ui_draw(fb, platform_millis());
            platform_present();
        }

        platform_sleep_ms(FRAME_MS);
    }

    platform_audio_stop();
}
