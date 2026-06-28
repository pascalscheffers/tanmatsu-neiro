// engine/juno_voice.h — Juno-106-inspired voice (ADR 0002, ADR 0008).
// Signal chain: PolyBLEP saw + sub (−1 oct) + noise → SVF LP → ADSR VCA.
// set_param() uses ParamId::* values (see param_id.h) — the param store
// pushes smoothed physical values from the table every block (Stage 2b).
#pragma once

#include "voice.h"
#include "param_id.h"
#include "dsp/osc.h"
#include "dsp/filter.h"
#include "dsp/env.h"
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

private:
    float              sample_rate_ = 48000.0f;
    bool               gate_        = false;
    float              vel_scale_   = 1.0f;

    dsp::Osc           osc_main_;
    dsp::Osc           osc_sub_;
    daisysp::WhiteNoise noise_;
    dsp::Filter        filter_;
    dsp::Env           env_;

    // Hardcoded defaults — Stage 2 lifts these to the param table.
    float p_osc_level_   = 0.70f;
    float p_sub_level_   = 0.30f;
    float p_noise_level_ = 0.05f;
    float p_cutoff_      = 2000.0f;
    float p_res_         = 0.30f;
    float p_attack_      = 0.010f;  // seconds
    float p_decay_       = 0.100f;
    float p_sustain_     = 0.700f;
    float p_release_     = 0.300f;
};
