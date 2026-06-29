// SPDX-License-Identifier: Unlicense OR CC0-1.0
// platform/device/midi_usb_host.c — USB-A host MIDI driver (Stage 5b-i/5b-ii).
//
// Adapted from:
//   Wunderbaeumchen99817/esp-idf, examples/peripherals/usb/host/midi/main/
//   (esp-idf PR #12566 fork), retrieved 2026-06-29.
//
// Changes from the vendored source:
//   - MIDIStreaming interface matching: class 0x01 (Audio) + subclass 0x03
//     (MIDIStreaming) instead of "first interface with endpoints".
//   - Init entry point (midi_usb_host_init) replaces app_main.
//   - BSP VBUS enable (bsp_power_set_usb_host_boost_enabled) before USB install.
//   - peripheral_map = 0 (default) → P4 HS peripheral = USB-A OTG controller.
//   - Stage 5b-i spike: ESP_LOGI hex dump of received packets (now guarded under
//     SYNTH_USB_HOST_DEBUG so normal builds are RT-log-free).
//   - Stage 5b-ii: USB-MIDI de-packetization via CIN length table; lock-free SPSC
//     byte ring; midi_usb_host_read() consumer-side drain. platform_midi_read in
//     midi_usb_device.c calls this so both transports merge into the existing seam.
//   - Receive-only (bulk IN); OUT path not opened.

#include "midi_usb_host.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp/power.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const char TAG[] = "midi_usb_host";

// USB-MIDI subclass code (USB MIDIStreaming).
// Note: USB_CLASS_AUDIO (0x01) is already defined in usb/usb_types_ch9.h;
// we do not redefine it here to avoid a -Wmacro-redefined warning.
#define USB_SUBCLASS_MIDISTREAMING 0x03u

// USB direction bit: bit 7 of bEndpointAddress is 1 for IN.
#define USB_EP_DIR_IN_MASK 0x80u

// Maximum transfer buffer for bulk IN.  USB HS bulk max packet = 512 bytes;
// a 64-byte buffer is more than enough for MIDI (4 bytes per event packet).
#define MIDI_BULK_IN_BUF 64u

// Task stack and priority — control-plane only, never the audio path.
#define DAEMON_STACK 4096u
#define CLASS_STACK  5120u
#define DAEMON_PRIO  2u
#define CLASS_PRIO   3u

// ---------------------------------------------------------------------------
// USB-MIDI de-packetization: CIN → MIDI byte count (USB MIDI 1.0 spec §4)
//
// Each USB-MIDI event packet is 4 bytes: byte0 = (cable<<4)|CIN; byte1..3 =
// MIDI data.  The CIN (Code Index Number, low nibble of byte0) encodes how many
// of byte1..3 carry real MIDI data.  Values 0x0 and 0x1 are reserved/misc
// (used for SysEx single and cable events with no standard MIDI bytes) — we
// skip them.  All 16 entries are listed for clarity; the table is indexed by
// the 4-bit CIN directly.
// ---------------------------------------------------------------------------
static const uint8_t s_cin_len[16] = {
    0,  // 0x0 misc/reserved — skip
    0,  // 0x1 cable event — skip
    2,  // 0x2 two-byte system-common (MTC quarter-frame, song-select)
    3,  // 0x3 three-byte system-common (song position pointer)
    3,  // 0x4 SysEx start/continue
    1,  // 0x5 single-byte system-common / SysEx ends with 1 byte
    2,  // 0x6 SysEx ends with 2 bytes
    3,  // 0x7 SysEx ends with 3 bytes
    3,  // 0x8 Note-Off
    3,  // 0x9 Note-On
    3,  // 0xA Poly key pressure
    3,  // 0xB Control change
    2,  // 0xC Program change
    2,  // 0xD Channel pressure
    3,  // 0xE Pitch bend
    1,  // 0xF single byte (system real-time)
};

// ---------------------------------------------------------------------------
// Lock-free SPSC byte ring
//
// Producer: midi_transfer_cb (USB client-task context — NOT an ISR).
// Consumer: midi_usb_host_read (control thread, called from midi_router_poll).
//
// C11 _Atomic head/tail; no mutex; only one producer and one consumer at a
// time.  On overflow: bytes are silently dropped (MIDI event boundary is
// preserved: we only push whole messages, but we may drop a whole message if
// there's no room).  Ring size must be a power of two for the mask trick.
// ---------------------------------------------------------------------------
#define RING_SIZE 512u  // bytes; must be power of two
#define RING_MASK (RING_SIZE - 1u)

typedef struct {
    uint8_t          buf[RING_SIZE];
    _Atomic uint32_t head;  // producer writes here (index of next free slot)
    _Atomic uint32_t tail;  // consumer reads here (index of next unread byte)
} MidiRing;

static MidiRing s_ring;

