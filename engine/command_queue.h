// engine/command_queue.h — lock-free note-command ring (control → audio).
//
// The control thread (UI/MIDI, core 0) and the audio thread (render, core 1)
// must never touch voice state at the same time. Instead of a mutex (forbidden
// in the audio path — CLAUDE.md RT rule #2), note events cross the boundary
// through this single-producer/single-consumer lock-free ring: the control
// thread push()es commands; synth_render() drains them with pop() at the top of
// the block, so voice allocation is mutated only on the audio thread.
//
// The generic ring lives in spsc_ring.h. This file defines NoteCmd and provides
// a backward-compatible CommandQueue<Cap> alias so existing callers need no
// changes. The param store (Stage 2a) imports spsc_ring.h directly.
#pragma once

#include <cstdint>
#include "spsc_ring.h"

// One control event destined for the voice allocator. Kept tiny + trivially
// copyable so push/pop are plain word stores.
struct NoteCmd {
    enum Type : uint8_t { kNoteOn = 0, kNoteOff = 1 };
    uint8_t type;
    uint8_t pitch;
    uint8_t velocity;  // unused for note_off
};

// Backward-compat alias: CommandQueue<Cap> == SpscRing<NoteCmd, Cap>.
// All existing call sites (synth.cpp, test_command_queue.cpp) continue to
// compile unchanged.
template <size_t Cap>
using CommandQueue = SpscRing<NoteCmd, Cap>;
