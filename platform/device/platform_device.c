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
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform.h"
#include "sdkconfig.h"
// Stage 5d: USB-C MIDI device (PHY swap + TinyUSB); owns platform_midi_read.
// Stage 5b: USB-A host MIDI (both builds; host-only in SYNTH_USB_HOST_DEBUG).
#include "midi_usb_device.h"
#include "midi_usb_host.h"

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
// Bytes per pixel of s_fb, set once at init from the chosen panel format (ADR
// 0022): the dirty-band present needs this to offset into the framebuffer by
// row, and the format is panel-dependent so it must not be hardcoded.
static size_t    s_fb_bpp       = 0;

// Present-side breakeven and scratch buffers (ADR 0023): the panel is
// portrait-native (s_h_res x s_v_res raw = 480 x 800) but PAX rotates the UI
// 270 degrees into it, so a logical scanline band maps to a raw COLUMN range,
// not a contiguous row range. Narrow column windows are packed into a
// scratch buffer and blitted; wide ones (>= half the logical width) are
// cheaper to blit whole. See platform_present() for the derivation.
#define BAND_PACK_THRESHOLD 240u  // logical px; = s_h_res / 2, the breakeven width
static uint8_t* s_scratch[2]   = {NULL, NULL};
static int      s_scratch_next = 0;

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

static size_t pax_format_bpp(pax_buf_type_t fmt) {
    switch (fmt) {
        case PAX_BUF_16_565RGB:
            return 2;
        case PAX_BUF_32_8888ARGB:
            return 4;
        case PAX_BUF_24_888RGB:
        default:
            return 3;
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
// Audio-block cycle profiler (SYNTH_PROFILE only)
// ---------------------------------------------------------------------------
// Budget derivation: 360 MHz × 64 frames / 48000 Hz = 480 000 cycles/block.
// These counters measure the render COMPUTE only; the i2s_channel_write
// DMA-wait is excluded by design — it IS the real-time deadline mechanism.
#ifdef SYNTH_PROFILE
static const uint32_t    kAudioBlockBudgetCyc = 480000u;  // 360 MHz × 64 / 48000
static volatile uint64_t s_ab_sum             = 0;
static volatile uint32_t s_ab_count           = 0;
static volatile uint32_t s_ab_max             = 0;
static volatile uint32_t s_ab_over            = 0;
#endif

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
#ifdef SYNTH_PROFILE
        uint32_t cyc_start = esp_cpu_get_cycle_count();
#endif
        s_render(s_left, s_right, n, s_render_user);
#ifdef SYNTH_PROFILE
        uint32_t dc = esp_cpu_get_cycle_count() - cyc_start;
        s_ab_sum   += dc;
        s_ab_count++;
        if (dc > s_ab_max) s_ab_max = dc;
        if (dc > kAudioBlockBudgetCyc) s_ab_over++;
#endif
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
        pax_buf_type_t pax_fmt = bsp_to_pax_format(color_format);
        pax_buf_init(&s_fb, NULL, s_h_res, s_v_res, pax_fmt);
        s_fb_bpp = pax_format_bpp(pax_fmt);
        pax_buf_reversed(&s_fb, endian == BSP_DISPLAY_ENDIAN_BIG);
        pax_buf_set_orientation(&s_fb, bsp_to_pax_orientation(bsp_display_get_default_rotation()));
        s_have_display = true;

        // Two PSRAM scratch buffers for the pack-and-blit present path (ADR
        // 0023), sized for the widest column window the pack path ever
        // handles (BAND_PACK_THRESHOLD logical px, raw column window of the
        // same width, full raw height). Allocated once here, not per-present
        // (no allocation in the present/audio path). If either allocation
        // fails, platform_present() falls back to full-screen blits only —
        // slower, but still correct — rather than leaving the display dead.
        size_t scratch_bytes = (size_t)BAND_PACK_THRESHOLD * s_v_res * s_fb_bpp;
        for (int i = 0; i < 2; i++) {
            s_scratch[i] = heap_caps_malloc(scratch_bytes, MALLOC_CAP_SPIRAM);
            if (s_scratch[i] == NULL) {
                ESP_LOGE(TAG,
                         "present scratch buffer %d alloc failed (%u bytes) -- "
                         "falling back to full-blit-only present",
                         i, (unsigned)scratch_bytes);
            }
        }
    } else if (res != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "display params failed: %d", res);
        return false;
    }

    if (bsp_input_get_queue(&s_input_queue) != ESP_OK) {
        ESP_LOGW(TAG, "no input queue");
        s_input_queue = NULL;
    }

    // USB init:
    //   SYNTH_USB_HOST_DEBUG (make build USBHOST_DEBUG=1): USB-A host only;
    //     USB-C console stays alive for debugging (no PHY swap).
    //   Normal build: USB-C device (Stage 5d, TinyUSB MIDI, console detached)
    //     + USB-A host (Stage 5b) — independent HS/FS controllers, coexist.
    // Done last so display/input/audio are already up before the 500 ms PHY
    // disconnect delay fires.
