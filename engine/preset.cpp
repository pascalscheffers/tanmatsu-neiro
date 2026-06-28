// engine/preset.cpp — preset serialisation + factory bank (Stage 2d).
#include "preset.h"
#include "param_desc.h"
#include "param_id.h"
#include <math.h>
#include <string.h>

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
            if (d.min > 0.0f && d.max > 0.0f)
                return d.min * powf(d.max / d.min, norm);
            return d.min + norm * (d.max - d.min);
        case CURVE_LOG: {
            const float t = log2f(1.0f + norm);
            return d.min + t * (d.max - d.min);
        }
        case CURVE_STEPPED: {
            const int steps = (int)(d.max - d.min);
            int step = (int)(norm * (float)(steps + 1));
            if (step > steps) step = steps;
            if (step < 0)     step = 0;
            return d.min + (float)step;
        }
    }
    return d.min + norm * (d.max - d.min);
}

// ---------------------------------------------------------------------------
// Factory bank — hardcoded physical values, no storage required
// ---------------------------------------------------------------------------
struct FactoryPreset {
    const char* name;
    uint16_t    ids[16];
    float       vals[16];
    int         count;
};

// All values in physical units (the same space engine_set_param() expects).
static const FactoryPreset k_factory[] = {
    // 0: INIT — all table defaults
    {
        "INIT",
        {ParamId::OSC_LEVEL, ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,
         ParamId::FILTER_CUTOFF, ParamId::FILTER_RES, ParamId::FILTER_MODE,
         ParamId::ENV_ATTACK,  ParamId::ENV_DECAY,   ParamId::ENV_SUSTAIN,  ParamId::ENV_RELEASE,
         ParamId::CHORUS_RATE, ParamId::CHORUS_DEPTH, ParamId::CHORUS_DELAY,
         ParamId::MASTER_GAIN},
        {0.70f, 0.30f, 0.05f,
         2000.0f, 0.30f, 0.0f,
         0.010f, 0.100f, 0.700f, 0.300f,
         0.500f, 0.700f, 0.400f,
         0.500f},
        14,
    },
    // 1: Bass — thick sub, tight attack, dark filter
    {
        "Bass",
        {ParamId::OSC_LEVEL, ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,
         ParamId::FILTER_CUTOFF, ParamId::FILTER_RES, ParamId::FILTER_MODE,
         ParamId::ENV_ATTACK,  ParamId::ENV_DECAY,   ParamId::ENV_SUSTAIN,  ParamId::ENV_RELEASE,
         ParamId::CHORUS_RATE, ParamId::CHORUS_DEPTH, ParamId::CHORUS_DELAY,
         ParamId::MASTER_GAIN},
        {0.85f, 0.60f, 0.00f,
         800.0f, 0.50f, 0.0f,
         0.002f, 0.15f, 0.50f, 0.08f,
         0.30f, 0.40f, 0.30f,
         0.60f},
        14,
    },
    // 2: Pad — slow attack, lush chorus, long release
    {
        "Pad",
        {ParamId::OSC_LEVEL, ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,
         ParamId::FILTER_CUTOFF, ParamId::FILTER_RES, ParamId::FILTER_MODE,
         ParamId::ENV_ATTACK,  ParamId::ENV_DECAY,   ParamId::ENV_SUSTAIN,  ParamId::ENV_RELEASE,
         ParamId::CHORUS_RATE, ParamId::CHORUS_DEPTH, ParamId::CHORUS_DELAY,
         ParamId::MASTER_GAIN},
        {0.75f, 0.20f, 0.08f,
         3000.0f, 0.15f, 0.0f,
         0.80f, 0.50f, 0.80f, 1.50f,
         0.40f, 0.90f, 0.55f,
         0.50f},
        14,
    },
    // 3: Lead — bright, cutting, light chorus
    {
        "Lead",
        {ParamId::OSC_LEVEL, ParamId::SUB_LEVEL,     ParamId::NOISE_LEVEL,
         ParamId::FILTER_CUTOFF, ParamId::FILTER_RES, ParamId::FILTER_MODE,
         ParamId::ENV_ATTACK,  ParamId::ENV_DECAY,   ParamId::ENV_SUSTAIN,  ParamId::ENV_RELEASE,
         ParamId::CHORUS_RATE, ParamId::CHORUS_DEPTH, ParamId::CHORUS_DELAY,
         ParamId::MASTER_GAIN},
        {0.90f, 0.10f, 0.00f,
         6000.0f, 0.60f, 0.0f,
         0.005f, 0.20f, 0.65f, 0.12f,
         1.00f, 0.50f, 0.30f,
         0.50f},
        14,
    },
};

