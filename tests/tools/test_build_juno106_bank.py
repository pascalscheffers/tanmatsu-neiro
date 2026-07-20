#!/usr/bin/env python3
"""Stdlib-only regression tests for the pinned KR-106 factory-bank import."""
import importlib.util
import json
import math
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

spec = importlib.util.spec_from_file_location(
    "build_juno106_bank", os.path.join(ROOT, "tools", "build_juno106_bank.py"))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def load_raw():
    with open(builder.RECORDS_JSON, "r") as f:
        return json.load(f)


class TestPinnedRawArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = load_raw()
        cls.patches = cls.raw["patches"]

    def test_exact_provenance(self):
        metadata = self.raw["metadata"]
        self.assertEqual(metadata["upstream_project"], "Ultramaster KR-106")
        self.assertEqual(metadata["upstream_version"], "2.5.13")
        self.assertEqual(
            metadata["upstream_commit"],
            "bc15caee5843ab238a25d0969e68d57db2b1615f")
        self.assertEqual(metadata["source_path"], "Source/KR106_Presets_JUCE.h")
        self.assertEqual(metadata["license"], "GPL-3.0-only")
        self.assertEqual(
            metadata["source_range"],
            {"bank": "J106", "end": 255, "start": 128})
        self.assertEqual(
            metadata["source_sha256"],
            "09dbe2669b7e0fd99bc1119229bdc9cdcc62f98942a5541d8c906efd039f8f04")
        self.assertEqual(len(metadata["parameter_order"]), 44)
        self.assertIn("pinned Git blob", metadata["extraction_method"])

    def test_exact_record_shape_and_bounds(self):
        self.assertEqual(len(self.patches), 128)
        for patch in self.patches:
            self.assertEqual(set(patch), {"name", "values"})
            self.assertEqual(len(patch["values"]), 44)
            for value in patch["values"]:
                self.assertIs(type(value), int)
                self.assertGreaterEqual(value, 0)
                self.assertLessEqual(value, 127)

    def test_exact_boundaries_and_known_source_values(self):
        self.assertEqual(self.patches[0]["name"], "A11 Brass")
        self.assertEqual(self.patches[-1]["name"], "B88 Owgan")
        self.assertEqual(
            self.patches[0]["values"],
            [0, 0, 120, 20, 49, 0, 102, 0, 0, 1, 35, 13, 58, 0, 86, 108,
             3, 49, 45, 32, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
             0, 0, 0, 1, 2, 0, 0, 0, 1])
        self.assertEqual(
            self.patches[-1]["values"],
            [0, 0, 120, 50, 0, 0, 45, 56, 0, 0, 38, 84, 32, 0, 127, 101,
             0, 49, 55, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0,
             0, 0, 0, 1, 2, 0, 0, 0, 1])

    def test_unique_complete_slot_prefixes(self):
        prefixes = [patch["name"].split(" ", 1)[0] for patch in self.patches]
        expected = [
            "%s%d%d" % (bank, group, slot)
            for bank in ("A", "B")
            for group in range(1, 9)
            for slot in range(1, 9)
        ]
        self.assertEqual(prefixes, expected)
        self.assertEqual(len(prefixes), len(set(prefixes)))

    def test_declared_switch_ranges(self):
        for patch in self.patches:
            for index, (lo, hi) in builder.SWITCH_RANGES.items():
                self.assertGreaterEqual(patch["values"][index], lo)
                self.assertLessEqual(patch["values"][index], hi)


