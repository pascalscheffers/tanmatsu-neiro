# Stage 12 — Output-path audit: residual onset crackle

**Status: RESOLVED 2026-07-17.** WO-12a device-verified: clean up to master gain 2.0
(the parameter maximum) after the Philips slot-format patch. H1 confirmed as root
cause. Remaining WOs re-scoped: 12c unnecessary; 12b cheap hygiene; 12d now a
quality (not crackle) item — re-judge by ear first; 12e optional hygiene.
See the 2026-07-17 resolution entry in [`../MEMORY.md`](../MEMORY.md).

**Original status: implementation-ready diagnostic plan.** Analysis by Opus 2026-07-17 from
`sniff.log` (build `236a984-dirty`), the 2026-07-16 line recordings, and a full
code audit of the render→codec chain. Execute the work-orders **in order** as
fresh worker jobs; each commits only on green and appends a tight MEMORY entry.

## Symptom (current, precise)

- Crackle **only at note onset**; sustained notes clean.
- Raising master gain increases it **strongly** (non-linear, threshold-like).
- Present on **both** built-in speaker and headphone out → upstream of the FM8002A amp.
- SD capture of the rendered stream **at the same gain** sounds clean — although
  clipping (exact-full-scale flat-tops) is *visible* in the captured waveform.
- Present with **no SD card mounted** (sniff.log run: mount failed `ESP_ERR_TIMEOUT`,
  crackle persisted) → not SD interference.

## Evidence base

| Fact | Source |
|---|---|
| Onset render spikes max 2658 µs vs 1333 µs budget, `over=8–21/750`; i2s `errors=0 short=0` | `sniff.log` lines 842–859 |
| Pre-gain mono peak 3.63; postg 0.67; limiter `gr=1.00` (never engages); out peak 0.62 | `sniff.log` line 844 |
| gain24.wav (line rec, gain 0.24): clean, peak 0.21. gain50.wav (gain 0.50): 79 non-periodic **+0.99997** clip steps | `specs/MEMORY.md` 2026-07-16 entry |
| Frozen-display run: crackle persists with `over=0`, no clip, out=0.73 — "load-independent, clean-timing" | commit `edcb5fc`, `specs/MEMORY.md` |
| Stagger of direct note-ons up to 12 blocks (~16 ms) did **not** fix it | `378fa72` → `af00241` → `236a984` |
| Codec/i2s/analog output path "never audited" | `specs/MEMORY.md` |

## Chain map (audited 2026-07-17)

voices sum → (chorus) → ×master gain (`engine/synth.cpp:650-651`) → DC-block →
limiter (`dsp/limiter.h`, THRESH 0.92, 1 ms attack) → `soft_clip`
(`dsp/saturate.h:9`, hard-clamp ±1.0 at |x|≥1.5) → float L/R →
**SD recorder tap** (`engine/synth.cpp:697`, own clamping f32→i16 in
`engine/record_ring.cpp:23-30`) → platform `to_i16` (clamping, cannot wrap;
`platform/device/platform_device.c:205-212`) → `i2s_channel_write` (blocking
`portMAX_DELAY`, 6×240-frame DMA ≈30 ms slack; `platform_device.c:255,262`) →
ES8156 DAC → FM8002A amp / headphone.

Because the recorder converts the *same floats* with an identical clamp, an SD
capture is bit-equivalent to what enters the DMA buffer. **A clean capture
therefore exonerates everything up to and including the DMA data — the fault is
in how the codec receives/interprets it, or beyond.**

## Hypotheses, ranked

### H1 — I2S slot-format mismatch (PRIME)

- P4 transmits **left-justified/MSB** 16-bit: `I2S_STD_MSB_SLOT_DEFAULT_CONFIG`
  in `managed_components/badgeteam__badge-bsp/targets/tanmatsu/badge_bsp_audio.c:32`.
- ES8156 is configured for **standard I2S (Philips)**: `es8156_configure` writes
  SDP REG11 with `sp_protocal=0`, `sp_wl=0`
  (`managed_components/nicolaielectronics__es8156/es8156.c:1329`).
- Philips receivers expect the MSB **one BCLK after** the WS edge; an MSB-justified
  transmitter puts it **at** the edge. The codec therefore reads every sample
  shifted left by one bit: value ×2 with sign-bit loss. Any sample |x| ≥ 0.5 FS
  **wraps** — a harsh step discontinuity inside the codec, invisible to every
  digital tap.
- Explains all symptoms: onset attack peaks (0.62 FS in sniff.log) cross 0.5 FS
  while sustain stays below (onset-only); threshold at 0.5 FS (strong gain
  sensitivity); digital, so both outputs; SD capture clean; `edcb5fc`'s
  "clean-timing crackle at out=0.73" (0.73 > 0.5).
- Open detail: gain50.wav shows clip steps pinned at **+FS** rather than
  wrap-to-negative; those flat-tops are H2's soft_clip output (exactly +1.0),
  which the mismatch then mangles. Sign bookkeeping of the wrap is a prediction
  to check in WO-12a's recording, not a blocker.

### H2 — limiter onset overshoot into soft_clip clamp (real, secondary)

- Limiter THRESH 0.92 with 1 ms attack lets onset transients through: at master
  gain 0.5, post-gain peaks reach 1.8; `soft_clip` hard-clamps ≥1.5 to exactly
  ±1.0 → the +0.99997 flat-tops in gain50.wav and in SD captures.
