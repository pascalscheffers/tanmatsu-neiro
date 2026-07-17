// engine/synth.cpp — 8-voice Juno allocator + param store + master chorus.
//
// IRAM_ATTR (ADR 0013): synth_render + JunoVoice::render + ParamStore::drain
// are in IRAM so the render path survives a flash write/erase (preset save,
// Stage 2d+). DaisySP vendor .cpp files remain in flash I-cache for now; full
// IRAM coverage for those is a later optimisation noted in spec 02.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

#include <math.h>
#include <string.h>
#include <atomic>
#include "Effects/chorus.h"
#include "arp.h"
#include "arp_clock.h"
#include "clock.h"
#include "command_queue.h"
#include "dsp/dcblock.h"
#include "dsp/lfo.h"
#include "dsp/limiter.h"
#include "dsp/saturate.h"
#include "juno_model.h"
#include "param_desc.h"
#include "param_id.h"
#include "param_store.h"
#include "platform.h"  // platform_cycles_now() — SYNTH_PROFILE CPU sub-timers
#include "record_ring.h"
#include "scheduler.h"
#include "synth.h"
#include "synth_config.h"
#include "voice_alloc.h"

// Equal-power unison gain compensation (ADR / this fix).
// Scaling by 1/sqrt(U) keeps perceived loudness roughly constant across unison
// settings for detuned (decorrelated) voices and keeps worst-case peaks below
// the soft_clip hard-clamp at ±1.5 (U=8 → factor ≈0.354; worst-case sum ≈1.41 < 1.5).
// U=1 → 1.0 (bit-identical to pre-unison output).
static inline float unison_gain(int count) {
    return 1.0f / sqrtf((float)(count < 1 ? 1 : count));
}

// Pre-allocated mono accumulation buffer (ADR RT rule #1: no alloc in render).
// 256 frames is the device platform ceiling (MAX_BLOCK in platform_device.c).
static const size_t kMaxBlock = 256;
static float        s_mono[kMaxBlock];

static JunoModel          s_juno_model;
static VoiceAlloc         s_alloc;
static daisysp::Chorus    s_chorus;
static dsp::LimiterStereo s_limiter;
// Master-bus DC blockers (one per output channel). The rendered wave is
// bottom-heavy (DC bias, envelope-proportional — see specs/MEMORY.md crackle
// forensics); block it before the limiter/soft_clip so peak-detect and clipping
// see a symmetric signal and no headroom is wasted on the offset.
static dsp::DcBlock       s_dc_l;
static dsp::DcBlock       s_dc_r;
static ParamStore         s_params;
static float              s_sample_rate = 48000.0f;

// ADR 0018: shared free-running LFOs — one pair for the whole engine.
// All voices receive the same block-end value (authentic Juno-106 behaviour:
// LFO1/2 are global, not per-voice). Per-note delay fade-in stays in JunoVoice.
static dsp::Lfo s_lfo1;
static dsp::Lfo s_lfo2;

// Stage 5c: channel-wide MIDI expression. Single-writer (control thread) /
// single-reader (audio thread); relaxed atomics, latest-value-wins.
static std::atomic<float> s_mod_wheel{0.0f};
static std::atomic<float> s_pitch_bend{0.0f};
static std::atomic<float> s_aftertouch{0.0f};
// ADR 0021: CC7 channel volume — attenuation-only, square-law taper applied in the
// router; default 1.0 (unity). Not a preset value; reset to 1.0 on panic/init.
static std::atomic<float> s_channel_vol{1.0f};
static std::atomic<bool>  s_panic{false};

// Note events cross from the control thread (core 0) to the audio thread
// (core 1) via this ring. 64 slots >> the few events a UI frame can produce.
static CommandQueue<64> s_cmds;
// Audio-thread-only direct-note start spacing. A value N means the next
// direct note-on may start after N more render-block boundaries.
static int              s_note_on_cooldown_blocks = 0;

// Master musical clock (ADR 0010). Updated once per block on the audio thread.
static Clock s_clock;

// Clock transport/BPM commands cross from the control thread to the audio
// thread via this ring. 16 slots >> the few transport events per UI frame.
static SpscRing<ClockCmd, 16> s_clock_cmds;

// ADR 0010: event scheduler (Stage 4a-ii). Producers (arp, sequencer, ...) push
// ScheduledEvents timestamped in Clock::free_pos() units into s_sched_in;
// the audio thread drains them into s_sched and dispatches due events each block.
// NOTE: free_pos() (not sample_pos()) is the scheduler time base so the arp
// fires regardless of transport start/stop (ADR 0019 free-running convention).
static Scheduler<64>                s_sched;
static SpscRing<ScheduledEvent, 64> s_sched_in;

// Stage 4b-iii: arpeggiator statics (audio thread only).
static Arp      s_arp;
static double   s_arp_phase     = 0.0;  // samples until next step; 0 = fire now
static bool     s_arp_had_notes = false;
static uint32_t s_arp_step      = 0;  // step counter for swing parity

#ifdef SYNTH_PROFILE
// Signal-magnitude probe (diagnostic only — never in the shipping image).
// Written on the audio thread (synth_render), read+reset on the control thread
// (engine_profile_read). Benign cross-core race: diagnostic accuracy is sufficient.
// s_pk_mono:     peak of the raw voice-sum (pre-gain), per render block batch
// s_pk_postgain: peak fed to the limiter (post-gain, pre-GR)
// s_min_gr:      worst (lowest) limiter gain-reduction factor seen
// s_pk_out:      peak output after soft_clip
static volatile float s_pk_mono     = 0.0f;
static volatile float s_pk_postgain = 0.0f;
static volatile float s_min_gr      = 1.0f;
static volatile float s_pk_out      = 0.0f;

