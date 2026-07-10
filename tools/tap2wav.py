#!/usr/bin/env python3
"""tap2wav.py — decode a [TAP] serial capture into a WAV file.

Usage:
    python3 tools/tap2wav.py sniff.log -o tap.wav

Ground truth for the poly onset-crackle diagnosis (specs/MEMORY.md,
2026-07-10): the [TAP] lines are a RAM tap of the last ~341 ms of rendered
stereo audio, post soft_clip -- exactly what was handed to the DAC --
captured by engine/synth.cpp (SYNTH_PROFILE builds only) and printed by
app/app.c the first time a loud transient (peak > 1.2) freezes the tap.

Capture the serial log with `make sniff`, smash keys until the crackle is
audible, wait for the "[TAP] end" line, then run this script. Interpretation:
flattened/saturated onsets in the resulting WAV mean the crackle is amplitude
clipping at render time; a clean WAV despite an audibly crackling take means
the fault is downstream (DMA/codec side).

Finds the `[TAP] hdr` / `[TAP] d` / `[TAP] end` lines anywhere in the capture
(other console output is ignored), decodes the base64 payload, verifies it
against the reported CRC-32 (a mismatch is a warning, not a hard failure --
serial capture can drop lines), and writes a PCM WAV at the reported sample
rate / channel count. Prints the trigger frame index and its timestamp.

Stdlib only (base64, binascii, wave, argparse, re).
"""
import argparse
import base64
import binascii
import os
import re
import sys
import tempfile
import wave

HDR_RE = re.compile(r"\[TAP\] hdr sr=(\d+) ch=(\d+) fmt=(\S+) frames=(\d+) trig_frame=(\d+)")
DATA_RE = re.compile(r"\[TAP\] d (\S+)")
END_RE = re.compile(r"\[TAP\] end crc32=([0-9a-fA-F]+)")


def parse(path):
    """Scan a text file for [TAP] lines. Returns (hdr_dict_or_None, [b64_str, ...], end_crc_or_None)."""
    hdr = None
    chunks = []
    end_crc = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            if hdr is None:
                m = HDR_RE.search(line)
                if m:
                    hdr = {
                        "sr": int(m.group(1)),
                        "ch": int(m.group(2)),
                        "fmt": m.group(3),
                        "frames": int(m.group(4)),
                        "trig_frame": int(m.group(5)),
                    }
                    continue
            m = DATA_RE.search(line)
            if m:
                chunks.append(m.group(1))
                continue
            m = END_RE.search(line)
            if m:
                end_crc = int(m.group(1), 16)
    return hdr, chunks, end_crc


def decode(hdr, chunks, end_crc):
    """Decode chunks against hdr; returns (raw_bytes, warnings_list)."""
    warnings = []
    raw = bytearray()
    bad = 0
    for c in chunks:
        try:
            raw += base64.b64decode(c)
        except (binascii.Error, ValueError):
            bad += 1
    if bad:
        warnings.append(f"{bad} data line(s) failed to decode (dropped)")

    expected_bytes = hdr["frames"] * hdr["ch"] * 2
    if len(raw) != expected_bytes:
        warnings.append(
            f"got {len(raw)} bytes, expected {expected_bytes} "
            f"({len(chunks)} data lines found) -- serial capture may have dropped lines"
        )

    if end_crc is not None:
        actual_crc = binascii.crc32(bytes(raw)) & 0xFFFFFFFF
        if actual_crc != end_crc:
            warnings.append(f"crc32 mismatch (capture={end_crc:08x} computed={actual_crc:08x})")
    else:
        warnings.append("no [TAP] end line found; crc32 not verified")

    return bytes(raw), warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sniff_log", nargs="?", help="serial capture text file (from `make sniff`)")
    ap.add_argument("-o", "--out", default="tap.wav", help="output WAV path (default: tap.wav)")
    ap.add_argument("--selftest", action="store_true", help="run a small round-trip self-test and exit")
    args = ap.parse_args()

    if args.selftest:
        _selftest()
        return

    if not args.sniff_log:
        ap.error("sniff_log is required unless --selftest")

    hdr, chunks, end_crc = parse(args.sniff_log)
    if hdr is None:
        print("error: no [TAP] hdr line found in capture", file=sys.stderr)
        sys.exit(1)
    if not chunks:
        print("error: no [TAP] d lines found in capture", file=sys.stderr)
        sys.exit(1)

    raw, warnings = decode(hdr, chunks, end_crc)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    if not warnings:
        print("crc32 OK")

    with wave.open(args.out, "wb") as w:
        w.setnchannels(hdr["ch"])
        w.setsampwidth(2)
        w.setframerate(hdr["sr"])
        w.writeframes(raw)

    trig_ms = hdr["trig_frame"] * 1000.0 / hdr["sr"]
    print(f"wrote {args.out}: {hdr['frames']} frames, {hdr['ch']} ch, {hdr['sr']} Hz")
    print(f"trigger at frame {hdr['trig_frame']} ({trig_ms:.1f} ms into the tap)")


def _selftest():
    """Round-trip: synthesize a small fake sniff log, parse+decode it, check bytes/crc match."""
    frames = 12
    ch = 2
    sr = 48000
    trig_frame = 5
    raw = bytes([(i * 7) % 256 for i in range(frames * ch * 2)])
    crc = binascii.crc32(raw) & 0xFFFFFFFF

    lines = [
        "[PROFILE] audio avg=100 max=200 over=0/1000 us-budget=1333",  # noise line, must be ignored
        f"[TAP] hdr sr={sr} ch={ch} fmt=s16le frames={frames} trig_frame={trig_frame}",
    ]
    chunk_size = 8  # deliberately not a multiple of the frame size, to exercise mid-frame line splits
    for off in range(0, len(raw), chunk_size):
        chunk = raw[off : off + chunk_size]
        lines.append("[TAP] d " + base64.b64encode(chunk).decode("ascii"))
    lines.append(f"[TAP] end crc32={crc:08x}")

    fd, path = tempfile.mkstemp(suffix=".log")
    os.close(fd)
    out_path = path + ".wav"
    try:
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")

        hdr, chunks, end_crc = parse(path)
        assert hdr is not None, "hdr not parsed"
        assert hdr["frames"] == frames, "frames mismatch"
        assert hdr["ch"] == ch, "ch mismatch"
        assert hdr["trig_frame"] == trig_frame, "trig_frame mismatch"
        assert end_crc == crc, "end crc32 not parsed correctly"

        decoded, warnings = decode(hdr, chunks, end_crc)
        assert decoded == raw, "round-trip byte mismatch"
        assert warnings == [], f"unexpected warnings: {warnings}"

        with wave.open(out_path, "wb") as w:
            w.setnchannels(hdr["ch"])
            w.setsampwidth(2)
            w.setframerate(hdr["sr"])
            w.writeframes(decoded)
        with wave.open(out_path, "rb") as w:
            assert w.getnchannels() == ch
            assert w.getframerate() == sr
            assert w.readframes(frames) == raw

        print("selftest OK")
    finally:
        for p in (path, out_path):
            if os.path.exists(p):
                os.remove(p)


if __name__ == "__main__":
    main()
