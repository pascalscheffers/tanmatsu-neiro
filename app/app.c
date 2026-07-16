// app.c — portable init + main loop.
//
// One implementation, two targets. Brings up the platform, configures the
// engine, starts the audio sink, then loops: drain input, redraw the UI,
// present, pace. Audio runs independently on the HAL's audio thread.
//
// Rendering runs on a separate lower-priority task (device) or inline on the
// main thread (host, where SDL requires main-thread rendering). The control
// loop is never blocked by the ~1.15 MB display blit, so the next note-on
// arrives within ~1 ms rather than waiting behind a full frame.
#include "app.h"
#include "control/keyboard.h"
#include "control/midi_router.h"
#include "platform.h"
#include "synth.h"
#include "ui.h"
#include "ui_dirty.h"
#ifdef SYNTH_BENCH
#include "bench.h"
#endif
#ifdef SYNTH_BENCH_INTERACTIVE
#include <stdio.h>
#include "pax_fonts.h"
#include "pax_text.h"
#endif
#ifdef SYNTH_PROFILE
#include <stdio.h>
#endif

// Audio format (spec 02): 64-frame blocks at 48 kHz.
#define SAMPLE_RATE 48000u
#define BLOCK_SIZE  64u

// Control-loop poll interval (~1 ms) and render interval (~16 ms / ~60 Hz).
// To enable per-render draw/present profiling: cmake -DSYNTH_PROFILE=ON
// (or, for the host build: cmake -B build-host -DSYNTH_PROFILE=ON)
// For the device build pass EXTRA_CFLAGS=-DSYNTH_PROFILE via idf.py or set it
// in your local CMakeLists component override:  make build EXTRA=-DSYNTH_PROFILE
// (EXTRA is not wired in the Makefile; see IDF_PARAMS in Makefile).
#define POLL_MS   1u
#define RENDER_MS 16u

// Voice-meter debounce (Stage 8 diag, 2026-07-07). The active-voice count churns
// rapidly while smashing a chord; blitting the status band on every change starves
// core-1 audio's PSRAM access -> block-budget overrun -> crackle (device A/B: with
// the meter blit suppressed, over=0/max~633us; with it live, over=15/max=2743us).
// Only commit a meter redraw once the count has held steady this long, so a smash
// produces no mid-play blits and the meter still settles imperceptibly at rest.
#define VOICE_METER_STABLE_MS 100u

#ifdef SYNTH_BENCH_INTERACTIVE
// Device bench is interactive: the badge console (USB-Serial-JTAG) and badgelink
// (USB-OTG) can't share the USB-C, so an AppFS-launched app starts with its
// console detached. We draw a prompt and wait for a key, giving time to switch
// the badge to debug/USB mode and attach `make sniff`/monitor before any output.
static void bench_screen(const char* l1, const char* l2) {
    pax_buf_t* fb = platform_framebuffer();
    if (fb == NULL) return;
    pax_background(fb, 0xFF101018);
    pax_draw_text(fb, 0xFFFFFFFF, pax_font_sky_mono, 24, 12, 12, "Tanmatsu Synth - CPU bench");
    pax_draw_text(fb, 0xFF30C0FF, pax_font_sky_mono, 20, 12, 56, l1);
    if (l2) pax_draw_text(fb, 0xFF7A7A8A, pax_font_sky_mono, 16, 12, 88, l2);
    platform_present(0, (int)pax_buf_get_height(fb));
}

// Block until the user presses a key on the badge (or asks to quit).
static void bench_wait_for_key(void) {
    bench_screen("Press any key to start", "Attach the console first (debug USB mode).");
    for (;;) {
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) platform_exit_to_launcher();
            if (ev.type == PLATFORM_EV_KEY && ev.pressed) return;
        }
        platform_sleep_ms(16);
    }
}
#endif

#ifdef SYNTH_PROFILE
// Audio RAM tap dump (crackle forensics; see engine/synth.h and
// specs/MEMORY.md 2026-07-10). One-time, control-thread-only work: not
// real-time, so a plain bitwise CRC-32 and table-free base64 are fine here
// even though the same style would be forbidden in the audio path.

// Standard CRC-32 (IEEE 802.3 / zlib) update, one byte at a time. Matches
// Python's zlib.crc32/binascii.crc32 so tools/tap2wav.py can verify without
// any custom polynomial code on the host side.
static uint32_t tap_crc32_update(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc;
}

