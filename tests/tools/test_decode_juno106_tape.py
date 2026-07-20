#!/usr/bin/env python3
"""
Stdlib-only tests for tools/decode_juno106_tape.py and the committed
third_party/juno106-factory/records.json it generated.

Deliberately does not import numpy/scipy: covers the pure record-layout
helpers (bit packing, slot labels, plausibility) and the shape of the
committed bank, not the waveform-decode path (which needs the real WAV
captures, not part of this repository).
"""
import importlib.util
import json
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

spec = importlib.util.spec_from_file_location(
    "decode_juno106_tape", os.path.join(ROOT, "tools", "decode_juno106_tape.py"))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class TestFieldValue(unittest.TestCase):
    def test_lsb_first(self):
        self.assertEqual(decoder.field_value([0, 0, 0, 0, 0, 0, 0]), 0)
        self.assertEqual(decoder.field_value([1, 0, 0, 0, 0, 0, 0]), 1)
        self.assertEqual(decoder.field_value([0, 1, 0, 0, 0, 0, 0]), 2)
        self.assertEqual(decoder.field_value([1, 1, 1, 1, 1, 1, 1]), 127)


class TestSlotLabel(unittest.TestCase):
    def test_boundaries(self):
        self.assertEqual(decoder.slot_label("A", 0), "A11")
        self.assertEqual(decoder.slot_label("A", 63), "A88")
        self.assertEqual(decoder.slot_label("B", 0), "B11")
        self.assertEqual(decoder.slot_label("B", 63), "B88")

    def test_uncertain_slots_match_known_record_indices(self):
        # The 8 tape-decode-residue records (specs/notes/juno106-tape-format.md
        # "Resolution"), identified by 0-indexed record position within each
        # file, must map to these canonical slot labels.
        cases = {
            ("A", 50): "A73", ("A", 51): "A74", ("A", 61): "A86",
            ("A", 62): "A87", ("A", 63): "A88",
            ("B", 44): "B65", ("B", 51): "B74", ("B", 63): "B88",
        }
        for (letter, idx), expected in cases.items():
            self.assertEqual(decoder.slot_label(letter, idx), expected)


class TestSwitchBytesLegal(unittest.TestCase):
    def test_bounds(self):
        base = [0] * 16
        self.assertTrue(decoder.switch_bytes_legal(base + [0, 0]))
        self.assertTrue(decoder.switch_bytes_legal(base + [127, 31]))
        self.assertFalse(decoder.switch_bytes_legal(base + [128, 0]))
        self.assertFalse(decoder.switch_bytes_legal(base + [0, 32]))


class TestCommittedRecordsJson(unittest.TestCase):
    def setUp(self):
        path = os.path.join(ROOT, "third_party", "juno106-factory", "records.json")
        with open(path) as f:
            self.records = json.load(f)

    def test_count_and_labels(self):
        self.assertEqual(len(self.records), 128)
        slots = [e["slot"] for e in self.records]
        self.assertEqual(len(set(slots)), 128)
        self.assertEqual(slots[0], "A11")
        self.assertEqual(slots[63], "A88")
        self.assertEqual(slots[64], "B11")
        self.assertEqual(slots[127], "B88")

    def test_uncertain_flags(self):
        expected = {"A73", "A74", "A86", "A87", "A88", "B65", "B74", "B88"}
        actual = {e["slot"] for e in self.records if e["uncertain"]}
        self.assertEqual(actual, expected)

    def test_records_are_seven_bit_clean(self):
        for e in self.records:
            self.assertEqual(len(e["record"]), 18)
            for v in e["record"]:
                self.assertTrue(0 <= v <= 127, f"{e['slot']}: field out of range: {v}")


if __name__ == "__main__":
    unittest.main()
