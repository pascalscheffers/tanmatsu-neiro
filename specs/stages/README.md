# Execution protocol — work-orders, dispatch, gates

These are **Opus-authored, source-pinned, sub-staged implementation plans** *executed by a
fresh-context Sonnet worker* one sub-stage at a time. The methodology — Opus orchestrates,
fresh-context workers execute closed work-orders — is ratified in
[ADR 0017](../decisions/0017-orchestrator-worker-methodology.md) (which amends
[ADR 0014](../decisions/0014-model-tier-workflow.md)). This file is the protocol; the
per-stage files are the work. **The same work-order template governs ad-hoc and debug tasks**
that Opus judges "big enough" to dispatch — not just numbered stages.

Read order at the start of any execution session: `CLAUDE.md` → `specs/00-overview.md` →
`specs/MAP.md` (the seam index — jump there before searching) → `specs/MEMORY.md` → this
file → the stage doc / work-order you're on.

> **`specs/MEMORY.md` is the live log; older entries live in `MEMORY-archive.md`.** Read only
> the **last 1–2 entries** plus the **"Open Opus gates"** heading if present — that is the live
> state. Pull from the archive only with a specific reason. Same for the big specs: open the
> **linked section**, not the whole file. (First line of defense against compaction — see below.)

## The work-order (the contract a worker receives)

Every dispatched task — a stage sub-stage **or** an ad-hoc/debug job — is handed to the worker
as a **closed work-order**: enough to execute from the doc + the named reads alone, and a hard
boundary on what to open. If it can't be executed from the work-order, the work-order is
incomplete — Opus fixes it; the worker does not improvise scope.

| Field | What it pins |
|---|---|
| **Touch list** | The ≤ ~8 files the worker may create/edit, named. |
| **Read list** | The exact `file:section` / `file:symbol` to open — *and nothing else*. |
| **Reuse** | Named existing symbols to build on (e.g. `engine/command_queue.h` SPSC ring), so the worker doesn't re-derive them. |
| **Don't-read** | Explicit anti-context: e.g. no DaisySP source, no other stage doc, no `managed_components/`. |
| **Acceptance** | The verifiable stop condition (builds/tests green, behaviour, membrane clean). |
| **Split-if** | The tripwire that forces a stop *before* writing code (see budget rule). |

**Debug variant** adds two fields up front: **Repro** (the failing observation + how to
trigger it) and **Root cause** (Opus's diagnosis — the worker implements the fix, it does not
re-investigate). Acceptance = repro no longer reproduces + a regression test.

**Hard budget rule (authoring time):** one work-order is **≤ ~8 touched files AND
≤ ~5 read-sections**. If a deliverable exceeds either, **Opus splits it before dispatch**
(the Stage 2d lesson — split at authoring, which is cheap judgment, not mid-execution, which
is expensive panic).

## Dispatch — orchestrator + worker

Opus stays the single interactive session and orchestrates; it does **not** implement
non-trivial work in its own context. Per work-order:

1. **Opus dispatches** a fresh-context worker with the work-order as a self-contained prompt
   (Agent tool, `model: sonnet` for implementation; `model: haiku` for the Explore/triage
   tier). The worker starts empty — it sees only the work-order, never Opus's conversation.
   **Match effort to the task** per the tier/effort grid in
   [ADR 0017](../decisions/0017-orchestrator-worker-methodology.md): effort is set per call
   when fanning out via a Workflow (`{model, effort}`); a single Agent-tool worker inherits
   the session effort, so wrap it in a one-item Workflow only when a different effort pays.
2. **The worker runs the per-sub-stage loop below**, then **returns only a summary** — what
   landed, the `make size` number, any gate hit, what's next. Its greps, file reads, and build
   logs stay quarantined in its context.
3. **Opus reviews the summary, not the diff** (it pulls the diff only if a summary smells
   wrong). This is what keeps the orchestrator's context flat across a whole stage.
4. On a gate, the worker returns the gate to Opus (see the gate protocol) — Opus resolves or
   escalates, then re-dispatches.

> Interactive model-switch (run the worker loop yourself after switching to Sonnet) is the
> **fallback** for exploratory work needing live steering — not the default (ADR 0017).

## The per-sub-stage loop (what the worker executes)

A sub-stage (e.g. `1b`) is one worker session — within the work-order budget (≤ ~8 files,
≤ ~5 read-sections), **no compaction**. For each:

1. **Read** only the work-order's read list (the sub-stage entry + the specs/ADRs/sections it
   names). Honour the Don't-read list. Need to find something not on the list? Use **Explore**
   so its output stays out of your context — don't widen your own reads.
2. **Implement** the minimum code to satisfy it. Reuse before writing (CLAUDE.md). Match
   surrounding style; keep files < ~350 lines.
