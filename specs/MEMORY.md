# Progress Log

The **live** log: recent entries + open gates. Older history is in
[`MEMORY-archive.md`](MEMORY-archive.md). One entry per dispatched job; **append new entries
just above the "Open Opus gates" section** (which stays last). Lean — link to specs, don't
restate. When this passes ~200 lines, rotate older entries into the archive.

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

## 2026-06-28 — Methodology: orchestrator/worker fan-out is now the default (ADR 0017)

- Diagnosed the "rapid start → slowdown" pattern (Stage 2d compacted): cause is context bloat
  from work sized by *feature* not *budget* + briefs that describe instead of pin reads + a
  fat always-loaded baseline — **not** the tree layout (own source ~4 KLoC; deps aren't loaded
  unless read). Subagents start with a **fresh** context, so they're the fix, not a workaround.
- **New default (ADR 0017, amends 0014):** Opus orchestrates; fresh-context Sonnet *workers*
  execute closed **work-orders** (Touch/Read/Reuse/Don't-read/Acceptance/Split-if; debug variant
  adds Repro/Root-cause) and return a summary. Opus reviews summaries, not diffs. Gates return
  to Opus instead of a model-switch. Altitude rule: inline if trivial/ambiguous, dispatch if
  specifiable. Hard budget: ≤ ~8 files / ≤ ~5 read-sections per work-order → Opus splits at
  authoring.
- **Enabling changes:** work-order template + dispatch loop in `stages/README.md`; CLAUDE.md
  rewritten (methodology is now the always-on content) + build catalog moved to
  `specs/09-build-and-run.md`; new `specs/MAP.md` seam index (read before grepping); `MEMORY.md`
  rotated 577→148 lines (history in `MEMORY-archive.md`); Stage 3 sub-stages retrofitted with
  work-orders (first stage to run under this model).
- **Next:** dispatch **Stage 3a** as the first live worker run; confirm Opus context stays flat
  (summary only) and the worker reads only its read-list. Record before/after bootstrap size.

## 2026-06-28 — Methodology: tier + effort grid added to dispatch (ADR 0017)

- Dispatch is now **two knobs, not one**: which model *and* how much reasoning effort, matched
  to the task. Grid in ADR 0017: Haiku for mechanical read/search/extract (Explore tier);
  Sonnet·low for cheap-but-needs-judgment; Sonnet·medium for normal implementation; Sonnet·high
  for tricky DSP correctness / verify / debug-root-cause confirmation; Opus for authoring,
  architecture, seams, gates.
- **Mechanism caveat (important):** `effort` is a per-call option only on a **Workflow
  `agent()`** dispatch. The plain **Agent-tool** single-worker dispatch (the default loop) has
  no effort field — it inherits the session effort; wrap in a one-item Workflow when a job
  genuinely needs a different effort. Model is settable on both.
- Folded into ADR 0017 (Decision), `stages/README.md` (dispatch step 1), CLAUDE.md (the loop).
- **Next:** unchanged — dispatch **Stage 3a** as the first live worker run.

## 2026-06-28 — Stage 3a: per-voice mod sources (ENV2 + LFO1/2) (COMPLETE)

- **`dsp/lfo.h`** (new, pure header-only): phase-accumulator LFO, 5 waveforms
  (SINE/TRI/SAW/SQUARE/S&H). Anti-denormal +1e-20f on continuous waves per ADR 0012.
  Not band-limited (expected at this stage; BL LFO would be a separate sub-stage).
- **`engine/juno_voice`** updated: `env2_` (second `dsp::Env` ADSR) + `lfo1_`/`lfo2_`
  (`dsp::Lfo`) rendered per sample in `render()`; `env2_value()`/`lfo1_value()`/
  `lfo2_value()` accessors expose last-sample outputs for the mod matrix (Stage 3b-i).
- **`engine/param_id.h`**: ENV2 group 0x40–0x43, LFO1 0x70–0x72, LFO2 0x78–0x7A.
  All new IDs < kParamIdMax=128; no param_store.h change needed.
- **`engine/param_desc.h/cpp`**: GROUP_ENV2=8 added; 10 new param rows (ENV2 ADSR,
  LFO1/2 rate+depth+shape). kJunoParamCount now 24.
- **`engine/preset.cpp`**: FactoryPreset arrays widened to [32], all 4 presets updated
  to 24 params with sensible ENV2/LFO defaults per preset character.
