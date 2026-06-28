# Stage Runbooks — execution protocol

These are **Opus-authored, source-pinned, sub-staged implementation plans** meant to be
*executed by Sonnet* one sub-stage at a time. The split (why, and which model does what) is
ratified in [ADR 0014](../decisions/0014-model-tier-workflow.md). This file is the protocol;
the per-stage files are the work.

Read order at the start of any execution session: `CLAUDE.md` → `specs/00-overview.md` →
`specs/MEMORY.md` → this file → the stage doc you're on.

## The per-sub-stage loop

A sub-stage (e.g. `1b`) is one Sonnet session — **5–15 files, no compaction**. For each:

1. **Read** the sub-stage entry in the stage doc and any specs/ADRs it links.
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

**When you reach a gate, Sonnet does exactly this:**

1. **Stop** — do not implement past the gated decision.
2. If the build is green, **commit** the work-in-progress so nothing is lost.
3. **Record** the open question in `specs/MEMORY.md` under an **"Open Opus gates"** heading
   (the topic, the stage/sub-stage id, the recommendation, what's blocked on it).
4. **Tell the user**, verbatim shape:
   > 🛑 Step `<id>` (`<topic>`) needs Opus. Recommendation: `<…>`. Switch model and resume.
5. End the turn. Do **not** proceed on a best guess.

When the user returns on Opus, Opus resolves the gate (often by ratifying a spec/ADR),
updates the stage doc to fold the answer in, clears the gate from "Open Opus gates", and
hands the now-unblocked sub-stage back to Sonnet.

### Ad-hoc gates (not pre-listed)

The pre-listed gates are not exhaustive. If Sonnet hits **any** decision that fits
CLAUDE.md's *"When to Ask vs. Decide — Ask"* list (a sonic/architecture choice not in the
specs; vendoring new deps or any GPL code; changing an acceptance criterion; anything that
changes how the synth sounds or plays), treat it as a gate and follow the same protocol —
even though it wasn't written down. When unsure whether something is a gate: it is.

**Decide-with-default (not a gate):** file/module layout, internal data structures, which
test to write, refactors of green code, naming within conventions. Just pick and proceed.