// Standard base64 (RFC 4648, '=' padding). `out` must hold at least
// 4*ceil(n/3)+1 bytes; n <= 48 in this file's usage (63 is comfortably safe).
static void tap_base64_encode(const uint8_t* in, uint32_t n, char* out) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t          oi    = 0, i;
    for (i = 0; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[oi++]  = tbl[(v >> 18) & 0x3F];
        out[oi++]  = tbl[(v >> 12) & 0x3F];
        out[oi++]  = tbl[(v >> 6) & 0x3F];
        out[oi++]  = tbl[v & 0x3F];
    }
    uint32_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[oi++]  = tbl[(v >> 18) & 0x3F];
        out[oi++]  = tbl[(v >> 12) & 0x3F];
        out[oi++]  = '=';
        out[oi++]  = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[oi++]  = tbl[(v >> 18) & 0x3F];
        out[oi++]  = tbl[(v >> 12) & 0x3F];
        out[oi++]  = tbl[(v >> 6) & 0x3F];
        out[oi++]  = '=';
    }
    out[oi] = '\0';
}

// Print the tap ring exactly once (guarded by a static bool at the call
// site), as [TAP] hdr / d.../ end lines. Unrolls the two physical spans
// (oldest->newest, per the header contract) via modular byte-offset math --
// no second ring-sized buffer needed. 48 raw bytes per base64 line, a
// platform_sleep_ms(2) every 16 lines so the dump does not starve other
// control-thread work (16, not 32: the USB-Serial-JTAG console was dropping
// ~6% of lines at 32).
//
// Each data line is stamped with its byte OFFSET into the logical buffer
// ([TAP] d <off> <b64>). tap2wav.py places each chunk at its offset, so a
// serial-dropped line leaves a zero gap at the correct position instead of
// shifting every later sample earlier -- alignment survives drops, which is
// what a sample-accurate diff against a line-out recording requires.
static void tap_dump(void) {
    uint32_t       frames, trig_frame, start_offset;
    const int16_t* ring = engine_tap_data(&frames, &trig_frame, &start_offset);
    if (ring == NULL) return;

    printf("[TAP] hdr sr=48000 ch=2 fmt=s16le frames=%u trig_frame=%u\n", (unsigned)frames, (unsigned)trig_frame);

    const uint8_t* bytes       = (const uint8_t*)ring;
    uint32_t       total_bytes = frames * 2u * (uint32_t)sizeof(int16_t);  // 2 ch, interleaved
    uint32_t       start_bytes = start_offset * 2u * (uint32_t)sizeof(int16_t);
    uint32_t       crc         = 0xFFFFFFFFu;
    uint32_t       lines_out   = 0;
    for (uint32_t off = 0; off < total_bytes; off += 48u) {
        uint8_t  chunk[48];
        uint32_t n = (total_bytes - off < 48u) ? (total_bytes - off) : 48u;
        for (uint32_t k = 0; k < n; k++) {
            uint32_t physical = (start_bytes + off + k) % total_bytes;  // unroll: two spans, one modulo
            chunk[k]          = bytes[physical];
            crc               = tap_crc32_update(crc, chunk[k]);
        }
        char b64[65];
        tap_base64_encode(chunk, n, b64);
        printf("[TAP] d %u %s\n", (unsigned)off, b64);
        lines_out++;
        if ((lines_out % 16u) == 0u) platform_sleep_ms(2u);
    }
    crc ^= 0xFFFFFFFFu;
    printf("[TAP] end crc32=%08x\n", (unsigned)crc);
}
#endif

// ---------------------------------------------------------------------------
// Render callback — called by the platform render task (device) or inline
// from the control loop (host). Reads UIState written by the control loop and
// repaints only when change_seq has advanced. Owns the framebuffer + present
// exclusively — the control loop must never call ui_draw or platform_present.
// ---------------------------------------------------------------------------
static uint32_t s_last_drawn_seq = 0;

