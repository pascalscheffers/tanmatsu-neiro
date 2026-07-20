#pragma once

// Fixed-storage embedded adaptation of Ultramaster KR-106 KR106Chorus.h,
// v2.5.13 / bc15caee5843ab238a25d0969e68d57db2b1615f. GPL-3.0-only;
// authors Karl LaRocca and David Mansfield. See vendor/kr106/README.md.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "vendor/kr106/BBDFilter.h"
#include "vendor/kr106/KR106AnalogNoise.h"

namespace kr106 {

inline float chorus_snap(float x) {
    return (!std::isfinite(x) || std::fabs(x) < 1.0e-30f) ? 0.0f : x;
}

struct ChorusLFO {
    float phase = 0.0f;
    float inc   = 0.0f;
    void  set_rate(float hz) { inc = hz / 48000.0f; }
    void  reset() { phase = 0.0f; }
    float triangle() {
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    }
};

struct ClickRing {
    float coeff = 0.0f;
    float damp  = 0.0f;
    float low   = 0.0f;
    float band  = 0.0f;
    void  init() {
        coeff = 2.0f * std::sin(static_cast<float>(M_PI) * 30.0f / 48000.0f);
        damp  = 1.0f / 18.0f;
        reset();
    }
    void  reset() { low = band = 0.0f; }
    float process(float input) {
        low  += coeff * band;
        band += coeff * (input - low - damp * band);
        low   = chorus_snap(low);
        band  = chorus_snap(band);
        return low;
    }
};

struct LeakNoise {
    uint32_t seed     = 0xDEADBEEFu;
    float    hp_state = 0.0f;
    float    hp_coeff = 0.0f;
    float    lp_state = 0.0f;
    float    lp_coeff = 0.0f;
    void     init() {
        hp_coeff = 1.0f - expf(-2.0f * static_cast<float>(M_PI) * 800.0f / 48000.0f);
        lp_coeff = 1.0f - expf(-2.0f * static_cast<float>(M_PI) * 4000.0f / 48000.0f);
    }
    float process() {
        seed        = seed * 196314165u + 907633515u;
        float white = 2.0f * static_cast<float>(seed) / static_cast<float>(UINT32_MAX) - 1.0f;
        hp_state   += hp_coeff * (white - hp_state);
        lp_state   += lp_coeff * (white - hp_state - lp_state);
        hp_state    = chorus_snap(hp_state);
        lp_state    = chorus_snap(lp_state);
        return lp_state;
    }
};

struct BBDClick {
    int                  counter          = -1;
    bool                 was_in_zone      = false;
    static constexpr int kDurationSamples = 8640;  // upstream 180 ms at 48 kHz
    void                 reset() {
        counter     = -1;
        was_in_zone = false;
    }
    void suppress() {
        counter     = -1;
        was_in_zone = true;
    }
    float process(float lfo) {
        bool in_zone = lfo > 0.95f;
        if (in_zone && !was_in_zone) counter = 0;
        was_in_zone = in_zone;
        if (counter < 0 || counter >= kDurationSamples) {
            counter = -1;
            return 0.0f;
        }
        float t = static_cast<float>(counter++) / static_cast<float>(kDurationSamples);
        if (t < 0.20f) {
            float u = t / 0.20f;
            return -expf(-4.0f * u) * sinf(static_cast<float>(M_PI) * u);
        }
        float u = (t - 0.20f) / 0.80f;
        return 0.85f * expf(-8.0f * u) * sinf(static_cast<float>(M_PI) * u);
    }
};

struct BBDLine {
    static constexpr std::size_t      kBufferSamples = 1024;
    static constexpr std::size_t      kBufferBytes   = kBufferSamples * sizeof(float);
    std::array<float, kBufferSamples> buffer{};
    std::size_t                       write_pos = 0;
    BBDFilter                         pre_filter;
    BBDFilter                         post_filter;
    float                             sat_drive        = 0.12f;
    float                             sat_drive_smooth = 0.12f;

