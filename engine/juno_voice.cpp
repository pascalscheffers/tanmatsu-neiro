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

#include "Utility/dsp.h"  // daisysp::mtof
#include "juno_voice.h"
#include "mod_matrix.h"
#include "param_id.h"
#include "synth_config.h"  // kPitchBendRangeSemis (Stage 5c)

void JunoVoice::init(float sample_rate) {
    sample_rate_ = sample_rate;
    gate_        = false;
    vel_scale_   = 1.0f;

    osc_main_.init(sample_rate);
    osc_sub_.init(sample_rate);
    noise_.Init();
    filter_.init(sample_rate);
    env_.init(sample_rate);

    // Stage 3a: init second envelope.
    // ADR 0018: LFOs moved to engine (shared free-running); no per-voice init.
    env2_.init(sample_rate);

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
}

void JunoVoice::note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    (void)expr;  // MPE fields wired in Stage 5

    midi_note_ = pitch;
    float freq = daisysp::mtof((float)pitch);
    osc_main_.set_freq(freq);
    osc_sub_.set_freq(freq * 0.5f);  // -1 octave sub

    vel_scale_ = (float)velocity / 127.0f;
    gate_      = true;

    // Reset LFO delay fade-in counters on each new note.
    lfo1_delay_pos_ = 0.0f;
    lfo2_delay_pos_ = 0.0f;
    // Zero the injected raw values so the one-block-latency mod-matrix read
    // doesn't see a stale value from a previous note (ADR 0018).
    lfo1_raw_       = 0.0f;
    lfo2_raw_       = 0.0f;
}

void JunoVoice::note_off() {
    gate_ = false;
}

// Stage 5c: inject channel-wide MIDI expression (called once per block by synth_render,
// before render(), mirroring set_lfo_inputs). pitch_bend is bipolar [-1,+1]; the voice
// scales it by kPitchBendRangeSemis and adds it directly to the pitch calculation.
void JunoVoice::set_expression(float mod_wheel, float pitch_bend, float aftertouch) {
    p_mod_wheel_  = mod_wheel;
    p_pitch_bend_ = pitch_bend;
    p_aftertouch_ = aftertouch;
}

void JunoVoice::reset() {
    gate_      = false;
    vel_scale_ = 1.0f;
    env_.reset();  // re-init to IDLE: no release tail, instant silence
    env2_.reset();
    osc_main_.reset();
    osc_sub_.reset();
    // ADR 0018: per-voice LFOs removed; engine owns the shared free-running LFO.
    env2_value_ = 0.0f;
    lfo1_value_ = 0.0f;
    lfo2_value_ = 0.0f;
    // Note: mod_matrix_ is not cleared on voice reset — the routing table is
    // patch data and persists across note events (Stage 3b-ii sets it from preset).
}