class TestControlConversion(unittest.TestCase):
    def test_timing_tables_match_runtime_shape(self):
        self.assertEqual(len(builder.J106_ATTACK_SECONDS), 128)
        self.assertEqual(len(builder.J106_DEC_REL_SECONDS), 128)
        self.assertEqual(builder.J106_ATTACK_SECONDS[0], 0.00100000005)
        self.assertEqual(builder.J106_ATTACK_SECONDS[127], 3.30313015)
        self.assertEqual(builder.J106_DEC_REL_SECONDS[0], 0.00423349999)
        self.assertEqual(builder.J106_DEC_REL_SECONDS[127], 21.7474918)

    def test_physical_seam_formulas(self):
        raw = 63
        norm = raw / 127.0
        self.assertAlmostEqual(
            builder.exponential_seam(raw, 0.01, 20.0),
            0.01 * (20.0 / 0.01) ** norm)
        self.assertAlmostEqual(
            builder.exponential_seam(raw, 20.0, 20000.0),
            20.0 * (20000.0 / 20.0) ** norm)

    def test_switch_semantics(self):
        self.assertEqual([builder.decode_octave(i) for i in range(3)],
                         [-12.0, 0.0, 12.0])
        values = [0] * 44
        self.assertEqual(builder.decode_chorus(values), 0.0)
        values[27] = 1
        self.assertEqual(builder.decode_chorus(values), 1.0)
        values[28] = 1
        self.assertEqual(builder.decode_chorus(values), 2.0)

    def test_a11_independent_spot_check(self):
        patch = builder.build_bank()[0]
        params = patch["params"]
        self.assertEqual(patch["name"], "A11 Brass")
        self.assertAlmostEqual(params["LFO1 Rate"],
                               0.01 * (20.0 / 0.01) ** (20.0 / 127.0))
        self.assertAlmostEqual(params["LFO1 Delay"], 5.0 * 49.0 / 127.0)
        self.assertAlmostEqual(params["Filter Cutoff"],
                               20.0 * (20000.0 / 20.0) ** (35.0 / 127.0))
        self.assertEqual(params["Attack"], builder.J106_ATTACK_SECONDS[3])
        self.assertEqual(params["Decay"], builder.J106_DEC_REL_SECONDS[49])
        self.assertAlmostEqual(params["Sustain"], 45.0 / 127.0)
        self.assertEqual(params["Release"], builder.J106_DEC_REL_SECONDS[32])
        self.assertEqual(params["OSC Range"], -12.0)
        self.assertEqual(params["OSC Pulse"], 0.0)
        self.assertEqual(params["OSC Saw"], 1.0)
        self.assertEqual(params["HPF Position"], 1.0)
        self.assertEqual(params["Chorus Mode"], 1.0)
        self.assertAlmostEqual(params["OSC PWM"], 102.0 / 127.0)
        self.assertAlmostEqual(params["Filter Res"], 13.0 / 127.0)
        self.assertAlmostEqual(params["VCF Env Depth"], 58.0 / 127.0)
        self.assertAlmostEqual(params["VCF Key Track"], 86.0 / 127.0)
        self.assertAlmostEqual(params["VCA Level"], 108.0 / 127.0)

    def test_sub_switch_and_long_time_clamp(self):
        raw_patch = load_raw()["patches"][0]
        entry = {"name": raw_patch["name"], "values": list(raw_patch["values"])}
        entry["values"][7] = 127
        entry["values"][25] = 0
        entry["values"][17] = 127
        entry["values"][19] = 127
        params = builder.build_patch(entry)["params"]
        self.assertEqual(params["Sub Level"], 0.0)
        self.assertEqual(params["Decay"], 5.0)
        self.assertEqual(params["Release"], 5.0)
        self.assertEqual(params["Env2 Decay"], 5.0)
        self.assertEqual(params["Env2 Release"], 5.0)

    def test_invalid_raw_and_switch_values_rejected(self):
        raw_patch = load_raw()["patches"][0]
        for index, value in ((3, 128), (29, 3)):
            entry = {"name": raw_patch["name"], "values": list(raw_patch["values"])}
            entry["values"][index] = value
            with self.assertRaises(ValueError):
                builder.build_patch(entry)


