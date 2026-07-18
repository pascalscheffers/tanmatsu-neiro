// engine/juno_model.cpp — JunoModel implementation.
#include "juno_model.h"
#include "juno_voice.h"

#ifdef ESP_PLATFORM
#include <assert.h>
#include <new>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"  // esp_ptr_internal
#ifdef SYNTH_PROFILE
#include "esp_log.h"
#endif
#endif

static const SynthModelMeta kJunoMeta = {"juno106", "Juno-106", 1};

void JunoModel::init(float sample_rate) {
    sample_rate_ = sample_rate;
}

const SynthModelMeta& JunoModel::meta() const {
    return kJunoMeta;
}

IVoice* JunoModel::make_voice() {
#ifdef ESP_PLATFORM
    // RT rule 4: per-voice state MUST live in internal SRAM. The render loop
    // reads/writes each voice's osc/filter/env state every sample; a
    // PSRAM-resident voice stalls on the shared MSPI bus whenever core-0 blits
    // the display — the proven ipc-collapse contention (specs/MEMORY.md
    // 2026-07-16 FREEZE_DISPLAY run: ipc 0.72 -> 0.23 at constant instret).
    // Plain `new` only *prefers* internal (SPIRAM_MALLOC_ALWAYSINTERNAL) and can
    // fall back to PSRAM — pin it hard so the render path has no PSRAM data dep.
    // Init-time only (not RT); voices live for the process lifetime, never freed,
    // so the placement-new needs no matching delete.
    void* mem = heap_caps_malloc(sizeof(JunoVoice), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(mem && "voice state must fit in internal SRAM (RT rule 4)");
#ifdef SYNTH_PROFILE
    // One-shot proof of where the *default* allocator would have placed a voice —
    // confirms or denies the PSRAM-voice-state stall hypothesis in one device run.
    void* probe = heap_caps_malloc(sizeof(JunoVoice), MALLOC_CAP_DEFAULT);
    ESP_LOGI("voice", "sizeof=%u pinned=%p internal=%d default=%p def_internal=%d", (unsigned)sizeof(JunoVoice), mem,
             esp_ptr_internal(mem) ? 1 : 0, probe, probe ? (esp_ptr_internal(probe) ? 1 : 0) : -1);
    heap_caps_free(probe);
#endif
    JunoVoice* v = new (mem) JunoVoice();
#else
    JunoVoice* v = new JunoVoice();
#endif
    v->init(sample_rate_);
    return v;
}
