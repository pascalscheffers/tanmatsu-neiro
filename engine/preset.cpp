// engine/preset.cpp — preset serialisation + factory bank (Stage 2d, bumped 3b-ii).
#include "preset.h"
#include <math.h>
#include <string.h>
#include "mod_matrix.h"
#include "param_desc.h"
#include "param_id.h"

// Virtual PWM destination (parallel to kModDestPitch = 0xFFFE in mod_matrix.h).
// No ParamId exists yet — PWM hardware wiring is Stage 3c.  The sentinel is
// stored in preset routings so blobs written now round-trip correctly once 3c
// promotes it to a proper ParamId or kModDestPwm constant in mod_matrix.h.
static constexpr uint16_t kPresetDestPwm = 0xFFFDu;

// ---------------------------------------------------------------------------
// Curve helper (mirrors ParamStore::apply_curve; kept local, no coupling)
// ---------------------------------------------------------------------------
static float apply_curve_local(const ParamDesc& d, float norm) {
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    switch (d.curve) {
        case CURVE_LIN:
            return d.min + norm * (d.max - d.min);
        case CURVE_EXP:
            if (d.min > 0.0f && d.max > 0.0f) return d.min * powf(d.max / d.min, norm);
            return d.min + norm * (d.max - d.min);
        case CURVE_LOG: {
            const float t = log2f(1.0f + norm);
            return d.min + t * (d.max - d.min);
        }
        case CURVE_STEPPED: {
            const int steps = (int)(d.max - d.min);
            int       step  = (int)(norm * (float)(steps + 1));
            if (step > steps) step = steps;
            if (step < 0) step = 0;
            return d.min + (float)step;
        }
    }
    return d.min + norm * (d.max - d.min);
}

// ---------------------------------------------------------------------------
// Factory bank — hardcoded physical values, no storage required
// ---------------------------------------------------------------------------

// "Clean 106" default routings (ADR 0009 §Default-patch voicing, RATIFIED).
// Amp envelope is hardwired in JunoVoice — do NOT add ENV1→amp here.
static const Routing k_clean_106_routings[] = {
    // source             dest_param_id              depth   curve
    {(uint8_t)ModSource::ENV2, (uint16_t)ParamId::FILTER_CUTOFF, +0.35f, (uint8_t)ModCurve::LIN},
    {(uint8_t)ModSource::LFO1, kPresetDestPwm, +0.20f, (uint8_t)ModCurve::LIN},
};
static constexpr int k_clean_106_count = (int)(sizeof(k_clean_106_routings) / sizeof(k_clean_106_routings[0]));

struct FactoryPreset {
    const char*    name;
    uint16_t       ids[48];  // Stage 3c-i: widened from 32 → 48 for full Juno param set
    float          vals[48];
    int            count;
    const Routing* routings;
    int            routing_count;
};