    void init() {
        buffer.fill(0.0f);
        write_pos = 0;
        pre_filter.Init(48000.0f);
        post_filter.Init(48000.0f);
    }
    static float hermite(float f, float y0, float y1, float y2, float y3) {
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    float read_hermite(float delay) const {
        float pos = static_cast<float>(write_pos) - delay;
        while (pos < 0.0f) pos += static_cast<float>(kBufferSamples);
        int   i = static_cast<int>(pos);
        float f = pos - static_cast<float>(i);
        return hermite(f, buffer[(i - 1) & 1023], buffer[i & 1023], buffer[(i + 1) & 1023], buffer[(i + 2) & 1023]);
    }
    void sanitize_filter(BBDFilter& filter) {
        filter.mBiquad.mIC1eq = chorus_snap(filter.mBiquad.mIC1eq);
        filter.mBiquad.mIC2eq = chorus_snap(filter.mBiquad.mIC2eq);
        filter.mPole.mS       = chorus_snap(filter.mPole.mS);
    }
    float process(float input, float delay, float noise) {
        float filtered = pre_filter.Process(input + noise + 1.0e-20f);
        sanitize_filter(pre_filter);
        sat_drive_smooth += (sat_drive - sat_drive_smooth) * 0.001f;
        sat_drive_smooth  = chorus_snap(sat_drive_smooth);
        float sat         = sat_drive_smooth > 0.01f ? tanhf(filtered * sat_drive_smooth) / sat_drive_smooth : filtered;
        buffer[write_pos] = chorus_snap(sat);
        float wet         = read_hermite(delay);
        write_pos         = (write_pos + 1) & 1023;
        float out         = post_filter.Process(wet + 1.0e-20f);
        sanitize_filter(post_filter);
        return chorus_snap(out);
    }
};

static_assert(BBDLine::kBufferSamples == 1024);
static_assert(BBDLine::kBufferBytes == 4096);

struct Chorus {
    static constexpr float kCenterDelayMs = 3.30f;
    static constexpr float kDryGain       = 0.863f;
    static constexpr float kWetGain       = 1.257f;
    static constexpr float kFadeInc       = 1.0f / (0.005f * 48000.0f);

    BBDLine          line0;
    BBDLine          line1;
    ChorusLFO        lfo;
    BBDClick         click0, click1, slow_click0, slow_click1;
    LeakNoise        leak0, leak1;
    ClickRing        ring0, ring1;
    AnalogFloorNoise pink0, pink1;
    RailRipple       ripple;
    int              mode           = 0;
    int              pending_mode   = 0;
    float            fade           = 0.0f;
    float            fade_target    = 0.0f;
    float            delay_depth    = 0.0f;
    float            target_depth   = 0.0f;
    float            gain_mod_scale = 1.0f;

    void Init(float sample_rate = 48000.0f) {
        (void)sample_rate;  // product-fixed rate; retained for the engine seam
        line0.init();
        line1.init();
        leak0.init();
        leak1.init();
        ring0.init();
        ring1.init();
        pink0.Init(48000.0f);
        pink1.Init(48000.0f);
        pink0.SetHighShelf(analog_noise::kWetShelfCornerHz, analog_noise::kWetShelfBoostDb, 48000.0f);
        pink1.SetHighShelf(analog_noise::kWetShelfCornerHz, analog_noise::kWetShelfBoostDb, 48000.0f);
        pink1.mSeed = 0x87654321u;
        leak1.seed  = 0xBADC0FFEu;
        ripple.SetMainsHz(60.0f, 48000.0f);
        ripple.SetAmplitudes(analog_noise::kWetRipple120, analog_noise::kWetRipple240, analog_noise::kWetRipple360);
        lfo.reset();
        click0.reset();
        click1.reset();
        slow_click0.reset();
        slow_click1.reset();
    }

    void SetMode(int new_mode) {
        new_mode = std::max(0, std::min(new_mode, 2));
        if (new_mode == mode && new_mode == pending_mode) return;
        if (mode > 0 && new_mode > 0) {
            pending_mode = new_mode;
            fade_target  = 0.0f;
            return;
        }
        pending_mode = mode = new_mode;
        if (mode == 0) {
            // The engine does not call Process while bypassed, so finish the
            // fade-out here and let the next engagement fill/fade naturally.
            fade        = 0.0f;
            fade_target = 0.0f;
            return;
        }
        configure_mode();
        fade_target = 1.0f;
        if (fade <= 0.0f) fade = kFadeInc;
        suppress_clicks();
    }

