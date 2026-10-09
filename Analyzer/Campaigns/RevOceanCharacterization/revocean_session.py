#!/usr/bin/env python3
"""Captures inside one instance of Arturia Rev OCEAN whose editor its owner has used.

`revocean.capture` renders every case in a fresh instance that never shows its editor, so it only
reaches what a fresh instance is: the Macro Mode Tide. The mode (Abyss, Foam, Tide) is not a host
parameter. This module drives the session host of Analyzer/Tools/RevOceanSession instead: one instance,
its editor on screen, the owner chooses the mode by hand and presses Start, and every capture after
that runs inside that instance. The plug-in stays in its normal demo mode (no state is kept, 20 minutes
per instance); nothing here inspects its code, stores its state or works around its licensing.

    python revocean_session.py                      opens a session that waits for its owner

    session = revocean_session.Session.attach()     in another Python, once the window is open
    session.wait_until_started(900.0)               until the owner has pressed Start
    result = session.run(revocean_session.case(stimulus, {"decay": revocean.normalised("decay", 2.0)}, session.sample_rate))
    capture = result.recordings[0]                  .output, .first_frame, .warmup_seconds, .settings, .latency
    session.close()

A job is a list of steps that the host runs in order: `set_parameters` (always all fifteen controls,
baseline merged, as `revocean.resolve` gives them), `silence` and `play`. The host counts every frame
the instance processes from the first one, and a recording says how many came before it. That number is
the warm-up of the campaign's models, because every clock of the reference counts processed frames:

    network_model.render(stimulus, capture.sample_rate, capture.warmup_seconds, decay_seconds, size_percent)
    reference_render.render_knobs(stimulus, capture.sample_rate, capture.warmup_seconds, capture.settings)

Between two cases of one instance the network has to be emptied: `flush` sets the next case's controls
with Decay at its minimum, processes silence, then sets the case's Decay and processes the pre-roll;
`case` is a flush followed by the recorded stimulus, and `first_case` is what `revocean.capture` does to
a fresh instance. What such captures are worth is measured in README.md, section "Session captures": the
first case of an instance is the Analyzer's capture bit for bit, sessions repeat bit for bit at Macro 0,
and a later case is the fresh instance's capture up to 1.4e-36, or, behind some changes of Size, to -126 dB.

Sessions live under Analyzer/Results/RevOceanCharacterization/work/session (ignored by Git).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.io import wavfile

import revocean

TOOL = Path(os.environ.get(
    "REVOCEAN_SESSION_TOOL",
    revocean.ROOT / "build-session/RevOceanSession_artefacts/Release/Rev OCEAN Session.app/Contents/MacOS/Rev OCEAN Session"))
SESSIONS = revocean.RESULTS / "work/session"
MAX_INPUT = 0.5               # the reference is linear only below 0.5629; campaign stimuli stay at or below 0.5
FLUSH_SECONDS = 8.0           # silence at Decay 0.5 s between two cases; the last trace was measured after 5.8 s,
                              # after 7.7 s when the next case has Pre-delay 2000 ms (Tide; Abyss keeps more, see README)
DEMO_SECONDS = 1200.0         # measured: 20:00.3 after its creation the instance hands its input back
PREROLL_SECONDS = 2.0         # silence at the case's own settings in front of its stimulus; Decay itself acts at once
OWNER_TITLE = "Rev OCEAN session - waiting for you"
CHECK_TITLE = "Rev OCEAN session - automatic check, do not touch"
OWNER_INSTRUCTION = ("Press Demo in the plug-in's Authorization box, select the mode Abyss with the arrows next to "
                     "the mode name, change nothing else, then press Start.")
CHECK_INSTRUCTION = "Automatic check of the capture tool. Please do not touch this window; it closes by itself."

# The reference as the host can load it. Measured on 2026-10-08: in JUCE's VST3 host a mode chosen in
# the editor never reaches the sound (the label changes, the output stays Tide's), so attended sessions
# host the Audio Unit. Its sound controls carry the VST3's numbers 1 to 14; it has no On/Off parameter.
HOSTED = {
    "VST3": {"path": revocean.PLUGIN, "binarySha256": revocean.PLUGIN_BINARY_SHA256,
             "identity": revocean.EXPECTED_PLUGIN, "absent": ()},
    "AudioUnit": {"path": Path("/Library/Audio/Plug-Ins/Components/Rev OCEAN.component"),
                  "binarySha256": "c4cd49d9b8b36a055471cb612c88725067f67f4872380e511dc3fd0c9a04e3a6",
                  "identity": {"name": "Rev OCEAN", "manufacturer": "Arturia", "version": "1.0.0", "format": "AudioUnit"},
                  "absent": ("bypass",)},
}


# ---------------------------------------------------------------- steps of a job

@dataclass(frozen=True)
class Step:
    kind: str                           # "set", "silence" or "wav"
    settings: dict | None = None        # set: every control, normalised
    frames: int = 0                     # silence
    stimulus: np.ndarray | None = None  # wav
    record: bool = False


def set_parameters(settings: dict | None = None) -> Step:
    """All fifteen host parameters: `settings` (control key -> normalised value) on top of revocean.BASELINE."""
    return Step("set", settings=revocean.resolve(settings))


def silence(frames: int, record: bool = False) -> Step:
    if int(frames) != frames or frames <= 0:
        raise ValueError("silence needs a positive whole number of frames")
    return Step("silence", frames=int(frames), record=record)


def play(stimulus: np.ndarray, record: bool = True) -> Step:
    """A stereo stimulus; it must carry its own tail, as for `revocean.capture`."""
    stimulus = np.ascontiguousarray(stimulus, dtype=np.float32)
    if stimulus.ndim != 2 or stimulus.shape[1] != 2 or stimulus.shape[0] == 0:
        raise ValueError("the stimulus must have shape (frames, 2)")
    if not np.all(np.isfinite(stimulus)):
        raise ValueError("the stimulus is not finite")
    if float(np.abs(stimulus).max()) > MAX_INPUT:
        raise ValueError(f"the stimulus exceeds {MAX_INPUT}; the reference is not linear above 0.5629")
    return Step("wav", stimulus=stimulus, record=record)


def frames_of(seconds: float, sample_rate: int) -> int:
    return int(round(seconds * sample_rate))


def flush(settings: dict | None, sample_rate: int, *, flush_seconds: float = FLUSH_SECONDS,
          preroll_seconds: float = PREROLL_SECONDS) -> list:
    """The steps between two cases of one instance: the next case's controls with Decay at its minimum,
    silence that empties the network, then the case's Decay and its pre-roll of silence."""
    emptying = dict(settings or {}, decay=0.0)
    steps = [set_parameters(emptying), silence(frames_of(flush_seconds, sample_rate)), set_parameters(settings)]
    return steps + ([silence(frames_of(preroll_seconds, sample_rate))] if preroll_seconds > 0.0 else [])


