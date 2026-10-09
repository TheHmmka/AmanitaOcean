#!/usr/bin/env python3
"""The built Amanita Ocean plug-in with its sixth Character, rendered the way the reference is captured.

`capture` renders a stereo stimulus through the VST3 bundle of the local build (build-ebb, not installed)
with the Amanita Analyzer that `revocean.capture` drives for the reference: file suite, a fresh plug-in
instance per case, a warm-up of silence, stereo 32-bit float. Every host parameter of the plug-in is set
explicitly: the sixth Character is selected, Ocean's own controls are at neutral (Low Cut 20 Hz, High
Damping 20 kHz, Focus 0, Harmony 0, Mono Safe off, Freeze off) and the rest follow `settings`.

Every instance of the plug-in draws the seed of its voice phase when it is created and keeps it out of its
state, so every `capture` is another realisation above Evolution 0, as every capture of the reference is.
At Evolution 0 no voice moves and a capture repeats to the bit.

Mix is Ocean's own: dry 1 - mix and wet mix, the law of the other Characters, with the wet as it is at
100 %. The reference keeps its dry at unity up to 50 %; the two agree at 0 and at 100 % only.

    capture(stimulus, settings, sample_rate=..., warmup=...)   one render; `settings` are normalised host values
    knobs(decay=2.0, size=100.0, ...)                          normalised host values for display values
    physical(settings)                                         what the plug-in hands its engine at those values
    nearest(decay_seconds, size_scale, ...)                    the knob positions nearest to physical values
    render(...)                                                the candidate of score_network.py and score_tide.py

The plug-in reports no latency: its frame n is the reference's raw frame n plus the latency the reference
reports. `raw` moves a render back by that latency, into the time base of `revocean.capture`.

`physical` repeats the arithmetic between a normalised host value and the engine's parameters (JUCE's
ranges, Source/PluginProcessor.cpp and Source/dsp/FDNReverb.cpp) in single precision, so that the engine's
own renderer can be given the same numbers; a render of the plug-in at Evolution 0 that equals the
renderer's bit for bit confirms them. From 50 % the plug-in's Size scale is the percentage times 0.01f.
Below, it follows the compact-range curve of the plug-in, repeated here as the arm64 Release build
evaluates it (two fused multiply-adds); the reference's range starts at a scale of 0.3, which is 26.8 %
of Ocean's knob.

Nothing is cached: the plug-in changes with every build. The hash of the rendered binary is in the result.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import uuid
from pathlib import Path
from typing import NamedTuple

import numpy as np
from scipy.io import wavfile

import converters
import network_model
import reference_render
import revocean

ROOT = revocean.ROOT
PLUGIN = Path(os.environ.get("AMANITA_OCEAN_VST3", ROOT / "build-ebb/AmanitaOcean_artefacts/Release/VST3/Amanita Ocean.vst3"))
EXPECTED_PLUGIN = {"name": "Amanita Ocean", "manufacturer": "Amanita Ocean", "format": "VST3"}
SCRATCH = revocean.RESULTS / "work" / "engine" / "candidate_scratch"
CHARACTER_COUNT = 6
CHARACTER_INDEX = 5                    # the sixth Character; its name is not used here
SIZE_LAW_FROM_PERCENT = 50.0           # below it the plug-in's Size follows its compact-range curve
SIZE_MINIMUM_SCALE = 0.15              # FDNReverb::minimumSizeScale

F = np.float32


class Parameter(NamedTuple):
    key: str
    vst3_id: int
    name: str
    neutral: float             # normalised value of the measurement baseline
    text: str | None = None    # display text the host must read back at the baseline; None where `settings` decide


# Host parameters in the order the plug-in exports them. The baseline is the sixth Character at 100 % wet
# with Evolution 0, no pre-delay, Width 100 % and every control of Ocean's own at its neutral position.
PARAMETERS = (
    Parameter("character", 225490031, "Character", CHARACTER_INDEX / (CHARACTER_COUNT - 1)),
    Parameter("mix", 108124, "Mix", 1.0),
    Parameter("decay", 95459258, "Decay", 0.5),
    Parameter("size", 3530753, "Size", 0.5),
    Parameter("predelay", 827409568, "Pre-delay", 0.0),
    Parameter("lowCut", 1050588750, "Low Cut", 0.0, "20"),
    Parameter("highDamping", 1122843904, "High Damping", 1.0, "20000"),
    Parameter("evolution", 261136251, "Evolution", 0.0),
    Parameter("width", 113126854, "Width", 0.5),
    Parameter("focus", 97604824, "Focus", 0.0, "0.0"),
    Parameter("freeze", 881080983, "Freeze", 0.0, "Off"),
    Parameter("harmony", 696768774, "Harmony", 0.0, "0.0"),
    Parameter("monoSafe", 1823229200, "Mono Safe", 0.0, "Off"),
    Parameter("bypass", 1652125811, "Bypass", 0.0, "Off"),
)
PARAMETER = {parameter.key: parameter for parameter in PARAMETERS}
NEUTRAL = {parameter.key: parameter.neutral for parameter in PARAMETERS}
# The controls a capture may move. All but Mix follow the reference's laws; Mix is Ocean's own below 100 %.
MOVABLE = ("mix", "decay", "size", "predelay", "evolution", "width")


# ---------------------------------------------------------------- the plug-in's parameter laws

# Decay: smoothLogarithmicRange(0.2 s, 30 s, centre 3 s) of Source/PluginProcessor.cpp, double precision
# between single-precision ends.
_DECAY_LOW, _DECAY_HIGH, _DECAY_CENTRE = F(0.2), F(30.0), F(3.0)
_FULL_SPAN, _CENTRE_SPAN = math.log(float(_DECAY_HIGH / _DECAY_LOW)), math.log(float(_DECAY_CENTRE / _DECAY_LOW))
_QUADRATIC, _LINEAR = 2.0 * _FULL_SPAN - 4.0 * _CENTRE_SPAN, 4.0 * _CENTRE_SPAN - _FULL_SPAN


def decay_from_normalised(position) -> np.float32:
    position = F(position)
    if position <= 0.0:
        return _DECAY_LOW
    if position >= 1.0:
        return _DECAY_HIGH
    p = float(position)
    return F(float(_DECAY_LOW) * math.exp(_QUADRATIC * p * p + _LINEAR * p))


def decay_to_normalised(seconds) -> np.float32:
    seconds = F(seconds)
    if seconds <= _DECAY_LOW:
        return F(0.0)
    if seconds >= _DECAY_HIGH:
        return F(1.0)
    log_value = math.log(float(seconds) / float(_DECAY_LOW))
    denominator = _LINEAR + math.sqrt(max(0.0, _LINEAR * _LINEAR + 4.0 * _QUADRATIC * log_value))
    return F(min(1.0, max(0.0, float(F(2.0 * log_value / denominator)))))


def _stepped(position, high: float, interval: float = 0.1) -> np.float32:
    """A linear range from 0 to `high` with steps of `interval`, as JUCE snaps it."""
    high, interval = F(high), F(interval)
    value = F(interval * np.floor(F(F(F(high * F(position)) / interval) + F(0.5))))
    return F(min(max(value, F(0.0)), high))


def _size_scale(percent: np.float32) -> np.float32:
    """sizeScaleFromPercent of Source/PluginProcessor.cpp: the percentage times 0.01f from 50 % up, and below
    it minimum + (0.5 - 2 minimum) n + minimum n^2 with n = percent / 50."""
    if percent >= SIZE_LAW_FROM_PERCENT:
        return F(percent * F(0.01))
    minimum, n = F(SIZE_MINIMUM_SCALE), F(percent / F(SIZE_LAW_FROM_PERCENT))
    fused = network_model.fused_multiply_add
    return fused(F(minimum * n), n, fused(F(F(0.5) - F(F(2.0) * minimum)), n, minimum))


def _held(law, inverse, position) -> np.float32:
    """The value a parameter attachment holds for a host value: the law, back to normalised, and the law again."""
    return law(inverse(law(position)))


def physical(settings: dict | None) -> dict:
    """The engine's parameters and the outer stages' arguments at normalised host values, as single-precision
    numbers in double precision: decay_seconds, size_scale, predelay_seconds, macro, width_scale, and mix,
    the amount of Ocean's own Mix."""
    resolved = resolve(settings)
    percent = lambda key, high: _held(lambda p: _stepped(p, high), lambda v: F(v / F(high)), resolved[key])
    decay = _held(decay_from_normalised, decay_to_normalised, resolved["decay"])
    return {"decay_seconds": float(decay), "size_scale": float(_size_scale(percent("size", 200.0))),
            "predelay_seconds": float(F(percent("predelay", 250.0) * F(0.001))),
            "macro": float(F(percent("evolution", 100.0) * F(0.01))),
            "width_scale": float(F(percent("width", 200.0) * F(0.01))), "mix": float(F(percent("mix", 100.0) * F(0.01)))}


