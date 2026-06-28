// platform_device.c — Tanmatsu HAL backend (ESP-IDF + badge-bsp).
//
// One of two implementations of platform.h (the other is platform/host/).
// BSP owns the display and input; we run a pinned high-priority task that pulls
// stereo float from the engine and writes int16 to the I2S DAC. Nothing above
// the membrane sees any of these headers — they live only here.
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "bsp/audio.h"
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp/power.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform.h"
#include "sdkconfig.h"

static const char TAG[] = "platform";

// Largest audio block we will be asked to render; sized once, used by the audio
// task's preallocated buffers so the real-time path never allocates.
#define MAX_BLOCK 256

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
static pax_buf_t s_fb           = {0};
static size_t    s_h_res        = 0;
static size_t    s_v_res        = 0;
static bool      s_have_display = false;

static pax_buf_type_t bsp_to_pax_format(bsp_display_color_format_t fmt) {
    switch (fmt) {
        case BSP_DISPLAY_COLOR_FORMAT_16_565RGB:
            return PAX_BUF_16_565RGB;
        case BSP_DISPLAY_COLOR_FORMAT_32_8888ARGB:
            return PAX_BUF_32_8888ARGB;
        case BSP_DISPLAY_COLOR_FORMAT_24_888RGB:
        default:
            return PAX_BUF_24_888RGB;
    }
}

static pax_orientation_t bsp_to_pax_orientation(bsp_display_rotation_t rot) {
    switch (rot) {
        case BSP_DISPLAY_ROTATION_90:
            return PAX_O_ROT_CCW;
        case BSP_DISPLAY_ROTATION_180:
            return PAX_O_ROT_HALF;
        case BSP_DISPLAY_ROTATION_270:
            return PAX_O_ROT_CW;
        case BSP_DISPLAY_ROTATION_0:
        default:
            return PAX_O_UPRIGHT;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static QueueHandle_t s_input_queue = NULL;

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static i2s_chan_handle_t        s_i2s         = NULL;
static TaskHandle_t             s_audio_task  = NULL;
static _Atomic bool             s_audio_run   = false;
static _Atomic bool             s_audio_done  = true;  // task has exited its loop
static platform_audio_render_fn s_render      = NULL;
static void*                    s_render_user = NULL;
static size_t                   s_block       = 0;

static float   s_left[MAX_BLOCK];
static float   s_right[MAX_BLOCK];
static int16_t s_interleaved[MAX_BLOCK * 2];

static inline int16_t to_i16(float v) {
    // A single NaN/Inf escaping upstream DSP must never reach the DAC (a loud
    // pop, or garbage from the cast). Fail safe in the audio path (CLAUDE.md).
    if (!isfinite(v)) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * 32767.0f);
}

static void audio_task(void* arg) {
    (void)arg;
    const size_t n = s_block;
    while (atomic_load(&s_audio_run)) {
        s_render(s_left, s_right, n, s_render_user);
        for (size_t i = 0; i < n; i++) {
            s_interleaved[i * 2 + 0] = to_i16(s_left[i]);
            s_interleaved[i * 2 + 1] = to_i16(s_right[i]);
        }
        size_t written = 0;
        // Blocking write on the DMA queue is the real-time deadline mechanism.
        i2s_channel_write(s_i2s, s_interleaved, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    }
    // Flush one block of silence so the DMA's last buffer isn't a stale tone the
    // codec would replay until the channel is disabled (platform_audio_stop does
    // that right after we exit).
    memset(s_interleaved, 0, n * 2 * sizeof(int16_t));
    size_t written = 0;
    i2s_channel_write(s_i2s, s_interleaved, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
    atomic_store(&s_audio_done, true);
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool platform_init(void) {
    gpio_install_isr_service(0);

    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        res = nvs_flash_init();
    }
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", res);
        return false;
    }

    const bsp_configuration_t cfg = {
        .display = {.requested_color_format = BSP_DISPLAY_COLOR_FORMAT_24_888RGB, .num_fbs = 1},
    };
    if (bsp_device_initialize(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "BSP init failed");
        return false;
    }

    bsp_display_color_format_t color_format;
    bsp_display_endianness_t   endian;
    res = bsp_display_get_parameters(&s_h_res, &s_v_res, &color_format, &endian);
    if (res == ESP_OK) {
        pax_buf_init(&s_fb, NULL, s_h_res, s_v_res, bsp_to_pax_format(color_format));
        pax_buf_reversed(&s_fb, endian == BSP_DISPLAY_ENDIAN_BIG);
        pax_buf_set_orientation(&s_fb, bsp_to_pax_orientation(bsp_display_get_default_rotation()));
        s_have_display = true;
    } else if (res != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "display params failed: %d", res);
        return false;
    }

    if (bsp_input_get_queue(&s_input_queue) != ESP_OK) {
        ESP_LOGW(TAG, "no input queue");
        s_input_queue = NULL;
    }
    return true;
}

pax_buf_t* platform_framebuffer(void) {
    return s_have_display ? &s_fb : NULL;
}

void platform_present(void) {
    if (!s_have_display) return;
    bsp_display_blit(0, 0, s_h_res, s_v_res, pax_buf_get_pixels(&s_fb));
}

bool platform_audio_start(const platform_audio_config_t* cfg, platform_audio_render_fn render, void* user) {
    if (cfg->block_size > MAX_BLOCK) {
        ESP_LOGE(TAG, "block_size %u exceeds MAX_BLOCK %u", (unsigned)cfg->block_size, MAX_BLOCK);
        return false;
    }
    if (bsp_audio_get_i2s_handle(&s_i2s) != ESP_OK || s_i2s == NULL) {
        ESP_LOGE(TAG, "no I2S handle");
        return false;
    }

    // Honor the requested rate: the BSP enables the channel at init, but the
    // clock can only be reconfigured while the channel is disabled.
    i2s_channel_disable(s_i2s);
    bsp_audio_set_rate(cfg->sample_rate);
    i2s_channel_enable(s_i2s);

    bsp_audio_set_volume(80.0f);
    bsp_audio_set_amplifier(true);  // headphone detection still routes correctly

    s_render      = render;
    s_render_user = user;
    s_block       = cfg->block_size;
    atomic_store(&s_audio_run, true);
    atomic_store(&s_audio_done, false);

    // Pin to the app core (1), leaving core 0 for UI/MIDI/SD. High priority so
    // the DMA queue never starves.
    BaseType_t ok =
        xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, configMAX_PRIORITIES - 2, &s_audio_task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        atomic_store(&s_audio_run, false);
        return false;
    }
    return true;
}

