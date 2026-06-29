<p align="center">
  <img src="icons/icon-full.png" alt="Neiro" width="200">
</p>

<h1 align="center">Neiro</h1>

<p align="center"><em>An 8-voice Juno-106 in your pocket — running on a <a href="https://tanmatsu.cloud">Tanmatsu</a> badge.</em></p>

> **Neiro** (音色) is Japanese for *tone-colour* — timbre, the one thing a synthesizer exists to shape. A fitting nod, too, to the Japanese heritage of the Roland Juno-106 it models.

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

| Key | Result |
|-----|--------|
| `A`–`;` (+ `W E T Y U O P`) | Play notes (just over an octave, white + black keys) |
| `Z` / `X` | Octave down / up |
| `←` / `→` | Previous / next parameter page |
| `↑` / `↓` | Select parameter on the page |
| `,` / `.` | Nudge the selected value down / up (hold **Shift** for coarse 10% steps) |
| `[` / `]` | Previous / next factory preset |
| `=` | Save current sound as your user preset |
| `Esc` | Quit (back to the launcher) |

Parameter **pages** (OSC, FILTER, ENV, LFO, FX, AMP, ARP, …) are generated from the parameter table, so the exact set and order can shift as the synth grows — navigate by the page name shown on screen rather than by position. The status strip along the bottom shows active voices, the current octave, and the loaded preset.

## License

Original code is [MIT](https://opensource.org/license/mit). Built on the [`tanmatsu-template`](https://github.com/Nicolai-Electronics/tanmatsu-template) (CC0-1.0) and the [PAX graphics](https://github.com/robotman2412/pax-graphics) library; vendored DSP (DaisySP and others) retains its own permissive licenses — see [`specs/02-synth-architecture.md`](specs/02-synth-architecture.md).