// --- Audio RAM tap (crackle forensics; diagnostic only) ---------------------
// Captures the last ~341 ms of rendered stereo output (post soft_clip, the
// exact signal handed to the DAC) into a RAM ring; freezes one-shot when a
// trigger fires, half the ring before the trigger and half after.
//
// 2026-07-16: amplitude theory FALSIFIED by a direct-line recording (step
// discontinuities, zero clamp hits, zero flat-tops -- see MEMORY.md). The
// auto trigger below no longer looks at post-gain peak; it looks at sample-
// to-sample STEP SIZE in the final output -- proven not to fire for this
// glitch (load-independent crackle, clean timing). The DMA-underrun
// hypothesis is also falsified (see MEMORY.md); the companion i2s
// write-timing starve counter that tested it has been removed.
//
// Manual on-demand freeze + re-arm (this round): a human reacting to an
// audible crackle presses SPACE; engine_tap_freeze_now() sets a request
// flag; tap_capture() below latches the trigger on the next block with only
// a short post-trigger tail so the frozen ring is almost entirely
// pre-keypress history. engine_tap_rearm() resets the ring after a dump so
// the next press captures again with no reboot. See synth.h for the reader-
// side contract and specs/MEMORY.md 2026-07-16 for the runbook.
//
// Ring doubled to 32768 frames (128 KiB, ~683 ms @ 48 kHz) so a human's
// ~250 ms reaction time lands comfortably inside the pre-trigger history
// window even after the manual short post-trigger tail.
//
// One-shot per freeze: once s_tap_frozen latches, tap_capture() below is a
// no-op until engine_tap_rearm() resets it (no reboot required). Ring/
// position statics are mutated only on the audio thread (single-writer
// contract); s_tap_frozen is the sole cross-thread signal (release/acquire)
// gating the control thread's read (and re-arm request) of the otherwise
// plain, non-atomic ring/position statics.
static constexpr uint32_t kTapFrames           = 32768;           // 512 blocks * 64 frames, ~683 ms @ 48 kHz
static constexpr uint32_t kTapFramesMask       = kTapFrames - 1;  // power of two -> mask, not %
static constexpr uint32_t kTapPostTrigFrames   = 16384;           // 256 blocks * 64 frames, half the ring (auto path)
// Legit band-limited audio slews well under this; the observed glitch steps
// were ~1.0 full-scale sample-to-sample. Tunable. (Auto path only.)
static constexpr float    kTapStepThreshold    = 0.6f;
// Manual freeze: a tiny post-trigger tail (~1.3 ms) so the frozen ring is
// almost entirely PRE-keypress history -- the human already heard the
// crackle by the time they hit SPACE, so the interesting audio is behind us.
static constexpr uint32_t kTapManualPostFrames = 64;

static int16_t           s_tap_ring[kTapFrames * 2];  // interleaved L,R, 128 KiB
static uint32_t          s_tap_write_pos      = 0;    // next slot to write == oldest valid frame once frozen
static int32_t           s_tap_trig_pos       = -1;   // physical frame index of the trigger, -1 = not yet armed-fired
static uint32_t          s_tap_post_remaining = 0;    // frames left to capture after trigger
static std::atomic<bool> s_tap_frozen{false};
static float             s_tap_prev_l = 0.0f;  // audio-thread-only, plain (matches ring convention)
static float             s_tap_prev_r = 0.0f;
// Control-thread -> audio-thread requests (single-flag, relaxed atomics):
// the control thread only ever sets these; all statics above are still
// mutated exclusively on the audio thread inside tap_capture().
static std::atomic<bool> s_tap_freeze_req{false};  // "freeze now" (manual trigger)
static std::atomic<bool> s_tap_rearm_req{false};   // "reset and unfreeze" (after a dump)

// Convert one output frame to s16 and store it in the ring; latch the
// trigger and, kTapPostTrigFrames later, freeze. Inlined into the step-6 hot
// loop (both the stereo-chorus and mono paths below) -- branch-light: two
// clamp+scale conversions, one index increment/mask, one compare. No
// allocation, no locks, no calls beyond this TU (matches unison_gain()'s
// static-inline convention above; the compiler inlines it at -O2).
// `pk` (post-gain peak) is no longer used for triggering -- see the
// discontinuity comment above -- kept in the signature for call-site
// stability.
static inline void tap_capture(float l, float r, float pk) {
    (void)pk;
    if (s_tap_frozen.load(std::memory_order_relaxed)) {
        // Rearm check runs while frozen (audio-thread-only mutation of the
        // ring/position statics, matching the single-writer contract) --
        // but capture itself still short-circuits below once frozen and not
        // re-arming this block.
        if (s_tap_rearm_req.load(std::memory_order_relaxed)) {
            s_tap_write_pos      = 0;
            s_tap_trig_pos       = -1;
            s_tap_post_remaining = 0;
            s_tap_prev_l         = 0.0f;
            s_tap_prev_r         = 0.0f;
            s_tap_rearm_req.store(false, std::memory_order_relaxed);
            s_tap_frozen.store(false, std::memory_order_release);  // last: unblocks capture next call
        }
        return;
    }
    float step   = fmaxf(fabsf(l - s_tap_prev_l), fabsf(r - s_tap_prev_r));
    s_tap_prev_l = l;
    s_tap_prev_r = r;
    int32_t li   = (int32_t)(l * 32767.0f);
    int32_t ri   = (int32_t)(r * 32767.0f);
    if (li > 32767)
        li = 32767;
    else if (li < -32768)
        li = -32768;
    if (ri > 32767)
        ri = 32767;
    else if (ri < -32768)
        ri = -32768;
    uint32_t pos            = s_tap_write_pos;
    s_tap_ring[pos * 2]     = (int16_t)li;
    s_tap_ring[pos * 2 + 1] = (int16_t)ri;
    s_tap_write_pos         = (pos + 1) & kTapFramesMask;
    if (s_tap_trig_pos < 0) {
        // Manual (human-in-the-loop) freeze takes priority over the auto
        // step-discontinuity trigger -- checked first each block once armed.
        if (s_tap_freeze_req.load(std::memory_order_relaxed)) {
            s_tap_trig_pos       = (int32_t)pos;
            s_tap_post_remaining = kTapManualPostFrames;
            s_tap_freeze_req.store(false, std::memory_order_relaxed);
        } else if (step > kTapStepThreshold) {
            // Auto path kept intact (harmless): still fires if a genuine
            // step discontinuity happens to occur before a manual request.
            s_tap_trig_pos       = (int32_t)pos;
            s_tap_post_remaining = kTapPostTrigFrames;
        }
    } else if (s_tap_post_remaining > 0) {
        if (--s_tap_post_remaining == 0) {
            s_tap_frozen.store(true, std::memory_order_release);
        }
    }
}