- By itself it sounds mild (captures sound fine) — but every clamped sample is
  ≥0.5 FS, i.e. guaranteed wrap fuel for H1. Fix after H1 (WO-12d), then re-judge.

### H3 — ES8156 register defaults unaudited (cheap check)

- `es8156_configure` never writes REG12 (automute) / REG13 (mute); reset defaults
  unverified. Volume mapping 80 % → REG14=144 assumed ≈ −23.5 dB (0xBF=0 dB) —
  verify. `sp_wl=0` (24-bit word length) against 16-BCLK slots — verify benign.

### Ruled out — do not revisit

DMA underrun (blocking write + ≈30 ms slack vs 2.6 ms worst render spike; frozen-
display run `edcb5fc`), SD-write interference (crackles with no card), note-on CPU
spikes (16 ms stagger no help), render-side DSP defects (floats verifiably clean).

## Running order

| Work-order | Deliverable | Effort | Depends on |
|---|---|---|---|
| 12a | slot-format fix experiment (decisive) | low | — |
| 12b | ES8156 register dump + datasheet audit | low | — |
| 12c | amplitude-threshold probe | medium | only if 12a fails |
| 12d | onset overshoot fix (limiter/headroom) | medium | 12a verdict |
| 12e | DMA underrun observability (hygiene) | low | last |

## WO-12a — slot-format experiment (decisive, run first)

**Hypothesis:** H1. **Change:** make transmitter and codec agree on framing.
Prefer changing the P4 side: `badge_bsp_audio.c:32`
`I2S_STD_MSB_SLOT_DEFAULT_CONFIG(...)` → `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...)`.
`managed_components/` is gitignored — deliver as a tracked patch in
`upstream-patches/` per its README (apply via `make patches`; the patch is the
future upstream PR). Do **not** also flip the codec side (`sp_protocal=1`) —
change exactly one side or the mismatch survives in mirror image.

**Verify gate (device):**
1. Build, flash. Same patch/chord, master gain 0.5, both speaker and headphone.
2. Expected if H1 true: onset crackle gone (or reduced to H2's mild saturation);
   overall level drops ≈6 dB (codec no longer reads ×2).
3. Line-record before/after at gain 0.5; after-recording must contain **no**
   non-periodic full-scale step discontinuities (flat-tops at exactly ±FS from
   soft_clip may remain — those are H2).
4. Regression: audio still plays at 48 kHz, both channels, correct pitch.

**If crackle persists unchanged:** H1 falsified for this fault → run WO-12c.
Keep the patch only if recordings confirm the framing fix is correct per scope
or datasheet; otherwise revert.

## WO-12b — ES8156 register audit

**Hypothesis:** H3. Add a one-shot boot dump (behind `SYNTH_PROFILE` or a debug
flag) reading REG 0x00–0x25 via the existing `es8156_read_register_page` helpers;
log hex values once after `bsp_audio_initialize`. Compare against the ES8156
datasheet: REG11 protocol/word-length actually latched, REG12 automute default,
REG13 mute bits, REG14 volume curve (is 144 really ≈ −23.5 dB? is 191 = 0 dB?).
Record findings + datasheet cross-refs in MEMORY. Remove or gate the dump after.

**Verify gate:** dump appears once at boot, values recorded, no audio-path change.

## WO-12c — amplitude-threshold probe (only if 12a fails)

**Purpose:** locate the fault's amplitude threshold, which fingerprints the
mechanism. Add a debug-only steady test tone (sine, e.g. 440 Hz) whose *output*
amplitude is settable in steps 0.40 → 0.95 FS (bypass limiter/soft_clip or set
master gain so the final float peak is the stepped value — assert via the
existing `sig` peak log). Play each step ≥5 s on device, note where crackle
starts.

**Decision tree:** onset ≈0.5 FS → bit-shift/framing class (revisit 12a variant:
flip codec side instead, check WS polarity `sp_lrp`, MCLK ratio);
onset ≈1.0 FS → clamp/clip class (go WO-12d, audit codec input headroom);
no steady-state crackle at any level → transient-only mechanism (return to 12b
findings: automute, volume-ramp, soft-start registers).

## WO-12d — onset overshoot fix (H2)

After 12a verdict. Options in preference order: (1) raise attack headroom —
lower default master gain landing (postg peaks ≤ ~1.2); (2) faster/lookahead
limiter attack so gr engages before the peak passes; (3) lower THRESH. Keep
`soft_clip` as final guard. Constraint: no audible dulling of sustained program;
host tests in `tests/` must stay green.

**Verify gate:** SD capture of a gain-0.5 8-note chord onset contains **no**
samples at exactly ±1.0 (no clamp flat-tops); crackle absent on device;
`make test` green.

## WO-12e — DMA underrun observability (hygiene, last)

Underrun is ruled out for this fault, but the instrumentation gap is real: the
blocking `portMAX_DELAY` write can never report starvation, and
`auto_clear_after_cb=false` means a genuine underrun silently replays a stale
buffer. Register the i2s event callback underrun counter and surface it in the
`[PROFILE] i2s` line; consider `auto_clear_after_cb=true` so a real underrun
emits silence instead of stale audio. Zero cost when `SYNTH_PROFILE` off.

**Verify gate:** counter present in profile output, reads 0 in normal play;
forced-starvation test (e.g. artificial delay in render under a debug flag)
increments it.
