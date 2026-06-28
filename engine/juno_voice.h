// engine/juno_voice.h — Juno-106-inspired voice (ADR 0002, ADR 0008).
// Signal chain: PolyBLEP saw + sub (−1 oct) + noise → SVF LP → ADSR VCA.
// set_param() uses ParamId::* values (see param_id.h) — the param store
// pushes smoothed physical values from the table every block (Stage 2b).
//
// Stage 3a additions:
//   env2_  — second ADSR (filter/mod env); rendered per voice; output readable
//             via env2_value() for the mod matrix (Stage 3b-i).
//   lfo1_, lfo2_ — per-voice LFOs; outputs readable via lfo1_value()/lfo2_value().
//
// Stage 3b-i additions:
//   mod_matrix_ — fixed 16-slot modulation matrix (ADR 0009).
//   set_mod_matrix / get_mod_matrix — wire in/read out the routing table.
//   apply_mod_outputs (private) — hot-path application of ModOutputs.
#pragma once

#include "voice.h"
#include "param_id.h"
#include "mod_matrix.h"
#include "dsp/osc.h"
#include "dsp/filter.h"
#include "dsp/env.h"
#include "dsp/lfo.h"
#include "Noise/whitenoise.h"

class JunoVoice final : public IVoice {
public:
    // Call before the first note; allocation allowed here.
    void init(float sample_rate);

    void note_on(uint8_t pitch, uint8_t velocity,
                 NoteExpression expr) override;
    void note_off() override;
    void reset() override;
    void set_param(int id, float value) override;
    void render(float* buf, size_t n) override;
    bool is_active() const override;

    // --- Stage 3a: mod-source accessors (for the mod matrix in Stage 3b-i) ---
    // Returns the last rendered per-sample output of each mod source.
    // Only meaningful after at least one call to render().
    float env2_value() const { return env2_value_; }
    float lfo1_value() const { return lfo1_value_; }
    float lfo2_value() const { return lfo2_value_; }

    // --- Stage 3b-i: modulation matrix wiring ---
    // Replace the entire 16-slot routing table for this voice.
    void set_mod_matrix(const ModMatrix& m) { mod_matrix_ = m; }

    // Direct access to edit individual slots in place (avoids a full copy).
    ModMatrix& mod_matrix() { return mod_matrix_; }
    const ModMatrix& mod_matrix() const { return mod_matrix_; }

private:
    float              sample_rate_ = 48000.0f;
    bool               gate_        = false;
    float              vel_scale_   = 1.0f;
    uint8_t            midi_note_   = 69;  // MIDI pitch from last note_on (for key_track)

    // Stage 3b-i: mod matrix instance (one per voice).
    ModMatrix          mod_matrix_;

    dsp::Osc           osc_main_;
    dsp::Osc           osc_sub_;
    daisysp::WhiteNoise noise_;
    dsp::Filter        filter_;
    dsp::Env           env_;

    // --- Stage 3a: second envelope + two LFOs ---
    dsp::Env           env2_;
    dsp::Lfo           lfo1_;
    dsp::Lfo           lfo2_;

    // Last rendered values from mod sources (updated once per render() call
    // at the last sample of the block — close enough for block-rate routing).
    float              env2_value_ = 0.0f;
    float              lfo1_value_ = 0.0f;
    float              lfo2_value_ = 0.0f;

    // Param cache — physical values pushed from the param store.
    float p_osc_level_    = 0.70f;
    float p_sub_level_    = 0.30f;
    float p_noise_level_  = 0.05f;
    float p_cutoff_       = 2000.0f;
    float p_res_          = 0.30f;
    float p_attack_       = 0.010f;  // seconds
    float p_decay_        = 0.100f;
    float p_sustain_      = 0.700f;
    float p_release_      = 0.300f;
    // ENV2 params
    float p_env2_attack_  = 0.005f;
    float p_env2_decay_   = 0.200f;
    float p_env2_sustain_ = 0.000f;
    float p_env2_release_ = 0.200f;
    // LFO params
    float p_lfo1_rate_    = 1.0f;
    float p_lfo1_depth_   = 0.5f;
    int   p_lfo1_shape_   = 0;   // LfoWave::SINE
    float p_lfo2_rate_    = 0.5f;
    float p_lfo2_depth_   = 0.5f;
    int   p_lfo2_shape_   = 0;   // LfoWave::SINE
};
