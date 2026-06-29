// engine/synth_model.h — model boundary (ADR 0008).
// A SynthModel is the factory and config for one engine type (e.g. Juno-106).
// The allocator (1c) calls make_voice() once per pool slot at init.
// Stage 1b: interface stub. Stage 1c: JunoModel concrete implementation.
#pragma once

#include "voice.h"

struct SynthModelMeta {
    const char* id;    // e.g. "juno106"
    const char* name;  // display name
    int         version;
};

class SynthModel {
public:
    virtual ~SynthModel() = default;

    virtual const SynthModelMeta& meta() const = 0;

    // Called once per voice slot at init (not in the RT path; allocation OK).
    // Caller owns the returned pointer.
    virtual IVoice* make_voice() = 0;

    // Called by the voice with the current sample rate before make_voice().
    virtual void init(float sample_rate) = 0;
};
