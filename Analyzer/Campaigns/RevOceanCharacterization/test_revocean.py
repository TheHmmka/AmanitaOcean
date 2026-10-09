#!/usr/bin/env python3
"""Tests of the Rev OCEAN capture helper.

The pure tests run anywhere. The reference tests need the pinned Analyzer and
the installed plug-in and are skipped when either is missing.
"""
from __future__ import annotations

import unittest

import numpy as np

import revocean

REFERENCE_AVAILABLE = revocean.ANALYZER.exists() and revocean.PLUGIN.exists() and revocean.PROCESSOR_LIBRARY.exists()


class DisplayLaws(unittest.TestCase):
    def test_declared_defaults(self):
        # The plug-in declares these normalised defaults for its neutral positions.
        self.assertAlmostEqual(revocean.BASELINE["master"], 0.693671, places=6)
        self.assertAlmostEqual(revocean.BASELINE["decay"], 0.396849, places=6)
        self.assertAlmostEqual(revocean.BASELINE["size"], 0.500003, places=5)
        self.assertAlmostEqual(revocean.normalised("width", 100.0), 0.5, places=7)

    def test_display_points_of_the_inspection(self):
        # Display texts the plug-in returns at 0.25 / 0.5 / 0.75.
        for key, points in {
            "decay": ((0.25, 1.98), (0.5, 6.44), (0.75, 19.8)),
            "size": ((0.25, 61.9), (0.5, 100.0), (0.75, 146.0)),
            "predelay": ((0.25, 17.3), (0.5, 94.9), (0.75, 442.0)),
            "hpf": ((0.25, 358.0), (0.5, 1536.0), (0.75, 5648.0)),
            "width": ((0.25, 58.6), (0.5, 100.0), (0.75, 129.0)),
            "master": ((0.25, -27.8), (0.5, -7.86), (0.75, 1.55)),
            "return": ((0.25, -12.0), (0.75, 12.0)),
            "transients": ((0.25, -2.5), (1.0, -10.0)),
        }.items():
            for position, text in points:
                self.assertAlmostEqual(revocean.CONTROL[key].display(position) / text, 1.0, delta=0.006, msg=key)

    def test_round_trip(self):
        for key in ("decay", "size", "predelay", "hpf", "lpf", "width", "master", "macro", "brightness"):
            control = revocean.CONTROL[key]
            for position in (0.0, 0.1, 0.37, 0.5, 0.9, 1.0):
                self.assertAlmostEqual(control.normalised(control.display(position)), position, places=9, msg=key)

    def test_out_of_range_display_value(self):
        with self.assertRaises(ValueError):
            revocean.normalised("decay", 0.2)


class Settings(unittest.TestCase):
    def test_resolve_sets_every_control(self):
        resolved = revocean.resolve({"macro": 0.25})
        self.assertEqual(set(resolved), set(revocean.CONTROL))
        self.assertEqual(resolved["macro"], 0.25)
        self.assertEqual(resolved["mix"], 1.0)

    def test_resolve_is_single_precision(self):
        resolved = revocean.resolve({"decay": 0.1173741234567})
        self.assertEqual(resolved["decay"], float(np.float32(0.1173741234567)))

    def test_resolve_rejects_unknown_freeze_and_range(self):
        with self.assertRaises(KeyError):
            revocean.resolve({"mode": 1.0})
        with self.assertRaises(ValueError):
            revocean.resolve({"decay": 1.0})
        with self.assertRaises(ValueError):
            revocean.resolve({"size": 1.5})

    def test_vst3_ids_are_unique_and_complete(self):
        self.assertEqual(sorted(control.vst3_id for control in revocean.CONTROLS), list(range(15)))


class Helpers(unittest.TestCase):
    def test_impulses(self):
        stimulus = revocean.impulses(0.01, [(10, None, 0.5), (20, 1, -0.25)])
        self.assertEqual(stimulus.shape, (480, 2))
        self.assertEqual(stimulus[10].tolist(), [0.5, 0.5])
        self.assertEqual(stimulus[20].tolist(), [0.0, -0.25])
        self.assertEqual(float(np.abs(stimulus).sum()), 1.25)

    def test_null_db(self):
        reference = np.ones((100, 2), np.float32)
        self.assertAlmostEqual(revocean.null_db(reference * 1.1, reference), -20.0, places=5)
        self.assertLess(revocean.null_db(reference, reference), -1000.0)

    def test_capture_rejects_bad_stimuli(self):
        with self.assertRaises(ValueError):
            revocean.capture(np.zeros(100, np.float32))
        with self.assertRaises(ValueError):
            revocean.capture(np.zeros((0, 2), np.float32))
        with self.assertRaises(ValueError):
            revocean.capture(np.full((10, 2), np.nan, np.float32))


@unittest.skipUnless(REFERENCE_AVAILABLE, "needs the pinned Analyzer and the installed Rev OCEAN")
class Reference(unittest.TestCase):
    def test_identity_is_the_pinned_one(self):
        self.assertEqual(revocean.identity()["pluginBinary"], revocean.PLUGIN_BINARY_SHA256)

    def test_baseline_capture_is_repeatable_wet_and_linear(self):
        stimulus = revocean.impulses(3.0, [(4800, None, 0.5)])
        settings = {"decay": revocean.normalised("decay", 0.5)}
        first = revocean.capture(stimulus, settings)
        self.assertTrue(first.meta["verifiedRepeat"])
        self.assertEqual(first.latency, 48)
        self.assertEqual(first.meta["readback"]["id:3"], "0.500")
        self.assertEqual(first.meta["readback"]["id:6"], "0.000")
        # 100 % wet: nothing at the impulse itself, and nothing before it.
        self.assertEqual(float(np.abs(first.output[:4800 + 48 + 200]).max()), 0.0)
        self.assertGreater(revocean.rms(first.output), 1e-5)
        # Linear at the baseline: the inverted impulse gives the inverted output.
        inverted = revocean.capture(-stimulus, settings)
        self.assertTrue(np.array_equal(inverted.output, -first.output))
        # A second call is served from the cache.
        again = revocean.capture(stimulus, settings)
        self.assertEqual(again.meta["key"], first.meta["key"])
        self.assertTrue(np.array_equal(again.output, first.output))

    def test_macro_makes_the_reference_unrepeatable(self):
        stimulus = revocean.impulses(3.0, [(4800, None, 0.5)])
        settings = {"decay": revocean.normalised("decay", 0.5), "macro": 0.5}
        first = revocean.capture(stimulus, settings, realisation=0)
        second = revocean.capture(stimulus, settings, realisation=1)
        self.assertFalse(first.meta["verifiedRepeat"])
        self.assertFalse(np.array_equal(first.output, second.output))


if __name__ == "__main__":
    unittest.main()
