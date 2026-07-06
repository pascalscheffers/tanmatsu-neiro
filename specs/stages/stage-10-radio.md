# Stage 10 — C6 radio (§4): jam sync, BLE-MIDI, radio mod source

> **Status: pre-runbook campaign brief.** Opus-authored map from `specs/FABLE-THOUGHTS.md` §4.
> NOT yet closed work-orders — resolve gates (this stage is gate-heavy: it costs real heap, flash,
> and power), then author each per `stages/README.md` (ADR 0017).
>
> **Execution unit:** one clean Sonnet 5 context per sub-stage. 10a (bring-up) is L and is
> deliberately its own context; 10a′ (protocol) is S once bring-up lands.
>
> **Depends on:** Stage 4 clock (`engine/clock.h`, 96 PPQN, transport, tap tempo — the jam-sync
> follower slews `CLOCK_BPM` and phase-locks the arp tick), Stage 5 MIDI transport seam (BLE-MIDI),
> Stage 9 arp/seq (what actually syncs). **Everything here is gated behind "radio page ON"** —
> the C6 stack is off today for heap/power reasons.
>
> Grounding: `specs/FABLE-THOUGHTS.md` §4a/§4b/§4c. Seams: `engine/clock.h` (BPM/transport/tap),
> `platform/platform.h` (MIDI transport seam, declared intent), a new RADIO UI page. The C6 hangs
> off SDIO and needs `esp-hosted`/`esp_wifi_remote` on the P4 side.

## The reframe (why, and why cautiously)
Badge-to-badge jam sync (§4a) is the one feature that **makes no sense on any synth except a
badge** — the most Tanmatsu-unique idea in the file. But the radio stack is real cost, so we gate
everything and pick features where *wireless is the point*, not a convenience. This is the last
campaign — schedule when an L-sized slot exists.

## Where we are entering Stage 10
- Stage 4 clock exposes BPM/transport/tap via `engine_*`; the arp/seq (Stage 9) run off it. A
  follower can slew `CLOCK_BPM` + phase-lock without touching audio-thread structure.
- Radio is **off**: no `esp-hosted`/`esp_wifi_remote` in the build. 10a pays that tax first.

## Kickoff gates (this stage is gate-dense)
- **G10a — esp-hosted bring-up cost (architecture + CPU/heap/flash/power). 🛑 OPEN, blocks all.**
  What does standing up the C6 stack cost in heap/flash/power, and is it acceptable behind a
  runtime "radio ON" toggle? This is the bulk of §4a. Recommendation: bring up + **measure** before
  committing any sync logic; if the cost is prohibitive, the whole stage re-scopes.
- **G10b — BLE-MIDI latency positioning (sonic/UX). 🛑 confirm at 10b.** Connection intervals →
  7.5–15 ms jitter: fine for pads/browsing, mediocre for drumming. Position as "wireless
  DAW/patch-librarian link", **not** the performance path (USB stays that). Confirm scope.
- **G10-Link — Ableton Link is GPLv2 (licensing). 🛑 HARD GATE.** Per CLAUDE.md, GPL is
  ask-before-vendoring and changes the license of what it touches. **Do not vendor Link** without an
  explicit decision recorded in `specs/decisions/`. ESP-NOW TSF sync (10a′) is our license-clean
  path and is the recommendation. RTP-MIDI over WiFi needs AP+mDNS (camp-hostile) — skip unless a
  use case demands it.

## Sub-stage decomposition (running order: 10a → 10a′ → 10b → 10c)

**10a — esp-hosted / C6 bring-up (§4a prerequisite, FOUNDATION).** Stand up the radio stack behind
a "radio page ON" toggle; ESP-NOW send/recv of a smoke-test packet; **measure heap/flash/power**.
This is the bulk of §4a and must land before any sync logic. *Seams:* `platform/device/` radio
init, a RADIO UI page (ON/OFF), build-config gating. **G10a gates.** *Acceptance:* radio initializes
on demand, smoke packet round-trips between two badges, cost measured + recorded. *One-context fit:*
L but self-contained — its own context; **split-if** bring-up + smoke-test exceed one context →
`10a-i` (stack init + toggle + cost measurement) / `10a-ii` (ESP-NOW smoke send/recv).

**10a′ — ESP-NOW jam sync protocol (§4a, the killer demo).** RADIO page JAM ON/OFF +
master/follow; master broadcasts `{tsf_downbeat, bpm, step}` a few times per bar; followers
schedule the downbeat against the WiFi **TSF timer** (shared µs clock) — jitter collapses to timer
accuracy, not packet luck. Loss-tolerant (each beacon re-anchors). Follower slews `CLOCK_BPM` +
phase-locks the arp tick. **Depends on 10a.** *Seams:* new `control/jamsync.*` (beacon encode/decode
+ follower slew), `engine/clock.h` (set BPM + phase), RADIO page controls. *Acceptance:* two badges
lock tempo + downbeat; follower recovers after dropped beacons. *One-context fit:* yes (protocol is
S once bring-up is done).

**10b — BLE-MIDI (§4b).** Wireless MIDI device to phone/tablet/DAW via the C6's BLE (lighter than
the WiFi heap appetite). Positioned as DAW/librarian link, not the perf path. **G10b gates scope.**
*Seams:* `platform/` MIDI transport seam (BLE backend), reuse the existing MIDI parse/dispatch from
Stage 5. *Acceptance:* enumerates as a BLE-MIDI device, notes + CC flow. *One-context fit:* M.

**10c — Radio as a modulation source (§4c, gimmick tier).** Channel RSSI / packet-arrival entropy →
a `RADIO` mod-matrix source (sample-and-hold random that reacts to people walking past). ~20 lines.
**After 10a** (needs the stack up). *Seams:* `engine/mod_matrix.h` new source, a radio-stat feed
from `platform/device/`. *Acceptance:* `RADIO` selectable as a mod source; modulates a dest audibly.
*One-context fit:* easily (S).

## Continuous (every sub-stage)
`make size` (radio stack moves flash + heap materially — watch the budget); host green (radio is
device-only — host stubs the seam, membrane stays clean); device green. Record the radio-stack cost
as a budget row. **No GPL vendored without a recorded decision (G10-Link).**

## First action at kickoff
Read this brief + `engine/clock.h` (BPM/transport/tap) + `platform/platform.h` MIDI/transport seam
+ the C6/esp-hosted notes in the build docs. Resolve **G10a** (the cost question) with Pascal
*before* authoring 10a — if the cost is prohibitive the stage re-scopes. Then author 10a, dispatch.
