// engine/param_id.h — stable uint16_t parameter IDs for the Juno synth model.
//
// IDs are grouped with 16-slot gaps so groups can grow without renumbering.
// These IDs are the lingua franca: UI, MIDI CC map, mod matrix, presets, and
// param-locks all reference them (spec 05). NEVER reuse or renumber a retired
// ID — old presets must still load (fill with the default).
//
// The preset file format (id→value serialisation) is gated at Stage 2d.
// The IDs themselves are internal data and are defined here.
#pragma once

#include <cstdint>

namespace ParamId {

// OSC group  (0x10-0x1F) — oscillator section
static constexpr uint16_t OSC_LEVEL    = 0x10;  // main oscillator mix level
static constexpr uint16_t SUB_LEVEL    = 0x11;  // sub-oscillator level
static constexpr uint16_t NOISE_LEVEL  = 0x12;  // noise mix level
static constexpr uint16_t OSC_PWM      = 0x13;  // DCO pulse-width amount (0=narrow..1=wide)
static constexpr uint16_t OSC_WAVEFORM = 0x14;  // DCO waveform select (0=saw, 1=pulse, 2=tri)
static constexpr uint16_t OSC_RANGE    = 0x15;  // DCO range offset in semitones (−24..+24)

// FILTER group (0x20-0x2F) — SVF multimode filter + HPF
static constexpr uint16_t FILTER_CUTOFF    = 0x20;  // VCF cutoff frequency
static constexpr uint16_t FILTER_RES       = 0x21;  // VCF resonance
static constexpr uint16_t FILTER_MODE      = 0x22;  // LP=0, BP=1, HP=2
static constexpr uint16_t HPF_CUTOFF       = 0x23;  // High-pass filter cutoff (Juno 4-pos HPF)
static constexpr uint16_t VCF_ENV_DEPTH    = 0x24;  // ENV2 → VCF cutoff mod depth
static constexpr uint16_t VCF_ENV_POLARITY = 0x25;  // ENV2 polarity: 0=positive, 1=negative
static constexpr uint16_t VCF_KEY_TRACK    = 0x26;  // Key-follow amount (0=off..1=full)
static constexpr uint16_t VCF_LFO_DEPTH    = 0x27;  // LFO1 → VCF cutoff mod depth

// ENV group (0x30-0x3F) — ADSR amplitude envelope
static constexpr uint16_t ENV_ATTACK  = 0x30;
static constexpr uint16_t ENV_DECAY   = 0x31;
static constexpr uint16_t ENV_SUSTAIN = 0x32;
static constexpr uint16_t ENV_RELEASE = 0x33;

// ENV2 group (0x40-0x4F) — filter/mod ADSR (second envelope)
static constexpr uint16_t ENV2_ATTACK  = 0x40;
static constexpr uint16_t ENV2_DECAY   = 0x41;
static constexpr uint16_t ENV2_SUSTAIN = 0x42;
static constexpr uint16_t ENV2_RELEASE = 0x43;

// FX group (0x50-0x5F) — Juno chorus
static constexpr uint16_t CHORUS_RATE  = 0x50;
static constexpr uint16_t CHORUS_DEPTH = 0x51;
static constexpr uint16_t CHORUS_DELAY = 0x52;
static constexpr uint16_t CHORUS_MODE  = 0x53;  // 0=off, 1=chorus I, 2=chorus II

// AMP group (0x60-0x6F) — VCA / output stage
static constexpr uint16_t MASTER_GAIN    = 0x60;  // global output gain
static constexpr uint16_t VCA_GATE_MODE  = 0x61;  // VCA driver: 0=env, 1=gate
static constexpr uint16_t VCA_LEVEL      = 0x62;  // per-voice VCA output level

// LFO1 group (0x70-0x77) — first per-voice LFO
static constexpr uint16_t LFO1_RATE  = 0x70;
static constexpr uint16_t LFO1_DEPTH = 0x71;
static constexpr uint16_t LFO1_SHAPE = 0x72;
static constexpr uint16_t LFO1_DELAY = 0x73;  // LFO1 fade-in delay time (seconds)

// LFO2 group (0x78-0x7F) — second per-voice LFO
static constexpr uint16_t LFO2_RATE  = 0x78;
static constexpr uint16_t LFO2_DEPTH = 0x79;
static constexpr uint16_t LFO2_SHAPE = 0x7A;
static constexpr uint16_t LFO2_DELAY = 0x7B;  // LFO2 fade-in delay time (seconds)

// kMax: all Juno IDs are < this. kParamIdMax in param_store.h must be >= kMax.
// Current highest ID: LFO2_DELAY = 0x7B; keep ceiling at 0x80 (= 128).
static constexpr uint16_t kMax = 0x80;

} // namespace ParamId
