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
// Gain pipeline:
//   Voices → mono bus → DaisySP Chorus (~−12 dB from equal-wet gain_frac=0.5)
//   → × MASTER_GAIN (default 0.5, ~−6 dB) → output.
// At default gain (0.5) a full 8-voice chord has substantial headroom.
// Soft-clip vs linear headroom is a 🛑 Stage-2 sonic gate (MEMORY.md);
// for now the output is linear-scaled only — no saturator.
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

    // 6. Mono bus → stereo chorus → master gain → output.
    float gain = s_params.get(ParamId::MASTER_GAIN);
    for (size_t i = 0; i < frames; i++) {
        s_chorus.Process(s_mono[i]);
        left[i]  = s_chorus.GetLeft()  * gain;
        right[i] = s_chorus.GetRight() * gain;
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
