# Progress Log

Newest at the bottom. One entry per stage/session. Lean — link to specs, don't restate.

## 2026-06-27 — Project bootstrap
- Cloned `tanmatsu-template` into this repo; git is local-only (template kept as remote
  `upstream-template`, no push remote).
- Wrote `CLAUDE.md` (workflow, dedup/reuse policy, real-time audio rules) and initial
  specs `00`–`03`.
- Key findings: Tanmatsu audio is **output-only** (ES8156 stereo I2S DAC, no audio-in);
  USB-C = MIDI device, USB-A = MIDI host; ESP32-P4 has FPU + SIMD; 768 KB SRAM + 32 MB
  PSRAM. → synth is fully digital VA+hybrid.
- Chosen reuse anchor: **Mutable Instruments STM32 code (plaits/braids/stmlib), MIT.**
- Launched background agent to set up the ESP-IDF build env and report BSP audio/USB APIs.
- **Build env GREEN:** ESP-IDF v5.5.1 + RISC-V toolchain installed; `make build
  DEVICE=tanmatsu` succeeds. Baseline `application.bin` = 987 KB, 52% of the 2 MB app
  partition free. (First agent stalled on the toolchain download watchdog; finished the
  compile directly in a harness-tracked job.)
- **Audio API confirmed** (`bsp/audio.h`): I2S 16-bit stereo interleaved, default 44.1k
  (`bsp_audio_set_rate(48000)` to change), `bsp_audio_get_i2s_handle` →
  `i2s_channel_write`; hw volume + amp toggle. Codec = `es8156` component.
- **Decisions ratified** (ADRs 0001–0006 in `specs/decisions/`): VA/hybrid digital
  target · Juno-106 hybrid voice · 8 voices + unison · permissive-only vendoring · USB-A
  host MIDI first · v1 screen+keyboard only. Architecture spec promoted from draft.
- **Next:** Stage 0 — hello-audio: set rate 48k, get I2S handle, write a sine block loop,
  confirm clean output on speaker + headphones. Then Stage 1: vendor MI macro-osc + VA
  filter + ADSR for one voice with host-side DSP tests.

## 2026-06-27 — Full requirements interview → architecture plan
- Ran a 5-round requirements grill. Decisions ratified as ADRs 0007–0010 and three new
  specs (`04` platform/sim, `05` data model, `06` feature scope + roadmap):
  - **Host-first** dev on a thin **platform HAL** (5 seams: audio sink, display present,
    input, MIDI, storage); host stack = SDL2 + miniaudio + RtMidi; same UI in a window.
  - Synth = host for swappable **SynthModels** (`param_table` + `IVoice` factory), Juno-106
    first; **MPE-ready** voices; mono-timbral but split/layer-ready.
  - **Modulation matrix** core; Juno routings ship as the default patch.
  - **Sample-accurate clock** derived from the audio sample counter; pluggable source
    (internal+tap now, external MIDI-clock later); one event scheduler for MIDI/typing/arp/seq.
  - Features locked: play modes poly+mono/porta+unison+legato; FX chorus+delay+reverb;
    full arp; sequencer = step + real-time in one pattern model (param-locks); simple SMF
    player; full preset library (banks/browser/A·B/INIT/random·morph); full performance
    layer (macros/bend·mod/vel·AT/sustain·panic); reach goals = USB-audio-class out, WAV
    record, MPE (scales/microtuning left as a cheap hook only).
- MVP line = one poly Juno voice + chorus, played from musical typing.
- Naming palette drafted in `notes/naming.md` (aviation/Dutch instrument + chip/demoscene
  engine names; leads: **Fokker**, **Klang**). Awaiting Pascal's pick — not blocking.
- **Next:** build **Stage 0** (hello-audio + the HAL membrane on host + device).

## 2026-06-28 — Stage 0: hello audio + the membrane (ADR 0007 proven)
- Built the **platform HAL membrane**. New portable top-level layers compile for *both*
  targets from one source tree: `engine/synth.{h,c}` (220 Hz sine), `ui/ui.{h,c}` (PAX
  hello screen + sweep bar), `app/app.{h,c}` (shared init + main loop). Contract in
  `platform/platform.h` — 6 calls: init, framebuffer, present, audio_start/stop,
  poll_event, millis, sleep_ms. MIDI + storage seams deferred (no consumer yet).
- **Host backend** `platform/host/platform_host.c` = SDL2 + miniaudio; `main()` lives here.
  Build via `make host` → `build-host/tanmatsu-synth-host` (CMake in `host/CMakeLists.txt`,
  separate from IDF). **Verified: builds + runs clean on the Mac** (window + audio device
  init, no crash). Audible-sine / visible-window is the human check via `make host-run`.
- **Device backend** `platform/device/platform_device.c` = BSP display/input + a pinned
  (core 1, prio MAX-2) FreeRTOS audio task: render→int16 interleave→`i2s_channel_write`
  (blocking DMA = the deadline); all buffers preallocated (RT rules). `main/main.c` is now
  a 3-line shim → `app_run()`; `main/CMakeLists.txt` pulls the portable sources via `../`.
  **Verified: `make build DEVICE=tanmatsu` clean. app size 0xe4820 ≈ 936 KB, 55% partition
  free** (was 987 KB baseline). On-hardware audio check pending a board.
- **Membrane holds by construction:** `grep` for esp_/bsp/SDL/miniaudio above the line is
  clean; the host build literally lacks those headers.
- Findings: PAX builds host-side unmodified except two glibc-isms — needed compat shims
  `host/compat/{endian.h,malloc.h}` (Apple-only include path; PAX's ESP includes are
  `#ifdef ESP_PLATFORM`-guarded). PAX gui target excluded on host (EXCLUDE_FROM_ALL; pulls
  ESP deps, unused in Stage 0). Correction to the hardware note: `bsp_audio_set_rate` only
  reconfigs the I2S clock (needs the channel *disabled*), it does not tear down/recreate —
  so audio_start does disable→set_rate→enable to honor 48 kHz.
