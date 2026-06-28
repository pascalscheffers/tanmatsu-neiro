// engine/voice.h — per-note synthesis interface (ADR 0008).
// IVoice is the swappable unit; the allocator (1c) and mod matrix (Stage 3)
// only see this interface, never a concrete model.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Per-note expression (MPE-ready). v1 fills from channel-wide controls.
// MPE maps the same fields per-note. Channel wired fully in Stage 5.
struct NoteExpression {
    float   bend;      // pitch bend, semitones e.g. [-2, 2]
    float   pressure;  // channel/poly aftertouch [0, 1]
    float   slide;     // MPE timbre / CC74 [0, 1]
    uint8_t channel;   // MIDI channel 1-16
};

class IVoice {
public:
    virtual ~IVoice() = default;

    // Trigger a note. Must be allocation-free.
    virtual void note_on(uint8_t pitch, uint8_t velocity,
                         NoteExpression expr) = 0;
    // Begin release; envelope continues until idle.
    virtual void note_off() = 0;
    // Force to silent/idle immediately (steal or free).
    virtual void reset() = 0;
    // Update a model-specific parameter. Stage 2 connects the param table.
    virtual void set_param(int id, float value) = 0;
    // Render n frames and *add* into buf[0..n-1] (mono). Real-time safe.
    virtual void render(float* buf, size_t n) = 0;
    // True while the envelope is running (allocator uses this for stealing).
    virtual bool is_active() const = 0;
};
