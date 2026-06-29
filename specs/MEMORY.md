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

## 2026-06-29 — perf: LFO block-rate advance (one sinf/block) (COMPLETE)

- **Root cause (Opus-profiled on device, -O2):** `dsp::Lfo` SINE mode cost 11,552 cyc/blk
  (180 cyc/smp — one `sinf` per sample). Two SINE LFOs per Juno voice → ~23k cyc/blk/voice,
  ~42% of per-voice cost. But all 63/64 intermediate `sinf` results were discarded; only the
  final per-block value (`lfo1_value_`/`lfo2_value_`) was ever used by the mod-matrix eval.
- **Fix:** `dsp::Lfo::process_block(uint32_t n)` added (advances phase by `n * phase_inc_`,
  wraps with S&H re-latch on cycle boundaries, evaluates waveform once). `juno_voice.cpp`
  `render()`: removed per-sample `lfo1_.process()`/`lfo2_.process()` calls from the inner loop;
  replaced with `lfo1_.process_block(n)` / `lfo2_.process_block(n)` called once after the loop.
  LFO delay-position counters advanced by `n` (block-granular, inaudible). `env2_.process(gate_)`
  remains per-sample (cheap linear state machine, exact transitions required). No audio change.
- **Expected drop:** ~23k → ~2k cyc/blk per LFO pair/voice; voice budget expected to drop
  from ~55k → ~32k cyc/blk. Requires Pascal's `make bench-device` re-run to confirm (Round B
  of the 🛑 3d-ii gate).
- **1 new host test** (102 total): `process_block(64)` matches 64-step per-sample reference
  (same phase position, value within 1e-5 for SINE). All 102 pass.
- `make test` ✅ (102/102) `make host` ✅ `make build` ✅ membrane clean (touched only
  `dsp/lfo.h`, `engine/juno_voice.cpp`, `tests/host/test_mod_sources.cpp`).
- `make size`: Flash 893 KB / DIRAM 145 KB (25.3%, 430 KB free) — **unchanged** (no new data).
- 🛑 3d-ii gate stays open pending Pascal's re-bench. Round B (change-gated param push,
  `engine/synth.cpp`) is the queued follow-up after the gate clears.

## 2026-06-29 — perf: change-gate per-block param push (COMPLETE)

- **Root cause (Opus-profiled, device):** `synth_render` step 3 called `voice->set_param()` for
  32 params × 8 voices = 256 calls every block unconditionally — including idle voices and settled
  params. Several setters recompute transcendentals (`FILTER_CUTOFF`→`sinf+powf`, each ADSR
  time→`expf+logf`). ~53k cyc/blk fixed overhead in steady state.
- **Fix:** `ParamStore::drain()` now tracks changed params per block (new target arrived OR smoother
  still converging). First `drain()` after `init()` force-marks all valid params dirty so voices
  receive initial values. `synth_render` step 3 loops only over `changed_count()` ids → steady state
  pushes nothing; a knob sweep pushes only that param. All voices (active + idle) still receive the
  push so newly triggered voices always hold current values.
- **New members:** `changed_ids_[kParamIdMax]`, `changed_count_`, `force_all_dirty_` — all fixed-size,
  no alloc. Snap-to-target on settle eliminates asymptotic crawl.
- **4 new host tests** (89 total, all pass): first-drain all-dirty, second-drain zero, param_set →
  id in changed list, smoothed param drops from changed list after settling.
- `make test` ✅ (89/89) `make host` ✅ `make build` ✅ membrane clean.
  `make size`: Flash 641 KB .text · DIRAM 145 KB (25.2%) · total image ~977 KB (52% free) — unchanged
  (tracking arrays live in existing struct padding zone).
- 🛑 3d-ii gate stays open pending Pascal's re-bench. This completes Round A+B of the queued
  CPU optimization pair — expected device steady-state overhead to drop from ~53k → near-zero cyc/blk.

## 2026-06-29 — 🛑 Stage 3d-ii CPU gate RATIFIED — Stage 3 COMPLETE (Opus 4.8)

Device re-bench (`make bench-device`) confirms the full Juno voice fits **8 voices + unison +
chorus** with large headroom. Numbers + journey recorded in
[`stage-3d-ii-results.md`](stage-3d-ii-results.md).
- **Section 3:** 8 voices = **243 790 cyc/blk = 50.8%**; worst case (U=8 + Chorus I, one note)
  = **50.8%** too. Per-voice ≈ **27.5k**, fixed intercept ≈ **23.5k**. ~19 pts under the 70%
  safe ceiling — room for Stage 4 FX + UI jitter.
- **Section 4:** fixed overhead 53k → **22.3k** (Round B); chorus confirmed ~free (9–12 cyc).
- **Arc:** per-voice 131k → 55k (-O2 + block-rate filter) → 27.5k (Round A LFO + Round B param
  push) = **4.75×**. No sonic change in any of the four fixes.
- **Verdict:** ADR 0003 (8 voices + unison) **stands, device-proven** — no poly/unison cap
  needed. The 🛑 gate below is **cleared**. Stage 3 (Modulation + full Juno) is **done**.