- **`ui/ui.cpp`**: GROUP_ENV2 and GROUP_LFO added to `group_name()` so new pages
  render automatically (page list is built dynamically from the table).
- **9 new host tests** (61 total, all pass): ENV2 independence, gate tracking,
  LFO oscillation, waveform shape, depth scaling, rate via set_param, S&H piecewise
  constant, range bounds, per-voice independence.
- `make test` ✅ (61/61) `make host` ✅ `make build` ✅ membrane clean.
  App: 964 KB / 2 MB (54% partition free, +3 KB from Stage 2d).
- **Next:** Stage 3b-i — mod-matrix engine (gated: matrix-shape ratification first).

## 2026-06-28 — Stage 3b-i: mod-matrix engine (COMPLETE)

- **`engine/mod_matrix.h/cpp`** (new): `Routing` struct (source:u8, dest_param_id:u16,
  depth:f32, curve:u8) matches the ADR 0009 frozen shape. `ModSource` enum (NONE=0,
  LFO1=1..AFTERTOUCH=9; ids 10–19 reserved for MPE/macros). `ModCurve` enum (LIN=0,
  SQR=1, CUBE=2). `ModMatrix` holds 16 slots; `eval(ModSources)→ModOutputs` is
  O(active routes), skips NONE/zero-depth slots. Anti-denormal: accumulators seeded
  with +1e-20f per ADR 0012 (P4 has no HW FTZ).
- **Audio-rate dests** (cutoff, pitch via `kModDestPitch=0xFFFE`, amp) block-smoothed
  per sample via linear interp from base to modulated value over the block. **Control-rate
  dests** (res, sub-level, noise-level) applied once per block.
- **`engine/juno_voice`** wired: `mod_matrix_` member per voice; `render()` evaluates
  the matrix once per block using last-block source values (ENV2, LFO1/2, velocity,
  key-track); applies audio-rate mods via per-sample freq/cutoff/amp ramps. `midi_note_`
  cached for key-track computation (centered on A4=69, ±1 unit/semitone/12).
- **12 new host tests** (73 total, all pass): exact-math depth, zero-depth/NONE no-op,
  multi-source summation, audible cutoff mod in voice, pitch semitone accumulation,
  LIN/SQR/CUBE curve outputs, all-inactive → near-zero, 16-slot sum, ENV2 exact math,
  velocity full-range, key-track polarity.
- `make test` ✅ (73/73) `make host` ✅ `make build` ✅ membrane clean.
  App: **965 KB / 2 MB (54% free)** (+1 KB from Stage 3a).
- **Next:** Stage 3b-ii — Juno default-patch voicing (🛑 sonic gate) + preset format
  carries routings.

## Open Opus gates
Sonnet appends a 🛑 gate here when a runbook step needs Opus (see `specs/stages/README.md`).
Opus clears the entry when the gate is resolved.

_(none open)_

✅ Stage 3 — Juno default-patch voicing — **RATIFIED 2026-06-28 (Opus 4.8)**
  Sonic gate during 3b-ii. Pascal chose **"Clean 106"**: matrix default routings =
  `ENV2→cutoff +0.35 LIN` and `LFO1→PWM +0.20 LIN` (2 of 16 slots; amp env already hardwired
  in `JunoVoice`, so these are additive). No default vibrato — left for the user/mod-wheel.
  Depths normalized + tunable on device without re-ratifying. Frozen in **ADR 0009 §Default-patch
  voicing**; stage-3 gate row ✅. → Stage 3b-ii unblocked; dispatch fresh worker.

✅ Stage 3 — Mod-matrix shape — **RATIFIED 2026-06-28 (Opus 4.8)**
  Gate before 3b-i (architecture + data-format: sizes the audio inner loop *and* the preset
  bytes). Pascal chose **16 fixed routing slots/patch**, record =
  `{source:u8, dest_param_id:u16, depth:f32 (bipolar [-1,+1]), curve:u8}` — keeps ADR 0009's
  per-routing `curve`. Audio-rate dests = pitch/PWM/cutoff/amp (per-block smoothed); all else
  control-rate. Inactive slot = `source==NONE` or `depth==0`. Serialize field-by-field; format
  bump deferred to 3b-ii. Frozen in **ADR 0009 §Frozen shape**; gate row in stage-3 doc marked ✅.
  → Stage 3b-i unblocked; dispatch fresh worker.

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

