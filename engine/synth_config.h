// engine/synth_config.h — compile-time tuning constants (ADR 0003, ADR 0015).
// Never use a literal voice count in pool arrays, loops, or unison math —
// always kNumVoices. Runtime fat/thin mode is Stage 3+.
#pragma once

static constexpr int kNumVoices = 8;

// Pitch-bend wheel range, applied directly to voice pitch (Stage 5c). Fixed ±2
// semitones (standard wheel range); not a patch param in v1. RPN bend-range ignored.
static constexpr float kPitchBendRangeSemis = 2.0f;

// Stage 8a: max direct-path (arp off) note-ons admitted per audio block. A
// chord slamming N note-ons into one block spikes render time (allocator scan
// + note_on() init x N) past the block budget and starves the blocking I2S
// DMA. Capping admissions spreads an 8-note chord over ceil(8/2)=4 blocks
// (~5.3ms at 1333us/block) -- well below strum-perception threshold. Note-offs
// are never capped (see synth.cpp drain loop).
static constexpr int kMaxNoteOnsPerBlock = 2;
