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
    // --- CLOCK (global tempo) ---
    // CLOCK_BPM: the param-table home for tempo (Stage 4a-iii). BPM is also settable via
    // engine_set_bpm() (tap-tempo / MIDI-clock path) — those remain valid direct paths.
    // smoothing_ms=0: tempo must not zipper-glide. flags=0: global, not per-voice, not a
    // mod dest, not audio-rate.
    {ParamId::CLOCK_BPM, GROUP_GLOBAL, "Tempo", "BPM", 20.0f, 300.0f, 120.0f, CURVE_LIN, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
    {ParamId::RECORD, GROUP_GLOBAL, "Record", "REC", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     FLAG_NO_PRESET},

    // --- OSC ---
    // id                       grp          name                  short     min     max       def    curve         unit
    // fmt      cc    smooth  flags
    {ParamId::OSC_LEVEL, GROUP_OSC, "Osc Level", "OscLvl", 0.0f, 1.0f, 0.70f, CURVE_LIN, UNIT_PCT, "%.2f", 70, 5.0f,
     FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::SUB_LEVEL, GROUP_OSC, "Sub Level", "SubLvl", 0.0f, 1.0f, 0.30f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 5.0f,
     FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::NOISE_LEVEL, GROUP_OSC, "Noise Level", "Noise", 0.0f, 1.0f, 0.05f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF,
     5.0f, FLAG_AUDIO_RATE | FLAG_PER_VOICE},
    // OSC_PWM: pulse-width base value [0, 1]. OSC_PULSE_ON must be on (1) for
    // PWM to be audible. Wired in Stage 3c-iii, retargeted WO-13c: render() applies
    // once per block via osc_pulse_.set_pw(). LFO→PWM routing (kModDestPwm) is live
    // in "Clean 106".
    {ParamId::OSC_PWM, GROUP_OSC, "OSC PWM", "PWM", 0.0f, 1.0f, 0.50f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 5.0f,
     FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    // OSC_WAVEFORM is RETIRED (ADR 0026 / WO-13c) — no descriptor row. See param_id.h.
    // OSC_RANGE: DCO range offset in semitones (−24 = 2 oct down, +24 = 2 oct up).
    {ParamId::OSC_RANGE, GROUP_OSC, "OSC Range", "Range", -24.0f, 24.0f, 0.0f, CURVE_STEPPED, UNIT_SEMI, "%.0f", 0xFF,
     0.0f, FLAG_PER_VOICE},
    // OSC_SAW_ON / OSC_PULSE_ON (WO-13c, ADR 0026): independent DCO wave-enable switches —
    // both can be on together and sum, matching the real Juno-106 DCO's byte-16 bits.
    // Neutral default: saw on / pulse off (matches the prior SAW-default INIT sound).
    {ParamId::OSC_SAW_ON, GROUP_OSC, "OSC Saw", "Saw", 0.0f, 1.0f, 1.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     FLAG_PER_VOICE},
    {ParamId::OSC_PULSE_ON, GROUP_OSC, "OSC Pulse", "Pulse", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF,
     0.0f, FLAG_PER_VOICE},
    // WO-13d: direct panel modulation controls (ADR 0026 modulation section).
    // DCO_LFO_DEPTH: LFO1 -> DCO pitch, applied directly in render() (not via matrix).
    {ParamId::DCO_LFO_DEPTH, GROUP_OSC, "DCO LFO Depth", "DcoLFO", 0.0f, 1.0f, 0.0f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF,
     5.0f, FLAG_AUDIO_RATE | FLAG_PER_VOICE},
    // PWM_MODE: 0=LFO (OSC_PWM read as amount around center), 1=Manual (OSC_PWM is
    // the fixed width). Default Manual so existing static-PWM patches are unaffected.
    {ParamId::PWM_MODE, GROUP_OSC, "PWM Mode", "PWMMode", 0.0f, 1.0f, 1.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF,
     0.0f, FLAG_PER_VOICE},

    // --- FILTER ---
    // Launchkey 37 pots: cutoff=CC21, res=CC22 (was GM 74/71 — freed; controller sends 21-28).
    {ParamId::FILTER_CUTOFF, GROUP_FILTER, "Filter Cutoff", "Cutoff", 20.0f, 20000.0f, 2000.0f, CURVE_EXP, UNIT_HZ,
     "%.0f", 21, 5.0f, FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::FILTER_RES, GROUP_FILTER, "Filter Res", "Res", 0.0f, 1.0f, 0.30f, CURVE_LIN, UNIT_PCT, "%.2f", 22, 5.0f,
     FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::FILTER_MODE, GROUP_FILTER, "Filter Mode", "Mode", 0.0f, 2.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f",
     0xFF, 0.0f, FLAG_PER_VOICE},
    // VCF_ENV_DEPTH: scales ENV2 contribution to VCF cutoff (0=no mod, 1=full).
    // Applied in juno_voice render as: cutoff += env2_value * VCF_ENV_DEPTH * kEnvModRange.
    {ParamId::VCF_ENV_DEPTH, GROUP_FILTER, "VCF Env Depth", "EnvDep", 0.0f, 1.0f, 0.35f, CURVE_LIN, UNIT_PCT, "%.2f",
     23, 5.0f, FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},
    // VCF_ENV_POLARITY: 0=positive (env opens filter), 1=negative (env closes).
    {ParamId::VCF_ENV_POLARITY, GROUP_FILTER, "VCF Env Polarity", "EnvPol", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE,
     "%.0f", 0xFF, 0.0f, FLAG_PER_VOICE},
    // VCF_KEY_TRACK: key-follow depth (0=off, 0.5=half, 1=full tracking).
    // Scales the built-in key_track source; this is the panel knob, separate from
    // any mod-matrix route that also uses key_track.
    {ParamId::VCF_KEY_TRACK, GROUP_FILTER, "VCF Key Track", "KTrack", 0.0f, 1.0f, 0.50f, CURVE_LIN, UNIT_PCT, "%.2f",
     0xFF, 5.0f, FLAG_PER_VOICE},
    // VCF_LFO_DEPTH: scales LFO1 contribution to VCF cutoff (panel knob, not matrix).
    {ParamId::VCF_LFO_DEPTH, GROUP_FILTER, "VCF LFO Depth", "LFODep", 0.0f, 1.0f, 0.0f, CURVE_LIN, UNIT_PCT, "%.2f", 24,
     5.0f, FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},

    // --- HPF ---
    // Juno-106 HPF: 4-position discrete switch on the original hardware; modeled
    // here as a continuous cutoff for finer control. DSP hook: adding the HPF
    // requires a second dsp::Filter object — that is a separate sub-stage (Split-if
    // hit at Stage 3c-i). This row exists so the param is addressable from the param
    // store, MIDI CC, and presets; set_param caches the value but takes no action
    // until the DSP block lands.
    {ParamId::HPF_CUTOFF, GROUP_HPF, "HPF Cutoff", "HPF", 20.0f, 1000.0f, 20.0f, CURVE_EXP, UNIT_HZ, "%.0f", 0xFF, 5.0f,
     FLAG_PER_VOICE},

    // --- ENV ---
    {ParamId::ENV_ATTACK, GROUP_ENV, "Attack", "Atk", 0.001f, 5.0f, 0.010f, CURVE_EXP, UNIT_SEC, "%.3f", 73, 10.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV_DECAY, GROUP_ENV, "Decay", "Dec", 0.001f, 5.0f, 0.100f, CURVE_EXP, UNIT_SEC, "%.3f", 0xFF, 10.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV_SUSTAIN, GROUP_ENV, "Sustain", "Sus", 0.0f, 1.0f, 0.700f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 0.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV_RELEASE, GROUP_ENV, "Release", "Rel", 0.001f, 5.0f, 0.300f, CURVE_EXP, UNIT_SEC, "%.3f", 28, 10.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},

    // --- FX (Juno chorus) ---
    {ParamId::CHORUS_RATE, GROUP_FX, "Chorus Rate", "ChoRt", 0.05f, 5.0f, 0.500f, CURVE_EXP, UNIT_HZ, "%.2f", 0xFF,
     20.0f, 0},
    {ParamId::CHORUS_DEPTH, GROUP_FX, "Chorus Depth", "ChoDep", 0.0f, 1.0f, 0.700f, CURVE_LIN, UNIT_PCT, "%.2f", 25,
     20.0f, 0},
    {ParamId::CHORUS_DELAY, GROUP_FX, "Chorus Delay", "ChoDly", 0.0f, 1.0f, 0.400f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF,
     20.0f, 0},
    // CHORUS_MODE: 0=off (dry), 1=Chorus I (slow, lush), 2=Chorus II (fast, wide).
    // Implemented as: 0 = bypass chorus; 1/2 = preset rate+depth scaled in synth.cpp.
    {ParamId::CHORUS_MODE, GROUP_FX, "Chorus Mode", "ChoMod", 0.0f, 2.0f, 1.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF,
     0.0f, 0},

    // --- ENV2 (filter/mod envelope) ---
    // A second ADSR used as a modulation source (Stage 3a). VCF_ENV_DEPTH scales
    // its contribution to the VCF cutoff. Defaults mimic a typical Juno filter-env
    // shape (fast attack, medium decay, no sustain).
    {ParamId::ENV2_ATTACK, GROUP_ENV2, "Env2 Attack", "E2Atk", 0.001f, 5.0f, 0.005f, CURVE_EXP, UNIT_SEC, "%.3f", 0xFF,
     10.0f, FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV2_DECAY, GROUP_ENV2, "Env2 Decay", "E2Dec", 0.001f, 5.0f, 0.200f, CURVE_EXP, UNIT_SEC, "%.3f", 0xFF,
     10.0f, FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV2_SUSTAIN, GROUP_ENV2, "Env2 Sustain", "E2Sus", 0.0f, 1.0f, 0.000f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF,
     0.0f, FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::ENV2_RELEASE, GROUP_ENV2, "Env2 Release", "E2Rel", 0.001f, 5.0f, 0.200f, CURVE_EXP, UNIT_SEC, "%.3f",
     0xFF, 10.0f, FLAG_PER_VOICE | FLAG_MOD_DEST},

    // --- LFO1 ---
    // Per-voice LFO, sub-audio rate. Shape is a stepped int (see LfoWave enum
    // in dsp/lfo.h: SINE=0, TRI=1, SAW=2, SQUARE=4, SH=5). Depth is a
    // normalised scale factor; the mod matrix (Stage 3b-i) interprets it.
    // LFO1_DELAY: fade-in time after note_on; LFO ramps from 0 to full depth
    // over this many seconds (counter-based in render, no new DSP block needed).
    {ParamId::LFO1_RATE, GROUP_LFO, "LFO1 Rate", "L1Rt", 0.01f, 20.0f, 1.0f, CURVE_EXP, UNIT_HZ, "%.2f", 27, 5.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::LFO1_DEPTH, GROUP_LFO, "LFO1 Depth", "L1Dep", 0.0f, 1.0f, 0.5f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 5.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::LFO1_SHAPE, GROUP_LFO, "LFO1 Shape", "L1Shp", 0.0f, 5.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF,
     0.0f, FLAG_PER_VOICE},
    {ParamId::LFO1_DELAY, GROUP_LFO, "LFO1 Delay", "L1Dly", 0.0f, 5.0f, 0.0f, CURVE_LIN, UNIT_SEC, "%.2f", 0xFF, 5.0f,
     FLAG_PER_VOICE},

    // --- LFO2 ---
    {ParamId::LFO2_RATE, GROUP_LFO, "LFO2 Rate", "L2Rt", 0.01f, 20.0f, 0.5f, CURVE_EXP, UNIT_HZ, "%.2f", 0xFF, 5.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::LFO2_DEPTH, GROUP_LFO, "LFO2 Depth", "L2Dep", 0.0f, 1.0f, 0.5f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 5.0f,
     FLAG_PER_VOICE | FLAG_MOD_DEST},
    {ParamId::LFO2_SHAPE, GROUP_LFO, "LFO2 Shape", "L2Shp", 0.0f, 5.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF,
     0.0f, FLAG_PER_VOICE},
    {ParamId::LFO2_DELAY, GROUP_LFO, "LFO2 Delay", "L2Dly", 0.0f, 5.0f, 0.0f, CURVE_LIN, UNIT_SEC, "%.2f", 0xFF, 5.0f,
     FLAG_PER_VOICE},

    // --- AMP / MIX ---
    // MASTER_GAIN: global output gain. Default 1.0 = unity (ADR 0021 amendment).
    //   CC7 removed (ADR 0021): CC7 is now an attenuation-only channel volume routed
    //   through engine_set_channel_volume(), not the param table. MASTER_GAIN is a
    //   manual headroom/output-trim knob only; no longer reachable by MIDI.
    // VCA_GATE_MODE: 0=envelope drives VCA (normal), 1=gate (hard on/off, no envelope).
    // VCA_LEVEL: per-voice output level; multiplies into the VCA after the envelope.
    {ParamId::MASTER_GAIN, GROUP_AMP, "Master Gain", "Gain", 0.0f, 2.0f, 1.000f, CURVE_LIN, UNIT_NONE, "%.2f", 0xFF,
     10.0f, FLAG_AUDIO_RATE},
    {ParamId::VCA_GATE_MODE, GROUP_AMP, "VCA Gate Mode", "VcaGt", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f",
     0xFF, 0.0f, FLAG_PER_VOICE},
    {ParamId::VCA_LEVEL, GROUP_AMP, "VCA Level", "VcaLvl", 0.0f, 1.0f, 1.0f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 5.0f,
     FLAG_AUDIO_RATE | FLAG_PER_VOICE | FLAG_MOD_DEST},

    // --- Stage 3d-i: play modes (global, not per-voice) ---
    // PLAY_MODE: 0=poly (default), 1=mono+retrigger, 2=mono+legato.
    //   Retrigger: new note while one is held restarts envelopes.
    //   Legato: new note while one is held continues envelopes (no retrigger).
    //   Portamento applies in both mono modes when PORTAMENTO_TIME > 0.
    // PORTAMENTO_TIME: glide time (seconds). 0 = snap; max 2 s. Log taper so
    //   the bottom half of the knob covers short glide times (< 0.5 s). Values
    //   below 0.001 s are treated as zero (off) in VoiceAlloc.
    {ParamId::PLAY_MODE, GROUP_AMP, "Play Mode", "Mode", 0.0f, 2.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
    {ParamId::PORTAMENTO_TIME, GROUP_AMP, "Portamento", "Porto", 0.0f, 2.0f, 0.0f, CURVE_LOG, UNIT_SEC, "%.3f", 5, 0.0f,
     0},

    // --- Stage 3d-ii: unison (global, not per-voice) ---
    // UNISON_COUNT: number of voices stacked per note (1 = off, up to kNumVoices = 6).
    //   Stepped int. U=1 is identical to the existing poly/mono path (no change).
    //   Effective polyphony = floor(kNumVoices / UNISON_COUNT). Pool exhaustion falls
    //   back to the normal steal policy; no allocation beyond the fixed pool.
    // UNISON_DETUNE: total spread of the detuned voices in cents (0 = unison/no detune,
    //   50 = ±25 cents across the stack). Voices are spread evenly; the centre voice
    //   (or centre pair) is closest to 0. Converted to semitones internally.
    {ParamId::UNISON_COUNT, GROUP_AMP, "Unison Count", "UniCnt", 1.0f, 8.0f, 1.0f, CURVE_STEPPED, UNIT_NONE, "%.0f",
     0xFF, 0.0f, 0},
    {ParamId::UNISON_DETUNE, GROUP_AMP, "Unison Detune", "UniDet", 0.0f, 50.0f, 7.0f, CURVE_LIN, UNIT_CENT, "%.1f", 26,
     5.0f, 0},

    // --- ARP (arpeggiator control, Stage 4b-ii) ---
    // ARP_RATE stepped index 0..5 → {1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32} at 96 PPQN; default 3 = 1/16.
    // Gate/Swing are continuous [0..1]-range scalars; index-to-ticks mapping wired in Stage 4b-iii.
    // All ARP params: midi_cc=0xFF (unassigned), smoothing=0 (instant), flags=0 (global, not per-voice).
    {ParamId::ARP_ON, GROUP_ARP, "Arp On", "ARP", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f, 0},
    {ParamId::ARP_MODE, GROUP_ARP, "Arp Mode", "MODE", 0.0f, 4.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
    {ParamId::ARP_RATE, GROUP_ARP, "Arp Rate", "RATE", 0.0f, 5.0f, 3.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
    {ParamId::ARP_OCTAVES, GROUP_ARP, "Octaves", "OCT", 1.0f, 4.0f, 1.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
    {ParamId::ARP_GATE, GROUP_ARP, "Gate", "GATE", 0.05f, 1.0f, 0.5f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 0.0f, 0},
    {ParamId::ARP_SWING, GROUP_ARP, "Swing", "SWING", 0.0f, 0.75f, 0.0f, CURVE_LIN, UNIT_PCT, "%.2f", 0xFF, 0.0f, 0},
    {ParamId::ARP_LATCH, GROUP_ARP, "Latch", "LATCH", 0.0f, 1.0f, 0.0f, CURVE_STEPPED, UNIT_NONE, "%.0f", 0xFF, 0.0f,
     0},
};
// NOLINTEND

const int kJunoParamCount = (int)(sizeof(JUNO_PARAM_TABLE) / sizeof(JUNO_PARAM_TABLE[0]));
