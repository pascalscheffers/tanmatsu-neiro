# MAP — seam index

One line per key seam so a worker (or any session) jumps straight to it instead of searching
the tree. **Read this before grepping.** If you add or move a seam, add/fix its line here —
a stale MAP is worse than none. Layers point downward only (CLAUDE.md architecture).

Paths are relative to repo root. Dependencies live in `managed_components/` (ESP-IDF) and
`dsp/vendor/` (DaisySP) — **don't open those unless a work-order says so.**

## engine/ — the synth, voices, params, presets
- `engine/synth.h` — **the render contract.** `synth_render(left,right,n,user)` (matches the
  HAL audio fn); `synth_init`; thread-safe `engine_note_on/off`, `engine_set_param[_norm]`,
  `engine_active_voices`. This is the only entry the platform/UI call into audio.
- `engine/synth.cpp` — composes the synth: drains queues at top of block, runs the voice
  allocator + master chorus + soft-clip, writes the stereo bus. IRAM-placed render path (ADR 0013).
- `engine/voice.h` — **`IVoice` ABC** (the swappable voice unit, ADR 0008) + `NoteExpression`
  (MPE-ready note params). The allocator and the mod matrix (Stage 3) talk to this, never a model.
- `engine/voice_alloc.{h,cpp}` — **`VoiceAlloc`**: model-agnostic polyphonic allocator over an
  `IVoice` pool (`VoiceSlot`). Play modes (mono/uni/legato) land here in Stage 3d.
- `engine/synth_model.h` — **`SynthModel`** factory seam: makes voices for the allocator.
- `engine/juno_model.{h,cpp}` / `engine/juno_voice.{h,cpp}` — the concrete Juno-106 model +
  voice (PolyBLEP saw + sub + noise → SVF LP → ADSR VCA). The first `SynthModel`/`IVoice` impl.
- `engine/spsc_ring.h` — **`SpscRing<T,Cap>`**: the reusable lock-free single-producer/
  single-consumer ring (power-of-two, atomic acquire/release). **Reuse this for any
  thread→audio handoff** — do not write a second ring.
- `engine/command_queue.h` — `CommandQueue<Cap>` of `NoteCmd`: the note-event queue (control
  thread → audio), built on the SPSC pattern.
- `engine/param_desc.{h,cpp}` — **`ParamDesc`** struct + the **`JUNO_PARAM_TABLE`** /
  `kJunoParamCount`: the single declarative parameter table (spec 05; central dedup mechanism).
  Adding a param = one row here, forever.
- `engine/param_id.h` — stable `uint16` param ids (preset-format-relevant; ADR/spec 05 gate).
- `engine/param_store.{h,cpp}` — **`ParamStore`**: `param_set[_norm](id,value,source)` applies
  curve+range and pushes a `ParamUpdate` into an `SpscRing`; audio thread `drain()`s and
  smooths. The single write path into the audio thread (no mutex, no alloc).
- `engine/preset.{h,cpp}` — preset serialize/parse **by param id** (`preset_serialize`,
  `preset_parse`) + INIT and the factory bank (`preset_factory_count/name/params`). Spec 05 format.
- `engine/synth_config.h` — named constants (sample rate, block, `kNumVoices`, table sizes).
  No magic numbers in DSP — they live here.
- `engine/bench.{c,h}` — Stage 0.5 CPU bench (built under `BENCH=1`).
- `engine/mod_matrix.h` — **`ModMatrix`** (16-slot fixed modulation matrix, ADR 0009).
  Key seams for Stage 3c-iii:
  - `kModDestPitch = 0xFFFE` — virtual dest: semitone pitch offset (already wired).
  - `kModDestPwm = 0xFFFD` — virtual dest: pulse-width offset (Stage 3c-iii). Was
    `kPresetDestPwm` local to `preset.cpp`; promoted here so `juno_voice.cpp` can read it.
  - `ModOutputs::pwm_mod` — new field accumulating the LFO1→PWM routing (Stage 3c-iii).
    Applied in `JunoVoice::render()` as: `pw = clamp(p_osc_pwm_ + mout.pwm_mod, 0.05f, 0.95f)`.
  - `ModOutputs` seeding: all accumulators start at `+1e-20f` (ADR 0012 denormal guard).

## dsp/ — pure, portable blocks (no ESP-IDF, no I/O, no globals)
- `dsp/osc.h` / `dsp/filter.h` / `dsp/env.h` — header-only wrappers over DaisySP
  (Oscillator / SVF / Adsr) with our seam (MIDI note in, anti-denormal per ADR 0012). Wrap,
  don't edit vendor.
  - `dsp::Osc::set_waveform(int wf)` — set main osc waveform: 0=SAW, 1=PULSE, 2=TRI
    (maps to `WAVE_POLYBLEP_SAW/SQUARE/TRI`; out-of-range clamps to SAW). Stage 3c-iii.
  - `dsp::Osc::set_pw(float pw)` — set pulse width for PULSE waveform; delegates to
    `daisysp::Oscillator::SetPw()`. JunoVoice applies once per block at [0.05, 0.95]
    after adding `mout.pwm_mod`. Stage 3c-iii.
- `dsp/saturate.h` — `soft_clip(float)`: the master soft-clip ceiling (ADR 0016).
- `dsp/vendor/daisysp/` — vendored DaisySP (pinned SHA in MEMORY/ledger). Read-only; don't open
  unless a work-order points at a specific file.

## platform/ — the HAL (the only place bsp/SDL/miniaudio may appear, ADR 0007)
- `platform/platform.h` — **the 5-seam HAL**: `platform_init`, `platform_framebuffer` +
  `platform_present` (display), `platform_audio_start/stop` + `platform_audio_render_fn` +
  `platform_event_t`/`platform_poll` (input). MIDI transport + **storage** seams are declared
  intent, wired as stages reach them (preset storage = Stage 2d).
- `platform/device/` — ESP-IDF + badge-bsp impl. `platform/host/` — SDL2 + miniaudio impl
  (host pays any P4↔host conversion, ADR 0011).
- `platform/platform.h` SD seam (planned) — `platform_sd_available` + `platform_sd_root`;
  boot-time `/sd` mount on device / `./sd` on host. Architecture: ADR 0024; closed work-orders:
  `specs/stages/stage-11-sd-recording.md`.

## control/ , ui/ , app/ — the brain (soft-real-time, normal tasks)
- `control/keyboard.{c,h}` — musical-typing / key input → note + param events.
- `ui/ui.{cpp,h}` — PAX param pages rendered **from the table** (no model knowledge, ADR 0008).
- `app/app.{c,h}` — app-level wiring (init order, the main loop).

## Where decisions and state live
- `specs/decisions/` — ADRs (the *why*); `specs/00-08` — specs (the *what*);
  `specs/09-build-and-run.md` — build/run reference.
- `specs/MEMORY.md` — live progress log (last few entries + open gates);
  `specs/MEMORY-archive.md` — older history.
- `specs/stages/` — stage runbooks + the work-order protocol (`README.md`).
