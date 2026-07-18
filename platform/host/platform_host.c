// platform_host.c — desktop HAL backend (SDL2 + miniaudio).
//
// One of two implementations of platform.h (the other is platform/device/).
// SDL2 owns the window, present, and input; miniaudio owns the audio thread.
// Nothing above the membrane sees either library — they live only here.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "miniaudio.h"
#include "pax_internal.h"  // pax_get_index_conv: native-format pixel -> ARGB
#include "platform.h"

// Match the device panel so UI/UX built on the Mac transfers 1:1 (spec 04).
#define WIN_W 800
#define WIN_H 480

// The shared UI renders into a framebuffer in the device panel's native format
// (ADR 0011: the engine/UI use the P4-optimal representation; the host pays the
// conversion tax). This mirrors the format the device backend requests from the
// BSP — keep the two in sync if the panel format ever changes.
#define HOST_FB_FORMAT PAX_BUF_24_888RGB

// Largest chunk we hand to the engine per render call inside the audio callback.
// A callback may exceed the requested period size. Split it without allocation
// so the engine's one-publish-per-call recording block stays bounded (ADR 0024).
#define MAX_CHUNK 64

static SDL_Window*   s_window   = NULL;
static SDL_Renderer* s_renderer = NULL;
static SDL_Texture*  s_texture  = NULL;
static pax_buf_t     s_fb       = {0};

static ma_device                s_device;
static bool                     s_audio_running = false;
static platform_audio_render_fn s_render        = NULL;
static void*                    s_render_user   = NULL;
static _Atomic uint32_t         s_volume_pct    = 100u;

static uint64_t s_start_ms = 0;

static const char* s_sd_root      = "./sd";
static bool        s_sd_available = false;