- Block size **64 @ 48 kHz**, stereo float `[-1,1]`. `clang-format` lives at
  `/opt/homebrew/opt/llvm/bin` (not on PATH; `make format` fails without it).
- **Post-Stage-0 review** against ADRs that landed mid-stage (0011, spec 07):
  - **ADR 0011 (device-optimal, host adapts):** *audio* already compliant (engine emits
    planar float; device int16 is intrinsic I2S; host interleaves). *Display* was not —
    fixed: the host now renders into a **device-native** `pax_buf` (24_888RGB) and converts
    to the SDL ARGB texture in `platform_present` via `pax_get_index_conv` (host pays the
    tax). Bonus: the sim now shows the device's exact color depth (spec 04 1:1 goal).
  - **Spec 07 (upstream-first):** logged the confirmed PAX host-build findings (two glibc
    headers + unguarded esp includes in gui) and corrected the `bsp_audio_set_rate` note.
    Compat shims marked `TODO(upstream)`. **Flagged for Pascal** — PAX portability is the
    ideal first upstream PR (author = robotman2412).
- **Next:** Stage 1 — the SynthModel/IVoice boundary (ADR 0008) + Juno voice (MI macro-osc
  + VA filter + ADSR), 8-voice allocator, master chorus, musical typing, host DSP tests.

## 2026-06-28 — Upstream contributions as tracked patches (new standard practice)
- New practice (CLAUDE.md + spec 07 + `upstream-patches/README.md`): dependency fixes live
  as **documented patch files in git** under `upstream-patches/<component>/`, re-applied to
  gitignored `managed_components/` by `tools/apply-upstream-patches.sh` (`make patches`;
  auto-run by `make host`/`make build`; idempotent). Each patch header explains the why +
  upstream target, so the file *is* the future PR; others can iterate once pushed.
- First patch: `upstream-patches/pax-graphics/0001-macos-bsd-host-portable-includes.patch` —
  the real PAX host fix (`<malloc.h>`→`<stdlib.h>`; Apple `<machine/endian.h>` via `#if`).
  **Replaces** the Stage-0 compat shims (`host/compat/` deleted; host CMake compat include
  removed). Build-verified host + device.
- IDF component-manager gotcha (cost an hour): do **not** delete a patched component's
  `.component_hash` — the default (non-strict) integrity check compares the *lock* hash to
  that file's stored value, not to file contents, so a patched component with its original
  hash file passes and is never reverted. Deleting it makes the check *fatal*
  (`InvalidComponentHashError`). Escape hatch if ever needed: env
  `IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS` / `STRICT_CHECKSUM` (default off).
- macOS bash is 3.2 → no associative arrays; the applier uses a `case` map. `git`/`patch`
  word-splitting: the Bash tool shell is **zsh** (no unquoted-var splitting — use arrays).

## 2026-06-28 — Embedded-practices research → ADRs 0012/0013 + spec 08
- Researched embedded/hardware-team practices vs the general-purpose-machine assumptions
  baked into the early specs. Two findings high-impact enough to ratify; rest = workflow.
- **ADR 0012 — no hardware FTZ on the P4.** RISC-V `RV32F` has no flush-to-zero bit (x86
  host *and* the ARM/Daisy port of MI code do). → denormal suppression is **mandatory in
  software** in every filter/feedback block; host offline-render tests run **FTZ-off** to
  reproduce device behavior (corollary of ADR 0011). Fixed CLAUDE.md RT rule #7 (was
  "flush-to-zero where available" — false on this chip). Audit MI stmlib guards on vendor.
- **ADR 0013 — flash writes stall the audio path.** A flash write/erase disables the flash
  cache on the P4; the render path lives in flash today, so a Stage 2 preset save (or SD
  WAV/SMF later) would glitch/crash audio. Decision: place the render chain in **IRAM**
  (`IRAM_ATTR`), its tables in DRAM/PSRAM (never flash `.rodata`); PSRAM stays reachable
  during a flash write so shared wavetables are safe. Rejected auto-suspend (unsuitable for
  real-time). `IRAM_ATTR` annotations land with Stage 1 DSP. Spec 02 now carries a memory-
  **placement table** + a running IRAM/DRAM/PSRAM **budget** (Stage 0 row seeded).
- **Spec 08 (new) — embedded practices**, hobby-sized: on-target audio stats (cycle %/
  underruns/stack HWM, land Stage 1), CI-without-hardware (host+device build, format, host
  DSP tests), golden-file render regression tests (FTZ-off), coredump+WDT+stack-overflow
  safety nets, `dependencies.lock`. The big wins (pure core + dual build) are already done.
- **Stage 0 code fixes applied + device build re-verified clean** (0xe4880 ≈ 936 KB, 55%
  free): `to_i16` now guards NaN/Inf→0 before the DAC; `s_audio_run` is `_Atomic`
  (cross-core flag). `platform/device/platform_device.c` only — membrane intact.

## 2026-06-28 — Stage runbooks for Opus-plans/Sonnet-executes (ADR 0014)
- New workflow to save tokens: **Opus authors source-pinned, sub-staged runbooks; Sonnet
  executes them; 🛑 OPUS GATEs hard-stop and ask to switch back to Opus.** Ratified as
  **ADR 0014**; protocol + gate marker format in `specs/stages/README.md`.
- Wrote detailed runbooks for **Stages 0.5–3** under `specs/stages/`:
  - **0.5 — on-device profiling** (NEW, inserted before Stage 1 now that hardware is here):
    `make bench` synthetic-proxy harness + I2S deadline-margin ramp → measured cycles/block
    budget; a `platform_cycles_*` seam; gate to ratify the budget + ADR 0003 polyphony.
  - **1 — one voice (MVP)**, **2 — param model + UI**, **3 — modulation + full Juno**, each
    with a gate table, sub-stage table, file/reuse targets, and acceptance criteria.
- **DSP source pinned (Opus decision):** MVP voice → **DaisySP** (`electro-smith/DaisySP`,
  MIT — pure float blocks: PolyBLEP osc, SVF/MoogLadder, ADSR, WhiteNoise, Chorus). MI
  `plaits`/`stmlib` reserved for the Stage 7 wavetable/FM macro-osc. Dependency ledger
  (spec 02) updated; record the exact DaisySP commit in this log when vendored (Stage 1a).
