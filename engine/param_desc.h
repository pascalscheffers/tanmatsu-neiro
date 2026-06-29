// engine/param_desc.h — parameter descriptor type and the Juno parameter table.
//
// The ParamDesc table is the central dedup mechanism (spec 05, CLAUDE.md Prime
// Directive 2). One row per knob — forever. UI, MIDI, presets, and the mod
// matrix all read from it. Adding a parameter = one row in JUNO_PARAM_TABLE.
// Never fork this into per-feature copies.
#pragma once

#include <cstdint>

// UI grouping for page layout (spec 03). One group → one page tab.
enum ParamGroup : uint8_t {
    GROUP_OSC    = 0,
    GROUP_SUB    = 1,
    GROUP_FILTER = 2,
    GROUP_ENV    = 3,  // ENV1 — amplitude ADSR
    GROUP_LFO    = 4,
    GROUP_FX     = 5,
    GROUP_AMP    = 6,
    GROUP_GLOBAL = 7,
    GROUP_ENV2   = 8,  // ENV2 — filter/mod ADSR (Stage 3a)
    GROUP_HPF    = 9,  // High-pass filter (Stage 3c-i)
    GROUP_ARP    = 10, // Arpeggiator control (Stage 4b-ii)
};

// Value-curve: how a normalised [0,1] input maps to the physical [min,max] range.
enum ParamCurve : uint8_t {
    CURVE_LIN     = 0,  // linear: v = min + norm*(max-min)
    CURVE_EXP     = 1,  // exponential: v = min*(max/min)^norm  (min/max must be >0)
    CURVE_LOG     = 2,  // log2 taper: v = min + (max-min)*log2(1+norm)
    CURVE_STEPPED = 3,  // integer steps: v = round(min..max)
};

// Physical unit for display.
enum ParamUnit : uint8_t {
    UNIT_NONE = 0,
    UNIT_HZ   = 1,
    UNIT_PCT  = 2,
    UNIT_DB   = 3,
    UNIT_SEMI = 4,
    UNIT_SEC  = 5,
    UNIT_MS   = 6,
    UNIT_CENT = 7,  // cents (1/100 of a semitone); e.g. for unison detune spread
};

// Bit flags describing parameter behaviour.
enum ParamFlags : uint16_t {
    FLAG_AUDIO_RATE = 1 << 0,  // changes must be block-smoothed
    FLAG_PER_VOICE  = 1 << 1,  // delivered to each active voice
    FLAG_MOD_DEST   = 1 << 2,  // can be a modulation destination
};

// One row in the parameter table (spec 05).
struct ParamDesc {
    uint16_t    id;
    ParamGroup  group;
    const char* name;        // full display name
    const char* short_name;  // ≤6 chars for tight UI cells
    float       min, max, def;
    ParamCurve  curve;
    ParamUnit   unit;
    const char* display_fmt;   // printf format for the numeric value; nullptr = auto
    uint8_t     midi_cc;       // default MIDI CC (0xFF = unassigned)
    float       smoothing_ms;  // zipper-noise removal; 0 = instant
    uint16_t    flags;
};

// Number of entries in JUNO_PARAM_TABLE.
extern const int kJunoParamCount;

// The one parameter table for the Juno voice model.
// Defined in param_desc.cpp. Never access by index — always look up by id.
extern const ParamDesc JUNO_PARAM_TABLE[];