// Per-region CPU sub-timers (cycles). Split the whole-block audio cost into four
// regions so a profile says WHERE the time goes — crucially both AVG and MAX per
// region, because the smash-crackle spike is one rare block/window (avg=646 but
// max=2676us): an avg-only readout hides it. The four regions together cover the
// entire block (no gap), so a per-region max that matches the whole-block audio
// max pins the culprit; if none match, the spike is preemption/stall outside the
// markers' additive model. Accumulated per block on the audio thread,
// snapshot+reset on the control thread (engine_profile_read_cpu). Benign race.
static volatile uint64_t s_cpu_drain      = 0;  // steps 1..1b (note + clock + sched drain)
static volatile uint64_t s_cpu_setup      = 0;  // steps 2..4 (param drain/push, arp, LFO, chorus)
static volatile uint64_t s_cpu_voices     = 0;  // step 5 voice-sum loop
static volatile uint64_t s_cpu_master     = 0;  // step 6 master chain loop
static volatile uint32_t s_cpu_drain_max  = 0;
static volatile uint32_t s_cpu_setup_max  = 0;
static volatile uint32_t s_cpu_voices_max = 0;
static volatile uint32_t s_cpu_master_max = 0;
static volatile uint32_t s_cpu_blocks     = 0;  // blocks accumulated since last read

// Stage 8 diag: worst-block snapshot for the voices region. Avg/max hides the
// one rare bad block's full shape; this freezes it. Discriminator:
//   ipc (instret/cycles) low, instret ~flat        -> memory/cache stall
//     - vmax_cyc dominates voices_cyc              -> one cold voice (PSRAM/wavetable)
//     - vmax_cyc small relative to voices_cyc       -> spread/global stall (I-cache XIP)
//   instret high, active LOW                        -> preemption by another task
//   instret high, scaling with active, ipc normal    -> genuine compute in voice render
static volatile uint32_t s_worst_voices_cyc     = 0;  // selection key: largest d_voices seen
static volatile uint32_t s_worst_voices_instret = 0;
static volatile uint32_t s_worst_active         = 0;  // active voice count in that block
static volatile uint32_t s_worst_vmax_cyc       = 0;  // single worst voice's render() cycles
static volatile uint32_t s_worst_vmax_idx       = 0;  // its slot index
static volatile uint32_t s_worst_drain_cyc      = 0;
static volatile uint32_t s_worst_setup_cyc      = 0;
static volatile uint32_t s_worst_master_cyc     = 0;
#endif

void synth_init(uint32_t sample_rate, size_t block_size) {
    s_sample_rate = (float)sample_rate;
    s_juno_model.init((float)sample_rate);
    s_alloc.init(&s_juno_model);
    s_note_on_cooldown_blocks = 0;

    s_chorus.Init((float)sample_rate);
    s_limiter.init((float)sample_rate);  // ADR 0021: master-bus peak limiter
    s_dc_l.init((float)sample_rate);     // master-bus DC blocker (per channel)
    s_dc_r.init((float)sample_rate);

    // ParamStore initialised with the Juno table. Smoothing coefficients are
    // block-rate (block_size / sample_rate per block). All params start at
    // their table defaults — the first synth_render drain propagates these
    // to voices so the sound is identical to the Stage 1 hardcoded values.
    s_params.init(JUNO_PARAM_TABLE, kJunoParamCount, (float)sample_rate, (int)block_size);

    // ADR 0021: reset channel volume to unity on (re)init.
    s_channel_vol.store(1.0f, std::memory_order_relaxed);

    // ADR 0010: master musical clock — derived from the audio sample counter.
    s_clock.init((float)sample_rate);
    // Stage 4a-iii: param table is the UI/preset home for tempo; seed the clock from the
    // table default so block 0 uses the right BPM (mirrors LFO init lines below).
    s_clock.set_bpm(s_params.get(ParamId::CLOCK_BPM));

    // Stage 4b-iii: seed arp config from table defaults so block 0 is correct.
    // Mirrors the LFO seed pattern below: params are the UI/preset home for config;
    // synth_render's step 2b keeps these in sync on subsequent blocks.
    s_arp.init();
    s_arp.set_mode((ArpMode)(int)s_params.get(ParamId::ARP_MODE));
    s_arp.set_octaves((int)s_params.get(ParamId::ARP_OCTAVES));
    s_arp.set_latch(s_params.get(ParamId::ARP_LATCH) > 0.5f);

    // ADR 0018: init shared LFOs from param table defaults so block 0 is correct.
    s_lfo1.init((float)sample_rate);
    s_lfo1.set_rate(s_params.get(ParamId::LFO1_RATE));
    s_lfo1.set_waveform((dsp::LfoWave)(int)s_params.get(ParamId::LFO1_SHAPE));
    s_lfo2.init((float)sample_rate);
    s_lfo2.set_rate(s_params.get(ParamId::LFO2_RATE));
    s_lfo2.set_waveform((dsp::LfoWave)(int)s_params.get(ParamId::LFO2_SHAPE));
}

