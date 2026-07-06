# Stage 9 — Rhythm / groove box (§2) + PERFORM HUD (§6b) + LEDs (§5) + browser (§6e)

> **Status: pre-runbook campaign brief.** Opus-authored map from `specs/FABLE-THOUGHTS.md` §2
> (the badge already has a TR-808 front panel) + §6b (PERFORM HUD) + §5 (RGB LEDs) + §6e (preset
> browser as instrument). NOT yet closed work-orders — resolve gates, then author each per
> `stages/README.md` (ADR 0017).
>
> **Execution unit:** one clean Sonnet 5 context per sub-stage.
>
> **Depends on:** Stage 4 clock/arp (96 PPQN, scheduler, `engine/arp.h`, `engine/clock.h`), Stage 6
> WS3 (all HUD widgets), Stage 8 8a note-on cap (so 303/arp bursts don't spike). 9c′ needs 9b + 9c.
>
> Grounding: `specs/FABLE-THOUGHTS.md` §2a/§2b/§2c, §6b, §5, §6e. Seams: `engine/arp.h` (arp tick),
> `engine/clock.h` (96 PPQN), `engine/command_queue.h` / `engine_note_on/off`, `engine/mod_matrix.h`
> (VELOCITY→cutoff route, `kModDestPitch` pattern), `engine/preset.{h,cpp}` (pattern-in-preset),
> `ui/ui.cpp` PERFORM page, `ui/ui_widgets.*`. **Everything in §2 is control/engine-side on the
> existing clock — the sacred audio thread's block structure is untouched.**

## The reframe (why this stage)
The QWERTY *is* a TR sequencer front panel, folded: steps 1–8 on `1…8`, steps 9–16 on `Q…I`
directly beneath — a 2×8 bank where physical geometry = screen geometry = pattern structure. §2
turns the synth into an instrument with a genre and a face. Priority within §2 (FABLE): 2b → 2a →
2c (toe-dip groove → genre → complete groove box).

## Where we are entering Stage 9
- Stage 4 gave us the sample-accurate clock (96 PPQN, `engine/clock.h`), the event scheduler, and
  the arp (`engine/arp.h`) running engine-side (ADR 0019) with swing. FX bus (delay + reverb) in
  the master chain.
- Mod matrix (`engine/mod_matrix.h`) has 16 slots + virtual dests (`kModDestPitch`, `kModDestPwm`).
  A **VELOCITY→cutoff** route is the 303 accent mechanism — check the matrix cutoff-scaling
  (cosmetic-no-op noted from the Launchkey session) and fix it here (repays debt + unlocks accent).
- WS3 (Stage 6) + `draw_curve` widget landed; note-on cap (8a) landed.

## Kickoff gates
- **G9c — 303 accent routing + matrix cutoff scaling (sonic). 🛑 OPEN at 9c.** Confirm accent =
  velocity→VCF-env-depth via the mod matrix (fixing the cutoff scaling) vs a dedicated engine path.
  Recommendation: mod-matrix route (fix the scaling — repays known debt). Changes how it sounds.
- **G9c′ — Pattern data model + preset storage (data-format). 🛑 OPEN at 9c′.** Step =
  `{note, octave±, accent, slide, gate}` ×16; how it serializes into the preset blob (spec 05
  reserves a pattern model). Confirm format before persisting. Coordinates with SD-card banks (§5,
  deferred) as the future bulk home.
- **G9e — Drum section as a second render path (architecture). 🛑 OPEN at 9e.** A fixed 4-slot
  percussion section rendered in `synth_render` after the voice loop is a *second* audio path —
  confirm it's not a full second `SynthModel` (that's a later stage) and its CPU cost fits the
  budget (~1–2k cyc/hit). Recommendation: 4 fixed slots, post-voice-loop, measured.

## Sub-stage decomposition (running order: 9a → 9b → 9c → 9c′ → 9d → 9e → 9f)

**9a — Euclidean arp (§2b — highest fun-per-token).** Add `ARP_EUCLID_LEN` (1–16) +
`ARP_EUCLID_FILL` (0–16); mask arp steps with `euclid_hit(s,fill,len) = fill>0 && (s*fill)%len < fill`
(Bresenham, no Bjorklund). Rests keep the clock so it locks with 9c. *Seams:* two ARP params
(`param_desc`/`param_id`), the arp tick in `engine/arp.h`/its wiring. *Acceptance:* E(3,8) dembow,
E(5,16) acid; locked to clock. *One-context fit:* easily (S).