    void Process(float input, float& out_l, float& out_r) {
        fade += fade < fade_target ? std::min(kFadeInc, fade_target - fade) : -std::min(kFadeInc, fade - fade_target);
        if (fade <= 0.0f && pending_mode != mode) {
            mode = pending_mode;
            configure_mode();
            delay_depth = target_depth;
            fade_target = 1.0f;
            suppress_clicks();
        }
        delay_depth       += (target_depth - delay_depth) * kFadeInc;
        float lv           = lfo.triangle();
        float d0           = std::max((kCenterDelayMs + delay_depth * lv) * 0.985f, 0.1f);
        float d1           = std::max((kCenterDelayMs - delay_depth * lv) * 1.015f, 0.1f);
        float c0           = std::max(256.0f / (2.0f * d0 * 0.001f), 5000.0f);
        float c1           = std::max(256.0f / (2.0f * d1 * 0.001f), 5000.0f);
        float inv_depth    = delay_depth > 1.0e-9f ? 1.0f / delay_depth : 0.0f;
        float n0           = 0.5f * ((d0 - kCenterDelayMs) * inv_depth + 1.0f);
        float n1           = 0.5f * ((d1 - kCenterDelayMs) * inv_depth + 1.0f);
        float leak_amount0 = delay_depth * (0.0126f + 0.9874f * n0);
        float leak_amount1 = delay_depth * (0.0126f + 0.9874f * n1);
        float p0           = pink0.Process() * analog_noise::kWetBroadbandGain;
        float p1           = pink1.Process() * analog_noise::kWetBroadbandGain;
        float cp0          = click0.process(-lv) * 0.11f * gain_mod_scale;
        float cp1          = click1.process(lv) * 0.11f * gain_mod_scale;
        float cs0          = slow_click0.process(lv) * 0.022f * gain_mod_scale;
        float cs1          = slow_click1.process(-lv) * 0.022f * gain_mod_scale;
        float wet0    = line0.process(input, d0 * 48.0f, p0 + leak0.process() * 8.8e-3f * leak_amount0 + cp0 - cs0);
        float wet1    = line1.process(input, d1 * 48.0f, p1 + leak1.process() * 8.8e-3f * leak_amount1 + cp1 - cs1);
        wet0         *= 1.04f * (1.0f - 4468.0f * (1.0f / c0 - 1.0f / 40000.0f));
        wet1         *= 0.96f * (1.0f - 4468.0f * (1.0f / c1 - 1.0f / 40000.0f));
        wet0         += ring0.process(cp0) * 0.06f;
        wet1         += ring1.process(cp1) * 0.06f;
        float rail    = ripple.Process();
        wet0         += rail;
        wet1         += rail;
        float dry_mix = 1.0f - fade * (1.0f - kDryGain);
        float wet_mix = fade * kWetGain;
        out_l         = chorus_snap(dry_mix * input + wet_mix * wet0);
        out_r         = chorus_snap(dry_mix * input + wet_mix * wet1);
    }

private:
    void suppress_clicks() {
        click0.suppress();
        click1.suppress();
        slow_click0.suppress();
        slow_click1.suppress();
        ring0.reset();
        ring1.reset();
    }
    void configure_mode() {
        if (mode == 1) {
            lfo.set_rate(0.514f);
            target_depth   = 2.13f;
            gain_mod_scale = 1.0f;
        } else {
            lfo.set_rate(0.842f);
            target_depth   = 1.71f;
            gain_mod_scale = 1.71f / 2.13f;
        }
    }
};

static_assert(std::is_trivially_destructible<Chorus>::value, "chorus must own no dynamic storage");
static_assert(sizeof(Chorus::line0.buffer) == 4096);
static_assert(sizeof(Chorus::line1.buffer) == 4096);
static_assert(sizeof(Chorus) <= 10 * 1024, "fixed chorus exceeded the 10 KiB budget");

}  // namespace kr106