// Render the full voice pool + chorus + master gain.
// IRAM_ATTR: this function must survive a flash write/erase (ADR 0013).
//
// Gain pipeline (ADR 0016):
//   Voices → mono bus → DaisySP Chorus (~−12 dB from equal-wet gain_frac=0.5)
//   → × MASTER_GAIN (default 0.5, ~−6 dB) → soft_clip → output.
// soft_clip (dsp/saturate.h): cubic S-curve, unity slope at 0, ±1 ceiling at ±1.5.
// Keeps loud chords from hard-clipping; subtle on normal playing.
IRAM_ATTR void synth_render(float* left, float* right, size_t n, void* user) {
    (void)user;
    size_t frames = n < kMaxBlock ? n : kMaxBlock;

#ifdef SYNTH_PROFILE
    // CPU sub-timers: mark region boundaries; accumulate at the end of the block.
    uint64_t _cpu_t0 = platform_cycles_now(), _cpu_t1 = 0, _cpu_t2 = 0, _cpu_t3 = 0;
    // Stage 8 diag: retired-instruction counter at the voices-region boundaries
    // (IPC discriminator), plus per-voice max cost + active count (hot-voice vs
    // uniform-slow discriminator). See worst-block statics above.
    uint64_t _ir_t2 = 0, _ir_t3 = 0;
    uint32_t _vmax_cyc = 0, _vmax_idx = 0, _active_n = 0;
#endif

    // 1. Drain control-thread note commands. This is the only place the voice
    //    pool is mutated (for direct notes); voices cannot race with the UI thread.
    //
    //    Route by ARP_ON (previous block's smoothed value — fine, one-block stale):
    //    - arp_on=true:  feed the arp's held-note set; the arp's scheduled note-on/off
    //      commands reach s_alloc via the scheduler dispatch in step 1b.
    //    - arp_on=false: existing direct path to s_alloc (byte-identical to pre-arp).
    bool    arp_on = s_params.get(ParamId::ARP_ON) > 0.5f;
    NoteCmd cmd;
    if (arp_on) {
        // Unchanged: arp feeds the scheduler, which is already rate-limited,
        // so drain everything into s_arp with no admission cap.
        while (s_cmds.pop(cmd)) {
            if (cmd.type == NoteCmd::kNoteOn) {
                s_arp.note_on(cmd.pitch, cmd.velocity);
            } else {
                s_arp.note_off(cmd.pitch);
            }
        }
    } else {
        // Stage 8 onset A/B: start at most one direct note, then leave five
        // intervening blocks start-free (6-block / 8 ms start interval).
        // Peek before pop so deferred commands remain ordered and none drop.
        // A note-off already at the queue head bypasses the cooldown; a
        // note-off behind a deferred note-on remains ordered behind it.
        if (s_note_on_cooldown_blocks > 0) s_note_on_cooldown_blocks--;
        bool note_on_admitted = false;
        while (true) {
            NoteCmd next;
            if (s_cmds.peek(next) && next.type == NoteCmd::kNoteOn &&
                (note_on_admitted || s_note_on_cooldown_blocks > 0)) {
                break;  // leave this note-on and later commands queued
            }
            if (!s_cmds.pop(cmd)) break;
            if (cmd.type == NoteCmd::kNoteOn) {
                NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
                s_alloc.note_on(cmd.pitch, cmd.velocity, expr);
                note_on_admitted          = true;
                s_note_on_cooldown_blocks = kNoteOnStartIntervalBlocks;
            } else {
                s_alloc.note_off(cmd.pitch);
            }
        }
    }

    // Stage 5c: panic overrides any notes drained this block.
    if (s_panic.exchange(false, std::memory_order_relaxed)) {
        s_alloc.reset_all();
        s_note_on_cooldown_blocks = 0;
        s_arp.clear();
        s_arp_had_notes = false;
        s_arp_step      = 0;
        s_arp_phase     = 0.0;
        // ADR 0021: reset channel volume to unity so panic can never latch the session quiet.
        s_channel_vol.store(1.0f, std::memory_order_relaxed);
    }

    // 1a. Drain clock commands from the control thread and advance the clock
    //     once for this block (ADR 0010). Mirroring the NoteCmd drain above.
    //
    // Ordering: capture block_start BEFORE advance() so the scheduler window
    // [block_start, block_start+frames) correctly identifies events whose
    // sample_time falls within THIS block. advance() then moves free_pos_
    // to block_start+frames (the start of the NEXT block).
    //
    // block_start is in Clock::free_pos() units (the free-running monotonic
    // counter). The arp (Stage 4b-iii) and future sequencer schedule events in
    // free_pos() units so timing is independent of transport start/stop
    // (ADR 0019 free-running arp convention). s_sched dispatches in these units.
    uint64_t block_start;
    {
        ClockCmd cc;
        while (s_clock_cmds.pop(cc)) {
            switch (cc.type) {
                case ClockCmd::kSetBpm:
                    s_clock.set_bpm(cc.arg);
                    break;
                case ClockCmd::kStart:
                    s_clock.start();
                    break;
                case ClockCmd::kStop:
                    s_clock.stop();
                    break;
                case ClockCmd::kContinue:
                    s_clock.cont();
                    break;
                case ClockCmd::kTap:
                    s_clock.tap();
                    break;
            }
        }
        block_start = s_clock.free_pos();  // free-running position at block start
        (void)s_clock.advance((uint32_t)frames);
    }

    // 1b. Drain the scheduled-event ring into the Scheduler, then dispatch
    //     events due in this block (ADR 0010 / Stage 4a-ii).
    //
    //     Dispatch is block-granular: the sub-block offset is computed and
    //     passed to the callback, but the engine applies all events at the
    //     block boundary (true sub-block render-splitting is deferred per
    //     ADR 0010 — "a profile says it's needed").
    {
        ScheduledEvent sev;
        while (s_sched_in.pop(sev)) {
            s_sched.schedule(sev.sample_time, sev.cmd);
        }

        s_sched.dispatch_due(block_start, (uint32_t)frames, [](const NoteCmd& cmd, uint32_t offset) {
            // offset: sample position within the block (deferred — ADR 0010).
            (void)offset;  // sub-block splitting deferred per ADR 0010

            NoteExpression expr{0.0f, 0.0f, 0.0f, 1};
            if (cmd.type == NoteCmd::kNoteOn) {
                s_alloc.note_on(cmd.pitch, cmd.velocity, expr);
            } else {
                s_alloc.note_off(cmd.pitch);
            }
        });
    }

#ifdef SYNTH_PROFILE
    _cpu_t1 = platform_cycles_now();  // end of command/clock/scheduler drain
#endif

    // 2. Advance the param store smoothers and update targets from the ring.
    s_params.drain();

    // 2a. Stage 3d-i/ii: update play mode, portamento, and unison from the param store,
    //     then advance the glide ramp by one block. Done after drain() so we
    //     use the freshest smoothed values. block_time is exact for this block.
    // unison_count is hoisted out of the inner block so step 6 can use it for
    // equal-power gain compensation (1/√U) without a second param read.
    int unison_count;
    {
        int   play_mode = (int)s_params.get(ParamId::PLAY_MODE);
        float porto     = s_params.get(ParamId::PORTAMENTO_TIME);
        s_alloc.set_play_mode(static_cast<PlayMode>(play_mode));
        s_alloc.set_portamento_time(porto);
        // Stage 3d-ii: unison (UNISON_COUNT and UNISON_DETUNE are CURVE_STEPPED/LIN;
        // block-rate update here matches the same pattern as play_mode/portamento).
        unison_count        = (int)s_params.get(ParamId::UNISON_COUNT);
        float unison_detune = s_params.get(ParamId::UNISON_DETUNE);
        s_alloc.set_unison_count(unison_count);
        s_alloc.set_unison_detune(unison_detune);
        float block_time = (float)frames / s_sample_rate;
        s_alloc.advance_glide(block_time);
    }

    // 2b. Stage 4b-iii: arpeggiator engine — only when ARP_ON.
    //     Re-read arp_on after drain() for freshest smoothed value.
    arp_on = s_params.get(ParamId::ARP_ON) > 0.5f;
    if (arp_on) {
        // Keep arp config in sync with params (block-rate; negligible cost).
        s_arp.set_mode((ArpMode)(int)s_params.get(ParamId::ARP_MODE));
        s_arp.set_octaves((int)s_params.get(ParamId::ARP_OCTAVES));
        s_arp.set_latch(s_params.get(ParamId::ARP_LATCH) > 0.5f);

        // Derive step period from BPM-accurate clock and rate index.
        double spt         = s_clock.samples_per_tick();
        double step_period = (double)arp_rate_ticks((int)s_params.get(ParamId::ARP_RATE)) * spt;

        // First-note alignment: when the first note arrives, reset phase so
        // the step fires immediately rather than waiting for a stale phase.
        bool have = s_arp.has_notes();
        if (have && !s_arp_had_notes) {
            s_arp_phase = 0.0;
            s_arp_step  = 0;
        }
        s_arp_had_notes = have;

        if (have) {
            ArpPhaseResult r = arp_advance_phase(&s_arp_phase, (uint32_t)frames, step_period);
            if (r.fire) {
                ArpNote a = s_arp.next();
                if (a.valid) {
                    float gate  = s_params.get(ParamId::ARP_GATE);
                    float swing = s_params.get(ParamId::ARP_SWING);

                    // Swing: delay odd steps by swing_fraction * 0.5 * step_period.
                    uint64_t on = block_start + (uint64_t)r.offset +
                                  ((s_arp_step & 1u) ? (uint64_t)(swing * 0.5 * step_period) : 0u);

                    // Gate: fraction of the step period. Force at least 1 sample length.
                    uint64_t gate_len = (uint64_t)(gate * step_period);
                    if (gate_len == 0) gate_len = 1;
                    uint64_t off = on + gate_len;

                    s_sched.schedule(on, NoteCmd{NoteCmd::kNoteOn, a.pitch, a.velocity});
                    s_sched.schedule(off, NoteCmd{NoteCmd::kNoteOff, a.pitch, 0});

                    s_arp_step++;
                }
            }
        }
    } else {
        // ARP_ON just turned off (or was never on): reset arp state so
        // toggling back on starts fresh (no stale chord or step phase).
        if (s_arp_had_notes || s_arp_phase != 0.0 || s_arp_step != 0) {
            s_arp.clear();
            s_arp_had_notes = false;
            s_arp_step      = 0;
            s_arp_phase     = 0.0;
        }
    }

    // 3. Push only params that changed this block to all voices (block-rate).
    //    Steady state pushes nothing; a knob sweep pushes only that param.
    //    Idle voices receive the push too (cheap now) so a newly triggered
    //    voice always holds current values. Voices ignore ids they don't handle.
    //    ADR 0018: LFO1/2 rate+shape also configure the shared engine LFOs here.
    const VoiceSlot* slots = s_alloc.slots();
    {
        int nch = s_params.changed_count();
        for (int c = 0; c < nch; c++) {
            uint16_t id  = s_params.changed_id(c);
            float    val = s_params.get(id);
            for (int v = 0; v < kNumVoices; v++) {
                slots[v].voice->set_param(id, val);
            }
            // Configure shared LFOs for rate/shape changes, and drive the clock from the
            // param table (Stage 4a-iii: CLOCK_BPM is the UI/preset home for tempo;
            // engine_set_bpm() via ClockCmd remains valid for tap-tempo and future
            // MIDI-clock write-back — both paths converge on s_clock.set_bpm()).
            switch (id) {
                case ParamId::CLOCK_BPM:
                    s_clock.set_bpm(val);
                    break;
                case ParamId::LFO1_RATE:
                    s_lfo1.set_rate(val);
                    break;
                case ParamId::LFO1_SHAPE:
                    s_lfo1.set_waveform((dsp::LfoWave)(int)val);
                    break;
                case ParamId::LFO2_RATE:
                    s_lfo2.set_rate(val);
                    break;
                case ParamId::LFO2_SHAPE:
                    s_lfo2.set_waveform((dsp::LfoWave)(int)val);
                    break;
                default:
                    break;
            }
        }
    }

    // 3a. ADR 0018: advance shared LFOs once for the whole block (unconditional —
    //     free-running regardless of voice activity) and inject into every voice.
    //     Stage 5c: also load channel-wide MIDI expression atomics once and inject.
    {
        float l1 = s_lfo1.process_block((uint32_t)frames);
        float l2 = s_lfo2.process_block((uint32_t)frames);
        float mw = s_mod_wheel.load(std::memory_order_relaxed);
        float pb = s_pitch_bend.load(std::memory_order_relaxed);
        float at = s_aftertouch.load(std::memory_order_relaxed);
        for (int v = 0; v < kNumVoices; v++) {
            slots[v].voice->set_lfo_inputs(l1, l2);
            slots[v].voice->set_expression(mw, pb, at);
        }
    }

    // 4. Update chorus (non-per-voice) from the param store.
    // CHORUS_MODE: 0=off, 1=Chorus I (slow/lush), 2=Chorus II (fast/wide).
    // Mode I/II differ in rate preset; depth and delay are user-tweakable on top.
    int chorus_mode = (int)s_params.get(ParamId::CHORUS_MODE);
    if (chorus_mode > 0) {
        // Apply mode-specific rate preset, then user rate on top (additive).
        // Mode I ≈ 0.5 Hz base; Mode II ≈ 1.0 Hz base (classic Juno behaviour).
        float mode_rate = (chorus_mode == 2) ? 1.0f : 0.5f;
        s_chorus.SetLfoFreq(mode_rate + s_params.get(ParamId::CHORUS_RATE) * 0.5f);
        s_chorus.SetLfoDepth(s_params.get(ParamId::CHORUS_DEPTH));
        s_chorus.SetDelay(s_params.get(ParamId::CHORUS_DELAY));
    }

    // 5. Sum all active voices into the mono bus.
#ifdef SYNTH_PROFILE
    _cpu_t2 = platform_cycles_now();  // start of voice-sum
    _ir_t2  = platform_instret_now();
#endif
    memset(s_mono, 0, frames * sizeof(float));
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) {
#ifdef SYNTH_PROFILE
            uint64_t _v0 = platform_cycles_now();
            slots[v].voice->render(s_mono, frames);
            uint32_t _vd = (uint32_t)(platform_cycles_now() - _v0);
            if (_vd > _vmax_cyc) {
                _vmax_cyc = _vd;
                _vmax_idx = (uint32_t)v;
            }
            _active_n++;
#else
            slots[v].voice->render(s_mono, frames);
#endif
        }
    }
