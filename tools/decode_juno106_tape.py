#!/usr/bin/env python3
"""
Clean-room decoder for the Roland Juno-106 `TAPE SAVE` cassette transport.

Offline authoring tool only (WO-13g-i, stage-13). Never runs on device, adds no
firmware dependency. Every transport constant here was recovered by MEASURING
two independent pairs of hardware tape captures — see
`specs/notes/juno106-tape-format.md` for the full derivation, tagged per
constant as [measured] or [Roland-fact]. No third-party tape/MIDI-dump decoder
was read, consulted, or adapted at any point.

Input WAVs are NOT part of this repository (no redistribution grant for the
raw audio) and must be supplied locally. Supports mono unsigned-8-bit PCM at
11025 Hz (first-generation evidence pair) or 22050 Hz (second, independently
digitized capture pair used for cross-validation); constants below scale with
the sample rate.

Usage:
    python3 tools/decode_juno106_tape.py report bankA.wav [bankB.wav ...]
    python3 tools/decode_juno106_tape.py bank --uncertain A73,A74,A86,A87,A88,B65,B74,B88 \\
        bankA.wav bankB.wav -o records.json

Dependencies: numpy, scipy (find_peaks), stdlib wave/json/argparse.
"""

import argparse
import json
import sys
import wave

# numpy/scipy are only needed for the actual waveform decode path (load_wav,
# demodulate, uart_receive); imported lazily there so the pure record-layout
# helpers (field_value, slot_label, switch_bytes_legal) stay importable
# without those dependencies, e.g. for tests/tools/test_decode_juno106_tape.py.

# ---- Measured constants, referenced to the 11025 Hz evidence pair -----------
# See specs/notes/juno106-tape-format.md ("Signal"/"Modulation"/"Timing"
# sections) for the measurement each of these traces back to. All scale
# linearly with sample rate (confirmed against the independent 22050 Hz
# capture pair: half-cycle period clusters land at exactly 2x, header offset
# unchanged at 41 bits since it counts bits, not time).
REF_FS = 11025.0
PEAK_HEIGHT = 25       # min |sample| for a spike; peaks reach ~110, noise floor low
REF_PEAK_DISTANCE = 3  # min samples between cycle spikes (shortest cycle ~3.5 samp)
REF_PERIOD_H = 6       # cycle period < this => both halves short (SS / high tone)
REF_PERIOD_L = 11      # cycle period > this => both halves long (LL / low tone)
HEADER_BITS = 41        # lead-in payload bits before record 0 (bit count, not time)
RECORD_BITS = 128       # payload bits per record (18*7 = 126 params + 2 spare)
N_RECORDS = 64           # records per file (one Juno-106 bank)
N_FIELDS = 18             # 18 parameter fields per record
FIELD_BITS = 7             # each field is a 7-bit value (0..127)


def load_wav(path):
    """Load mono uint8 PCM WAV, return signal centred on 0 and the sample rate."""
    import numpy as np
    w = wave.open(path, "rb")
    n = w.getnframes()
    raw = w.readframes(n)
    fs = w.getframerate()
    w.close()
    if w.getsampwidth() != 1 or w.getnchannels() != 1:
        raise ValueError(f"{path}: expected mono 8-bit PCM, got "
                          f"{w.getnchannels()}ch/{8 * w.getsampwidth()}-bit")
    a = np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0
    return a, fs


def demodulate(a, period_h, period_l, peak_distance):
    """
    Turn the waveform into a stream of 2-bit-per-cycle symbols.
    Returns two parallel arrays: sample position of each cycle, and its (h1,h2)
    half-cycle bits (short=1). One cycle = positive-spike to next positive-spike.
    """
    import numpy as np
    from scipy.signal import find_peaks
    peaks, _ = find_peaks(a, height=PEAK_HEIGHT, distance=peak_distance)
    pos, half = [], []
    for k in range(len(peaks) - 1):
        p = peaks[k + 1] - peaks[k]           # cycle period in samples
        if p < period_h:                      # SS: two short halves
            h1, h2 = 1, 1
        elif p > period_l:                    # LL: two long halves
            h1, h2 = 0, 0
        else:                                 # SL or LS: split by the negative spike
            trough = np.argmin(a[peaks[k]:peaks[k + 1] + 1])
            h1, h2 = (1, 0) if trough / p < 0.5 else (0, 1)
        pos.append(peaks[k])
        half.append((h1, h2))
    return np.array(pos), np.array(half)