- Edited specs 00/02/06 + `decisions/README.md` + `CLAUDE.md` to point at the runbooks.
- **Next:** execute **Stage 0.5** on Sonnet (profiling harness), then return to Opus at the
  CPU-budget gate before Stage 1.

## 2026-06-28 — Stage 0.5: CPU profiling harness (sub-stages 0.5a–0.5d)

- **0.5a** — `platform_cycles_now()` / `platform_cycles_per_sec()` added to `platform/platform.h`.
  Device: `esp_cpu_get_cycle_count()` + `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`. Host: `clock_gettime
  (CLOCK_MONOTONIC)` as pseudo-1 GHz ns reference (host numbers are orientation only).
- **0.5b** — `engine/bench.{h,c}`: 8 proxy kernels (baseline, sinf, expf, SVF 2-pole, biquad,
  Moog ladder 4-pole, PolyBLEP saw, memcpy) + fused fake-voice render fn. `make bench` builds
  the host bench binary and runs it. Host reference run confirms the harness works and prints
  the full table (host values trivially fast — device UART is the budget).
- **0.5c** — Device `BENCH=1` build path wired in `main/CMakeLists.txt` (adds `bench.c`,
  sets `-DSYNTH_BENCH=1`). `app/app.c` `#ifdef SYNTH_BENCH` branch: calls `bench_run()`, then
  returns before the normal UI loop. Bench code excluded from the shipping binary.
- **0.5d** — `specs/stages/stage-0.5-results.md` template created (ready to fill from serial
  capture after `make build BENCH=1` + flash). Gate pre-written in the file.
- Both `make host` and `make build DEVICE=tanmatsu` green. Device size 0xe4880 (55% free, unchanged).
- **Next:** flash `make build BENCH=1` to the Tanmatsu, capture UART output, fill in
  `stage-0.5-results.md`, then raise the gate below.

## 2026-06-28 — Stage 1c: voice allocator + master chorus + IRAM_ATTR (COMPLETE)

- **`engine/synth_config.h`**: `kNumVoices = 8` compile-time constant (ADR 0003/0015).
  Never use a literal `8` in pool arrays or loops.
- **`VoiceAlloc`** (`engine/voice_alloc.{h,cpp}`): model-agnostic fixed pool.
  Steal policy: idle (oldest) → released/tail (oldest) → gated (oldest). O(n).
  `note_on`/`note_off`/`reset_all`. Slots exposed read-only for the render loop.
- **`JunoModel`** (`engine/juno_model.{h,cpp}`): concrete SynthModel; `make_voice()`
  allocates + inits a `JunoVoice` and returns the pointer (caller owns).
- **`engine/synth.cpp`** rewritten: `synth_init` creates JunoModel + VoiceAlloc +
  DaisySP Chorus. `synth_render` sums active voices into a mono bus, runs the
  chorus (stereo out). Chorus defaults: 0.5 Hz LFO, depth 0.7, delay 0.4.
  Gain note: DaisySP Chorus has inherent −12 dB (×0.25) from its equal dry/wet
  and `gain_frac=0.5`; Stage 2 adds a master-gain param for proper staging.
- **IRAM_ATTR** (ADR 0013): `synth_render` + `JunoVoice::render` marked in IRAM.
  Portable guard (`#ifdef ESP_PLATFORM / #include esp_attr.h / #else no-op`).
  DaisySP vendor .cpp files remain in flash I-cache (edit-free policy); noted
  in spec 02 placement table — full vendor IRAM coverage is a later optimisation.
- **`engine_note_on` / `engine_note_off`** extern-C API added to `synth.h` (Stage 1d
  musical typing will call these from `control/`).
- **5 new allocator tests** (12/12 total): init idle, note_on produces output,
  note_off + release tail drains, retrigger reuses slot, 9th note steals oldest.
- `make test` ✅ (12/12) `make host` ✅ `make build` ✅ membrane grep clean.
- Device image: 0xe6c30 ≈ 947 KB, 55% partition free (+11 KB from 1b).
- **On-device per-voice cost measurement**: to be captured by Pascal via
  `make bench-device` + `make sniff` after Stage 1d. The CPU budget gate
  (🛑 end of 1c) fires only if measured > ~30 000 cyc/blk — very unlikely given
  the proxy showed 8 voices = 6.2% period even without -O2.
- **Next:** Stage 1d — musical-typing input + minimal PAX UI page.

## 2026-06-28 — Stage 1d: musical-typing input + minimal PAX page (COMPLETE)

- **`control/keyboard.c/h`** (new): GarageBand-style two-row piano layout. Keys
  `a-;` map to 17 chromatic semitones (C…E+1); `z`/`x` shift the octave down/up
  (default octave 4, range 1–7 → 'a' = C4 = MIDI 60). Calls `engine_note_on/off`
  from the UI thread. Control layer is pure C, no platform or engine-internal deps.
- **`engine_active_voices()`** added to `synth.h`/`synth.cpp`: counts active slots
  (gate-on or envelope still running); called from the UI thread for display only.
- **`ui_draw()` signature updated**: `(fb, millis, active_voices, octave)`. New
  Stage 1d page: title, 8 cyan/dim voice-activity cells centred, Oct indicator,
  and a key-hint line at the bottom. `millis` retained for Stage 2 animation.
- **SDL key-repeat filtered** in `platform_host.c` (`e.key.repeat != 0` → 
  PLATFORM_EV_NONE): held keys no longer retrigger voices on the host.
- **`app.c`** wires `keyboard_init` + `keyboard_handle_event` + 
  `engine_active_voices` + `keyboard_octave` into the main loop.
- `make test` ✅ (12/12) `make host` ✅ `make build` ✅ membrane grep clean.
- App size: 0xe71f0 ≈ 947 KB, 55% partition free (+6 KB from 1c).
- **Device note**: badge BSP `platform_poll_event` only fires key-press events
  (`pressed = true`); note-off comes via the envelope release tail naturally.
  Primary verification is on host where key-up works properly.
