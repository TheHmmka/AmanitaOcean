#!/usr/bin/env python3
"""Black-box reference captures of Arturia Rev OCEAN for the Tide structural model.

The reference is rendered offline by the Amanita Analyzer of the sibling
AmanitaSaturator checkout (file suite, a fresh plug-in instance per case,
stereo 32-bit float). The plug-in runs in its normal demo mode; nothing here
inspects its code or works around its licensing.

Facts this module relies on (measured on 6 October 2026, see README.md):

* A fresh instance starts in the factory preset "Cleaner Tides", whose Macro
  Mode is Tide. The mode is not a host parameter, so every capture is Tide.
* Parameters the host has not set read back as 0 and are unreliable, so every
  capture sets all fifteen sound parameters explicitly.
* With Macro at 0 the output is bit-identical between fresh instances once
  the warm-up is long enough (10 s of silence; 0.5 s leaves about -100 dB of
  difference). With Macro above 0 two instances differ, so those captures are
  realisations, not a single reference.

Captures are cached by content under Analyzer/Results (ignored by Git). A
capture at Macro 0 is rendered twice and kept only when both renders agree
bit for bit.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import shutil
import subprocess
import uuid
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / "Analyzer/Results/RevOceanCharacterization"
CACHE = RESULTS / "captures"
ANALYZER = Path(os.environ.get(
    "AMANITA_ANALYZER",
    ROOT.parent / "AmanitaSaturator/Analyzer/build/release-v123/AmanitaAnalyzer_artefacts/Release/AmanitaAnalyzer"))
ANALYZER_SHA256 = "9eeb76c3a2c84961120307fc9a9d172aec133b6f867330db185a2e2d4d04b697"
PLUGIN = Path("/Library/Audio/Plug-Ins/VST3/Rev OCEAN.vst3")
PLUGIN_BINARY_SHA256 = "eb43f0bde6dacc5e566784829bff5f9c50d1f901f8158fbbdc6b95bb94b5ad26"
# The VST3 binary loads its processor from this library; it is hashed for identity only.
PROCESSOR_LIBRARY = Path("/Library/Arturia/Rev OCEAN/resources/librevoceanProcessor.dylib")
PROCESSOR_LIBRARY_SHA256 = "ec29614feb7d274773d0a4d557b874b609afebdcde623c06ba43953a7c3ca433"
EXPECTED_PLUGIN = {"name": "Rev OCEAN", "manufacturer": "Arturia", "version": "1.0.0.5848", "format": "VST3"}

SAMPLE_RATE = 48000
BLOCK_SIZE = 512
WARMUP_SECONDS = 10.0
MAX_STIMULUS_SECONDS = 110.0          # the Analyzer limits a case to 120 s
CACHE_LIMIT_BYTES = 28 * 1024 ** 3    # refuse new captures beyond this; the disk is nearly full
CACHE_SCHEMA = 1


def exp_map(normalised: float, low: float, high: float, shape: float) -> float:
    """The vendor's Exp(shape) display law: low + (high - low)(e^(shape x) - 1)/(e^shape - 1)."""
    return low + (high - low) * math.expm1(shape * normalised) / math.expm1(shape)


def exp_inverse(value: float, low: float, high: float, shape: float) -> float:
    return math.log1p((value - low) / (high - low) * math.expm1(shape)) / shape


@dataclass(frozen=True)
class Control:
    key: str
    vst3_id: int
    name: str
    baseline: float            # normalised value of the neutral measurement baseline
    low: float | None = None   # display range and Exp shape; shape 0 means linear
    high: float | None = None
    shape: float = 0.0
    unit: str = ""

    def normalised(self, value: float) -> float:
        """Display value -> normalised."""
        if self.low is None:
            raise ValueError(f"{self.key} has no display law")
        if self.shape == 0.0:
            return (value - self.low) / (self.high - self.low)
        return exp_inverse(value, self.low, self.high, self.shape)

    def display(self, normalised: float) -> float:
        if self.low is None:
            raise ValueError(f"{self.key} has no display law")
        if self.shape == 0.0:
            return self.low + (self.high - self.low) * normalised
        return exp_map(normalised, self.low, self.high, self.shape)


