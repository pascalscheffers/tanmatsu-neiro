// engine/synth_config.h — compile-time tuning constants (ADR 0003, ADR 0015).
// Never use a literal voice count in pool arrays, loops, or unison math —
// always kNumVoices. Runtime fat/thin mode is Stage 3+.
#pragma once

static constexpr int kNumVoices = 6;

// Pitch-bend wheel range, applied directly to voice pitch (Stage 5c). Fixed ±2
// semitones (standard wheel range); not a patch param in v1. RPN bend-range ignored.
static constexpr float kPitchBendRangeSemis = 2.0f;

// Minimum distance between direct-path (arp-off) note starts. The gate still
// admits at most ONE note-on per render block — that cap is a real deadline fix
// (two fresh voices in one block spiked to ~2.5 ms vs the 1333 us budget). The
// cross-block cooldown below is the tunable part: the WO-12a I2S-framing fix
// removed the audible onset crackle that motivated the old conservative 12-block
// (16 ms) spacing, so it is reduced to 2 blocks (~2.67 ms; an 8-note chord now
// spans 7 * 2.67 ms ≈ 19 ms first-to-last, down from 112 ms). Note-offs ahead of
// a deferred note-on are still drained immediately (see synth.cpp). Confirm onset
// `over=0` on a PROFILE device run; raise again only if onsets miss the deadline.
static constexpr int kNoteOnStartIntervalBlocks = 2;