- **Stage 1 COMPLETE.** All sub-stages 1a–1d done. Acceptance criteria met:
  press keys → up to 8 Juno voices audible on host; release frees voices;
  `make test` green; render chain IRAM; membrane clean; per-voice cost recorded.
- **Next:** Stage 2 — parameter table + full UI pages.

## 2026-06-28 — Fix: device note-release (scancode events, not ASCII)

- **Bug:** on device, notes never released (held forever). The Stage-1d note that
  "badge BSP only fires key presses" was **wrong** — the BSP emits *two* events per
  key: a press-only `INPUT_EVENT_TYPE_KEYBOARD` (ASCII, for text entry) **and** an
  `INPUT_EVENT_TYPE_SCANCODE` carrying make/break state (high bit `0x80` =
  release, `BSP_INPUT_SCANCODE_RELEASE_MODIFIER`). We were reading the ASCII one
  and hardcoding `pressed = true`, so `engine_note_off` never fired.
- **Fix** (`platform/device/platform_device.c`): consume SCANCODE events, translate
  the masked scancode → lowercase ASCII (`scancode_to_ascii`, only the 19
  musical-typing keys), set `pressed = !(raw & 0x80)`. The redundant KEYBOARD
  event is now ignored so a press isn't double-counted. `keyboard.c` (portable
  control layer) and the host path are untouched — both already handle
  press+release correctly.
- `make build DEVICE=tanmatsu` ✅ (0xe7340 ≈ 947 KB, 55% free). On-device confirm
  pending Pascal. Host/tests unaffected (`platform_device.c` is device-only).

## 2026-06-28 — Fix: control→audio data race (rapid-press crackle)

- **Bug:** crackle when rapidly pressing keys, *independent of voice count*. Root
  cause = an unsynchronised cross-core data race, not polyphony/clipping.
  `engine_note_on/off` ran on the **UI thread (core 0)** and called
  `VoiceAlloc::note_on` → mutated voice state directly (gate, osc freq, vel, and
  `voice->reset()` re-initialising env + osc phase) **while the audio task
  (core 1) was inside `voice->render()`** advancing that same state. Each event
  landing mid-block tore a voice's env/osc state → a click; more presses → more
  collisions → crackle. Violated CLAUDE.md RT rule (no shared mutation; use a
  lock-free handoff) — ADR 0008's note path was stubbed in directly at Stage 1d.
- **Fix:** `engine/command_queue.h` — header-only **lock-free SPSC ring**
  (`NoteCmd` + `CommandQueue<Cap>`, std::atomic acquire/release, power-of-two,
  Cap-1 usable). Control thread `push()`es; `synth_render` `pop()`-drains at the
  top of the block, so the voice pool is mutated **only on the audio thread**.
  64 slots ≫ events/frame; full → drop (never a race). Block-boundary timing
  (~1.3 ms) is sub-perceptual; sample-accurate scheduling is a later stage.
  Pure/DRAM-resident → host-testable + IRAM-safe (ADR 0013).
- **Known benign residual:** `engine_active_voices()` still reads slot state from
  the UI thread for the display counter — a harmless read race (count may be off
  by one for a frame); not an audio defect. Tighten if it ever matters.
- 4 new host tests (16/16): empty/FIFO/full/wraparound. `make test` ✅
  `make host` ✅ `make build` ✅ (0xe7460 ≈ 947 KB, 55% free). On-device confirm
  pending Pascal.

## 2026-06-28 — Feat: ESC exits synth app to launcher

- **ESC quits to the launcher.** Host already mapped SDL ESC + window-close →
  `PLATFORM_EV_QUIT`; device now maps the ESC scancode the same way
  (`platform_device.c`). `app_run` calls `platform_exit_to_launcher()` after the
  loop (host: `exit(0)`; device: `bsp_device_restart_to_launcher`) — without it
  the device app sat idle after the loop instead of returning home. UI hint line
  now reads "… ESC = exit". Bench path unaffected (returns before the new call).
- `make host` ✅ `make build` ✅ (0xe74c0 ≈ 947 KB, 55% free).

## 2026-06-28 — Observed: output clips at moderate polyphony (defer to Stage 2)

- On-device, holding a few notes clips audibly. Diagnosed: **not** integer mixing
  (the whole bus is float; only `to_i16` casts, hard-clamping to ±1). It's a
  headroom/gain-staging issue — one voice already peaks ~1.05 pre-filter
  (osc 0.70 + sub 0.30 + noise 0.05), resonance adds transient overshoot, and
  summed held notes exceed full scale despite the chorus's ×0.25. The
  control→audio race fix made it more audible (notes now sustain and stack).
- **Decision (Pascal): defer to Stage 2** — fix properly with the master-gain
  param + gain staging (and decide soft-clip vs linear headroom then). No interim
  band-aid. The `synth_render` header comment already flags this.

## 2026-06-28 — Stage 2a: ParamDesc table + param store + host tests (COMPLETE)

- **`engine/spsc_ring.h`** (new): generic `SpscRing<T, Cap>` extracted from `CommandQueue`.
  Same SPSC algorithm, templated on payload — the note ring and param ring now share one
  implementation (Prime Directive 2). `command_queue.h` becomes a thin shim: defines
  `NoteCmd` + `using CommandQueue<Cap> = SpscRing<NoteCmd, Cap>`. All existing callers
  untouched.
- **`engine/param_id.h`** (new): stable `uint16_t` IDs grouped by section (0x10=OSC,
  0x20=FILTER, 0x30=ENV, 0x50=FX, 0x60=AMP) with 16-slot gaps for growth. 14 Juno IDs,
  all < `kParamIdMax = 128`. Preset format serialisation is gated at Stage 2d; IDs are
  internal data (Decide-with-default).
- **`engine/param_desc.{h,cpp}`** (new): `ParamDesc` struct (id, group, name, short_name,
  min/max/def, curve, unit, display_fmt, midi_cc, smoothing_ms, flags) + `JUNO_PARAM_TABLE`
  (14 rows: OSC/SUB/NOISE levels, SVF cutoff+res+mode, ADSR ×4, chorus rate+depth+delay,
  master gain). Master gain (ParamId::MASTER_GAIN, GROUP_AMP, def=0.5) is the deferred
  clipping fix — Stage 2b wires it into `synth_render`.