# Host parameters in the order the plug-in exports them. Ranges and Exp shapes
# are the ones the plug-in declares; the baseline is the neutral state used for
# every capture unless a setting overrides it: 100 % wet, Macro 0, no pre-delay,
# open input filter, no transient shaping, no ducking, unity gains, Width 100 %.
CONTROLS = (
    Control("bypass", 0, "On/Off", 0.0),
    Control("mix", 1, "Mix", 1.0, 0.0, 100.0, 0.0, "%"),
    Control("master", 2, "Master Volume", exp_inverse(0.0, -70.0, 6.0, -3.0), -70.0, 6.0, -3.0, "dB"),
    Control("decay", 3, "Decay", exp_inverse(4.0, 0.5, 60.0, 4.4), 0.5, 60.0, 4.4, "s"),
    Control("size", 4, "Size", exp_inverse(100.0, 30.0, 200.0, 0.71337), 30.0, 200.0, 0.71337, "%"),
    Control("brightness", 5, "Brightness", 0.5, -100.0, 100.0, 0.0, "%"),
    Control("macro", 6, "Macro", 0.0, 0.0, 100.0, 0.0, "%"),
    Control("hpf", 7, "HPF", 0.0, 20.0, 20000.0, 5.0, "Hz"),
    Control("lpf", 8, "LPF", 1.0, 20.0, 20000.0, 5.0, "Hz"),
    Control("return", 9, "Return", 0.5, -24.0, 24.0, 0.0, "dB"),
    Control("transients", 10, "Transients", 0.0, 0.0, -10.0, 0.0, "dB"),
    Control("predelay", 11, "Predelay", 0.0, 0.0, 2000.0, 6.0, "ms"),
    Control("predelay_synced", 12, "Predelay Synced", 0.5),
    Control("ducking", 13, "Ducking", 0.0, 0.0, 100.0, 0.0, "%"),
    Control("width", 14, "Width", 0.5, 0.0, 150.0, -1.38629435, "%"),
)
CONTROL = {control.key: control for control in CONTROLS}
BASELINE = {control.key: control.baseline for control in CONTROLS}
DECAY_FREEZE_THRESHOLD = 0.999   # the Decay knob turns into Freeze from here up


def normalised(key: str, value: float) -> float:
    """Display value of a control (seconds, percent, Hz, dB, ms) -> normalised."""
    result = CONTROL[key].normalised(value)
    if not 0.0 <= result <= 1.0:
        raise ValueError(f"{key} = {value} is outside the control's range")
    return result


def resolve(settings: dict | None) -> dict:
    """Baseline merged with the settings; every value is normalised and float32-exact."""
    merged = dict(BASELINE)
    for key, value in (settings or {}).items():
        if key not in CONTROL:
            raise KeyError(f"unknown control {key!r}")
        merged[key] = value
    for key, value in merged.items():
        if not 0.0 <= value <= 1.0:
            raise ValueError(f"{key} = {value} is not a normalised value")
        merged[key] = float(np.float32(value))   # the host interface carries single precision
    if merged["decay"] >= DECAY_FREEZE_THRESHOLD:
        raise ValueError("Decay at or above 0.999 engages Freeze; it is not part of the baseline campaign")
    return merged


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


_identity: dict | None = None


def identity() -> dict:
    """Hashes of the Analyzer and the reference; raises when either is not the pinned one."""
    global _identity
    if _identity is None:
        found = {
            "analyzer": sha256_file(ANALYZER),
            "pluginBinary": sha256_file(PLUGIN / "Contents/MacOS/Rev OCEAN"),
            "processorLibrary": sha256_file(PROCESSOR_LIBRARY),
        }
        pinned = {"analyzer": ANALYZER_SHA256, "pluginBinary": PLUGIN_BINARY_SHA256,
                  "processorLibrary": PROCESSOR_LIBRARY_SHA256}
        for key, value in pinned.items():
            if found[key] != value:
                raise RuntimeError(f"{key} hash is {found[key]}, the campaign is pinned to {value}")
        _identity = found
    return _identity


@dataclass(frozen=True)
class Capture:
    output: np.ndarray     # float32, (frames, 2), not latency compensated
    latency: int           # samples the plug-in reports
    sample_rate: int
    meta: dict

    def aligned(self) -> np.ndarray:
        """Output with the reported latency removed, so sample n answers input sample n."""
        return self.output[self.latency:]