#ifdef SYNTH_USB_HOST_DEBUG
    midi_usb_host_init();  // host only; USB-C console stays alive
#else
    midi_usb_device_init();  // USB-C device (5d): PHY swap + TinyUSB
    midi_usb_host_init();    // USB-A host   (5b): independent HS controller
#endif

#ifdef SYNTH_PROFILE
    // Stage 8 diag: the RV32 HP-core cycle/instret counters can be gated by
    // mcountinhibit (CSR 0x320): bit 0 = cycle, bit 2 = instret. Clear both
    // once here so platform_instret_now() (and, defensively, mcycle) actually
    // count. Diagnostic-only -- not present in shipping builds.
    {
        uint32_t inhibit;
        __asm__ volatile("csrr %0, 0x320" : "=r"(inhibit));
        inhibit &= ~((1u << 0) | (1u << 2));
        __asm__ volatile("csrw 0x320, %0" ::"r"(inhibit));
    }
#endif

    return true;
}

pax_buf_t* platform_framebuffer(void) {
    return s_have_display ? &s_fb : NULL;
}

void platform_display_stop(void) {
    // Diagnostic bus-quiet baseline (SYNTH_QUIET_DISPLAY). The Tanmatsu panel is
    // MIPI-DSI DPI (video mode): the DSI bridge continuously DMA-streams the
    // whole framebuffer out of PSRAM at the refresh rate, forever, independent
    // of any CPU repaint. FREEZE_DISPLAY only skips the repaint/blit, so that
    // scanout keeps loading the memory bus. Deleting the panel stops the DPI
    // DMA outright — the only way to make the bus truly quiet without a BSP fork.
    //
    // Called once from the render task after the first frame is on screen; the
    // pax framebuffer (s_fb) is a separate buffer and stays valid, so clearing
    // s_have_display first makes platform_present()/framebuffer() no-op safely.
    if (!s_have_display) return;
    s_have_display               = false;
    esp_lcd_panel_handle_t panel = NULL;
    if (bsp_display_get_panel(&panel) == ESP_OK && panel != NULL) {
        esp_lcd_panel_del(panel);
        ESP_LOGW(TAG, "QUIET_DISPLAY: DPI panel torn down, scanout DMA stopped");
    }
}

