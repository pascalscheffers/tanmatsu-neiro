# Stage 5 — MIDI I/O (campaign brief)

> **Status: pre-runbook.** Opus-authored campaign map for Stage 5, written at the pause after
> Stage 4b so the next (fresh-context) session can drive it. NOT yet a set of Sonnet-executable
> work-orders — the **Kickoff gates** below must be resolved with Pascal first, then each
> sub-stage's closed work-order is authored per `stages/README.md` (ADR 0017).
>
> Grounding: roadmap `specs/06` (Stage 5 row), **ADR 0005** (USB-A host MIDI first),
> **ADR 0010** (clock is a pluggable source — MIDI-clock-in slots in here), spec `03`
> (control & UI — note-input sources + performance layer), spec `02` (dependency table:
> `esp_tinyusb`/TinyUSB MIDI, host RtMidi). Seams: `specs/MAP.md`.
>
> **Read order for the resuming session:** `CLAUDE.md` → `specs/00-overview.md` → `specs/MAP.md`
> → `specs/MEMORY.md` (last entries + open gates) → `stages/README.md` → this brief → ADR 0005
> + ADR 0010 + spec 03 (note-input section).

## Where we are entering Stage 5
- **Stage 4 is paused after 4b**, by Pascal's call (pivot to MIDI I/O). **Done & on `main`:**
  4a (sample-accurate clock + event scheduler + BPM param) and 4b (full arpeggiator —
  up/down/up-down/order/random, octaves, gate, swing, latch; free-running, ADR 0019).
  **Deferred (still on the roadmap, revisit after MIDI or per Pascal):** 4d FX (tempo-synced
  delay + DaisySP `ReverbSc`, G4 ratified, device-CPU gate pending) and 4c pattern sequencer
  (data-model gate G3 still open).