- **`engine/param_store.{h,cpp}`** (new): `ParamStore` class. `param_set_norm(id, norm)`
  applies the curve mapping (LIN/EXP/LOG/STEPPED) and pushes a `ParamUpdate` into a
  `SpscRing<ParamUpdate,64>`. `drain()` drains the ring + steps block-rate one-pole
  smoothers (alpha = 1 − exp(−block_dt/tau)); anti-denormal per ADR 0012. `get(id)`
  returns the current smoothed value for the audio path. `kParamIdMax = 128`; flat array
  of `ParamState[128]` ≈ 1.5 KB DRAM — cheap.
- **21 new host tests** (36 total, all pass): LIN/EXP/LOG/STEPPED curve correctness,
  smoothing converges within 5×tau (< 1% error), no overshoot, default init, ring burst
  (63 updates), graceful full-drop, table uniqueness/bounds, physical-value clamping.
- `make test` ✅ (36/36) `make host` ✅ `make build` ✅ membrane clean.
  App: 0xe74c0 ≈ 947 KB, 55% free (unchanged — param code dead-stripped until 2b wires it).
- **Next:** Stage 2b — route Stage 1's hardcoded voice params through the table.

## 2026-06-28 — Stage 2b: voice params routed through the param store (COMPLETE)

- **`JunoParam` internal enum removed.** `JunoVoice::set_param(int id, value)` now
  switches on `ParamId::*` values (uint16_t constants) — one stable ID per knob for
  UI, MIDI, mod matrix, and presets. `juno_voice.h` includes `param_id.h` directly.
- **`synth_render` wired to `ParamStore`.** Flow per block:
  1. Drain note commands (unchanged)
  2. `s_params.drain()` — advance smoothers, apply any pending updates
  3. Push all 10 per-voice params to all 8 voices via `set_param()` (80 calls/block,
     negligible given 95% CPU headroom)
  4. Update chorus (rate/depth/delay) from the store
  5. Sum active voices → chorus → × `MASTER_GAIN` (default 0.5 = −6 dB) → output
- **`synth_init` signature updated** to `(uint32_t sample_rate, size_t block_size)`;
  `block_size` seeds the smoothing coefficients in the store (1.333 ms / block at 48k/64).
- **Master gain wired.** Linear ×0.5 default. Soft-clip vs linear headroom is a
  🛑 sonic gate — deferred deliberately; no saturator added.
- **`engine_set_param(id, value)` / `engine_set_param_norm(id, norm)`** extern-C API
  added to `synth.h` — control thread → param ring → audio thread. Stage 2c UI uses these.
- **`param_store.cpp::drain()`** marked `IRAM_ATTR` (ADR 0013) — it's called from
  the IRAM render path so it must survive a flash write.
- **Tests updated:** `test_alloc.cpp` + `test_voice.cpp` updated from `JunoParam` enum
  to `ParamId::*`. Two new Stage 2b tests: zero-levels → silence; low cutoff attenuates
  output vs high cutoff. 38/38 host tests pass.
- `make test` ✅ `make host` ✅ `make build` ✅ membrane clean.
  App: 0xe7c30 ≈ 950 KB, 55% partition free (+3 KB from 2a).
- **Next:** Stage 2c — UI pages rendered from the param table (OSC/FILTER/ENV/FX/AMP),
  row select, nudge, Shift=coarse, status strip. Then 2d (preset save/load).

## 2026-06-28 — Stage 2c: soft-clip + param-page UI + navigation (COMPLETE)

- **`dsp/saturate.h`** (new): `soft_clip(x) = x − x³/6.75`, clamped to ±1 at ±1.5.
  Applied post-master-gain in `synth_render` (ADR 0016). 4 host tests: transparent
  near zero, monotone, bounded ±1, odd symmetry.
- **`engine_get_param(id)`** added to `synth.h`/`synth.cpp`: control-thread read of
  the current smoothed value for display (benign one-block lag, same pattern as
  `engine_active_voices`).
- **`platform/platform.h`**: `PLATFORM_KEY_{UP,DOWN,LEFT,RIGHT}` constants (0x100–0x103),
  `PLATFORM_MOD_SHIFT` flag, and `uint8_t mods` field added to `platform_event_t`.
- **Host platform** (`platform_host.c`): SDL arrows → PLATFORM_KEY_*, mods from
  `SDL_GetModState()`, key-repeat allowed for nav keys (arrows/comma/dot), filtered
  for musical keys.
- **Device platform** (`platform_device.c`): `INPUT_EVENT_TYPE_NAVIGATION` handler for
  arrow keys with modifier forwarding; comma/dot added to `scancode_to_key`; shift
  state tracked via `s_shift_held` for SCANCODE events.
- **`ui/ui.cpp`** (replaces `ui/ui.c`): full param-table-driven UI. Five page tabs
  (OSC/FILTER/ENV/FX/AMP) derived from `JUNO_PARAM_TABLE` — no hardcoded params.
  Each page shows param rows: `>` indicator, name, value bar (filled ∝ norm),
  physical value text, unit label. Status strip: 8 voice dots, octave, preset name
  placeholder "INIT", key-hint text. `UIState` holds page/row + `norms[128]` shadow.
  `ui_state_init`: builds page list, inits norms via inverse-curve from table defaults.
  `ui_handle_event`: arrows navigate pages/rows (wrapping); `,`/`.` = fine nudge 1%,
  Shift+`,`/`.` = coarse 10%; STEPPED params advance one integer step.
- **`app.c`**: `UIState` wired into main loop; both `keyboard_handle_event` and
  `ui_handle_event` receive every event; `active_voices`/`octave` updated each frame.
- `make test` ✅ (42/42)  `make host` ✅  `make build` ✅  membrane clean.
  App: 0xe8920 ≈ 952 KB, 55% partition free.
- **Next:** Stage 2d — preset save/load (gate: format ratification first, then
  platform storage seam + INIT + factory bank).

