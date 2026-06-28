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
#include "juno_voice.h"
#include "voice_alloc.h"
#include "command_queue.h"
#include "param_desc.h"
#include "param_id.h"
#include "param_store.h"
#include "synth_config.h"
#include "dsp/saturate.h"
#include "Effects/chorus.h"
#include <string.h>

// Pre-allocated mono accumulation buffer (ADR RT rule #1: no alloc in render).
// 256 frames is the device platform ceiling (MAX_BLOCK in platform_device.c).
static const size_t kMaxBlock = 256;
static float        s_mono[kMaxBlock];

static JunoModel        s_juno_model;
static VoiceAlloc       s_alloc;
static daisysp::Chorus  s_chorus;
static ParamStore       s_params;

// Note events cross from the control thread (core 0) to the audio thread
// (core 1) via this ring. 64 slots >> the few events a UI frame can produce.
static CommandQueue<64> s_cmds;

void synth_init(uint32_t sample_rate, size_t block_size) {
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

    // 3. Push smoothed per-voice params to all voices (block-rate update).
    //    Idle voices receive the push too — negligible cost (8 voices × 10
    //    params), ensures newly triggered voices always have current values.
    const VoiceSlot* slots = s_alloc.slots();
    for (int v = 0; v < kNumVoices; v++) {
        IVoice* voice = slots[v].voice;
        voice->set_param(ParamId::OSC_LEVEL,     s_params.get(ParamId::OSC_LEVEL));
        voice->set_param(ParamId::SUB_LEVEL,     s_params.get(ParamId::SUB_LEVEL));
        voice->set_param(ParamId::NOISE_LEVEL,   s_params.get(ParamId::NOISE_LEVEL));
        voice->set_param(ParamId::FILTER_CUTOFF, s_params.get(ParamId::FILTER_CUTOFF));
        voice->set_param(ParamId::FILTER_RES,    s_params.get(ParamId::FILTER_RES));
        voice->set_param(ParamId::FILTER_MODE,   s_params.get(ParamId::FILTER_MODE));
        voice->set_param(ParamId::ENV_ATTACK,    s_params.get(ParamId::ENV_ATTACK));
        voice->set_param(ParamId::ENV_DECAY,     s_params.get(ParamId::ENV_DECAY));
        voice->set_param(ParamId::ENV_SUSTAIN,   s_params.get(ParamId::ENV_SUSTAIN));
        voice->set_param(ParamId::ENV_RELEASE,   s_params.get(ParamId::ENV_RELEASE));
        // Stage 3a: ENV2 + LFO params.
        voice->set_param(ParamId::ENV2_ATTACK,   s_params.get(ParamId::ENV2_ATTACK));
        voice->set_param(ParamId::ENV2_DECAY,    s_params.get(ParamId::ENV2_DECAY));
        voice->set_param(ParamId::ENV2_SUSTAIN,  s_params.get(ParamId::ENV2_SUSTAIN));
        voice->set_param(ParamId::ENV2_RELEASE,  s_params.get(ParamId::ENV2_RELEASE));
        voice->set_param(ParamId::LFO1_RATE,     s_params.get(ParamId::LFO1_RATE));
        voice->set_param(ParamId::LFO1_DEPTH,    s_params.get(ParamId::LFO1_DEPTH));
        voice->set_param(ParamId::LFO1_SHAPE,    s_params.get(ParamId::LFO1_SHAPE));
        voice->set_param(ParamId::LFO2_RATE,     s_params.get(ParamId::LFO2_RATE));
        voice->set_param(ParamId::LFO2_DEPTH,    s_params.get(ParamId::LFO2_DEPTH));
        voice->set_param(ParamId::LFO2_SHAPE,    s_params.get(ParamId::LFO2_SHAPE));
    }

    // 4. Update chorus (non-per-voice) from the param store.
    s_chorus.SetLfoFreq(s_params.get(ParamId::CHORUS_RATE));
    s_chorus.SetLfoDepth(s_params.get(ParamId::CHORUS_DEPTH));
    s_chorus.SetDelay(s_params.get(ParamId::CHORUS_DELAY));

    // 5. Sum all active voices into the mono bus.
    memset(s_mono, 0, frames * sizeof(float));
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) {
            slots[v].voice->render(s_mono, frames);
        }
    }

    // 6. Mono bus → stereo chorus → master gain → soft-clip → output (ADR 0016).
    float gain = s_params.get(ParamId::MASTER_GAIN);
    for (size_t i = 0; i < frames; i++) {
        s_chorus.Process(s_mono[i]);
        left[i]  = soft_clip(s_chorus.GetLeft()  * gain);
        right[i] = soft_clip(s_chorus.GetRight() * gain);
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
    // Cast to JunoVoice* is safe: only JunoVoice instances are created by JunoModel.
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
        JunoVoice* jv = static_cast<JunoVoice*>(slots[v].voice);
        jv->set_mod_matrix(mat);
    }
}

float engine_get_param(uint16_t id) {
    // Control-thread read of the smoothed param value. May lag the audio thread
    // by up to one block (~1.3 ms at 48k/64) — fine for display use only.
    return s_params.get(id);
}