void platform_exit_to_launcher(void) {
    // Clears the AppFS one-shot magic and reboots into the launcher (badge-bsp).
    bsp_device_restart_to_launcher();
}

void platform_audio_stop(void) {
    if (s_audio_task == NULL) return;  // not running
    atomic_store(&s_audio_run, false);
    // Wait for the task to finish its current write, flush silence, and exit
    // (bounded — it may be blocked in one final block-write, ~1.3 ms at 48 k).
    for (int i = 0; i < 50 && !atomic_load(&s_audio_done); i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    // Stop the DMA so the codec doesn't replay the last buffer forever (sounds
    // like a crash) and drop the amplifier so nothing bleeds through.
    if (s_i2s != NULL) {
        i2s_channel_disable(s_i2s);
    }
    bsp_audio_set_amplifier(false);
    s_audio_task = NULL;
}

// Translate a (release-bit-masked) BSP scancode to the lowercase ASCII the
// portable control layer expects. Only the musical-typing keys matter; anything
// else returns 0 (filtered out by the caller). Scancodes carry press/release
// state — unlike the ASCII KEYBOARD event, which only fires on press — so we
// drive note on/off from these. See bsp/input.h.
static int scancode_to_ascii(bsp_input_scancode_t sc) {
    switch (sc) {
        case BSP_INPUT_SCANCODE_A:         return 'a';
        case BSP_INPUT_SCANCODE_W:         return 'w';
        case BSP_INPUT_SCANCODE_S:         return 's';
        case BSP_INPUT_SCANCODE_E:         return 'e';
        case BSP_INPUT_SCANCODE_D:         return 'd';
        case BSP_INPUT_SCANCODE_F:         return 'f';
        case BSP_INPUT_SCANCODE_T:         return 't';
        case BSP_INPUT_SCANCODE_G:         return 'g';
        case BSP_INPUT_SCANCODE_Y:         return 'y';
        case BSP_INPUT_SCANCODE_H:         return 'h';
        case BSP_INPUT_SCANCODE_U:         return 'u';
        case BSP_INPUT_SCANCODE_J:         return 'j';
        case BSP_INPUT_SCANCODE_K:         return 'k';
        case BSP_INPUT_SCANCODE_O:         return 'o';
        case BSP_INPUT_SCANCODE_L:         return 'l';
        case BSP_INPUT_SCANCODE_P:         return 'p';
        case BSP_INPUT_SCANCODE_SEMICOLON: return ';';
        case BSP_INPUT_SCANCODE_Z:         return 'z';
        case BSP_INPUT_SCANCODE_X:         return 'x';
        default:                           return 0;
    }
}

bool platform_poll_event(platform_event_t* out) {
    if (!s_input_queue) return false;
    bsp_input_event_t ev;
    if (xQueueReceive(s_input_queue, &ev, 0) != pdTRUE) {
        return false;
    }
    // Drive keys from SCANCODE events: they carry make/break state (the high
    // 0x80 bit = release). The BSP also emits a press-only ASCII KEYBOARD event
    // per key — we ignore it here so a press isn't counted twice.
    if (ev.type == INPUT_EVENT_TYPE_SCANCODE) {
        bool                 released = (ev.args_scancode.scancode & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != 0;
        bsp_input_scancode_t base = (bsp_input_scancode_t)(ev.args_scancode.scancode & ~BSP_INPUT_SCANCODE_RELEASE_MODIFIER);
        if (base == BSP_INPUT_SCANCODE_ESC && !released) {
            // ESC quits to the launcher — symmetric with the host (SDL ESC).
            out->type = PLATFORM_EV_QUIT;
        } else {
            int ascii = scancode_to_ascii(base);
            if (ascii == 0) {
                out->type = PLATFORM_EV_NONE;
            } else {
                out->type    = PLATFORM_EV_KEY;
                out->key     = ascii;
                out->pressed = !released;
            }
        }
    } else {
        out->type = PLATFORM_EV_NONE;
    }
    return true;
}

uint64_t platform_millis(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

void platform_sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ---------------------------------------------------------------------------
// Cycle counter (0.5a)
// ---------------------------------------------------------------------------
uint64_t platform_cycles_now(void) {
    // esp_cpu_get_cycle_count() wraps at 2^32; callers must take differences
    // within a few milliseconds to avoid wrap (a 64-sample block at 400 MHz is
    // ~533 k cycles, safely within 32 bits).
    return (uint64_t)esp_cpu_get_cycle_count();
}

uint32_t platform_cycles_per_sec(void) {
    // CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ is set in sdkconfig for the P4 target
    // (400 MHz by default). Using the compile-time constant avoids pulling in
    // private clock headers and is correct for our fixed-frequency use case.
    return (uint32_t)(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000UL);
}
