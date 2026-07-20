#!/usr/bin/env python3
"""
Stdlib-only tests for tools/build_juno106_bank.py (WO-13h) and the committed
engine/banks/juno106_factory.json it generates from
third_party/juno106-factory/records.json. No numpy/scipy dependency (unlike
the tape waveform decoder) — this only exercises the pure record->patch
mapping.
"""
import importlib.util
import json
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

spec = importlib.util.spec_from_file_location(
    "build_juno106_bank", os.path.join(ROOT, "tools", "build_juno106_bank.py"))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class TestDecodeCurve(unittest.TestCase):
    def test_lin_endpoints_and_midpoint(self):
        self.assertAlmostEqual(builder.decode_curve(0, builder.CURVE_LIN, 0.0, 1.0), 0.0)
        self.assertAlmostEqual(builder.decode_curve(127, builder.CURVE_LIN, 0.0, 1.0), 1.0)
        self.assertAlmostEqual(builder.decode_curve(0, builder.CURVE_LIN, 20.0, 40.0), 20.0)

    def test_exp_endpoints(self):
        self.assertAlmostEqual(builder.decode_curve(0, builder.CURVE_EXP, 20.0, 20000.0), 20.0)
        self.assertAlmostEqual(builder.decode_curve(127, builder.CURVE_EXP, 20.0, 20000.0), 20000.0, places=3)

    def test_monotonic(self):
        # Every curve must be non-decreasing across the full 7-bit range —
        # required for the "simplest monotonic fallback" claim.
        vals = [builder.decode_curve(b, builder.CURVE_EXP, 0.01, 20.0) for b in range(128)]
        self.assertEqual(vals, sorted(vals))
        vals = [builder.decode_curve(b, builder.CURVE_LIN, 0.0, 1.0) for b in range(128)]
        self.assertEqual(vals, sorted(vals))

    def test_exp_rejects_nonpositive_lo(self):
        with self.assertRaises(ValueError):
            builder.decode_curve(64, builder.CURVE_EXP, 0.0, 1.0)

    def test_out_of_range_byte_rejected(self):
        with self.assertRaises(ValueError):
            builder.decode_curve(128, builder.CURVE_LIN, 0.0, 1.0)


class TestDecodeSwitch16(unittest.TestCase):
    def test_range_canonical_values(self):
        semi0, _, _, _ = builder.decode_switch16(0b000)
        semi1, _, _, _ = builder.decode_switch16(0b001)
        semi2, _, _, _ = builder.decode_switch16(0b010)
        self.assertEqual((semi0, semi1, semi2), (-12.0, 0.0, 12.0))

    def test_range_out_of_table_clamps_high(self):
        # Values 3-7 are not a legal 16'/8'/4' code; clamp to the top
        # canonical position rather than raising (calibration-pending, see
        # juno106-control-curves.md).
        semi, _, _, _ = builder.decode_switch16(0b111)
        self.assertEqual(semi, 12.0)

    def test_pulse_and_saw_independent_bits(self):
        _, pulse, saw, _ = builder.decode_switch16(0b11000)
        self.assertEqual((pulse, saw), (1.0, 1.0))
        _, pulse, saw, _ = builder.decode_switch16(0b00000)
        self.assertEqual((pulse, saw), (0.0, 0.0))

    def test_chorus_off_and_modes(self):
        # bit5 set (inverted) => chorus off, regardless of bit6.
        _, _, _, mode = builder.decode_switch16(0b0100000)
        self.assertEqual(mode, 0.0)
        # bit5 clear, bit6 clear => chorus I.
        _, _, _, mode = builder.decode_switch16(0b0000000)
        self.assertEqual(mode, 1.0)
        # bit5 clear, bit6 set => chorus II.
        _, _, _, mode = builder.decode_switch16(0b1000000)
        self.assertEqual(mode, 2.0)

    def test_masks_stray_high_bit(self):
        # Tape-decode residue (uncertain slots) can carry a stray bit 7;
        # must not raise.
        builder.decode_switch16(0xFF)