static void ensure_sd_root(void) {
    struct stat st;
    if (stat(s_sd_root, &st) != 0) {
        if (errno != ENOENT || mkdir(s_sd_root, 0755) != 0) {
            SDL_Log("SD root unavailable at %s: %s", s_sd_root, strerror(errno));
            return;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        SDL_Log("SD root is not a directory: %s", s_sd_root);
        return;
    }
    if (access(s_sd_root, R_OK | W_OK | X_OK) != 0) {
        SDL_Log("SD root is not usable at %s: %s", s_sd_root, strerror(errno));
        return;
    }
    s_sd_available = true;
}

// Staging buffer: the panel-native framebuffer converted to the ARGB8888 the SDL
// texture wants. Filled every present(); host-only, so a static buffer is fine.
static uint32_t s_present[WIN_W * WIN_H];

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
    const float  volume = (float)atomic_load(&s_volume_pct) / 100.0f;

    ma_uint32 done = 0;
    while (done < frame_count) {
        ma_uint32 n = frame_count - done;
        if (n > MAX_CHUNK) {
            n = MAX_CHUNK;
        }
        s_render(left, right, n, s_render_user);
        for (ma_uint32 i = 0; i < n; i++) {
            out[(done + i) * 2 + 0] = left[i] * volume;
            out[(done + i) * 2 + 1] = right[i] * volume;
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

    // Render in the device-native format (ADR 0011); present() converts to the
    // SDL texture format, so the sim shows the same color depth as the panel.
    pax_buf_init(&s_fb, NULL, WIN_W, WIN_H, HOST_FB_FORMAT);

    ensure_sd_root();
    s_start_ms = SDL_GetTicks64();
    return true;
}

pax_buf_t* platform_framebuffer(void) {
    return &s_fb;
}

void platform_display_stop(void) {
    // Device-only diagnostic (SYNTH_QUIET_DISPLAY); nothing to tear down on host.
}

void platform_present(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > WIN_H) y1 = WIN_H;
    if (y0 >= y1) return;

    // Convert only the dirty band to ARGB8888 (host pays the tax, ADR 0011;
    // ADR 0022 narrows it to [y0,y1)). pax_get_index_conv normalizes whatever
    // the buffer format is, so the result matches the device's on-screen
    // color exactly.
    for (int i = y0 * WIN_W; i < y1 * WIN_W; i++) {
        s_present[i] = 0xFF000000u | pax_get_index_conv(&s_fb, i);
    }
    // The texture retains previously-updated rows outside the band, so
    // updating just this sub-rect and re-copying the whole texture to the
    // window is correct and cheap (GPU blit, not the PSRAM lever ADR 0022
    // targets).
    SDL_Rect r = {0, y0, WIN_W, y1 - y0};
    SDL_UpdateTexture(s_texture, &r, s_present + (size_t)y0 * WIN_W, WIN_W * 4);
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

uint32_t platform_audio_volume_get(void) {
    return atomic_load(&s_volume_pct);
}

bool platform_audio_volume_set(uint32_t pct) {
    if (pct > 100u) pct = 100u;
    atomic_store(&s_volume_pct, pct);
    return true;
}

void platform_exit_to_launcher(void) {
    // No launcher on the host — just leave the process.
    exit(0);
}

bool platform_poll_event(platform_event_t* out) {
    SDL_Event e;
    if (!SDL_PollEvent(&e)) {
        return false;
    }
    out->mods = 0;
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
            if (e.type == SDL_KEYDOWN && e.key.repeat) {
                // Allow auto-repeat for navigation keys (arrows, comma, dot) so
                // the user can hold them for continuous scroll/nudge. Filter
                // everything else to prevent musical-key retriggering.
                SDL_Keycode sym = e.key.keysym.sym;
                int         is_nav =
                    (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT || sym == SDLK_COMMA ||
                     sym == SDLK_PERIOD || sym == SDLK_LEFTBRACKET || sym == SDLK_RIGHTBRACKET);
                if (!is_nav) {
                    out->type = PLATFORM_EV_NONE;
                    return true;
                }
            }
            {
                // Map SDL navigation keysyms to PLATFORM_KEY_* constants.
                int key = e.key.keysym.sym;
                switch (key) {
                    case SDLK_UP:
                        key = PLATFORM_KEY_UP;
                        break;
                    case SDLK_DOWN:
                        key = PLATFORM_KEY_DOWN;
                        break;
                    case SDLK_LEFT:
                        key = PLATFORM_KEY_LEFT;
                        break;
                    case SDLK_RIGHT:
                        key = PLATFORM_KEY_RIGHT;
                        break;
                    case SDLK_VOLUMEUP:
                        key = PLATFORM_KEY_VOLUME_UP;
                        break;
                    case SDLK_VOLUMEDOWN:
                        key = PLATFORM_KEY_VOLUME_DOWN;
                        break;
                    // Shape buttons (F1–F6): map to PLATFORM_KEY_F1–F6.
                    // Auto-repeat is already blocked for non-nav keys above, so
                    // only real down/up edges reach here.
                    case SDLK_F1:
                        key = PLATFORM_KEY_F1;
                        break;
                    case SDLK_F2:
                        key = PLATFORM_KEY_F2;
                        break;
                    case SDLK_F3:
                        key = PLATFORM_KEY_F3;
                        break;
                    case SDLK_F4:
                        key = PLATFORM_KEY_F4;
                        break;
                    case SDLK_F5:
                        key = PLATFORM_KEY_F5;
                        break;
                    case SDLK_F6:
                        key = PLATFORM_KEY_F6;
                        break;
                    default:
                        break;
                }
                SDL_Keymod mod = SDL_GetModState();
                out->type      = PLATFORM_EV_KEY;
                out->key       = key;
                out->pressed   = (e.type == SDL_KEYDOWN);
                out->mods      = ((mod & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0) ? PLATFORM_MOD_SHIFT : 0;
            }
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
// Cycle counter (0.5a) — pseudo-1 GHz on host; device numbers are the budget
// ---------------------------------------------------------------------------
uint64_t platform_cycles_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint32_t platform_cycles_per_sec(void) {
    return 1000000000u;  // 1 GHz pseudo-clock matching the ns timebase above
}

uint64_t platform_instret_now(void) {
    // Stage 8 diag: no retired-instruction counter on host; the discriminator
    // (IPC = instret/cycles) is device-only. Returning 0 is documented and
    // acceptable -- the readout will show ipc=0 rather than anything wrong.
    return 0;
}

// ---------------------------------------------------------------------------
// Storage (Stage 2d) — POSIX file-per-key under ./presets/
// ---------------------------------------------------------------------------
static const char* s_preset_dir = "presets";

static void ensure_preset_dir(void) {
    struct stat st;
    if (stat(s_preset_dir, &st) != 0) {
        mkdir(s_preset_dir, 0755);
    }
}

int platform_storage_save(const char* key, const void* data, size_t len) {
    ensure_preset_dir();
    char path[256];
    snprintf(path, sizeof(path), "%s/%.230s.tnp", s_preset_dir, key);
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

int platform_storage_load(const char* key, void* buf, size_t max_len) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%.230s.tnp", s_preset_dir, key);
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, max_len, f);
    fclose(f);
    return (int)n;
}

bool platform_sd_available(void) {
    return s_sd_available;
}

const char* platform_sd_root(void) {
    return s_sd_root;
}

bool platform_sd_preallocate(const char* path, uint64_t size) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return false;
    bool ok = ftruncate(fd, (off_t)size) == 0;
    if (close(fd) != 0) ok = false;
    if (!ok) {
        unlink(path);
    }
    return ok;
}

void* platform_sd_alloc_io_buffer(size_t size) {
    return malloc(size);
}

void platform_sd_free_io_buffer(void* ptr) {
    free(ptr);
}

// ---------------------------------------------------------------------------
// Audio-block cycle profiler read — host stub
// ---------------------------------------------------------------------------
// The host has no dedicated audio task and no cycle-budget pressure; always
// returns zeros so callers compile and link on both targets.
void platform_audio_profile_read(uint32_t* out_avg_cyc, uint32_t* out_max_cyc, uint32_t* out_over,
                                 uint32_t* out_count) {
    *out_avg_cyc = 0;
    *out_max_cyc = 0;
    *out_over    = 0;
    *out_count   = 0;
}

void platform_audio_i2s_profile_read(platform_audio_i2s_profile_t* out) {
    *out = (platform_audio_i2s_profile_t){0};
}

// ---------------------------------------------------------------------------
// Render task (input-latency fix) — host stubs
// ---------------------------------------------------------------------------
// SDL must render on the main thread; returning false lets app.c call
// render_cb inline from the control loop instead of spawning a task.
bool platform_render_task_start(void (*render_cb)(void* ctx), void* ctx, uint32_t render_ms) {
    (void)render_cb;
    (void)ctx;
    (void)render_ms;
    return false;
}

void platform_render_task_stop(void) {
    // no-op on host
}

// ---------------------------------------------------------------------------
// Storage worker
// ---------------------------------------------------------------------------
static pthread_t   s_storage_thread;
static atomic_bool s_storage_done      = true;
static bool        s_storage_joinable  = false;
static void (*s_storage_cb)(void* ctx) = NULL;
static void* s_storage_ctx             = NULL;

static void* storage_thread(void* arg) {
    (void)arg;
    s_storage_cb(s_storage_ctx);
    s_storage_done = true;
    return NULL;
}

bool platform_storage_worker_start(void (*storage_cb)(void* ctx), void* ctx) {
    if (storage_cb == NULL || !s_storage_done || s_storage_joinable) return false;

    s_storage_cb   = storage_cb;
    s_storage_ctx  = ctx;
    s_storage_done = false;
    int err        = pthread_create(&s_storage_thread, NULL, storage_thread, NULL);
    if (err != 0) {
        SDL_Log("storage worker create failed: %s", strerror(err));
        s_storage_done = true;
        return false;
    }
    s_storage_joinable = true;
    return true;
}

bool platform_storage_worker_stop(void) {
    if (!s_storage_joinable) return s_storage_done;

    for (int i = 0; i < 50 && !s_storage_done; i++) {
        usleep(2000);
    }
    if (!s_storage_done) {
        SDL_Log("storage worker did not stop within 100 ms");
        return false;
    }

    int err = pthread_join(s_storage_thread, NULL);
    if (err != 0) {
        SDL_Log("storage worker join failed: %s", strerror(err));
        return false;
    }
    s_storage_joinable = false;
    return true;
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
