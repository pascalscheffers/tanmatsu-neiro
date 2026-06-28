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

// OSC group  (0x10-0x1F) — oscillator mix levels
static constexpr uint16_t OSC_LEVEL   = 0x10;
static constexpr uint16_t SUB_LEVEL   = 0x11;
static constexpr uint16_t NOISE_LEVEL = 0x12;

// FILTER group (0x20-0x2F) — SVF multimode filter
static constexpr uint16_t FILTER_CUTOFF = 0x20;
static constexpr uint16_t FILTER_RES    = 0x21;
static constexpr uint16_t FILTER_MODE   = 0x22;

// ENV group (0x30-0x3F) — ADSR amplitude envelope
static constexpr uint16_t ENV_ATTACK  = 0x30;
static constexpr uint16_t ENV_DECAY   = 0x31;
static constexpr uint16_t ENV_SUSTAIN = 0x32;
static constexpr uint16_t ENV_RELEASE = 0x33;

// FX group (0x50-0x5F) — Juno chorus
static constexpr uint16_t CHORUS_RATE  = 0x50;
static constexpr uint16_t CHORUS_DEPTH = 0x51;
static constexpr uint16_t CHORUS_DELAY = 0x52;

// AMP group (0x60-0x6F) — output stage
static constexpr uint16_t MASTER_GAIN = 0x60;

// kMax: all Juno IDs are < this. Increase if the namespace grows beyond 0x7F.
static constexpr uint16_t kMax = 0x80;

} // namespace ParamId