- **Next:** re-plan Stages 4–7 with Opus against the measured headroom (~27.5k/voice, ~22k
  fixed ≈ 49% of budget free at full load). Non-blocking debt still open: inert HPF row (needs a
  2nd `dsp::Filter` in `JunoVoice`); `kPresetDestPwm=0xFFFD` sentinel → `ParamId::OSC_PWM`.

## 2026-06-29 — fix: shared free-running LFO (ADR 0018) — stale per-voice LFO phase bug CLOSED

**Bug closed.** Root cause was per-voice LFO phase freezing on idle and resuming stale on
note reuse; UNISON=8 made it audible as beating between voices.

**Fix:** Moved `dsp::Lfo` from per-voice to a single shared pair (`s_lfo1`, `s_lfo2`) in
`engine/synth.cpp` (authentic Juno-106: one global LFO, all voices in lock-step). The engine
advances both LFOs unconditionally once per block and injects the block-end raw values into
every voice via new `IVoice::set_lfo_inputs(float, float)`. **Per-note delay fade-in stays
per-voice** (per-note `lfo*_delay_pos_` counter, reset on `note_on`). Decision ratified by
Pascal before dispatch; frozen in ADR 0018 (`specs/decisions/0018-shared-free-running-lfo.md`).

**Files changed:** `engine/voice.h` (new virtual), `engine/juno_voice.h` (removed `lfo1_`/
`lfo2_` members + rate/shape caches, added `lfo1_raw_`/`lfo2_raw_`), `engine/juno_voice.cpp`
(removed LFO init/reset/set_param cases, swapped `lfo*.process_block()` for `lfo*_raw_` in
render), `engine/synth.cpp` (added `s_lfo1`/`s_lfo2`, init from defaults, rate/shape config
in changed-param loop, inject before voice render loop), `tests/host/test_mod_sources.cpp`
(replaced oscillation/waveform/rate voice tests with injection/depth/delay/determinism tests).

- `make test` ✅ (102/102) `make host` ✅ `make build` ✅ membrane clean.
  Flash 893 KB / DIRAM 145 KB — unchanged (fewer per-voice LFO objects, two new shared ones).
- **Next:** re-plan Stages 4–7 (non-blocking debt still open: inert HPF row, `kPresetDestPwm`
  sentinel).

## 2026-06-29 — investigation: residual per-note variation is free-running osc, NOT a bug

After the LFO fix, Pascal still heard each note sound slightly different (esp. UNISON=8 attack
"phasing"). Suspected another un-reset "decay counter". Built a host **bisection harness**
(measurement spike, since reverted — not committed) that diffed the same note rendered with
different prior voice histories, neutralising one state source at a time:

- **Oscillator phase is ~100% of it.** Resetting osc phase alone cut history-dependent divergence
  40× (RMS 0.542 → 0.013). **ENV2 hard-retrigger, filter SVF reset, and zeroing the cached
  `env2_value_` each changed divergence by ~0%** — the "decay counter" hypothesis is disproven.
  (INIT ENV2 has sustain 0 / short times, so it reaches idle before a voice frees.)
- **Decision (Pascal):** keep oscillators **free-running** (authentic DCO drift; `note_on` does
  NOT reset osc phase). The residual note-to-note variation is *intended character, not a bug* —
  **do not re-investigate.** If tighter unison is ever wanted, the only effective lever is
  resetting osc phase on note_on (all-zero, or a per-unison `i/U` spread).
- Side findings (left as-is, both negligible): `JunoVoice::reset()` (steal path) does **not**
  clear the filter SVF state (~4e-4 RMS residual); `daisysp::WhiteNoise` PRNG has no reset
  (noise is noise). `daisysp::Adsr::Retrigger(true)` *does* preserve ADSR times (verified) — keep
  in mind if a deterministic env restart is ever needed.

## 2026-06-29 — Stage 3 closed; Stage 4 campaign brief authored (RESUME HERE)

Stage 3 is done (voice + mod + play modes + unison, all ratified) and the post-fix
non-determinism investigation is closed (free-running osc, by design — entry above). **Next
campaign = Stage 4 (Timing, Arp, Sequencer, FX).** Opus authored the campaign map at
[`stages/stage-4-timing-arp-seq-fx.md`](stages/stage-4-timing-arp-seq-fx.md): sub-stage
decomposition (4a clock+scheduler → 4b arp → 4d FX → 4c sequencer), seams, and **five kickoff
gates (G1–G5)** to resolve with Pascal before authoring work-orders — incl. the 🛑 reverb
device-CPU profile (ADR 0015) and the sequencer data-model/storage format (extends spec 05).
**On resume:** read that brief + `specs/06` + ADR 0010 + ADR 0015 + `stage-3d-ii-results.md`,
run G1–G4 with Pascal, then dispatch the **4a** work-order (everything hangs off the clock).

## 2026-06-29 — Stage 4 KICKOFF: G1/G2/G4 ratified; 4a decomposed; 4a-i dispatched (Opus 4.8)