static void render_cb(void* arg) {
    UIState*   s  = (UIState*)arg;
    pax_buf_t* fb = platform_framebuffer();
    if (!fb) return;
#ifdef SYNTH_QUIET_DISPLAY
    // True bus-quiet baseline: paint frame 1, then on the NEXT tick tear down the
    // DPI panel (stops the continuous PSRAM scanout DMA). The one-tick delay lets
    // frame 1's blit DMA finish before the panel is deleted. Afterwards
    // platform_framebuffer() returns NULL, so this returns at the check above.
    static int s_quiet_frames = 0;
    if (s_quiet_frames == 1) {
        platform_display_stop();
        s_quiet_frames = 2;
        return;
    }
#endif
#ifdef SYNTH_FREEZE_DISPLAY
    // Freeze: let the first frame paint normally, then never repaint again.
    // This removes display-blit / memory-bus pressure so an audio crackle test
    // with PROFILE=1 isolates compute cost from core-0 contention.
    static bool painted_once = false;
    if (painted_once) return;
#endif
    uint32_t seq = s->change_seq;         // volatile read
    if (seq == s_last_drawn_seq) return;  // nothing changed — skip draw + blit
#ifdef SYNTH_PROFILE
    static uint64_t prof_draw_sum = 0, prof_draw_min = UINT64_MAX, prof_draw_max = 0;
    static uint64_t prof_pres_sum = 0, prof_pres_min = UINT64_MAX, prof_pres_max = 0;
    static int      prof_frames = 0;
    uint64_t        t0          = platform_cycles_now();
#endif
    ui_draw(fb, platform_millis(), s);
#ifdef SYNTH_PROFILE
    uint64_t t1 = platform_cycles_now();
#endif
    // ADR 0022: ui_draw always fully repaints the framebuffer, so it is
    // always correct; only the blit is narrowed. The failure mode of the
    // whole dirty-band scheme is therefore "present more," never "present
    // stale": an empty band here (nothing invalidated since the last take)
    // falls back to a full present. The ui_dirty_take/ui_invalidate race that
    // used to risk a silently dropped union is closed with std::atomic (ADR
    // 0023) — a union either lands fully before or fully after a concurrent
    // take, never lost in between.
    // The very first present must be full-screen: the panel/SDL texture starts
    // with undefined contents, and the frame-1 status reconciliation (voices +
    // octave, above) only invalidates the narrow status band — so an incidental
    // partial band would leave the freshly-drawn content area never blitted.
    // s_last_drawn_seq is still 0 only on this first paint (change_seq starts at
    // 1); ui_dirty_take still runs first to clear the pending band either way.
    int  py0, py1;
    bool have_band = ui_dirty_take(&py0, &py1);
    if (!have_band || s_last_drawn_seq == 0) {
        py0 = 0;
        py1 = (int)pax_buf_get_height(fb);
    }
    platform_present(py0, py1);
#ifdef SYNTH_PROFILE
    uint64_t t2       = platform_cycles_now();
    uint64_t cyc_draw = t1 - t0;
    uint64_t cyc_pres = t2 - t1;
    prof_draw_sum    += cyc_draw;
    prof_pres_sum    += cyc_pres;
    if (cyc_draw < prof_draw_min) prof_draw_min = cyc_draw;
    if (cyc_draw > prof_draw_max) prof_draw_max = cyc_draw;
    if (cyc_pres < prof_pres_min) prof_pres_min = cyc_pres;
    if (cyc_pres > prof_pres_max) prof_pres_max = cyc_pres;
    prof_frames++;
    if (prof_frames >= 120) {
        uint32_t hz  = platform_cycles_per_sec();
        uint32_t div = hz / 1000000u;
        if (div == 0) div = 1;
        printf("[PROFILE] draw  avg=%u min=%u max=%u us\n", (unsigned)(prof_draw_sum / (uint64_t)prof_frames / div),
               (unsigned)(prof_draw_min / div), (unsigned)(prof_draw_max / div));
        printf("[PROFILE] pres  avg=%u min=%u max=%u us\n", (unsigned)(prof_pres_sum / (uint64_t)prof_frames / div),
               (unsigned)(prof_pres_min / div), (unsigned)(prof_pres_max / div));
        prof_draw_sum = prof_pres_sum = 0;
        prof_draw_min = prof_pres_min = UINT64_MAX;
        prof_draw_max = prof_pres_max = 0;
        prof_frames                   = 0;
    }
#endif
    s_last_drawn_seq = seq;
#ifdef SYNTH_FREEZE_DISPLAY
    painted_once = true;
#endif
#ifdef SYNTH_QUIET_DISPLAY
    if (s_quiet_frames == 0) s_quiet_frames = 1;  // frame 1 shown; tear down next tick
#endif
}

