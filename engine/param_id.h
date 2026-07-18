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

// CLOCK group (0x00-0x0F) — master musical clock (Stage 4a)
// 0x00 reserved (invalid/sentinel).
static constexpr uint16_t CLOCK_BPM = 0x01;  // tempo in BPM [20..300]; UI/preset home for tempo
static constexpr uint16_t RECORD    = 0x02;  // session-only WAV recording toggle; never stored in presets

// ARP group (0x08–0x0F) — arpeggiator control (Stage 4b-ii)
// ARP_RATE stepped index 0..5 → divisions {1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32} at 96 PPQN;
// the index→ticks mapping is wired in Stage 4b-iii.
static constexpr uint16_t ARP_ON      = 0x08;  // 0=off, 1=on
static constexpr uint16_t ARP_MODE    = 0x09;  // 0=up,1=down,2=up-down,3=order,4=random
static constexpr uint16_t ARP_RATE    = 0x0A;  // stepped clock-division index (see 4b-iii)
static constexpr uint16_t ARP_OCTAVES = 0x0B;  // 1..4
static constexpr uint16_t ARP_GATE    = 0x0C;  // gate length as fraction of step
static constexpr uint16_t ARP_SWING   = 0x0D;  // swing amount (delays even steps)
static constexpr uint16_t ARP_LATCH   = 0x0E;  // 0=off, 1=on

// OSC group  (0x10-0x1F) — oscillator section
static constexpr uint16_t OSC_LEVEL    = 0x10;  // main oscillator mix level
static constexpr uint16_t SUB_LEVEL    = 0x11;  // sub-oscillator level
static constexpr uint16_t NOISE_LEVEL  = 0x12;  // noise mix level
static constexpr uint16_t OSC_PWM      = 0x13;  // DCO pulse-width amount (0=narrow..1=wide)
// OSC_WAVEFORM (0x14) is RETIRED (ADR 0026 / WO-13c): the Juno-106 DCO's saw and pulse
// waves are independent switches (both can sound at once, matching the original
// byte-16 bits), not a mutually-exclusive select. The ID is kept defined (unused, no
// descriptor row, no set_param case) purely so old references still compile; it must
// never be reused for a new meaning and never gains a table row again.
static constexpr uint16_t OSC_WAVEFORM = 0x14;  // RETIRED — do not use; see note above.
static constexpr uint16_t OSC_RANGE    = 0x15;  // DCO range offset in semitones (−24..+24)
// WO-13c: independent DCO wave-enable switches (ADR 0026). Both may be on together;
// their outputs sum. Fixed neutral default: saw on, pulse off (matches the prior
// SAW-default sound so the INIT patch is unchanged).
static constexpr uint16_t OSC_SAW_ON   = 0x16;  // 0=off, 1=on (default: on)
static constexpr uint16_t OSC_PULSE_ON = 0x17;  // 0=off, 1=on (default: off)

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

// AMP group (0x60-0x6F) — VCA / output stage + play-mode globals
static constexpr uint16_t MASTER_GAIN     = 0x60;  // global output gain
static constexpr uint16_t VCA_GATE_MODE   = 0x61;  // VCA driver: 0=env, 1=gate
static constexpr uint16_t VCA_LEVEL       = 0x62;  // per-voice VCA output level
// Stage 3d-i: play modes — allocated globally (not per-voice), in the AMP group
// so the table-driven UI shows them on the existing AMP page without touching ui/.
static constexpr uint16_t PLAY_MODE       = 0x63;  // 0=poly, 1=mono+retrigger, 2=mono+legato
static constexpr uint16_t PORTAMENTO_TIME = 0x64;  // glide time in seconds (0=off, 2=max)
// Stage 3d-ii: unison — global, AMP group (same table-driven UI logic as play modes).
static constexpr uint16_t UNISON_COUNT    = 0x65;  // voice stack depth 1..kNumVoices (stepped)
static constexpr uint16_t UNISON_DETUNE   = 0x66;  // total detune spread in cents (0..50)

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

}  // namespace ParamId
