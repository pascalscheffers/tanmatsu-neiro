// platform/device/midi_usb_device.c — USB-C MIDI device transport (Stage 5d).
//
// Swaps the shared USB-C PHY from Serial/JTAG console mode to USB-OTG device
// mode, then installs TinyUSB as a USB Full-Speed MIDI device.  After init the
// USB-C console is no longer available (accepted: AppFS apps already run with
// the USB-C console detached from the launcher).
//
// platform_midi_read() — moved here from the no-op stub in platform_device.c —
// calls tud_midi_stream_read() which de-packetizes USB-MIDI 4-byte event
// packets and returns raw MIDI bytes, satisfying the contract that midi_in.c
// expects.  esp_tinyusb spawns its own tud_task() internally; we do not poll
// it ourselves.
//
// All USB/TinyUSB/hal symbols are confined to this file (membrane check).
#include "midi_usb_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "platform.h"  // for platform_midi_read declaration
#include "tinyusb.h"

static const char TAG[] = "midi_usb_device";

// ---------------------------------------------------------------------------
// Interface and endpoint numbering
// ---------------------------------------------------------------------------
enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_COUNT
};

enum {
    EP_EMPTY = 0,
    EPNUM_MIDI,
};

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

// String descriptors (index 0 = language, 1 = Manufacturer, 2 = Product,
// 3 = Serial, 4 = MIDI interface name).
static const char* s_str_desc[] = {
    (char[]){0x09, 0x04},   // 0: language = English (0x0409)
    "Nicolai Electronics",  // 1: Manufacturer
    "Tanmatsu Synth",       // 2: Product
    "000001",               // 3: Serial
    "Tanmatsu MIDI",        // 4: MIDI interface
};

// Full-Speed configuration descriptor: one MIDI interface, 64-byte endpoints.
static const uint8_t s_midi_cfg_desc[] = {
    // config#, iface count, string index, total length, attrib, power mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),

    // iface#, string index, EP OUT addr, EP IN addr, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};

// ---------------------------------------------------------------------------
// PHY swap helper — moves the USB-C port from Serial/JTAG to OTG device mode.
// Mirrors what the Tanmatsu launcher does before starting TinyUSB.
// ---------------------------------------------------------------------------
static void phy_swap_to_otg(void) {
    // Manually pull D+/D- low first to signal a disconnect to the host.
    const usb_serial_jtag_pull_override_vals_t pull_off = {
        .dm_pd = true, .dm_pu = false, .dp_pd = true, .dp_pu = false};
    const usb_serial_jtag_pull_override_vals_t pull_on = {
        .dm_pd = false, .dm_pu = false, .dp_pd = false, .dp_pu = true};

    usb_serial_jtag_ll_phy_enable_pull_override(&pull_off);  // disconnect
    vTaskDelay(pdMS_TO_TICKS(500));                          // let host notice
    usb_serial_jtag_ll_phy_select(1);                        // 1 = OTG (0 = JTAG)
    usb_serial_jtag_ll_phy_enable_pull_override(&pull_on);   // re-assert D+
    usb_serial_jtag_ll_phy_disable_pull_override();          // hand control to OTG
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void midi_usb_device_init(void) {
    ESP_LOGI(TAG, "PHY swap: Serial/JTAG → USB-OTG device");
    phy_swap_to_otg();

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor        = NULL,  // use Kconfig VID/PID
        .string_descriptor        = s_str_desc,
        .string_descriptor_count  = sizeof(s_str_desc) / sizeof(s_str_desc[0]),
        .external_phy             = false,  // internal Full-Speed PHY
        .configuration_descriptor = s_midi_cfg_desc,
    };

    // Fail safe: a USB problem must not brick startup — the synth must still
    // boot and play (musical typing / host MIDI). Degrade gracefully instead of
    // aborting (and after the PHY swap there's no USB-C console to see a panic).
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s — USB MIDI disabled", esp_err_to_name(err));
        return;
    }
    // Note: after this point the USB-C console is no longer available.
    // Further ESP_LOGI calls won't reach a connected terminal.
    ESP_LOGI(TAG, "USB MIDI device ready — console detached");
}

// ---------------------------------------------------------------------------
// platform_midi_read — real implementation (replaces the no-op stub)
// ---------------------------------------------------------------------------
// Called each frame by midi_router_poll() (control/midi_router.c).
// tud_midi_stream_read() de-packetizes USB-MIDI 4-byte event packets and
// copies raw MIDI bytes into buf, returning the byte count (0 = nothing ready).
// This exactly matches the platform_midi_read seam contract.
size_t platform_midi_read(uint8_t* buf, size_t max_len) {
    if (!tud_midi_mounted()) return 0;
    return tud_midi_stream_read(buf, max_len);
}
