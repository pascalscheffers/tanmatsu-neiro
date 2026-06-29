// engine/juno_model.h — Juno-106 SynthModel (ADR 0008).
// Factory for JunoVoice instances; wired into VoiceAlloc in synth.cpp.
#pragma once

#include "synth_model.h"

class JunoModel final : public SynthModel {
public:
    void                  init(float sample_rate) override;
    const SynthModelMeta& meta() const override;
    IVoice*               make_voice() override;

private:
    float sample_rate_ = 48000.0f;
};