3. **Verify** — all of these must pass before you commit:
   - `make host` green **and** `make build` green (both targets always stay buildable).
   - `make test` green (host DSP tests) once the test harness exists (stood up in 1a).
   - The sub-stage's own **acceptance criteria** in the stage doc.
   - **Membrane grep** still clean — no `esp_`/`bsp_`/`SDL`/`miniaudio` above `platform/`
     (ADR 0007). DSP stays pure (no ESP-IDF, no I/O, no globals).
4. **Commit** — one atomic commit, CLAUDE.md message format.
5. **Memorize** — append a short entry to `specs/MEMORY.md` (what landed, sizes from
   `make size`, decisions, what's next). Update the running budget table in `specs/02`.
6. **Next** sub-stage, or stop at the stage's end.

If a sub-stage is getting too big, split it (`1b` → `1b-i`, `1b-ii`) and stop at a clean,
committed point rather than blowing context (CLAUDE.md staging rule).

## Keep the session small — fit one sub-stage without compacting

A sub-stage **must complete in a single context window with no compaction.** Compaction
mid-stage is a failure signal: the sub-stage was too big, or context was spent on the wrong
things. (Stage 2d compacted — that was too large; that's the bar we're now under.) Defenses,
in order of impact:

1. **Read narrow, not whole.** Open the linked spec *section*, not the file. Read the last
   1–2 `MEMORY.md` entries, not the log. Don't re-read a file you just edited — the tools
   track its state.
2. **Delegate searches to a sub-agent.** "Where is X / what calls Y / does Z exist" → the
   **Explore** agent. Its file dumps stay out of your context; you get back the conclusion.
   This is the single biggest saver when a sub-stage touches unfamiliar code.
3. **Stay within the work-order budget** (≤ ~8 touched files, ≤ ~5 read-sections, ~one
   feature). If the work-order implies more — a vendor import *and* wiring *and* UI, two
   unrelated concerns — that is an authoring miss: the budget rule splits it *before* dispatch
   (`2d` → `2d-i`, `2d-ii`). If you discover it mid-work, stop at a clean commit and return
   "this needs splitting" to the orchestrator rather than pushing through to compaction.
4. **Write the `MEMORY.md` entry tight.** A dozen scannable lines: what landed, sizes, key
   decisions, what's next. Link to specs/ADRs; don't restate them. A bloated entry costs the
   *next* session too (it gets read in).
5. **Don't paste large output into context.** Build/test logs: act on the failure line, not
   the whole transcript. `make size`: record the one number, don't echo the table.

If you feel context filling before the sub-stage's acceptance criteria are met: **stop at the
last green commit, record the remainder in `MEMORY.md`, and hand the rest to a fresh session.**
A clean stop is always better than a compaction.

## 🛑 OPUS GATE — escalation protocol

Some decisions are **not Sonnet's to make**: anything that changes how the synth *sounds*,
the shape of an architecture seam, a licensing call, a persisted-data format, or the CPU
budget. Per the user's standing choice, gates are a **hard stop** — do not guess past one.

Each stage doc lists its gates up front in a table and marks them inline in this format:

```
🛑 OPUS GATE — <short topic>
  Why Opus: <sonic | architecture | licensing | data-format | CPU-budget>
  Decision: <the precise question to resolve>
  Recommendation: <Opus's default, so the user can confirm in one word>
  Sonnet action: STOP.
```

**When you reach a gate, the worker does exactly this:**

1. **Stop** — do not implement past the gated decision.
2. If the build is green, **commit** the work-in-progress so nothing is lost.
3. **Record** the open question in `specs/MEMORY.md` under an **"Open Opus gates"** heading
   (the topic, the stage/sub-stage id, the recommendation, what's blocked on it).
4. **Return the gate to the orchestrator** as the worker's summary, verbatim shape:
   > 🛑 Step `<id>` (`<topic>`) needs Opus. Recommendation: `<…>`. Blocked: `<what>`.
5. End the turn. Do **not** proceed on a best guess.

Opus (the orchestrator, already the right tier) resolves the gate — usually by ratifying a
spec/ADR — updates the stage doc/work-order to fold the answer in, clears the gate from
"Open Opus gates", and **re-dispatches** the now-unblocked sub-stage to a fresh worker. (In
the interactive fallback, this is where the user switches back to Opus instead.)

### Ad-hoc gates (not pre-listed)

The pre-listed gates are not exhaustive. If Sonnet hits **any** decision that fits
CLAUDE.md's *"When to Ask vs. Decide — Ask"* list (a sonic/architecture choice not in the
specs; vendoring new deps or any GPL code; changing an acceptance criterion; anything that
changes how the synth sounds or plays), treat it as a gate and follow the same protocol —
even though it wasn't written down. When unsure whether something is a gate: it is.

**Decide-with-default (not a gate):** file/module layout, internal data structures, which
test to write, refactors of green code, naming within conventions. Just pick and proceed.