def knobs(**display) -> dict:
    """Normalised host values for display values: decay in seconds, size and width in percent of Ocean's knobs,
    predelay in milliseconds, evolution and mix as fractions."""
    laws = {"decay": lambda value: float(decay_to_normalised(value)), "size": lambda value: value / 200.0,
            "predelay": lambda value: value / 250.0, "width": lambda value: value / 200.0,
            "evolution": lambda value: value, "mix": lambda value: value}
    return {key: laws[key](value) for key, value in display.items()}


def nearest(decay_seconds: float, size_scale: float, predelay_seconds: float = 0.0, macro: float = 0.0,
            width_scale: float = 1.0, mix: float = 1.0) -> dict:
    """Normalised host values at which the plug-in works with the numbers nearest to the physical values
    given (the arguments of `reference_render.render`; `mix` is the amount of Ocean's own Mix, which means
    what the reference's means at 0 and 1 only). Size, Pre-delay, Evolution, Width and Mix move in steps of
    a tenth of a percent or of a millisecond; Decay is continuous and takes the single-precision host value
    whose Decay lies nearest."""
    position = decay_to_normalised(decay_seconds)
    candidates = [position]
    for direction in (0.0, 1.0):
        step = position
        for _ in range(8):
            step = np.nextafter(step, F(direction))
            candidates.append(step)
    decay = min(candidates, key=lambda p: abs(float(_held(decay_from_normalised, decay_to_normalised, p)) - float(decay_seconds)))
    tenths = lambda value, high: min(max(round(10.0 * value), 0), round(10.0 * high)) / (10.0 * high)
    size = min(range(2001), key=lambda step: abs(float(_size_scale(_stepped(step / 2000.0, 200.0))) - float(size_scale))) / 2000.0
    return {"decay": float(decay), "size": size,
            "predelay": tenths(1000.0 * predelay_seconds, 250.0), "evolution": tenths(100.0 * macro, 100.0),
            "width": tenths(100.0 * width_scale, 200.0), "mix": tenths(100.0 * mix, 100.0)}


