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

    filter_.set_freq(p_cutoff_);
    filter_.set_res(p_res_);
    env_.set_attack(p_attack_);
    env_.set_decay(p_decay_);
    env_.set_sustain(p_sustain_);
    env_.set_release(p_release_);
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
    osc_main_.reset();
    osc_sub_.reset();
}

void JunoVoice::set_param(int id, float value) {
    switch (id) {
        case JUNO_PARAM_OSC_LEVEL:
            p_osc_level_ = value;
            break;
        case JUNO_PARAM_SUB_LEVEL:
            p_sub_level_ = value;
            break;
        case JUNO_PARAM_NOISE_LEVEL:
            p_noise_level_ = value;
            break;
        case JUNO_PARAM_FILTER_CUTOFF:
            p_cutoff_ = value;
            filter_.set_freq(value);
            break;
        case JUNO_PARAM_FILTER_RES:
            p_res_ = value;
            filter_.set_res(value);
            break;
        case JUNO_PARAM_FILTER_MODE:
            filter_.set_mode((dsp::FilterMode)(int)value);
            break;
        case JUNO_PARAM_ENV_ATTACK:
            p_attack_ = value;
            env_.set_attack(value);
            break;
        case JUNO_PARAM_ENV_DECAY:
            p_decay_ = value;
            env_.set_decay(value);
            break;
        case JUNO_PARAM_ENV_SUSTAIN:
            p_sustain_ = value;
            env_.set_sustain(value);
            break;
        case JUNO_PARAM_ENV_RELEASE:
            p_release_ = value;
            env_.set_release(value);
            break;
        default:
            break;
    }
}

IRAM_ATTR void JunoVoice::render(float* buf, size_t n) {
    // Early exit when envelope is idle (post-release or pre-first-note).
    if (!gate_ && env_.is_idle()) {
        return;
    }

    for (size_t i = 0; i < n; i++) {
        float osc   = osc_main_.process() * p_osc_level_;
        float sub   = osc_sub_.process()  * p_sub_level_;
        float noise = noise_.Process()     * p_noise_level_;
        float mixed = osc + sub + noise;

        filter_.process(mixed);           // anti-denormal inside filter.h
        float filtered = filter_.output();

        float env_val = env_.process(gate_) * vel_scale_;
        buf[i] += filtered * env_val;
    }
}

bool JunoVoice::is_active() const {
    // Active while gate is held OR the release tail is still running.
    return gate_ || !env_.is_idle();
}
