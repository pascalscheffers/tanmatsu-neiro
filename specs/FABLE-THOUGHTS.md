 
I want an analysis of this project, with the intent to make it the best musical soft synth on Tanmatsu I can with the limited resources of the device, limited sonnet 5 opus 4.8 tokens.
  
  Focus on things the older models would miss and save them in I fairly
  continuous live stream of work/analysis items in specs/FABLE-THOUGHTS.md, the end of this file. Do not wait until a final conclusion
  is made, write as you go, tokens may run out before you do. 
  
Things I'm particularly interested in:
- Musicality. Ways to interact with the synth in interesting ways. One thing I don't like about the current implementation is the keyboard. The rectangular grid makes for an awkward standard 12 tone layout. Which is also not super useful. Some kind of interval/harmony based keyboard where you set the key center or it follows dynamically... or. multiple layouts for musical styles. Think like an accordion keyboard, but modern and dynamic. Intstantly fun and intuitive.
- Rythm. Something which makes it more percussion / bass line than regular synth. Again needs research and fresh ideas. 
- Algorithmic optimisations/efficiency. We're on quite limited hardware. If there are tricks to make the dsp code more efficient/faster? Preferably at low/no audible cost, but if something is inaudible but gains more voices. fine tuning for ESP32-P4 perhaps.
- Is there anything we can do with the radio / CPU / signal processing capabilities of the C6 radio that would be interesting for a synth?

Overall - looking for Synth features and improvements which are uniquely suited to tanmatsu. 

Critital: Do no implement. Design, give code snippets. Be frugal with tokens for these and early out. Short and sweet suggestions. Assume I'll be able to expand on the idea and prompt the lower tier models effectively.

Make a short and sweet plan of this, then quickly exit plan mode without too many questions unless critical information is missing.

---

# Fable analysis — 2026-07-06

Written incrementally, best ideas first. Each item: the idea, why it fits *this* device/
codebase, a sketch, effort (S/M/L), and a one-line work-order hook for Sonnet/Opus.

## 1. Keyboard: stop emulating a piano — the badge is a button-field instrument

**The core reframe:** the Tanmatsu QWERTY is not a bad piano; it's a *good concertina*.
It's a ~14×5 staggered button grid — the exact geometry that isomorphic layouts
(Wicki–Hayden, harmonic table) were designed for. The QWERTY row stagger (~half a key)
is the half-offset those layouts want. The current GarageBand layout throws this away and
inherits the piano's worst property (every key needs different fingering) with none of its
benefits (no velocity, no width). Isomorphic layouts have the property that **every chord
and interval is the same finger-shape in every key** — that's the "instantly fun and
intuitive" you asked for, and it's the thing a rectangular grid does *better* than a piano.

All of these are just alternative fills of one function — the seam already exists:
`key_to_semitone()` in `control/keyboard.c` (public via `keyboard_semitone_for_key()`),
consumed by the F5 overlay (`ui/ui_overlay.cpp`) with no table duplication. Generalize to
a layout-table pointer and everything downstream (overlay included) follows.

### 1a. Wicki–Hayden layout (the flagship — do this one first)

Axes: right = +2 semitones, up-right = +7 (fifth), up-left = +5 (fourth). Four usable
rows (`zxcv…`, `asdf…`, `qwer…`, `1234…`) give ~2.5 octaves without octave-shift, vs 1.3
now. A major triad is one compact shape *everywhere*; scales are two alternating rows.

```c
// keyboard.c — layouts become data. Grid position (row, col) per key, one formula per layout.
typedef struct { uint8_t row, col; } KeyPos;          // from a scancode→pos table
static int wicki_hayden_semitone(KeyPos p) {
    // row 0 = zxcv row. Each row up: +7 then +5 alternating ≈ +6/row with stagger,
    // equivalently: semis = col*2 + row*5 + (row+1)/2*2;  tune constants to taste.
    return p.col * 2 + p.row * 5 + ((p.row + 1) / 2) * 2;
}
```

The BSP delivers scancodes (row/col is derivable from a static 60-entry table); host SDL
side uses `SDL_Scancode`, which is *already* positional (layout-independent) — nice bonus:
musical typing stops breaking on non-US keyboards.

- **Effort:** S–M (one table + one enum param + overlay render per layout).
- **WO hook:** "Add `KEYB_LAYOUT` stepped param (Piano/Wicki–Hayden); scancode→(row,col)
  table in keyboard.c; overlay draws note names from the active layout fn."

### 1b. Scale-locked dynamic layout with movable key center

