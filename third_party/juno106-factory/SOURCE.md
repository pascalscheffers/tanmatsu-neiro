# Juno-106 factory bank — decoded parameter records

`records.json` is the 128 raw 18-byte Juno-106 `TAPE SAVE` parameter records (canonical
slots `A11`–`A88`, `B11`–`B88`), decoded **once, offline**, by `tools/decode_juno106_tape.py`
from two Roland Juno-106 hardware `TAPE SAVE` cassette recordings. It is the raw source-record
payload only — 18 unsigned 7-bit fields per slot, field order and meaning documented in
`specs/notes/juno106-parameter-set.md` ("Source record contract"). It is **not** yet mapped
through Neiro's control curves into a `PresetPatch`; that mapping is stage-13 WO-13h (not done
here).

## What is and isn't in this repository

- **In:** `records.json` (decoded parameter bytes only) and the decoder tool
  (`tools/decode_juno106_tape.py`), plus the transport description it implements
  (`specs/notes/juno106-tape-format.md`).
- **Not in:** the source WAV audio. No cassette recording, in any form (original capture or a
  re-digitized copy), is vendored into this repository. The 8-byte-per-field parameter data in
  `records.json` is Pascal's own hardware capture, decoded independently by a clean-room
  transport implementation (see Provenance below); it carries no Roland copyrighted expression
  (audio, artwork, firmware) — only the numeric slider/switch positions the original synth
  patch used.

This resolves the stage-13 WO-13g-ii Opus gate (`specs/MEMORY.md` "Open Opus gates") for the
**decoded parameter records only**. The gate's original, broader ask — vendoring the licensed
WAV audio itself — is now moot: that audio is deliberately excluded, not license-cleared.
Pascal made this scope call explicitly on 2026-07-20: ship the decoded numbers, not the
recording.

## Source captures (not included; hashes are the identity check for reproducing this file)

Two independently-digitized capture pairs of the same two physical cassette tapes (Roland
Juno-106 factory banks A and B), per `specs/notes/juno106-tape-format.md`. `records.json` was
generated from the newer (22050 Hz) pair — cross-validation in that note shows it agrees with
the older pair on 120/128 records byte-for-byte, and is the tiebreak source for the other 8 (see
"Resolution" in that note).

| capture | role | PCM shape | SHA-256 |
|---|---|---|---|
| `junot020.wav` | bank A, original evidence | mono u8 PCM, 11025 Hz | `0c5d2e93dc98a88ebc66920aa8b1ff805aefeb17771219b9e7b4b06f6b8b8bc3` |
| `junot040.wav` | bank B, original evidence | mono u8 PCM, 11025 Hz | `542b2c62242ded92d7f0957574cf64c3a9b279a24363df9aaddbb0c9dc35b4d9` |
| `JUNO106 Bank A.wav` | bank A, second capture — **used to generate `records.json`** | mono u8 PCM, 22050 Hz | `bbeaa46fd73ec95162763750877cc2c72c5b11bdd6a6d5613ebf08e0c418a3c9` |
| `JUNO106 Bank B.wav` | bank B, second capture — **used to generate `records.json`** | mono u8 PCM, 22050 Hz | `a06ee32c2974e3427787a24e98472abdc8569c4a99f5bd960f672bee9eef554e` |

## Regenerating `records.json`

```
python3 tools/decode_juno106_tape.py bank \
  --uncertain A73,A74,A86,A87,A88,B65,B74,B88 \
  --allow-invalid \
  -o third_party/juno106-factory/records.json \
  "JUNO106 Bank A.wav" "JUNO106 Bank B.wav"
```

`--allow-invalid` is required because 8 of the 128 records fail strict switch-byte plausibility
(tape lead-out and pre-existing source-tape/dub content errors, not decode bugs) — see
`specs/notes/juno106-tape-format.md` "Resolution" for the full disposition and why each of the
8 uncertain slots is still included rather than dropped.

## Provenance

Clean-room per stage-13 WO-13g-i (`specs/stages/stage-13-juno106-factory-bank.md`): the
transport was derived solely from measuring the evidence recordings plus Roland's public
18-byte record-shape documentation. No third-party tape/MIDI-dump decoder, bank file, or
generated header was read, consulted, or used as a design input. Full transport derivation,
every constant tagged `[measured]` or `[Roland-fact]`, and the disposition of the 8 uncertain
slots: `specs/notes/juno106-tape-format.md`.

## Status

`records.json` holds raw source-record bytes, not yet decoded into Neiro `PresetPatch` values
(control curves: stage-13 WO-13h, not started) or exposed through the preset browser (WO-13i).
