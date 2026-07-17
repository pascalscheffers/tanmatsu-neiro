// engine/synth_config.h — compile-time tuning constants (ADR 0003, ADR 0015).
// Never use a literal voice count in pool arrays, loops, or unison math —
// always kNumVoices. Runtime fat/thin mode is Stage 3+.
#pragma once

static constexpr int kNumVoices = 8;

// Pitch-bend wheel range, applied directly to voice pitch (Stage 5c). Fixed ±2
// semitones (standard wheel range); not a patch param in v1. RPN bend-range ignored.
static constexpr float kPitchBendRangeSemis = 2.0f;

// Stage 8 onset-crackle diagnostic: minimum distance between direct-path
// (arp-off) note starts. At 64 frames / 48 kHz, 3 blocks = 4 ms; an 8-note
// chord therefore spans 7 * 4 ms = 28 ms from first start to last. This is a
// deliberately conservative device A/B. Tighten only after PROFILE shows the
// onset blocks staying below the 1333 us deadline. Note-offs ahead of a
// deferred note-on are still drained immediately (see synth.cpp).
static constexpr int kNoteOnStartIntervalBlocks = 3;