class TestBuildBank(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bank = builder.build_bank()

    def test_names_are_descriptive_and_have_no_uncertain_suffix(self):
        self.assertEqual(len(self.bank), 128)
        self.assertEqual(self.bank[0]["name"], "A11 Brass")
        self.assertEqual(self.bank[63]["name"], "A88 Caverns")
        self.assertEqual(self.bank[64]["name"], "B11 Strings")
        self.assertEqual(self.bank[127]["name"], "B88 Owgan")
        self.assertFalse(any("(uncertain)" in patch["name"] for patch in self.bank))
        self.assertTrue(all(" " in patch["name"] for patch in self.bank))

    def test_neiro_extensions_and_no_legacy_noops(self):
        forbidden = {"Filter Mode", "Chorus Rate", "Chorus Depth", "Chorus Delay"}
        for patch in self.bank:
            params = patch["params"]
            self.assertEqual(params["Osc Level"], 1.0)
            self.assertEqual(params["Master Gain"], 1.0)
            self.assertTrue(forbidden.isdisjoint(params))

    def test_adsr_duplicated_into_env2(self):
        for patch in self.bank:
            params = patch["params"]
            for env1, env2 in (
                    ("Attack", "Env2 Attack"), ("Decay", "Env2 Decay"),
                    ("Sustain", "Env2 Sustain"), ("Release", "Env2 Release")):
                self.assertEqual(params[env1], params[env2])

    def test_all_values_finite_and_in_declared_ranges(self):
        ranges = {
            "Osc Level": (0.0, 1.0), "Sub Level": (0.0, 1.0),
            "Noise Level": (0.0, 1.0), "OSC PWM": (0.0, 1.0),
            "OSC Range": (-24.0, 24.0), "OSC Saw": (0.0, 1.0),
            "OSC Pulse": (0.0, 1.0), "DCO LFO Depth": (0.0, 1.0),
            "PWM Mode": (0.0, 1.0), "Filter Cutoff": (20.0, 20000.0),
            "Filter Res": (0.0, 1.0), "VCF Env Depth": (0.0, 1.0),
            "VCF Env Polarity": (0.0, 1.0), "VCF Key Track": (0.0, 1.0),
            "VCF LFO Depth": (0.0, 1.0), "HPF Position": (0.0, 3.0),
            "Attack": (0.001, 5.0), "Decay": (0.001, 5.0),
            "Sustain": (0.0, 1.0), "Release": (0.001, 5.0),
            "Env2 Attack": (0.001, 5.0), "Env2 Decay": (0.001, 5.0),
            "Env2 Sustain": (0.0, 1.0), "Env2 Release": (0.001, 5.0),
            "LFO1 Rate": (0.01, 20.0), "LFO1 Delay": (0.0, 5.0),
            "Master Gain": (0.0, 2.0), "VCA Gate Mode": (0.0, 1.0),
            "VCA Level": (0.0, 1.0), "Chorus Mode": (0.0, 2.0),
        }
        for patch in self.bank:
            self.assertEqual(set(patch["params"]), set(ranges))
            for name, value in patch["params"].items():
                self.assertIs(type(value), float, name)
                self.assertTrue(math.isfinite(value), name)
                lo, hi = ranges[name]
                self.assertGreaterEqual(value, lo, name)
                self.assertLessEqual(value, hi, name)


class TestCommittedBankIsCurrent(unittest.TestCase):
    def test_render_matches_committed_file(self):
        text = builder.render_bank_json(builder.build_bank())
        with open(builder.BANK_JSON, "r") as f:
            self.assertEqual(text, f.read())

    def test_check_flag_exits_zero(self):
        result = subprocess.run(
            [sys.executable, os.path.join(ROOT, "tools", "build_juno106_bank.py"), "--check"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_detects_stale_output(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as stale:
            stale.write("[]\n")
            stale_path = stale.name
        try:
            result = subprocess.run(
                [sys.executable, os.path.join(ROOT, "tools", "build_juno106_bank.py"),
                 "--check", "--out", stale_path],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
        finally:
            os.unlink(stale_path)


if __name__ == "__main__":
    unittest.main()