static constexpr int k_factory_count = (int)(sizeof(k_factory) / sizeof(k_factory[0]));

int preset_factory_count(void) { return k_factory_count; }

const char* preset_factory_name(int idx) {
    if (idx < 0 || idx >= k_factory_count) return "";
    return k_factory[idx].name;
}

int preset_factory_params(int idx,
                          uint16_t* ids_out, float* vals_out, int max_count) {
    if (idx < 0 || idx >= k_factory_count) return -1;
    const FactoryPreset& fp = k_factory[idx];
    int n = (fp.count < max_count) ? fp.count : max_count;
    for (int i = 0; i < n; i++) {
        ids_out[i]  = fp.ids[i];
        vals_out[i] = fp.vals[i];
    }
    return n;
}

// ---------------------------------------------------------------------------
// Serialisation helpers (explicit byte-level I/O avoids alignment/ABI issues)
// ---------------------------------------------------------------------------
static void wr_u8(uint8_t** p, uint8_t v)   { **p = v; (*p)++; }
static void wr_u16(uint8_t** p, uint16_t v) { memcpy(*p, &v, 2); (*p) += 2; }
static void wr_f32(uint8_t** p, float v)    { memcpy(*p, &v, 4); (*p) += 4; }

static uint8_t  rd_u8(const uint8_t** p)  { uint8_t  v = **p; (*p)++;       return v; }
static uint16_t rd_u16(const uint8_t** p) { uint16_t v; memcpy(&v,*p,2); (*p)+=2; return v; }
static float    rd_f32(const uint8_t** p) { float    v; memcpy(&v,*p,4); (*p)+=4; return v; }

// ---------------------------------------------------------------------------
// Public serialisation API
// ---------------------------------------------------------------------------
int preset_serialize(void* buf, size_t buf_max,
                     const char* name,
                     const float* norms, int norms_len) {
    const size_t param_count = (size_t)kJunoParamCount;
    const size_t need = 42u + param_count * 6u;
    if (buf_max < need) return -1;

    uint8_t* p = (uint8_t*)buf;

    // Header
    memcpy(p, "TNMT", 4); p += 4;
    wr_u8(&p, PRESET_FORMAT_VERSION);
    wr_u8(&p, PRESET_MODEL_JUNO);
    wr_u16(&p, 0u);  // flags, reserved

    // Name (32 bytes, null-padded)
    char name_buf[PRESET_NAME_LEN] = {};
    if (name) strncpy(name_buf, name, PRESET_NAME_LEN - 1);
    memcpy(p, name_buf, PRESET_NAME_LEN); p += PRESET_NAME_LEN;

    // Count
    wr_u16(&p, (uint16_t)param_count);

    // Param entries: norm → physical via the table's curve
    for (int i = 0; i < kJunoParamCount; i++) {
        const ParamDesc& d = JUNO_PARAM_TABLE[i];
        float norm = (d.id < (uint16_t)norms_len) ? norms[d.id] : 0.0f;
        float phys = apply_curve_local(d, norm);
        wr_u16(&p, d.id);
        wr_f32(&p, phys);
    }

    return (int)(p - (uint8_t*)buf);
}

int preset_parse(const void* buf, size_t len,
                 char* name_out, int name_max,
                 uint16_t* ids_out, float* vals_out, int max_count) {
    static constexpr size_t kHeaderSize = 42u;
    if (len < kHeaderSize) return -1;

    const uint8_t* p = (const uint8_t*)buf;

    // Magic
    if (memcmp(p, "TNMT", 4) != 0) return -1;
    p += 4;

    uint8_t version  = rd_u8(&p);
    uint8_t model_id = rd_u8(&p);
    rd_u16(&p);  // flags, ignored

    if (version != PRESET_FORMAT_VERSION || model_id != PRESET_MODEL_JUNO) return -1;

    // Name
    if (name_out && name_max > 0) {
        int copy = (name_max - 1 < PRESET_NAME_LEN) ? name_max - 1 : PRESET_NAME_LEN;
        memcpy(name_out, p, copy);
        name_out[copy] = '\0';
        // Ensure null-termination at the actual string end.
        name_out[name_max - 1] = '\0';
    }
    p += PRESET_NAME_LEN;

    uint16_t count = rd_u16(&p);

    // Validate enough bytes remain for the declared count.
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
    return n;
}
