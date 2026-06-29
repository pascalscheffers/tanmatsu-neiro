// platform/device/midi_usb_device.h — USB-C MIDI device transport (Stage 5d).
//
// Swaps the shared USB-C PHY from Serial/JTAG to OTG, installs TinyUSB as a
// USB-MIDI device, and provides the real platform_midi_read() implementation
// that feeds the Stage-5a incremental parser in control/midi_in.c.
//
// Nothing above platform/device/ includes this header.  All USB/TinyUSB/hal
// symbols are confined to midi_usb_device.c.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Perform the PHY swap (Serial/JTAG → OTG) and install TinyUSB MIDI device.
// Called once near the end of platform_init(), after display/input/audio.
void midi_usb_device_init(void);

#ifdef __cplusplus
}
#endif