void JunoVoice::set_param(int id, float value) {
    switch (id) {
        // --- OSC ---
        case ParamId::OSC_LEVEL:
            p_osc_level_ = value;
            break;
        case ParamId::SUB_LEVEL:
            p_sub_level_ = value;
            break;
        case ParamId::NOISE_LEVEL:
            p_noise_level_ = value;
            break;
        // OSC_PWM: pulse-width base value; applied in render() via osc_main_.set_pw().
        case ParamId::OSC_PWM:
            p_osc_pwm_ = value;
            break;
        // OSC_WAVEFORM: 0=SAW, 1=PULSE, 2=TRI; applied in render() via osc_main_.set_waveform().
        case ParamId::OSC_WAVEFORM:
            p_osc_waveform_ = (int)value;
            break;
        // OSC_RANGE: semitone offset applied to base freq in render().
        case ParamId::OSC_RANGE:
            p_osc_range_semi_ = value;
            break;

        // --- FILTER ---
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
        case ParamId::VCF_ENV_DEPTH:
            p_vcf_env_depth_ = value;
            break;
        case ParamId::VCF_ENV_POLARITY:
            p_vcf_env_polarity_ = (int)value;
            break;
        case ParamId::VCF_KEY_TRACK:
            p_vcf_key_track_ = value;
            break;
        case ParamId::VCF_LFO_DEPTH:
            p_vcf_lfo_depth_ = value;
            break;
        // HPF_CUTOFF: cached; DSP block (second Filter) is a separate sub-stage.
        case ParamId::HPF_CUTOFF:
            p_hpf_cutoff_ = value;
            break;

        // --- ENV ---
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
        // LFO1_RATE and LFO1_SHAPE are no longer handled per-voice (ADR 0018:
        // shared engine LFO; rate+shape configured on s_lfo1/s_lfo2 in synth.cpp).
        case ParamId::LFO1_DEPTH:
            p_lfo1_depth_ = value;
            break;
        case ParamId::LFO1_DELAY:
            p_lfo1_delay_       = value;
            lfo1_delay_samples_ = value * sample_rate_;
            break;

        // --- Stage 3a: LFO2 ---
        // LFO2_RATE and LFO2_SHAPE: engine-owned (ADR 0018); voice ignores them.
        case ParamId::LFO2_DEPTH:
            p_lfo2_depth_ = value;
            break;
        case ParamId::LFO2_DELAY:
            p_lfo2_delay_       = value;
            lfo2_delay_samples_ = value * sample_rate_;
            break;

        // --- Stage 3c-i: VCA ---
        case ParamId::VCA_GATE_MODE:
            p_vca_gate_mode_ = (int)value;
            break;
        case ParamId::VCA_LEVEL:
            p_vca_level_ = value;
            break;

        default:
            break;
    }
}