def resolve(settings: dict | None) -> dict:
    """The baseline merged with the settings; every value is normalised and float32-exact."""
    merged = dict(NEUTRAL)
    for key, value in (settings or {}).items():
        if key not in MOVABLE:
            raise KeyError(f"{key!r} is not a control a capture may move; the others stay at neutral")
        merged[key] = value
    for key, value in merged.items():
        if not 0.0 <= value <= 1.0:
            raise ValueError(f"{key} = {value} is not a normalised value")
        merged[key] = float(F(value))
    return merged


# ---------------------------------------------------------------- rendering

class Render(NamedTuple):
    output: np.ndarray     # float32, (frames, 2), as the plug-in returns it
    latency: int           # samples the plug-in reports
    sample_rate: int
    meta: dict


def raw(output: np.ndarray, sample_rate: int) -> np.ndarray:
    """A render of the plug-in moved back by the latency the reference reports: the time base of `revocean.capture`."""
    latency = converters.reported_latency(sample_rate)
    result = np.zeros(output.shape, np.float64)
    result[latency:] = output[:len(output) - latency]
    return result


def capture(stimulus: np.ndarray, settings: dict | None = None, *, sample_rate: int = revocean.SAMPLE_RATE,
            block_size: int = revocean.BLOCK_SIZE, warmup: float = revocean.WARMUP_SECONDS, timeout: float = 600.0) -> Render:
    """One render of a stereo stimulus through a new instance of the built plug-in. `settings` maps the keys
    of MOVABLE to normalised host values on top of NEUTRAL (see `knobs`). The stimulus must contain its own tail."""
    stimulus = np.ascontiguousarray(stimulus, dtype=np.float32)
    if stimulus.ndim != 2 or stimulus.shape[1] != 2 or not np.all(np.isfinite(stimulus)):
        raise ValueError("the stimulus must be finite and have shape (frames, 2)")
    if not 0 < stimulus.shape[0] <= revocean.MAX_STIMULUS_SECONDS * sample_rate:
        raise ValueError(f"the stimulus must be at most {revocean.MAX_STIMULUS_SECONDS} s long")
    if not (PLUGIN / "Contents").is_dir():
        raise RuntimeError(f"{PLUGIN} is missing: build the target AmanitaOcean_VST3")
    if revocean.sha256_file(revocean.ANALYZER) != revocean.ANALYZER_SHA256:
        raise RuntimeError("the Analyzer is not the one the campaign is pinned to")
    resolved = resolve(settings)
    scratch = SCRATCH / uuid.uuid4().hex
    scratch.mkdir(parents=True)
    try:
        source, run = scratch / "stimulus.wav", scratch / "run"
        wavfile.write(source, sample_rate, stimulus)
        command = [str(revocean.ANALYZER), "run", "--plugin", str(PLUGIN), "--suite", "file", "--input-wav", str(source),
                   "--sweep", f"id:{PARAMETER['mix'].vst3_id}", "--sweep-values", repr(resolved["mix"]),
                   "--sample-rate", str(sample_rate), "--block-size", str(block_size), "--channels", "2",
                   "--bits", "32", "--warmup-seconds", repr(float(warmup)), "--no-inputs", "--output", str(run)]
        for parameter in PARAMETERS:
            if parameter.key != "mix":
                command += ["--set", f"id:{parameter.vst3_id}={resolved[parameter.key]!r}"]
        finished = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        if finished.returncode != 0 or not (run / "run.json").exists():
            raise RuntimeError(f"Analyzer failed ({finished.returncode}): {finished.stdout[-800:]} {finished.stderr[-800:]}")
        facts = json.loads((run / "run.json").read_text())
        if facts.get("status") != "complete" or facts.get("failed") != 0 or facts.get("succeeded") != 1:
            raise RuntimeError("Analyzer run is not complete")
        for key, value in EXPECTED_PLUGIN.items():
            if facts.get("plugin", {}).get(key) != value:
                raise RuntimeError(f"plug-in {key} is {facts.get('plugin', {}).get(key)!r}")
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
        texts = {parameter.key: readback.get(f"id:{parameter.vst3_id}") for parameter in PARAMETERS}
        if None in texts.values():
            raise RuntimeError(f"the host did not read back every parameter: {texts}")
        for parameter in PARAMETERS:
            if parameter.text is not None and texts[parameter.key] != parameter.text:
                raise RuntimeError(f"{parameter.name} reads {texts[parameter.key]!r} at the baseline, not {parameter.text!r}")
        rate, output = wavfile.read(cases[0] / "output.wav")
        if rate != sample_rate or output.dtype != np.float32 or output.ndim != 2 or output.shape[1] != 2:
            raise RuntimeError("unexpected output format")
        if not np.all(np.isfinite(output)):
            raise RuntimeError("output is not finite")
        meta = {"settings": resolved, "readback": texts, "sampleRate": sample_rate, "blockSize": block_size,
                "warmupSeconds": float(warmup), "pluginVersion": facts["plugin"].get("version"),
                "pluginBinarySha256": facts.get("pluginArtifact", {}).get("binary", {}).get("sha256"),
                "finishedAt": facts.get("finishedAt")}
        return Render(output, int(result["metrics"]["reportedLatencySamples"]), sample_rate, meta)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def reference_settings(decay_seconds: float, size_percent: float, macro_percent: float = 0.0) -> dict:
    """Normalised parameters of the reference for display values, as the campaign's scorers set them."""
    return {"decay": revocean.normalised("decay", decay_seconds), "size": revocean.normalised("size", size_percent),
            "macro": macro_percent / 100.0}


def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro_percent=0.0, seed=0, phase=None) -> np.ndarray:
    """The plug-in behind the interfaces of score_network.py and score_tide.py: its raw output at the baseline
    for display values of the reference's Decay, Size and Macro. Its knobs take the positions nearest to what
    the reference works with at those values (`nearest`). Every call renders a new instance, which draws a
    voice seed of its own: `seed` counts the realisations a scorer asks for and selects nothing, and no
    `phase` can be prescribed."""
    if phase is not None:
        raise ValueError("the plug-in runs from the voice seed its instance drew; it takes no phase")
    wanted = reference_render.physical(reference_settings(decay_seconds, size_percent, macro_percent))
    settings = nearest(wanted["decay_seconds"], wanted["size_scale"], macro=wanted["macro"])
    return raw(capture(stimulus, settings, sample_rate=int(sample_rate), warmup=warmup_seconds).output[:len(stimulus)], int(sample_rate))
