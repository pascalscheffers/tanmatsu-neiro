# Tanmatsu Synth — Claude Code Guidelines

An analog-modeling (virtual-analog) / hybrid synthesizer for the **Tanmatsu** badge
(Nicolai Electronics / badge.team), built on **ESP-IDF** for the **ESP32-P4**.

> Read this file, then `specs/00-overview.md` and `specs/MEMORY.md` at the start of
> every session. The specs are the source of truth for *what* we're building; this
> file is the source of truth for *how* we build it.

---

## Prime Directives

1. **Reuse before you write.** This is an embedded, memory-limited target. Every line
   of DSP we don't write is a line we don't have to debug, optimize, or fit in flash.
   Prefer vetted, permissively-licensed embedded synth code (see *Code Reuse* below)
   over hand-rolled DSP. Writing a band-limited oscillator from scratch is a last resort,
   not a starting point.
2. **Deduplicate ruthlessly.** Memory is the binding constraint. One oscillator engine,
   not five. One parameter table that drives UI *and* MIDI *and* presets. Shared
   read-only wavetables, never per-voice copies. If a thing exists twice, that's a bug.
3. **Modular and AI-maintainable.** Small files, clear seams, one responsibility each.
   A future session (human or model) should be able to open any file and understand it
   without loading the whole repo into context.
4. **Spec-first.** No non-trivial code without a spec entry it implements. Decisions
   live in `specs/`, not in our heads or in chat scrollback.
5. **The audio thread is sacred.** No allocation, no logging, no blocking, no surprises
   in the real-time path. See *Real-Time Audio Rules*.

---

## Development Workflow

### Spec-first, staged development

Every feature follows this order:

1. **Specify** — Add/update a spec in `specs/`. Include acceptance criteria.
2. **Plan** — For non-trivial work, propose stages and get approval before coding.
3. **Implement** — Minimum code to satisfy the spec.
4. **Verify** — It builds (`make build DEVICE=tanmatsu`); it runs on device or in the
   host test harness; the acceptance criteria are met.
5. **Commit** — One clean commit per stage.
6. **Memorize** — Append progress to `specs/MEMORY.md` so context can be cleared.

### Stages

- A stage is a self-contained unit completable in **one session without compaction**.
- Aim for **5–15 files** changed per stage. Bigger → split into sub-stages (1a, 1b…).
- Stop at a clean point, memorize, and commit what works rather than blowing context.

### Memory protocol (kryten-style: memory lives in the repo)

`specs/MEMORY.md` is the portable, in-repo log of where we are. At the end of every
stage record: what was accomplished, key decisions, current state, what's next. The
repo is the source of truth so progress travels across machines and sessions. Keep it
lean and scannable — link out to spec files rather than restating them.

---

## Architecture & Modularity

The system is layered. **Dependencies point downward only** — a lower layer never knows
about a higher one.

```
  ui/         PAX-graphics screens, parameter pages, input handling
  control/    MIDI in (USB host + device), keyboard-as-keys, preset load/save
  ─────────── (everything above is "the brain": soft-real-time, runs in normal tasks)
  engine/     voice allocator, modulation matrix, the synth as a whole
  dsp/        pure, reusable signal-processing blocks (osc, filter, env, fx)
  ─────────── (everything below is "the heart": hard-real-time audio path)
  platform/   BSP/IDF glue: I2S/codec output, USB, SD, timing
```

- **`dsp/` is pure and portable.** No ESP-IDF, no globals, no I/O. A block in `dsp/`
  takes buffers and parameters in, produces samples out. This is what makes it testable
  on the host and reusable. Vendored MIT code (Mutable Instruments etc.) lives under
  `dsp/vendor/` and is wrapped, not edited in place where avoidable.
- **`engine/` composes `dsp/` blocks into voices** and manages polyphony. A voice owns
  *state*, never its own copy of shared tables.
- **`control/` and `ui/` never touch the audio path directly.** They mutate a shared
  parameter store; the engine reads it. Communication into the audio thread is via
  lock-free single-writer params or a small command ring buffer — never a mutex.
- **One parameter, defined once.** Every tweakable lives in a single declarative
  parameter table (id, name, range, curve, default, MIDI CC, smoothing). The UI renders
  from it, MIDI maps through it, presets serialize it. Adding a knob = one table entry,
  not edits in four files. This is the central dedup mechanism — protect it.

### Replaceability

Define a seam (a struct of function pointers or a C++ interface) only when a second
implementation or host-side testing actually demands it. Start concrete. The audio
output sink, the MIDI transport, and the oscillator model are the seams most likely to
earn abstraction — let need pull them out, don't push them in "just in case."

### No duplication

If logic appears twice, extract it. A small amount of duplication beats a wrong
abstraction — but in *this* repo, memory pressure tips the scale toward extraction
earlier than usual. When unsure, extract.

---

## Code Reuse & Licensing

**Reuse is policy, not preference.** Before writing DSP, check whether one of these has
it (all permissively licensed — see `specs/02-synth-architecture.md` for the map):

- **Mutable Instruments `eurorack` (STM32 parts) — MIT.** `stmlib` (DSP utils, units,
  filters, ring buffers), `plaits`/`braids` (macro-oscillators: VA, wavetable, FM,
  granular). Already ported to other float-DSP MCUs (Daisy). This is our primary engine
  source. Vendor under `dsp/vendor/mi/`.
- **ESP-IDF + `badge-bsp`** — board support: display, input, audio/I2S, power, LEDs.
- **ESP-IDF `esp_tinyusb` / TinyUSB** — USB device (incl. MIDI class). USB host MIDI
  per `specs/03-control-ui.md`.