Bottom two rows = **scale degrees, not chromatics**: 7 keys per octave means the same
physical span covers ~2 octaves diatonically and *you cannot hit a wrong note* — the
accordion-like "instant fun" mode. Key center + scale are two stepped params
(`KEY_CENTER` 0–11, `SCALE` major/minor/dorian/mixo/penta/blues…), so presets carry them
(a bass patch in E minor pentatonic *loads* that way). Row above = same degrees +1 octave;
shift = chromatic accidental. Dynamic follow (key center tracks the last-played root, or a
simple weighted pitch-class histogram) is a v2 toggle — start with explicit setting, it's
one param row and zero magic.

- **Effort:** S (a 12-entry table per scale; reuses the same layout seam as 1a).
- **WO hook:** "Add SCALE + KEY_CENTER params (GROUP_GLOBAL) + `scale_layout_semitone()`;
  overlay shows degree numbers + note names."

### 1c. Stradella chord row (accordion left hand, modernized)

Top row (`1234…`) becomes **chord buttons in the current key**: I ii iii IV V vi vii°,
then secondary dominants / borrowed chords further right. One press = engine_note_on ×3–4
(control-side loop, audio thread untouched). With ARP_ON + LATCH this is instantly a
one-finger accompaniment machine; with the 303 mode below it's a bassline+chords duo on
one badge. Depends on 1b's KEY_CENTER/SCALE params — build after it.

- **Effort:** S once 1b exists.
- **WO hook:** "Chord-row mode: top row triggers diatonic triads of KEY_CENTER/SCALE;
  velocity from BASE_VELOCITY; release = matching note_offs (track per-key note lists)."

**Sequencing note for the campaign:** 1a/1b/1c share one refactor (`key_to_semitone` →
layout dispatch + scancode→grid table). Spec that refactor as WO-1, then each layout is an
independent, parallel-safe worker job.

## 2. Rhythm: the badge already has a TR-808 front panel

**The core reframe:** a row of the QWERTY keyboard *is* a 16-step TR sequencer front
panel. `1234567890` + four more keys = 16 physical step buttons in a straight line, with
the screen right above them to show the running step. No synth-with-a-screen has that;
step toggling with real buttons is the whole reason people love x0x boxes. Everything in
this section is **control-side** (a sequencer task feeding `engine_note_on/off` through the
existing `CommandQueue`) — the sacred audio thread is untouched.

### 2a. 303 bassline mode (cheapest big win — the engine is already 80% there)

A 303 is: mono + slide + accent + 16-step pattern. The engine already has mono/legato
(`PLAY_MODE`), portamento (`PORTAMENTO_TIME`), a resonant LP, and a filter env
(`VCF_ENV_DEPTH`). What's missing is only the *pattern* semantics:

- **Step** = `{note, octave±, accent:1, slide:1, gate:1}` — one byte-ish each, 16 steps,
  lives beside the arp state.
- **Slide** = send next note_on *before* current note_off (legato path already suppresses
  retrigger and portamento glides) — zero engine change.
- **Accent** = velocity ≥ threshold; one small engine addition: route velocity into
  VCF env depth (the 303's accent secret). Could be a mod-matrix route (`VELOCITY` source →
  cutoff dest) if the matrix's cutoff scaling is fixed (known cosmetic-no-op issue from the
  Launchkey session) — fixing that unlocks accent *and* repays existing debt.

Entry UI: step keys toggle gate; hold step + arrows set pitch; hold step + F1/F2 = accent/
slide. Pattern storage rides the preset blob (spec 05 already reserves a pattern model).

- **Effort:** M (sequencer core + step-entry UI + pattern-in-preset).
- **WO hook:** "SEQ mode on the PERFORM page: 16-step mono pattern, accent→VCF depth via
  matrix, slide via legato note-overlap, runs off the existing 96 PPQN arp clock."

### 2b. Euclidean arp (S — one evening, huge groove-per-line ratio)

The arp already ticks at 96 PPQN with swing. Add `ARP_EUCLID_LEN` (1–16) and
`ARP_EUCLID_FILL` (0–16): mask arp steps with the Euclidean pattern E(fill,len).
No Bjorklund recursion needed:

```c
// step s fires iff its bucket crosses a multiple of len/fill  (Bresenham form)
static bool euclid_hit(int s, int fill, int len) {
    return fill > 0 && ((s * fill) % len) < fill;   // E(3,8)=x..x..x., E(5,8)=x.xx.xx.
}
```

Two param rows on the ARP group; rests keep the clock so it locks with 2a. E(3,8) on a
bass patch is instant dembow; E(5,16) is instant acid. This is the highest
fun-per-token item in this file.