#ifdef SYNTH_PROFILE
    _cpu_t3 = platform_cycles_now();  // end of voice-sum / start of master chain
    _ir_t3  = platform_instret_now();
#endif

    // 6. Mono bus → stereo chorus → master gain → peak limiter → soft-clip → output
    //    (ADR 0016 / ADR 0021).
    // Gain = MASTER_GAIN (manual trim) × channel_vol (CC7 attenuation, ADR 0021) × 1/√U.
    // channel_vol is 0..1 (unity at 1.0, square-law taper from CC7 in the router).
    // Apply equal-power unison compensation (1/√U) so U voices stacked on one note
    // do not sum louder than a single voice. Uses unison_count from step 2a above.
    // Limiter (ADR 0021): feed-forward stereo-linked, THRESH=0.92, 1 ms attack, 120 ms release.
    // Provides smooth gain reduction before soft_clip (the retained transient safety net, ADR 0016).
    float gain =
        s_params.get(ParamId::MASTER_GAIN) * s_channel_vol.load(std::memory_order_relaxed) * unison_gain(unison_count);
    for (size_t i = 0; i < frames; i++) {
        if (chorus_mode > 0) {
            s_chorus.Process(s_mono[i]);
            float lg = s_dc_l.process(s_chorus.GetLeft() * gain);
            float rg = s_dc_r.process(s_chorus.GetRight() * gain);
            float gr = s_limiter.process(fmaxf(fabsf(lg), fabsf(rg)));
            left[i]  = soft_clip(lg * gr);
            right[i] = soft_clip(rg * gr);
#ifdef SYNTH_PROFILE
            {
                float mono_abs = fabsf(s_mono[i]);
                float postgain = fmaxf(fabsf(lg), fabsf(rg));
                float out_abs  = fabsf(left[i]);
                if (mono_abs > s_pk_mono) s_pk_mono = mono_abs;
                if (postgain > s_pk_postgain) s_pk_postgain = postgain;
                if (gr < s_min_gr) s_min_gr = gr;
                if (out_abs > s_pk_out) s_pk_out = out_abs;
                tap_capture(left[i], right[i], postgain);
            }
#endif
        } else {
            // Chorus off: output mono signal to both channels (no stereo spread).
            // Single DC blocker (s_dc_l) drives both channels.
            float m  = s_dc_l.process(s_mono[i] * gain);
            float gr = s_limiter.process(fabsf(m));
            float v  = soft_clip(m * gr);
            left[i]  = v;
            right[i] = v;
#ifdef SYNTH_PROFILE
            {
                float mono_abs = fabsf(s_mono[i]);
                float out_abs  = fabsf(v);
                if (mono_abs > s_pk_mono) s_pk_mono = mono_abs;
                if (fabsf(m) > s_pk_postgain) s_pk_postgain = fabsf(m);
                if (gr < s_min_gr) s_min_gr = gr;
                if (out_abs > s_pk_out) s_pk_out = out_abs;
                tap_capture(v, v, fabsf(m));
            }
#endif
        }
    }

    // ADR 0024: publish the completed final master exactly once per render
    // block, after both chorus branches converge. Disabled capture is one
    // relaxed atomic load; oversized future-backend blocks fail closed.
    (void)record_ring_publish(left, right, frames);

