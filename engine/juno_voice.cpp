// engine/juno_voice.cpp — Juno-106-inspired voice implementation.
// See juno_voice.h for architecture and param-table design note.
// IRAM_ATTR (ADR 0013): render() placed in IRAM so it survives a flash write.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

#include "juno_voice.h"
#include "param_id.h"
#include "Utility/dsp.h"  // daisysp::mtof

void JunoVoice::init(float sample_rate) {
    sample_rate_ = sample_rate;
    gate_        = false;
    vel_scale_   = 1.0f;

    osc_main_.init(sample_rate);
    osc_sub_.init(sample_rate);
    noise_.Init();
    filter_.init(sample_rate);
    env_.init(sample_rate);

    // Stage 3a: init second envelope and both LFOs.
    env2_.init(sample_rate);
    lfo1_.init(sample_rate);
    lfo2_.init(sample_rate);

    filter_.set_freq(p_cutoff_);
    filter_.set_res(p_res_);
    env_.set_attack(p_attack_);
    env_.set_decay(p_decay_);
    env_.set_sustain(p_sustain_);
    env_.set_release(p_release_);

    env2_.set_attack(p_env2_attack_);
    env2_.set_decay(p_env2_decay_);
    env2_.set_sustain(p_env2_sustain_);
    env2_.set_release(p_env2_release_);

    lfo1_.set_rate(p_lfo1_rate_);
    lfo1_.set_waveform(static_cast<dsp::LfoWave>(p_lfo1_shape_));
    lfo2_.set_rate(p_lfo2_rate_);
    lfo2_.set_waveform(static_cast<dsp::LfoWave>(p_lfo2_shape_));
}

void JunoVoice::note_on(uint8_t pitch, uint8_t velocity,
                        NoteExpression expr) {
    (void)expr;  // MPE fields wired in Stage 5

    float freq = daisysp::mtof((float)pitch);
    osc_main_.set_freq(freq);
    osc_sub_.set_freq(freq * 0.5f);  // -1 octave sub

    vel_scale_ = (float)velocity / 127.0f;
    gate_      = true;
}

void JunoVoice::note_off() {
    gate_ = false;
}

void JunoVoice::reset() {
    gate_      = false;
    vel_scale_ = 1.0f;
    env_.reset();       // re-init to IDLE: no release tail, instant silence
    env2_.reset();
    osc_main_.reset();
    osc_sub_.reset();
    lfo1_.reset();
    lfo2_.reset();
    env2_value_ = 0.0f;
    lfo1_value_ = 0.0f;
    lfo2_value_ = 0.0f;
}

void JunoVoice::set_param(int id, float value) {
    switch (id) {
        case ParamId::OSC_LEVEL:
            p_osc_level_ = value;
            break;
        case ParamId::SUB_LEVEL:
            p_sub_level_ = value;
            break;
        case ParamId::NOISE_LEVEL:
            p_noise_level_ = value;
            break;
        case ParamId::FILTER_CUTOFF:
            p_cutoff_ = value;
            filter_.set_freq(value);
            break;
        case ParamId::FILTER_RES:
            p_res_ = value;
            filter_.set_res(value);
            break;
        case ParamId::FILTER_MODE:
            filter_.set_mode((dsp::FilterMode)(int)value);
            break;
        case ParamId::ENV_ATTACK:
            p_attack_ = value;
            env_.set_attack(value);
            break;
        case ParamId::ENV_DECAY:
            p_decay_ = value;
            env_.set_decay(value);
            break;
        case ParamId::ENV_SUSTAIN:
            p_sustain_ = value;
            env_.set_sustain(value);
            break;
        case ParamId::ENV_RELEASE:
            p_release_ = value;
            env_.set_release(value);
            break;
        // --- Stage 3a: ENV2 ---
        case ParamId::ENV2_ATTACK:
            p_env2_attack_ = value;
            env2_.set_attack(value);
            break;
        case ParamId::ENV2_DECAY:
            p_env2_decay_ = value;
            env2_.set_decay(value);
            break;
        case ParamId::ENV2_SUSTAIN:
            p_env2_sustain_ = value;
            env2_.set_sustain(value);
            break;
        case ParamId::ENV2_RELEASE:
            p_env2_release_ = value;
            env2_.set_release(value);
            break;
        // --- Stage 3a: LFO1 ---
        case ParamId::LFO1_RATE:
            p_lfo1_rate_ = value;
            lfo1_.set_rate(value);
            break;
        case ParamId::LFO1_DEPTH:
            p_lfo1_depth_ = value;
            break;
        case ParamId::LFO1_SHAPE:
            p_lfo1_shape_ = (int)value;
            lfo1_.set_waveform(static_cast<dsp::LfoWave>(p_lfo1_shape_));
            break;
        // --- Stage 3a: LFO2 ---
        case ParamId::LFO2_RATE:
            p_lfo2_rate_ = value;
            lfo2_.set_rate(value);
            break;
        case ParamId::LFO2_DEPTH:
            p_lfo2_depth_ = value;
            break;
        case ParamId::LFO2_SHAPE:
            p_lfo2_shape_ = (int)value;
            lfo2_.set_waveform(static_cast<dsp::LfoWave>(p_lfo2_shape_));
            break;
        default:
            break;
    }
}

IRAM_ATTR void JunoVoice::render(float* buf, size_t n) {
    // Early exit when both envelopes are idle (post-release or pre-first-note).
    if (!gate_ && env_.is_idle() && env2_.is_idle()) {
        // LFOs still tick even when voice is idle (stay phase-coherent for
        // re-trigger). But nothing writes to buf — skip the inner loop.
        return;
    }

    float e2 = env2_value_;
    float l1 = lfo1_value_;
    float l2 = lfo2_value_;

    for (size_t i = 0; i < n; i++) {
        float osc   = osc_main_.process() * p_osc_level_;
        float sub   = osc_sub_.process()  * p_sub_level_;
        float noise = noise_.Process()     * p_noise_level_;
        float mixed = osc + sub + noise;

        filter_.process(mixed);           // anti-denormal inside filter.h
        float filtered = filter_.output();

        float env_val = env_.process(gate_) * vel_scale_;
        buf[i] += filtered * env_val;

        // Stage 3a: advance mod sources (output consumed by mod matrix in 3b-i).
        // Processed at audio rate so they're ready sample-accurate for the matrix.
        e2 = env2_.process(gate_);
        l1 = lfo1_.process() * p_lfo1_depth_;
        l2 = lfo2_.process() * p_lfo2_depth_;
    }

    // Cache last-sample values for the mod matrix accessor.
    env2_value_ = e2;
    lfo1_value_ = l1;
    lfo2_value_ = l2;
}

bool JunoVoice::is_active() const {
    // Active while gate is held OR the release tail is still running.
    return gate_ || !env_.is_idle();
}
