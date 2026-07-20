# Juno-106 `TAPE SAVE` transport — clean-room note (WO-13g-i)

**Status: transport fully characterized. Decode of the two candidate evidence WAVs is
123/128 records clean, not 128/128 — split-if triggered per `WORK-ORDER.md`, reported below,
not forced.** This note is the transport description only. It contains no WAV audio, no
decoded patch data, and no third-party decoder was read, adapted, or used as a design input
at any point (clean-room isolation per stage-13 WO-13g-i).

Every constant below is tagged **[measured]** (recovered from the two candidate evidence
recordings) or **[Roland-fact]** (from Roland's public 18-byte record-shape documentation,
which says nothing about the tape transport itself).

## Signal [measured]

AC-coupled square-wave FSK on mono unsigned-8-bit PCM at 11025 Hz. Each square edge plays
back as a sharp spike + exponential ringing decay (tape/AC-coupling response); zero-crossing
counting over-counts due to ringing overshoot, so the reliable feature is the positive spike,
one per fundamental cycle.

## Modulation [measured]

Each cycle = two half-cycles, each SHORT (~2 samp) or LONG (~7 samp), split cleanly at 4
samples (bimodal histogram, empty gap 4–5 samp). **Short half-cycle = bit 1.** A cycle
therefore carries 2 bits. The three raw "tone" periods (3.5 / 8.65 / 13.75 samp) are just the
three half-cycle patterns SS / (SL|LS) / LL; SL vs. LS is disambiguated by the negative
spike's fractional position in the cycle (cleanly bimodal at 0.22 vs 0.78).

## Timing [measured]

Self-clocking / variable-rate — a record occupies a fixed number of cycles (~80), not a fixed
number of samples; record length in samples jitters. No tape-speed drift detected (half-cycle
periods stable to <1% across each file), so one fixed short/long threshold holds end to end.

## Framing [measured]

Standard async UART: 10-bit frames = start(1) + 8 data bits + stop(0), contiguous (no
inter-record gaps). Data bits are **LSB-first and inverted**. A self-syncing receiver (hunt
start bit, read 8, verify stop bit) decodes both evidence files with **zero frame errors**.

## Block structure [measured]

Per file: 41 lead-in header bits, then 64 records × 128 payload bits, contiguous:

```
[ 41 header bits ] [ record 0 : 128 bits ] ... [ record 63 : 128 bits ]
```

Each record = 18 parameter fields of 7 bits, LSB-first, packed contiguously across UART byte
boundaries (126 bits) + 2 trailing spare bits. Byte-stream autocorrelation peaks sharply at
lag 16 (16 UART bytes = 128 bits), independently confirming the record period;
`(bytes − 5) / 16 = 64.00` exactly in both evidence files. Field order matches the
Roland-fact 18-byte layout below (fields 16/17 land exactly on the two switch bytes, which
pins the packing phase — see Validity rule).

## Payload field layout [Roland-fact]

18 fields, 0–127 each: 0 LFO rate, 1 LFO delay, 2 DCO LFO depth, 3 PWM amount, 4 noise level,
5 VCF cutoff, 6 VCF resonance, 7 VCF ENV depth, 8 VCF LFO depth, 9 VCF key tracking, 10 VCA
level, 11–14 ENV A/D/S/R, 15 sub level, 16 switch byte 1 (range bits 0–2, pulse bit 3, saw
bit 4, chorus-off bit 5 inverted, chorus I/II bit 6), 17 switch byte 2 (PWM manual bit 0, VCF
ENV inverse bit 1, VCA gate bit 2, HPF bits 3–4 inverted). This is public record shape, not a
tape-transport fact — it says nothing about carrier, framing, or timing.

## Validity rule [measured]

**The transport has no arithmetic per-record checksum.** Exhaustively tested against all 128
decoded records: 8-bit and 7-bit sum ≡ 0, two's-complement sum ≡ 0, XOR/longitudinal parity,
every field as sum/xor-of-the-other-17 (mod 128 and mod 256), same on the 16 raw UART bytes,
per-record parity in the 2 spare bits, spare-bits = sum mod 4 — **none holds**. Records are
not double-written (all 64 per file are unique). Integrity is provided only by (a) UART
start/stop framing, and (b) record-contract plausibility: every field ≤127 (guaranteed by
7-bit packing), switch byte 16 < 128, switch byte 17 < 32.

## Result on the two candidate evidence recordings

| file | records | plausible (framing-clean + switch enums legal) |
|---|---:|---:|
| `junot020.wav` (bank A) | 64 | **62 / 64** |
| `junot040.wav` (bank B) | 64 | **61 / 64** |

**123/128 total, 0 framing errors.** The 5 failures are deterministic — two independent
demodulators (positive-peak+negative-spike-split, and interleaved positive/negative
half-cycle timing) agree exactly on which records fail, so this is a property of the
recorded data, not decoder noise:

- **Record 63, both files** — tail fields (16, 17) decode to the same fixed alternating
  lead-out pattern in both banks; this is the tape's run-out overwriting the last record's
  tail (also shows up as a 1-bit shortfall in the header/record bit count). A systematic
  boundary effect of these two specific recordings, not a transport ambiguity.
- **Record 61 (bank A); records 44, 51 (bank B)** — genuine multi-bit data errors (no
  single-bit flip restores switch-enum legality), with normal local amplitude, clean cycle
  periods, and no framing slip. Source-recording bit errors that this checksum-less format
  has no mechanism to catch or repair.

## Split-if — reported, not forced

Per `WORK-ORDER.md`: the waveform does not yield a consistent 64-record, all-valid decode for
either file, and the transport itself carries no per-record checksum to validate or repair the
residue against. This is reported as designed, not papered over. The transport is fully
characterized; 96% of records (123/128) recover cleanly from the two candidate evidence
recordings measured. The gap is a property of those two specific recordings (tape lead-out +
a few source bit errors), not missing knowledge of the format — a cleaner capture of the same
two banks would be expected to decode 128/128 against this same transport description.

## Provenance

Clean-room per stage-13 WO-13g-i: derived solely from measuring the two candidate evidence
recordings (peak/period/phase statistics) plus Roland's public 18-byte record-shape
documentation (payload field layout only, no transport information). No third-party
tape/MIDI-dump decoder, bank file, or generated header was read, consulted, or used as a
design input. See stage-13 doc's provenance-gate section for the still-open question of
redistribution rights on the underlying WAV recordings (unaffected by this transport
description, which is Roland-fact + our own measurements only).