void app_run(void) {
    if (!platform_init()) {
        return;
    }

#ifdef SYNTH_BENCH
    // Profiling mode (Stage 0.5): run the kernel table + load ramp, then stop.
    // Never enters the normal UI loop. The bench binary is built separately
    // (make bench / BENCH=1); the shipping image never defines SYNTH_BENCH.
#ifdef SYNTH_BENCH_INTERACTIVE
    bench_wait_for_key();
    bench_screen("Running...", "Watch the console for the result table.");
#endif
    bench_run(SAMPLE_RATE, BLOCK_SIZE);
#ifdef SYNTH_BENCH_INTERACTIVE
    bench_screen("Done.", "Returning to launcher...");
    fflush(stdout);
    platform_sleep_ms(2500);  // let the final console lines drain before reboot
    platform_exit_to_launcher();
#endif
    return;
#endif

    keyboard_init();
    midi_router_init();
    synth_init(SAMPLE_RATE, BLOCK_SIZE);

    // Initialise UI state: computes normalised defaults from the param table,
    // loads the boot preset, sets preset_name to "INIT".
    UIState ui_state;
    ui_state_init(&ui_state);

    const platform_audio_config_t audio_cfg = {
        .sample_rate = SAMPLE_RATE,
        .block_size  = BLOCK_SIZE,
    };
    platform_audio_start(&audio_cfg, synth_render, NULL);

    // Start a dedicated render task on device (core 0, RENDER_PRIO 2); on host
    // SDL requires main-thread rendering so this returns false and we render
    // inline in the control loop below.
    bool     has_render_task = platform_render_task_start(render_cb, &ui_state, RENDER_MS);
    bool     running         = true;
    uint64_t next_ctrl       = 0;
#ifdef SYNTH_PROFILE
    uint64_t next_prof = 0;
#endif

    // SINGLE-PRODUCER INVARIANT: all engine_note_on/off calls happen on this one
    // task (via keyboard_handle_event and midi_router_poll). The s_cmds SPSC ring
    // in engine/command_queue.h is single-producer by design — never move keyboard
    // or MIDI dispatch to a second task without redesigning that ring.
    while (running) {
        // --- Input phase (~1 ms cadence) ---
        // Drain the full event queue before sleeping so no event waits an extra
        // POLL_MS tick.  All state writes happen here; change_seq is bumped
        // AFTER the state is committed so the render task sees a consistent snapshot.
        platform_event_t ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PLATFORM_EV_QUIT) running = false;
#ifdef SYNTH_PROFILE
            // Manual on-demand tap freeze (crackle forensics, 2026-07-16): SPACE
            // is unused by musical typing (control/keyboard.c) and the UI, so it's
            // free to repurpose here. Diagnostic-only -- intercepted before
            // keyboard/UI dispatch so it never also triggers musical/UI behavior.
            if (ev.type == PLATFORM_EV_KEY && ev.pressed && ev.key == 32 /* SPACE */) {
                engine_tap_freeze_now();
                printf("[TAP] freeze requested\n");
                continue;
            }
#endif
            keyboard_handle_event(&ev);
            int  prev_page     = ui_state.page;
            bool prev_keyguide = ui_state.show_keyguide;
            if (ui_handle_event(&ui_state, &ev)) {
                ui_state.change_seq++;
                if (ui_state.page != prev_page || ui_state.show_keyguide != prev_keyguide) {
                    ui_invalidate_all();
                } else {
                    int a, b;
                    ui_band_content(&a, &b);
                    ui_invalidate(a, b);
                }
            }
        }
        midi_router_poll();
        {
            uint16_t fid;
            float    fnorm;
            if (midi_router_take_param_focus(&fid, &fnorm)) {
                int prev_page = ui_state.page;
                if (ui_focus_param(&ui_state, fid, fnorm)) {
                    ui_state.change_seq++;
                    if (ui_state.page != prev_page) {
                        ui_invalidate_all();
                    } else {
                        int a, b;
                        ui_band_content(&a, &b);
                        ui_invalidate(a, b);
                    }
                }
            }
        }

        // --- Control tick + inline render (when no render task) ---
        uint64_t now = platform_millis();
        if (now >= next_ctrl) {
            int v                  = engine_active_voices();
            int o                  = keyboard_octave();
            // Keep the drawn value fresh; the status-band blit is debounced so a
            // chord smash (rapid voice-count churn) doesn't starve the audio task.
            ui_state.active_voices = v;
            if (v != ui_state.last_voices) {
                static int      s_vpend       = -1;
                static uint64_t s_vpend_since = 0;
                if (v != s_vpend) {  // new candidate -> (re)start the debounce window
                    s_vpend       = v;
                    s_vpend_since = now;
                }
                if (now - s_vpend_since >= VOICE_METER_STABLE_MS) {
                    ui_state.last_voices = v;
                    ui_state.change_seq++;
                    int a, b;
                    ui_band_status(&a, &b);
                    ui_invalidate(a, b);
#ifdef SYNTH_PROFILE
                    printf("[PROFILE] voices=%d\n", v);
#endif
                }
            }
            if (o != ui_state.last_octave) {
                ui_state.octave      = o;
                ui_state.last_octave = o;
                ui_state.change_seq++;
                int a, b;
                ui_band_status(&a, &b);
                ui_invalidate(a, b);
            } else {
                ui_state.octave = o;
            }

            // Drive hold-to-repeat for F1/F2 shape buttons (WO-5). State writes
            // happen inside ui_tick; bump change_seq afterwards if a repeat fired.
            ui_tick(&ui_state, now);
            if (ui_state.held_dir != 0) {
                ui_state.change_seq++;
                int a, b;
                ui_band_content(&a, &b);
                ui_invalidate(a, b);
            }

            // On host there is no render task; draw inline on the main thread.
            if (!has_render_task) render_cb(&ui_state);

            next_ctrl = now + RENDER_MS;
        }

