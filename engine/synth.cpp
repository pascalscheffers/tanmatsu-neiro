// engine/synth.cpp — Stage 1c: 8-voice allocator + master chorus.
//
// The extern "C" interface in synth.h is unchanged — callers (platform_*.c)
// see C linkage regardless of this translation unit being compiled as C++.
//
// IRAM_ATTR (ADR 0013): synth_render and JunoVoice::render are in IRAM so
// the render path survives a flash write/erase (preset save, Stage 2+).
// DaisySP vendor .cpp files remain in flash I-cache for now; full IRAM
// coverage for those is a later optimisation noted in spec 02.
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

void synth_init(uint32_t sample_rate) {
    s_juno_model.init((float)sample_rate);
    s_alloc.init(&s_juno_model);

    s_chorus.Init((float)sample_rate);
    // Juno-106 chorus character: slow, warm sweep.
    // Stage 2 lifts these to the param table (hardcoded for now).
    s_chorus.SetLfoFreq(0.5f);   // Hz — slow triangle LFO
    s_chorus.SetLfoDepth(0.7f);  // 0-1
    s_chorus.SetDelay(0.4f);     // 0-1, maps to ~3.3 ms delay internally
}

// Render the full voice pool + chorus.
// IRAM_ATTR: this function must survive a flash write/erase (ADR 0013).
// Gain: DaisySP Chorus has an inherent ×0.25 output gain (~-12 dB) from its
// equal dry/wet mix (×0.5 per engine) and gain_frac=0.5. This gives good
// headroom for 1-4 simultaneous voices; at 8 voices the bus may clip in the
// extreme case. Stage 2 adds a master-gain param to tune this properly.
IRAM_ATTR void synth_render(float* left, float* right, size_t n, void* user) {
    (void)user;
    size_t frames = n < kMaxBlock ? n : kMaxBlock;
    memset(s_mono, 0, frames * sizeof(float));

    // Sum all active voices into the mono bus.
    const VoiceSlot* slots = s_alloc.slots();
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) {
            slots[v].voice->render(s_mono, frames);
        }
    }

    // Mono bus → stereo chorus → output.
    for (size_t i = 0; i < frames; i++) {
        s_chorus.Process(s_mono[i]);
        left[i]  = s_chorus.GetLeft();
        right[i] = s_chorus.GetRight();
    }
}

void engine_note_on(uint8_t pitch, uint8_t velocity) {
    NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
    s_alloc.note_on(pitch, velocity, expr);
}

void engine_note_off(uint8_t pitch) {
    s_alloc.note_off(pitch);
}