- **PAX graphics** — all 2D UI rendering. Don't hand-roll drawing.

Licensing rules for anything vendored:

- **Strongly prefer MIT / BSD / Apache-2.0 / CC0.** These compose cleanly and match the
  badge.team / template ethos (template is CC0; MIT recommended for our own code).
- **GPL/LGPL/AGPL: ask before vendoring.** Not banned (this is open hardware, not an App
  Store binary), but it changes the license of anything it touches. A licensing decision
  is a spec decision — record it in `specs/decisions/`. Don't silently pull GPL DSP in.
- **Record every third-party component** (name, version, license, why) in
  `specs/02-synth-architecture.md`'s dependency table. Check transitive deps.
- License our own original code **MIT** unless decided otherwise.

When you vendor code: pin the source commit, note it, and keep local edits minimal and
clearly marked so upstream updates stay tractable.

---

## Real-Time Audio Rules

The audio callback runs on a strict deadline (one block every `BLOCK/Fs` seconds). Miss
it and you get clicks. These rules are not negotiable in the audio path:

1. **No allocation.** No `malloc`/`new`/`free` in or below `engine/process()`. All
   buffers, voices, and tables are preallocated at init. The voice pool is fixed-size.
2. **No blocking.** No mutexes, no `vTaskDelay`, no file I/O, no `ESP_LOG*`, no `printf`
   in the audio path. Pass data in/out via lock-free structures.
3. **Block-based processing.** Process N samples per call (block size in
   `specs/02-synth-architecture.md`), not sample-by-sample, to amortize overhead and
   enable vectorization.
4. **Hot data in internal SRAM, bulk data in PSRAM.** Per-voice state, working buffers,
   and anything touched every block live in internal SRAM. Wavetables/samples live in
   PSRAM (read-only, shared). PSRAM random access is slow — stream/cache, don't thrash.
5. **Float DSP, fixed where it pays.** The P4 has a single-precision FPU; default to
   `float`. Reach for fixed-point only where a profile says it matters.
6. **Profile before optimizing.** Measure the actual block-time cost (cycle counter)
   before rewriting anything for speed. Intuition about hot paths is usually wrong.
   Vectorize / use lookup tables only *after* a profile pins the cost.
7. **Denormals & NaNs kill.** Flush-to-zero where available; sanitize feedback paths
   (filters, delays) with tiny DC offsets or explicit clamps.

---

## Code Conventions

- **Language:** C for BSP/glue, C or C++ for DSP/engine (Mutable code is C++). Pick per
  layer; don't mix styles within a file. Match the surrounding code.
- **Naming:** `snake_case` for C functions/vars, `CamelCase` for C++ types,
  `UPPER_SNAKE` for constants/macros. Prefix public symbols of a module with the module
  name (`osc_`, `voice_`, `param_`).
- **Headers:** one module = one `.h`/`.c(pp)` pair. Header declares the seam; the source
  hides the rest. No business logic in headers.
- **Formatting:** use the repo `.clang-format` (`make format`). Don't hand-format.
- **Error handling:** check `esp_err_t`; fail loud at init, fail safe in the audio path.
- **No magic numbers in DSP** — name them (sample rate, block size, table size) so the
  relationships are visible.

---

## Context Management (file size & shape)

- **No source file over ~500 lines.** Approaching it → split. DSP blocks, engine pieces,
  and UI screens should be small and focused. Sweet spot 150–350 lines.
- **One primary type/responsibility per file.** Tiny closely-related helpers can stay.
- **Many small files over one big file** (kryten convention). Cheaper to read, cheaper
  on context, fewer merge conflicts.
- **Prefer Markdown for specs/notes.** Keep them small and scannable; link, don't repeat.

---

## Git

Local-only for now (prototyping). The template's origin is kept as `upstream-template`
for reference; **there is no push remote** — don't add one without asking.

Commit message format:

```
<type>: <imperative summary, ≤50 chars, no period>

<why, not what — wrap at 72>

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
```

Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `chore`, `test`, `vendor` (importing
third-party code). Keep commits atomic — one logical change each.

---

## Build, Flash, Run

From the repo root (`DEVICE=tanmatsu` is the default target):

- `make prepare` — one-time: clone ESP-IDF v5.5.1 + toolchain into `./esp-idf(-tools)`.
- `make build` — build the app.
- `make flash PORT=/dev/tty.usbmodemXXXX` — flash over USB.
- `make flashmonitor PORT=…` — flash + serial monitor.
- `make menuconfig` — sdkconfig editor.
- `make size` / `make size-components` — track flash/RAM budget (do this often).
- `make format` — clang-format the tree.

Host-side DSP unit tests (the `dsp/` layer is pure, so it compiles and runs on the Mac)
build separately — see `specs/02-synth-architecture.md` once the test harness exists.

---

## When to Ask vs. Decide

**Ask** before: a sonic/architecture decision not in the specs; vendoring GPL code or any
new dependency; changing an acceptance criterion; anything that changes how the synth
sounds or plays in a way the specs don't cover.

**Decide yourself:** file/module naming and layout; which test to write; whether to
refactor green code; internal data structures. Follow the conventions; don't ask about
them.

When uncertain, prefer a 30-second question over a 30-minute wrong turn — but record the
answer in a spec so it's only asked once.

---

## Naming (kryten convention)

Name things deliberately. The synth, its modes, presets, and internal codenames are
worth a moment's thought — lean on music history, the classic synths we're modeling, and
Pascal's literary/aviation interests over generated-sounding names. A name with a story
beats a clever one. Capture naming candidates in a spec note if the discussion runs long.
