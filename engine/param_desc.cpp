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
    // id                    grp          name               short     min     max       def    curve         unit      fmt      cc    smooth  flags
    {ParamId::OSC_LEVEL,   GROUP_OSC,  "Osc Level",        "OscLvl", 0.0f,   1.0f,   0.70f, CURVE_LIN,    UNIT_PCT, "%.2f",  70,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::SUB_LEVEL,   GROUP_OSC,  "Sub Level",        "SubLvl", 0.0f,   1.0f,   0.30f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::NOISE_LEVEL, GROUP_OSC,  "Noise Level",      "Noise",  0.0f,   1.0f,   0.05f, CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE},

    // --- FILTER ---
    {ParamId::FILTER_CUTOFF, GROUP_FILTER, "Filter Cutoff","Cutoff", 20.0f, 20000.0f, 2000.0f, CURVE_EXP, UNIT_HZ,  "%.0f",  74,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::FILTER_RES,    GROUP_FILTER, "Filter Res",   "Res",    0.0f,   1.0f,   0.30f, CURVE_LIN,    UNIT_PCT, "%.2f",  71,    5.0f,  FLAG_AUDIO_RATE|FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::FILTER_MODE,   GROUP_FILTER, "Filter Mode",  "Mode",   0.0f,   2.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},

    // --- ENV ---
    {ParamId::ENV_ATTACK,  GROUP_ENV,  "Attack",           "Atk",   0.001f,  5.0f,   0.010f, CURVE_EXP,   UNIT_SEC, "%.3f",  73,   10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_DECAY,   GROUP_ENV,  "Decay",            "Dec",   0.001f,  5.0f,   0.100f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_SUSTAIN, GROUP_ENV,  "Sustain",          "Sus",    0.0f,   1.0f,   0.700f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF,  0.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV_RELEASE, GROUP_ENV,  "Release",          "Rel",   0.001f,  5.0f,   0.300f, CURVE_EXP,   UNIT_SEC, "%.3f",  72,   10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},

    // --- FX (Juno chorus) ---
    {ParamId::CHORUS_RATE,  GROUP_FX, "Chorus Rate",       "ChoRt",  0.05f,  5.0f,   0.500f, CURVE_EXP,   UNIT_HZ,  "%.2f",  0xFF, 20.0f,  0},
    {ParamId::CHORUS_DEPTH, GROUP_FX, "Chorus Depth",      "ChoDep", 0.0f,   1.0f,   0.700f, CURVE_LIN,   UNIT_PCT, "%.2f",  93,   20.0f,  0},
    {ParamId::CHORUS_DELAY, GROUP_FX, "Chorus Delay",      "ChoDly", 0.0f,   1.0f,   0.400f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF, 20.0f,  0},

    // --- ENV2 (filter/mod envelope) ---
    // A second ADSR used as a modulation source (Stage 3a). Not wired to any
    // destination yet — the mod matrix (Stage 3b-i) routes it. Defaults mimic
    // a typical Juno filter-env shape (fast attack, medium decay, no sustain).
    {ParamId::ENV2_ATTACK,  GROUP_ENV2, "Env2 Attack",  "E2Atk", 0.001f,  5.0f,  0.005f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_DECAY,   GROUP_ENV2, "Env2 Decay",   "E2Dec", 0.001f,  5.0f,  0.200f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_SUSTAIN, GROUP_ENV2, "Env2 Sustain", "E2Sus", 0.0f,    1.0f,  0.000f, CURVE_LIN,   UNIT_PCT, "%.2f",  0xFF,  0.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::ENV2_RELEASE, GROUP_ENV2, "Env2 Release", "E2Rel", 0.001f,  5.0f,  0.200f, CURVE_EXP,   UNIT_SEC, "%.3f",  0xFF, 10.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},

    // --- LFO1 ---
    // Per-voice LFO, sub-audio rate. Shape is a stepped int (see LfoWave enum
    // in dsp/lfo.h: SINE=0, TRI=1, SAW=2, SQUARE=4, SH=5). Depth is a
    // normalised scale factor; the mod matrix (Stage 3b-i) interprets it.
    {ParamId::LFO1_RATE,  GROUP_LFO, "LFO1 Rate",  "L1Rt",  0.01f,  20.0f,  1.0f,  CURVE_EXP,    UNIT_HZ,  "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO1_DEPTH, GROUP_LFO, "LFO1 Depth", "L1Dep", 0.0f,   1.0f,   0.5f,  CURVE_LIN,    UNIT_PCT, "%.2f",  1,     5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO1_SHAPE, GROUP_LFO, "LFO1 Shape", "L1Shp", 0.0f,   5.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},

    // --- LFO2 ---
    {ParamId::LFO2_RATE,  GROUP_LFO, "LFO2 Rate",  "L2Rt",  0.01f,  20.0f,  0.5f,  CURVE_EXP,    UNIT_HZ,  "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO2_DEPTH, GROUP_LFO, "LFO2 Depth", "L2Dep", 0.0f,   1.0f,   0.5f,  CURVE_LIN,    UNIT_PCT, "%.2f",  0xFF,  5.0f,  FLAG_PER_VOICE|FLAG_MOD_DEST},
    {ParamId::LFO2_SHAPE, GROUP_LFO, "LFO2 Shape", "L2Shp", 0.0f,   5.0f,   0.0f,  CURVE_STEPPED,UNIT_NONE,"%.0f",  0xFF,  0.0f,  FLAG_PER_VOICE},

    // --- AMP / MIX ---
    // Master gain fixes the headroom issue noted in MEMORY.md (one voice peaks
    // ~1.05 pre-filter; held chords clip without attenuation). Default 0.5
    // gives -6 dB of headroom; the user can raise it for louder single-voice
    // patches. Soft-clip vs linear is a 🛑 Stage-2 sonic gate (MEMORY.md).
    {ParamId::MASTER_GAIN, GROUP_AMP, "Master Gain",       "Gain",   0.0f,   2.0f,   0.500f, CURVE_LIN,   UNIT_NONE,"%.2f",   7,   10.0f,  FLAG_AUDIO_RATE},
};
// NOLINTEND

const int kJunoParamCount = (int)(sizeof(JUNO_PARAM_TABLE) / sizeof(JUNO_PARAM_TABLE[0]));
