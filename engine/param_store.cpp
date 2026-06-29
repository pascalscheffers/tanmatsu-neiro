// engine/param_store.cpp — ParamStore implementation (spec 05).
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

#include "param_store.h"
#include <math.h>
#include <string.h>

void ParamStore::init(const ParamDesc* table, int count,
                      float sample_rate, int block_size) {
    table_ = table;
    count_ = count;
    memset(s_, 0, sizeof(s_));

    // block_dt: time (seconds) represented by one audio block.
    // alpha: one-pole coefficient — fraction of distance covered per block.
    const float block_dt = (float)block_size / sample_rate;

    changed_count_   = 0;
    force_all_dirty_ = true;  // first drain() must push all params to voices

    for (int i = 0; i < count; i++) {
        const ParamDesc& d = table[i];
        if (d.id >= kParamIdMax) continue;
        ParamState& s = s_[d.id];
        s.valid   = true;
        s.current = d.def;
        s.target  = d.def;
        if (d.smoothing_ms > 0.0f) {
            const float tau = d.smoothing_ms * 0.001f;
            s.alpha = 1.0f - expf(-block_dt / tau);
        } else {
            s.alpha = 1.0f;  // instant: current tracks target exactly
        }
    }
}

bool ParamStore::param_set_norm(uint16_t id, float norm, uint8_t source) {
    (void)source;
    const ParamDesc* d = find_desc(id);
    if (!d) return false;
    const float v = apply_curve(*d, norm);
    return ring_.push(ParamUpdate{id, v});
}

bool ParamStore::param_set(uint16_t id, float value, uint8_t source) {
    (void)source;
    const ParamDesc* d = find_desc(id);
    if (!d) return false;
    const float v = clamp_value(*d, value);
    return ring_.push(ParamUpdate{id, v});
}

IRAM_ATTR void ParamStore::drain() {
    changed_count_ = 0;

    // Force-all-dirty path: first drain after init() marks every valid param
    // changed so voices receive all initial values unconditionally.
    if (force_all_dirty_) {
        force_all_dirty_ = false;
        // Drain the ring first to pick up any pre-init targets.
        ParamUpdate upd;
        while (ring_.pop(upd)) {
            if (upd.id < kParamIdMax && s_[upd.id].valid) {
                s_[upd.id].target = upd.value;
                if (s_[upd.id].alpha >= 1.0f) {
                    s_[upd.id].current = upd.value;
                }
            }
        }
        // Advance smoothers then mark every valid param as changed.
        for (uint16_t i = 0; i < kParamIdMax; i++) {
            ParamState& s = s_[i];
            if (!s.valid) continue;
            if (s.alpha < 1.0f) {
                s.current += (s.target - s.current) * s.alpha;
                s.current += 1e-18f;
                s.current -= 1e-18f;
            }
            changed_ids_[changed_count_++] = i;
        }
        return;
    }

    // 1. Drain the ring: update targets (and snap instants immediately).
    //    Track which ids received a new target this block.
    bool new_target[kParamIdMax] = {};
    ParamUpdate upd;
    while (ring_.pop(upd)) {
        if (upd.id < kParamIdMax && s_[upd.id].valid) {
            s_[upd.id].target = upd.value;
            if (s_[upd.id].alpha >= 1.0f) {
                s_[upd.id].current = upd.value;
            }
            new_target[upd.id] = true;
        }
    }

    // 2. Advance one-pole smoothers; collect changed ids.
    for (uint16_t i = 0; i < kParamIdMax; i++) {
        ParamState& s = s_[i];
        if (!s.valid) continue;

        if (s.alpha >= 1.0f) {
            // Instant param: only changed if a new target arrived this block.
            if (new_target[i]) {
                changed_ids_[changed_count_++] = i;
            }
            continue;
        }

        const float before = s.current;
        s.current += (s.target - s.current) * s.alpha;
        // Anti-denormal: P4 has no hardware FTZ (ADR 0012). Smoothed params
        // can approach zero asymptotically; a tiny DC offset flushes the
        // denormal before it stalls the FPU pipeline.
        s.current += 1e-18f;
        s.current -= 1e-18f;

        // Snap to target when settled (avoids asymptotic crawl).
        const float tgt = s.target;
        if (fabsf(tgt - s.current) <= fabsf(tgt) * 1e-5f + 1e-9f) {
            s.current = tgt;
        }

        if (new_target[i] || s.current != before) {
            changed_ids_[changed_count_++] = i;
        }
    }
}

float ParamStore::get(uint16_t id) const {
    if (id >= kParamIdMax || !s_[id].valid) return 0.0f;
    return s_[id].current;
}

const ParamDesc* ParamStore::find_desc(uint16_t id) const {
    for (int i = 0; i < count_; i++) {
        if (table_[i].id == id) return &table_[i];
    }
    return nullptr;
}

float ParamStore::apply_curve(const ParamDesc& d, float norm) {
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    switch (d.curve) {
        case CURVE_LIN:
            return d.min + norm * (d.max - d.min);
        case CURVE_EXP:
            if (d.min > 0.0f && d.max > 0.0f) {
                return d.min * powf(d.max / d.min, norm);
            }
            // Fallback to linear if range is not positive-definite.
            return d.min + norm * (d.max - d.min);
        case CURVE_LOG: {
            // log2(1+norm) maps [0,1] → [0,1] with logarithmic taper.
            // norm=0 → 0, norm=1 → log2(2)=1. Good for dB-like controls.
            const float t = log2f(1.0f + norm);
            return d.min + t * (d.max - d.min);
        }
        case CURVE_STEPPED: {
            const int steps = (int)(d.max - d.min);
            int step  = (int)(norm * (float)(steps + 1));
            if (step > steps) step = steps;
            if (step < 0)     step = 0;
            return d.min + (float)step;
        }
    }
    return d.min + norm * (d.max - d.min);  // unreachable, satisfies compiler
}

float ParamStore::clamp_value(const ParamDesc& d, float v) {
    if (v < d.min) return d.min;
    if (v > d.max) return d.max;
    return v;
}
