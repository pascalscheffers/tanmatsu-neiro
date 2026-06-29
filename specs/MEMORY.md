# Progress Log

The **live** log: recent entries + open gates. Older history is in
[`MEMORY-archive.md`](MEMORY-archive.md). One entry per dispatched job; **append new entries
just above the "Open Opus gates" section** (which stays last). Lean — link to specs, don't
restate. When this passes ~200 lines, rotate older entries into the archive.


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

## 2026-06-28 — Stage 3b-ii: Juno default patch + preset format carries routings (COMPLETE)

- **Preset format bumped to v2.** Routings block appended after param pairs: `count:u16` + N × 8-byte
  records (`source:u8 + dest_param_id:u16 + depth:f32 + curve:u8`, field-by-field, no struct memcpy).
  `PRESET_BLOB_MAX` widened to 384 (was 256) to cover 16 routing slots + 24 params. v1 blobs still
  parse (zero routings returned — back-compat). Unknown source ids skipped (forward-compat).
- **"Clean 106" routings** (ADR 0009 RATIFIED) shipped in all 4 factory presets: `ENV2→FILTER_CUTOFF
  +0.35 LIN`, `LFO1→0xFFFD (PWM sentinel) +0.20 LIN`. PWM dest stored as `kPresetDestPwm=0xFFFD`
  (parallel to `kModDestPitch=0xFFFE`); promote to `mod_matrix.h` in Stage 3c when PWM param added.
- **`engine_set_routings(routings, count)`** added to `synth.h`/`synth.cpp`: builds a `ModMatrix`
  from the array and pushes it to all 8 JunoVoice instances. Called from control thread; audio thread
  picks it up next block (no lock needed — per-voice matrix is written atomically as a struct copy).
- **`ui.cpp`** wired: factory preset cycle loads routings via `preset_factory_routings` + `engine_set_routings`;
  startup init loads INIT routings; save ('=') serializes routings via `preset_serialize` (v2).
- **5 new host tests** (77 total): routing round-trip (all 4 fields), v1 back-compat, INIT Clean 106
  content assertion, factory_routings OOB, zero-routings serialize/parse.
- `make test` ✅ (77/77) `make host` ✅ `make build` ✅ membrane clean.
  App: **966 KB / 2 MB (54% free)** (+1 KB from Stage 3b-i).
- **Next:** Stage 3c-i — full Juno param set as table rows (OSC_PWM + remaining Juno params).

### Seam fix: set_mod_matrix promoted to IVoice (2026-06-28)
- **What:** `engine_set_routings()` in `synth.cpp` was downcasting `IVoice*` → `JunoVoice*` to
  call `set_mod_matrix`. Added `ModMatrix` forward-decl + pure-virtual `set_mod_matrix(const ModMatrix&)`
  to `IVoice`; marked `JunoVoice::set_mod_matrix` `override`; removed the `static_cast` and the
  now-unused `#include "juno_voice.h"` from `synth.cpp`.
- **Why:** Restores ADR 0008 seam ("allocator/matrix only see IVoice") and ADR 0009 model-agnostic
  matrix contract. Found during review of Stage 3b-ii deliverable.
- `make test` ✅ (79/79) `make host` ✅ `make build` ✅. Flash: **856 KB** / DIRAM: **148 KB** (54% free). No behavior change.
- **Next:** Stage 3c-i — full Juno param set as table rows.

## 2026-06-28 — Stage 3c-i: full Juno param set as table rows (COMPLETE)