#ifdef SYNTH_PROFILE
    // Accumulate this block's per-region cycle costs (avg via sum, plus max).
    // The four regions tile the whole block: drain=[t0,t1] setup=[t1,t2]
    // voices=[t2,t3] master=[t3,end].
    uint64_t _cpu_end = platform_cycles_now();
    uint32_t d_drain  = (uint32_t)(_cpu_t1 - _cpu_t0);
    uint32_t d_setup  = (uint32_t)(_cpu_t2 - _cpu_t1);
    uint32_t d_voices = (uint32_t)(_cpu_t3 - _cpu_t2);
    uint32_t d_master = (uint32_t)(_cpu_end - _cpu_t3);
    s_cpu_drain      += d_drain;
    s_cpu_setup      += d_setup;
    s_cpu_voices     += d_voices;
    s_cpu_master     += d_master;
    if (d_drain > s_cpu_drain_max) s_cpu_drain_max = d_drain;
    if (d_setup > s_cpu_setup_max) s_cpu_setup_max = d_setup;
    if (d_voices > s_cpu_voices_max) s_cpu_voices_max = d_voices;
    if (d_master > s_cpu_master_max) s_cpu_master_max = d_master;
    s_cpu_blocks++;

    // Stage 8 diag: worst-block snapshot, keyed on the voices region (already
    // localized as the culprit region). Freeze the full breakdown of the
    // single worst block in this read-window so the mechanism (stall vs
    // preemption vs compute vs hot-voice) can be read off in one device run.
    uint32_t d_voices_instret = (uint32_t)(_ir_t3 - _ir_t2);
    if (d_voices > s_worst_voices_cyc) {
        s_worst_voices_cyc     = d_voices;
        s_worst_voices_instret = d_voices_instret;
        s_worst_active         = _active_n;
        s_worst_vmax_cyc       = _vmax_cyc;
        s_worst_vmax_idx       = _vmax_idx;
        s_worst_drain_cyc      = d_drain;
        s_worst_setup_cyc      = d_setup;
        s_worst_master_cyc     = d_master;
    }
