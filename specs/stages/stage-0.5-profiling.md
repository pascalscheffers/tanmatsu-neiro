# Stage 0.5 — On-device profiling & CPU budget

**Status:** planned · **Executor:** Sonnet · **Protocol:** [stages/README.md](README.md)

## Why this stage exists
Every downstream sizing decision — 8 voices + unison (ADR 0003), block 64, macro-osc
complexity, chorus + reverb — rests on a *theoretical* CPU budget. Hardware is now in hand.
This stage produces a **measured cycles-per-block budget** and the **I2S deadline margin**
at 64 @ 48 kHz, on real P4 silicon, so Stage 1's voice is sized against fact. Scope is
**CPU + audio-deadline margin** only (no PSRAM-bandwidth or SIMD/PIE probing — noted as a
later option below). Benchmarks are **synthetic proxies**, not yet the real DSP.

This is the gate that unblocks Stage 1's per-voice complexity target.

## Gate table
| Gate | When | Why Opus | Recommendation |
|---|---|---|---|
| 🛑 Ratify CPU budget + polyphony | end of 0.5d, after numbers land | CPU-budget / architecture | Keep ADR 0003 (8+unison) if margin allows; else amend with measured ceiling |

## Sub-stages
| id | Deliverable | Ends when |
|---|---|---|
| 0.5a | `platform_cycles_*` seam (device + host) | both builds green; counter reads sanely |
| 0.5b | `engine/bench.{h,c}` proxy kernels + fused fake-voice; `make bench` (host) | host prints a kernel-cost table |
| 0.5c | Device bench (`BENCH=1`) pushed via **AppFS** (`make bench-device`) + serial readout; deadline-margin load ramp | numbers captured from hardware |
| 0.5d | Fill `stage-0.5-results.md`, seed cycles/block into spec 02 budget; **gate** | results committed; gate raised |

### 0.5a — cycle-count seam
Cycle counting is platform-specific, so it lives **behind the membrane** (ADR 0007), not in
`engine/`. Add to `platform/platform.h`:
- `uint64_t platform_cycles_now(void);` — monotonic cycle counter.
- `uint32_t platform_cycles_per_sec(void);` — for converting cycles ↔ time.

Implement:
- **Device** (`platform/device/platform_device.c`): `esp_cpu_get_cycle_count()` (RISC-V
  `mcycle` CSR, 32-bit — handle wrap, or accumulate to 64-bit); `platform_cycles_per_sec`
  from the configured CPU frequency (`esp_clk_cpu_freq()` / `CONFIG_*_CPU_FREQ_MHZ`).
- **Host** (`platform/host/platform_host.c`): `clock_gettime(CLOCK_MONOTONIC)` → ns, scaled
  to a nominal 1 GHz so the host prints comparable "pseudo-cycles" (clearly labelled a
  *reference*, not the device). Host numbers orient; **device numbers are the budget.**