class TestDecodeSwitch17(unittest.TestCase):
    def test_pwm_mode_bit0(self):
        self.assertEqual(builder.decode_switch17(0b00000)[0], 0.0)
        self.assertEqual(builder.decode_switch17(0b00001)[0], 1.0)

    def test_env_polarity_bit1(self):
        self.assertEqual(builder.decode_switch17(0b00010)[1], 1.0)

    def test_vca_gate_bit2(self):
        self.assertEqual(builder.decode_switch17(0b00100)[2], 1.0)

    def test_hpf_formula_exact(self):
        # position = 3 - ((byte17 >> 3) & 3) — Source record contract formula.
        self.assertEqual(builder.decode_switch17(0b00000)[3], 3.0)
        self.assertEqual(builder.decode_switch17(0b01000)[3], 2.0)
        self.assertEqual(builder.decode_switch17(0b10000)[3], 1.0)
        self.assertEqual(builder.decode_switch17(0b11000)[3], 0.0)

    def test_masks_stray_high_bits(self):
        builder.decode_switch17(0xFF)


class TestBuildBank(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bank = builder.build_bank()

    def test_128_patches(self):
        self.assertEqual(len(self.bank), 128)

    def test_slot_label_boundaries(self):
        names = [p["name"] for p in self.bank]
        self.assertEqual(names[0], "A11")
        self.assertEqual(names[63], "A88 (uncertain)")
        self.assertEqual(names[64], "B11")

    def test_exactly_8_uncertain(self):
        uncertain = [p["name"] for p in self.bank if "(uncertain)" in p["name"]]
        self.assertEqual(len(uncertain), 8)
        # Not hardcoded from a fixed list — derived from records.json's own
        # "uncertain" flag. Cross-check against the currently-known residue
        # slots (specs/notes/juno106-tape-format.md "Resolution") without
        # the generator itself consulting this list.
        expected = {"A73", "A74", "A86", "A87", "A88", "B65", "B74", "B88"}
        actual = {n.split(" ")[0] for n in uncertain}
        self.assertEqual(actual, expected)

    def test_every_patch_has_neiro_extensions_neutral(self):
        for p in self.bank:
            self.assertEqual(p["params"]["Osc Level"], 1.0)
            self.assertEqual(p["params"]["Filter Mode"], 0.0)
            self.assertEqual(p["params"]["Master Gain"], 1.0)

    def test_adsr_duplicated_into_env2(self):
        for p in self.bank:
            params = p["params"]
            self.assertEqual(params["Attack"], params["Env2 Attack"])
            self.assertEqual(params["Decay"], params["Env2 Decay"])
            self.assertEqual(params["Sustain"], params["Env2 Sustain"])
            self.assertEqual(params["Release"], params["Env2 Release"])

    def test_all_values_are_finite_numbers(self):
        for p in self.bank:
            for k, v in p["params"].items():
                self.assertIsInstance(v, float, "%s should be a float" % k)
                self.assertFalse(v != v, "%s must not be NaN" % k)  # NaN != NaN


class TestCommittedBankIsCurrent(unittest.TestCase):
    """The --check acceptance criterion: engine/banks/juno106_factory.json
    must be byte-identical to what build_bank()/render_bank_json() produce
    from the committed records.json right now."""

    def test_render_matches_committed_file(self):
        bank = builder.build_bank()
        text = builder.render_bank_json(bank)
        with open(builder.BANK_JSON, "r") as f:
            committed = f.read()
        self.assertEqual(text, committed)

    def test_check_flag_exits_zero(self):
        result = subprocess.run(
            [sys.executable, os.path.join(ROOT, "tools", "build_juno106_bank.py"), "--check"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_committed_file_is_valid_json_array_of_128(self):
        with open(builder.BANK_JSON, "r") as f:
            data = json.load(f)
        self.assertEqual(len(data), 128)


if __name__ == "__main__":
    unittest.main()