#endif
}

// Control thread (core 0): enqueue for the audio thread to apply.
void engine_note_on(uint8_t pitch, uint8_t velocity) {
    NoteCmd c{NoteCmd::kNoteOn, pitch, velocity};
    s_cmds.push(c);
}

void engine_note_off(uint8_t pitch) {
    NoteCmd c{NoteCmd::kNoteOff, pitch, 0};
    s_cmds.push(c);
}

void engine_set_param(uint16_t id, float value) {
    s_params.param_set(id, value);
}

void engine_set_param_norm(uint16_t id, float norm) {
    s_params.param_set_norm(id, norm);
}

int engine_active_voices(void) {
    const VoiceSlot* slots = s_alloc.slots();
    int              count = 0;
    for (int v = 0; v < kNumVoices; v++) {
        if (slots[v].voice->is_active()) count++;
    }
    return count;
}

void engine_set_routings(const Routing* routings, int count) {
    // Build a ModMatrix from the supplied routings, then push it to every voice.
    // Called from the control thread (preset load); not in the audio path.
    ModMatrix mat;
    mat.clear();
    if (routings && count > 0) {
        int slots_to_set = (count < kMaxRoutes) ? count : kMaxRoutes;
        for (int i = 0; i < slots_to_set; i++) {
            mat.set_route(i, routings[i]);
        }
    }
    const VoiceSlot* slots = s_alloc.slots();
    for (int v = 0; v < kNumVoices; v++) {
        slots[v].voice->set_mod_matrix(mat);
    }
}

float engine_get_param(uint16_t id) {
    // Control-thread read of the smoothed param value. May lag the audio thread
    // by up to one block (~1.3 ms at 48k/64) — fine for display use only.
    return s_params.get(id);
}

// --- Clock API (ADR 0010) ---------------------------------------------------
// Control-thread setters push a ClockCmd into the lock-free ring; the audio
// thread drains and applies them at the top of each block.

void engine_set_bpm(float bpm) {
    ClockCmd c{ClockCmd::kSetBpm, bpm};
    s_clock_cmds.push(c);
}

void engine_transport_start() {
    ClockCmd c{ClockCmd::kStart, 0.0f};
    s_clock_cmds.push(c);
}

void engine_transport_stop() {
    ClockCmd c{ClockCmd::kStop, 0.0f};
    s_clock_cmds.push(c);
}

void engine_transport_continue() {
    ClockCmd c{ClockCmd::kContinue, 0.0f};
    s_clock_cmds.push(c);
}

void engine_tap_tempo() {
    ClockCmd c{ClockCmd::kTap, 0.0f};
    s_clock_cmds.push(c);
}

// --- MIDI expression API (Stage 5c) -----------------------------------------
// Control-thread setters; relaxed atomics, latest-value-wins.

void engine_set_pitch_bend(float norm_bipolar) {
    s_pitch_bend.store(norm_bipolar, std::memory_order_relaxed);
}
void engine_set_mod_wheel(float norm) {
    s_mod_wheel.store(norm, std::memory_order_relaxed);
}
void engine_set_aftertouch(float norm) {
    s_aftertouch.store(norm, std::memory_order_relaxed);
}
// ADR 0021: CC7 channel volume — attenuation-only [0..1], square-law taper applied
// by the caller (midi_router). Reset to 1.0 on init/panic.
void engine_set_channel_volume(float vol) {
    s_channel_vol.store(vol, std::memory_order_relaxed);
}

void engine_all_notes_off(void) {
    s_panic.store(true, std::memory_order_relaxed);
}

uint16_t engine_cc_to_param(uint8_t cc) {
    if (cc == 0xFF) return 0;
    for (int i = 0; i < kJunoParamCount; i++) {
        if (JUNO_PARAM_TABLE[i].midi_cc == cc) return JUNO_PARAM_TABLE[i].id;
    }
    return 0;
}

// --- Event scheduler API (ADR 0010, Stage 4a-ii) ----------------------------
// Lock-free: control thread pushes a ScheduledEvent into s_sched_in; the audio
// thread drains and dispatches it at block_start when sample_time is due.
// sample_time must be in Clock::free_pos() units (monotonic; unaffected by
// transport start/stop — same time base as the arp, ADR 0019).

void engine_schedule_note(uint64_t sample_time, uint8_t pitch, uint8_t velocity, int on) {
    NoteCmd cmd;
    cmd.type     = on ? NoteCmd::kNoteOn : NoteCmd::kNoteOff;
    cmd.pitch    = pitch;
    cmd.velocity = velocity;
    ScheduledEvent ev;
    ev.sample_time = sample_time;
    ev.cmd         = cmd;
    s_sched_in.push(ev);
}

// Read-only helpers — control-thread safe; may read a frame-stale value.
// Use for display only, not for audio logic.

int engine_clock_running(void) {
    return s_clock.running() ? 1 : 0;
}

uint64_t engine_clock_tick_pos(void) {
    return s_clock.tick_pos();
}

float engine_clock_bpm(void) {
    return s_clock.bpm();
}

