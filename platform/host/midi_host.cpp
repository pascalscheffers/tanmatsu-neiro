// platform/host/midi_host.cpp — RtMidi backend for platform_midi_read (Stage 5a-ii).
//
// The ONLY file that includes RtMidi; keeps the HAL membrane clean (no RtMidi
// symbols leak into portable code). Lazy-init on first poll: if no MIDI device
// is connected, platform_midi_read returns 0 forever without crashing or blocking.
#include <rtmidi/RtMidi.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

extern "C" {
#include "platform.h"
}

// Lazy-init state.
static RtMidiIn* s_midi_in = nullptr;
static bool      s_tried   = false;
static bool      s_ok      = false;

static void midi_ensure_init(void) {
    if (s_tried) return;
    s_tried = true;
    try {
        s_midi_in = new RtMidiIn();
        if (s_midi_in->getPortCount() > 0) {
            s_midi_in->openPort(0);
            // Don't ignore any message types (sysex excluded is fine — keep defaults).
            s_ok = true;
        }
        // No ports: leave closed, s_ok stays false.
    } catch (...) {
        // RtMidiError or any other exception: mark unavailable, never retry.
        delete s_midi_in;
        s_midi_in = nullptr;
        s_ok      = false;
    }
}

extern "C" size_t platform_midi_read(uint8_t* buf, size_t max_len) {
    midi_ensure_init();
    if (!s_ok || !s_midi_in) return 0;

    size_t               total = 0;
    std::vector<uint8_t> msg;
    double               stamp;
    while (total < max_len) {
        try {
            stamp = s_midi_in->getMessage(&msg);
        } catch (...) {
            // If RtMidi throws mid-session, stop polling this frame.
            break;
        }
        (void)stamp;
        if (msg.empty()) break;  // queue drained

        for (size_t i = 0; i < msg.size() && total < max_len; i++) {
            buf[total++] = msg[i];
        }
    }
    return total;
}