// All values in physical units (the same space engine_set_param() expects).
// Param ordering follows JUNO_PARAM_TABLE order (cosmetic — loader goes by id).
// Stage 3a: added ENV2 (attack/decay/sustain/release) + LFO1/2 (rate/depth/shape).
// Stage 3b-ii: added routings fields; all presets carry "Clean 106" routings.
// Stage 3c-i: added OSC_PWM/WAVEFORM/RANGE, HPF_CUTOFF, VCF_ENV_DEPTH/POLARITY/
//             KEY_TRACK/LFO_DEPTH, LFO1/2_DELAY, CHORUS_MODE, VCA_GATE_MODE/LEVEL.
// Stage 3d-i: added PLAY_MODE and PORTAMENTO_TIME.
// Stage 3d-ii: added UNISON_COUNT and UNISON_DETUNE.
static const FactoryPreset
    k_factory[] =
        {
            // 0: INIT — all table defaults + "Clean 106" routings (ADR 0009 RATIFIED)
            {
                "INIT",
                {ParamId::OSC_LEVEL,     ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,     ParamId::OSC_PWM,
                 ParamId::OSC_WAVEFORM,  ParamId::OSC_RANGE,     ParamId::FILTER_CUTOFF,   ParamId::FILTER_RES,
                 ParamId::FILTER_MODE,   ParamId::HPF_CUTOFF,    ParamId::VCF_ENV_DEPTH,   ParamId::VCF_ENV_POLARITY,
                 ParamId::VCF_KEY_TRACK, ParamId::VCF_LFO_DEPTH, ParamId::ENV_ATTACK,      ParamId::ENV_DECAY,
                 ParamId::ENV_SUSTAIN,   ParamId::ENV_RELEASE,   ParamId::ENV2_ATTACK,     ParamId::ENV2_DECAY,
                 ParamId::ENV2_SUSTAIN,  ParamId::ENV2_RELEASE,  ParamId::LFO1_RATE,       ParamId::LFO1_DEPTH,
                 ParamId::LFO1_SHAPE,    ParamId::LFO1_DELAY,    ParamId::LFO2_RATE,       ParamId::LFO2_DEPTH,
                 ParamId::LFO2_SHAPE,    ParamId::LFO2_DELAY,    ParamId::CHORUS_RATE,     ParamId::CHORUS_DEPTH,
                 ParamId::CHORUS_DELAY,  ParamId::CHORUS_MODE,   ParamId::MASTER_GAIN,     ParamId::VCA_GATE_MODE,
                 ParamId::VCA_LEVEL,     ParamId::PLAY_MODE,     ParamId::PORTAMENTO_TIME, ParamId::UNISON_COUNT,
                 ParamId::UNISON_DETUNE},
                {0.70f,  0.30f,  0.05f,  0.50f,  0.0f,   0.0f,   2000.0f, 0.30f,  0.0f, 20.0f, 0.35f, 0.0f, 0.50f, 0.0f,
                 0.010f, 0.100f, 0.700f, 0.300f, 0.005f, 0.200f, 0.000f,  0.200f, 1.0f, 0.5f,  0.0f,  0.0f, 0.5f,  0.5f,
                 0.0f,   0.0f,   0.500f, 0.700f, 0.400f, 1.0f,   0.500f,  0.0f,   1.0f, 0.0f,  0.0f, /* poly, no glide
                                                                                                      */
                 1.0f,   7.0f}, /* U=1 (no unison), 7 cents ready (table default) */
                41,
                k_clean_106_routings,
                k_clean_106_count,
            },
            // 1: Bass — thick sub, tight attack, dark filter + mono retrigger + subtle glide
            {
                "Bass",
                {ParamId::OSC_LEVEL,     ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,     ParamId::OSC_PWM,
                 ParamId::OSC_WAVEFORM,  ParamId::OSC_RANGE,     ParamId::FILTER_CUTOFF,   ParamId::FILTER_RES,
                 ParamId::FILTER_MODE,   ParamId::HPF_CUTOFF,    ParamId::VCF_ENV_DEPTH,   ParamId::VCF_ENV_POLARITY,
                 ParamId::VCF_KEY_TRACK, ParamId::VCF_LFO_DEPTH, ParamId::ENV_ATTACK,      ParamId::ENV_DECAY,
                 ParamId::ENV_SUSTAIN,   ParamId::ENV_RELEASE,   ParamId::ENV2_ATTACK,     ParamId::ENV2_DECAY,
                 ParamId::ENV2_SUSTAIN,  ParamId::ENV2_RELEASE,  ParamId::LFO1_RATE,       ParamId::LFO1_DEPTH,
                 ParamId::LFO1_SHAPE,    ParamId::LFO1_DELAY,    ParamId::LFO2_RATE,       ParamId::LFO2_DEPTH,
                 ParamId::LFO2_SHAPE,    ParamId::LFO2_DELAY,    ParamId::CHORUS_RATE,     ParamId::CHORUS_DEPTH,
                 ParamId::CHORUS_DELAY,  ParamId::CHORUS_MODE,   ParamId::MASTER_GAIN,     ParamId::VCA_GATE_MODE,
                 ParamId::VCA_LEVEL,     ParamId::PLAY_MODE,     ParamId::PORTAMENTO_TIME, ParamId::UNISON_COUNT,
                 ParamId::UNISON_DETUNE},
                {0.85f,  0.60f, 0.00f, 0.50f, 0.0f,   -12.0f,              /* bass: 1 oct down */
                 800.0f, 0.50f, 0.0f,  20.0f, 0.50f,  0.0f,   0.30f, 0.0f, /* stronger env mod for bass filter sweep */
                 0.002f, 0.15f, 0.50f, 0.08f, 0.002f, 0.10f,  0.00f, 0.08f, 0.5f, 0.3f, 0.0f, 0.0f,  0.5f,
                 0.3f,   0.0f,  0.0f,  0.30f, 0.40f,  0.30f,  1.0f,  0.60f, 0.0f, 1.0f, 1.0f, 0.06f, /* mono+retrigger,
                                                                                                        60 ms glide */
                 1.0f,   0.0f}, /* U=1 (no unison — bass lines stay tight) */
                41,
                k_clean_106_routings,
                k_clean_106_count,
            },
            // 2: Pad — slow attack, lush chorus, long release, poly + unison 2 + 7 cents
            {
                "Pad",
                {ParamId::OSC_LEVEL,     ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,     ParamId::OSC_PWM,
                 ParamId::OSC_WAVEFORM,  ParamId::OSC_RANGE,     ParamId::FILTER_CUTOFF,   ParamId::FILTER_RES,
                 ParamId::FILTER_MODE,   ParamId::HPF_CUTOFF,    ParamId::VCF_ENV_DEPTH,   ParamId::VCF_ENV_POLARITY,
                 ParamId::VCF_KEY_TRACK, ParamId::VCF_LFO_DEPTH, ParamId::ENV_ATTACK,      ParamId::ENV_DECAY,
                 ParamId::ENV_SUSTAIN,   ParamId::ENV_RELEASE,   ParamId::ENV2_ATTACK,     ParamId::ENV2_DECAY,
                 ParamId::ENV2_SUSTAIN,  ParamId::ENV2_RELEASE,  ParamId::LFO1_RATE,       ParamId::LFO1_DEPTH,
                 ParamId::LFO1_SHAPE,    ParamId::LFO1_DELAY,    ParamId::LFO2_RATE,       ParamId::LFO2_DEPTH,
                 ParamId::LFO2_SHAPE,    ParamId::LFO2_DELAY,    ParamId::CHORUS_RATE,     ParamId::CHORUS_DEPTH,
                 ParamId::CHORUS_DELAY,  ParamId::CHORUS_MODE,   ParamId::MASTER_GAIN,     ParamId::VCA_GATE_MODE,
                 ParamId::VCA_LEVEL,     ParamId::PLAY_MODE,     ParamId::PORTAMENTO_TIME, ParamId::UNISON_COUNT,
                 ParamId::UNISON_DETUNE},
                {0.75f,   0.20f, 0.08f, 0.60f, 0.0f,  0.0f, /* wider PWM for pad shimmer */
                 3000.0f, 0.15f, 0.0f,  20.0f, 0.25f, 0.0f,  0.60f, 0.0f, 0.80f, 0.50f, 0.80f,
                 1.50f,   0.50f, 0.80f, 0.00f, 0.80f, 0.3f,  0.6f,  0.0f, 0.3f, /* LFO1 delay 0.3s for gentle vibrato
                                                                                   fade-in */
                 0.2f,    0.4f,  1.0f,  0.0f,  0.40f, 0.90f, 0.55f, 2.0f,       /* Chorus II for wider stereo spread */
                 0.50f,   0.0f,  1.0f,  0.0f,  0.0f,                            /* poly, no glide */
                 2.0f,    7.0f}, /* U=2, 7 cents spread — fat pad shimmer */
                41,
                k_clean_106_routings,
                k_clean_106_count,
            },
            // 3: Lead — bright, cutting, legato mono + short glide + unison 2 + 10 cents
            {
                "Lead",
                {ParamId::OSC_LEVEL,     ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,     ParamId::OSC_PWM,
                 ParamId::OSC_WAVEFORM,  ParamId::OSC_RANGE,     ParamId::FILTER_CUTOFF,   ParamId::FILTER_RES,
                 ParamId::FILTER_MODE,   ParamId::HPF_CUTOFF,    ParamId::VCF_ENV_DEPTH,   ParamId::VCF_ENV_POLARITY,
                 ParamId::VCF_KEY_TRACK, ParamId::VCF_LFO_DEPTH, ParamId::ENV_ATTACK,      ParamId::ENV_DECAY,
                 ParamId::ENV_SUSTAIN,   ParamId::ENV_RELEASE,   ParamId::ENV2_ATTACK,     ParamId::ENV2_DECAY,
                 ParamId::ENV2_SUSTAIN,  ParamId::ENV2_RELEASE,  ParamId::LFO1_RATE,       ParamId::LFO1_DEPTH,
                 ParamId::LFO1_SHAPE,    ParamId::LFO1_DELAY,    ParamId::LFO2_RATE,       ParamId::LFO2_DEPTH,
                 ParamId::LFO2_SHAPE,    ParamId::LFO2_DELAY,    ParamId::CHORUS_RATE,     ParamId::CHORUS_DEPTH,
                 ParamId::CHORUS_DELAY,  ParamId::CHORUS_MODE,   ParamId::MASTER_GAIN,     ParamId::VCA_GATE_MODE,
                 ParamId::VCA_LEVEL,     ParamId::PLAY_MODE,     ParamId::PORTAMENTO_TIME, ParamId::UNISON_COUNT,
                 ParamId::UNISON_DETUNE},
                {0.90f, 0.10f, 0.00f, 0.50f, 0.0f,   0.0f,  6000.0f, 0.60f, 0.0f,   80.0f, /* Lead: slight HPF to thin
                                                                                              low end */
                 0.30f, 0.0f,  0.70f, 0.0f,  0.005f, 0.20f, 0.65f,   0.12f, 0.003f, 0.15f, 0.00f,
                 0.10f, 5.0f,  0.4f,  0.0f,  0.0f,   3.0f,  0.2f,    0.0f,  0.0f,   1.00f, 0.50f,
                 0.30f, 1.0f,  0.50f, 0.0f,  1.0f,   2.0f,  0.08f, /* mono+legato, 80 ms glide for expressive phrasing
                                                                    */
                 2.0f,  10.0f},                                    /* U=2, 10 cents — thicker lead without muddiness */
                41,
                k_clean_106_routings,
                k_clean_106_count,
            },
};

