#!/usr/bin/env python3
"""
tools/build_juno106_bank.py — offline mapper: raw Juno-106 tape records
(third_party/juno106-factory/records.json) -> the embedded JSON factory bank
(engine/banks/juno106_factory.json). WO-13h, per ADR 0027.

There is NO runtime record decoder (ADR 0027 superseded the earlier plan): the
128 records are mapped to patches once, offline, by this script. The result
is committed and embedded verbatim via EMBED_TXTFILES (main/CMakeLists.txt),
then parsed at boot by the existing bank_json codec (engine/bank_json.cpp) —
the identical mechanism already used for engine/banks/neiro_factory.json.

Byte layout and switch-decode formulas are pinned in the Source record
contract (specs/stages/stage-13-juno106-factory-bank.md) and restated in
specs/notes/juno106-parameter-set.md §A. Continuous-value curves (byte
0-127 -> physical units) are documented in full, with rationale and
calibration-pending flags, in specs/notes/juno106-control-curves.md — this
script implements exactly what that note describes and nothing more.

Clean-room only: written from the Source record contract and public Roland
documentation already in this repository. No KR-106 or other third-party
decoder/curve table was read or consulted at any point (CLAUDE.md Code
Reuse & Licensing).

Usage:
    python3 tools/build_juno106_bank.py              # (re)write the bank
    python3 tools/build_juno106_bank.py --check       # verify it's current

Stdlib-only (json/argparse/os/sys/math). No numpy/scipy — those are only
needed by the (separate, already-run) tape waveform decoder.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECORDS_JSON = os.path.join(ROOT, "third_party", "juno106-factory", "records.json")
BANK_JSON = os.path.join(ROOT, "engine", "banks", "juno106_factory.json")

N_RECORDS = 128
N_FIELDS = 18

# The 8 tape-decode-residue slots (specs/notes/juno106-tape-format.md
# "Resolution") are NOT hardcoded here — the generator trusts each record's
# own "uncertain" flag in records.json, so this script never needs to know
# the list or re-derive it.

# ---- Curve helpers (specs/notes/juno106-control-curves.md) ----------------
# Every continuous byte reuses the curve already declared for its
# destination ParamId in engine/param_desc.cpp (CURVE_LIN / CURVE_EXP) —
# see the control-curves note for why, and which rows are calibration-
# pending pending real hardware measurement.

CURVE_LIN = "lin"
CURVE_EXP = "exp"


def decode_curve(byte7, curve, lo, hi):
    """Map a 7-bit tape field (0-127) to a physical value via the named
    ParamCurve formula (engine/param_desc.h), using that ParamId's own
    declared [lo, hi]. No third-party curve table involved."""
    if not (0 <= byte7 <= 127):
        raise ValueError("tape field out of 7-bit range: %r" % (byte7,))
    norm = byte7 / 127.0
    if curve == CURVE_LIN:
        return lo + norm * (hi - lo)
    if curve == CURVE_EXP:
        if lo <= 0:
            raise ValueError("CURVE_EXP requires lo > 0, got %r" % (lo,))
        return lo * (hi / lo) ** norm
    raise ValueError("unsupported curve: %r" % (curve,))


# byte index -> (param name, curve, lo, hi) for the 12 single-destination
# continuous bytes (everything except the shared ADSR and the two switch
# bytes, handled separately below).
CONTINUOUS_MAP = {
    0: ("LFO1 Rate", CURVE_EXP, 0.01, 20.0),
    1: ("LFO1 Delay", CURVE_LIN, 0.0, 5.0),
    2: ("DCO LFO Depth", CURVE_LIN, 0.0, 1.0),
    3: ("OSC PWM", CURVE_LIN, 0.0, 1.0),
    4: ("Noise Level", CURVE_LIN, 0.0, 1.0),
    5: ("Filter Cutoff", CURVE_EXP, 20.0, 20000.0),
    6: ("Filter Res", CURVE_LIN, 0.0, 1.0),
    7: ("VCF Env Depth", CURVE_LIN, 0.0, 1.0),
    8: ("VCF LFO Depth", CURVE_LIN, 0.0, 1.0),
    9: ("VCF Key Track", CURVE_LIN, 0.0, 1.0),
    10: ("VCA Level", CURVE_LIN, 0.0, 1.0),
    15: ("Sub Level", CURVE_LIN, 0.0, 1.0),
}

# byte index -> (ENV1 param, ENV2 param, curve, lo, hi). The one real Juno
# ADSR is decoded once and duplicated into both Neiro envelopes (Source
# record contract).
ADSR_MAP = {
    11: ("Attack", "Env2 Attack", CURVE_EXP, 0.001, 5.0),
    12: ("Decay", "Env2 Decay", CURVE_EXP, 0.001, 5.0),
    13: ("Sustain", "Env2 Sustain", CURVE_LIN, 0.0, 1.0),
    14: ("Release", "Env2 Release", CURVE_EXP, 0.001, 5.0),
}

# Neiro-only extensions with no Juno-panel equivalent: loaded neutral/unity
# for every imported original (juno106-control-curves.md "Neiro-only
# extensions").
EXTENSION_DEFAULTS = {
    "Osc Level": 1.0,
    "Filter Mode": 0.0,
    "Master Gain": 1.0,
}


def decode_switch16(byte16):
    """Range, Pulse, Saw, Chorus Mode — see control-curves.md "Switch byte
    16". Range's exact bit-pattern-to-octave mapping beyond the 3 canonical
    codes is calibration-pending; out-of-table codes clamp to the highest
    canonical position rather than erroring. A handful of records (the tape-
    decode-residue slots already flagged "uncertain" in records.json) carry
    a stray bit 7 from unresolved tape noise; masked off rather than treated
    as a hard error, since those slots are already marked uncertain to the
    user."""
    byte16 &= 0x7F
    range_field = min(byte16 & 0x7, 2)
    range_semi = {0: -12.0, 1: 0.0, 2: 12.0}[range_field]
    pulse_on = 1.0 if (byte16 >> 3) & 1 else 0.0
    saw_on = 1.0 if (byte16 >> 4) & 1 else 0.0
    chorus_off = bool((byte16 >> 5) & 1)  # inverted: raw 1 = physically off
    if chorus_off:
        chorus_mode = 0.0
    else:
        chorus_mode = 2.0 if (byte16 >> 6) & 1 else 1.0
    return range_semi, pulse_on, saw_on, chorus_mode


def decode_switch17(byte17):
    """PWM Mode, VCF Env Polarity, VCA Gate Mode, HPF Position — see
    control-curves.md "Switch byte 17". HPF formula is the exact Source
    record contract formula; the other three are direct bit reads. As with
    byte 16, stray high bits from tape-decode residue (already-flagged
    "uncertain" slots) are masked off rather than treated as a hard error."""
    byte17 &= 0x1F
    pwm_mode = 1.0 if byte17 & 1 else 0.0
    env_polarity = 1.0 if (byte17 >> 1) & 1 else 0.0
    vca_gate = 1.0 if (byte17 >> 2) & 1 else 0.0
    hpf_position = float(3 - ((byte17 >> 3) & 3))
    return pwm_mode, env_polarity, vca_gate, hpf_position


def build_patch(entry):
    """Map one records.json entry ({"slot", "uncertain", "record"}) to a
    bank_json patch dict ({"name", "params"})."""
    slot = entry["slot"]
    record = entry["record"]
    if len(record) != N_FIELDS:
        raise ValueError("slot %s: expected %d fields, got %d" % (slot, N_FIELDS, len(record)))
    for i, v in enumerate(record):
        if not (0 <= v <= 127):
            raise ValueError("slot %s: field %d out of 7-bit range: %r" % (slot, i, v))

    params = dict(EXTENSION_DEFAULTS)

    for byte_idx, (name, curve, lo, hi) in CONTINUOUS_MAP.items():
        params[name] = decode_curve(record[byte_idx], curve, lo, hi)

    for byte_idx, (name1, name2, curve, lo, hi) in ADSR_MAP.items():
        value = decode_curve(record[byte_idx], curve, lo, hi)
        params[name1] = value
        params[name2] = value

    range_semi, pulse_on, saw_on, chorus_mode = decode_switch16(record[16])
    params["OSC Range"] = range_semi
    params["OSC Pulse"] = pulse_on
    params["OSC Saw"] = saw_on
    params["Chorus Mode"] = chorus_mode

    pwm_mode, env_polarity, vca_gate, hpf_position = decode_switch17(record[17])
    params["PWM Mode"] = pwm_mode
    params["VCF Env Polarity"] = env_polarity
    params["VCA Gate Mode"] = vca_gate
    params["HPF Position"] = hpf_position

    name = slot + (" (uncertain)" if entry.get("uncertain") else "")
    return {"name": name, "params": params}


def build_bank(records_path=RECORDS_JSON):
    with open(records_path, "r") as f:
        entries = json.load(f)
    if len(entries) != N_RECORDS:
        raise ValueError("expected %d records, got %d" % (N_RECORDS, len(entries)))
    return [build_patch(e) for e in entries]


def render_bank_json(bank):
    # Deterministic, sorted-key JSON so the checked-in file is reproducible
    # byte-for-byte from records.json (the --check acceptance criterion).
    return json.dumps(bank, indent=2, sort_keys=True) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                     help="verify engine/banks/juno106_factory.json is byte-current "
                          "from records.json; exit non-zero if stale")
    ap.add_argument("--records", default=RECORDS_JSON, help="path to records.json (testing)")
    ap.add_argument("--out", default=BANK_JSON, help="output bank path (testing)")
    args = ap.parse_args(argv)

    bank = build_bank(args.records)
    text = render_bank_json(bank)

    if args.check:
        try:
            with open(args.out, "r") as f:
                current = f.read()
        except FileNotFoundError:
            print("MISSING: %s (run without --check to generate it)" % args.out, file=sys.stderr)
            return 1
        if current != text:
            print("STALE: %s does not match records.json — rerun without --check" % args.out,
                  file=sys.stderr)
            return 1
        print("OK: %s is current (%d patches)." % (args.out, len(bank)))
        return 0

    with open(args.out, "w") as f:
        f.write(text)
    print("wrote %d patches to %s" % (len(bank), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