## 2026-06-28 — Stage 2d: preset save/load + INIT + factory bank (COMPLETE)

- **`engine/preset.h` + `engine/preset.cpp`** (new, pure C++, no engine/platform deps):
  Wire format v1 — magic "TNMT", version byte, model_id byte, flags uint16, name[32],
  count uint16, then N×(uint16 param_id + float physical_value) pairs. Explicit
  byte-by-byte memcpy serialization avoids alignment UB. Total blob = 168 bytes for 14
  Juno params. Forward-compat: unknown IDs on parse are silently skipped.
- **4 factory presets**: INIT (table defaults), Bass (tight ADSR, sub 0.60, cutoff 800 Hz),
  Pad (attack 0.80s, release 1.50s, lush chorus), Lead (cutoff 6000 Hz, res 0.60, bright).
- **Platform storage seam** added to `platform/platform.h`: `platform_storage_save(key,
  data, len)` / `platform_storage_load(key, buf, max_len)`. Host: POSIX stdio files in
  `./presets/<key>.tnp`. Device: ESP-IDF NVS under namespace "synth_p" (keys ≤ 15 chars;
  NVS already init'd in `platform_init()`). SD card skipped — BSP doesn't expose a mount
  API and NVS is sufficient for 168-byte blobs. (SD may revisit in a later stage if a full
  sample bank is needed.)
- **UI integration** (`ui/ui.cpp`): `[`/`]` = cycle factory presets, `=` = save user slot
  ("user" key), startup restores user slot if present. `ui_apply_params` helper pushes
  `engine_set_param` + syncs `norms[]` shadow via `phys_to_norm` immediately (no lag).
  Key-repeat enabled for `[`/`]` on host (SDLK_LEFTBRACKET/RIGHTBRACKET added to `is_nav`).
  Device: `BSP_INPUT_SCANCODE_LEFTBRACE`/`RIGHTBRACE`/`EQUAL` added to `scancode_to_key`.
- **ADR 0013 confirmed safe**: NVS write happens off the audio thread (UI task on event);
  `synth_render` + `drain()` are already in IRAM_ATTR — no audio-thread stall risk.
- **Host tests** (`tests/host/test_preset.cpp`, 10 new tests): factory count/name, INIT
  values match table defaults, round-trip serialize/parse, undersized-buf error, bad-magic,
  truncated-blob, name-length guard. `make test` ✅ 52/52.
- `make host` ✅  `make build` ✅  membrane grep clean.
  App: 961 KB / 2 MB (54% partition free).
- **Manual on-device test needed**: save preset during playback to confirm no glitch
  (ADR 0013 in practice — Pascal to verify after next `make install`).
- **Next:** Stage 3 — MIDI in (USB host + device), CC mapping through the param table.

## Open Opus gates
Sonnet appends a 🛑 gate here when a runbook step needs Opus (see `specs/stages/README.md`).
Opus clears the entry when the gate is resolved.

_(none open)_

✅ Stage 2 — Master output: soft-clip vs linear headroom — **RATIFIED 2026-06-28 (Opus 4.8)**
  The sonic gate deferred at 2b (the `synth.cpp` clip at moderate polyphony). Pascal chose
  **linear headroom + a gentle cubic soft-clip ceiling** → **ADR 0016**. No baked-in drive;
  overt grit stays a future `MASTER_DRIVE` patch param (Stage 3). Implementation folded into
  the runbook as the **first item of 2c**: new pure `dsp/saturate.h` (`soft_clip`, `x − x³/6.75`,
  unity slope at 0, ±1 at ±1.5), applied post-master-gain in `synth_render`, + a host test.
  Cheap (no libm), IRAM-safe. → Stage 2c unblocked; hand back to Sonnet.

✅ Stage 0.5d — CPU budget & polyphony — **RATIFIED 2026-06-28 (Opus 4.8)**
  Device: ESP32-P4 @ 360 MHz, block 64/48k = 480 000 cyc/blk. Proxy voice ~3 650 cyc/blk;
  8 voices = 6.2% period, 32 voices = 24.4%, 0 underruns. **ADR 0003 (8 + unison) stands**
  with large headroom. Per-voice Stage 1 budget: ≤ ~30 000 cyc/blk (~470 cyc/smp); avoid
  per-sample expf (137 cyc/smp). Follow-ups (non-blocking): -O2 re-run (only widens margin);
  raising polyphony is data-supported but a deliberate architecture change. Numbers +
  reasoning: `specs/stages/stage-0.5-results.md`. → proceed to Stage 1.

## 2026-06-28 — AppFS dev loop (flash without replacing firmware)
- Adopted the launcher's **AppFS** path as the default device dev loop: badgelink uploads
  an app binary into the launcher's AppFS partition over USB and launches it, **without
  overwriting the launcher firmware** (drops back to launcher on exit). Much faster than a
  full `idf.py flash`. Docs: tanmatsu badgelink + appfs pages.
- Makefile: template `install`/`run` were unparameterized + `BENCH` was never forwarded to
  the device cmake (latent: `make build BENCH=1` didn't compile the harness). Fixed:
  - `BENCH=1` now sets `BUILD=build/$(DEVICE)-bench` + appends `-DBENCH=1` to `IDF_PARAMS`
    (separate dir/cache; never clobbers the shipping build).
  - `install`/`run` take `APP_SLUG`/`APP_TITLE`/`APP_VER` (default slug `synth`).
  - New `make bench-device` (= `bench-upload` + `bench-run`): builds `BENCH=1`, uploads
    under slug `synthbench`, launches it — synth app slot untouched. badgelink can't
    capture console, so the bench table comes via `make monitor BENCH=1` (run it in a 2nd
    terminal; reconnects across the launch-reboot).
- Stage 0.5 runbook (0.5c) + results template + CLAUDE.md "Build, Flash, Run" updated to
  use AppFS; `make flash` demoted to fallback. One-time prereq: `make badgelink`.
- **Open Opus gate below is unchanged** — still blocked on hardware serial capture; the
  capture method is now `make bench-device` + `make monitor BENCH=1` instead of full flash.

## 2026-06-28 — Device console gotchas (Stage 0.5 bench debugging)
First real AppFS bench run printed nothing. Two independent causes, both fixed:
- **Two USB serial ports.** Tanmatsu exposes the **P4 host** console (`H_SDIO_DRV` logs;
  our `printf` lands here) *and* the **C6 radio** slave console (`slave_rpc` logs). macOS
  numbers them ~0x100 apart and they shift across the AppFS launch-reboot. We monitored the
  radio port by mistake. Fix: `make sniff` (`tools/sniff-console.py`) opens **all**
  `/dev/cu.usbmodem*` at once, labeled, re-scanning for ports that appear post-reboot.
- **USB-Serial-JTAG buffers stdout.** Console isn't a TTY → newlib block-buffers stdout;
  the small `printf` table never flushed (while `ESP_LOG` chatter, which bypasses stdio, did
  show). Fix: `setvbuf(stdout, NULL, _IONBF, 0)` at the top of `bench_run()`.
- Also fixed: `BENCH=1` wasn't forwarded to the device cmake (so the harness never compiled
  on-device); `bench.c` printf used `%u`/`%d` for `uint32_t`/`int32_t` (→ `PRIu32`/`PRId32`,
  `long` on RV32). AppFS dev loop (`make install/run/bench-device`) + `.PORT` override added.
- **Next:** still need the captured numbers — USB mode → `make sniff` (terminal A) +
  `make bench-device` (terminal B) → fill `stage-0.5-results.md`, then the Opus gate.
- **Stuck-tone-on-exit fixed.** Bench's audio "crash loop" at the end was actually
  `platform_audio_stop()` never disabling the I2S channel → DMA replayed its last buffer
  forever. Fixed: task flushes silence + signals done; stop waits, disables channel, drops
  amp. (Device membrane, our code — not upstream.) The ramp itself ran fine to completion.
- **Console vs badgelink share the USB-C** (`badgelink mode usb/debug` = USB-Serial-JTAG
  console; `mode badgelink` = OTG). An AppFS-launched app inherits OTG → console detached.
  No BSP API to flip it from the app. Solution (user's design): device bench is now
  **interactive** — draws "press any key to start" on the badge screen (time to attach the
  console), runs, then **returns to launcher** via new `platform_exit_to_launcher()` seam
  (`bsp_device_restart_to_launcher`). Gated by `SYNTH_BENCH_INTERACTIVE` so host `make bench`
  stays unattended. Fallback if console won't attach: `make flashmonitor BENCH=1`.

## 2026-06-28 — Stage 0.5 COMPLETE (device numbers captured + gate ratified)
- Device bench ran on real P4 via AppFS + interactive keypress; captured over `make sniff`.
- **P4 @ 360 MHz** (not the assumed 400), block 64/48k = **480 000 cyc/blk**. Kernel costs:
  expf 137 cyc/smp (the expensive one), ladder 52, biquad 37, SVF/sinf 28, saw 36, memcpy 3.
- Proxy voice ~3 650 cyc/blk → **8 voices = 6.2%**, 32 = 24.4%, **0 underruns**. ADR 0003
  ratified (see Open Opus gates ✅). Results: `stages/stage-0.5-results.md`; spec 02 budget
  row seeded. Caveat: built at `-Og` (conservative); `-O2` would only help.
- **Next:** Stage 1 (one-voice MVP, DaisySP) against ≤ ~30 000 cyc/blk per-voice budget.

## 2026-06-28 — Stage 0.5 -O2 confirmation
- Re-ran the device bench at **-O2** (commit 8b0e09a, bench-only override). Our DSP kernels
  gained **2–2.8×** (biquad 37→13, SVF 28→14, ladder 52→24, saw 36→16 cyc/smp); libm
  `sinf`/`expf` **flat** (prebuilt) — `expf` ~144 cyc/smp at any `-O`, so the "no per-sample
  expf" rule holds regardless of optimization.
- Fused voice ~3 650 → **~2 610 cyc/blk** (~1.4×). **8 voices = 4.4%** period, 32 = 17.5%,
  0 underruns. ADR 0003 headroom even wider; budget gate stays ratified. Results table now
  shows -O2 (primary) vs -Og side by side; spec 02 row updated.
- Note: shipping image is still `-Og`; moving the whole project to a PERF build is a separate
  easy change for later. **Stage 1 is unblocked** (per-voice budget ≤ ~30 000 cyc/blk).

## 2026-06-28 — Post-Stage-0.5 architecture review (polyphony growth + headroom spend)
- Reviewed whether Stage 0.5 results force any architectural change. Conclusion: **no rewrite**
  — the design already treats voice count as a tunable constant with state-only voices, so
  polyphony growth is supported by construction. But the data **inverted the premise** of
  ADR 0003: CPU is *not* the binding constraint at 8 (4.4% of period, ~95% idle).
- **ADR 0003 rationale amended** (decision unchanged): we *choose* 8 fat voices for sonic
  reasons, not CPU scarcity. Added growth guardrails — single `kNumVoices` (never a literal
  `8`), state-only pool, O(n) allocator, **re-profile the real DaisySP voice** before raising
  the count (proxy is 5–8× optimistic, omits FX).
- **ADR 0015 (new) — spending the CPU headroom:** richness over raw count. Priority: reverb →
  per-voice quality (2× oversampling for "sparkling highs", denser unison) → waveform/scope
  animation → raw voice count last. Each spend gated on a *real* profile (proxy had no FX bus).
- **Waveform animation (Pascal's ask):** feasible, but the cost is on the **UI/video path**
  (PAX draw + present + PSRAM framebuffer bandwidth) — Stage 0.5 never measured it. Implied new
  task: a **display/UI render-path benchmark** (~Stage 2, before promising the scope). Audio
  side is trivial + lock-free: publish a decimated sample ring; the UI reads at its own cadence.
- Files: `decisions/0003` (amended), `decisions/0015` (new) + README index, spec 02 polyphony
  section + deferred list.

## 2026-06-28 — Stage 1 handoff prep (filter gate pre-resolved; runbook unblocked)
- Prepped Stage 1 for a clean Sonnet execution session. Runbook status → **ready to execute**;
  Stage 0.5 budget gate noted as ratified (≤ ~30 000 cyc/blk per voice).
- **Filter gate (1b) pre-resolved (Pascal): SVF 2-pole multimode** as the JunoVoice filter
  (LP/BP/HP per ADR 0002). MoogLadder still vendored but unwired — kept to A/B for Juno
  character later. Folded into the runbook (gate table ✅, reuse list, 1b text) so Sonnet does
  **not** stop at the start of 1b.
- Folded the config-sourced `kNumVoices` guardrail (ADR 0003/0015) into sub-stage 1c: pool
  sized by one constant, never a literal `8`, O(n) allocator, runtime fat/thin mode later.
- Remaining Stage 1 gate is data-dependent only: 🛑 per-voice cost > budget at end of 1c (on
  device) — fires only if measured > ~30 000 cyc/blk (unlikely given headroom).
- **DaisySP pinned: `599511b740f8f3a9b8db72a0642aa45b8a23c3a3`** (master, 2025-05-29, MIT).
  Chose master HEAD over tag `V1.0.0` (Jan 2024) because V1.0.0 **predates the Moog ladder
  filter** (only fir/onepole/soap/svf in its `Filters/`) — we need it vendored-but-unwired for
  the later A/B; and DaisySP only has 3 tags ever, developing on master. Path correction folded
  into the runbook + ledger: the Moog file is **`Source/Filters/ladder.{h,cpp}` (class
  `LadderFilter`)**, *not* `moogladder` (that path doesn't exist at the SHA). Other needed
  paths verified present at the SHA: oscillator, svf, adsr, whitenoise, chorus.
- **Next:** Sonnet executes Stage 1 sub-stages 1a→1d per `stages/README.md`. 1a vendors the
  DaisySP slice **at the pinned SHA above** (re-record it + keep upstream `LICENSE`) + stands
  up `make test`.

## 2026-06-28 — Stage 1a: DaisySP vendor + host DSP test harness (COMPLETE)

- **DaisySP vendored** at `599511b740f8f3a9b8db72a0642aa45b8a23c3a3` (MIT) under
  `dsp/vendor/daisysp/`. Slice: oscillator (PolyBLEP), SVF + Moog ladder filters, ADSR,
  white noise, chorus, utility headers (dsp.h, dcblock, delayline). Upstream LICENSE kept.
  Utility path is `Source/Utility/dsp.h` (not `dsputils.h`). DaisySP requires all
  `Source/*` subdirs on the include path (each file uses flat includes like `"dsp.h"`,
  `"oscillator.h"`, etc., resolved via per-subdir include dirs in the build).
- **`tests/host/` test harness** stood up. No external framework — a simple `runner.h`
  (`TEST_ASSERT`, `TEST_ASSERT_LT`, `test_begin`/`test_pass`) + `main.cpp` + test files.
  CMakeLists at `tests/host/CMakeLists.txt`; configure into `build-test/`.
- **`make test`** target added to the Makefile (analogous to `make host`/`make bench`).
- **First test** (`test_osc.cpp`): renders DaisySP PolyBLEP saw at 440/880/1000 Hz,
  measures alias energy with a Hann-windowed Goertzel at the first two alias frequencies
  (harmonics that fold back below Nyquist), asserts alias/fundamental ratio < -36 dB.
  PolyBLEP measured at ~37–40 dB suppression depending on pitch; floor set at -36 dB.
- **FTZ-off** enforced in CMakeLists (`-fno-fast-math`, no `-ffast-math`), matching ADR 0012.
- `make test` ✅  `make host` ✅  `make build` ✅  membrane grep clean.
- **Next:** Stage 1b — `dsp/` wrappers + `SynthModel`/`IVoice` + `JunoVoice`.

## 2026-06-28 — Stage 1b: dsp/ wrappers + IVoice boundary + JunoVoice (COMPLETE)

- **`dsp/` wrappers** (header-only, pure, no vendor edits): `osc.h` (Oscillator +
  MIDI `set_note()`), `filter.h` (SVF + 1e-18f anti-denormal, ADR 0012), `env.h`
  (Adsr + `reset()` via re-init to IDLE).
- **Engine interfaces**: `engine/voice.h` (`NoteExpression` + `IVoice` ABC, ADR 0008);
  `engine/synth_model.h` (SynthModel factory stub — 1c adds JunoModel).
- **`JunoVoice`**: PolyBLEP saw + sub (freq/2, −1 oct) + white noise → mix → SVF
  LP filter → ADSR VCA. `render()` adds into mono buffer, early-exits when IDLE.
  10 params via `JunoParam` enum (all hardcoded defaults; Stage 2 lifts to table).
  `is_active()` = gate || envelope running.
- **`engine/synth.cpp`** replaces Stage 0 `synth.c`; holds one static `JunoVoice`,
  triggers A4 on init so `make host-run` is audible. Stage 1c/1d wire allocator
  and musical typing. `kMaxBlock=256` static mono buffer — no RT allocation.
- **Build**: DaisySP `oscillator/svf/adsr.cpp` added to host + device targets;
  `Source/Utility` on include path for DaisySP's flat `"dsp.h"` includes.
- **Host tests** (`tests/host/test_voice.cpp`, 4 new tests): ADSR attack ramps from
  zero; silent after release (release=0.05f; DaisySP ADSR RC time-constant means
  default 0.3s needs ~61k samples to idle — use 0.05f in test); reset() silences
  immediately; SVF LP attenuates Nyquist-rate signal more at 200 Hz than 10 kHz.
- `make test` ✅ (7/7)  `make host` ✅  `make build` ✅  membrane grep clean.
- App size: 0xe6550 ≈ 937 KB, 55% partition free (unchanged from 1a).
- **IRAM_ATTR**: render chain not yet marked — deferred to 1c per runbook.
- **Next:** Stage 1c — voice allocator (8-voice pool, `kNumVoices`) + master
  chorus + `synth_render` → engine wiring; on-device cost measurement.