Stage 4 (Timing/Arp/Seq/FX) kicked off. Kickoff gates run with Pascal — recorded in
[`stages/stage-4-timing-arp-seq-fx.md`](stages/stage-4-timing-arp-seq-fx.md):
- **G1 ✅** order = **4a → 4b (arp) → 4d (FX) → 4c (seq)**, each sub-stage completed fully in turn.
- **G2 ✅** **full arp** (up/down/up-down/order/random, octaves 1–4, clock-div rate, gate, swing, latch).
- **G4 ✅** reverb algo = **DaisySP `ReverbSc`** (reuse); device-CPU profile still gates 4d commit (ADR 0015).
- **G3 ⏳** sequencer data-model/storage gate **deferred to 4c authoring** (heavy data-format decision).
- **G5** UI render-path profile scheduled when 4b/4c UI work begins.

**4a split into 3 tight work-orders** (≤8-file budget): **4a-i** clock core + transport API
(header-only `engine/clock.h`, reused `SpscRing<ClockCmd>`, no CMake ripple) → **4a-ii** event
scheduler → **4a-iii** clock params in table + UI. **4a-i dispatched** to a fresh Sonnet worker.
- **Next:** review 4a-i summary; dispatch 4a-ii (scheduler), then 4a-iii (params/UI), then 4b (arp).

## 2026-06-29 — Stage 4a-i: master clock core + transport API (COMPLETE)

- **`engine/clock.h`** (new, header-only, pure): `Clock` class at 96 PPQN. `double` accumulator
  for long-run drift-free tick counting; `uint64_t` for monotonic counters. Transport
  `start()`/`stop()`/`cont()`; free-running `free_pos_` (never reset, used for tap timing).
  Tap tempo: two-tap interval → BPM; 2 s fence resets the sequence; [20–300] BPM plausibility
  guard. No platform deps; no globals.
- **`struct ClockCmd`** in `clock.h`: `{type:u8, arg:float}` — mirrors `NoteCmd` pattern.
- **`engine/synth.cpp`**: `static Clock s_clock` + `static SpscRing<ClockCmd,16> s_clock_cmds`.
  `synth_init` calls `s_clock.init(sr)`. `synth_render` drains `s_clock_cmds` (same pattern as
  NoteCmd drain) then calls `s_clock.advance(frames)` once per block; tick count unused for now
  (4a-ii scheduler will consume it).
- **`engine/synth.h`**: 5 control-thread setters (`engine_set_bpm`, `engine_transport_start/stop/
  continue`, `engine_tap_tempo`) + 3 read-only helpers (`engine_clock_running/tick_pos/bpm`).
- **11 new host tests** (113 total, all pass): SPT math, 64-frame block accumulation, transport
  gating, BPM clamping, tap tempo exact BPM, implausible-gap restart.
- `make test` ✅ (113/113) `make build` ✅ membrane clean (no `esp_`/`bsp_`/`SDL`/`miniaudio`
  above `platform/`). `make size`: DIRAM **146 748 B (25.46%)**, Flash .text 641 KB.
- **Next:** Stage 4a-ii — event scheduler (tick-timestamped events dispatched sub-block into the engine).

## 2026-06-29 — Stage 4a-ii: event scheduler (COMPLETE)

- **`engine/scheduler.h`** (new, header-only, pure): `ScheduledEvent {sample_time:u64, cmd:NoteCmd}`;
  `Scheduler<Cap=64>` — fixed array, no alloc. `schedule()` stores; `dispatch_due(now, frames, fn)`
  finds earliest due events in `[now, now+frames)` in ascending sample_time order, calls `fn(cmd, offset)`,
  removes dispatched. Late events clamp to `offset=0`. O(Cap²) — correct and simple; Cap is small.
  Header-only confirmed — no CMake ripple.
- **`engine/synth.cpp`**: `static Scheduler<64> s_sched` + `static SpscRing<ScheduledEvent,64> s_sched_in`.
  `synth_render` now captures `block_start = s_clock.sample_pos()` BEFORE `s_clock.advance()`, drains
  `s_sched_in` into `s_sched`, then calls `dispatch_due(block_start, frames, fn)` — sub-block offset
  computed but `(void)offset` per ADR 0010 (block-granular dispatch; splitting deferred).
- **`engine/synth.h`**: `engine_schedule_note(sample_time, pitch, velocity, on)` — lock-free
  control-thread API; mirrors `engine_note_on/off` pattern via `s_sched_in.push()`.
- **6 new host tests** (119 total, all pass): basic dispatch + correct offsets across blocks,
  out-of-order → ascending sort, late-event offset=0 clamp, Cap limit returns false + no corruption,
  `clear()` empties pending, dispatched events removed (second call fires nothing).
- `make test` ✅ (119/119) `make host` ✅ `make build` ✅ membrane clean.
  `make size`: **1,002,042 bytes total image (~979 KB, 52% partition free)** — negligible delta.
- **Next:** Stage 4a-iii — clock params in the table + UI (BPM/swing visible and editable on device).

## 2026-06-29 — Stage 4a-iii: CLOCK_BPM as a table param (COMPLETE)

