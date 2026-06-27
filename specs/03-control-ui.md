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

## Open questions
- See `specs/02` Q3 (MIDI role priority) and Q5 (physical controls in scope).
- Musical-typing layout: piano-style (recommended, familiar) vs chromatic/isomorphic?
- Do we want an on-device step sequencer / arpeggiator in scope for v1, or MIDI-in only?