static constexpr int k_factory_count = (int)(sizeof(k_factory) / sizeof(k_factory[0]));

int preset_factory_count(void) {
    return k_factory_count;
}

const char* preset_factory_name(int idx) {
    if (idx < 0 || idx >= k_factory_count) return "";
    return k_factory[idx].name;
}

int preset_factory_params(int idx, uint16_t* ids_out, float* vals_out, int max_count) {
    if (idx < 0 || idx >= k_factory_count) return -1;
    const FactoryPreset& fp = k_factory[idx];
    int                  n  = (fp.count < max_count) ? fp.count : max_count;
    for (int i = 0; i < n; i++) {
        ids_out[i]  = fp.ids[i];
        vals_out[i] = fp.vals[i];
    }
    return n;
}

int preset_factory_routings(int idx, Routing* routings_out, int max_count) {
    if (idx < 0 || idx >= k_factory_count) return -1;
    const FactoryPreset& fp = k_factory[idx];
    if (!fp.routings || fp.routing_count == 0) return 0;
    int n = (fp.routing_count < max_count) ? fp.routing_count : max_count;
    for (int i = 0; i < n; i++) {
        routings_out[i] = fp.routings[i];
    }
    return n;
}

// ---------------------------------------------------------------------------
// Serialisation helpers (explicit byte-level I/O avoids alignment/ABI issues)
// ---------------------------------------------------------------------------
static void wr_u8(uint8_t** p, uint8_t v) {
    **p = v;
    (*p)++;
}
static void wr_u16(uint8_t** p, uint16_t v) {
    memcpy(*p, &v, 2);
    (*p) += 2;
}
static void wr_f32(uint8_t** p, float v) {
    memcpy(*p, &v, 4);
    (*p) += 4;
}

