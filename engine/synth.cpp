// engine/synth.cpp — 8-voice Juno allocator + param store + master chorus.
//
// IRAM_ATTR (ADR 0013): synth_render + JunoVoice::render + ParamStore::drain
// are in IRAM so the render path survives a flash write/erase (preset save,
// Stage 2d+). DaisySP vendor .cpp files remain in flash I-cache for now; full
// IRAM coverage for those is a later optimisation noted in spec 02.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

#include "synth.h"
#include "juno_model.h"
#include "voice_alloc.h"
#include "command_queue.h"
#include "param_desc.h"
#include "param_id.h"
#include "param_store.h"
#include "synth_config.h"
#include "dsp/saturate.h"
#include "Effects/chorus.h"
#include <string.h>
#include <math.h>

// Equal-power unison gain compensation (ADR / this fix).
// Scaling by 1/sqrt(U) keeps perceived loudness roughly constant across unison
// settings for detuned (decorrelated) voices and keeps worst-case peaks below
// the soft_clip hard-clamp at ±1.5 (U=8 → factor ≈0.354; worst-case sum ≈1.41 < 1.5).
// U=1 → 1.0 (bit-identical to pre-unison output).
static inline float unison_gain(int count) {
    return 1.0f / sqrtf((float)(count < 1 ? 1 : count));
}

// Pre-allocated mono accumulation buffer (ADR RT rule #1: no alloc in render).
// 256 frames is the device platform ceiling (MAX_BLOCK in platform_device.c).
static const size_t kMaxBlock = 256;
static float        s_mono[kMaxBlock];

static JunoModel        s_juno_model;
static VoiceAlloc       s_alloc;
static daisysp::Chorus  s_chorus;
static ParamStore       s_params;
static float            s_sample_rate = 48000.0f;

// Note events cross from the control thread (core 0) to the audio thread
// (core 1) via this ring. 64 slots >> the few events a UI frame can produce.
static CommandQueue<64> s_cmds;

void synth_init(uint32_t sample_rate, size_t block_size) {
    s_sample_rate = (float)sample_rate;
    s_juno_model.init((float)sample_rate);
    s_alloc.init(&s_juno_model);

    s_chorus.Init((float)sample_rate);

    // ParamStore initialised with the Juno table. Smoothing coefficients are
    // block-rate (block_size / sample_rate per block). All params start at
    // their table defaults — the first synth_render drain propagates these
    // to voices so the sound is identical to the Stage 1 hardcoded values.
    s_params.init(JUNO_PARAM_TABLE, kJunoParamCount, (float)sample_rate, (int)block_size);
}