- **13 new param ids** added (all < kMax=0x80=128): OSC_PWM(0x13), OSC_WAVEFORM(0x14), OSC_RANGE(0x15); HPF_CUTOFF(0x23), VCF_ENV_DEPTH(0x24), VCF_ENV_POLARITY(0x25), VCF_KEY_TRACK(0x26), VCF_LFO_DEPTH(0x27); CHORUS_MODE(0x53); VCA_GATE_MODE(0x61), VCA_LEVEL(0x62); LFO1_DELAY(0x73), LFO2_DELAY(0x7B).
- **kJunoParamCount: 24 → 37**. All ids unique, all < kParamIdMax=128.
- **DSP hooks wired** in `JunoVoice::set_param` + `render()`: OSC_RANGE (freq multiplier), VCF_ENV_DEPTH/POLARITY (ENV2→cutoff ±8 kHz), VCF_KEY_TRACK (key-follow scaling), VCF_LFO_DEPTH (LFO1→cutoff panel mod), VCA_GATE_MODE (gate vs env), VCA_LEVEL (per-voice level), LFO1/2_DELAY (counter fade-in ramp), CHORUS_MODE (0=off mono, 1=I, 2=II).
- **OSC_PWM now exists** (id 0x13, flag MOD_DEST). The `kPresetDestPwm=0xFFFD` sentinel in `preset.cpp` can now be unified — deliberate follow-up, not in scope here.
- **HPF_CUTOFF row exists** (id 0x23, GROUP_HPF=9) — cached only. Split-if hit: adding a real HPF requires a second `dsp::Filter` object in JunoVoice (that's a separate sub-stage). `ui/ui.cpp` group_name() updated with GROUP_HPF="HPF".
- **OSC_WAVEFORM** row exists (stepped, 0=SAW default) — cached only; `dsp/osc.h` supports only SAW; PULSE/TRI is a future osc sub-stage.
- **Factory presets** widened to `[48]`, all 4 updated to 37 params. `test_preset.cpp` buffers widened to 64 for future growth.
- **8 new host tests** in `tests/host/test_params.cpp` (85 total, all pass): table coverage, ID uniqueness+range, kJunoParamCount ≥ 37, OSC_PWM metadata, VCA_LEVEL=0 silences, OSC_RANGE shifts pitch, VCA_GATE_MODE=1 immediate output, VCF_ENV_DEPTH modulates filter.
- `make test` ✅ (85/85) `make host` ✅ `make build` ✅ membrane clean. App: **968 KB / 2 MB (54% free, +2 KB)**.
- **Next:** Stage 3c-ii — complete UI pages so every row is reachable. Also: unify `kPresetDestPwm` sentinel with `ParamId::OSC_PWM` (follow-up); HPF DSP block (separate sub-stage after HPF_CUTOFF row landed).

## 2026-06-28 — Stage 3c-ii: UI pages complete — all 37 rows reachable (COMPLETE)

- **Root cause:** `draw_rows` vertically centred the full row block but did not clip or scroll. The LFO
  group has 8 rows × 56 px = 448 px, which exceeds the 402 px content area — row 8 rendered behind the
  status strip and was never visible.
- **Fix (ui/ui.cpp, draw_rows only):** stateless "follow-selection" scroll. Compute `visible =
  floor(CONTENT_H/ROW_H) = 7`, derive `scroll_top` so the selected row stays centred, clamp to stay
  in-bounds. Pages with ≤7 rows are unaffected (scroll_top=0, block vertically centred as before).
- **All 8 groups have readable page names** (group_name() already covered them all from Stage 3c-i).
- **All 37 rows reachable** on host and device: navigation wraps through all rows/pages; scroll brings
  any row into view. No model knowledge added (ADR 0008 clean). Membrane grep clean.
- Tracked open items (not in scope here): HPF DSP wiring (second dsp::Filter in JunoVoice); unify
  `kPresetDestPwm=0xFFFD` sentinel with `ParamId::OSC_PWM`.
- `make test` ✅ (85/85) `make host` ✅ `make build` ✅. Flash: **968 KB / 2 MB (54% free)**, DIRAM: **149 KB** (unchanged from 3c-i).
- **Next:** Stage 3d-i — play modes (mono/portamento/legato) in the allocator.

## 2026-06-28 — Stage 3d-i: play modes (mono/portamento/legato) in the allocator (COMPLETE)

- **Play modes added to `VoiceAlloc`:** `PlayMode::kPoly` (unchanged), `kMono` (mono+retrigger),
  `kLegato` (mono+legato). Set via `set_play_mode()` / `set_portamento_time()` called each block
  from `synth_render()`. `advance_glide(block_time_secs)` steps the portamento ramp.
- **Priority rule (mono):** last-note priority with steal-back. Fixed 8-slot stack tracks held notes;
  `note_off` pops the released note and re-gates the previous held note (if any).
- **Legato rule:** `kLegato` skips envelope retrigger when a new note arrives while at least one
  note was already held; clean attacks (no held note) retrigger normally. `kMono` always retriggers.
- **Portamento:** `glide_offset_` semitones applied to voice via new `IVoice::set_pitch_offset()`.
  Ramps from `old_effective_pitch − new_pitch` toward 0 at `|offset| / portamento_time_` semi/s.
  Values < 0.001 s treated as zero (snap). Applies on both note-on and steal-back.
- **New `IVoice::set_pitch_offset(float semitones)` method** (8th file: `engine/voice.h`).
  `JunoVoice` stores `p_pitch_offset_`; added to `range_semi` in `render()` so it shifts the base
  freq (and mod ramp). Inline in `juno_voice.h` — no new cpp code.
- **Two new param ids** (AMP group, IDs < kMax=0x80=128, not per-voice):
  `PLAY_MODE = 0x63` (stepped 0=poly/1=mono/2=legato), `PORTAMENTO_TIME = 0x64` (0–2 s, CURVE_LOG).
  Placed in `GROUP_AMP` — the table-driven UI shows them on the AMP page automatically without
  touching `ui/`.
- **kJunoParamCount: 37 → 39.**
- **Factory presets updated** (count 37 → 39): INIT/Pad = poly, no glide; Bass = mono+retrigger,
  0.06 s glide; Lead = mono+legato, 0.08 s glide.
- **6 new host tests** (91 total, all pass): mono single-voice, steal-back, all-off, portamento
  ramp, poly restored after mono, legato transition + gate tracking.
- `make test` ✅ (91/91) `make host` ✅ `make build` ✅ membrane clean. App: **0xece90 ≈ 952 KB / 2 MB (54% free)**.
- Tracked open items unchanged: HPF DSP wiring; `kPresetDestPwm=0xFFFD` sentinel unification.
- **Next:** Stage 3d-ii — unison + on-device CPU gate (🛑 needs Opus + Pascal's hardware).

## 2026-06-29 — Stage 3d-ii: unison voice stacking + detune (COMPLETE — host side)

- **Unison added to `VoiceAlloc`** — NOT a parallel allocator. `set_unison_count(U)` /
  `set_unison_detune(cents)` extend the ONE existing allocator. U voices drawn from the
  fixed pool; effective polyphony = `floor(kNumVoices / U)`; pool exhaustion falls through
  to the normal steal policy; no `malloc`.
- **Group tracking:** per-slot `unison_tag_[kNumVoices]` (pitch of the note, or `0xFF` =
  ungrouped). `note_off` scans by tag and releases all U group members atomically.
- **Detune:** U voices spread symmetrically ±(cents/2)/100 semitones via
  `IVoice::set_pitch_offset()` (reused from 3d-i; no downcast, no `JunoVoice` visibility).
  Spacing: voice `gi/(U-1)` in [0,1] mapped to `[-spread/2, +spread/2]` semitones.
  Glide offset is additive on top for mono+unison.
- **Mono + unison:** U detuned voices for the held note. Steal-back (legato or mono)
  rebuilds the full U-voice group for the previous held note. Portamento glide applies
  to all group members with their per-voice detune added.
- **U=1:** identical to the 3d-i poly/mono/legato path — no code change in that branch.
- **Two new param ids** (AMP group, `< kMax=0x80`):
  `UNISON_COUNT=0x65` (stepped 1..8, def=1), `UNISON_DETUNE=0x66` (0..50 cents, def=7).
  `UNIT_CENT` added to `ParamUnit` enum. **kJunoParamCount: 39 → 41.**
- **Factory presets** (count 39→41): INIT/Bass=U1; Pad=U2/7¢ (fat shimmer);
  Lead=U2/10¢ (thick mono lead). All INIT values match table defaults (test verified).
- **5 new host tests** (96 total, all pass): U=4 stacks 4 voices, detune produces output,
  note_off releases all group voices, U=4 limits polyphony to 2 notes, U=1 unchanged.
- `make test` ✅ (96/96) `make host` ✅ `make build` ✅ membrane clean.
  App: **0xed7d0 ≈ 973 KB / 2 MB (54% free, +21 KB from 3d-i)**.
- Tracked open items: HPF DSP wiring; `kPresetDestPwm=0xFFFD` sentinel unification.
- **Next (blocked by gate):** see 🛑 below — device CPU measurement needed before
  declaring Stage 3 fully done.

## 2026-06-29 — Bug fix: unison U>=3 clipping (COMPLETE)

- **Bug (device-confirmed):** With `UNISON_COUNT >= 3`, the synth audibly distorted/harshly
  clipped. Root cause: U detuned voices summed into the mono bus without any level compensation.
  With `MASTER_GAIN=0.5`, one note at U=3 produced `~3 × 0.5 = 1.5` — exactly `soft_clip`'s
  hard-clamp point → gross clipping. U=1,2 were fine; U=3 broke.
- **Fix (`engine/synth.cpp`):** Added `unison_gain(int count) = 1/sqrt(U)` equal-power
  compensation applied to `MASTER_GAIN` before the per-sample output loop. `unison_count` hoisted
  from its anonymous block to function scope so step 6 can use it without a redundant param read.
  U=1 → factor 1.0 (bit-identical to pre-fix output). U=8 worst-case: `8 × 0.5 × (1/√8) ≈ 1.41 < 1.5` — stays below soft-clip hard-clamp.
- **Test (`tests/host/test_alloc.cpp`):** 5 new unison_gain tests (101 total): U=1=1.0 exact,
  U=4=0.5 exact, monotonically decreasing U=1..8, worst-case U=8 stays below 1.5 ceiling,
  count<1 clamps to 1.0 (no NaN/Inf).
- `make test` ✅ (101/101) `make host` ✅ `make build` ✅ membrane clean.
  App: **0xed830 ≈ 973 KB / 2 MB (54% free)** (unchanged — sqrtf is a single instruction on P4 FPU).
- Note: 1/√U is the standard equal-power default. If Pascal later prefers a different loudness
  curve (e.g. 1/U for maximum headroom), it's a one-line change to `unison_gain()`.

## 2026-06-29 — bench.c now drives real `synth_render` (Stage 3d-ii harness) (COMPLETE)

- **`engine/bench.c`** extended with **Section 3 — real-voice load ramp**. The bench now calls
  `synth_init` + `engine_set_routings` (Clean 106: ENV2→cutoff + LFO1→PWM) and times
  `synth_render()` directly (500-block loop, same t0/t1 cycle-counter as the proxy kernels) for
  `nv=1..8` genuine Juno voices (PolyBLEP saw+sub+noise → SVF → 2×ADSR + 2×LFO + mod matrix).
  A **worst-case line** measures all 8 voices detuned via UNISON_COUNT=8 + CHORUS_MODE=1 on a
  single note. `engine_active_voices()` is printed per row to confirm the correct count is running.
- **Proxy ramp (Section 2) kept** as a labelled comparison baseline; Section 3 is the deliverable.
- `BenchRouting` (C-compatible layout mirror of `mod_matrix.h::Routing`) with `_Static_assert`
  guards against drift; valid cast to `const struct Routing*` for `engine_set_routings`.
  Param drain ordered correctly: one render call flushes UNISON_COUNT before note_on.
- **To run on device:** `make bench-device` then `make sniff` — capture Section 3 rows.
  Host run gives orientation numbers only (pseudo-1GHz ns); device numbers close the gate.
- `make test` ✅ (101/101) `make host` ✅ `make bench` ✅ (host bench runs, Section 3 produces 8+worst-case rows).
- **Next:** Pascal runs `make bench-device` + `make sniff` on the P4; captures real cyc/blk numbers
  for the 🛑 3d-ii gate; Opus ratifies.

## 2026-06-29 — Stage 3 roll-up + session handoff (Opus orchestration)

Stage 3 (Modulation + full Juno) is **code-complete** — all 7 sub-stages landed under the
ADR 0017 orchestrator/worker model (7 fresh-context Sonnet workers; Opus reviewed summaries,
not diffs): 3a mod sources · 3b-i matrix engine · 3b-ii preset v2 + Clean 106 · 3c-i full
param set (41 rows) · 3c-ii UI reaches every row · 3d-i mono/porta/legato · 3d-ii unison.
- **2 gates ratified** (see ✅ below): matrix shape (16 slots, `{source,dest,depth,curve}`)
  and Clean 106 default voicing.
- **1 seam fix caught in review:** an `IVoice→JunoVoice` downcast in `engine_set_routings`
  violated ADR 0008/0009 → promoted `set_mod_matrix` onto the `IVoice` interface; 3d-i/3d-ii
  then reused `IVoice::set_pitch_offset` for glide/detune instead of casting.
- **1 device-reported bug fixed:** unison U≥3 audibly clipped (voices summed with no level
  comp → `3×0.5=1.5` = soft-clip hard-clamp). Fix = equal-power `1/√U` master compensation.
- Tests **52 → 101**; app **~973 KB / 54% free**; every build green throughout.

**THE ONE OPEN ITEM — resume here:** the 🛑 3d-ii CPU gate (below) needs **Pascal's device
bench run**. The bench harness now drives the *real* `synth_render` (not the Stage 0.5 proxy),
and the device image builds (`build/tanmatsu-bench`). Procedure: `make sniff` in terminal A
first, then `make bench-device` in terminal B (interactive — press a key on the device);
capture the Section-3 cyc/blk · %period · verdict table incl. the worst-case U=8 + chorus line.
Opus then confirms within the 70% / 480k-cyc budget — or caps `UNISON_COUNT`/poly — and Stage 3
is **done**.

**Non-blocking debt (Pascal's call on timing):** HPF is a navigable param row but **inert**
(needs a 2nd `dsp::Filter` in `JunoVoice` — its own sub-stage); `kPresetDestPwm=0xFFFD` preset
sentinel can collapse into `ParamId::OSC_PWM` (which now exists since 3c-i).

**After Stage 3:** re-plan Stages 4–7 with Opus (timing/arp/seq/FX, MIDI I/O, library/capture,
2nd engine) — informed by the real CPU headroom the device bench reveals (the per-voice cost
grew a lot vs the Stage 0.5 proxy).

## 2026-06-29 — Two device CPU fixes (Fix A: -O2; Fix B: block-rate SVF cutoff) (COMPLETE)

Root-caused by Opus (bench showed ~131k cyc/blk/voice vs 2.6k for the Stage 0.5 proxy).
- **Fix A:** `sdkconfigs/general` → `CONFIG_COMPILER_OPTIMIZATION_PERF=y` (was debug/-Og).
  `-O2` confirmed in `build/tanmatsu/compile_commands.json` on juno_voice compile unit.
  `sdkconfig_tanmatsu` (generated cache) updated consistently for the current session.
- **Fix B:** `engine/juno_voice.cpp` — `filter_.set_freq(cutoff_end)` moved out of the
  64-sample inner loop to once per block (before the loop, after `set_res()`). Eliminates
  a `sinf`+`powf` per sample per voice. Per-sample osc freq + amp ramps unchanged.
  Cutoff mod bandwidth: 750 Hz at 64/48k — inaudible for Juno filter sweeps.
- All 73 host tests pass unchanged (cutoff-mod-in-voice test asserts RMS change across
  200 blocks — unaffected by block-rate cutoff). Membrane clean.
- `make size`: total image 1,000,970 bytes (~977 KiB; 52% partition free).
- **🛑 3d-ii device CPU gate stays open** — these fixes should each cut voice cost
  substantially, but the actual device numbers require Pascal's `make bench-device` re-run.
  Opus clears the gate after the re-bench confirms the budget.

## 2026-06-29 — Granular CPU bench: Section 4 (fixed-overhead) + Section 5 (per-block micro-bench) (COMPLETE)

- **Section 4** (`engine/bench.c`) — isolates idle `synth_render` cost (param drain + 8-slot
  allocator loop + stereo bus) with no active voices. Three rows: chorus off / Chorus I / Chorus II,
  plus a derived "chorus I cost" diff line. Device run will show actual BBD chorus overhead.
- **Section 5** (`engine/bench_blocks.cpp`, new C++ TU, C-ABI entry `bench_blocks_run()`) — times
  each real DSP building block in isolation: `dsp::Osc`, `dsp::Filter` (block-rate `set_freq`),
  `dsp::Env`, `dsp::Lfo SINE` vs `TRI` (sinf premium visible), `WhiteNoise`, `ModMatrix eval`
  (per-block semantics). Both sections wired into `main/CMakeLists.txt` (BENCH_SRCS) and
  `host/CMakeLists.txt` so `make bench` and `make build BENCH=1` both compile the new file.
- Host Section 5 numbers (orientation only; host ≈ 1GHz ns, device @ 360 MHz matters):
  Osc 616 cyc/blk · Filter(block-rate) 1579 · Env 468 · Lfo-SINE 466 · Lfo-TRI 375 ·
  WhiteNoise 185 · ModMatrix eval 38 cyc/eval.
- `make bench` ✅ `make host` ✅ `make test` ✅ (101/101) `make build` ✅ `make build BENCH=1` ✅.
  App size **unchanged**: 1,000,970 bytes (~977 KiB, 52% free) — bench_blocks.cpp excluded by BENCH guard.
- 🛑 3d-ii gate stays open — device numbers required: `make bench-device` + `make sniff`.

## Open Opus gates
Sonnet appends a 🛑 gate here when a runbook step needs Opus (see `specs/stages/README.md`).
Opus clears the entry when the gate is resolved.

🛑 Stage 3d-ii (unison / voice CPU cost) needs device measurement. The full-featured Juno
  voice (mod matrix + ENV2 + 2 LFOs + portamento + unison detuning via set_pitch_offset) has
  not been benched since Stage 0.5's proxy voice. Unison at U=8 maxes the pool and runs all
  8 real voices per block — worst case CPU. ADR 0003 budget: 480k cyc/blk (P4 @ 360 MHz,
  64/48k); Stage 0.5 proxy was 6.2%. Recommendation: run `BENCH=1` on device, measure 8 real
  Juno voices at steady state, confirm within the 70% ceiling (chorus + fx headroom); cap
  `UNISON_COUNT` max or reduce max polyphony only if it blows. Blocked: declaring Stage 3
  fully done.

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

