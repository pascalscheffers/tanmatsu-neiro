// platform/device/midi_usb_host.h — USB-A host MIDI driver (Stage 5b-i/5b-ii).
//
// Compiled into the device image in all builds (normal and SYNTH_USB_HOST_DEBUG).
// In the normal build, midi_usb_host_init() is called alongside
// midi_usb_device_init() — the two controllers are independent (USB-A OTG-HS and
// USB-C OTG-FS) and coexist.
// In the SYNTH_USB_HOST_DEBUG build, only midi_usb_host_init() is called and the
// USB-C console remains alive.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// midi_usb_host_init — enable USB-A VBUS, zero the MIDI byte ring, install the
// USB Host Library, and spawn the host-lib daemon + class-driver tasks.  May be
// called alongside midi_usb_device_init() in the normal build; the two USB
// controllers (HS for USB-A, FS for USB-C) are independent on the ESP32-P4.
void midi_usb_host_init(void);

// midi_usb_host_read — drain the SPSC byte ring populated by the USB transfer
// callback.  Copies up to max_len raw MIDI bytes into buf; returns the count
// copied (may be 0 if no data is available).
//
// Caller: control thread only (platform_midi_read in midi_usb_device.c).
// Producer: USB class-driver task callback (NOT an ISR).
// Thread-safety: SPSC lock-free via C11 _Atomic head/tail; safe when called
// exclusively from the control thread while the USB task is the sole producer.
size_t midi_usb_host_read(uint8_t* buf, size_t max_len);

#ifdef __cplusplus
}
#endif
