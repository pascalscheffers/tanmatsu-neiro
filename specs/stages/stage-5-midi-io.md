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
