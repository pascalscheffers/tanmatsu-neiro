// platform_host.c — desktop HAL backend (SDL2 + miniaudio).
//
// One of two implementations of platform.h (the other is platform/device/).
// SDL2 owns the window, present, and input; miniaudio owns the audio thread.
// Nothing above the membrane sees either library — they live only here.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "miniaudio.h"
#include "platform.h"

// Match the device panel so UI/UX built on the Mac transfers 1:1 (spec 04).
#define WIN_W 800
#define WIN_H 480

// Largest chunk we hand to the engine per render call inside the audio callback.
// miniaudio's frame count per callback is bounded by the period size we request
// (the engine block size), but we chunk defensively to a fixed static buffer so
// the callback never allocates.
#define MAX_CHUNK 1024

static SDL_Window*   s_window   = NULL;
static SDL_Renderer* s_renderer = NULL;
static SDL_Texture*  s_texture  = NULL;
static pax_buf_t     s_fb       = {0};

static ma_device                s_device;
static bool                     s_audio_running = false;
static platform_audio_render_fn s_render        = NULL;
static void*                    s_render_user   = NULL;

static uint64_t s_start_ms = 0;

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static void audio_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    (void)input;
    float* out = (float*)output;
    if (!s_render) {
        for (ma_uint32 i = 0; i < frame_count * 2; i++) {
            out[i] = 0.0f;
        }
        return;
    }

    static float left[MAX_CHUNK];
    static float right[MAX_CHUNK];

    ma_uint32 done = 0;
    while (done < frame_count) {
        ma_uint32 n = frame_count - done;
        if (n > MAX_CHUNK) {
            n = MAX_CHUNK;
        }
        s_render(left, right, n, s_render_user);
        for (ma_uint32 i = 0; i < n; i++) {
            out[(done + i) * 2 + 0] = left[i];
            out[(done + i) * 2 + 1] = right[i];
        }
        done += n;
    }
    (void)device;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool platform_init(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    s_window = SDL_CreateWindow("Tanmatsu Synth", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                                SDL_WINDOW_SHOWN);
    if (!s_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, 0);  // fall back to software
    }
    if (!s_renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIN_W, WIN_H);
    if (!s_texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }

    // Framebuffer pixel layout matches the SDL texture so colors pass through
    // 1:1; the device backend uses whatever format the BSP reports instead.
    pax_buf_init(&s_fb, NULL, WIN_W, WIN_H, PAX_BUF_32_8888ARGB);

    s_start_ms = SDL_GetTicks64();
    return true;
}

pax_buf_t* platform_framebuffer(void) {
    return &s_fb;
}

void platform_present(void) {
    SDL_UpdateTexture(s_texture, NULL, pax_buf_get_pixels(&s_fb), WIN_W * 4);
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

bool platform_audio_start(const platform_audio_config_t* cfg, platform_audio_render_fn render, void* user) {
    s_render      = render;
    s_render_user = user;

    ma_device_config config   = ma_device_config_init(ma_device_type_playback);
    config.playback.format    = ma_format_f32;
    config.playback.channels  = 2;
    config.sampleRate         = cfg->sample_rate;
    config.periodSizeInFrames = (ma_uint32)cfg->block_size;
    config.dataCallback       = audio_callback;

    if (ma_device_init(NULL, &config, &s_device) != MA_SUCCESS) {
        SDL_Log("miniaudio: device init failed");
        return false;
    }
    if (ma_device_start(&s_device) != MA_SUCCESS) {
        SDL_Log("miniaudio: device start failed");
        ma_device_uninit(&s_device);
        return false;
    }
    s_audio_running = true;
    return true;
}

void platform_audio_stop(void) {
    if (s_audio_running) {
        ma_device_uninit(&s_device);
        s_audio_running = false;
    }
}

bool platform_poll_event(platform_event_t* out) {
    SDL_Event e;
    if (!SDL_PollEvent(&e)) {
        return false;
    }
    switch (e.type) {
        case SDL_QUIT:
            out->type = PLATFORM_EV_QUIT;
            return true;
        case SDL_WINDOWEVENT:
            out->type = (e.window.event == SDL_WINDOWEVENT_CLOSE) ? PLATFORM_EV_QUIT : PLATFORM_EV_NONE;
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                out->type = PLATFORM_EV_QUIT;
                return true;
            }
            out->type    = PLATFORM_EV_KEY;
            out->key     = e.key.keysym.sym;
            out->pressed = (e.type == SDL_KEYDOWN);
            return true;
        default:
            out->type = PLATFORM_EV_NONE;
            return true;
    }
}

uint64_t platform_millis(void) {
    return SDL_GetTicks64() - s_start_ms;
}

void platform_sleep_ms(uint32_t ms) {
    SDL_Delay(ms);
}

// ---------------------------------------------------------------------------
// Host entry point
// ---------------------------------------------------------------------------
extern void app_run(void);

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    app_run();
    return 0;
}
