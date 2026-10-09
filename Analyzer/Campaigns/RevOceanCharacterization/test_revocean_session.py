#!/usr/bin/env python3
"""Tests of the session captures.

The pure tests build jobs and read results in a temporary folder and run anywhere. The reference tests
need the built session host, the installed plug-in and the pinned Analyzer and are skipped when one is
missing. They open six windows titled "Rev OCEAN session - automatic check, do not touch" for a few
seconds each; nothing has to be done in them.
"""
from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

import numpy as np
from scipy.io import wavfile

import revocean
import revocean_session as rs

REFERENCE_AVAILABLE = (rs.TOOL.exists() and revocean.ANALYZER.exists() and revocean.PLUGIN.exists()
                       and revocean.PROCESSOR_LIBRARY.exists())
RATE = revocean.SAMPLE_RATE


class Steps(unittest.TestCase):
    def test_set_parameters_sets_every_control(self):
        step = rs.set_parameters({"macro": 0.25})
        self.assertEqual(step.kind, "set")
        self.assertEqual(set(step.settings), set(revocean.CONTROL))
        self.assertEqual(step.settings["macro"], 0.25)
        self.assertEqual(step.settings["mix"], 1.0)
        with self.assertRaises(KeyError):
            rs.set_parameters({"mode": 1.0})
        with self.assertRaises(ValueError):
            rs.set_parameters({"decay": 1.0})          # Freeze

    def test_silence_and_play_reject_what_the_host_would(self):
        self.assertEqual(rs.silence(480).frames, 480)
        for frames in (0, -5, 1.5):
            with self.assertRaises(ValueError):
                rs.silence(frames)
        for stimulus in (np.zeros(100, np.float32), np.zeros((0, 2), np.float32), np.full((10, 2), np.nan, np.float32),
                         np.full((10, 2), 0.51, np.float32)):
            with self.assertRaises(ValueError):
                rs.play(stimulus)
        step = rs.play(np.full((10, 2), 0.5))
        self.assertEqual(step.stimulus.dtype, np.float32)
        self.assertTrue(step.record)

    def test_flush_empties_at_minimum_decay_and_then_sets_the_case(self):
        settings = {"decay": revocean.normalised("decay", 8.0), "size": revocean.normalised("size", 150.0), "macro": 0.5}
        emptying, wait, restore, preroll = rs.flush(settings, RATE)
        self.assertEqual([step.kind for step in (emptying, wait, restore, preroll)], ["set", "silence", "set", "silence"])
        self.assertEqual(emptying.settings["decay"], 0.0)
        self.assertEqual(restore.settings, revocean.resolve(settings))
        self.assertEqual({key: value for key, value in emptying.settings.items() if key != "decay"},
                         {key: value for key, value in restore.settings.items() if key != "decay"})
        self.assertEqual((wait.frames, preroll.frames), (rs.frames_of(rs.FLUSH_SECONDS, RATE), rs.frames_of(rs.PREROLL_SECONDS, RATE)))
        self.assertFalse(wait.record or preroll.record)
        self.assertEqual(len(rs.flush(settings, RATE, preroll_seconds=0.0)), 3)
        self.assertEqual(rs.flush(settings, 44100, flush_seconds=1.5)[1].frames, 66150)

    def test_case_and_first_case(self):
        stimulus = revocean.impulses(0.1, [(10, None, 0.5)])
        steps = rs.case(stimulus, None, RATE)
        self.assertEqual([step.kind for step in steps], ["set", "silence", "set", "silence", "wav"])
        self.assertTrue(steps[-1].record)
        controls, warmup, played = rs.first_case(stimulus, {"macro": 0.5}, RATE)
        self.assertEqual(controls.settings["macro"], 0.5)
        self.assertEqual(warmup.frames, 480000)
        self.assertTrue(np.array_equal(played.stimulus, stimulus))


