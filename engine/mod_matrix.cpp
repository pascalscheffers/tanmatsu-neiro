// engine/mod_matrix.cpp — modulation matrix implementation (ADR 0009).
// See mod_matrix.h for the frozen shape, enums, and usage notes.
//
// Eval is O(kMaxRoutes) worst-case; inactive slots (NONE source or zero depth)
// are skipped with a single branch.  Denormal safety: accumulators are
// initialized with a +1e-20f DC offset (ADR 0012 — P4 RV32F has no HW FTZ).

#include "mod_matrix.h"
#include <cmath>    // fabsf
#include <cstring>  // memset
#include "param_id.h"

// ── helpers ────────────────────────────────────────────────────────────────

float ModMatrix::apply_curve(float x, uint8_t curve_id) {
    switch (static_cast<ModCurve>(curve_id)) {
        case ModCurve::LIN:
            return x;
        case ModCurve::SQR:
            return x * fabsf(x);  // signed square
        case ModCurve::CUBE:
            return x * x * x;  // signed cube
        default:
            return x;  // unknown → LIN
    }
}

float ModMatrix::source_value(ModSource src_id, const ModSources& s) {
    switch (src_id) {
        case ModSource::LFO1:
            return s.lfo1;
        case ModSource::LFO2:
            return s.lfo2;
        case ModSource::ENV1:
            return s.env1;
        case ModSource::ENV2:
            return s.env2;
        case ModSource::VELOCITY:
            return s.velocity;
        case ModSource::KEY_TRACK:
            return s.key_track;
        case ModSource::MOD_WHEEL:
            return s.mod_wheel;
        case ModSource::PITCH_BEND:
            return s.pitch_bend;
        case ModSource::AFTERTOUCH:
            return s.aftertouch;
        case ModSource::NONE:
        default:
            return 0.0f;
    }
}

bool ModMatrix::is_audio_rate_dest(uint16_t dest_id) {
    return dest_id == kModDestPitch || dest_id == ParamId::FILTER_CUTOFF || dest_id == ParamId::OSC_LEVEL;
}

// ── ModMatrix public API ───────────────────────────────────────────────────

void ModMatrix::clear() {
    for (int i = 0; i < kMaxRoutes; i++) {
        routes_[i] = Routing{};  // zero-init: source=0 (NONE), depth=0
    }
}

void ModMatrix::set_route(int idx, Routing r) {
    if (idx < 0 || idx >= kMaxRoutes) return;
    routes_[idx] = r;
}

Routing ModMatrix::get_route(int idx) const {
    if (idx < 0 || idx >= kMaxRoutes) return Routing{};
    return routes_[idx];
}

ModOutputs ModMatrix::eval(const ModSources& src) const {
    // Seed with tiny DC offset to prevent denormal underflow in any accumulated
    // sum that remains very close to zero after the loop (ADR 0012).
    ModOutputs out{};
    out.pitch_semi = 1e-20f;
    out.cutoff_mod = 1e-20f;
    out.amp_mod    = 1e-20f;
    out.res_mod    = 1e-20f;
    out.osc_sub    = 1e-20f;
    out.osc_noise  = 1e-20f;
    out.pwm_mod    = 1e-20f;  // denormal guard (ADR 0012)

    for (int i = 0; i < kMaxRoutes; i++) {
        const Routing& r = routes_[i];

        // Skip inactive slots — no branch cost for the common (inactive) case
        // because source == NONE (0) is the zero-init default.
        const ModSource src_id = static_cast<ModSource>(r.source);
        if (src_id == ModSource::NONE || r.depth == 0.0f) continue;

        float raw    = source_value(src_id, src);
        float shaped = apply_curve(raw * r.depth, r.curve);

        // Sum into the appropriate accumulator.
        const uint16_t dest = r.dest_param_id;
        if (dest == kModDestPitch) {
            out.pitch_semi += shaped;
        } else if (dest == ParamId::FILTER_CUTOFF) {
            out.cutoff_mod += shaped;
        } else if (dest == ParamId::OSC_LEVEL) {
            out.amp_mod += shaped;
        } else if (dest == ParamId::FILTER_RES) {
            out.res_mod += shaped;
        } else if (dest == ParamId::SUB_LEVEL) {
            out.osc_sub += shaped;
        } else if (dest == ParamId::NOISE_LEVEL) {
            out.osc_noise += shaped;
        } else if (dest == kModDestPwm) {
            out.pwm_mod += shaped;
        }
        // Unknown dest_param_id: silently ignored (forward-compat, ADR 0009).
    }

    return out;
}
