// engine/juno_voice.h — Juno-106-inspired voice (ADR 0002, ADR 0008).
// Signal chain: PolyBLEP saw + sub (−1 oct) + noise → SVF LP → ADSR VCA.
// set_param() uses ParamId::* values (see param_id.h) — the param store
// pushes smoothed physical values from the table every block (Stage 2b).
//
// Stage 3a additions:
//   env2_  — second ADSR (filter/mod env); rendered per voice; output readable
//             via env2_value() for the mod matrix (Stage 3b-i).
//   lfo1_raw_/lfo2_raw_ — injected each block by synth_render via set_lfo_inputs()
//     (ADR 0018: shared free-running LFO; per-note delay fade-in stays per-voice).
//
// Stage 3b-i additions:
//   mod_matrix_ — fixed 16-slot modulation matrix (ADR 0009).
//   set_mod_matrix / get_mod_matrix — wire in/read out the routing table.
//   apply_mod_outputs (private) — hot-path application of ModOutputs.
#pragma once

#include "Noise/whitenoise.h"
#include "dsp/env.h"
#include "dsp/filter.h"
#include "dsp/lfo.h"
#include "dsp/osc.h"
#include "mod_matrix.h"
#include "param_id.h"
#include "voice.h"

class JunoVoice final : public IVoice {
   public:
    // Call before the first note; allocation allowed here.
    void init(float sample_rate);

    void note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) override;
    void note_off() override;
    void reset() override;
    void set_param(int id, float value) override;
    void render(float* buf, size_t n) override;
    bool is_active() const override;

    // --- Stage 3a: mod-source accessors (for the mod matrix in Stage 3b-i) ---
    // Returns the last rendered per-sample output of each mod source.
    // Only meaningful after at least one call to render().
    float env2_value() const {
        return env2_value_;
    }
    float lfo1_value() const {
        return lfo1_value_;
    }
    float lfo2_value() const {
        return lfo2_value_;
    }

    // --- Stage 3b-i: modulation matrix wiring ---
    // Replace the entire 16-slot routing table for this voice.
    void set_mod_matrix(const ModMatrix& m) override {
        mod_matrix_ = m;
    }

    // Direct access to edit individual slots in place (avoids a full copy).
    ModMatrix& mod_matrix() {
        return mod_matrix_;
    }
    const ModMatrix& mod_matrix() const {
        return mod_matrix_;
    }

    // IVoice: per-block pitch offset for portamento glide (semitones).
    void set_pitch_offset(float semitones) override {
        p_pitch_offset_ = semitones;
    }

    // IVoice: inject shared engine LFO raw outputs for this block (ADR 0018).
    // The voice applies per-note delay scale and depth; lfo*_raw_ are in [-1,+1].
    void set_lfo_inputs(float lfo1_raw, float lfo2_raw) override {
        lfo1_raw_ = lfo1_raw;
        lfo2_raw_ = lfo2_raw;
    }

   private:
    float   sample_rate_    = 48000.0f;
    bool    gate_           = false;
    float   vel_scale_      = 1.0f;
    uint8_t midi_note_      = 69;    // MIDI pitch from last note_on (for key_track)
    float   p_pitch_offset_ = 0.0f;  // portamento glide offset (semitones, from alloc)

    // Stage 3b-i: mod matrix instance (one per voice).
    ModMatrix mod_matrix_;

    dsp::Osc            osc_main_;
    dsp::Osc            osc_sub_;
    daisysp::WhiteNoise noise_;
    dsp::Filter         filter_;
    dsp::Env            env_;

    // --- Stage 3a: second envelope ---
    dsp::Env env2_;

    // Last rendered values from mod sources (updated once per render() call
    // at the last sample of the block — close enough for block-rate routing).
    float env2_value_ = 0.0f;
    float lfo1_value_ = 0.0f;  // depth+delay-scaled; fed into mod matrix
    float lfo2_value_ = 0.0f;

    // ADR 0018: shared free-running LFO raw outputs injected by synth_render each
    // block via set_lfo_inputs(). In [-1,+1] before per-note depth+delay scaling.
    float lfo1_raw_ = 0.0f;
    float lfo2_raw_ = 0.0f;

    // Param cache — physical values pushed from the param store.
    float p_osc_level_        = 0.70f;
    float p_sub_level_        = 0.30f;
    float p_noise_level_      = 0.05f;
    float p_osc_pwm_          = 0.50f;  // pulse-width amount (0=narrow..1=wide; cache-only until osc gains set_pw)
    int   p_osc_waveform_     = 0;      // waveform select: 0=saw, 1=pulse, 2=tri
    float p_osc_range_semi_   = 0.0f;   // DCO range offset in semitones
    float p_cutoff_           = 2000.0f;
    float p_res_              = 0.30f;
    float p_vcf_env_depth_    = 0.35f;   // ENV2 → VCF mod depth
    int   p_vcf_env_polarity_ = 0;       // 0=positive, 1=negative
    float p_vcf_key_track_    = 0.50f;   // key-follow amount (0..1)
    float p_vcf_lfo_depth_    = 0.0f;    // LFO1 → VCF panel mod depth
    float p_hpf_cutoff_       = 20.0f;   // HPF cutoff (cached; DSP block pending)
    float p_attack_           = 0.010f;  // seconds
    float p_decay_            = 0.100f;
    float p_sustain_          = 0.700f;
    float p_release_          = 0.300f;
    // ENV2 params
    float p_env2_attack_      = 0.005f;
    float p_env2_decay_       = 0.200f;
    float p_env2_sustain_     = 0.000f;
    float p_env2_release_     = 0.200f;
    // LFO params (rate+shape removed: engine owns them via the shared LFO — ADR 0018)
    float p_lfo1_depth_       = 0.5f;
    float p_lfo1_delay_       = 0.0f;  // fade-in delay time (seconds)
    float p_lfo2_depth_       = 0.5f;
    float p_lfo2_delay_       = 0.0f;  // fade-in delay time (seconds)
    // VCA params
    int   p_vca_gate_mode_    = 0;     // 0=env, 1=gate
    float p_vca_level_        = 1.0f;  // per-voice output level
    // LFO delay state: sample counter used to implement the fade-in ramp.
    // Reset on note_on; increments each sample; depth scales from 0→1 over delay_samples.
    float lfo1_delay_samples_ = 0.0f;  // total fade-in length in samples
    float lfo2_delay_samples_ = 0.0f;
    float lfo1_delay_pos_     = 0.0f;  // current sample position in fade-in
    float lfo2_delay_pos_     = 0.0f;
};