def case(stimulus: np.ndarray, settings: dict | None, sample_rate: int, **flush_options) -> list:
    """A flush and the recorded stimulus: one case in an instance that has processed other things before."""
    return flush(settings, sample_rate, **flush_options) + [play(stimulus)]


def first_case(stimulus: np.ndarray, settings: dict | None, sample_rate: int,
               warmup_seconds: float = revocean.WARMUP_SECONDS) -> list:
    """What `revocean.capture` does to its fresh instance: the controls, the warm-up, the stimulus."""
    return [set_parameters(settings), silence(frames_of(warmup_seconds, sample_rate)), play(stimulus)]


# ---------------------------------------------------------------- results of a job

@dataclass(frozen=True)
class Recording:
    output: np.ndarray      # float32, (frames, 2), raw: the reported latency is still in it
    first_frame: int        # frames the instance had processed before the first frame of this step
    sample_rate: int
    latency: int            # samples the plug-in reports
    settings: dict | None   # normalised host parameters in force, as far as this Session object has set them
    step: dict              # the host's record of the step

    @property
    def warmup_seconds(self) -> float:
        """The warm-up of the campaign's models: seconds of processing in front of this step."""
        return self.first_frame / self.sample_rate

    def aligned(self) -> np.ndarray:
        return self.output[self.latency:]


@dataclass(frozen=True)
class Result:
    name: str
    record: dict            # the host's record of the job: identity, rate, latency and every step
    recordings: list        # one Recording per recorded step, in order

    def readback(self, index: int) -> dict:
        """Display texts by control key that the plug-in returned at set step `index`."""
        by_id = {control.vst3_id: control.key for control in revocean.CONTROLS}
        return {by_id[entry["id"]]: entry["text"] for entry in self.record["steps"][index]["parameters"]}


