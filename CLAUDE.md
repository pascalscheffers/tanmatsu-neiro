# Tanmatsu Synth — Claude Code Guidelines

An analog-modeling (virtual-analog) / hybrid synthesizer for the **Tanmatsu** badge
(Nicolai Electronics / badge.team), built on **ESP-IDF** for the **ESP32-P4**.

> Read this file, then `specs/00-overview.md`, `specs/MAP.md` (the seam index — jump there
> before searching the tree), and `specs/MEMORY.md` (live log; older entries in
> `MEMORY-archive.md`) at the start of every session. The specs are the source of truth for
> *what* we're building; this file is the source of truth for *how* we build it.
>
> **How we build this project (default):** Opus orchestrates; fresh-context Sonnet *workers*
> execute closed **work-orders**, one bounded job each, and return a summary. See
> *Development Workflow* below; methodology in **ADR 0017** (amends 0014), work-order template
> + gate protocol in `specs/stages/README.md`. Build/run reference: `specs/09-build-and-run.md`.

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

### The standard methodology: orchestrate, then dispatch (ADR 0017)

The bulk of this project is built by **Opus orchestrating fresh-context Sonnet workers**, not
by Opus writing code in its own session. The reason is context economy: a worker subagent
starts empty (it sees only its prompt, never the orchestrator's conversation) and returns only
a summary, so Opus can run a whole stage while keeping file-dumps and build logs out of its
context. This is the project's main defense against the "rapid start, steady slowdown" that
context bloat causes.

**The altitude call (Opus decides, per task):**

- **Inline** — trivial/single-file edits, docs/spec changes, exploration, or anything ambiguous
  or needing tight human steering: do it in this session.
- **Spec-and-dispatch** — a stage sub-stage, a multi-file feature, or a bug whose fix is
  understood and mechanical enough to specify: author a closed **work-order** and dispatch a
  Sonnet worker. **Litmus test:** *if it can be written as a closed work-order, it should be* —
  that specifiability is the signal it's worker-ready. If it can't be specified yet, investigate
  first (inline or via an **Explore** subagent so the dumps stay out of context), *then*
  specify, *then* dispatch.

**The loop:** Opus authors the work-order → dispatches a worker (Agent tool, `model: sonnet`
for implementation, `model: haiku` for Explore/triage; effort matched to the task per ADR 0017) →
the worker implements, verifies, commits on green, appends a tight `MEMORY.md` entry, and
returns a **summary** → Opus reviews the summary (not the diff) and re-dispatches the next.
Template, budget rule (≤ ~8 files / ≤ ~5 read-sections), and the gate protocol live in
[`specs/stages/README.md`](specs/stages/README.md). **Debugging** follows the same loop with a
debug brief (repro + root cause up front); small fixes stay inline.

Every dispatched job still follows: **Specify** (the work-order) → **Implement** (minimum code,
reuse first) → **Verify** (`make build`/`make host`/`make test` green, acceptance met,
membrane clean) → **Commit** (one atomic commit) → **Memorize**. Interactive model-switch is a
fallback for exploratory work, not the default.

### Memory protocol (kryten-style: memory lives in the repo)

`specs/MEMORY.md` is the portable, in-repo log of where we are — the **live** file holds the
last few entries + any open gates; older history is rotated into `specs/MEMORY-archive.md`. At
the end of every dispatched job record: what was accomplished, key decisions, current state,
what's next. The repo is the source of truth so progress travels across machines and sessions.
Keep it lean and scannable — link out to spec files rather than restating them.

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

**Reuse is policy, not preference** (Prime Directive 1). Before writing DSP, check the
permissively-licensed sources first — Mutable Instruments (`stmlib`/`plaits`/`braids`),
DaisySP, ESP-IDF + `badge-bsp`, TinyUSB, PAX graphics. The full map (what each provides, where
it's vendored) is the dependency table in [`specs/02-synth-architecture.md`](specs/02-synth-architecture.md).

Rules (always apply):

- **Prefer MIT / BSD / Apache-2.0 / CC0.** **GPL/LGPL/AGPL: ask before vendoring** — it's not
  banned (open hardware), but it changes the license of what it touches; record the call in
  `specs/decisions/`. Don't silently pull GPL in.
- **Record every third-party component** (name, version, license, why) in spec 02's dependency
  table; check transitive deps. License our own original code **MIT**.
- When you vendor: pin the source commit, note it, keep local edits minimal and marked.
- **Upstream the platform, don't fork it.** A bug/gap/perf problem in PAX / badge-bsp / the
  launcher → fix it upstream and flag Pascal, not a silent local workaround (profile before
  claiming "slow"). Fixes live as **tracked patches** under `upstream-patches/`, committed to
  git and re-applied to the build tree by `make patches`/`make build`. Policy + mechanism:
  [`specs/07-upstream-contributions.md`](specs/07-upstream-contributions.md) and
  `upstream-patches/README.md`.

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
7. **Denormals & NaNs kill — and the P4 has no hardware flush-to-zero.** RISC-V `RV32F`
   has no FTZ/DAZ bit, so denormal suppression is **mandatory in software**, not "where
   available": sanitize every filter/feedback path (filters, delays, reverb) with a tiny
   DC offset / `+1e-20f` or explicit clamps, and sanitize NaN/Inf before output. Host DSP
   tests run with FTZ *disabled* to match the device. See ADR 0012.
8. **Optimize for the P4; the simulator adapts.** When a representation (buffer/pixel
   format, layout, alignment, endianness, fixed vs float) favors one side, the shared code
   uses the P4-optimal form and `platform/host/` does any conversion. The device never
   burns cycles for the host's convenience. See `specs/decisions/0011-...`.

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

**Auto-commit in logical chunks.** Don't wait to be asked. As soon as a coherent unit of
work is complete and verifies (a sub-stage, a self-contained doc/spec change, a vendored
component, a green refactor), commit it. Prefer several small atomic commits over one large
one — each commit is one logical change that builds/passes on its own. Still **never push**
(no remote) and never commit broken/unverified work or secrets. When several unrelated
changes are staged together, split them into separate commits.

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

Everyday loop (`DEVICE=tanmatsu` is default; full reference — device flash, AppFS, serial
capture, bench — in [`specs/09-build-and-run.md`](specs/09-build-and-run.md)):

- `make host` / `make test` — build+run the host target / host DSP unit tests (the fast loop).
- `make build` — build the device app. `make install` + `make run` — upload to AppFS and launch.
- `make size` — track flash/RAM budget (do this often). `make format` — clang-format the tree.
- `make sniff` — capture device serial (console is USB-Serial-JTAG; numbers shift on reboot).

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