void platform_present(int y0, int y1) {
    if (!s_have_display) return;

    // The present seam speaks LOGICAL UI scanlines (ADR 0023 §1). The raw
    // framebuffer is s_h_res x s_v_res (480 x 800, portrait-native panel);
    // PAX rotates the 800x480 landscape UI into it (PAX_O_ROT_CW), so the
    // logical height equals the raw WIDTH (s_h_res), not the raw height.
    // Derive the clamp from s_h_res rather than hardcode 480.
    int logical_h = (int)s_h_res;
    if (y0 < 0) y0 = 0;
    if (y1 > logical_h) y1 = logical_h;
    if (y0 >= y1) return;

    // PAX_O_ROT_CW maps logical (x, y) -> raw (s_h_res - y, x)
    // (pax_orientation.c: pax_orient_ccw3_vec2f), so a logical scanline band
    // [y0, y1) is a raw COLUMN range [X0, X1), not a row range (ADR 0023 RC1).
    int x0 = logical_h - y1;
    int x1 = logical_h - y0;
    if (x0 < 0) x0 = 0;
    if (x1 > (int)s_h_res) x1 = (int)s_h_res;
    if (x0 >= x1) return;
    size_t w = (size_t)(x1 - x0);

    const uint8_t* pix = (const uint8_t*)pax_buf_get_pixels(&s_fb);

    // Full-blit fallback at the breakeven width, or if the scratch buffers
    // failed to allocate. Breakeven math (ADR 0023): the pack path moves
    // ~4*w*s_v_res*bpp bytes (pack read + pack write + draw-bitmap read +
    // panel-fb write); the full path moves ~2*s_h_res*s_v_res*bpp (read +
    // write). Equal at w = s_h_res/2 = BAND_PACK_THRESHOLD.
    bool scratch_ok = s_scratch[0] != NULL && s_scratch[1] != NULL;
    if (w >= BAND_PACK_THRESHOLD || !scratch_ok) {
        // bsp_display_blit's 3rd/4th args are END-EXCLUSIVE coordinates
        // (x_end, y_end) on the tanmatsu target -- it forwards straight to
        // esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end,
        // buf), NOT (width, height) as bsp/display.h's doc comment claims
        // (ADR 0023 RC2 -- upstream doc/impl mismatch, flagged for a
        // badge-bsp fix). Here x_end=s_h_res and y_end=s_v_res are also the
        // full width/height, so this call is correct either reading.
        bsp_display_blit(0, 0, s_h_res, s_v_res, pix);
        return;
    }

    // Pack path: copy the raw column window [x0, x1) into a scratch buffer,
    // one raw row at a time, then blit just that column strip. Two scratch
    // buffers, alternated per call: use_dma2d=true makes bsp_display_blit's
    // draw_bitmap async, so the flush semaphore inside it only serializes
    // the *next* blit call, not our repacking of a buffer the in-flight
    // DMA2D transfer may still be reading (ADR 0023).
    uint8_t* scratch = s_scratch[s_scratch_next];
    s_scratch_next   = 1 - s_scratch_next;
    for (size_t r = 0; r < s_v_res; r++) {
        memcpy(scratch + r * w * s_fb_bpp, pix + (r * s_h_res + (size_t)x0) * s_fb_bpp, w * s_fb_bpp);
    }
    // bsp_display_blit(x_start, y_start, x_end, y_end, buffer) -- end-exclusive
    // coordinates, see the RC2 comment above.
    bsp_display_blit((size_t)x0, 0, (size_t)x1, s_v_res, scratch);
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

// Translate a (release-bit-masked) BSP scancode to the key code the portable
// control layer expects. Musical-typing keys return their lowercase ASCII value;
// comma and dot return their ASCII values for UI nudge; anything else returns 0.
static int scancode_to_key(bsp_input_scancode_t sc) {
    switch (sc) {
        case BSP_INPUT_SCANCODE_A:
            return 'a';
        case BSP_INPUT_SCANCODE_W:
            return 'w';
        case BSP_INPUT_SCANCODE_S:
            return 's';
        case BSP_INPUT_SCANCODE_E:
            return 'e';
        case BSP_INPUT_SCANCODE_D:
            return 'd';
        case BSP_INPUT_SCANCODE_F:
            return 'f';
        case BSP_INPUT_SCANCODE_T:
            return 't';
        case BSP_INPUT_SCANCODE_G:
            return 'g';
        case BSP_INPUT_SCANCODE_Y:
            return 'y';
        case BSP_INPUT_SCANCODE_H:
            return 'h';
        case BSP_INPUT_SCANCODE_U:
            return 'u';
        case BSP_INPUT_SCANCODE_J:
            return 'j';
        case BSP_INPUT_SCANCODE_K:
            return 'k';
        case BSP_INPUT_SCANCODE_O:
            return 'o';
        case BSP_INPUT_SCANCODE_L:
            return 'l';
        case BSP_INPUT_SCANCODE_P:
            return 'p';
        case BSP_INPUT_SCANCODE_SEMICOLON:
            return ';';
        case BSP_INPUT_SCANCODE_Z:
            return 'z';
        case BSP_INPUT_SCANCODE_X:
            return 'x';
        case BSP_INPUT_SCANCODE_COMMA:
            return ',';
        case BSP_INPUT_SCANCODE_DOT:
            return '.';
        case BSP_INPUT_SCANCODE_LEFTBRACE:
            return '[';
        case BSP_INPUT_SCANCODE_RIGHTBRACE:
            return ']';
        case BSP_INPUT_SCANCODE_EQUAL:
            return '=';
        case BSP_INPUT_SCANCODE_SPACE:
            return ' ';  // manual tap-freeze key under SYNTH_PROFILE (crackle forensics)
        default:
            return 0;
    }
}

// Shift key held state — tracked from SCANCODE press/release events so that
// comma/dot nudge events carry the correct modifier flag.
static bool s_shift_held = false;

bool platform_poll_event(platform_event_t* out) {
    if (!s_input_queue) return false;
    bsp_input_event_t ev;
    if (xQueueReceive(s_input_queue, &ev, 0) != pdTRUE) {
        return false;
    }
    out->mods = 0;

    if (ev.type == INPUT_EVENT_TYPE_SCANCODE) {
        // Drive keys from SCANCODE events: they carry make/break state (the high
        // 0x80 bit = release). The BSP also emits a press-only ASCII KEYBOARD
        // event per key — we ignore it here so a press isn't counted twice.
        bool                 released = (ev.args_scancode.scancode & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != 0;
        bsp_input_scancode_t base =
            (bsp_input_scancode_t)(ev.args_scancode.scancode & ~BSP_INPUT_SCANCODE_RELEASE_MODIFIER);

        // Track shift key state for modifying comma/dot nudge.
        if (base == BSP_INPUT_SCANCODE_LEFTSHIFT || base == BSP_INPUT_SCANCODE_RIGHTSHIFT) {
            s_shift_held = !released;
            out->type    = PLATFORM_EV_NONE;
            return true;
        }

        if (base == BSP_INPUT_SCANCODE_ESC && !released) {
            out->type = PLATFORM_EV_QUIT;
        } else {
            int key = scancode_to_key(base);
            if (key == 0) {
                out->type = PLATFORM_EV_NONE;
            } else {
                out->type    = PLATFORM_EV_KEY;
                out->key     = key;
                out->pressed = !released;
                out->mods    = s_shift_held ? PLATFORM_MOD_SHIFT : 0;
            }
        }
    } else if (ev.type == INPUT_EVENT_TYPE_NAVIGATION) {
        // Navigation events carry arrow keys, modifiers, and press/release state.
        const bsp_input_event_args_navigation_t* nav        = &ev.args_navigation;
        int                                      mapped_key = 0;
        switch (nav->key) {
            case BSP_INPUT_NAVIGATION_KEY_UP:
                mapped_key = PLATFORM_KEY_UP;
                break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                mapped_key = PLATFORM_KEY_DOWN;
                break;
            case BSP_INPUT_NAVIGATION_KEY_LEFT:
                mapped_key = PLATFORM_KEY_LEFT;
                break;
            case BSP_INPUT_NAVIGATION_KEY_RIGHT:
                mapped_key = PLATFORM_KEY_RIGHT;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F1:
                mapped_key = PLATFORM_KEY_F1;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F2:
                mapped_key = PLATFORM_KEY_F2;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F3:
                mapped_key = PLATFORM_KEY_F3;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F4:
                mapped_key = PLATFORM_KEY_F4;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F5:
                mapped_key = PLATFORM_KEY_F5;
                break;
            case BSP_INPUT_NAVIGATION_KEY_F6:
                mapped_key = PLATFORM_KEY_F6;
                break;
            default:
                out->type = PLATFORM_EV_NONE;
                return true;
        }
        out->type    = PLATFORM_EV_KEY;
        out->key     = mapped_key;
        out->pressed = nav->state;
        out->mods    = ((nav->modifiers & BSP_INPUT_MODIFIER_SHIFT) != 0) ? PLATFORM_MOD_SHIFT : 0;
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
// Storage (Stage 2d) — NVS blob per key under namespace "synth_p"
// ---------------------------------------------------------------------------
// NVS is already initialised in platform_init() via nvs_flash_init().
// Key names are limited to 15 characters; we truncate silently.
// Blobs up to ~32 KB each; our presets are ~126 bytes — well within budget.
#include "nvs.h"

#define STORAGE_NVS_NS "synth_p"

static void nvs_truncate_key(const char* key, char* out, size_t out_max) {
    size_t i;
    for (i = 0; i < out_max - 1 && key[i]; i++) out[i] = key[i];
    out[i] = '\0';
}

int platform_storage_save(const char* key, const void* data, size_t len) {
    char safe[16];
    nvs_truncate_key(key, safe, sizeof(safe));
    nvs_handle_t h;
    if (nvs_open(STORAGE_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_blob(h, safe, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? 0 : -1;
}

int platform_storage_load(const char* key, void* buf, size_t max_len) {
    char safe[16];
    nvs_truncate_key(key, safe, sizeof(safe));
    nvs_handle_t h;
    if (nvs_open(STORAGE_NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    size_t    len = max_len;
    esp_err_t err = nvs_get_blob(h, safe, buf, &len);
    nvs_close(h);
    return (err == ESP_OK) ? (int)len : -1;
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

uint64_t platform_instret_now(void) {
    // Stage 8 diag: RISC-V minstret CSR (0xB02), low 32 bits, read via inline
    // asm (esp_cpu.h has no wrapper for this counter). mcountinhibit bit 2 is
    // cleared once in platform_init() so this actually increments. Wraps at
    // 2^32 like mcycle -- callers must diff within a block.
    uint32_t v;
    __asm__ volatile("csrr %0, minstret" : "=r"(v));
    return (uint64_t)v;
}

// ---------------------------------------------------------------------------
// Render task (input-latency fix)
// ---------------------------------------------------------------------------
// Priority ordering on core 0:
//   control (app_run task) : CONTROL_PRIO 5  — serviced promptly, above USB
//   usbh_midi (class driver): CLASS_PRIO   3  (midi_usb_host.c)
//   usbh_daemon             : DAEMON_PRIO  2  (midi_usb_host.c)
//   render                  : RENDER_PRIO  2  — same as daemon; display blit
//                                               never delays note input
// Audio task runs on core 1 at configMAX_PRIORITIES-2; this is untouched.
#define RENDER_PRIO  2u
#define CONTROL_PRIO 5u

static TaskHandle_t  s_render_task    = NULL;
static volatile bool s_render_run     = false;
static volatile bool s_render_done    = true;
static void (*s_render_cb)(void* ctx) = NULL;
static void*    s_render_ctx          = NULL;
static uint32_t s_render_ms           = 16u;

static void render_task(void* arg) {
    (void)arg;
    while (s_render_run) {
        s_render_cb(s_render_ctx);
        vTaskDelay(pdMS_TO_TICKS(s_render_ms));
    }
    s_render_done = true;
    vTaskDelete(NULL);
}

bool platform_render_task_start(void (*render_cb)(void* ctx), void* ctx, uint32_t render_ms) {
    s_render_cb   = render_cb;
    s_render_ctx  = ctx;
    s_render_ms   = render_ms;
    s_render_run  = true;
    s_render_done = false;

    BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, RENDER_PRIO, &s_render_task, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "render task create failed");
        s_render_run  = false;
        s_render_done = true;
        return false;
    }
    // Raise the calling (control) task so input polling always wins over USB-host
    // tasks (DAEMON_PRIO 2, CLASS_PRIO 3) and the render task (RENDER_PRIO 2).
    vTaskPrioritySet(NULL, CONTROL_PRIO);
    return true;
}

void platform_render_task_stop(void) {
    if (s_render_task == NULL) return;
    s_render_run = false;
    // Wait up to ~100 ms for the task to finish its current callback + sleep.
    for (int i = 0; i < 50 && !s_render_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    s_render_task = NULL;
}

// ---------------------------------------------------------------------------
// Audio-block cycle profiler read + reset
// ---------------------------------------------------------------------------
void platform_audio_profile_read(uint32_t* out_avg_cyc, uint32_t* out_max_cyc, uint32_t* out_over,
                                 uint32_t* out_count) {
#ifdef SYNTH_PROFILE
    // Snapshot (benign cross-core race — diagnostic only).
    uint64_t sum   = s_ab_sum;
    uint32_t count = s_ab_count;
    uint32_t max   = s_ab_max;
    uint32_t over  = s_ab_over;
    // Reset for the next window.
    s_ab_sum       = 0;
    s_ab_count     = 0;
    s_ab_max       = 0;
    s_ab_over      = 0;
    *out_avg_cyc   = count ? (uint32_t)(sum / count) : 0u;
    *out_max_cyc   = max;
    *out_over      = over;
    *out_count     = count;
#else
    *out_avg_cyc = 0;
    *out_max_cyc = 0;
    *out_over    = 0;
    *out_count   = 0;
#endif
}
