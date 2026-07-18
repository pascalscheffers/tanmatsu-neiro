// SPDX-License-Identifier: MIT
#include "audio_volume.h"
#include <math.h>

// badge-bsp maps one percentage point to 1.8 ES8156 register steps. The
// register rises by approximately 0.5 dB per step, so one BSP point is 0.9 dB.
static const float kCodecCeilingPct = 90.0f;
static const float kCodecDbPerPct   = 0.9f;

float platform_volume_gain(uint32_t pct) {
    if (pct > 100u) pct = 100u;
    float norm = (float)pct / 100.0f;
    return norm * norm;
}

float platform_volume_codec_pct(uint32_t pct) {
    float gain = platform_volume_gain(pct);
    if (gain <= 0.0f) return 0.0f;

    float codec_pct = kCodecCeilingPct + 20.0f * log10f(gain) / kCodecDbPerPct;
    if (codec_pct < 0.0f) return 0.0f;
    if (codec_pct > kCodecCeilingPct) return kCodecCeilingPct;
    return codec_pct;
}
