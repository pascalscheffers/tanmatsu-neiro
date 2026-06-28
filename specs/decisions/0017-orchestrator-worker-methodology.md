# ADR 0017 — Orchestrator/worker fan-out is the default build methodology

**Status:** accepted (2026-06-28) · **Amends:** [ADR 0014](0014-model-tier-workflow.md)

## Context
ADR 0014 split the work by tier (Opus plans, Sonnet executes) but assumed the worker was an
*interactive* Sonnet session reached by switching the model. That model has a flaw we hit in
practice: the execution session pays a fixed bootstrap tax every time (all of CLAUDE.md +
`MEMORY.md` + the stage doc, read into context), then does its own searching and reading on
top — so context fills and the session slows or compacts. Stage 2d compacted this way.

Two facts reframe the fix:
- The bloat is *authored and read in*, not a property of the tree. Our own source is small
  (~4 KLoC); the bulk on disk (`managed_components/`, ESP-IDF, `miniaudio.h`) is never loaded
  unless something reads it. Restructuring the tree would change nothing.
- An **Agent/Task subagent starts with a fresh, empty context window** — it sees only the
  prompt it is handed, never the parent's conversation or file reads, and only its final
  summary returns to the parent. That is precisely the focused, disposable worker we want.

## Decision
**Opus-orchestrated, fresh-context Sonnet-worker fan-out is the standard way this project is
built** — for planned stages *and* for "big enough" ad-hoc work, including debugging. Opus
stays the single interactive session, orchestrates, and keeps only work-orders + tight
summaries in context; the implementation lives in disposable worker contexts.

**The altitude call (Opus, per task).** For every piece of work Opus decides:
- **Inline** — trivial/single-file edits, docs/spec changes, exploration/understanding, or
  anything ambiguous or needing tight human steering: Opus does it in this session.
- **Spec-and-dispatch** — a stage sub-stage, a multi-file feature, or a bug whose fix is
  understood and mechanical enough to specify: Opus authors a closed work-order and dispatches
  a worker. **Litmus test:** if the work can be written as a closed work-order
  (touch list / read list / reuse / acceptance), it should be — and that specifiability *is*
  the signal it's worker-ready. If it can't be specified yet, Opus investigates first (inline
  or via an **Explore** subagent so the dumps stay out of context), *then* specifies, *then*
  dispatches.

**The loop.** Opus authors a closed work-order (template + budget rule in
[`specs/stages/README.md`](../stages/README.md)) → dispatches a Sonnet worker (Agent tool,
`model: sonnet`) with that work-order as a self-contained prompt → the worker implements,
verifies (`make host`/`make build`/`make test` green + membrane grep clean), commits on
green, appends a tight `MEMORY.md` entry, and **returns only a summary** (what landed, the
`make size` number, gate hits, next) → Opus reviews the **summary, not the diff** (pulling the
diff only if a summary smells wrong). That review-the-summary discipline is what keeps the
orchestrator's context flat across many tasks.

**Gates without a model-switch.** A worker cannot switch its own model. On a 🛑 OPUS GATE (or
an ad-hoc gate per the "Ask" list) it **stops, commits green WIP, records the open question in
`MEMORY.md` under "Open Opus gates", and returns the gate to Opus** — who, already the right
tier, resolves it (ratify a spec/ADR, amend the work-order) or escalates to the user, then
re-dispatches. This replaces ADR 0014's "tell the user to switch models" step and is cleaner:
the decider is already in the room.

**Debugging.** Diagnosis/root-causing is Opus-level (or delegated to Explore). Once root cause
and fix shape are known and the fix is non-trivial, Opus writes a **debug brief** (the
work-order template + **Repro** and **Root cause** up front; acceptance = repro fixed +
regression test) and dispatches. Small fixes stay inline.

**Fallback.** Interactive model-switch (ADR 0014's mechanism) is retained for genuinely
exploratory work where live steering matters — but it is the exception, not the default.

## Consequences
- The orchestrator's context stays roughly constant across a whole stage: it accumulates
  work-orders and summaries, never file-dumps or build logs. This is the project's main
  context-efficiency lever, superseding ADR 0014's "switch model" mechanism.
- Each worker gets a full, fresh window for one bounded job, so the per-sub-stage compaction
  failure mode (2d) largely disappears — *provided* the work-order obeys the budget rule
  (≤ ~8 touched files / ≤ ~5 read-sections). Splitting moves to authoring time (Opus
  judgment, cheap) instead of mid-execution panic (expensive).
- Tradeoffs, stated honestly: **no mid-flight human steering** of a worker (mitigated by tight
  work-orders + the gate-return path); and a too-big work-order can still exhaust even a fresh
  window (mitigated by the budget rule and "split before dispatch").
- Work-orders are repo artefacts (the stage docs and, for ad-hoc work, the dispatched prompt
  recorded in the `MEMORY.md` entry), consistent with spec-first. They drift, so a stage is
  re-read and may be amended by Opus at pickup.
- Residual risk unchanged from ADR 0014: a gate missed at authoring. Same mitigation — the
  ad-hoc-gate clause and "when unsure, it's a gate"; the failure mode is an extra stop, not a
  silent wrong turn.