// Push up to `len` bytes from `data` into the ring.  Drops bytes that don't
// fit.  Called from the USB transfer callback (producer, client-task context).
static void ring_push(const uint8_t* data, uint8_t len) {
    uint32_t head = atomic_load_explicit(&s_ring.head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&s_ring.tail, memory_order_acquire);
    uint32_t used = head - tail;  // wraps naturally with unsigned arithmetic
    uint32_t free = RING_SIZE - used;
    if (len > free) {
#ifdef SYNTH_USB_HOST_DEBUG
        ESP_LOGW(TAG, "ring overflow — dropped %u bytes", (unsigned)len);
#endif
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        s_ring.buf[(head + i) & RING_MASK] = data[i];
    }
    atomic_store_explicit(&s_ring.head, head + len, memory_order_release);
}

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  interface_number;
    uint8_t  alternate_setting;
    uint8_t  ep_in_addr;
    uint16_t ep_in_mps;  // max packet size
} midi_intf_t;

typedef struct {
    usb_host_client_handle_t client_hdl;
    usb_device_handle_t      dev_hdl;
    uint8_t                  dev_addr;
    midi_intf_t              intf;
    usb_transfer_t*          transfer;
    bool                     dev_open;
    bool                     intf_claimed;
    // Pending action flags (driven from the client-event callback into the task).
    bool                     action_open_dev;
    bool                     action_close_dev;
} class_driver_t;

static class_driver_t s_driver;

// ---------------------------------------------------------------------------
// Descriptor parsing: find the MIDIStreaming interface + bulk IN endpoint
// ---------------------------------------------------------------------------