// --- Signal-magnitude probe (diagnostic, SYNTH_PROFILE only) ----------------
// Snapshot-and-reset the four master-chain peak accumulators.  Called from the
// control thread (~1 s cadence); single-writer audio / single-reader control —
// the benign race is acceptable for a diagnostic (ADR/work-order).
// Returns zeros when SYNTH_PROFILE is off (shipping image unchanged).
void engine_profile_read(float* pk_mono, float* pk_postgain, float* min_gr, float* pk_out) {
#ifdef SYNTH_PROFILE
    *pk_mono      = s_pk_mono;
    *pk_postgain  = s_pk_postgain;
    *min_gr       = s_min_gr;
    *pk_out       = s_pk_out;
    s_pk_mono     = 0.0f;
    s_pk_postgain = 0.0f;
    s_min_gr      = 1.0f;
    s_pk_out      = 0.0f;
#else
    *pk_mono     = 0.0f;
    *pk_postgain = 0.0f;
    *min_gr      = 0.0f;
    *pk_out      = 0.0f;
#endif
}

// --- Per-region CPU sub-timers (diagnostic, SYNTH_PROFILE only) --------------
// Snapshot-and-reset the four region accumulators as PER-BLOCK AVG (total/blocks)
// and MAX cycles, so the caller can print them in the same us units as the
// whole-block audio profiler. The MAX fields are what expose the smash-crackle
// spike (one rare block/window). Zeros when the probe saw no blocks or
// SYNTH_PROFILE is off (shipping image unchanged).
void engine_profile_read_cpu(EngineCpuProfile* out) {
#ifdef SYNTH_PROFILE
    uint32_t blocks = s_cpu_blocks;
    if (blocks == 0) blocks = 1;
    out->drain_avg   = (uint32_t)(s_cpu_drain / blocks);
    out->setup_avg   = (uint32_t)(s_cpu_setup / blocks);
    out->voices_avg  = (uint32_t)(s_cpu_voices / blocks);
    out->master_avg  = (uint32_t)(s_cpu_master / blocks);
    out->drain_max   = s_cpu_drain_max;
    out->setup_max   = s_cpu_setup_max;
    out->voices_max  = s_cpu_voices_max;
    out->master_max  = s_cpu_master_max;
    s_cpu_drain      = 0;
    s_cpu_setup      = 0;
    s_cpu_voices     = 0;
    s_cpu_master     = 0;
    s_cpu_drain_max  = 0;
    s_cpu_setup_max  = 0;
    s_cpu_voices_max = 0;
    s_cpu_master_max = 0;
    s_cpu_blocks     = 0;

    // Stage 8 diag: hand back the worst-block snapshot, then reset the
    // selection key so the NEXT read-window finds its own worst block.
    out->worst_voices_cyc     = s_worst_voices_cyc;
    out->worst_voices_instret = s_worst_voices_instret;
    out->worst_active         = s_worst_active;
    out->worst_vmax_cyc       = s_worst_vmax_cyc;
    out->worst_vmax_idx       = s_worst_vmax_idx;
    out->worst_drain_cyc      = s_worst_drain_cyc;
    out->worst_setup_cyc      = s_worst_setup_cyc;
    out->worst_master_cyc     = s_worst_master_cyc;
    s_worst_voices_cyc        = 0;
    s_worst_voices_instret    = 0;
    s_worst_active            = 0;
    s_worst_vmax_cyc          = 0;
    s_worst_vmax_idx          = 0;
    s_worst_drain_cyc         = 0;
    s_worst_setup_cyc         = 0;
    s_worst_master_cyc        = 0;
#else
    out->drain_avg = out->setup_avg = out->voices_avg = out->master_avg = 0;
    out->drain_max = out->setup_max = out->voices_max = out->master_max = 0;
    out->worst_voices_cyc = out->worst_voices_instret = out->worst_active = 0;
    out->worst_vmax_cyc = out->worst_vmax_idx = 0;
    out->worst_drain_cyc = out->worst_setup_cyc = out->worst_master_cyc = 0;
#endif
}

// --- Audio RAM tap reader (SYNTH_PROFILE only; crackle forensics) ----------
// See the tap_capture()/statics block above for the writer and synth.h for
// the full dump-order contract.

bool engine_tap_frozen(void) {
#ifdef SYNTH_PROFILE
    return s_tap_frozen.load(std::memory_order_acquire);
#else
    return false;
#endif
}

const int16_t* engine_tap_data(uint32_t* out_frames, uint32_t* out_trig_frame, uint32_t* out_start_offset) {
#ifdef SYNTH_PROFILE
    // The acquire load in engine_tap_frozen() (called by the caller before
    // this) is what makes it safe to read the plain (non-atomic)
    // s_tap_write_pos/s_tap_trig_pos here: they were last written before the
    // release store that set s_tap_frozen, and the writer never touches them
    // again once frozen.
    *out_frames        = kTapFrames;
    uint32_t start     = s_tap_write_pos;  // oldest valid frame once frozen (see header contract)
    *out_start_offset  = start;
    uint32_t trig_phys = (s_tap_trig_pos >= 0) ? (uint32_t)s_tap_trig_pos : 0u;  // trigger always latches before freeze
    *out_trig_frame    = (trig_phys + kTapFrames - start) & kTapFramesMask;      // physical -> logical (0 = oldest)
    return s_tap_ring;
#else
    *out_frames       = 0;
    *out_trig_frame   = 0;
    *out_start_offset = 0;
    return NULL;
#endif
}

// Control thread: request an immediate manual freeze / a re-arm after a
// dump. Single-flag requests only -- the audio thread (tap_capture()) does
// all the actual ring/position mutation, preserving the single-writer
// contract. See synth.h for the full contract.

void engine_tap_freeze_now(void) {
#ifdef SYNTH_PROFILE
    s_tap_freeze_req.store(true, std::memory_order_relaxed);
#endif
}

void engine_tap_rearm(void) {
#ifdef SYNTH_PROFILE
    s_tap_rearm_req.store(true, std::memory_order_relaxed);
#endif
}