- **WO hook:** "Two ARP params + euclid mask in the arp tick; overlay/PERFORM page shows
  the pattern as dots."

### 2c. One cheap drum voice (percussion channel, not a second engine)

Analog-model drums are *comically* cheap next to a Juno voice — a kick is a sine with an
exponential pitch env (~180→45 Hz) + click transient; hat/snare = white noise → HP/BP +
fast env. No PolyBLEP, no SVF sweep smoothing, ~1–2k cycles/hit vs ~27k/voice. Rather than
a full second `SynthModel` (that's Stage 7's job), add a fixed **4-slot percussion
section** rendered in `synth_render` after the voice loop: kick/snare/closed-hat/open-hat,
triggered via reserved notes (MIDI ch10 map + bottom-left QWERTY keys in drum mode), each
~20 lines of DSP. Sequenced by 2a's engine on a second track and you have a groove box.

- **Effort:** M (S for the DSP, M with a second sequencer track + mix params).
- **WO hook:** "dsp/drum.h: KickVoice/NoiseVoice (sine+pitch-env, noise+SVF+env);
  4 fixed slots in synth.cpp post-voice-loop; DRUM_LEVEL param; ch10 note map."

**Priority within §2:** 2b → 2a → 2c. 2b is a toe-dip that makes the existing arp
groovier immediately; 2a turns the badge into an instrument with a genre; 2c completes
the groove box.

## 3. P4 efficiency: fix the spikes before chasing throughput

Your own profile data says average load is fine (8 voices ≈ 52%); the crackle is
**per-block spikes** (note-on bursts + blit contention, per the open MEMORY handoff). So
the highest-value "optimizations" are spike-flatteners, not inner-loop rewrites.

### 3a. Note-on admission control (tiny, directly targets the open poly-crackle item)

A chord drains 4–8 note-ons in *one* block: allocator scan + `note_on()` init × N, on top
of normal render. Cap it: in `synth.cpp`'s command drain, admit at most 2 note-ons per
block and leave the rest in the `CommandQueue` for the next block. Blocks are 1.33 ms —
spreading a chord over 2–4 blocks is far below strum-perception (~10 ms+), but it halves
the worst-case block. Note-offs stay unlimited (they're cheap and dropping them is how
voices stick).

```cpp
int admitted = 0;
while (admitted < kMaxNoteOnsPerBlock ? s_cmds.pop(cmd) : pop_offs_only(cmd)) { ... }
// simplest form: peek; if cmd is NoteOn and admitted==cap, break and leave queue intact
```

- **Effort:** S. **WO hook:** "Cap note-on admissions per block (kMaxNoteOnsPerBlock=2,
  synth_config.h); regression: 8-note chord produces no over-budget block (PROFILE=1)."

### 3b. What NOT to do (save the tokens)

- **PIE SIMD for the float path** — the P4's 128-bit PIE is *integer* SIMD (8/16/32-bit);
  there is no 4×f32 vector unit. Hand-vectorizing the float voice loop is a dead end.
  Where PIE *does* pay: the final float→int16 interleave at the platform sink, and any
  future int16 delay-line/sample copies — via **esp-dsp** (Apache-2.0, Espressif,
  P4-optimized) rather than hand-written intrinsics. Reuse-first applies.
- **Look-ahead limiter / headroom work** — already falsified by the signal probe (gr=1.00
  during crackle). MEMORY says this; don't let a fresh worker re-derive it.
- **Fixed-point voice DSP** — the FPU is good; per ratified numbers the float voice is
  ~27.5k cycles. Only revisit if a profile fingers a specific block.

### 3c. Cheap wins worth a profile-gated look (in order)

1. **Dirty-rect blit (WS3 / Stage 2B)** — already the known lever; it's a *DSP* win
   because the 1.15 MB PSRAM blit steals the audio core's memory bandwidth. Do it before
   any inner-loop work; it also buys the FX budget for Stage 4d.
2. **Block size 64 → 128** if spikes persist after 3a: halves per-block fixed overhead
   (drains, allocator, chorus setup) and doubles spike headroom for +1.3 ms latency
   (total still < 5 ms — inaudible for keys). One constant in `synth_config.h` *by
   design* — cheap experiment, easy revert.
3. **`-ffast-math` on `dsp/`+`engine/` only** — worth ~5–15% on FPU-heavy code, **but**
   it breaks the NaN guards (limiter, output sanitize) via `-ffinite-math-only`. Use
   `-funsafe-math-optimizations -fno-math-errno` *without* finite-math, or keep NaN checks
   as integer-bit tests (`(bits & 0x7F800000) == 0x7F800000`). Needs a device A/B bench
   (`make bench`), not faith.
