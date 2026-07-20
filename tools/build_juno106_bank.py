#!/usr/bin/env python3
"""
tools/build_juno106_bank.py — offline mapper: pinned KR-106 Juno-106 factory
controller data -> the embedded JSON factory bank. WO-14g, per ADR 0028.

There is NO runtime record decoder (ADR 0027 superseded the earlier plan): the
128 records are mapped to patches once, offline, by this script. The result
is committed and embedded verbatim via EMBED_TXTFILES (main/CMakeLists.txt),
then parsed at boot by the existing bank_json codec (engine/bank_json.cpp) —
the identical mechanism already used for engine/banks/neiro_factory.json.

Input provenance and controller order are committed alongside the raw data.
The conversion and its bank-only semantic approximations are documented in
specs/notes/juno106-control-curves.md.

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
RECORDS_JSON = os.path.join(ROOT, "third_party", "kr106-presets", "juno106_factory_raw.json")
BANK_JSON = os.path.join(ROOT, "engine", "banks", "juno106_factory.json")

N_RECORDS = 128
N_FIELDS = 44

# WO-14b generated these exact seconds tables offline from the same pinned
# KR-106 commit. Keeping the bank conversion on the runtime's own timing law
# avoids the old generic ParamCurve fallback. Neiro's public ADSR seam tops
# out at 5 seconds, so build_patch clamps table values at that boundary.
J106_ATTACK_SECONDS = (
    0.00100000005, 0.00523350015, 0.0137005011, 0.0221675001, 0.0306345019, 0.0433350019, 0.0518020056,
    0.0602690056, 0.0645025074, 0.0772030056, 0.0856700018, 0.0899035111, 0.0983704999, 0.111071005,
    0.119538009, 0.128005013, 0.132238507, 0.140705511, 0.153406009, 0.161873013, 0.166106507,
    0.17880702, 0.187274009, 0.195741013, 0.204208016, 0.208441511, 0.221142009, 0.229608998,
    0.233842507, 0.246543005, 0.255010009, 0.263476998, 0.267710537, 0.280411035, 0.284644514,
    0.297345012, 0.301578492, 0.31427902, 0.318512529, 0.331213027, 0.335446507, 0.343913525,
    0.356614023, 0.360847533, 0.373548031, 0.38201502, 0.390482008, 0.398949027, 0.403182507,
    0.415883005, 0.420116514, 0.428583503, 0.437050521, 0.44551751, 0.453984529, 0.462451518,
    0.475152045, 0.479385525, 0.492086023, 0.496319503, 0.504786551, 0.517487049, 0.525954008,
    0.534421027, 0.538654506, 0.555588543, 0.568289042, 0.58098954, 0.597923577, 0.606390536,
    0.623324573, 0.640258491, 0.661426067, 0.674126565, 0.691060543, 0.71222806, 0.737629056,
    0.763030052, 0.77996403, 0.805365026, 0.834999561, 0.864634037, 0.890035033, 0.923903048,
    0.962004542, 1.00433958, 1.05090797, 1.08054256, 1.10170996, 1.13557804, 1.15674555,
    1.19484711, 1.21601462, 1.25834954, 1.28375053, 1.33455253, 1.35995352, 1.41498911,
    1.44462359, 1.50812602, 1.54199409, 1.60973001, 1.65206504, 1.73250151, 1.77907002,
    1.87220716, 1.92724264, 2.03731346, 2.10081601, 2.10081601, 2.1643188, 2.1643188,
    2.23628831, 2.23628831, 2.31249118, 2.31249118, 2.38869429, 2.38869429, 2.47759748,
    2.47759748, 2.56650114, 2.56650114, 2.66810513, 2.77394247, 2.88824725, 3.01525211,
    3.15072417, 3.30313015,
)

J106_DEC_REL_SECONDS = (
    0.00423349999, 0.00846699998, 0.00846699998, 0.0127005, 0.0211674999, 0.0211674999, 0.0254009999,
    0.0296345018, 0.0296345018, 0.0381015018, 0.0423349999, 0.0508019999, 0.0592690036, 0.0762030035,
    0.101604, 0.152406007, 0.156639501, 0.16510652, 0.16934, 0.173573509, 0.182040513,
    0.190507516, 0.194741026, 0.203207999, 0.211675018, 0.224375516, 0.232842505, 0.245543018,
    0.258243501, 0.275177538, 0.287878036, 0.309045523, 0.325979501, 0.351380497, 0.376781553,
    0.410649538, 0.444517553, 0.495319545, 0.546121538, 0.618091047, 0.698527575, 0.821299076,
    0.973705113, 1.22771502, 1.23618209, 1.26158321, 1.29121757, 1.329319, 1.35895371,
    1.39282155, 1.42668951, 1.47325814, 1.50712621, 1.55369449, 1.600263, 1.64683163,
    1.69763362, 1.74843562, 1.80770469, 1.88390768, 1.93470967, 2.00667906, 2.09558272,
    2.17601919, 2.23105454, 2.43849611, 2.46389699, 2.4977653, 2.53163314, 2.56973481,
    2.60360265, 2.6671052, 2.69673991, 2.75600863, 2.81104398, 2.85761261, 2.904181,
    2.94228292, 2.99731827, 3.0819881, 3.10738897, 3.1666584, 3.23862791, 3.31059742,
    3.35293198, 3.4587698, 3.51803875, 3.61964273, 3.67891169, 3.75934839, 3.83555126,
    3.9540894, 4.04299307, 4.14883041, 4.22926664, 4.47480965, 4.5044446, 4.60604858,
    4.74575424, 4.89392662, 5.05903244, 5.2072053, 5.36384487, 5.60092115, 5.75332689,
    5.93960094, 6.25711298, 6.45608759, 6.71009779, 7.04877758, 7.35782337, 7.8362093,
    8.06905079, 8.5220356, 8.91151905, 9.49574184, 10.0418625, 10.7869596, 11.7140951,
    12.8190384, 13.8393126, 15.1601648, 16.7942963, 19.1269531, 19.651907, 20.1726303,
    20.8965569, 21.7474918,
)

NORMALIZED_MAP = {
    5: "DCO LFO Depth",
    6: "OSC PWM",
    7: "Sub Level",
    8: "Noise Level",
    11: "Filter Res",
    12: "VCF Env Depth",
    13: "VCF LFO Depth",
    14: "VCF Key Track",
    15: "VCA Level",
}

# Neiro-only extensions with no Juno-panel equivalent: loaded neutral/unity
# for every imported original (juno106-control-curves.md "Neiro-only
# extensions").
EXTENSION_DEFAULTS = {
    "Osc Level": 1.0,
    "Master Gain": 1.0,
}


SWITCH_RANGES = {
    9: (0, 3),
    23: (0, 1),
    24: (0, 1),
    25: (0, 1),
    26: (0, 1),
    27: (0, 1),
    28: (0, 1),
    29: (0, 2),
    32: (0, 1),
    33: (0, 1),
    34: (0, 1),
    35: (0, 1),
}


def normalize(raw):
    return raw / 127.0


def exponential_seam(raw, lo, hi):
    """Bank-only fallback for Neiro seams expressed in physical units."""
    return lo * (hi / lo) ** normalize(raw)


def decode_octave(raw):
    return {-1: None, 0: -12.0, 1: 0.0, 2: 12.0}.get(raw)


def decode_chorus(values):
    # KR index 26 is a UI-derived "off" field, not a controller to import.
    # The two actual Juno buttons are mutually exclusive in the factory data.
    if values[28]:
        return 2.0
    if values[27]:
        return 1.0
    return 0.0


def build_patch(entry):
    """Map one raw KR-106 {name, values} record to a bank JSON patch."""
    name = entry["name"]
    values = entry["values"]
    if len(values) != N_FIELDS:
        raise ValueError("patch %s: expected %d fields, got %d" % (name, N_FIELDS, len(values)))
    for i, v in enumerate(values):
        if not (0 <= v <= 127):
            raise ValueError("patch %s: field %d out of 7-bit range: %r" % (name, i, v))
    for i, (lo, hi) in SWITCH_RANGES.items():
        if not (lo <= values[i] <= hi):
            raise ValueError("patch %s: switch %d out of range: %r" % (name, i, values[i]))

    params = dict(EXTENSION_DEFAULTS)

    for index, param_name in NORMALIZED_MAP.items():
        params[param_name] = normalize(values[index])
    if not values[25]:
        params["Sub Level"] = 0.0

    params["LFO1 Rate"] = exponential_seam(values[3], 0.01, 20.0)
    params["LFO1 Delay"] = 5.0 * normalize(values[4])
    params["Filter Cutoff"] = exponential_seam(values[10], 20.0, 20000.0)
    params["HPF Position"] = float(values[9])

    attack = min(J106_ATTACK_SECONDS[values[16]], 5.0)
    decay = min(J106_DEC_REL_SECONDS[values[17]], 5.0)
    sustain = normalize(values[18])
    release = min(J106_DEC_REL_SECONDS[values[19]], 5.0)
    for param_name, value in (
        ("Attack", attack),
        ("Decay", decay),
        ("Sustain", sustain),
        ("Release", release),
        ("Env2 Attack", attack),
        ("Env2 Decay", decay),
        ("Env2 Sustain", sustain),
        ("Env2 Release", release),
    ):
        params[param_name] = value

    octave = decode_octave(values[29])
    if octave is None:
        raise ValueError("patch %s: invalid octave: %r" % (name, values[29]))
    params["OSC Range"] = octave
    params["OSC Pulse"] = float(values[23])
    params["OSC Saw"] = float(values[24])
    params["Chorus Mode"] = decode_chorus(values)
    params["PWM Mode"] = float(values[33])
    params["VCF Env Polarity"] = float(values[34])
    params["VCA Gate Mode"] = float(values[35])

    return {"name": name, "params": params}


def build_bank(records_path=RECORDS_JSON):
    with open(records_path, "r") as f:
        source = json.load(f)
    entries = source["patches"]
    if len(entries) != N_RECORDS:
        raise ValueError("expected %d records, got %d" % (N_RECORDS, len(entries)))
    return [build_patch(e) for e in entries]


def render_bank_json(bank):
    # Deterministic, sorted-key JSON so the checked-in file is reproducible
    # byte-for-byte from the pinned raw JSON (the --check acceptance criterion).
    return json.dumps(bank, indent=2, sort_keys=True) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                     help="verify engine/banks/juno106_factory.json is byte-current "
                          "from raw KR-106 JSON; exit non-zero if stale")
    ap.add_argument("--records", default=RECORDS_JSON, help="path to raw KR-106 JSON (testing)")
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
            print("STALE: %s does not match raw KR-106 JSON — rerun without --check" % args.out,
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