### 0.5b — proxy kernels (portable, pure)
`engine/bench.{h,c}` — no ESP-IDF, no I/O beyond `printf` of the final table (bench is a
standalone diagnostic mode, **not** the audio path, so `printf` is allowed here). Each
kernel runs over a 64-sample block, repeated enough times to swamp timer overhead; report
**cycles/sample** and **cycles/64-block**. Synthetic proxies to time:
- `sinf`, `expf` (IDF math lib cost — used by envelopes/LFOs/pitch).
- a 2-pole **SVF** step and a 4-pole **ladder** step (filter candidates).
- a **PolyBLEP saw** step (the MVP oscillator's hot path shape).
- a generic **biquad** step.
- **block memcpy** (buffer-shuffling overhead floor).
- a **fused fake-voice**: phase increment + osc approx + filter step + env multiply, the
  rough shape of one Stage 1 voice. Parameterize as `bench_voice_proxy(n_voices, l, r, n)`
  so it can also serve as a `synth_render`-shaped load generator in 0.5c.

Subtract a measured empty-loop baseline. Denormals: run FTZ-off (ADR 0012) so costs match
the device. `make bench` builds the host with `-DSYNTH_BENCH` and runs it.

### 0.5c — device bench + deadline margin
- Build the device image with `BENCH=1` (sets `-DSYNTH_BENCH`); `app/app.c` gets an
  `#ifdef SYNTH_BENCH` branch: run `bench_run_kernels()` once at boot and `printf` the
  table over UART, then start the **load ramp**.
- **Load it via AppFS, not a full reflash.** The bench is a throwaway diagnostic — there's
  no reason to overwrite the launcher firmware for it. Push it into the launcher's AppFS
  partition over USB and launch it; the launcher stays put and you drop back into it when
  the bench reboots. One-time: `make badgelink` (clones the tool). The device must be in
  **USB mode** (launcher home screen → purple diamond; USB icon top-right) so badgelink can
  find it. The device bench is **interactive** (see below) precisely so you have time to
  attach the console after launch. Flow:
  1. `make bench-device` — builds `BENCH=1`, uploads under the `synthbench` AppFS slug (the
     synth app's slot is untouched), and starts it. The badge screen shows
     **"Press any key to start"**.
  2. Get the console attached (the badge must be in **debug/USB** mode for USB-Serial-JTAG
     to reach the host — it can't while badgelink/OTG owns the USB-C). Then `make sniff` —
     it reads **all** `/dev/cu.usbmodem*` ports at once, labeled, and tees to
     `build/<dev>-bench/console.log`.
  3. Press a key **on the badge** → the bench runs and the table streams to the console.
  4. When done the bench **returns to the launcher** on its own
     (`platform_exit_to_launcher` → `bsp_device_restart_to_launcher`).

  **Console gotchas (each cost us a run — don't relearn them):**
  1. **Console vs badgelink share the USB-C.** `badgelink mode {usb/debug | device/badgelink}`
     — `usb/debug` exposes USB-Serial-JTAG (console/flash/monitor); `badgelink` is OTG for
     uploads. Mutually exclusive. An AppFS-launched app inherits OTG, so its console is
     detached until the badge is in debug mode. The keypress prompt buys time to sort this.
  2. **Two serial interfaces.** P4 host (shows `H_SDIO_DRV`; our `printf` lands here) and the
     C6 radio slave (shows `slave_rpc`); numbers shift across reboots. `make sniff` opens all
     of them so you can't pick wrong — the table appears on the P4 line.
  3. **USB-Serial-JTAG block-buffers stdout** (not a TTY). `bench_run()` calls
     `setvbuf(stdout, NULL, _IONBF, 0)` so the table streams live — keep it, or output sits
     invisibly while only `ESP_LOG` chatter shows.

  If the console still won't attach in your setup, fall back to a full image:
  `make flashmonitor BENCH=1` (overwrites the launcher; re-flash it afterward to restore
  AppFS). See the AppFS dev loop in `CLAUDE.md` → *Build, Flash, Run*.
- **Load ramp (the real measurement):** install `bench_voice_proxy` as the audio render fn.
  Around the render call in the audio task, read `platform_cycles_now()` before/after and
  accumulate into a lock-free stat (the spec 08 "measure in the audio thread, report from a
  normal task" pattern — never `printf` *inside* the audio callback). A low-priority task
  prints, per N blocks: render cycles, **% of the block period**, and any I2S underrun
  count. Sweep `n_voices` upward; record the count at which render time crosses a safe
  ceiling (target **≤ ~70%** of the period, leaving headroom for chorus/reverb/UI/jitter).
- Block period in cycles = `platform_cycles_per_sec() * 64 / 48000`. Report margin as
  period − render.

### 0.5d — record + gate
Fill in `specs/stages/stage-0.5-results.md` (template below). Add a **cycles/block** column
to the running budget table in `specs/02-synth-architecture.md` (Stage 0.5 row) and note
the empirical max-voice ceiling. Then raise the gate:

```
🛑 OPUS GATE — CPU budget & polyphony
  Why Opus: CPU-budget / architecture (touches ADR 0003, sizes all of Stage 1).
  Decision: What is the per-voice cycle budget, and does 8 voices + unison fit with
            headroom for chorus + a future reverb? Keep, raise, or lower the voice count?
  Recommendation: keep ADR 0003 (8 + unison) if the fake-voice ramp clears 8 voices at
            ≤70% period with room to spare; otherwise amend ADR 0003 to the measured
            ceiling and note the unison cost multiplier.
  Sonnet action: STOP — record results, raise this in MEMORY.md "Open Opus gates", ask
            the user to switch to Opus before Stage 1.
```

## Results template (`stage-0.5-results.md`, filled by the run)
- Device: chip, CPU freq (MHz), IDF version, optimization flags (`-O2`?), FTZ state.
- Kernel cost table: kernel · cycles/sample · cycles/64-block · ns/block (host ref beside).
- Deadline: block period (cycles & µs); render cycles vs `n_voices`; safe voice ceiling.
- Underruns observed during the ramp; stack high-water of the audio task.
- One-paragraph read: the proposed per-voice budget and whether ADR 0003 stands.

## Acceptance
- `platform_cycles_*` works on both targets; `make bench` prints a kernel table on host.
- Device prints kernel costs + a deadline-margin ramp over serial; numbers are captured in
  `stage-0.5-results.md` and the spec 02 budget row.
- Membrane clean; `make build` and `make host` green; bench code is excluded from the
  normal (non-`BENCH`) build path so it costs nothing in the shipping image.
- Gate raised for budget ratification (does **not** proceed into Stage 1).

## Later (explicitly out of scope here)
PSRAM sequential-vs-random bandwidth (matters once wavetables/samples land) and a SIMD/PIE
(`esp-dsp` float) probe. Both are higher-value but more open-ended; schedule as their own
mini-stage if/when the budget says we're tight.
