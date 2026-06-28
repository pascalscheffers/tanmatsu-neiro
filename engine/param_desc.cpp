// engine/param_desc.cpp — the one Juno parameter table (spec 05).
//
// Adding a param = one row here. Never add param handling in four files
// (CLAUDE.md Prime Directive 2). The midi_cc field uses standard GM/MIDI-spec
// assignments where available (74=brightness, 71=resonance, etc.); 0xFF =
// unassigned. Smoothing times: 5 ms for audio-rate params (no zipper), 10 ms
// for ADSR (less critical, avoids glitches when tweaking held notes), 0 for
// stepped/instant params.
#include "param_desc.h"
#include "param_id.h"

// NOLINTBEGIN — the initialiser list is intentionally wide for readability.
const ParamDesc JUNO_PARAM_TABLE[] = {
    // --- OSC ---
    // id                       grp          name                  short     min     max       def    curve         unit      fmt      cc    smooth  flags
    {ParamId::OSC_LEVEL,      GROUP_OSC,  "Osc Level",           "OscLvl", 0.0f,   1.0f,   0.70f, CURVE_LIN,    UNIT_PCT, "%.2f",  70,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::SUB_LEVEL,      GROUP_OSC,  "Sub Level",           "SubLvl", 0.0f,   1.0f,   0.30f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::NOISE_LEVEL,    GROUP_OSC,  "Noise Level",         "Noise",  0.0f,   1.0f,   0.05f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE},
    // OSC_PWM: pulse-width amount. The oscillator waveform switch (OSC_WAVEFORM)
    // must select pulse (1) for PWM to be audible. Row exists so the mod matrix
    // can route LFO1→OSC_PWM (the "Clean 106" patch). DSP hook: dsp/osc.h gains
    // set_pw() in a future sub-stage; until then the value is cached only.
    {ParamId::OSC_PWM,        GROUP_OSC,  "OSC PWM",             "PWM",    0.0f,   1.0f,   0.50f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    // OSC_WAVEFORM: DCO waveform select. Currently only SAW (0) is implemented
    // in dsp/osc.h; PULSE(1) and TRI(2) are reserved for a future sub-stage.
    {ParamId::OSC_WAVEFORM,   GROUP_OSC,  "OSC Waveform",        "Wave",   0.0f,   2.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    // OSC_RANGE: DCO range offset in semitones (−24 = 2 oct down, +24 = 2 oct up).
    {ParamId::OSC_RANGE,      GROUP_OSC,  "OSC Range",           "Range",  -24.0f, 24.0f,  0.0f,  CURVE_STEPPED,UNIT_SEMI,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},

    // --- FILTER ---
    {ParamId::FILTER_CUTOFF,    GROUP_FILTER, "Filter Cutoff",   "Cutoff", 20.0f, 20000.0f, 2000.0f, CURVE_EXP, UNIT_HZ,  "%.0f",  74,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::FILTER_RES,       GROUP_FILTER, "Filter Res",      "Res",    0.0f,   1.0f,   0.30f, CURVE_LIN,    UNIT_PCT, "%.2f",  71,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::FILTER_MODE,      GROUP_FILTER, "Filter Mode",     "Mode",   0.0f,   2.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    // VCF_ENV_DEPTH: scales ENV2 contribution to VCF cutoff (0=no mod, 1=full).
    // Applied in juno_voice render as: cutoff += env2_value * VCF_ENV_DEPTH * kEnvModRange.
    {ParamId::VCF_ENV_DEPTH,    GROUP_FILTER, "VCF Env Depth",   "EnvDep", 0.0f,   1.0f,   0.35f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    // VCF_ENV_POLARITY: 0=positive (env opens filter), 1=negative (env closes).
    {ParamId::VCF_ENV_POLARITY, GROUP_FILTER, "VCF Env Polarity","EnvPol", 0.0f,   1.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    // VCF_KEY_TRACK: key-follow depth (0=off, 0.5=half, 1=full tracking).
    // Scales the built-in key_track source; this is the panel knob, separate from
    // any mod-matrix route that also uses key_track.
    {ParamId::VCF_KEY_TRACK,    GROUP_FILTER, "VCF Key Track",   "KTrack", 0.0f,   1.0f,   0.50f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE},
    // VCF_LFO_DEPTH: scales LFO1 contribution to VCF cutoff (panel knob, not matrix).
    {ParamId::VCF_LFO_DEPTH,    GROUP_FILTER, "VCF LFO Depth",   "LFODep", 0.0f,   1.0f,   0.0f,  CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},

    // --- HPF ---
    // Juno-106 HPF: 4-position discrete switch on the original hardware; modeled
    // here as a continuous cutoff for finer control. DSP hook: adding the HPF
    // requires a second dsp::Filter object — that is a separate sub-stage (Split-if
    // hit at Stage 3c-i). This row exists so the param is addressable from the param
    // store, MIDI CC, and presets; set_param caches the value but takes no action
    // until the DSP block lands.
    {ParamId::HPF_CUTOFF, GROUP_HPF, "HPF Cutoff", "HPF",  20.0f, 1000.0f, 20.0f, CURVE_EXP, UNIT_HZ, "%.0f", 0xFF, 5.0f, FLAG_PER_VOICE},

    // --- ENV ---
    {ParamId::ENV_ATTACK,  GROUP_ENV,  "Attack",    "Atk",   0.001f,  5.0f,   0.010f, CURVE_EXP,   UNIT_SEC, "%.3f",  73,   10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_DECAY,   GROUP_ENV,  "Decay",     "Dec",   0.001f,  5.0f,   0.100f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_SUSTAIN, GROUP_ENV,  "Sustain",   "Sus",    0.0f,   1.0f,   0.700f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF,  0.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_RELEASE, GROUP_ENV,  "Release",   "Rel",   0.001f,  5.0f,   0.300f, CURVE_EXP,   UNIT_SEC, "%.3f",  72,   10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},

    // --- FX (Juno chorus) ---
    {ParamId::CHORUS_RATE,  GROUP_FX, "Chorus Rate",   "ChoRt",  0.05f,  5.0f,   0.500f, CURVE_EXP,   UNIT_HZ,  "%.2f",  0xFF, 20.0f,  0},
    {ParamId::CHORUS_DEPTH, GROUP_FX, "Chorus Depth",  "ChoDep", 0.0f,   1.0f,   0.700f, CURVE_LIN,   UNIT_PCT, "%.2f",  93,   20.0f,  0},
    {ParamId::CHORUS_DELAY, GROUP_FX, "Chorus Delay",  "ChoDly", 0.0f,   1.0f,   0.400f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF, 20.0f,  0},
    // CHORUS_MODE: 0=off (dry), 1=Chorus I (slow, lush), 2=Chorus II (fast, wide).
    // Implemented as: 0 = bypass chorus; 1/2 = preset rate+depth scaled in synth.cpp.
    {ParamId::CHORUS_MODE,  GROUP_FX, "Chorus Mode",   "ChoMod", 0.0f,   2.0f,   1.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  0},

    // --- ENV2 (filter/mod envelope) ---
    // A second ADSR used as a modulation source (Stage 3a). VCF_ENV_DEPTH scales
    // its contribution to the VCF cutoff. Defaults mimic a typical Juno filter-env
    // shape (fast attack, medium decay, no sustain).
    {ParamId::ENV2_ATTACK,  GROUP_ENV2, "Env2 Attack",  "E2Atk", 0.001f,  5.0f,  0.005f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_DECAY,   GROUP_ENV2, "Env2 Decay",   "E2Dec", 0.001f,  5.0f,  0.200f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_SUSTAIN, GROUP_ENV2, "Env2 Sustain", "E2Sus", 0.0f,    1.0f,  0.000f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF,  0.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_RELEASE, GROUP_ENV2, "Env2 Release", "E2Rel", 0.001f,  5.0f,  0.200f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},

    // --- LFO1 ---
    // Per-voice LFO, sub-audio rate. Shape is a stepped int (see LfoWave enum
    // in dsp/lfo.h: SINE=0, TRI=1, SAW=2, SQUARE=4, SH=5). Depth is a
    // normalised scale factor; the mod matrix (Stage 3b-i) interprets it.
    // LFO1_DELAY: fade-in time after note_on; LFO ramps from 0 to full depth
    // over this many seconds (counter-based in render, no new DSP block needed).
    {ParamId::LFO1_RATE,  GROUP_LFO, "LFO1 Rate",  "L1Rt",  0.01f,  20.0f,  1.0f,  CURVE_EXP,    UNIT_HZ,  "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO1_DEPTH, GROUP_LFO, "LFO1 Depth", "L1Dep", 0.0f,   1.0f,   0.5f,  CURVE_LIN,    UNIT_PCT, "%.2f",  1,     5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO1_SHAPE, GROUP_LFO, "LFO1 Shape", "L1Shp", 0.0f,   5.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    {ParamId::LFO1_DELAY, GROUP_LFO, "LFO1 Delay", "L1Dly", 0.0f,   5.0f,   0.0f,  CURVE_LIN,    UNIT_SEC, "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE},

    // --- LFO2 ---
    {ParamId::LFO2_RATE,  GROUP_LFO, "LFO2 Rate",  "L2Rt",  0.01f,  20.0f,  0.5f,  CURVE_EXP,    UNIT_HZ,  "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO2_DEPTH, GROUP_LFO, "LFO2 Depth", "L2Dep", 0.0f,   1.0f,   0.5f,  CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO2_SHAPE, GROUP_LFO, "LFO2 Shape", "L2Shp", 0.0f,   5.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    {ParamId::LFO2_DELAY, GROUP_LFO, "LFO2 Delay", "L2Dly", 0.0f,   5.0f,   0.0f,  CURVE_LIN,    UNIT_SEC, "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE},

    // --- AMP / MIX ---
    // MASTER_GAIN: global output gain. Default 0.5 = −6 dB headroom (Stage 2b).
    // VCA_GATE_MODE: 0=envelope drives VCA (normal), 1=gate (hard on/off, no envelope).
    // VCA_LEVEL: per-voice output level; multiplies into the VCA after the envelope.
    {ParamId::MASTER_GAIN,   GROUP_AMP, "Master Gain",  "Gain",   0.0f,   2.0f,   0.500f, CURVE_LIN,   UNIT_NONE,"%.2f",   7,   10.0f,  FLAG_AUDIO_RATE},
    {ParamId::VCA_GATE_MODE, GROUP_AMP, "VCA Gate Mode","VcaGt",  0.0f,   1.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},
    {ParamId::VCA_LEVEL,     GROUP_AMP, "VCA Level",    "VcaLvl", 0.0f,   1.0f,   1.0f,  CURVE_LIN,   UNIT_PCT, "%.2f",  7,     5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
};
// NOLINTEND

const int kJunoParamCount = (int)(sizeof(JUNO_PARAM_TABLE) / sizeof(JUNO_PARAM_TABLE[0]));