- **`engine/param_id.h`**: CLOCK group 0x00–0x0F; `CLOCK_BPM = 0x01` (0x00 reserved/invalid).
- **`engine/param_desc.cpp`**: one new row (GROUP_GLOBAL, "Tempo"/"BPM", [20..300], def 120, CURVE_LIN, UNIT_NONE, "%.0f", cc=0xFF, smoothing=0 instant, flags=0). **kJunoParamCount: 41 → 42.**
- **`engine/synth.cpp`**: `synth_init` seeds `s_clock.set_bpm()` from the table default; changed-param `switch` drives `s_clock.set_bpm(val)` on `CLOCK_BPM` change. `engine_set_bpm()` (tap-tempo/ClockCmd path) left as-is — both paths remain valid.
- **`engine/preset.cpp`**: `CLOCK_BPM = 120.0` added to all 4 factory presets; count 41 → 42. Array `ids[48]` had 7 spare slots — no widening needed.
- **`ui/ui.cpp`**: `group_name(GROUP_GLOBAL)` → `"Clock"`; the dynamic page builder surfaces the Tempo control automatically — a new "Clock" page is reachable with the BPM knob.
- **`tests/host/test_params.cpp`**: CLOCK_BPM coverage assertion added to `test_params_table_coverage`; new `test_params_clock_bpm_row` asserts group/curve/min/max/def/smoothing. 120/120 tests pass.
- `make test` ✅ (120/120) `make host` ✅ `make build` ✅ membrane clean.
  Image: **1,002,470 bytes (~979 KB, 52% partition free)** — +428 bytes vs 4a-ii (one row + switch case + string).
- **Stage 4a COMPLETE.** Next: Stage 4b — arpeggiator (clock-driven, table-param rate + mode).

## 2026-06-29 — ADR 0019 (note-gen seam); 4b decomposed + 4b-i dispatched (Opus 4.8)

**Seam ratified (ADR 0019):** note generators (arp 4b, seq 4c) run **engine-side on the audio
thread**, driven from `synth_render` (observe NoteCmd drain → read clock → push to scheduler).
Overrides the brief's tentative control-layer placement. Pascal confirmed.

**4b (full arp, G2) split into 3 work-orders:** **4b-i** pure arp core (header-only `engine/arp.h`:
held set + modes up/down/up-down/order/random + octaves + latch + `next()`; timing-free) →
**4b-ii** arp params + UI → **4b-iii** synth wiring (step timing from clock + RATE @ 96 PPQN,
gate/swing, schedule note on/off into the scheduler). **4b-i dispatched.**
- **Next:** review 4b-i; dispatch 4b-ii then 4b-iii; then 4d (FX: delay → ReverbSc w/ device gate).

## 2026-06-29 — Stage 4b-i: arpeggiator pure core (COMPLETE)

- **`engine/arp.h`** (new, header-only, pure): `Arp` class + `ArpMode` enum + `ArpNote` struct.
  Fixed-size (`kMaxHeld=16`, `kMaxOctaves=4`). No alloc, no logging, no platform deps.
- **Modes:** kUp/kDown (sorted ascending, forward/backward traversal of expanded list);
  kUpDown (ping-pong, endpoints NOT repeated, period 2L-2 for L>1; L=1 stays at 0);
  kOrder (as-played insertion order); kRandom (deterministic 64-bit LCG, Knuth MMIX coefficients,
  same seed+chord → same sequence after `clear()`).
- **Octaves:** outer dimension; expanded list length L = `held_count * octaves`. All base notes
  at octave 0, then +12, etc. Out-of-range stacking clamps to [0,127].
- **Latch semantics:** latch ON keeps released notes in the held set; first physical key-down
  after full release (physical_count_ 0→1) clears the held set for a fresh chord. Latch OFF
  with no physical keys down: clear the held set immediately (notes stop).
- **step_ management:** kUp/kDown/kOrder carry step_ in [0,L); kUpDown in [0,2L-2);
  kRandom: step_ unused. `remove_held` keeps step_ in range after shrinking.
- **14 new host tests** (134 total, all pass): kUp wrap, kDown wrap, kUpDown period+endpoint rule,
  kOrder as-played, octaves=2, kRandom determinism, empty→invalid, latch retain, latch fresh-chord
  on new key, latch-off clears, dynamic mid-pattern removal, kUpDown L=1, velocity preserved,
  pitch clamped at 127.
- `make test` ✅ (134/134) `make host` ✅ `make build` ✅ membrane clean (no `esp_`/`bsp_`/SDL/alloc/I/O).
- `make size`: Flash **1,002,470 bytes (52% partition free, UNCHANGED)** — arp.h not yet
  included by any shipped TU.
- **Next:** Stage 4b-ii — arp params in the table + UI (ARP_MODE, ARP_RATE, ARP_OCTAVES,
  ARP_LATCH, ARP_GATE, ARP_SWING, ARP_ON).

## 2026-06-29 — Stage 4b-ii: arp params + table + UI (COMPLETE — finished inline by Opus)

- **7 ARP param rows** added (ids 0x08–0x0E, all < kMax): `ARP_ON/MODE/RATE/OCTAVES/GATE/SWING/LATCH`.
  New `GROUP_ARP = 10`; `ui.cpp group_name()` → "Arp" (dynamic page builder surfaces the page).
  **kJunoParamCount: 42 → 49.** ARP_RATE stepped 0..5 → {1/4,1/8,1/8T,1/16,1/16T,1/32} @ 96 PPQN, def 3 (1/16).
- **`PRESET_BLOB_MAX` 384 → 512** (no `PRESET_FORMAT_VERSION` change — buffer ceiling only; worst-case
  49 params + 16 routings = 466 B). FactoryPreset `ids/vals` arrays widened `[48] → [64]`.