// Walk the configuration descriptor, find a MIDIStreaming interface
// (bInterfaceClass==0x01 AND bInterfaceSubClass==0x03), then find its bulk IN
// endpoint.  Returns true and fills *out on success.
static bool find_midi_interface(const usb_config_desc_t* cfg, midi_intf_t* out) {
    int      offset    = 0;
    uint16_t total_len = cfg->wTotalLength;
    bool     in_midi   = false;

    const usb_standard_desc_t* cur = (const usb_standard_desc_t*)cfg;

    while (1) {
        cur = usb_parse_next_descriptor(cur, total_len, &offset);
        if (cur == NULL) break;

        if (cur->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t* intf = (const usb_intf_desc_t*)cur;
            if (intf->bInterfaceClass == USB_CLASS_AUDIO && intf->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING) {
                in_midi                = true;
                out->interface_number  = intf->bInterfaceNumber;
                out->alternate_setting = intf->bAlternateSetting;
                out->ep_in_addr        = 0;
                out->ep_in_mps         = 0;
            } else {
                in_midi = false;  // entered a different interface
            }
        }

        if (in_midi && cur->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t* ep      = (const usb_ep_desc_t*)cur;
            // We want a bulk IN endpoint.
            bool                 is_in   = (ep->bEndpointAddress & USB_EP_DIR_IN_MASK) != 0;
            bool                 is_bulk = (USB_EP_DESC_GET_XFERTYPE(ep) == USB_TRANSFER_TYPE_BULK);
            if (is_in && is_bulk && out->ep_in_addr == 0) {
                out->ep_in_addr = ep->bEndpointAddress;
                out->ep_in_mps  = ep->wMaxPacketSize;
                return true;  // found everything we need
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Transfer callback: de-packetize USB-MIDI event packets → SPSC ring
// ---------------------------------------------------------------------------

// Each USB-MIDI event packet is 4 bytes: byte0 = (cable<<4)|CIN; byte1..3 =
// MIDI data bytes.  We extract exactly s_cin_len[CIN] bytes and push them into
// the ring for consumption by midi_usb_host_read().
//
// This callback runs in the USB class-driver task context (NOT an ISR); plain
// C11 atomic ops on the ring are sufficient (no ISR-safe special handling needed).
static void midi_transfer_cb(usb_transfer_t* xfer) {
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0) {
        uint8_t* data     = xfer->data_buffer;
        int      num_pkts = xfer->actual_num_bytes / 4;  // each USB-MIDI event = 4 bytes

#ifdef SYNTH_USB_HOST_DEBUG
        // Hex-dump the raw received bytes (debug build only — RT-log in normal build).
        char hex[MIDI_BULK_IN_BUF * 3 + 1];
        int  pos = 0;
        for (int i = 0; i < xfer->actual_num_bytes && pos < (int)(sizeof(hex) - 3); i++) {
            pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", data[i]);
        }
        if (pos > 0) hex[pos - 1] = '\0';
        ESP_LOGI(TAG, "MIDI rx [%d bytes]: %s", xfer->actual_num_bytes, hex);
#endif

        for (int p = 0; p < num_pkts; p++) {
            uint8_t* pkt = data + p * 4;
            uint8_t  cin = pkt[0] & 0x0Fu;
            uint8_t  len = s_cin_len[cin];
            if (len > 0) {
                ring_push(&pkt[1], len);  // byte1..byte(len) are the MIDI bytes
            }
        }
    } else if (xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "transfer status %d", (int)xfer->status);
    }

    // Resubmit to keep receiving.  Ignore error — device may have disconnected.
    usb_host_transfer_submit(xfer);
}

// ---------------------------------------------------------------------------
// Open device and claim the MIDIStreaming interface
// ---------------------------------------------------------------------------

static void open_device(class_driver_t* drv) {
    esp_err_t err = usb_host_device_open(drv->client_hdl, drv->dev_addr, &drv->dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device_open failed: %s", esp_err_to_name(err));
        return;
    }
    drv->dev_open = true;

    // Log device info (VID/PID).
    usb_device_info_t info;
    if (usb_host_device_info(drv->dev_hdl, &info) == ESP_OK) {
        const usb_device_desc_t* dev_desc;
        if (usb_host_get_device_descriptor(drv->dev_hdl, &dev_desc) == ESP_OK) {
            ESP_LOGI(TAG, "Device connected: VID=0x%04X PID=0x%04X addr=%u", dev_desc->idVendor, dev_desc->idProduct,
                     drv->dev_addr);
        } else {
            ESP_LOGI(TAG, "Device connected: addr=%u (no descriptor)", drv->dev_addr);
        }
    }

    // Retrieve configuration descriptor and find MIDIStreaming interface.
    const usb_config_desc_t* cfg;
    err = usb_host_get_active_config_descriptor(drv->dev_hdl, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_active_config_descriptor failed: %s", esp_err_to_name(err));
        return;
    }

    if (!find_midi_interface(cfg, &drv->intf)) {
        ESP_LOGW(TAG, "No MIDIStreaming interface found — device is not a class-compliant MIDI device");
        return;
    }

    ESP_LOGI(TAG, "Found MIDIStreaming: intf=%u alt=%u ep_in=0x%02X mps=%u", drv->intf.interface_number,
             drv->intf.alternate_setting, drv->intf.ep_in_addr, drv->intf.ep_in_mps);

    // Claim the interface.
    err = usb_host_interface_claim(drv->client_hdl, drv->dev_hdl, drv->intf.interface_number,
                                   drv->intf.alternate_setting);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "interface_claim failed: %s", esp_err_to_name(err));
        return;
    }
    drv->intf_claimed = true;

    // Allocate a transfer and submit the first bulk IN read.
    err = usb_host_transfer_alloc(MIDI_BULK_IN_BUF, 0, &drv->transfer);
    if (err != ESP_OK || drv->transfer == NULL) {
        ESP_LOGE(TAG, "transfer_alloc failed: %s", esp_err_to_name(err));
        return;
    }
    drv->transfer->device_handle    = drv->dev_hdl;
    drv->transfer->bEndpointAddress = drv->intf.ep_in_addr;
    drv->transfer->num_bytes        = MIDI_BULK_IN_BUF;
    drv->transfer->callback         = midi_transfer_cb;
    drv->transfer->context          = drv;
    drv->transfer->timeout_ms       = 0;  // no timeout — keeps polling

    err = usb_host_transfer_submit(drv->transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transfer_submit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Bulk IN transfer submitted — MIDI → ring active");
    }
}

// ---------------------------------------------------------------------------
// Close device and release resources
// ---------------------------------------------------------------------------

static void close_device(class_driver_t* drv) {
    if (drv->transfer != NULL) {
        usb_host_transfer_free(drv->transfer);
        drv->transfer = NULL;
    }
    if (drv->intf_claimed) {
        usb_host_interface_release(drv->client_hdl, drv->dev_hdl, drv->intf.interface_number);
        drv->intf_claimed = false;
    }
    if (drv->dev_open) {
        usb_host_device_close(drv->client_hdl, drv->dev_hdl);
        drv->dev_hdl  = NULL;
        drv->dev_open = false;
    }
    ESP_LOGI(TAG, "Device disconnected — resources released");
}

// ---------------------------------------------------------------------------
// Client event callback (called from usb_host_client_handle_events context)
// ---------------------------------------------------------------------------

static void client_event_cb(const usb_host_client_event_msg_t* msg, void* arg) {
    class_driver_t* drv = (class_driver_t*)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        drv->dev_addr        = msg->new_dev.address;
        drv->action_open_dev = true;
        ESP_LOGI(TAG, "New device event: addr=%u", drv->dev_addr);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        drv->action_close_dev = true;
        ESP_LOGI(TAG, "Device gone event");
    }
}

// ---------------------------------------------------------------------------
// Class driver task — registers the client and pumps client events
// ---------------------------------------------------------------------------

