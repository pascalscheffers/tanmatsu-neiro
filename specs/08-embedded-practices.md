# Embedded Practices

How we get hardware/software-team rigor on a hobby budget. The premise: most "embedded
best practice" is about *not being able to see inside a running deadline* and *not having
the target on your desk*. We already have the two biggest wins — a pure portable core
behind a thin HAL (ADR 0007) and a dual host+device build from one tree. This spec captures
the rest, sized for a one-person open-source project: no MISRA, no commercial HIL rigs,
just the cheap habits with high payoff. Adopt incrementally; none of it blocks Stage 1.

Related: real-time rules live in `CLAUDE.md`; denormals in ADR 0012; memory placement in
ADR 0013 + spec 02; upstreaming in spec 07.

## See inside the deadline (on-target measurement)
"The audio thread is sacred" and "profile before optimizing" are aspirational until there's
a number. Cheapest high-value habit:
- A tiny **audio-stats** struct, written by the audio task and read (lock-free) by the UI
  core: cycle-counter delta per block via `esp_cpu_get_cycle_count()` → **% of the
  1.33 ms budget** (64 frames @ 48 kHz), a **DMA-underrun counter**, and the audio task's
  **stack high-water** (`uxTaskGetStackHighWaterMark`).
- Surface it on a debug UI page (or serial). This turns "is the budget blown?" into a glance.
- **Land it in Stage 1** with the first real voice — sized from data, not guessed. The
  Stage 0 audio task stack is 4096 B (fine for a sine); MI `plaits` + filters will grow it,
  and the watchpoint-based stack-overflow detector only catches the last 32 bytes.

## CI without hardware
A push-triggered pipeline (GitHub Actions or a local pre-commit) that needs **no board**:
1. Build host (CMake).
2. Build device (Espressif IDF docker image, pinned to v5.5.1).
3. Run host DSP unit tests.
4. `clang-format` check (the binary lives at `/opt/homebrew/opt/llvm/bin` — see MEMORY).
5. Optional: `clang-tidy` / `cppcheck` static analysis.

Targets borrowed from firmware CI norms: reproducible build < 5 min, host unit tests < 30 s.
Device validation stays a **manual smoke checklist per stage** (audio clean on speaker +
headphones, UI presents, no panic) — the right altitude for a hobby project; skip HIL rigs.

## Golden-file audio regression tests
Spec 04 promises an **offline-render mode** (run `synth_render` faster than real time to a
WAV). Use it as a gate: lock **reference WAVs / FFT spectra** per DSP block and diff in CI.
This makes "A/B vs reference Juno samples" a regression test rather than a vibe — and it is
where the **FTZ-off** host setting (ADR 0012) catches device-only denormal regressions.
Acceptance criteria per DSP block (aliasing floor, filter response, envelope shape) become
the golden checks.

## Turn on the safety nets
Cheap insurance for a device flashed and crashed repeatedly during development:
- **Coredump to flash** — `idf.py coredump-info` / `coredump-debug` give crashed-task
  backtraces and a GDB session **without JTAG**. (Keep the coredump stack ≥ ~1300 B.)
- **Task watchdog** + **stack-overflow detection** (watchpoint mode) enabled in dev builds.
- **Heap integrity checks** at "Light impact" in debug builds (`heap_caps_check_integrity_*`)
  to localize corruption early. (The audio path stays heap-free — RT rules — but UI/MIDI/SD
  don't.)
- Fail loud at init, fail safe in the audio path (already the policy; the `to_i16` NaN/Inf
  guard is the audio-path instance).

## Reproducibility
- Commit `dependencies.lock`; keep spec 02's dependency ledger honest (name, version,
  license, why; check transitive deps).
- IDF pinned to v5.5.1 (`make prepare`). Record the exact commit if we ever float it.

## Status / adoption order
| Practice | When | Notes |
|---|---|---|
| On-target audio stats | Stage 1 | with the first real voice |
| Golden-file render tests | Stage 1 | reuse the offline-render harness; FTZ off (ADR 0012) |
| CI (host+device build, format, tests) | Stage 1–2 | additive; no hardware needed |
| Coredump + WDT + stack detection | Stage 1 | sdkconfig flags, one-time |
| Static analysis (clang-tidy/cppcheck) | when noisy | optional, low priority |
| `dependencies.lock` committed | now | trivial |
