// engine/juno_model.cpp — JunoModel implementation.
#include "juno_model.h"
#include "juno_voice.h"

static const SynthModelMeta kJunoMeta = {"juno106", "Juno-106", 1};

void JunoModel::init(float sample_rate) {
    sample_rate_ = sample_rate;
}

const SynthModelMeta& JunoModel::meta() const {
    return kJunoMeta;
}

IVoice* JunoModel::make_voice() {
    JunoVoice* v = new JunoVoice();
    v->init(sample_rate_);
    return v;
}