static uint8_t rd_u8(const uint8_t** p) {
    uint8_t v = **p;
    (*p)++;
    return v;
}
static uint16_t rd_u16(const uint8_t** p) {
    uint16_t v;
    memcpy(&v, *p, 2);
    (*p) += 2;
    return v;
}
static float rd_f32(const uint8_t** p) {
    float v;
    memcpy(&v, *p, 4);
    (*p) += 4;
    return v;
}

// ---------------------------------------------------------------------------
// Public serialisation API
// ---------------------------------------------------------------------------
int preset_serialize(void* buf, size_t buf_max, const char* name, const float* norms, int norms_len,
                     const Routing* routings, int routings_len) {
    const size_t param_count = (size_t)kJunoParamCount;
    const int    r_count     = (routings && routings_len > 0) ? routings_len : 0;
    // header(42) + params(6 each) + routing_count(2) + routings(8 each)
    const size_t need        = 42u + param_count * 6u + 2u + (size_t)r_count * 8u;
    if (buf_max < need) return -1;

    uint8_t* p = (uint8_t*)buf;

    // Header
    memcpy(p, "TNMT", 4);
    p += 4;
    wr_u8(&p, PRESET_FORMAT_VERSION);  // v2
    wr_u8(&p, PRESET_MODEL_JUNO);
    wr_u16(&p, 0u);  // flags, reserved

    // Name (32 bytes, null-padded)
    char name_buf[PRESET_NAME_LEN] = {};
    if (name) strncpy(name_buf, name, PRESET_NAME_LEN - 1);
    memcpy(p, name_buf, PRESET_NAME_LEN);
    p += PRESET_NAME_LEN;

    // Count
    wr_u16(&p, (uint16_t)param_count);

    // Param entries: norm → physical via the table's curve
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d    = JUNO_PARAM_TABLE[i];
        float            norm = (d.id < (uint16_t)norms_len) ? norms[d.id] : 0.0f;
        float            phys = apply_curve_local(d, norm);
        wr_u16(&p, d.id);
        wr_f32(&p, phys);
    }

    // Routings block (v2): count + records, field-by-field (1+2+4+1 = 8 bytes each)
    wr_u16(&p, (uint16_t)r_count);
    for (int i = 0; i < r_count; i++) {
        wr_u8(&p, routings[i].source);
        wr_u16(&p, routings[i].dest_param_id);
        wr_f32(&p, routings[i].depth);
        wr_u8(&p, routings[i].curve);
    }

    return (int)(p - (uint8_t*)buf);
}

