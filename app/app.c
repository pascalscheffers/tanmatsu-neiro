// app.c — portable init + main loop.
//
// One implementation, two targets. Brings up the platform, configures the
// engine, starts the audio sink, then loops: drain input, redraw the UI,
// present, pace. Audio runs independently on the HAL's audio thread.
#include "app.h"
#include "platform.h"
#include "synth.h"
#include "ui.h"

// Audio format for Stage 0 (spec 02): 64-frame blocks at 48 kHz.
#define SAMPLE_RATE 48000u
#define BLOCK_SIZE  64u

// ~60 Hz UI cadence; the audio thread is decoupled from this.
#define FRAME_MS 16u

void app_run(void) {
    if (!platform_init()) {
        return;
    }

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