static void class_driver_task(void* arg) {
    SemaphoreHandle_t sem = (SemaphoreHandle_t)arg;

    // Wait until the host lib daemon task has installed the host library.
    xSemaphoreTake(sem, portMAX_DELAY);

    const usb_host_client_config_t client_cfg = {
        .is_synchronous    = false,
        .max_num_event_msg = 5,
        .async =
            {
                .client_event_callback = client_event_cb,
                .callback_arg          = &s_driver,
            },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &s_driver.client_hdl));
    ESP_LOGI(TAG, "USB host client registered");

    while (1) {
        if (s_driver.action_open_dev) {
            s_driver.action_open_dev = false;
            open_device(&s_driver);
        }
        if (s_driver.action_close_dev) {
            s_driver.action_close_dev = false;
            close_device(&s_driver);
        }
        // Block until the next client event (new device, device gone, transfer done).
        usb_host_client_handle_events(s_driver.client_hdl, portMAX_DELAY);
    }
    // Unreachable in normal operation; cleanup omitted intentionally.
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Host library daemon task — installs the USB host library and pumps lib events
// ---------------------------------------------------------------------------

static void host_lib_daemon_task(void* arg) {
    SemaphoreHandle_t sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(TAG, "Installing USB Host Library (OTG-HS = USB-A port)");

    // peripheral_map = 0: use the default peripheral.  On ESP32-P4, the default
    // is the High-Speed OTG controller (USB-A), as documented in USB_DWC_LL_GET_HW:
    //   USB_DWC_LL_GET_HW(0) → USB_DWC_HS (USB-A)
    //   USB_DWC_LL_GET_HW(1) → USB_DWC_FS (USB-C, used by Stage 5d TinyUSB)
    // This is independent of the USB-C PHY swap — the HS controller has its own PHY.
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
        .peripheral_map = 0,  // 0 = default = HS = USB-A on P4
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    ESP_LOGI(TAG, "USB Host Library installed");

    // Signal the class-driver task that the library is ready.
    xSemaphoreGive(sem);

    while (1) {
        uint32_t  event_flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "lib_handle_events error: %s", esp_err_to_name(err));
            continue;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "USB host: no clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB host: all devices free");
        }
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void midi_usb_host_init(void) {
    // Step 0: zero-init the ring (static, but explicit for clarity).
    atomic_store(&s_ring.head, 0u);
    atomic_store(&s_ring.tail, 0u);

    // Step 1: enable USB-A VBUS (without this nothing enumerates).
    esp_err_t err = bsp_power_set_usb_host_boost_enabled(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_power_set_usb_host_boost_enabled failed: %s — USB-A may not power", esp_err_to_name(err));
        // Continue — the device might still enumerate if VBUS is externally powered.
    } else {
        ESP_LOGI(TAG, "USB-A VBUS boost enabled");
    }

    // Step 2: create a semaphore so the class task waits until the lib is installed.
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem == NULL) {
        ESP_LOGE(TAG, "Failed to create sync semaphore");
        return;
    }

    // Step 3: spawn the host-library daemon (installs the lib, pumps lib events).
    // Pinned to core 0; control-plane, not the audio path (audio is on core 1).
    BaseType_t ok =
        xTaskCreatePinnedToCore(host_lib_daemon_task, "usbh_daemon", DAEMON_STACK, (void*)sem, DAEMON_PRIO, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create daemon task");
        vSemaphoreDelete(sem);
        return;
    }

    // Step 4: spawn the class-driver task (registers client, pumps client events).
    ok = xTaskCreatePinnedToCore(class_driver_task, "usbh_midi", CLASS_STACK, (void*)sem, CLASS_PRIO, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create class driver task");
        // Daemon task is already running; cannot easily clean up, but the driver
        // will simply be inactive.  Log and return.
        return;
    }

    ESP_LOGI(TAG, "USB-A host MIDI initialised — plug in a class-compliant MIDI controller");
}

// ---------------------------------------------------------------------------
// Consumer: drain the SPSC ring into caller's buffer (control thread only)
// ---------------------------------------------------------------------------

// Called from platform_midi_read (via midi_usb_device.c) on the control thread.
// Returns the number of raw MIDI bytes written into buf (may be 0).
size_t midi_usb_host_read(uint8_t* buf, size_t max_len) {
    uint32_t tail  = atomic_load_explicit(&s_ring.tail, memory_order_relaxed);
    uint32_t head  = atomic_load_explicit(&s_ring.head, memory_order_acquire);
    uint32_t avail = head - tail;  // unsigned wraps correctly
    if (avail == 0) return 0;

    size_t n = (avail < max_len) ? (size_t)avail : max_len;
    for (size_t i = 0; i < n; i++) {
        buf[i] = s_ring.buf[(tail + i) & RING_MASK];
    }
    atomic_store_explicit(&s_ring.tail, tail + (uint32_t)n, memory_order_release);
    return n;
}