int preset_parse(const void* buf, size_t len, char* name_out, int name_max, uint16_t* ids_out, float* vals_out,
                 int max_count, Routing* routings_out, int max_routings, int* routings_count_out) {
    static constexpr size_t kHeaderSize = 42u;
    if (len < kHeaderSize) return -1;

    const uint8_t* p   = (const uint8_t*)buf;
    const uint8_t* end = p + len;

    // Magic
    if (memcmp(p, "TNMT", 4) != 0) return -1;
    p += 4;

    uint8_t version  = rd_u8(&p);
    uint8_t model_id = rd_u8(&p);
    rd_u16(&p);  // flags, ignored

    // Accept v1 (no routings block) and v2 (with routings block).
    if ((version != 1 && version != PRESET_FORMAT_VERSION) || model_id != PRESET_MODEL_JUNO) return -1;

    // Name
    if (name_out && name_max > 0) {
        int copy = (name_max - 1 < PRESET_NAME_LEN) ? name_max - 1 : PRESET_NAME_LEN;
        memcpy(name_out, p, copy);
        name_out[copy]         = '\0';
        // Ensure null-termination at the actual string end.
        name_out[name_max - 1] = '\0';
    }
    p += PRESET_NAME_LEN;

    uint16_t count = rd_u16(&p);

    // Validate enough bytes remain for the declared param count.
    if (len < kHeaderSize + (size_t)count * 6u) return -1;

    int n = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t id  = rd_u16(&p);
        float    val = rd_f32(&p);
        if (n < max_count) {
            ids_out[n]  = id;
            vals_out[n] = val;
            n++;
        }
    }

    // Routings block (v2 only — v1 blobs end here).
    int r_out = 0;
    if (version == 2 && p < end) {
        // Need at least 2 bytes for the routing count.
        if ((size_t)(end - p) >= 2u) {
            uint16_t r_count = rd_u16(&p);
            // Each routing = 1+2+4+1 = 8 bytes.
            for (uint16_t i = 0; i < r_count; i++) {
                if ((size_t)(end - p) < 8u) break;  // truncated — stop safely
                uint8_t  src   = rd_u8(&p);
                uint16_t dest  = rd_u16(&p);
                float    depth = rd_f32(&p);
                uint8_t  curve = rd_u8(&p);
                // Skip unknown source ids (forward-compat).
                if (src >= (uint8_t)ModSource::_COUNT && src != 0) {
                    // unknown source: skip (dest/depth/curve already consumed above)
                    continue;
                }
                if (routings_out && r_out < max_routings) {
                    routings_out[r_out].source        = src;
                    routings_out[r_out].dest_param_id = dest;
                    routings_out[r_out].depth         = depth;
                    routings_out[r_out].curve         = curve;
                    r_out++;
                }
            }
        }
    }

    if (routings_count_out) *routings_count_out = r_out;
    return n;
}