- **All 4 factory presets** carry the 7 arp params, **arp OFF** (ARP_ON=0) so existing patches sound
  identical until enabled; counts 42 → 49.
- **Test_params.cpp:** ARP coverage in `test_params_table_coverage` + new `test_params_arp_rows`
  (group/curve/range/defaults). **139 tests pass** (was 134).
- **Process note:** the dispatched 4b-ii worker died mid-run (API connection drop) after editing
  param_id/desc/preset headers but BEFORE the factory-preset values, ui.cpp, tests, or commit. Opus
  verified the partial edits were correct and finished the remainder inline (factory values + counts,
  ui group name, tests) rather than re-dispatching half-done work.
- `make test` ✅ (139) `make host` ✅ `make build` ✅ membrane clean. Image **0xf5090 ≈ 980 KB (52% free)**.
- **Next:** Stage 4b-iii — wire arp into `synth_render` (clock-driven steps, ARP_RATE→ticks, gate/swing,
  schedule note on/off into the 4a scheduler). First point the arp makes sound.

## 2026-06-29 — Stage 4b-iii: arp wired into synth_render (COMPLETE)

- **`engine/arp_clock.h`** (new, header-only, pure): `arp_rate_ticks(index)` maps ARP_RATE
  0–5 → {96,48,32,24,16,12} PPQN ticks, out-of-range clamps to 24 (1/16). `ArpPhaseResult
  arp_advance_phase(phase*, frames, period)` advances the free-running step phase by one
  block; fires when phase crosses zero, returns offset within block + rolls phase forward.
  Zero/negative period never fires. No platform deps.
- **`engine/arp.h`**: zero-init `sorted_pitches[]` to silence device GCC
  `-Werror=maybe-uninitialized` (semantically no-op; `build_sorted` always fills it).
- **`engine/synth.cpp`** wired (ADR 0019 engine-side pattern):
  - *Step 1 (note drain):* reads `ARP_ON` before the loop; when on, routes note-on/off to
    `s_arp` (held set) instead of `s_alloc` directly. Direct path byte-identical when off.
  - *Step 1a:* `block_start` switched from `s_clock.sample_pos()` → `s_clock.free_pos()`.
    The scheduler now dispatches in free_pos units — arp fires regardless of transport state.
    Comment updated. All downstream scheduler code unchanged.
  - *Step 2b (new):* when `ARP_ON`, configures `s_arp` from fresh params, derives
    `step_period = arp_rate_ticks(ARP_RATE) * samples_per_tick()`, first-note aligns phase
    to 0, calls `arp_advance_phase`. On fire: `s_arp.next()` → schedules note-on at
    `block_start+offset+(swing*0.5*period if odd step)` and note-off at `on+gate*period`
    (force ≥1 sample). When `ARP_OFF`: calls `s_arp.clear()` and resets phase/step so
    toggling back on starts fresh.
- **Free-running arp + free_pos scheduler clock:** this is a **play-feel decision Opus
  made** (ADR 0019) — standard hardware-arp behaviour (plays on note-hold, not on
  transport). Conventional and reversible; noted here as Opus's call.
- **8 new host tests** (7 arp_clock + zero period). `make test` ✅ (146/146) `make build` ✅.
  `make size`: application.bin **0xf59b0 = 1,005,630 bytes total image (~981 KB, 52% free)**.
- **Stage 4b COMPLETE.** Next: **Stage 4d — FX: tempo-synced delay → DaisySP ReverbSc
  w/ device-CPU gate** (ADR 0015 / G4 ratified in 4b kickoff).

## 2026-06-29 — fix: UI page list capped at 8 hid the Arp page (Opus, inline)

- **Bug (Pascal-reported):** only the "Clock" page was visible, not "Arp". Root cause: `ui.h`
  `page_groups[8]` + the `num_pages < 8` guard in `ui_init` capped the page list at 8, but the
  table now has **10 ParamGroups**. CLOCK_BPM sits early in the table so its page made the cut;
  GROUP_ARP (appended at table end) was the 9th/10th group → dropped past the cap → never rendered.
- **Fix:** `page_groups[8] → [16]`; guard now bounds on `sizeof(page_groups)` (self-sizing, no
  magic 8). Tab bar renders all groups. `make host`/`make test` green (146).
- Found while answering "how do I trigger the arp" — the arp page existed in the table all along.

## 2026-06-29 — Stage 4 PAUSED after 4b; Stage 5 (MIDI I/O) brief authored (RESUME HERE)

Arp ratified by Pascal on device ("they're all good, keep it like this") — free-running arp +
latch + all modes confirmed. **Pascal's call: pause Stage 4, pivot to MIDI I/O.**
- **Stage 4 done:** 4a (clock/scheduler/BPM) + 4b (full arp). **Deferred (still roadmap):**
  4d FX (delay → `ReverbSc`, G4 ratified, device gate pending) and 4c sequencer (G3 open).