4. **Silent-voice early-out** — verify the voice loop skips `!is_active()` slots *before*
   touching their state (cache). If it already does, done; if not, it's a 2-line win.

## 4. C6 radio: badge-to-badge jam sync is the one that matters

Reality check first: the C6 hangs off SDIO and needs the `esp-hosted`/`esp_wifi_remote`
stack on the P4 side — that's real heap, flash, and power (why it's off today). So gate
everything here behind "radio page ON", and pick features where *wireless is the point*,
not a convenience.

### 4a. ESP-NOW jam sync (the killer demo — uniquely badge-shaped)

Two+ Tanmatsus at a camp table, one is tempo master, all sequencers/arps lock. ESP-NOW is
connectionless (no AP, no pairing UI — broadcast on a channel), ~1–2 ms one-way, and the
WiFi **TSF timer** gives receivers a shared microsecond clock: master broadcasts
`{tsf_timestamp_of_next_downbeat, bpm, pattern_step}` a few times per bar; peers schedule
the downbeat against TSF instead of reacting to packet arrival — jitter collapses to
timer accuracy, not network luck. Payload is one struct; loss-tolerant by design (each
beacon re-anchors). Also carries note events for "play my badge from yours" duets.
This feature makes no sense on any synth *except* a badge — it's the most
Tanmatsu-unique idea in this file.

- **Effort:** L (esp-hosted bring-up is the bulk; the sync protocol itself is S).
- **WO hook:** "RADIO page: JAM ON/OFF + master/follow; ESP-NOW beacon
  `{tsf_downbeat, bpm, step}`; follower slews CLOCK_BPM + phase-locks the arp tick."

### 4b. BLE-MIDI (standard, but know the latency)

Works with every phone/tablet/DAW as a wireless MIDI device — great reach, and the C6
does BLE without the WiFi stack's heap appetite. But connection intervals mean 7.5–15 ms
latency with jitter: fine for pads/browsing, mediocre for drumming. Position it as
"wireless DAW/patch-librarian link", not the performance path (USB stays that). RTP-MIDI
over WiFi is the lower-latency alternative but needs an AP + mDNS — camp-hostile; skip
unless a use case demands it. **Ableton Link is GPLv2 — license gate per CLAUDE.md before
even considering it.**

### 4c. Radio as a modulation source (gimmick tier, but 20 lines)

Channel RSSI / packet-arrival entropy → a `RADIO` mod-matrix source. Sample-and-hold
random that reacts to people walking past the badge. Silly, memorable, cheap — but only
once 4a has paid the esp-hosted tax.

## 5. Tanmatsu-unique grab bag

- **6 RGB LEDs (coprocessor):** beat/bar indicator (downbeat = accent color), arp/seq step
  chase, voice-count meter, limiter-GR red flash. Control-side I2C, trivially cheap, huge
  stage presence. **S.** Do it alongside §2 — a groove box needs blinkenlights.
- **Scope page:** the render buffer is already in memory; a UI page plotting the last
  block (waveform) or a small FFT (esp-dsp) at 800×480 is the best screen-flex per token.
  Gate on dirty-rect blit landing first. **S–M.**
- **Headphone-detect (BSP):** auto-duck MASTER_GAIN into speaker-safe range when the amp
  is on and restore on headphone insert — the tiny speaker distorts before the DSP does.
  **S.**
- **SD card:** user preset *banks* + pattern storage (2a) + MIDI-file player (spec 03)
  live here. Also the future wavetable home: stream/copy tables to SRAM at patch load,
  never per-voice. **M, staged.**
- **Personality header (I2S + GPIO):** a future "pro audio" expansion — external DAC or
  CV/gate for modular. Don't build; just don't *block* it: keep the audio sink behind the
  existing 5-seam HAL (it already is — noting for the record).
- **No key velocity** on the badge: in drum/step modes, use *row* as velocity zones
  (bottom row = accent) — turns a hardware limitation into a playing technique. Free
  once 2a exists.

## Priorities if I had one campaign to spend

1. **§2b Euclidean arp + §3a note-on cap** — two S-sized WOs, immediate groove + fixes
   the open crackle item.
2. **§1 keyboard campaign** (refactor WO, then Wicki–Hayden + scale-lock) — transforms
   what the instrument *is*.
3. **§2a 303 sequencer + LEDs** — gives it a genre and a face.
4. **WS3 dirty-rect blit** — unlocks FX budget and the scope page (already planned; keep
   it queued).
5. **§4a jam sync** — the demo nobody else can do; schedule when an L-sized slot exists.