class Files(unittest.TestCase):
    """The driver's side of the job and result files, against a folder the host never sees."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.folder = Path(self.scratch.name)
        (self.folder / "queue").mkdir()
        (self.folder / "session.json").write_text(json.dumps({"sampleRate": 44100, "blockSize": 512}))
        (self.folder / "session-record.json").write_text(json.dumps({"status": "running", "processId": os.getpid()}))
        self.session = rs.Session(self.folder)

    def tearDown(self):
        self.scratch.cleanup()

    def test_submit_writes_a_whole_job(self):
        stimulus = revocean.impulses(0.01, [(10, 0, 0.5)], 44100)
        steps = rs.case(stimulus, {"decay": revocean.normalised("decay", 2.0)}, 44100)
        name = self.session.submit(steps, "case")
        self.assertEqual(name, "0001-case")
        job = json.loads((self.folder / "queue" / "0001-case.json").read_text())
        self.assertEqual(job["schema"], "revocean.session.job/1")
        self.assertFalse(job["quit"])
        self.assertEqual([entry["kind"] for entry in job["steps"]], ["set", "silence", "set", "silence", "wav"])
        emptying, wait, restore, preroll, played = job["steps"]
        self.assertEqual([parameter["id"] for parameter in restore["parameters"]], list(range(15)))
        self.assertEqual(emptying["parameters"][revocean.CONTROL["decay"].vst3_id]["value"], 0.0)
        self.assertEqual(restore["parameters"][revocean.CONTROL["decay"].vst3_id]["value"],
                         float(np.float32(revocean.normalised("decay", 2.0))))
        self.assertEqual((wait["frames"], wait["record"], preroll["frames"]), (352800, False, 88200))
        self.assertEqual((played["file"], played["record"]), ("0001-case.004.wav", True))
        rate, written = wavfile.read(self.folder / "queue" / played["file"])
        self.assertEqual((rate, written.dtype), (44100, np.float32))
        self.assertTrue(np.array_equal(written, stimulus))
        self.assertEqual(self.session.submit([], "quit", quit=True), "0002-quit")
        self.assertEqual(list((self.folder / "queue").glob("*.partial")), [])

    def host_answers(self, name: str, steps: list, output: np.ndarray, readback: float | None = None) -> None:
        """Writes what the host would for a job of one set step, some silence and one recorded stimulus."""
        folder = self.folder / "results" / name
        folder.mkdir(parents=True)
        wavfile.write(folder / "step-002.wav", 44100, output)
        parameters = [{"id": control.vst3_id, "requested": steps[0].settings[control.key],
                       "readback": steps[0].settings[control.key] if readback is None else readback, "text": "x"}
                      for control in revocean.CONTROLS]
        record = {"status": "ok", "error": "", "reportedLatencySamples": 44,
                  "plugin": dict(revocean.EXPECTED_PLUGIN, binarySha256=revocean.PLUGIN_BINARY_SHA256),
                  "steps": [{"kind": "set", "firstFrame": 0, "frames": 0, "parameters": parameters},
                            {"kind": "silence", "firstFrame": 0, "frames": 441000},
                            {"kind": "wav", "firstFrame": 441000, "frames": len(output), "recorded": "step-002.wav"}]}
        (folder / "record.json").write_text(json.dumps(record))

    def test_result_gives_the_frames_in_front_of_a_recording(self):
        stimulus = revocean.impulses(0.01, [(10, 0, 0.5)], 44100)
        steps = rs.first_case(stimulus, {"macro": 0.25}, 44100)
        name = self.session.submit(steps)
        output = np.linspace(-0.1, 0.1, 2 * len(stimulus), dtype=np.float32).reshape(-1, 2)
        self.host_answers(name, steps, output)
        result = self.session.result(name, steps, timeout=1.0)
        recording, = result.recordings
        self.assertTrue(np.array_equal(recording.output, output))
        self.assertEqual((recording.first_frame, recording.warmup_seconds, recording.latency), (441000, 10.0, 44))
        self.assertEqual(recording.settings["macro"], 0.25)
        self.assertEqual(len(recording.aligned()), len(output) - 44)
        self.assertEqual(result.readback(0)["decay"], "x")
        self.assertEqual(list((self.folder / "results" / name).glob("*.wav")), [])     # the audio is not kept by default
        self.assertEqual(list((self.folder / "queue").glob("*.wav")), [])

    def test_result_refuses_a_wrong_readback_and_a_failed_job(self):
        stimulus = revocean.impulses(0.01, [(10, 0, 0.5)], 44100)
        steps = rs.first_case(stimulus, None, 44100)
        name = self.session.submit(steps)
        self.host_answers(name, steps, np.zeros((len(stimulus), 2), np.float32), readback=0.123)
        with self.assertRaises(RuntimeError):
            self.session.result(name, steps, timeout=1.0)
        other = self.session.submit(steps)
        (self.folder / "results" / other).mkdir()
        (self.folder / "results" / other / "record.json").write_text(json.dumps({"status": "rejected", "error": "no"}))
        with self.assertRaises(RuntimeError):
            self.session.result(other, steps, timeout=1.0)
        with self.assertRaises(TimeoutError):
            self.session.result("0009-never", steps, timeout=0.2)

    def test_result_refuses_an_instance_whose_demo_time_is_over(self):
        stimulus = revocean.impulses(0.01, [(10, 0, 0.5)], 44100)
        steps = rs.first_case(stimulus, None, 44100)
        name = self.session.submit(steps)
        self.host_answers(name, steps, stimulus)          # the input, bit for bit and undelayed
        with self.assertRaises(RuntimeError) as over:
            self.session.result(name, steps, timeout=1.0)
        self.assertIn("demo time is over", str(over.exception))


@unittest.skipUnless(REFERENCE_AVAILABLE, "needs the built session host, the pinned Analyzer and the installed Rev OCEAN")
class Reference(unittest.TestCase):
    STIMULUS = revocean.impulses(3.0, [(4800, None, 0.5), (30011, 0, -0.4), (52001, 1, 0.3)])

    def open(self, idle_seconds: float = 30.0, **options) -> rs.Session:
        session = rs.Session.open(start_after=0.0, idle_seconds=idle_seconds, **options)
        self.addCleanup(session.close)
        return session

    def test_first_case_is_the_analyzers_capture(self):
        settings = {"decay": revocean.normalised("decay", 2.0), "size": revocean.normalised("size", 150.0), "mix": 0.7}
        reference = revocean.capture(self.STIMULUS, settings)
        padded = np.zeros_like(reference.output)       # the Analyzer renders past the end of the stimulus
        padded[:len(self.STIMULUS)] = self.STIMULUS
        session = self.open()
        result = session.run(rs.first_case(padded, settings, RATE))
        recording, = result.recordings
        self.assertEqual((recording.first_frame, recording.latency), (480000, reference.latency))
        self.assertEqual(result.readback(0)["decay"], reference.meta["readback"]["id:3"])
        self.assertTrue(np.array_equal(recording.output, reference.output))
        final = session.close()
        self.assertEqual((final["status"], final["stopReason"], final["framesProcessed"]), ("stopped", "quit", 480000 + len(padded)))

    def later_cases(self) -> list:
        # Size stays where it is: a change of Size can leave a later case a last place away from a fresh instance.
        steps = rs.case(self.STIMULUS, {"decay": revocean.normalised("decay", 8.0)}, RATE)
        second = {"decay": revocean.normalised("decay", 2.0)}
        return steps + rs.case(self.STIMULUS, second, RATE, flush_seconds=6.0, preroll_seconds=0.5)

    def test_later_cases_are_fresh_instances_and_sessions_repeat(self):
        import network_model
        steps = self.later_cases()
        first = self.open().run(steps).recordings
        again = self.open().run(steps).recordings
        self.assertEqual([recording.first_frame for recording in first], [480000, 480000 + 144000 + 312000])
        for recording, other, decay in zip(first, again, (8.0, 2.0), strict=True):
            self.assertTrue(np.array_equal(recording.output, other.output))
            fresh = revocean.capture(self.STIMULUS, recording.settings, warmup=recording.warmup_seconds).output[:len(self.STIMULUS)]
            arrival = int(np.flatnonzero(np.any(fresh != 0.0, axis=1))[0])
            self.assertTrue(np.array_equal(recording.output[arrival:], fresh[arrival:]))
            self.assertLess(float(np.abs(recording.output[:arrival]).max()), 1e-35)     # what a signal leaves in the instance for good
            model = network_model.render(self.STIMULUS, RATE, recording.warmup_seconds, decay, 100.0)
            self.assertLess(revocean.null_db(model, recording.output), -100.0)

    def test_a_bad_job_is_rejected_and_the_safety_margin_stops_a_running_one(self):
        session = self.open(stop_after_minutes=0.1)
        (session.folder / "queue" / "0001-bad.json").write_text(json.dumps(
            {"schema": "revocean.session.job/1",       # the plug-in declares parameters 0 to 2188
             "steps": [{"kind": "silence", "frames": 4800}, {"kind": "set", "parameters": [{"id": 100000, "value": 0.5}]}]}))
        with self.assertRaises(RuntimeError) as rejected:
            session.result("0001-bad", [], timeout=20.0)
        self.assertIn("rejected", str(rejected.exception))
        self.assertEqual(session.record()["framesProcessed"], 0)
        with self.assertRaises(RuntimeError) as stopped:
            session.run([rs.set_parameters(), rs.silence(60 * 60 * RATE)], timeout=60.0)
        self.assertIn("demo_margin", str(stopped.exception))
        final = session.close()
        self.assertEqual((final["status"], final["stopReason"]), ("stopped", "demo_margin"))
        self.assertLess(final["secondsSinceCreation"], 12.0)

    def test_a_session_without_jobs_ends_by_itself(self):
        session = self.open(idle_seconds=1.5)
        session.process.wait(20.0)
        final = session.record()
        self.assertEqual((final["status"], final["stopReason"], final["framesProcessed"]), ("stopped", "idle", 0))

    def test_a_session_can_be_closed_before_start(self):
        session = rs.Session.open(start_after=300.0)
        self.assertEqual(session.record()["status"], "waiting_for_start")
        final = session.close()
        self.assertEqual((final["status"], final["stopReason"], final["framesProcessed"]), ("stopped", "terminated", 0))
        self.assertFalse(session.alive())


if __name__ == "__main__":
    unittest.main()
