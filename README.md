<p align="center">
  <img src="icons/icon-full.png" alt="Neiro" width="200">
</p>

<h1 align="center">Neiro</h1>

<p align="center"><em>An 8-voice Juno-106 in your pocket — running on a <a href="https://tanmatsu.cloud">Tanmatsu</a> badge.</em></p>

> **Neiro** (音色) is Japanese for *tone-colour* — timbre, the one thing a synthesizer exists to shape. A nod to the Japanese heritage of the Roland Juno-106 it models.

## What it is

A polyphonic **virtual-analog synthesizer** for the [Tanmatsu](https://tanmatsu.cloud) badge (Nicolai Electronics / badge.team, ESP32-P4). No analog in the signal path — it's all DSP modelling a classic Juno-style poly voice, played live from a USB MIDI keyboard, a DAW, or the badge's own keyboard, and tweaked on its screen.

Reuse-first and dedup-first by design: vetted MIT DSP (DaisySP), and a single parameter table that drives the UI, MIDI, and presets alike. The DSP layer is pure C/C++ and runs on the desktop too, so you can hack on it without the hardware.

## What it can do (today)

- **8-voice Juno voice** — PolyBLEP saw + sub + noise → state-variable filter → ADSR amp. Fat bass, sparkling highs.
- **Modulation** — 2 envelopes, 2 shared free-running LFOs (authentic Juno-106), and a 16-slot mod matrix.
- **Play modes** — poly / mono / legato, portamento, and unison with detune.
- **Chorus** — the Juno BBD chorus (modes I & II).
- **Arpeggiator** — up / down / up-down / as-played / random, 1–4 octaves, clock-synced rate, gate, swing, latch.
- **Clock** — internal master clock with adjustable BPM.
- **MIDI I/O** — USB-A host (plug in a keyboard), USB-C device (play from a DAW), plus expression: pitch-bend, mod wheel, aftertouch, sustain (CC64), and panic.
- **Controller follow** — 8 hardware pots (CC 21–28) map to key parameters; turning a knob jumps the screen to that parameter's page and tracks the value live.
- **Presets** — factory bank + save-your-own.
- **Musical typing** — make sound standalone with nothing plugged in.

Delay, reverb, a step sequencer, and an SMF player are designed-for and on the roadmap — see [`specs/06-feature-scope-and-roadmap.md`](specs/06-feature-scope-and-roadmap.md).

## Quick start

```sh
# Desktop simulator (SDL2 + miniaudio) — the fast loop, no hardware needed
make host          # build & run the synth on your Mac/Linux box
make test          # run the host-side DSP unit tests

# On the badge (put it in USB mode first: launcher → purple diamond)
make install       # build + upload into AppFS (slug: synth)
make run           # launch it
```

Full command catalogue — firmware flash, serial capture, bench — in [`specs/09-build-and-run.md`](specs/09-build-and-run.md).

## Playing it

Note input comes from a USB MIDI keyboard, a DAW over USB-C, or the badge's own keyboard ("musical typing", GarageBand-style):

```
   W E     T Y U     O P            ← black keys (sharps)
  A S D F G H J K L  ;              ← white keys: C D E F G A B C D E
```

| Key / Button | Result |
|---|---|
| `A`–`;` (+ `W E T Y U O P`) | Play notes (just over an octave, white + black keys) |
| `Z` / `X` | Octave down / up |
| `←` / `→` | Previous / next parameter page |
| `↑` / `↓` | Select parameter row on the page |
| ✕ (F1, X button) | Nudge selected value **down** — tap = fine step; hold = ramps to full range in ~2 s |
| △ (F2, triangle) | Nudge selected value **up** — same hold-to-ramp behaviour |
| □ (F3, square) | **Back** — return to the PRESET page; on the PRESET page, cancel an in-progress audition |
| ○ (F4, circle) | **Load / confirm** — commit the highlighted preset on the PRESET page |
| ☘ (F5, three-lobe) | Toggle the **musical-typing key-guide overlay** (works on any page) |
| ◇ (F6, diamond) | **Save** current sound as your user preset |
| `[` / `]` | Previous / next factory preset |
| `=` | Save current sound as your user preset (same as ◇) |
| `Esc` | Quit (back to the launcher) |

### MIDI control (USB)

Connect a USB MIDI controller to the badge's USB-A port. The following CCs are mapped out of the box:

| Source | CC | Effect |
|---|---|---|
| Mod wheel | CC 1 | Brightens the filter (adds up to +8 kHz to cutoff) |
| Pot 1 | CC 21 | Filter cutoff |
| Pot 2 | CC 22 | Filter resonance |
| Pot 3 | CC 23 | Filter envelope depth |
| Pot 4 | CC 24 | Filter LFO depth |
| Pot 5 | CC 25 | Chorus depth |
| Pot 6 | CC 26 | Unison detune |
| Pot 7 | CC 27 | LFO 1 rate |
| Pot 8 | CC 28 | Release time |
| Sustain pedal | CC 64 | Hold notes |
| — | CC 120 / 123 | Panic (all notes off) |

These CC numbers match the default pot assignments on a Novation Launchkey 37 (pots in Custom mode sending CC 21–28). Turning any mapped knob instantly jumps the screen to that parameter's page and shows the value bar moving live.

A few standard General-MIDI CCs are also recognised — handy if your controller or DAW sends them. These follow the screen the same way as the pots:

| Source | CC | Effect |
|---|---|---|
| Volume | CC 7 | Channel volume (attenuation-only, square-law taper; 127=unity, never boosts) |
| Portamento time | CC 5 | Glide rate |
| Sound controller | CC 70 | Oscillator level |
| Attack time | CC 73 | Amp-envelope attack |

**Preset page (home, page 1):** scroll the list with ↑/↓ to audition patches live — the sound changes as you browse. Press ○ or Enter to commit the highlighted preset. Press □ or navigate away without confirming to revert to the sound you had before browsing.

**Parameter pages (pages 2–9):** nine fixed pages in order — PRESET · PERFORM · OSC · FILTER · AMP ENV · MOD ENV · LFO · FX · AMP. Multi-group pages (PERFORM = Clock + Arp; FILTER = VCF + HPF) show a section sub-header before each group. The status strip at the bottom shows active voices, the current octave, and the loaded preset name.

## Known issues

- **Crackle on dense, loud chords.** Smashing roughly four or more notes at once — or fewer notes hit hard — can produce brief crackle. It is **not** distortion or clipping: the signal chain stays clean (the limiter never even engages). It is an audio-block CPU **underrun** — individual blocks momentarily spike to nearly twice their time budget during note-on bursts, and the full-screen redraw competing for the memory bus while the voice meter animates makes it worse. The steady cost of eight voices is well within budget; only the transient spikes drop samples. The next step is to confirm how much of the spike is display contention (dirty-rect redraw, below) versus note-on cost. Soft or sparse playing is clean.
- **High-pass filter is not wired.** The FILTER page shows an HPF group, but its cutoff does not yet affect the audio path.

## Roadmap

Bigger features that are designed-for but not yet built (see [`specs/06-feature-scope-and-roadmap.md`](specs/06-feature-scope-and-roadmap.md) for the full scope):

- **Effects:** tempo-synced delay and a reverb (DaisySP `ReverbSc`) — the FX page is in place ahead of the DSP.
- **Step sequencer** and a **Standard MIDI File player** for hands-free playback.
- **Dirty-rect display blitting** — only repaint the parts of the screen that changed, instead of the whole ~1.15 MB framebuffer. Cuts the memory-bus contention behind the crackle above and frees CPU budget for the effects.
- **Wire up the HPF**, a tap-tempo button on the UI, and MIDI-learn for per-controller CC mapping.

## License

Original code is [MIT](https://opensource.org/license/mit). Built on the [`tanmatsu-template`](https://github.com/Nicolai-Electronics/tanmatsu-template) (CC0-1.0) and the [PAX graphics](https://github.com/robotman2412/pax-graphics) library; vendored DSP (DaisySP and others) retains its own permissive licenses — see [`specs/02-synth-architecture.md`](specs/02-synth-architecture.md).