#ifdef SYNTH_PROFILE
        // Audio-block cycle readout + signal-magnitude probe — ~1 s cadence.
        if (now >= next_prof) {
            uint32_t avg_cyc, max_cyc, over, count;
            platform_audio_profile_read(&avg_cyc, &max_cyc, &over, &count);
            uint32_t hz  = platform_cycles_per_sec();
            uint32_t div = hz / 1000000u;
            if (div == 0) div = 1;
            printf("[PROFILE] audio avg=%u max=%u over=%u/%u us-budget=1333\n", (unsigned)(avg_cyc / div),
                   (unsigned)(max_cyc / div), (unsigned)over, (unsigned)count);
            float pk_mono, pk_postgain, min_gr, pk_out;
            engine_profile_read(&pk_mono, &pk_postgain, &min_gr, &pk_out);
            printf("[PROFILE] sig  mono=%.2f postg=%.2f gr=%.2f out=%.2f\n", (double)pk_mono, (double)pk_postgain,
                   (double)min_gr, (double)pk_out);
            // Per-region CPU split (avg/max us per block): where the smash-crackle
            // spike lives. The max fields matter — the spike is one rare block/window.
            EngineCpuProfile cp;
            engine_profile_read_cpu(&cp);
            printf("[PROFILE] cpu  drain=%u/%u setup=%u/%u voices=%u/%u master=%u/%u us(avg/max)\n",
                   (unsigned)(cp.drain_avg / div), (unsigned)(cp.drain_max / div), (unsigned)(cp.setup_avg / div),
                   (unsigned)(cp.setup_max / div), (unsigned)(cp.voices_avg / div), (unsigned)(cp.voices_max / div),
                   (unsigned)(cp.master_avg / div), (unsigned)(cp.master_max / div));
            // Stage 8 diag: worst-block snapshot -- the mechanism discriminator.
            // ipc (instret/cycles, x100 scaled) separates stall (low ipc, flat
            // instret) from preemption (high instret, low active) from genuine
            // compute (instret scales with active, normal ipc); vmax@v pins a
            // single hot voice vs a uniform-slow spread. See specs/MEMORY.md.
            uint32_t ipc_x100 = cp.worst_voices_cyc ? (cp.worst_voices_instret * 100u) / cp.worst_voices_cyc : 0u;
            printf(
                "[PROFILE] worst voices=%uus instret=%u ipc=%u.%02u active=%u vmax=%uus@v%u | drain=%uus "
                "setup=%uus master=%uus\n",
                (unsigned)(cp.worst_voices_cyc / div), (unsigned)cp.worst_voices_instret, (unsigned)(ipc_x100 / 100u),
                (unsigned)(ipc_x100 % 100u), (unsigned)cp.worst_active, (unsigned)(cp.worst_vmax_cyc / div),
                (unsigned)cp.worst_vmax_idx, (unsigned)(cp.worst_drain_cyc / div), (unsigned)(cp.worst_setup_cyc / div),
                (unsigned)(cp.worst_master_cyc / div));
            next_prof = now + 1000u;

            // Audio RAM tap (crackle forensics): dump once per freeze. make
            // PROFILE=1 build install run; make sniff; play until the crackle
            // is audible; tap SPACE (manual freeze -- mostly pre-keypress
            // history); wait for "[TAP] end"; then python3 tools/tap2wav.py
            // sniff.log -o tap.wav.
            static bool s_tap_dumped = false;
            if (!s_tap_dumped && engine_tap_frozen()) {
                s_tap_dumped = true;
                tap_dump();
                // Re-arm loop: reset the tap so the next SPACE press captures
                // again with no reboot required.
                engine_tap_rearm();
                s_tap_dumped = false;
            }
        }
#endif
        platform_sleep_ms(POLL_MS);
    }

    platform_render_task_stop();
    platform_audio_stop();
    // ESC / window-close ends the loop; hand control back to the launcher (on
    // host this just exits the process). Without this the device app would sit
    // idle after the loop instead of returning home.
    platform_exit_to_launcher();
}
