// engine/param_store.h — single-writer parameter store (spec 05).
//
// All value changes — UI knobs, MIDI CC, preset load, mod matrix (later) —
// go through param_set() or param_set_norm(). The call applies the curve,
// clamps the range, and pushes a ParamUpdate into a lock-free SPSC ring.
// The audio thread calls drain() once per block: it drains the ring and
// advances one-pole smoothers so voices always read a smooth value (no
// zipper noise). One writer, one reader — no locks anywhere.
//
// Stage 2b wires drain() into synth_render() and passes smoothed values
// to voices. For now (Stage 2a) ParamStore is standalone and tested in
// isolation.
#pragma once

#include <cstdint>
#include "param_desc.h"
#include "spsc_ring.h"

// A normalised-value update pushed by the control thread.
// value is already in physical units (post-curve, clamped to [min, max]).
struct ParamUpdate {
    uint16_t id;
    float    value;
};

// Maximum param ID accepted by the store. All Juno IDs are < ParamId::kMax
// (0x80 = 128). Increase if the ID namespace ever grows beyond 0x7F.
static constexpr uint16_t kParamIdMax = 128;

class ParamStore {
public:
    // Initialise from the param table and audio parameters. Precomputes
    // block-rate smoothing coefficients from (sample_rate, block_size).
    // block_size must match the audio render block size.
    void init(const ParamDesc* table, int count, float sample_rate, int block_size);

    // ---- Control-thread API ----

    // Set by normalised position in [0, 1]; curve mapping applied internally.
    // source: 0=UI, 1=MIDI, 2=preset, 3=mod (future — ignored for now).
    // Returns false if the ring is full (event dropped gracefully).
    bool param_set_norm(uint16_t id, float norm, uint8_t source = 0);

    // Set directly in physical units (clamped to [min, max]).
    bool param_set(uint16_t id, float value, uint8_t source = 0);

    // ---- Audio-thread API ----

    // Drain the update ring and advance smoothers. Call once per block,
    // before rendering. Must be called on the audio thread only.
    // After drain(), changed_count()/changed_id() report which params moved.
    void drain();

    // Read the current smoothed value. Returns 0 for unknown ids.
    // Read-only — safe to call from the audio thread.
    float get(uint16_t id) const;

    // Param ids whose smoothed value changed in the most recent drain().
    // Valid until the next drain(). Audio-thread only.
    int      changed_count() const { return changed_count_; }
    uint16_t changed_id(int i) const { return changed_ids_[i]; }

private:
    struct ParamState {
        float target;   // last value pushed from the control thread
        float current;  // smoothed output consumed by the audio path
        float alpha;    // block-rate one-pole coefficient (0=no-op, 1=instant)
        bool  valid;    // true if this slot has a ParamDesc entry
    };

    ParamState                s_[kParamIdMax];
    SpscRing<ParamUpdate, 64> ring_;
    const ParamDesc*          table_ = nullptr;
    int                       count_ = 0;

    // Changed-set: populated fresh by each drain(). Audio-thread only.
    uint16_t changed_ids_[kParamIdMax];
    int      changed_count_   = 0;
    bool     force_all_dirty_ = false;  // set true by init(); consumed on first drain()

    const ParamDesc* find_desc(uint16_t id) const;
    static float     apply_curve(const ParamDesc& d, float norm);
    static float     clamp_value(const ParamDesc& d, float v);
};