- **Stage 5 (MIDI I/O) campaign brief authored:** [`stages/stage-5-midi-io.md`](stages/stage-5-midi-io.md).
  Sub-stages 5a HAL-MIDI-seam + RtMidi host + note normalization (foundation, host-first to dodge
  the USB-host driver risk) → 5b USB-A host MIDI (the risk: P4 driver feasibility, gate G1 spike) →
  5c expression/CC map (bend/mod/AT/sustain/panic, CC→param via `ParamDesc.midi_cc`) → 5d USB-C
  device (TinyUSB) → 5e SMF player (reuses the 4a scheduler) → (5f) MIDI-clock-in + the
  `CLOCK_SOURCE` param deferred from 4a-iii. **6 kickoff gates G1–G6** (driver feasibility, HAL seam
  shape, note-event model, MPE scope, CC/MIDI-learn, host dep) to run with Pascal before work-orders.
- **Context will be cleared next.** On resume: read order is in the brief's header; **first action =
  G1 driver-feasibility spike + G2/G3, then author + dispatch the 5a work-order.**

## 2026-06-29 — Stage 5a-i: pure incremental MIDI parser + host tests (COMPLETE)

- **`control/midi_in.h`** (new): `MidiMsgType` enum (NOTE_OFF/NOTE_ON/CC/OTHER), `MidiMsg`
  struct (type/channel/data1/data2), `MidiParser` state struct. C-linkage-safe (`extern "C"` guard).
  No engine/platform/io deps — `<stdint.h>/<stdbool.h>` only.
- **`control/midi_in.c`** (new, 157 lines): incremental byte-stream parser. Handles running
  status (status preserved across data pairs), interleaved System Real-Time bytes (0xF8–0xFF
  transparently discarded, parser state undisturbed), System Common (0xF0–0xF7 resets running
  status). Correct data-byte counts (2 for NoteOff/NoteOn/PolyAT/CC/PitchBend; 1 for ProgChange/ChanAT).
  **Normalization:** Note-On velocity=0 emitted as `MIDI_NOTE_OFF` (standard MIDI). Channel
  preserved (0–15), omni handling deferred to 5a-ii+.
- **`tests/host/CMakeLists.txt`**: `project` widened to `C CXX` (needed to compile the `.c` source);
  `midi_in.c` added to the `tanmatsu-tests` source list; `test_midi_parse.cpp` registered.
- **`tests/host/main.cpp`**: `test_midi_parse_suite()` declared + called.
- **7 new host tests** (153 total, all pass): Note-On, Note-Off, vel-0→NoteOff normalization,
  running status (3 consecutive messages, channel preserved), CC framing, RT byte interleaved
  mid-message, 1-byte Program Change followed by fresh Note-On (stream stays locked).
- `make test` ✅ (153/153) `make format` ✅ membrane clean (no `engine/`/`platform/`/`synth.h`
  includes in `control/midi_in.*`).
- **Next:** Stage 5a-ii — HAL transport seam + RtMidi host backend + engine wire-in (note events
  from MIDI parser routed to `engine_note_on/off/cc`). This is the first sub-stage that touches
  platform code.

## 2026-06-29 — Stage 5a-ii: MIDI HAL seam + RtMidi host backend + engine wire-in (COMPLETE)

- **`platform/platform.h`**: `platform_midi_read(buf, max_len) → size_t` seam added (in-only raw
  byte stream; comment updated to replace the "intentionally absent" note with the Stage 5a/5d arc).
- **`platform/host/midi_host.cpp`** (new, C++): RtMidi backend. Lazy-init on first `platform_midi_read`
  (static `RtMidiIn*` + `tried/ok` flags); try/catch on construction + open; no port → silent, any
  exception → silent and permanent (a missing MIDI device never crashes or blocks). getMessage loop
  drains the internal queue each poll, appending bytes to caller buf. `extern "C"` linkage.
- **`platform/device/platform_device.c`**: no-op `platform_midi_read` stub appended (USB transport
  arrives in Stage 5d/5b). 6 lines, pure C.
- **`control/midi_router.h/c`** (new): owns a file-static `MidiParser`; `midi_router_init()` inits it;
  `midi_router_poll()` drains `platform_midi_read` into the parser and dispatches NOTE_ON/NOTE_OFF to
  `engine_note_on/off` (omni — channel ignored). CC/expression deferred to Stage 5c. No test build
  contamination: `midi_router.c` is NOT in the test build (only `midi_in.c` is).
- **`app/app.c`**: `midi_router_init()` beside `keyboard_init()`; `midi_router_poll()` once per frame
  after the `platform_poll_event` while-loop.
- **`host/CMakeLists.txt`**: `pkg_check_modules(RTMIDI REQUIRED rtmidi)` + include/lib dirs + `${RTMIDI_LIBRARIES}`
  (mirrors SDL2 pattern); `midi_host.cpp`, `midi_in.c`, `midi_router.c` added to `HOST_SRCS`;
  `-framework CoreMIDI` in `if(APPLE)` block.
- **`main/CMakeLists.txt`** (device): `midi_in.c` + `midi_router.c` added to `SRCS`.
  (This was not listed in the work-order touch-list but was required to satisfy the device linker —
  `app.c` calls `midi_router_init/poll`, which the device build must compile. Reported as scope
  clarification.)
- `make test` ✅ (153/153, parser tests unaffected). `make host` ✅ `make build` ✅.
  `make size`: Flash 895 KB / DIRAM **151 KB (26.2%)** — small delta from `midi_in.c` + router
  (device-only; RtMidi is host-only, zero device flash delta for that library).
  `make format` ✅ (reformatted `midi_host.cpp` — within touch list; no out-of-touch-list files changed).
