# Control & UI (DRAFT)

How the player makes notes and tweaks sound. Driven entirely by the parameter table
(`specs/02`) so UI and MIDI never hardcode parameter knowledge.

## Note input sources (any/all → one internal note event stream)
1. **USB-MIDI host (USB-A):** plug in a hardware MIDI keyboard/controller. Primary
   "real instrument" path. Needs a USB-host MIDI class driver (verify IDF/BSP support).
2. **USB-MIDI device (USB-C):** play from a computer/DAW; also the natural sequencing
   path. TinyUSB MIDI class.
3. **QWERTY keyboard:** built-in fallback "musical typing" (rows = piano keys) so the
   badge makes sound standalone with nothing plugged in.
4. *(Later, maybe)* BLE-MIDI over the C6 radio; expansion-header controls.

All sources normalize to one `note_event` (note on/off, velocity, channel, pitch-bend,
CC) consumed by the engine — sources are interchangeable and added without touching the
engine.

## Live parameter tweaking (the headline UX requirement)
Goal: **fast, glanceable, one- or two-key access to the sound-shaping knobs while
playing.** The 800×480 screen + QWERTY is the surface.

Proposed model (refine after first build):
- **Parameter pages** grouped by function (OSC / FILTER / ENV / LFO / FX / MIX), rendered
  from the parameter table. One page visible at a time; function keys (F-keys / number
  row) jump directly to a page.
- **Per-page controls** mapped to a fixed key cluster so muscle memory works: a row of
  keys selects which param on the page; another axis (e.g. `,`/`.` or arrow nav) nudges
  value, hold-Shift = coarse. Live value + a bar/dial drawn via PAX.
- **Always-visible status strip:** active voices, MIDI activity, CPU/block load, current
  preset. RGB LEDs mirror activity/clip.
- **Macro knobs (optional):** a few assignable "performance" params on the home page for
  big live moves (filter cutoff, chorus, etc.).
- **No layout shifts / fixed positions** so the player isn't hunting mid-performance.

> Concrete key map is a spec decision once we see PAX text/layout on the real panel.
> Keep controls in the parameter-table-driven renderer, not bespoke per page.

## Presets
- Stored on **SD card** (FATFS) as small files; the parameter table defines the schema,
  so a preset is just the serialized value set + name + metadata. Versioned so the table
  can grow without breaking old presets.
- Ship a handful of factory presets that show off fat bass / sparkling highs.

## Decided control features (see `specs/06` for stage order, `specs/05` for data)
- **Performance layer (all in):** assignable **macros** (routed via the mod matrix),
  **pitch-bend + mod wheel**, **velocity + aftertouch** response, **sustain/hold + panic**.
- **Arpeggiator (full):** up/down/up-down/random/as-played, octave range, clock-synced
  rate, gate length, swing, latch/hold.
- **Sequencer (step + real-time, one model):** grid programming *and* real-time record
  into the same pattern, with per-step parameter locks; patterns chain into songs.
  Data model in `specs/05`; timing in ADR 0010.
- **MIDI-file player:** pick a `.mid` (SMF type 0/1) on SD; its notes drive the current
  patch (mono-timbral) — play with nothing plugged in.
- **Clock:** internal master clock + **tap tempo**; pluggable so external MIDI-clock sync
  can slave it later (ADR 0010). Drives arp, sequencer, and tempo-synced delay.

## UI paradigm — hybrid panel + pages
A **Juno-style panel overview** (glanceable state, live performance) plus **focused edit
pages** for detail, both rendered from the parameter table via PAX, identical on host and
device. Always-visible status strip (voices, MIDI/clock activity, CPU/block load, preset);
RGB LEDs mirror activity/clip. Fixed control positions — no layout shift while playing.

## Presets — full library (detail in `specs/05`)
Factory + user banks on SD, category/tag browser, A/B compare, INIT patch, randomize/morph.
Parameter-table-driven and versioned so the format survives feature growth.

## Still to design (not blocking)
- Exact musical-typing layout (lean: piano-style rows) and the live-tweak key map — settle
  when the PAX UI work starts on the real panel.
- MIDI-learn (assignable CC) — easy on top of the param table; schedule with MIDI I/O.
