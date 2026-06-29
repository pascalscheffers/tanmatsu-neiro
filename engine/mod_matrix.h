// engine/mod_matrix.h — fixed 16-slot modulation matrix (ADR 0009).
//
// Frozen shape (ratified 2026-06-28): 16 Routing slots per patch, fixed array,
// no allocation, O(active routes). See ADR 0009 §Frozen shape for full rationale.
//
// Audio-rate dests (FILTER_CUTOFF, pitch, OSC_LEVEL) are accumulated per eval()
// call; all other dests are control-rate (once per block). The distinction is
// encoded in is_audio_rate_dest().
//
// Denormal safety (ADR 0012): accumulators are protected by a +1e-20f DC offset
// so they never underflow into the subnormal range on the P4 (no HW FTZ/DAZ).
//
// Usage:
//   ModMatrix mat;
//   mat.clear();
//   mat.set_route(0, {ModSource::LFO1, ParamId::FILTER_CUTOFF, 0.5f, ModCurve::LIN});
//   ModOutputs out = mat.eval(sources, n_samples);
//   float cutoff_mod = out.cutoff;   // semitone/Hz offset to add to base cutoff

#pragma once
#include <cmath>
#include <cstdint>
#include "param_id.h"

// ── enums ──────────────────────────────────────────────────────────────────

// Stable source ids — never reuse or renumber (preset-format relevant).
// Gaps are reserved for MPE/macros/S&H/seq in later stages.
enum class ModSource : uint8_t {
    NONE = 0,  // slot inactive

    // Per-voice sources (resolve per voice):
    LFO1      = 1,
    LFO2      = 2,
    ENV1      = 3,  // amp envelope
    ENV2      = 4,  // filter/mod envelope
    VELOCITY  = 5,
    KEY_TRACK = 6,

    // Global sources (resolve once per block):
    MOD_WHEEL  = 7,
    PITCH_BEND = 8,
    AFTERTOUCH = 9,

    // ids 10–19: reserved for MPE pressure, MPE slide, macros, S&H, seq lanes
    _COUNT = 10,  // count of currently-allocated source ids
};

// Curve shapes applied to (depth × source) before summing into the dest.
// LIN is required; SQR/CUBE are cheap extra shapes. 0 = LIN (default).
enum class ModCurve : uint8_t {
    LIN  = 0,  // identity — y = x
    SQR  = 1,  // signed square — y = x * |x|   (preserves sign)
    CUBE = 2,  // signed cube  — y = x * x * x
};

// ── virtual dest sentinel ──────────────────────────────────────────────────

// kModDestPitch: virtual destination not in JUNO_PARAM_TABLE.
// Unit: semitone offset (added to MIDI pitch before mtof()).
// The JunoVoice render() reads mod_out.pitch_semi for this.
static constexpr uint16_t kModDestPitch = 0xFFFE;

// ── Routing slot ──────────────────────────────────────────────────────────

// 8 bytes incl. padding; serialized field-by-field 1+2+4+1 = 8 bytes (ADR 0009).
// A slot is inactive (skipped) when source == NONE or depth == 0.
struct Routing {
    uint8_t  source;         // ModSource id
    uint16_t dest_param_id;  // ParamId or kModDestPitch
    float    depth;          // bipolar [-1, +1]; scales the dest's natural range
    uint8_t  curve;          // ModCurve id, default 0 = LIN
};

static constexpr int kMaxRoutes = 16;  // fixed slots per patch (ADR 0009)

// ── source bundle ─────────────────────────────────────────────────────────

// Per-eval call: caller fills in the values for each ModSource.
// Per-voice fields come from JunoVoice accessors; global fields are filled once
// per block by the engine (Stage 3c will wire global sources).
// Unimplemented fields should remain 0.0f.
struct ModSources {
    float lfo1       = 0.0f;  // lfo1_value()  (already depth-scaled in 3a)
    float lfo2       = 0.0f;  // lfo2_value()
    float env1       = 0.0f;  // amp envelope last-sample output
    float env2       = 0.0f;  // env2_value()
    float velocity   = 0.0f;  // [0, 1] from note velocity
    float key_track  = 0.0f;  // [−1, +1] key-tracking offset
    float mod_wheel  = 0.0f;  // [0, 1] CC1
    float pitch_bend = 0.0f;  // [−1, +1] pitch bend wheel
    float aftertouch = 0.0f;  // [0, 1] channel aftertouch
};

// ── accumulated outputs ───────────────────────────────────────────────────

// Results of one eval() call — one accumulator per dest the voice knows about.
// "audio-rate" dests: voice applies per-sample smoothing (block-smoothed interp).
// "ctrl-rate" dests: voice applies once at the start of the block.
struct ModOutputs {
    // Audio-rate (pitch, cutoff, amp):
    float pitch_semi = 0.0f;  // semitone offset for kModDestPitch
    float cutoff_mod = 0.0f;  // absolute Hz offset for FILTER_CUTOFF
    float amp_mod    = 0.0f;  // linear offset for OSC_LEVEL (VCA)

    // Control-rate:
    float res_mod   = 0.0f;  // offset for FILTER_RES
    float osc_sub   = 0.0f;  // offset for SUB_LEVEL
    float osc_noise = 0.0f;  // offset for NOISE_LEVEL
};

// ── ModMatrix ─────────────────────────────────────────────────────────────

class ModMatrix {
public:
    // Zero all 16 slots (source = NONE, depth = 0).
    void clear();

    // Write one routing slot (idx 0..kMaxRoutes-1). Silently clamps idx.
    void set_route(int idx, Routing r);

    // Read back a slot.
    Routing get_route(int idx) const;

    // Evaluate all active routes given the current source values and return
    // the accumulated per-dest modulation outputs.
    // n_samples: block size hint (currently unused — control-rate eval uses 1
    // tick; audio-rate callers scale by block size if needed).
    // O(kMaxRoutes) worst-case; skips inactive slots early.
    ModOutputs eval(const ModSources& src) const;

    // True when dest_id is an audio-rate destination.
    static bool is_audio_rate_dest(uint16_t dest_id);

private:
    Routing routes_[kMaxRoutes] = {};

    // Apply curve shaping to a value.
    static float apply_curve(float x, uint8_t curve_id);

    // Look up the raw source value for a given source id.
    static float source_value(ModSource src_id, const ModSources& s);
};