IRAM_ATTR void JunoVoice::render(float* buf, size_t n) {
    // Early exit when both envelopes are idle (post-release or pre-first-note).
    // ADR 0018: LFO phase is now engine-owned (free-running); the engine advances
    // the shared LFO unconditionally every block and injects via set_lfo_inputs().
    if (!gate_ && env_.is_idle() && env2_.is_idle()) {
        return;
    }

    // Stage 3b-i: evaluate mod matrix once per block (control-rate eval).
    // Per-voice sources are the last-block cached values (close enough for 1 block
    // of latency; exact per-sample mod is not needed at control rate).
    // Key-track: center on MIDI note 69 (A4), ±1 unit per semitone / 12 → [-1,+1]
    // across a ±1-octave range. Clamped to [-1, +1].
    float key_track_raw = ((float)midi_note_ - 69.0f) / 12.0f;
    if (key_track_raw > 1.0f) key_track_raw = 1.0f;
    if (key_track_raw < -1.0f) key_track_raw = -1.0f;

    ModSources msrc;
    msrc.lfo1       = lfo1_value_;
    msrc.lfo2       = lfo2_value_;
    msrc.env1       = 0.0f;  // amp env not yet cached; filled below if needed
    msrc.env2       = env2_value_;
    msrc.velocity   = vel_scale_;  // [0,1]
    msrc.key_track  = key_track_raw;
    // Stage 5c: global MIDI expression — injected each block via set_expression().
    msrc.mod_wheel  = p_mod_wheel_;
    msrc.pitch_bend = p_pitch_bend_;
    msrc.aftertouch = p_aftertouch_;

    ModOutputs mout = mod_matrix_.eval(msrc);

    // --- Control-rate mod applications (once per block) ---
    // Resonance:
    float eff_res = p_res_ + mout.res_mod;
    if (eff_res < 0.0f) eff_res = 0.0f;
    if (eff_res > 1.0f) eff_res = 1.0f;
    filter_.set_res(eff_res);

    // --- Audio-rate mod: compute start/end values for per-sample interpolation.
    // Pitch (semitone offset → freq). OSC_RANGE adds a fixed offset in semitones.
    // p_pitch_offset_ is a portamento glide semitone offset set by VoiceAlloc each
    // block; it is already block-rate-smoothed by the allocator.
    // Stage 5c: direct pitch-bend path — always-on, ±kPitchBendRangeSemis.
    // Flows into both base_freq and mod_freq_end so bend is smooth across the block.
    // The pitch_bend mod-matrix SOURCE (msrc.pitch_bend) remains available for patches
    // but the direct path is primary. Both can coexist without double-counting because
    // the matrix source is only used when a patch route maps it — which is additive.
    float range_semi   = p_osc_range_semi_ + p_pitch_offset_ + p_pitch_bend_ * kPitchBendRangeSemis;
    float base_freq    = daisysp::mtof((float)midi_note_ + range_semi);
    float mod_freq_end = daisysp::mtof((float)midi_note_ + range_semi + mout.pitch_semi);

    // Cutoff: built-in panel mods (ENV depth, key-track, LFO) added on top of
    // the matrix cutoff_mod. kEnvModRange = 8000 Hz: ENV2 at depth=1 shifts
    // cutoff ±8 kHz (centered, so 2000 Hz + 8000 = 10 kHz full open).
    static constexpr float kEnvModRange         = 8000.0f;
    // ENV polarity: 1.0f = positive, -1.0f = negative.
    float                  env_sign             = (p_vcf_env_polarity_ != 0) ? -1.0f : 1.0f;
    // Key-track mod: VCF_KEY_TRACK scales the key_track_raw contribution.
    // Full (1.0) = ±1-octave shift across key range; scaled linearly by knob.
    static constexpr float kKeyTrackRange       = 4000.0f;  // Hz per unit of key_track_raw
    // Mod wheel → cutoff: hardwired additive brightener (like the panel mods above).
    // p_mod_wheel_ [0,1] is injected each block by set_expression(); additive on top of
    // the patch cutoff so wheel at 0 = patch unchanged. (Stage 5c Launchkey mapping.)
    static constexpr float kModWheelCutoffRange = 8000.0f;  // wheel fully open adds +8 kHz
    float cutoff_end = p_cutoff_ + mout.cutoff_mod + env2_value_ * p_vcf_env_depth_ * env_sign * kEnvModRange +
                       key_track_raw * p_vcf_key_track_ * kKeyTrackRange +
                       lfo1_value_ * p_vcf_lfo_depth_ * kEnvModRange + p_mod_wheel_ * kModWheelCutoffRange;
    if (cutoff_end < 20.0f) cutoff_end = 20.0f;
    if (cutoff_end > 20000.0f) cutoff_end = 20000.0f;

    // Amp (OSC_LEVEL mod), clamped [0, 1]:
    float amp_end = p_osc_level_ + mout.amp_mod;
    if (amp_end < 0.0f) amp_end = 0.0f;
    if (amp_end > 1.0f) amp_end = 1.0f;

    // Waveform: apply once per block (waveform switch is not audio-rate; block-rate is fine).
    osc_main_.set_waveform(p_osc_waveform_);

    // PWM: apply once per block (block-rate, ~750 Hz @ 64/48k — ample for a slow LFO sweep).
    // Clamp [0.05, 0.95] to avoid degenerate silent/full-duty pulse at the extremes.
    float pw = p_osc_pwm_ + mout.pwm_mod;
    if (pw < 0.05f) pw = 0.05f;
    if (pw > 0.95f) pw = 0.95f;
    osc_main_.set_pw(pw);

    // Sub / noise level mods (also once per block — fast enough):
    float eff_sub   = p_sub_level_ + mout.osc_sub;
    float eff_noise = p_noise_level_ + mout.osc_noise;
    if (eff_sub < 0.0f) eff_sub = 0.0f;
    if (eff_sub > 1.0f) eff_sub = 1.0f;
    if (eff_noise < 0.0f) eff_noise = 0.0f;
    if (eff_noise > 1.0f) eff_noise = 1.0f;

    // Filter cutoff updated once per block (block-rate): SetFreq computes sinf+powf,
    // so calling it per sample is prohibitively expensive on RISC-V without hw trig.
    // At 64-sample blocks (48 kHz) this gives 750 Hz mod bandwidth — inaudible limit
    // for Juno-style filter sweeps. Must follow set_res() because SetFreq reads res_.
    filter_.set_freq(cutoff_end);

    // Block-smooth audio-rate dests: linear interpolation from prev to end
    // value over n samples (avoids zipper noise on fast LFO modulation).
    const float inv_n = (n > 1) ? (1.0f / (float)(n - 1)) : 1.0f;

    // We do per-sample freq ramp; enough for click-free modulation at 64 samples.
    float freq_step = (mod_freq_end - base_freq) * inv_n;
    float amp_step  = (amp_end - p_osc_level_) * inv_n;

    float e2 = env2_value_;

    for (size_t i = 0; i < n; i++) {
        // Per-sample modulated freq (smooth pitch mod).
        float cur_freq = base_freq + freq_step * (float)i;
        float cur_amp  = p_osc_level_ + amp_step * (float)i;

        osc_main_.set_freq(cur_freq);
        osc_sub_.set_freq(cur_freq * 0.5f);
        // filter_.set_freq is called once per block above (block-rate cutoff).

        float osc   = osc_main_.process() * cur_amp;
        float sub   = osc_sub_.process() * eff_sub;
        float noise = noise_.Process() * eff_noise;
        float mixed = osc + sub + noise;

        filter_.process(mixed);  // anti-denormal inside filter.h
        float filtered = filter_.output();

        // VCA: gate-mode selects between envelope output and raw gate (1.0).
        float env_val;
        if (p_vca_gate_mode_ != 0) {
            env_val = gate_ ? vel_scale_ : 0.0f;  // gate mode: hard on/off
        } else {
            env_val = env_.process(gate_) * vel_scale_;
        }
        buf[i] += filtered * env_val * p_vca_level_;

        // Stage 3a: advance ENV2 at audio rate so its per-sample state machine
        // is exact; cache the last sample for the next block's mod-matrix eval.
        e2 = env2_.process(gate_);
    }

    // ADR 0018: LFO raw outputs are injected each block by synth_render via
    // set_lfo_inputs(). The engine owns and advances the shared free-running LFO;
    // we apply per-note delay fade-in scale and depth here (still per-voice).
    // LFO delay fade-in: advance the position counter by the whole block, then
    // compute the applied-depth scale once (block-granular fade is inaudible).
    if (gate_) {
        if (lfo1_delay_pos_ < lfo1_delay_samples_) {
            lfo1_delay_pos_ += (float)n;
            if (lfo1_delay_pos_ > lfo1_delay_samples_) lfo1_delay_pos_ = lfo1_delay_samples_;
        }
        if (lfo2_delay_pos_ < lfo2_delay_samples_) {
            lfo2_delay_pos_ += (float)n;
            if (lfo2_delay_pos_ > lfo2_delay_samples_) lfo2_delay_pos_ = lfo2_delay_samples_;
        }
    }
    float l1_delay_scale =
        (lfo1_delay_samples_ < 1.0f)
            ? 1.0f
            : (lfo1_delay_pos_ >= lfo1_delay_samples_ ? 1.0f : lfo1_delay_pos_ / lfo1_delay_samples_);
    float l2_delay_scale =
        (lfo2_delay_samples_ < 1.0f)
            ? 1.0f
            : (lfo2_delay_pos_ >= lfo2_delay_samples_ ? 1.0f : lfo2_delay_pos_ / lfo2_delay_samples_);
    float l1 = lfo1_raw_ * p_lfo1_depth_ * l1_delay_scale;
    float l2 = lfo2_raw_ * p_lfo2_depth_ * l2_delay_scale;

    // Cache last-block values for the mod matrix accessor.
    env2_value_ = e2;
    lfo1_value_ = l1;
    lfo2_value_ = l2;
}

bool JunoVoice::is_active() const {
    // Active while gate is held OR the release tail is still running.
    return gate_ || !env_.is_idle();
}
