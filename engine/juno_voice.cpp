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
#include "dsp/vendor/kr106/KR106VCA.h"
#include "juno_voice.h"
#include "mod_matrix.h"
#include "param_id.h"
#include "synth_config.h"  // kPitchBendRangeSemis (Stage 5c)

namespace {

// Generated offline from pinned KR-106 commit
// bc15caee5843ab238a25d0969e68d57db2b1615f by evaluating
// ADSR::AttackMs(i / 127) and ADSR::DecRelMs(i / 127), then converting ms
// to seconds. Both tables are monotonic; together they occupy exactly 1 KiB.
static constexpr float kJ106AttackSeconds[128] = {
    0.00100000005f, 0.00523350015f, 0.0137005011f, 0.0221675001f, 0.0306345019f, 0.0433350019f, 0.0518020056f,
    0.0602690056f,  0.0645025074f,  0.0772030056f, 0.0856700018f, 0.0899035111f, 0.0983704999f, 0.111071005f,
    0.119538009f,   0.128005013f,   0.132238507f,  0.140705511f,  0.153406009f,  0.161873013f,  0.166106507f,
    0.17880702f,    0.187274009f,   0.195741013f,  0.204208016f,  0.208441511f,  0.221142009f,  0.229608998f,
    0.233842507f,   0.246543005f,   0.255010009f,  0.263476998f,  0.267710537f,  0.280411035f,  0.284644514f,
    0.297345012f,   0.301578492f,   0.31427902f,   0.318512529f,  0.331213027f,  0.335446507f,  0.343913525f,
    0.356614023f,   0.360847533f,   0.373548031f,  0.38201502f,   0.390482008f,  0.398949027f,  0.403182507f,
    0.415883005f,   0.420116514f,   0.428583503f,  0.437050521f,  0.44551751f,   0.453984529f,  0.462451518f,
    0.475152045f,   0.479385525f,   0.492086023f,  0.496319503f,  0.504786551f,  0.517487049f,  0.525954008f,
    0.534421027f,   0.538654506f,   0.555588543f,  0.568289042f,  0.58098954f,   0.597923577f,  0.606390536f,
    0.623324573f,   0.640258491f,   0.661426067f,  0.674126565f,  0.691060543f,  0.71222806f,   0.737629056f,
    0.763030052f,   0.77996403f,    0.805365026f,  0.834999561f,  0.864634037f,  0.890035033f,  0.923903048f,
    0.962004542f,   1.00433958f,    1.05090797f,   1.08054256f,   1.10170996f,   1.13557804f,   1.15674555f,
    1.19484711f,    1.21601462f,    1.25834954f,   1.28375053f,   1.33455253f,   1.35995352f,   1.41498911f,
    1.44462359f,    1.50812602f,    1.54199409f,   1.60973001f,   1.65206504f,   1.73250151f,   1.77907002f,
    1.87220716f,    1.92724264f,    2.03731346f,   2.10081601f,   2.10081601f,   2.1643188f,    2.1643188f,
    2.23628831f,    2.23628831f,    2.31249118f,   2.31249118f,   2.38869429f,   2.38869429f,   2.47759748f,
    2.47759748f,    2.56650114f,    2.56650114f,   2.66810513f,   2.77394247f,   2.88824725f,   3.01525211f,
    3.15072417f,    3.30313015f};

static constexpr float kJ106DecRelSeconds[128] = {
    0.00423349999f, 0.00846699998f, 0.00846699998f, 0.0127005f,    0.0211674999f, 0.0211674999f, 0.0254009999f,
    0.0296345018f,  0.0296345018f,  0.0381015018f,  0.0423349999f, 0.0508019999f, 0.0592690036f, 0.0762030035f,
    0.101604f,      0.152406007f,   0.156639501f,   0.16510652f,   0.16934f,      0.173573509f,  0.182040513f,
    0.190507516f,   0.194741026f,   0.203207999f,   0.211675018f,  0.224375516f,  0.232842505f,  0.245543018f,
    0.258243501f,   0.275177538f,   0.287878036f,   0.309045523f,  0.325979501f,  0.351380497f,  0.376781553f,
    0.410649538f,   0.444517553f,   0.495319545f,   0.546121538f,  0.618091047f,  0.698527575f,  0.821299076f,
    0.973705113f,   1.22771502f,    1.23618209f,    1.26158321f,   1.29121757f,   1.329319f,     1.35895371f,
    1.39282155f,    1.42668951f,    1.47325814f,    1.50712621f,   1.55369449f,   1.600263f,     1.64683163f,
    1.69763362f,    1.74843562f,    1.80770469f,    1.88390768f,   1.93470967f,   2.00667906f,   2.09558272f,
    2.17601919f,    2.23105454f,    2.43849611f,    2.46389699f,   2.4977653f,    2.53163314f,   2.56973481f,
    2.60360265f,    2.6671052f,     2.69673991f,    2.75600863f,   2.81104398f,   2.85761261f,   2.904181f,
    2.94228292f,    2.99731827f,    3.0819881f,     3.10738897f,   3.1666584f,    3.23862791f,   3.31059742f,
    3.35293198f,    3.4587698f,     3.51803875f,    3.61964273f,   3.67891169f,   3.75934839f,   3.83555126f,
    3.9540894f,     4.04299307f,    4.14883041f,    4.22926664f,   4.47480965f,   4.5044446f,    4.60604858f,
    4.74575424f,    4.89392662f,    5.05903244f,    5.2072053f,    5.36384487f,   5.60092115f,   5.75332689f,
    5.93960094f,    6.25711298f,    6.45608759f,    6.71009779f,   7.04877758f,   7.35782337f,   7.8362093f,
    8.06905079f,    8.5220356f,     8.91151905f,    9.49574184f,   10.0418625f,   10.7869596f,   11.7140951f,
    12.8190384f,    13.8393126f,    15.1601648f,    16.7942963f,   19.1269531f,   19.651907f,    20.1726303f,
    20.8965569f,    21.7474918f};

static int nearest_timing_index(const float (&table)[128], float seconds) {
    if (seconds <= table[0]) return 0;
    if (seconds >= table[127]) return 127;

    int lo = 0;
    int hi = 127;
    while (lo + 1 < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (table[mid] < seconds) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (seconds - table[lo] <= table[hi] - seconds) ? lo : hi;
}

static void configure_amp_envelope(kr106::ADSR& env, float sample_rate, float attack, float decay, float sustain,
                                   float release) {
    env.mModel = kr106::kJ106;
    env.SetSampleRate(sample_rate);
    env.Set106Attack((float)nearest_timing_index(kJ106AttackSeconds, attack) / 127.0f);
    env.Set106Decay(nearest_timing_index(kJ106DecRelSeconds, decay));
    env.SetSustain(std::clamp(sustain, 0.0f, 1.0f));
    env.Set106Release(nearest_timing_index(kJ106DecRelSeconds, release));
}

}  // namespace

void JunoVoice::init(float sample_rate) {
    sample_rate_  = sample_rate;
    gate_         = false;
    amp_rendered_ = false;
    vel_scale_    = 1.0f;

    oscillators_.mPulseInvert = true;
    oscillators_.mSawAmp      = kr106::kSawAmpJ106;
    oscillators_.mPulseAmp    = kr106::kPulseAmpJ106;
    oscillators_.mSubAmp      = kr106::kSubAmpJ106;
    oscillators_.mNoiseAmp    = kr106::kNoiseAmpJ106;
    oscillators_.Init(sample_rate);
    oscillators_.mSawGain   = p_osc_saw_on_ ? 1.0f : 0.0f;
    oscillators_.mPulseGain = p_osc_pulse_on_ ? 1.0f : 0.0f;
    oscillators_.mSubGain   = p_sub_level_ > 0.0f ? 1.0f : 0.0f;
    filter_.SetOversample(1);
    filter_.mJ106Res = true;
    filter_.SetSampleRate(sample_rate);
    filter_.Reset();
    configure_amp_envelope(amp_env_, sample_rate_, p_attack_, p_decay_, p_sustain_, p_release_);

    // Stage 3a: init second envelope.
    // ADR 0018: LFOs moved to engine (shared free-running); no per-voice init.
    env2_.init(sample_rate);

    filter_.UpdateCoeffs(p_cutoff_ / (sample_rate_ * 0.5f), p_res_);
    env2_.set_attack(p_env2_attack_);
    env2_.set_decay(p_env2_decay_);
    env2_.set_sustain(p_env2_sustain_);
    env2_.set_release(p_env2_release_);
}

void JunoVoice::note_on(uint8_t pitch, uint8_t velocity, NoteExpression expr) {
    (void)expr;  // MPE fields wired in Stage 5

    midi_note_    = pitch;
    vel_scale_    = (float)velocity / 127.0f;
    gate_         = true;
    amp_rendered_ = false;

    // Reset LFO delay fade-in counters on each new note.
    lfo1_delay_pos_ = 0.0f;
    lfo2_delay_pos_ = 0.0f;
    // Zero the injected raw values so the one-block-latency mod-matrix read
    // doesn't see a stale value from a previous note (ADR 0018).
    lfo1_raw_       = 0.0f;
    lfo2_raw_       = 0.0f;
    amp_env_.NoteOn();
}

void JunoVoice::note_off() {
    gate_ = false;
    if (amp_rendered_) {
        amp_env_.NoteOff();
    } else {
        // A note born and released entirely between audio blocks was never
        // audible, so it must not leave a synthetic release tail behind.
        amp_env_ = kr106::ADSR{};
        configure_amp_envelope(amp_env_, sample_rate_, p_attack_, p_decay_, p_sustain_, p_release_);
    }
    env2_.process(false);
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
    gate_         = false;
    amp_rendered_ = false;
    vel_scale_    = 1.0f;
    amp_env_      = kr106::ADSR{};
    configure_amp_envelope(amp_env_, sample_rate_, p_attack_, p_decay_, p_sustain_, p_release_);
    env2_.reset();
    oscillators_.Reset();
    filter_.Reset();
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
        // OSC_PWM: pulse-width base value; applied in render() via osc_pulse_.set_pw().
        case ParamId::OSC_PWM:
            p_osc_pwm_ = value;
            break;
        // WO-13c (ADR 0026): independent wave-enable switches, gate contribution only —
        // never resets phase (that only happens in note_on()/reset()).
        case ParamId::OSC_SAW_ON:
            p_osc_saw_on_ = (int)value;
            break;
        case ParamId::OSC_PULSE_ON:
            p_osc_pulse_on_ = (int)value;
            break;
        // OSC_RANGE: semitone offset applied to base freq in render().
        case ParamId::OSC_RANGE:
            p_osc_range_semi_ = value;
            break;
        // WO-13d: direct panel modulation — LFO1 -> DCO pitch depth, PWM mode.
        case ParamId::DCO_LFO_DEPTH:
            p_dco_lfo_depth_ = value;
            break;
        case ParamId::PWM_MODE:
            p_pwm_mode_ = (int)value;
            break;

        // --- FILTER ---
        case ParamId::FILTER_CUTOFF:
            p_cutoff_ = value;
            break;
        case ParamId::FILTER_RES:
            p_res_ = value;
            break;
        case ParamId::FILTER_MODE:
            // The Juno-106 VCF is low-pass only.
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
        // --- ENV ---
        case ParamId::ENV_ATTACK:
            p_attack_ = value;
            amp_env_.Set106Attack((float)nearest_timing_index(kJ106AttackSeconds, value) / 127.0f);
            break;
        case ParamId::ENV_DECAY:
            p_decay_ = value;
            amp_env_.Set106Decay(nearest_timing_index(kJ106DecRelSeconds, value));
            break;
        case ParamId::ENV_SUSTAIN:
            p_sustain_ = value;
            amp_env_.SetSustain(std::clamp(value, 0.0f, 1.0f));
            break;
        case ParamId::ENV_RELEASE:
            p_release_ = value;
            amp_env_.Set106Release(nearest_timing_index(kJ106DecRelSeconds, value));
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
    const float* noise_input = (noise_input_ != nullptr && noise_input_count_ >= n) ? noise_input_ : nullptr;
    noise_input_             = nullptr;
    noise_input_count_       = 0;

    // Early exit when both envelopes are idle (post-release or pre-first-note).
    // ADR 0018: LFO phase is now engine-owned (free-running); the engine advances
    // the shared LFO unconditionally every block and injects via set_lfo_inputs().
    if (!amp_env_.GetBusy() && env2_.is_idle()) {
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
    // --- Audio-rate mod: compute start/end values for per-sample interpolation.
    // Pitch (semitone offset → freq). OSC_RANGE adds a fixed offset in semitones.
    // p_pitch_offset_ is a portamento glide semitone offset set by VoiceAlloc each
    // block; it is already block-rate-smoothed by the allocator.
    // Stage 5c: direct pitch-bend path — always-on, ±kPitchBendRangeSemis.
    // Flows into both base_freq and mod_freq_end so bend is smooth across the block.
    // The pitch_bend mod-matrix SOURCE (msrc.pitch_bend) remains available for patches
    // but the direct path is primary. Both can coexist without double-counting because
    // the matrix source is only used when a patch route maps it — which is additive.
    // WO-13d: DCO_LFO_DEPTH is a direct panel path — LFO1 applied straight to
    // pitch, independent of the mod matrix. kDcoLfoRange bounds the max swing
    // to +-2 semitones at depth=1 with a fully-swung LFO (block-rate, matches
    // the VCF_LFO_DEPTH panel-mod style already used for cutoff above).
    static constexpr float kDcoLfoRange = 2.0f;
    float                  range_semi   = p_osc_range_semi_ + p_pitch_offset_ + p_pitch_bend_ * kPitchBendRangeSemis +
                       lfo1_value_ * p_dco_lfo_depth_ * kDcoLfoRange;
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

    // PWM: apply once per block (block-rate, ~750 Hz @ 64/48k — ample for a slow LFO sweep).
    // Clamp [0.05, 0.95] to avoid degenerate silent/full-duty pulse at the extremes.
    // Only affects the pulse output; saw and sub share the same DCO phase.
    // WO-13d: PWM_MODE selects the direct panel interpretation of OSC_PWM —
    // LFO mode reads it as a modulation amount swung around the hardware-neutral
    // 50% center by the shared LFO1; Manual mode reads it as the fixed width.
    // Mod-matrix pwm_mod (kModDestPwm) remains an optional additive extension on top.
    float pw;
    if (p_pwm_mode_ == 0) {
        // LFO mode: OSC_PWM in [0,1] is a depth around center 0.5. kPwmLfoRange
        // bounds full-amount/full-swing to +-0.45 (keeps pw within the sane
        // pulse range even before the final safety clamp below).
        static constexpr float kPwmLfoRange = 0.45f;
        pw                                  = 0.5f + lfo1_value_ * p_osc_pwm_ * kPwmLfoRange + mout.pwm_mod;
    } else {
        // Manual mode: OSC_PWM is the fixed pulse width directly.
        pw = p_osc_pwm_ + mout.pwm_mod;
    }
    if (pw < 0.05f) pw = 0.05f;
    if (pw > 0.95f) pw = 0.95f;
    // Sub / noise level mods (also once per block — fast enough):
    float eff_sub   = p_sub_level_ + mout.osc_sub;
    float eff_noise = p_noise_level_ + mout.osc_noise;
    if (eff_sub < 0.0f) eff_sub = 0.0f;
    if (eff_sub > 1.0f) eff_sub = 1.0f;
    if (eff_noise < 0.0f) eff_noise = 0.0f;
    if (eff_noise > 1.0f) eff_noise = 1.0f;
    float noise_gain = 0.0f;
    if (eff_noise > 0.0f) {
        static constexpr float kNoiseScale  = 1.0632f;
        static constexpr float kNoiseKnee   = 0.0146f;
        static constexpr float kNoiseTurnOn = 0.0594f;
        const float            d            = eff_noise - kNoiseTurnOn;
        noise_gain                          = kNoiseScale * (sqrtf(d * d + kNoiseKnee * kNoiseKnee) + d) * 0.5f;
    }
    static constexpr float kSilentLevel = 1e-10f;
    const bool             dco_enabled =
        (p_osc_saw_on_ != 0 || p_osc_pulse_on_ != 0) && (p_osc_level_ > kSilentLevel || amp_end > kSilentLevel);
    const bool source_enabled = dco_enabled || eff_sub > kSilentLevel || eff_noise > kSilentLevel;

    // KR-106 VCF control-rate update. Its frequency input is normalized to
    // Nyquist; coefficients are held for the whole block at 1x oversampling.
    filter_.UpdateCoeffs(cutoff_end / (sample_rate_ * 0.5f), eff_res);

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

        // WO-14a: saw, pulse, and sub are generated from one phase accumulator.
        // Keep the independent target level on the main waveforms; KR-106 applies
        // its calibrated J106 mix ratios and the tapered sub level internally.
        oscillators_.mSawAmp   = kr106::kSawAmpJ106 * cur_amp;
        oscillators_.mPulseAmp = kr106::kPulseAmpJ106 * cur_amp;
        bool  sync             = false;
        float osc = oscillators_.Process(cur_freq / sample_rate_, pw, p_osc_saw_on_ != 0, p_osc_pulse_on_ != 0,
                                         eff_sub > 0.0f, kr106::Oscillators::AudioTaper(eff_sub), 0.0f, sync);
        if (p_osc_saw_on_ == 0 && p_osc_pulse_on_ == 0 && eff_sub <= 0.0f) osc = 0.0f;
        float noise = noise_input != nullptr ? noise_input[i] * noise_gain * kr106::kNoiseAmpJ106 : 0.0f;
        float mixed = osc + noise;

        filter_.TrackInputEnv(mixed);
        float filtered = filter_.ProcessSample(mixed);
        if (!source_enabled) filtered = 0.0f;

        // The firmware envelope always advances at audio rate. Gate mode uses
        // its BA662-style edge smoothing; envelope mode passes through the
        // measured J106 VCA transfer curve.
        const float amp_env = std::clamp(amp_env_.Process(), 0.0f, 1.0f);
        amp_rendered_       = true;
        float env_val;
        if (p_vca_gate_mode_ != 0) {
            env_val = amp_env_.mGateEnv;
        } else {
            env_val = kr106::VCAGainJ106(amp_env);
        }
        buf[i] += filtered * env_val * vel_scale_ * p_vca_level_;

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
    return amp_env_.GetBusy();
}
