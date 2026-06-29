// platform/device/midi_usb_host.h — USB-A host MIDI driver (Stage 5b-i spike).
//
// Debug-only entry point: compiled into the image unconditionally so `make build`
// links it, but only called when SYNTH_USB_HOST_DEBUG is defined (see Makefile
// USBHOST_DEBUG=1 switch).  In normal builds platform_init() calls
// midi_usb_device_init() instead (Stage 5d USB-C device path).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// midi_usb_host_init — enable USB-A VBUS, install the USB Host Library, and
// spawn the host-lib daemon task + class-driver task.  Logs enumeration events
// and raw USB-MIDI packets via ESP_LOGI.  Does NOT touch platform_midi_read or
// the engine — this is a bring-up spike only (Stage 5b-i).
void midi_usb_host_init(void);

#ifdef __cplusplus
}
#endif