- **Reusable seams MIDI will build on (don't re-derive):**
  - `control/keyboard.{c,h}` — the *existing* note source: parses `platform_event_t` → calls
    `engine_note_on/off`. MIDI input is a **second source of the same shape** (ADR 0005:
    "all sources normalize to one internal note event stream"). Mirror it.
  - `engine/synth.h` C API — `engine_note_on/off`, `engine_set_param[_norm]`, `engine_get_param`.
    The lock-free control→audio entry points MIDI drives. `NoteExpression{bend,pressure,timbre,
    channel}` (`engine/voice.h`) is already **MPE-ready**.
  - `engine/command_queue.h` (`NoteCmd` ring) + `engine/scheduler.h` (4a) — the SMF player
    timestamps file events into the **4a scheduler**; no new timing engine needed.
  - **`ParamDesc.midi_cc`** (per-row default CC, `param_desc.h`) — the CC→param map already
    lives in the table. MIDI CC handling reads it; MIDI-learn writes it. Central dedup — protect it.
  - `engine/clock.h` — pluggable source (ADR 0010): **MIDI-clock-in** is a new source feeding the
    same clock; this is where the **`CLOCK_SOURCE` param deferred from 4a-iii** becomes real.
- **The HAL MIDI seam does not exist yet.** `platform.h` wires audio/display/input/storage but
  **MIDI transport is declared intent, absent** (by design — "let need pull out the seam").
  Stage 5 adds it. Two backends must satisfy it: `platform/device/` (IDF USB) and
  `platform/host/` (RtMidi, per spec 02).

## The headline risk (own it early)
**USB-host (USB-A) MIDI class driver availability on the ESP32-P4 is unverified** (ADR 0005
§Consequences; spec 02 line ~169). IDF USB-host + a MIDI class driver "may need a small driver."
This risk is owned by *this* stage. **Mitigation baked into the order below:** stand up the HAL
seam + the **host (RtMidi)** path + note normalization FIRST (zero device-USB risk, fully
host-testable), so engine-side MIDI is proven before the device USB-host spike. A **driver
feasibility spike (Explore/triage)** precedes committing 5b scope (gate G1).

## Sub-stage decomposition (suggested — confirm at kickoff)
**Status (2026-06-29):** ✅ 5a (host RtMidi + parser/router), ✅ 5d (USB-C device, TinyUSB),
✅ 5b (USB-A host, vendored CC0 class driver) — all hardware-verified; device+host coexist.
Remaining: 5c (expression/CC), 5e (SMF player), optional 5f (MIDI-clock-in).

**5a — MIDI HAL seam + host (RtMidi) backend + note-event normalization (FOUNDATION).** Add the
MIDI transport seam to `platform.h` (poll/queue incoming messages; out path stubbed). Host impl =
RtMidi. New `control/midi_in.{c,h}`: parse MIDI bytes → `engine_note_on/off` + expression/CC,
normalizing all sources to one stream (ADR 0005). Host-testable (feed raw MIDI byte streams →
assert engine calls). *Unblocks all MIDI work without the device-USB risk.*

**5b — USB-A host MIDI (device backend).** The `platform/device/` impl of the seam: IDF USB host +
MIDI class driver (**the risk — gate G1 spike first**). Plug a hardware controller into USB-A →
plays the synth standalone. The "real instrument" moment (ADR 0005).

**5c — MIDI expression + control mapping.** Pitch-bend, mod-wheel (CC1), velocity (have), channel
aftertouch, **sustain/hold pedal (CC64)**, **panic**, and **CC→param via `ParamDesc.midi_cc`**.
Maps onto `NoteExpression` + the param store. (Performance layer, spec 03 §Decided control features.)

**5d — USB-C MIDI device mode.** `esp_tinyusb`/TinyUSB MIDI class (MIT/Apache, spec 02). Badge
appears as a MIDI device to a computer/DAW. Additive given 5a's normalization.

**5e — SMF player from SD.** Parse `.mid` (SMF type 0/1) → timestamp note events into the **4a
scheduler** → current patch (mono-timbral). Play with nothing plugged in (spec 03).

**(5f, optional/late) — MIDI-clock-in as external clock source + `CLOCK_SOURCE` param.** ADR 0010
designed for it; closes the 4a-iii deferral. Schedule when external sync is wanted.

*Order rationale:* 5a is the hard prerequisite (seam + normalization) and de-risks by going
host-first. Then the USB-A spike (G1) decides whether 5b or 5d (USB-C device) comes next.

## 🛑 Kickoff gates — resolve with Pascal BEFORE authoring work-orders
- **G1 — USB-host MIDI driver feasibility (RISK / architecture).** Spike: what does IDF/TinyUSB-host
  give on the P4? Decide 5b scope/effort and whether USB-C device (5d) jumps ahead of USB-A host.
  Investigate (Explore/triage) before committing — don't author 5b blind.
- **G2 — MIDI transport HAL seam shape (architecture).** The `platform.h` MIDI contract: poll-based
  message queue vs callback; raw bytes (parse in `control/`) vs parsed events; in-only now or in+out.
  Mirrors the `platform_poll_event` input seam. Sizes how MIDI crosses the membrane.
- **G3 — Internal note-event / expression model (architecture).** Confirm the one `note_event`
  (note on/off, velocity, channel, bend, CC, aftertouch) and how it extends `command_queue`/
  `NoteExpression`. Channel handling (omni vs filter; multitimbral is out-of-scope, spec 06).
- **G4 — MPE scope (sonic/feature).** How much MPE now vs later (per-note bend/pressure/timbre →
  mod matrix). `NoteExpression` is ready; the question is depth of routing in v1.
- **G5 — CC mapping + MIDI-learn (feature).** Fixed CC→param via `ParamDesc.midi_cc` now; MIDI-learn
  (assignable CC, writes the table) — in this stage or deferred (spec 03 "schedule with MIDI I/O").
- **G6 — Host MIDI dependency (licensing/dep).** Confirm **RtMidi** for `platform/host/` (spec 02
  names it) + `esp_tinyusb` for device — both permissive; record versions in spec 02's dep table.

## ✅ Kickoff gate resolutions (2026-06-29, with Pascal)
- **First push:** 5a only (foundation) — host-testable, zero device-USB risk.
- **G1 RESOLVED ON HARDWARE (2026-06-29):** the USB-A host risk is retired. The 5b-i spike on
  the device enumerated a class-compliant controller (Novation, VID 0x1235), matched the
  MIDIStreaming interface, and received 4-byte USB-MIDI packets — `usb_host_install(peripheral_map=0)`
  targets the P4 OTG-HS = USB-A, USB-A VBUS via `bsp_power_set_usb_host_boost_enabled(true)`, no PHY
  swap needed. Driver = vendored CC0 (PR #12566). 5b-ii wires it to the engine + coexists with 5d.
- **G1 + ordering → [ADR 0018](../decisions/0018-usb-c-device-midi-first.md):** research showed
  USB-C device (5d) has a first-party TinyUSB example (we already vendor TinyUSB) while USB-A host
  (5b) has no mainline ESP-IDF driver. **Device order flips to USB-C device first, USB-A host
  later** (amends ADR 0005). G1's host-driver spike still owned by this stage, just sequenced after
  5d. New sub-stage order: 5a → 5c → 5d → 5b → 5e.
- **G1 deep-dive — USB-A host MIDI driver (2026-06-29, current docs IDF v5.5.1):** confirmed
  there is **no first-party ESP-IDF USB-host MIDI component** (registry host class drivers = CDC-ACM
  / MSC / HID / UVC / UAC; UAC is audio-streaming, *not* MIDIStreaming). A small custom MIDIStreaming
  class driver on the native **USB Host Library** is required (claim iface class 0x01/subclass 0x03,
  bulk-IN, 4-byte USB-MIDI Event Packets). **Reuse, don't write from scratch — vendor base:**
  esp-idf PR #12566 `examples/.../usb/host/midi/main/midi_class_driver.c` (~313 lines, **license
  `Unlicense OR CC0-1.0`** — pristine; receive-only; targets S2/S3 but the Host Library API is
  identical on P4). **P4-proven cross-reference (no license → reference only, do NOT vendor):**
  `github.com/chegewara/esp32-p4-host-midi-demo` (native Host Library, runs on P4-EV board).
  `sauloverissimo/ESP32_Host_MIDI` is Arduino/TinyUSB-host only → not usable in ESP-IDF.
  **Architecture notes for 5b:** USB-A is the P4 **OTG-HS** controller — independent of the USB-C
  **FS** PHY, so 5b needs **no `usb_serial_jtag_ll_phy_select` swap** (that was USB-C-only) and can
  in principle run *alongside* the 5d device. `CONFIG_USB_HOST_*` is already enabled. Open work:
  verify on P4 silicon, confirm host+device controller coexistence, thread bulk-IN packets through
  the existing `platform_midi_read` → 5a parser/router. License of our port = MIT (project policy).
- **G2 — seam shape:** mirror `platform_poll_event` — **poll-based, raw MIDI bytes** (parse in
  `control/midi_in`), **in-only** for now (out path added with 5d). Seam:
  `size_t platform_midi_read(uint8_t* buf, size_t max_len)`.
- **G3 — note-event model:** parser emits channel-voice messages; `control/midi_in` normalizes all
  sources to `engine_note_on/off`. **Omni** channel handling in v1 (accept all channels). Note-On
  vel 0 → Note-Off. Expression fields (bend/CC/aftertouch) framed but acted on in 5c.
- **G4 — MPE:** **deferred.** Channel-wide expression only in v1; per-note routing to the mod matrix
  is later. `NoteExpression` stays ready.
- **G5 — CC/MIDI-learn:** **fixed CC→param via `ParamDesc.midi_cc` in 5c; MIDI-learn deferred.**
- **G6 — host dep:** **RtMidi as a system dependency via pkg-config** (`brew install rtmidi`),
  mirroring SDL2 — NOT vendored. Recorded in spec 02's dep table. `esp_tinyusb` pinned at 5d.

## Continuous (every sub-stage)
Track `make size`; keep host + device green; **profile before optimizing**; membrane stays clean
(MIDI/USB symbols live ONLY in `platform/`; `control/` sees the parsed stream, never `esp_`/USB
types). Record every new dependency (RtMidi, TinyUSB MIDI) in spec 02's table with version + license.

## First action at kickoff
Read this brief + ADR 0005 + ADR 0010 + spec 03 (note-input + performance layer), run **G1's
driver-feasibility spike** (Explore/haiku) and **G2/G3** with Pascal, then author the **5a** closed
work-order (HAL MIDI seam + RtMidi host backend + `control/midi_in` normalization) and dispatch a
fresh Sonnet worker. Everything downstream hangs off 5a's note-event normalization.
</content>
