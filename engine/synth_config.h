// engine/synth_config.h — compile-time tuning constants (ADR 0003, ADR 0015).
// Never use a literal voice count in pool arrays, loops, or unison math —
// always kNumVoices. Runtime fat/thin mode is Stage 3+.
#pragma once

static constexpr int kNumVoices = 8;

// Pitch-bend wheel range, applied directly to voice pitch (Stage 5c). Fixed ±2
// semitones (standard wheel range); not a patch param in v1. RPN bend-range ignored.
static constexpr float kPitchBendRangeSemis = 2.0f;
