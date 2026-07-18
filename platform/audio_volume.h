// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Square-law listening taper for a logical 0..100 volume control.
float platform_volume_gain(uint32_t pct);

// Map the same taper onto badge-bsp's ES8156 0..100 scale, capped at the
// measured-safe 90% landing. Zero retains the BSP's maximum attenuation.
float platform_volume_codec_pct(uint32_t pct);

#ifdef __cplusplus
}
#endif
