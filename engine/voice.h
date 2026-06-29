// engine/voice.h — per-note synthesis interface (ADR 0008).
// IVoice is the swappable unit; the allocator (1c) and mod matrix (Stage 3)
// only see this interface, never a concrete model.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declaration — keeps voice.h free of heavy includes.
// mod_matrix.h is only needed by files that call set_mod_matrix.
class ModMatrix;

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
    virtual void note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) = 0;
    // Begin release; envelope continues until idle.
    virtual void note_off()                                                    = 0;
    // Force to silent/idle immediately (steal or free).
    virtual void reset()                                                       = 0;
    // Update a model-specific parameter. Stage 2 connects the param table.
    virtual void set_param(int id, float value)                                = 0;
    // Render n frames and *add* into buf[0..n-1] (mono). Real-time safe.
    virtual void render(float* buf, size_t n)                                  = 0;
    // True while the envelope is running (allocator uses this for stealing).
    virtual bool is_active() const                                             = 0;
    // Install the per-voice modulation routings (control thread; not audio-path).
    virtual void set_mod_matrix(const ModMatrix& mat)                          = 0;

    // Set a per-voice pitch offset in semitones (used by the allocator for
    // portamento glide). Called once per block from VoiceAlloc::advance_glide().
    // Must be allocation-free and audio-safe.
    virtual void set_pitch_offset(float semitones) = 0;

    // Inject the shared engine LFO outputs for this block (called once per block
    // from synth_render, before render()). The raw values are in [-1, +1]; the
    // voice applies its own per-note delay fade-in scale and depth.
    virtual void set_lfo_inputs(float lfo1_raw, float lfo2_raw) = 0;

    // Inject channel-wide MIDI expression for this block (called once per block from
    // synth_render, before render(), like set_lfo_inputs). Channel-wide (omni) in v1;
    // per-note MPE is future. mod_wheel/aftertouch in [0,1]; pitch_bend bipolar [-1,+1]
    // (the voice scales it by kPitchBendRangeSemis and applies it directly to pitch).
    virtual void set_expression(float mod_wheel, float pitch_bend, float aftertouch) = 0;
};