# ---------------------------------------------------------------- a session

class Session:
    """One instance of the reference in the session host. Use `open` or `attach`."""

    def __init__(self, folder: Path, process: subprocess.Popen | None = None):
        self.folder = Path(folder)
        self.process = process
        self.description = json.loads((self.folder / "session.json").read_text())
        self.sample_rate = int(self.description["sampleRate"])
        self.block_size = int(self.description["blockSize"])
        bundle = self.description.get("plugin", {}).get("path")
        self.hosted = next((entry for entry in HOSTED.values() if str(entry["path"]) == bundle), HOSTED["VST3"])
        self.settings: dict | None = None      # the last settings this object has sent

    # ------------------------------------------------------------ opening

    @classmethod
    def open(cls, instruction: str | None = None, *, sample_rate: int = revocean.SAMPLE_RATE,
             block_size: int = revocean.BLOCK_SIZE, start_after: float | None = None,
             stop_after_minutes: float | None = None, idle_seconds: float = 0.0, timeout: float = 120.0,
             plugin_format: str = "VST3", bpm: float | None = None,
             transport_offset_seconds: float | None = None, transport_playing: bool | None = None) -> "Session":
        """Starts the host with a new instance and returns once its window is open.

        `start_after=None` waits for the owner to press Start; a number of seconds starts by itself, for
        unattended checks in Tide. `stop_after_minutes` replaces the host's safety margin before the demo
        limit; `idle_seconds` ends the session when no job arrives for that long.
        """
        if not TOOL.exists():
            raise RuntimeError(f"the session host is not built: {TOOL} (see Analyzer/Tools/RevOceanSession/README.md)")
        if revocean.sha256_file(revocean.PROCESSOR_LIBRARY) != revocean.PROCESSOR_LIBRARY_SHA256:
            raise RuntimeError("the processor library is not the one the campaign is pinned to")
        attended = start_after is None
        hosted = HOSTED[plugin_format]
        folder = SESSIONS / f"{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:6]}"
        (folder / "queue").mkdir(parents=True)
        description = {
            "schema": "revocean.session/1",
            "plugin": {"path": str(hosted["path"]), "name": hosted["identity"]["name"],
                       "manufacturer": hosted["identity"]["manufacturer"],
                       "version": hosted["identity"]["version"], "binarySha256": hosted["binarySha256"]},
            "sampleRate": int(sample_rate), "blockSize": int(block_size),
            "title": OWNER_TITLE if attended else CHECK_TITLE,
            "instruction": instruction or (OWNER_INSTRUCTION if attended else CHECK_INSTRUCTION),
            "idleSeconds": float(idle_seconds),
        }
        if stop_after_minutes is not None:
            description["stopAfterMinutes"] = float(stop_after_minutes)
        if bpm is not None:                        # the play head's tempo; the host's default is 120
            description["bpm"] = float(bpm)
        if transport_offset_seconds is not None:   # the play head's position at the first processed frame; default 0
            description["transportOffsetSeconds"] = float(transport_offset_seconds)
        if transport_playing is not None:          # False: the play head says stopped and stays at the offset
            description["transportPlaying"] = bool(transport_playing)
        (folder / "session.json").write_text(json.dumps(description, indent=1))
        command = [str(TOOL), "--session", str(folder)] + ([] if attended else ["--start-after", repr(float(start_after))])
        with open(folder / "host.log", "w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=folder)
        session = cls(folder, process)
        try:
            session._wait(lambda: session._status() in ("waiting_for_start", "running"), timeout, "the window to open")
        except BaseException:
            session.close()
            raise
        return session

    @classmethod
    def attach(cls, folder: Path | None = None) -> "Session":
        """A session that is already open, by its folder; without one, the newest that has not stopped."""
        newest_first = sorted(SESSIONS.glob("*/session-record.json"), reverse=True)
        for candidate in [Path(folder)] if folder is not None else [record.parent for record in newest_first]:
            session = cls(candidate)
            if session.alive():
                return session
        raise RuntimeError(f"no open session in {folder or SESSIONS}")

    # ------------------------------------------------------------ state

    def record(self) -> dict:
        """The host's record of the session: identity, state, frames processed, and in the end why it stopped."""
        path = self.folder / "session-record.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def _status(self) -> str:
        return self.record().get("status", "")

    def alive(self) -> bool:
        if self.process is not None:
            return self.process.poll() is None
        record = self.record()
        if record.get("status") not in ("waiting_for_start", "running", "stopping"):
            return False
        try:
            os.kill(int(record["processId"]), 0)        # signal 0 only asks whether the process is there
        except OSError:
            return False
        return True

    def _wait(self, done, timeout: float, what: str) -> None:
        deadline = time.monotonic() + timeout
        while not done():
            if not self.alive() and not done():
                raise RuntimeError(f"the session host ended while waiting for {what}: {self._ending()}")
            if time.monotonic() > deadline:
                raise TimeoutError(f"no {what} after {timeout:.0f} s")
            time.sleep(0.05)

    def _ending(self) -> str:
        record = self.record()
        log = self.folder / "host.log"
        return (f"status {record.get('status')!r}, stop reason {record.get('stopReason')!r}, error {record.get('error')!r}; "
                + (log.read_text()[-600:] if log.exists() else ""))

    def wait_until_started(self, timeout: float) -> None:
        """Returns once Start was pressed (or the host has started by itself)."""
        self._wait(lambda: self._status() == "running", timeout, "Start")

    def screenshot(self, path: Path) -> bool:
        """A picture of the session's own window and of nothing else on the screen, taken by the system's
        screencapture: the only record of what the editor shows, since the mode is not a host parameter.
        False when the calling terminal is not allowed to record the screen."""
        command = ["/usr/sbin/screencapture", "-x", "-o", "-l", str(self.record()["windowNumber"]), str(path)]
        return subprocess.run(command, capture_output=True, timeout=30.0).returncode == 0 and Path(path).exists()

    # ------------------------------------------------------------ jobs

    def _next_name(self, label: str) -> str:
        taken = [entry.name for entry in (self.folder / "results").glob("*")] \
            + [entry.name for entry in (self.folder / "queue").glob("*.json*")]
        numbers = [int(name[:4]) for name in taken if name[:4].isdigit()]
        return f"{max(numbers, default=0) + 1:04d}-{label}"

    def submit(self, steps, label: str = "job", quit: bool = False) -> str:
        """Hands a job to the host and returns its name; the host runs jobs in the order of their names."""
        name = self._next_name(label)
        queue = self.folder / "queue"
        entries = []
        for index, step in enumerate(steps):
            if step.kind == "set":
                entries.append({"kind": "set", "parameters": [{"id": control.vst3_id, "value": step.settings[control.key]}
                                                              for control in revocean.CONTROLS
                                                              if control.key not in self.hosted["absent"]]})
            elif step.kind == "silence":
                entries.append({"kind": "silence", "frames": step.frames, "record": step.record})
            else:
                source = f"{name}.{index:03d}.wav"
                wavfile.write(queue / source, self.sample_rate, step.stimulus)
                entries.append({"kind": "wav", "file": source, "record": step.record})
        pending = queue / f"{name}.json.partial"
        pending.write_text(json.dumps({"schema": "revocean.session.job/1", "steps": entries, "quit": quit}, indent=1))
        pending.rename(queue / f"{name}.json")       # the host takes whole files only
        return name

    def result(self, name: str, steps, timeout: float = 600.0, keep_audio: bool = False) -> Result:
        """Waits for the job and returns its recordings. `steps` are the steps it was submitted with."""
        folder = self.folder / "results" / name
        self._wait(lambda: (folder / "record.json").exists(), timeout, f"result of {name}")
        record = json.loads((folder / "record.json").read_text())
        if record["status"] != "ok":
            raise RuntimeError(f"job {name} {record['status']}: {record['error']}")
        for key, value in self.hosted["identity"].items():
            if record["plugin"][key] != value:
                raise RuntimeError(f"plug-in {key} is {record['plugin'][key]!r}")
        if record["plugin"]["binarySha256"] != self.hosted["binarySha256"]:
            raise RuntimeError("plug-in binary hash differs")
        recordings = []
        for step, entry in zip(steps, record["steps"], strict=True):
            if step.kind == "set":
                for parameter in entry["parameters"]:
                    if np.float32(parameter["readback"]) != np.float32(parameter["requested"]):
                        raise RuntimeError(f"read-back of parameter {parameter['id']} is {parameter['readback']}")
                self.settings = dict(step.settings)
            elif step.record:
                rate, output = wavfile.read(folder / entry["recorded"])
                if rate != self.sample_rate or output.dtype != np.float32 or output.shape != (entry["frames"], 2):
                    raise RuntimeError("unexpected output format")
                if not np.all(np.isfinite(output)):
                    raise RuntimeError("output is not finite")
                # measured: 20 minutes after its creation the demo hands its input back, bit for bit and undelayed.
                # The clock decides; equality with the stimulus is a second witness, but not at Mix 0 %, where a
                # steady input comes back as itself anyway.
                if entry.get("secondsSinceCreation", 0.0) + entry.get("wallSeconds", 0.0) >= DEMO_SECONDS - 1.0:
                    raise RuntimeError(f"job {name}: step {entry['index']} ran into the end of the demo time")
                wet = self.settings is None or self.settings.get("mix", 1.0) > 0.0
                if wet and step.kind == "wav" and np.any(step.stimulus) and np.array_equal(output, step.stimulus):
                    raise RuntimeError(f"job {name}: the instance returned its input untouched; its demo time is over")
                recordings.append(Recording(output, int(entry["firstFrame"]), self.sample_rate,
                                            int(record["reportedLatencySamples"]),
                                            None if self.settings is None else dict(self.settings), entry))
        if not keep_audio:
            for audio in list(folder.glob("step-*.wav")) + list((self.folder / "queue").glob(f"{name}.*.wav")):
                audio.unlink()
        return Result(name, record, recordings)

    def run(self, steps, label: str = "job", timeout: float = 600.0, keep_audio: bool = False) -> Result:
        """Submits a job and waits for its result."""
        steps = list(steps)
        return self.result(self.submit(steps, label), steps, timeout, keep_audio)

    # ------------------------------------------------------------ closing

    def close(self, timeout: float = 30.0) -> dict:
        """Ends the session and returns the host's final record. Nothing is left running."""
        if self.alive() and self._status() == "running":
            self.submit([], "quit", quit=True)           # the host ends by itself behind the jobs it still has
            deadline = time.monotonic() + timeout
            while self.alive() and time.monotonic() < deadline:
                time.sleep(0.05)
        if self.alive():                                 # never started, or it did not end in time
            self._signal(signal.SIGTERM)
            deadline = time.monotonic() + 10.0
            while self.alive() and time.monotonic() < deadline:
                time.sleep(0.05)
            if self.alive():
                self._signal(signal.SIGKILL)
        if self.process is not None:
            self.process.wait(10.0)
        shutil.rmtree(self.folder / "queue", ignore_errors=True)
        return self.record()

    def _signal(self, number: int) -> None:
        if self.process is not None:
            self.process.send_signal(number)
        else:
            os.kill(int(self.record()["processId"]), number)

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *exception) -> None:
        self.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Open a Rev OCEAN session that waits for its owner.")
    parser.add_argument("--instruction", default=OWNER_INSTRUCTION, help="the line shown above the editor")
    parser.add_argument("--sample-rate", type=int, default=revocean.SAMPLE_RATE)
    parser.add_argument("--block-size", type=int, default=revocean.BLOCK_SIZE)
    arguments = parser.parse_args()
    session = Session.open(arguments.instruction, sample_rate=arguments.sample_rate, block_size=arguments.block_size)
    print(f"session open: {session.folder}")
    print("choose the mode in its window and press Start; captures then come from another Python:")
    print(f"    session = revocean_session.Session.attach(\"{session.folder}\")")
    try:
        session.wait_until_started(session.record()["stopAfterMinutes"] * 60.0)
        picture = session.folder / "window-at-start.png"
        print(f"started; the window as it was then: {picture}" if session.screenshot(picture)
              else "started; no picture of the window (the terminal may not record the screen)")
        session.process.wait()
    except KeyboardInterrupt:
        session.close()
    except RuntimeError:
        pass                              # the window was closed before Start
    print(f"session ended: {session.record().get('stopReason')}")


if __name__ == "__main__":
    main()