// Render the full voice pool + chorus + master gain.
// IRAM_ATTR: this function must survive a flash write/erase (ADR 0013).
//
// Gain pipeline (ADR 0016):
//   Voices → mono bus → DaisySP Chorus (~−12 dB from equal-wet gain_frac=0.5)
//   → × MASTER_GAIN (default 0.5, ~−6 dB) → soft_clip → output.
// soft_clip (dsp/saturate.h): cubic S-curve, unity slope at 0, ±1 ceiling at ±1.5.
// Keeps loud chords from hard-clipping; subtle on normal playing.
IRAM_ATTR void synth_render(float* left, float* right, size_t n, void* user) {
    (void)user;
    size_t frames = n < kMaxBlock ? n : kMaxBlock;

    // 1. Drain control-thread note commands. This is the only place the voice
    //    pool is mutated; voices cannot race with the UI thread.
    NoteCmd cmd;
    while (s_cmds.pop(cmd)) {
        if (cmd.type == NoteCmd::kNoteOn) {
            NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
            s_alloc.note_on(cmd.pitch, cmd.velocity, expr);
        } else {
            s_alloc.note_off(cmd.pitch);
        }
    }

    // 2. Advance the param store smoothers and update targets from the ring.
    s_params.drain();

    // 2a. Stage 3d-i/ii: update play mode, portamento, and unison from the param store,
    //     then advance the glide ramp by one block. Done after drain() so we
    //     use the freshest smoothed values. block_time is exact for this block.
    // unison_count is hoisted out of the inner block so step 6 can use it for
    // equal-power gain compensation (1/√U) without a second param read.
    int unison_count;
    {
        int   play_mode = (int)s_params.get(ParamId::PLAY_MODE);
        float porto     = s_params.get(ParamId::PORTAMENTO_TIME);
        s_alloc.set_play_mode(static_cast<PlayMode>(play_mode));
        s_alloc.set_portamento_time(porto);
        // Stage 3d-ii: unison (UNISON_COUNT and UNISON_DETUNE are CURVE_STEPPED/LIN;
        // block-rate update here matches the same pattern as play_mode/portamento).
        unison_count        = (int)s_params.get(ParamId::UNISON_COUNT);
        float unison_detune = s_params.get(ParamId::UNISON_DETUNE);
        s_alloc.set_unison_count(unison_count);
        s_alloc.set_unison_detune(unison_detune);
        float block_time = (float)frames / s_sample_rate;
        s_alloc.advance_glide(block_time);
    }

    // 3. Push only params that changed this block to all voices (block-rate).
    //    Steady state pushes nothing; a knob sweep pushes only that param.
    //    Idle voices receive the push too (cheap now) so a newly triggered
    //    voice always holds current values. Voices ignore ids they don't handle.
    const VoiceSlot* slots = s_alloc.slots();
    {
        int nch = s_params.changed_count();
        for (int c = 0; c < nch; c++) {
            uint16_t id  = s_params.changed_id(c);
            float    val = s_params.get(id);
            for (int v = 0; v < kNumVoices; v++) {
                slots[v].voice->set_param(id, val);
            }
        }
    }

    // 4. Update chorus (non-per-voice) from the param store.
    // CHORUS_MODE: 0=off, 1=Chorus I (slow/lush), 2=Chorus II (fast/wide).
    // Mode I/II differ in rate preset; depth and delay are user-tweakable on top.
    int chorus_mode = (int)s_params.get(ParamId::CHORUS_MODE);
    if (chorus_mode > 0) {
        // Apply mode-specific rate preset, then user rate on top (additive).
        // Mode I ≈ 0.5 Hz base; Mode II ≈ 1.0 Hz base (classic Juno behaviour).
        float mode_rate = (chorus_mode == 2) ? 1.0f : 0.5f;
        s_chorus.SetLfoFreq(mode_rate + s_params.get(ParamId::CHORUS_RATE) * 0.5f);
        s_chorus.SetLfoDepth(s_params.get(ParamId::CHORUS_DEPTH));
        s_chorus.SetDelay(s_params.get(ParamId::CHORUS_DELAY));
    }

    // 5. Sum all active voices into the mono bus.
    memset(s_mono, 0, frames * sizeof(float));
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) {
            slots[v].voice->render(s_mono, frames);
        }
    }

    // 6. Mono bus → stereo chorus → master gain → soft-clip → output (ADR 0016).
    // Apply equal-power unison compensation (1/√U) so U voices stacked on one note
    // do not sum louder than a single voice. Uses unison_count from step 2a above.
    float gain = s_params.get(ParamId::MASTER_GAIN) * unison_gain(unison_count);
    for (size_t i = 0; i < frames; i++) {
        if (chorus_mode > 0) {
            s_chorus.Process(s_mono[i]);
            left[i]  = soft_clip(s_chorus.GetLeft()  * gain);
            right[i] = soft_clip(s_chorus.GetRight() * gain);
        } else {
            // Chorus off: output mono signal to both channels (no stereo spread).
            float v = soft_clip(s_mono[i] * gain);
            left[i]  = v;
            right[i] = v;
        }
    }
}

// Control thread (core 0): enqueue for the audio thread to apply.
void engine_note_on(uint8_t pitch, uint8_t velocity) {
    NoteCmd c{NoteCmd::kNoteOn, pitch, velocity};
    s_cmds.push(c);
}

void engine_note_off(uint8_t pitch) {
    NoteCmd c{NoteCmd::kNoteOff, pitch, 0};
    s_cmds.push(c);
}

void engine_set_param(uint16_t id, float value) {
    s_params.param_set(id, value);
}

void engine_set_param_norm(uint16_t id, float norm) {
    s_params.param_set_norm(id, norm);
}

int engine_active_voices(void) {
    const VoiceSlot* slots = s_alloc.slots();
    int count = 0;
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) count++;
    }
    return count;
}

void engine_set_routings(const Routing* routings, int count) {
    // Build a ModMatrix from the supplied routings, then push it to every voice.
    // Called from the control thread (preset load); not in the audio path.
    ModMatrix mat;
    mat.clear();
    if (routings && count > 0) {
        int slots_to_set = (count < kMaxRoutes) ? count : kMaxRoutes;
        for (int i = 0; i < slots_to_set; i++) {
            mat.set_route(i, routings[i]);
        }
    }
    const VoiceSlot* slots = s_alloc.slots();
    for (int v = 0; v < kNumVoices; v++) {
        slots[v].voice->set_mod_matrix(mat);
    }
}

float engine_get_param(uint16_t id) {
    // Control-thread read of the smoothed param value. May lag the audio thread
    // by up to one block (~1.3 ms at 48k/64) — fine for display use only.
    return s_params.get(id);
}