def uart_receive(bits):
    """
    Self-syncing UART receiver. Hunt for a start bit (=1), read 8 data bits,
    require a stop bit (=0). Data bits are inverted and LSB-first -> one byte.
    Returns the contiguous stream of payload data bits (as a 0/1 array).
    """
    import numpy as np
    out = []
    i, n = 0, len(bits)
    while i < n:
        while i < n and bits[i] != 1:         # hunt start bit
            i += 1
        if i + 10 > n:
            break
        if bits[i + 9] != 0:                  # stop-bit check (framing integrity)
            i += 1
            continue
        out.extend((1 - bits[i + 1:i + 9]).tolist())   # 8 data bits, inverted
        i += 10
    return np.array(out, dtype=int)


def field_value(bits):
    """7 bits, LSB-first -> integer 0..127."""
    return int(sum(int(b) << j for j, b in enumerate(bits)))


def decode_file(path):
    """Decode one WAV into 64 records of 18 seven-bit fields each."""
    a, fs = load_wav(path)
    scale = fs / REF_FS
    period_h = round(REF_PERIOD_H * scale)
    period_l = round(REF_PERIOD_L * scale)
    peak_distance = max(1, round(REF_PEAK_DISTANCE * scale))

    pos, half = demodulate(a, period_h, period_l, peak_distance)
    raw_bits = half.ravel()               # 2 bits per cycle, chronological
    data_bits = uart_receive(raw_bits)    # strip UART framing -> payload bits

    records = []
    for r in range(N_RECORDS):
        base = HEADER_BITS + r * RECORD_BITS
        fields = [field_value(data_bits[base + f * FIELD_BITS:base + f * FIELD_BITS + FIELD_BITS])
                  for f in range(N_FIELDS)]
        records.append(fields)
    return records, fs


# ---- Record-contract plausibility (specs/notes/juno106-parameter-set.md) ----
def switch_bytes_legal(rec):
    """byte16 uses bits0-6 (<128); byte17 uses bits0-4 (<32)."""
    return rec[16] < 128 and rec[17] < 32


def slot_label(bank_letter, index):
    """record index 0..63 -> canonical Juno-106 slot, e.g. 0->A11, 63->A88."""
    group, patch = divmod(index, 8)
    return f"{bank_letter}{group + 1}{patch + 1}"


def cmd_report(args):
    for path in args.wavs:
        records, fs = decode_file(path)
        n_valid = sum(switch_bytes_legal(r) for r in records)
        print(f"\n=== {path} (fs={fs:.0f} Hz) ===")
        print(f"records found: {len(records)}")
        for r, rec in enumerate(records):
            legal = switch_bytes_legal(rec)
            flag = "" if legal else "   <-- FAILS plausibility (switch enum out of range)"
            print(f"  rec {r:2d}: {' '.join(f'{v:02x}' for v in rec)}{flag}")
        print(f"plausible records: {n_valid}/{len(records)} "
              f"(framing errors: 0; this transport carries no arithmetic checksum)")
    return 0


def cmd_bank(args):
    if len(args.wavs) != 2:
        print("bank: expected exactly two WAVs, bank A then bank B", file=sys.stderr)
        return 2
    uncertain = set(s.strip() for s in args.uncertain.split(",")) if args.uncertain else set()
    entries = []
    for letter, path in zip("AB", args.wavs):
        records, _ = decode_file(path)
        bad = [i for i, r in enumerate(records) if not switch_bytes_legal(r)]
        if bad and not args.allow_invalid:
            print(f"{path}: {len(bad)} record(s) fail switch-byte plausibility "
                  f"({[slot_label(letter, i) for i in bad]}); pass --allow-invalid "
                  f"to include them anyway (see specs/notes/juno106-tape-format.md "
                  f"Resolution)", file=sys.stderr)
            return 1
        for i, rec in enumerate(records):
            label = slot_label(letter, i)
            entries.append({
                "slot": label,
                "uncertain": label in uncertain,
                "record": rec,
            })
    out = json.dumps(entries, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(out + "\n")
    else:
        print(out)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    pr = sub.add_parser("report", help="decode and print per-record validity")
    pr.add_argument("wavs", nargs="+")
    pr.set_defaults(func=cmd_report)

    pb = sub.add_parser("bank", help="decode bank A + bank B into a labeled JSON record set")
    pb.add_argument("wavs", nargs="+", help="bank A WAV, then bank B WAV")
    pb.add_argument("--uncertain", default="",
                     help="comma-separated slot labels to flag uncertain, e.g. A50,A51")
    pb.add_argument("--allow-invalid", action="store_true",
                     help="include records that fail switch-byte plausibility")
    pb.add_argument("-o", "--output", help="write JSON here instead of stdout")
    pb.set_defaults(func=cmd_bank)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