**9b — PERFORM page → performance HUD (§6b).** Big BPM readout (tap-tempo target — hook exists),
arp state, Euclid ring (from 9a) with playhead sweep, KEY/SCALE badge (from Stage 7), octave, voice
meter. Param rows stay below; dirty-rect per widget. *Seams:* `ui/ui.cpp` PERFORM header,
`ui/ui_widgets.*`. *Acceptance:* glanceable from a meter away; each widget dirty-rect only.
*One-context fit:* M — **split-if** → `9b-i` (BPM/octave/voice large-type + layout) / `9b-ii`
(Euclid ring + arp/key widgets).

**9c — 303 sequencer core (§2a part 1).** 16-step mono pattern model + tick playback off the arp
clock: `{note, octave±, accent, slide, gate}`. Slide = next note_on before current note_off (legato
path already suppresses retrigger + glides portamento — zero engine change). Accent = velocity ≥
threshold routed to VCF env depth via the mod matrix. *Seams:* pattern state beside arp,
`engine/mod_matrix.h` (VELOCITY→cutoff + scaling fix), `PLAY_MODE`/`PORTAMENTO_TIME` reuse. **G9c
gates.** *Acceptance:* a hand-loaded pattern plays with audible slide + accent; runs off the 96
PPQN clock. *One-context fit:* yes (engine-side core only, no UI).

**9c′ — 303 step-entry UI + pattern-in-preset (§2a part 2).** 2×8 key bank (`1…8`/`Q…I`) toggles
gate; hold step + arrows set pitch; hold step + F1/F2 = accent/slide. On-screen 2×8 grid mirrors
keys (gate=filled, accent=bright, slide=tie-line, pitch label, running step highlighted); held-step
big readout (§6d pattern). Pattern rides the preset blob. **Depends on 9b + 9c. G9c′ gates the
format.** *Seams:* `ui/ui.cpp` (PERFORM HUD grid), `control/keyboard.c` step-mode, `engine/preset.cpp`.
*Acceptance:* full step-entry loop; pattern saves/loads. *One-context fit:* M — **split-if** →
`9c′-i` (step entry + grid render) / `9c′-ii` (pattern-in-preset serialization).

**9d — RGB LED indicators (§5).** 6 coprocessor RGB LEDs: beat/bar indicator (downbeat = accent
color), arp/seq step chase, voice-count meter, limiter-GR red flash. Control-side I2C, cheap, huge
stage presence. *Seams:* new `control/leds.*` (or platform LED seam), driven from clock/arp/voice
state. *Acceptance:* LEDs track beat + step + voices on device. *One-context fit:* yes (S). A groove
box needs blinkenlights — lands with §2.

**9e — Drum voice section (§2c).** `dsp/drum.{h,cpp}`: KickVoice (sine + exp pitch env ~180→45 Hz +
click) / NoiseVoice (white → HP/BP + fast env), ~20 lines DSP each, ~1–2k cyc/hit. 4 fixed slots
(kick/snare/closed-hat/open-hat) rendered in `synth.cpp` post-voice-loop; `DRUM_LEVEL` param; MIDI
ch10 map + bottom-left QWERTY keys in drum mode. **G9e gates.** *Seams:* `dsp/drum.*`, `synth.cpp`
post-voice mix, one param, ch10 note map. *Acceptance:* four drum sounds trigger; CPU measured
within budget. *One-context fit:* M — **split-if** → `9e-i` (`dsp/drum.*` + host tests) / `9e-ii`
(4 slots in synth.cpp + param + ch10 map). Sequenced by 9c on a second track = groove box.

**9f — Preset browser as instrument (§6e).** Two-column browse: big patch name + category tag +
**fingerprint sparkline** (8 key params as mini-bars) so patches are recognizable before loading.
Keep the existing audition-with-revert model (WO-3) — presentation only. *Seams:* `ui/ui_presets.cpp`
+ `ui/ui_widgets.*` (sparkline). *Acceptance:* sparkline + name/category render; audition unchanged.
*One-context fit:* S–M.

## Grab-bag folded in (§5)
Velocity-by-row (§5): in drum/step modes, bottom row = accent — free rider on 9c/9e. SD-card
pattern/preset banks (§5): the future bulk home for 9c′ patterns — note as a dependency, full SD
staging deferred to its own later stage.

## Continuous (every sub-stage)
`make size`; host + device green; **audio thread untouched** — all sequencing goes through
`engine_note_on/off` + the existing scheduler/clock; membrane clean. Every drum/FX spend is a
measured budget row (ADR 0015). Update README play-instructions when step/drum keys ship.

## First action at kickoff
Read this brief + `engine/arp.h`/`clock.h` (Stage 4) + `engine/mod_matrix.h` (accent route) +
`engine/preset.cpp` (pattern storage). Author **9a** (Euclid, no gate) first, dispatch. Resolve
**G9c** before 9c, **G9c′** before 9c′, **G9e** before 9e.
