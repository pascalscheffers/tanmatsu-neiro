# ADR 0014 — Opus plans, Sonnet executes, gates escalate

**Status:** accepted (2026-06-28)

## Context
This synth is built almost entirely by AI sessions. The model tiers differ in cost and in
judgment: the larger model (Opus) is the right tool for *deciding* — sonic character,
architecture seams, licensing, persisted-data formats, the CPU budget — while a cheaper,
fast model (Sonnet) is well-suited to *executing* a sufficiently specified plan. Running
every keystroke on the top tier wastes tokens; running open-ended design on the cheap tier
risks wrong-but-plausible turns the specs can't catch.

We already work spec-first with a hard "Ask vs. Decide" boundary (CLAUDE.md). The gap was
that the boundary lived in prose and assumed one model. We want the bulk of Stages 0.5–3
done cheaply without losing the high-judgment calls.

## Decision
**Opus authors durable, source-pinned, sub-staged runbooks under `specs/stages/`; Sonnet
executes them sub-stage by sub-stage; the runbooks embed explicit 🛑 OPUS GATE markers and
Sonnet hard-stops at each, asking the user to switch back to Opus.**

- The runbook **is the contract**: each sub-stage names the files, the reuse target,
  acceptance criteria, and its gates. If it can't be executed from the doc alone, the doc
  is incomplete — fix the doc, don't improvise in code.
- A **gate** is any decision on CLAUDE.md's "Ask" list (sonic / architecture / licensing /
  data-format / CPU-budget). At a gate Sonnet stops, commits green WIP, records the open
  question in `MEMORY.md` ("Open Opus gates"), and tells the user to switch models. It does
  **not** proceed on a guess. Protocol + marker format: [`specs/stages/README.md`](../stages/README.md).
- **Unanticipated** decisions matching the "Ask" list are treated as ad-hoc gates the same
  way, even if the runbook didn't list them. "When unsure whether it's a gate: it is."
- Opus resolves a gate by ratifying the relevant spec/ADR and folding the answer into the
  runbook, then hands the unblocked sub-stage back.

## Consequences
- Bulk implementation runs at low cost; Opus time is spent on judgment and authoring, not
  transcription. This is the project's main token-efficiency lever.
- Escalation points are visible *before* work starts (each stage's gate table), so the user
  can anticipate when an Opus session is needed.
- The runbooks are real repo artefacts (reviewable, diffable, outlive any chat), consistent
  with spec-first. They can drift from reality as earlier stages teach us things — so a
  stage is re-read (and may be amended by Opus) at the moment it's picked up, not trusted
  blindly months later.
- Residual risk: a gate missed at authoring time. Mitigated by the ad-hoc-gate clause and
  by the "when unsure, it's a gate" default — the failure mode is an *extra* stop, not a
  silent wrong turn.
- Stage 0.5's profiling output (the CPU budget) is itself a gated input to Stage 1, so the
  first real DSP is sized against measured silicon, not a guess.