- Membrane clean: RtMidi included only in `midi_host.cpp`; `midi_router.c` includes only
  its own header + `midi_in.h` + `platform.h` + `synth.h`.
- **Stage 5a COMPLETE** (5a-i parser + 5a-ii HAL + router). Next: Stage 5c (expression/CC: bend/mod/AT/
  sustain/panic; CC→param via `ParamDesc.midi_cc`), then 5d (USB-C device / TinyUSB out path).

## 2026-06-29 — Stage 5d: USB-C MIDI device (TinyUSB) + PHY swap (COMPLETE — build-verified)

- **`platform/device/midi_usb_device.h/c`** (new, ~100 lines after format): PHY swap
  (`usb_serial_jtag_ll_phy_select(1)` via `hal/usb_serial_jtag_ll.h`) followed by
  `tinyusb_driver_install()` with a Full-Speed MIDI config descriptor. Strings:
  Manufacturer="Nicolai Electronics", Product="Tanmatsu Synth". Real `platform_midi_read`
  uses `tud_midi_stream_read()` — raw bytes, de-packetized by TinyUSB, fed directly to the
  5a incremental parser. All USB/TinyUSB/hal symbols confined to this file (membrane clean).
- **`platform/device/platform_device.c`**: removed no-op `platform_midi_read` stub;
  added `#include "midi_usb_device.h"` + `midi_usb_device_init()` call at end of
  `platform_init()` (after display/input/audio — so the 500 ms PHY disconnect delay fires
  last).
- **`main/idf_component.yml`**: added `espressif/esp_tinyusb: "^1.1"` (resolved to 1.7.6~2).
- **`sdkconfigs/tanmatsu`** (extra file beyond the 7-file touch list, required): added
  `CONFIG_TINYUSB_MIDI_COUNT=1` — this is the IDF defaults mechanism; without it cmake
  reconfigures with 0. `sdkconfig_tanmatsu` (gitignored full config) also shows `=1` after
  reconfigure. The TinyUSB Kconfig maps `CONFIG_TINYUSB_MIDI_COUNT → CFG_TUD_MIDI`.
- **`main/CMakeLists.txt`**: `midi_usb_device.c` added to device SRCS.
- **Build delta (vs pre-5d):** `0xf5b70` → `0xfc860` (+27,888 bytes ≈ +27 KB) for
  esp_tinyusb 1.7.6~2 + TinyUSB 0.19.0~3. Flash 923 KB / DIRAM 153 KB (26.5%); 51% partition
  free. Plenty of headroom.
- `make build` ✅ `make test` ✅ (153/153 host tests unaffected) `make format` ✅ `make size` ✅.
- **Enumeration is Pascal's hardware check.** USB-C console is detached after init (accepted for
  AppFS apps). On flashing, Mac should show a new USB MIDI device ("Tanmatsu Synth");
  any DAW or MIDI monitor app should be able to receive notes.
- **Remaining Stage 5:** 5b USB-A host MIDI (G1 spike: P4 driver feasibility), 5c CC/expression.

## 2026-06-29 — Stage 5d FIX: boot hang (wrong USB OTG port — HS vs FS)

- **Symptom (device):** synth hung on startup after 5d — `midi_usb_device_init()` (last call in
  `platform_init()`) blocked.
- **Root cause:** esp_tinyusb's `TINYUSB_RHPORT` Kconfig **defaults to HS (OTG2.0) on the
  ESP32-P4**, but our USB-C PHY swap (`usb_serial_jtag_ll_phy_select(1)`) routes the shared
  **full-speed** PHY to **OTG1.1**. So `tinyusb_driver_install` brought up the wrong/unready
  HS controller (the USB-A host side) and blocked.
- **Fix:** `CONFIG_TINYUSB_RHPORT_FS=y` in `sdkconfigs/tanmatsu`. **Gotcha:** IDF defaults only
  fill *missing* keys — the stale generated `sdkconfig_tanmatsu` had HS baked in from the first
  reconfigure, so it had to be deleted to re-apply defaults (`rm sdkconfig_tanmatsu && make build`).
- Hardened `midi_usb_device_init()`: a `tinyusb_driver_install` failure now logs + returns
  (fail-safe) instead of `ESP_ERROR_CHECK` aborting boot — the synth must still play if USB MIDI
  fails (and the swap detaches the console, so a panic would be invisible).
- `make build` ✅; generated config confirms `CONFIG_TINYUSB_RHPORT_FS=y`. Live enumeration is
  Pascal's hardware re-check.

## 2026-06-29 — Stage 5b-i: USB-A host MIDI bring-up spike (COMPLETE — build-verified)

