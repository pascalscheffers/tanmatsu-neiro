# ADR 0027 — JSON bank format, embedded/SD storage, 6-voice reset

**Status:** accepted (2026-07-18)

## Context
Stage 13 ships 128 original Juno-106 patches plus the 12 existing Neiro patches as
factory banks, and introduces a real user-bank concept. The prior plan (recorded in
spec 05) was a compact binary preset format with a runtime `uint8_t[18]` Juno-record
decoder running on-device. That pipeline adds firmware complexity (a decode-on-request
provider, a disposable wire-v3 blob) for data that is static after the tape is decoded
once. Separately, the Juno-fidelity pass (ADR 0026) adds per-voice DSP (independent
saw/pulse, square sub, HPF block, direct panel modulation) that needs CPU/RAM headroom,
and Stage 13 revisits polyphony to match the real Juno-106 (6 voices) rather than
Neiro's original 8 (ADR 0003).

## Decision
- **Preset/bank format is JSON**, parsed with **cJSON** (the ESP-IDF `json` component —
  already available via ESP-IDF, so this adds **no new firmware dependency**). All
  patches — factory and user — are JSON. This supersedes spec 05's binary preset
  format described there.
- **The 128 original Juno patches and the Neiro bank ship as JSON banks embedded in
  firmware flash** via `EMBED_TXTFILES` (CMake). **User banks are `.json` files on
  SD/AppFS**, following the existing storage-base-path convention (ADR 0007).
- **The 128 originals are decoded from their 18-byte Juno tape records exactly once,
  offline**, by build tooling (the clean-room tape decoder, Stage 13g-i/13h), directly
  into the embedded JSON bank. There is **no runtime record decoder** and no
  decode-on-request provider in firmware — the device only ever parses JSON.
- **The 12 Neiro factory patches move out of hardcoded `FactoryPreset` C data** into a
  Neiro JSON bank, embedded the same way as the originals.
- **Polyphony drops from 8 to 6 voices** (`kNumVoices`), matching the authentic
  Juno-106 voice count and freeing the CPU/RAM budget the added per-voice DSP from
  ADR 0026 needs. A PROFILE baseline job (Stage 13-baseline) measures the current
  8-voice worst-block cost and `sizeof(JunoVoice)` before the drop, so later split-if
  decisions have a real number to compare against.
- Old NVS/preset blobs written under the binary format **may fail closed to the
  default factory patch** on load — no migration path is required for the pre-JSON
  binary format; this is an accepted one-time break, not an ongoing compatibility
  target.

## Consequences
- Spec 05's `Patch` value object becomes a JSON document with the schema:
  `{ "name": string, "params": { <id-or-name>: value, ... }, "routes": [ {source,
  dest, depth, curve}, ... ] }`. See spec 05 for the full mapping to `ParamDesc`/
  `Routing`.
- Spec 02's dependency ledger gains a `cJSON` row (MIT, part of ESP-IDF's `json`
  component) and a source-gate note against the 128-patch payload's licensing.
- Firmware flash gains the embedded JSON banks (size cost tracked via `make size`) but
  loses the binary preset codec and runtime record decoder entirely — net firmware
  code is smaller and simpler; more of the complexity moves to offline build tooling
  that never ships.
- `kNumVoices` changes from 8 to 6 everywhere it is the single source of truth
  (ADR 0003's "one `kNumVoices`, never a literal 8" convention holds); UNISON doc
  comments and voice-stealing timing (`kNoteOnStartIntervalBlocks`) are revisited for
  the smaller pool in Stage 13-baseline.
- Any spec or code text describing the binary wire format is historical from this
  point forward; the JSON schema in spec 05 is the current contract.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