def cache_bytes() -> int:
    if not CACHE.exists():
        return 0
    return sum(entry.stat().st_size for entry in CACHE.rglob("output.npy"))


def _key(stimulus: np.ndarray, settings: dict, sample_rate: int, block_size: int, warmup: float, realisation: int) -> str:
    description = {
        "schema": CACHE_SCHEMA,
        "stimulus": hashlib.sha256(stimulus.tobytes()).hexdigest(),
        "frames": int(stimulus.shape[0]),
        "settings": {key: repr(value) for key, value in sorted(settings.items())},
        "sampleRate": sample_rate,
        "blockSize": block_size,
        "warmupSeconds": repr(float(warmup)),
        "realisation": realisation,
        "identity": identity(),
    }
    return hashlib.sha256(json.dumps(description, sort_keys=True).encode()).hexdigest()


def _render(stimulus: np.ndarray, settings: dict, sample_rate: int, block_size: int, warmup: float, timeout: float) -> tuple:
    """One Analyzer run in a scratch folder; returns (output, latency, run facts)."""
    scratch = RESULTS / "scratch" / uuid.uuid4().hex
    scratch.mkdir(parents=True)
    try:
        source = scratch / "stimulus.wav"
        wavfile.write(source, sample_rate, stimulus)
        run = scratch / "run"
        command = [str(ANALYZER), "run", "--plugin", str(PLUGIN), "--suite", "file", "--input-wav", str(source),
                   "--sweep", f"id:{CONTROL['mix'].vst3_id}", "--sweep-values", repr(settings["mix"]),
                   "--sample-rate", str(sample_rate), "--block-size", str(block_size), "--channels", "2",
                   "--bits", "32", "--warmup-seconds", repr(float(warmup)), "--no-inputs", "--output", str(run)]
        for control in CONTROLS:
            if control.key != "mix":
                command += ["--set", f"id:{control.vst3_id}={settings[control.key]!r}"]
        finished = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        if finished.returncode != 0 or not (run / "run.json").exists():
            raise RuntimeError(f"Analyzer failed ({finished.returncode}): {finished.stdout[-800:]} {finished.stderr[-800:]}")
        facts = json.loads((run / "run.json").read_text())
        if facts.get("status") != "complete" or facts.get("failed") != 0 or facts.get("succeeded") != 1:
            raise RuntimeError("Analyzer run is not complete")
        for key, value in EXPECTED_PLUGIN.items():
            if facts.get("plugin", {}).get(key) != value:
                raise RuntimeError(f"plug-in {key} is {facts.get('plugin', {}).get(key)!r}")
        if facts.get("pluginArtifact", {}).get("binary", {}).get("sha256") != PLUGIN_BINARY_SHA256:
            raise RuntimeError("plug-in binary hash differs")
        if facts.get("caseIsolation") != "fresh_plugin_instance_per_case":
            raise RuntimeError("the Analyzer did not isolate the case")
        cases = sorted((run / "cases").iterdir())
        if len(cases) != 1:
            raise RuntimeError("expected exactly one case")
        result = json.loads((cases[0] / "result.json").read_text())
        if result.get("status") != "ok":
            raise RuntimeError(f"case failed: {result.get('error')}")
        readback = {}
        for entry in result["parameterReadback"]:
            if abs(entry["requestedNormalised"] - entry["readbackNormalised"]) > 1e-6:
                raise RuntimeError(f"read-back of {entry['selector']} is {entry['readbackNormalised']}")
            readback[entry["selector"]] = entry["displayText"]
        rate, output = wavfile.read(cases[0] / "output.wav")
        if rate != sample_rate or output.dtype != np.float32 or output.ndim != 2 or output.shape[1] != 2:
            raise RuntimeError("unexpected output format")
        if not np.all(np.isfinite(output)):
            raise RuntimeError("output is not finite")
        return output, int(result["metrics"]["reportedLatencySamples"]), {"readback": readback, "finishedAt": facts.get("finishedAt")}
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def capture(stimulus: np.ndarray, settings: dict | None = None, *, sample_rate: int = SAMPLE_RATE,
            block_size: int = BLOCK_SIZE, warmup: float = WARMUP_SECONDS, realisation: int = 0,
            timeout: float = 600.0) -> Capture:
    """Render a stereo stimulus through the reference, or return the cached capture.

    `settings` maps control keys to normalised values on top of BASELINE (use
    `normalised("decay", 2.0)` for display values). The stimulus must contain
    its own tail: the Analyzer renders only 0.25 s past its end.

    At Macro 0 the capture is rendered twice and accepted only when both renders
    are bit-identical. Above Macro 0 the reference is not repeatable; `realisation`
    then selects one of several independent renders.
    """
    stimulus = np.ascontiguousarray(stimulus, dtype=np.float32)
    if stimulus.ndim != 2 or stimulus.shape[1] != 2:
        raise ValueError("the stimulus must have shape (frames, 2)")
    if not 0 < stimulus.shape[0] <= MAX_STIMULUS_SECONDS * sample_rate:
        raise ValueError(f"the stimulus must be at most {MAX_STIMULUS_SECONDS} s long")
    if not np.all(np.isfinite(stimulus)):
        raise ValueError("the stimulus is not finite")
    if not 0.0 <= warmup <= 60.0:
        raise ValueError("the Analyzer accepts a warm-up of 0 to 60 s")
    resolved = resolve(settings)
    deterministic = resolved["macro"] == 0.0
    if deterministic and realisation != 0:
        raise ValueError("captures at Macro 0 are repeatable; they have no realisations")
    key = _key(stimulus, resolved, sample_rate, block_size, warmup, realisation)
    folder = CACHE / key[:2] / key
    if (folder / "meta.json").exists():
        meta = json.loads((folder / "meta.json").read_text())
        return Capture(np.load(folder / "output.npy"), meta["latency"], sample_rate, meta)
    if cache_bytes() > CACHE_LIMIT_BYTES:
        raise RuntimeError("the capture cache is over its size limit; remove captures that are no longer needed")
    output, latency, facts = _render(stimulus, resolved, sample_rate, block_size, warmup, timeout)
    if deterministic:
        again, latency_again, _ = _render(stimulus, resolved, sample_rate, block_size, warmup, timeout)
        if latency_again != latency or not np.array_equal(output, again):
            raise RuntimeError("two renders of a Macro 0 capture differ; the capture is rejected "
                               "(the machine may be overloaded, or the warm-up is too short)")
    meta = {
        "schema": CACHE_SCHEMA, "key": key, "latency": latency, "sampleRate": sample_rate, "blockSize": block_size,
        "warmupSeconds": float(warmup), "realisation": realisation, "verifiedRepeat": deterministic,
        "frames": int(output.shape[0]), "stimulusFrames": int(stimulus.shape[0]),
        "stimulusSha256": hashlib.sha256(stimulus.tobytes()).hexdigest(),
        "settings": resolved, "identity": identity(), "plugin": EXPECTED_PLUGIN, **facts,
    }
    pending = CACHE / key[:2] / f".{key}.{uuid.uuid4().hex}"
    pending.mkdir(parents=True)
    np.save(pending / "output.npy", output)
    (pending / "meta.json").write_text(json.dumps(meta, indent=1, sort_keys=True))
    try:
        pending.rename(folder)
    except OSError:                       # another process stored the same capture first
        shutil.rmtree(pending, ignore_errors=True)
    return Capture(output, latency, sample_rate, meta)


def silence(seconds: float, sample_rate: int = SAMPLE_RATE) -> np.ndarray:
    return np.zeros((int(round(seconds * sample_rate)), 2), np.float32)


def impulses(seconds: float, events, sample_rate: int = SAMPLE_RATE) -> np.ndarray:
    """Silence with unit-sample impulses; events are (sample index, channel or None for both, amplitude)."""
    stimulus = silence(seconds, sample_rate)
    for index, channel, amplitude in events:
        if channel is None:
            stimulus[index, :] += amplitude
        else:
            stimulus[index, channel] += amplitude
    return stimulus


def rms(signal: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(signal, dtype=np.float64)))) if signal.size else 0.0


def decibels(value: float) -> float:
    return 20.0 * math.log10(max(abs(value), 1e-300))


def null_db(candidate: np.ndarray, reference: np.ndarray) -> float:
    """20 log10(|candidate - reference| / |reference|) over the common length; no gain or delay fit."""
    frames = min(len(candidate), len(reference))
    difference = candidate[:frames].astype(np.float64) - reference[:frames].astype(np.float64)
    return decibels(rms(difference)) - decibels(rms(reference[:frames]))