- **`platform/device/midi_usb_host.h/c`** (new, CC0 vendored + adapted): USB Host Library
  class driver spike for USB-A MIDI enumeration and raw-packet logging. SPDX:
  `Unlicense OR CC0-1.0`; provenance: Wunderbaeumchen99817/esp-idf (PR #12566 fork),
  retrieved 2026-06-29.
- **MIDIStreaming interface matching:** `find_midi_interface()` walks the config descriptor
  and matches `bInterfaceClass==0x01` (Audio) AND `bInterfaceSubClass==0x03`
  (MIDIStreaming); claims its bulk IN endpoint. Named constants for both magic bytes.
- **USB-A VBUS:** `bsp_power_set_usb_host_boost_enabled(true)` called first in
  `midi_usb_host_init()` — without this nothing enumerates. Logs + continues if BSP call
  fails (VBUS may be externally powered).
- **P4 peripheral map:** `peripheral_map = 0` (default) → `USB_DWC_HS` = USB-A OTG HS
  controller (confirmed via `USB_DWC_LL_GET_HW(0) → USB_DWC_HS` in
  `hal/esp32p4/include/hal/usb_dwc_ll.h`). Independent of the USB-C Full-Speed PHY used
  by Stage 5d TinyUSB. No `peripheral_map` special-casing needed.
- **Task lifecycle:** host-lib daemon task (installs `usb_host_install`, pumps
  `usb_host_lib_handle_events`) + class-driver task (registers client, pumps
  `usb_host_client_handle_events`). Semaphore synchronization: class task blocks until
  lib is installed. Both pinned to core 0, modest stack/priority (control-plane).
- **Spike behaviour:** `ESP_LOGI` VID/PID on connect; hex-dump raw USB-MIDI 4-byte event
  packets in transfer callback. Does NOT touch `platform_midi_read`, the parser, or the
  engine — that is 5b-ii.
- **`platform/device/platform_device.c`**: `#ifdef SYNTH_USB_HOST_DEBUG` →
  `midi_usb_host_init()`; `#else` → `midi_usb_device_init()` (Stage 5d). Both headers
  included unconditionally so both compile in all builds.
- **`Makefile`**: `USBHOST_DEBUG=1` switch injects `-DSYNTH_USB_HOST_DEBUG=1` via
  `IDF_PARAMS`; builds into separate `build/tanmatsu-usbhost/` dir (cmake cache isolation).
- **`main/CMakeLists.txt`**: `midi_usb_host.c` added to device SRCS; `usb` added to
  `PRIV_REQUIRES`; `if(SYNTH_USB_HOST_DEBUG)` propagates C macro via COMPILE_OPTIONS.
- **Build results:**
  - `make build USBHOST_DEBUG=1` ✅ spike build: 1,084,076 bytes (48% partition free)
  - `make build` ✅ normal build: 1,032,792 bytes (51% free, unchanged path)
  - `make test` ✅ (153/153 — touched no host/test/parser code)
  - `make format` ✅ (only touch-list files affected)
  - Flash delta (USB Host Library): **+51,284 bytes (~50 KB)** spike vs normal build.
  - Membrane clean: no `usb_host*` symbols in `control/`, `engine/`, `ui/`, `app/`.
- **Next:** 5b-ii — Pascal's hardware check (make sniff — see hardware steps below), then
  de-packetize 4-byte USB-MIDI event packets → `platform_midi_read` coexistence with 5d
  device. Also still open: 5c expression/CC map.

**Hardware verification steps (Pascal):**
1. `make build USBHOST_DEBUG=1 && make install USBHOST_DEBUG=1 APP_SLUG=synth-usbhost && make run APP_SLUG=synth-usbhost`
2. In another terminal: `make sniff` (reads all USB-A modem ports)
3. Plug a class-compliant USB-A MIDI controller into the Tanmatsu USB-A port
4. Watch for `I (midi_usb_host): Device connected: VID=... PID=...` + `Found MIDIStreaming`
5. Play some notes; watch for `I (midi_usb_host): MIDI rx [N bytes]: 09 90 3C 64 ...`
   (4-byte USB-MIDI event packets in hex)

## Open Opus gates
Sonnet appends a 🛑 gate here when a runbook step needs Opus (see `specs/stages/README.md`).
Opus clears the entry when the gate is resolved.

*(none open — Stage 3d-ii CPU gate cleared 2026-06-29; see entry above and `stage-3d-ii-results.md`.)*

✅ Stage 3d-ii (unison / voice CPU cost) — **RATIFIED 2026-06-29 (Opus 4.8)**. Device bench:
  8 voices + worst-case unison+chorus = 50.8% of the 480k-cyc budget (per-voice ~27.5k, fixed
  ~22k) after four transparent perf fixes (-O2 build, block-rate SVF cutoff, block-rate LFO,
  change-gated param push). ADR 0003 stands; no cap needed. Numbers: `stage-3d-ii-results.md`.

<!-- Historical gate text (resolved) retained for context:
🛑 Stage 3d-ii (unison / voice CPU cost) needs device measurement. The full-featured Juno
  voice (mod matrix + ENV2 + 2 LFOs + portamento + unison detuning via set_pitch_offset) has
  not been benched since Stage 0.5's proxy voice. Unison at U=8 maxes the pool and runs all
  8 real voices per block — worst case CPU. ADR 0003 budget: 480k cyc/blk (P4 @ 360 MHz,
  64/48k); Stage 0.5 proxy was 6.2%. Recommendation: run `BENCH=1` on device, measure 8 real
  Juno voices at steady state, confirm within the 70% ceiling (chorus + fx headroom); cap
  `UNISON_COUNT` max or reduce max polyphony only if it blows. Blocked: declaring Stage 3
  fully done.
-->

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

