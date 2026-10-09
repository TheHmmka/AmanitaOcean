#!/usr/bin/env python3
"""Outer controls of Rev OCEAN around the Tide network: Mix, Return, Master Volume, Width, Pre-delay, levels.

What is measured. With Macro at 0 the reference is linear and repeatable, so a
correct law of an outer control predicts one capture from another exactly. Each
law below is therefore stated as a formula, used to predict a capture from the
100 % wet capture of the same stimulus, and scored by the null

    20 log10(rms(prediction - capture) / rms(capture))

without any fitted gain, delay or polarity. Fitted coefficients are reported
next to the law so the two can be compared.

The owner listens at Macro (the Tide knob) 100 %, Mix 100 %. There the reference
is not repeatable between instances, so the same laws are checked again at
Macro 100 % with the parts that stay exact (the dry coefficient in a window the
wet has not reached, mono output at Width 0 %, the first non-zero wet sample
against Pre-delay) and statistically for the rest (levels of 18 s of noise over
several realisations).

Two level-dependent stages sit outside the network and are active at the
neutral state: a soft clipper on the final output and the Ducking compressor,
whose threshold at Ducking 0 % is 0 dBFS. Both are measured here because every
other law holds only below their thresholds.

Run (all captures are cached; a cold run renders roughly 550 short cases):

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    cd Analyzer/Campaigns/RevOceanCharacterization
    $PY measure_io.py            # writes tide_structural_data/io.json
"""
from __future__ import annotations

import json
import math
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

import revocean as ro

FS = ro.SAMPLE_RATE
DECAY = ro.normalised("decay", 0.5)
DATA = Path(__file__).resolve().parent / "tide_structural_data" / "io.json"
WORKERS = 6

CLIP_THRESHOLD = 10.0 ** (8.0 / 20.0)      # the output clipper is linear up to +8 dBFS
CLIP_CEILING = 10.0 ** (12.0 / 20.0)       # and never exceeds +12 dBFS
DUCK_SLOPE = 5.0 / 7.0                     # gain reduction per dB above the knee (ratio 3.5 : 1)
DUCK_KNEE_DB = 9.983                       # fitted knee width at Ducking 0 %; 10 dB is the round value
DUCK_ATTACK_MS = 5.0
DUCK_RELEASE_MS = 300.0


# ---------------------------------------------------------------------------
# The laws. Everything below this block only tests them.
# ---------------------------------------------------------------------------

def single(value: float) -> float:
    """A normalised parameter as the host interface carries it."""
    return float(np.float32(value))


def mix_gains(mix: float) -> tuple:
    """(dry gain, wet gain) at normalised Mix: the dry stays at unity up to 50 %, the wet from 50 %."""
    return min(1.0, 2.0 * (1.0 - mix)), min(1.0, 2.0 * mix)


def decibel_gain(key: str, normalised: float) -> float:
    """Return and Master Volume are 10^(displayed dB / 20); Master at exactly 0 is a mute."""
    if key == "master" and normalised == 0.0:
        return 0.0
    return 10.0 ** (ro.CONTROL[key].display(normalised) / 20.0)


def width_gains(normalised: float) -> tuple:
    """(mid gain, side gain) of the wet output: side/mid = 2 x, both scaled by sqrt(2 / (1 + 2 x))."""
    side_ratio = 2.0 * normalised
    mid = math.sqrt(2.0 / (1.0 + side_ratio))
    return mid, side_ratio * mid


def width_matrix(normalised: float) -> np.ndarray:
    """Row vector of wet [L, R] times this matrix gives the wet at that Width."""
    mid, side = width_gains(normalised)
    return np.array([[(mid + side) / 2.0, (mid - side) / 2.0], [(mid - side) / 2.0, (mid + side) / 2.0]])


def predelay_samples(normalised: float, sample_rate: int) -> int:
    """Whole samples by which Pre-delay delays the input of the network, relative to Pre-delay 0."""
    exact = ro.CONTROL["predelay"].display(normalised) * sample_rate / 1000.0
    return max(0, math.floor(exact) - 1)


def soft_clip(signal: np.ndarray, threshold: float = CLIP_THRESHOLD, ceiling: float = CLIP_CEILING) -> np.ndarray:
    """Static output clipper: unity to the threshold, a quadratic knee, then the ceiling."""
    magnitude = np.abs(signal)
    knee = magnitude - (magnitude - threshold) ** 2 / (4.0 * (ceiling - threshold))
    return np.sign(signal) * np.where(magnitude <= threshold, magnitude,
                                      np.where(magnitude >= 2.0 * ceiling - threshold, ceiling, knee))


def ducking_curve_db(level_db: np.ndarray, slope: float = DUCK_SLOPE, knee_db: float = DUCK_KNEE_DB,
                     threshold_db: float = 0.0) -> np.ndarray:
    """Static gain of the Ducking compressor at Ducking 0 %: a soft-knee curve around 0 dBFS."""
    over = np.asarray(level_db, dtype=np.float64) - threshold_db
    knee = -slope * (over + knee_db / 2.0) ** 2 / (2.0 * knee_db)
    return np.where(over <= -knee_db / 2.0, 0.0, np.where(over >= knee_db / 2.0, -slope * over, knee))


def ducking_gain_db(stimulus: np.ndarray, sample_rate: int, latency: int, frames: int,
                    knee_db: float = DUCK_KNEE_DB) -> np.ndarray:
    """Wet gain in dB per output sample: the curve of max(|L|, |R|) of the input, smoothed in dB
    with a 5 ms one-pole when the reduction grows and a 300 ms one-pole when it recovers.
    The smoother runs in single precision as g = G + k (g - G), k = exp(-1 / (tau fs)); in double
    precision the same law nulls 40 dB less deeply."""
    key = np.max(np.abs(stimulus.astype(np.float64)), axis=1)
    reduction = np.zeros(frames)
    if key.max() <= 10.0 ** (-knee_db / 40.0):
        return reduction
    with np.errstate(divide="ignore"):
        target = ducking_curve_db(20.0 * np.log10(key), knee_db=knee_db).astype(np.float32)
    attack = np.float32(math.exp(-1.0 / (DUCK_ATTACK_MS * 1e-3 * sample_rate)))
    release = np.float32(math.exp(-1.0 / (DUCK_RELEASE_MS * 1e-3 * sample_rate)))
    state = np.float32(0.0)
    for index in range(frames - latency):
        goal = target[index] if index < len(target) else np.float32(0.0)
        state = goal + (attack if goal < state else release) * (state - goal)
        reduction[index + latency] = state
    return reduction


def outer_shell(stimulus: np.ndarray, wet: np.ndarray, settings: dict, sample_rate: int, latency: int) -> np.ndarray:
    """Output of the reference from its input and from `wet`, the 100 % wet output of the network
    for the input delayed by predelay_samples (Width 100 %, Return and Master 0 dB, no ducking)."""
    resolved = ro.resolve(settings)
    dry_gain, wet_gain = mix_gains(resolved["mix"])
    ducked = wet * (10.0 ** (ducking_gain_db(stimulus, sample_rate, latency, len(wet)) / 20.0))[:, None]
    shaped = ducked @ width_matrix(resolved["width"]) * decibel_gain("return", resolved["return"])
    mixed = dry_gain * delayed(stimulus, len(wet), latency) + wet_gain * shaped
    return soft_clip(decibel_gain("master", resolved["master"]) * mixed)


# ---------------------------------------------------------------------------
# Stimuli and helpers
# ---------------------------------------------------------------------------

def probe(sample_rate: int = FS) -> np.ndarray:
    """3 s: an L impulse, an R impulse, a two-channel impulse and 100 ms of independent stereo noise."""
    generator = np.random.default_rng(1)
    stimulus = np.zeros((int(3.0 * sample_rate), 2), np.float32)
    stimulus[int(0.25 * sample_rate), 0] = 0.5
    stimulus[int(0.75 * sample_rate), 1] = 0.5
    stimulus[int(1.25 * sample_rate), 0] = 0.5
    stimulus[int(1.25 * sample_rate), 1] = -0.3
    start, stop = int(1.75 * sample_rate), int(1.85 * sample_rate)
    stimulus[start:stop] = generator.uniform(-0.25, 0.25, (stop - start, 2)).astype(np.float32)
    return stimulus


def padded(stimulus: np.ndarray, seconds: float, sample_rate: int = FS) -> np.ndarray:
    return np.concatenate([stimulus, np.zeros((int(seconds * sample_rate) - len(stimulus), 2), np.float32)])


def shifted(stimulus: np.ndarray, samples: int) -> np.ndarray:
    """The stimulus moved later by a whole number of samples (earlier when negative), same length."""
    result = np.zeros_like(stimulus)
    if samples >= 0:
        result[samples:] = stimulus[: len(stimulus) - samples]
    else:
        result[:samples] = stimulus[-samples:]
    return result


def delayed(stimulus: np.ndarray, frames: int, latency: int) -> np.ndarray:
    """The dry signal as it appears in a capture of `frames` frames."""
    result = np.zeros((frames, 2), np.float64)
    result[latency:latency + len(stimulus)] = stimulus[: frames - latency]
    return result


def capture_all(jobs: dict) -> dict:
    """jobs maps a key to (stimulus, settings on top of Decay 0.5 s, capture keyword arguments)."""
    def render(key):
        stimulus, settings, options = jobs[key]
        return ro.capture(stimulus, {"decay": DECAY, **settings}, **options)
    keys = list(jobs)
    with ThreadPoolExecutor(WORKERS) as pool:
        return dict(zip(keys, pool.map(render, keys)))


def wide(capture: ro.Capture) -> np.ndarray:
    return capture.output.astype(np.float64)


def fit(columns: list, target: np.ndarray) -> tuple:
    """Least-squares coefficients of the columns and the null of the fit."""
    matrix = np.stack([column.ravel() for column in columns], axis=1)
    coefficients, *_ = np.linalg.lstsq(matrix, target.ravel(), rcond=None)
    return coefficients, ro.null_db(matrix @ coefficients, target.ravel())


def db(value: float) -> float:
    return 20.0 * math.log10(value) if value > 0.0 else float("-inf")


def rounded(value, digits: int = 6):
    if isinstance(value, (list, tuple, np.ndarray)):
        return [rounded(item, digits) for item in value]
    if value is None or isinstance(value, (bool, str, int)):
        return value
    value = float(value)
    if not math.isfinite(value):
        return None
    return round(value, digits)


# ---------------------------------------------------------------------------
# 1. Mix
# ---------------------------------------------------------------------------

MIX_PERCENT = (0, 1, 2, 5, 10, 12.5, 20, 25, 30, 100 / 3, 40, 45, 49, 50, 51, 55, 60, 200 / 3, 70, 75, 80, 87.3, 90, 95, 98, 99, 100)


def measure_mix() -> dict:
    stimulus = probe()
    captures = capture_all({percent: (stimulus, {"mix": percent / 100.0}, {}) for percent in MIX_PERCENT})
    wet = wide(captures[100])
    latency = captures[100].latency
    dry = delayed(stimulus, len(wet), latency)
    points = []
    for percent in MIX_PERCENT:
        output = wide(captures[percent])
        mix = single(percent / 100.0)
        dry_gain, wet_gain = mix_gains(mix)
        coefficients, fit_null = fit([dry, wet], output)
        single_precision = np.float32(dry_gain) * dry.astype(np.float32) + np.float32(wet_gain) * captures[100].output
        points.append({
            "mixPercent": rounded(percent, 4), "displayed": captures[percent].meta["readback"]["id:1"],
            "dryGainFitted": rounded(coefficients[0], 9), "wetGainFitted": rounded(coefficients[1], 9),
            "dryGainLaw": rounded(dry_gain, 9), "wetGainLaw": rounded(wet_gain, 9),
            "fitNullDb": rounded(fit_null, 2), "lawNullDb": rounded(ro.null_db(dry_gain * dry + wet_gain * wet, output), 2),
            "singlePrecisionFormulaBitIdentical": bool(np.array_equal(single_precision, captures[percent].output)),
        })
    partial = [point["lawNullDb"] for point in points if 0 < point["mixPercent"] < 100]
    return {
        "law": "out = a(m) * dry[n - latency] + b(m) * wet,  a = min(1, 2 (1 - m)),  b = min(1, 2 m),  m = Mix / 100",
        "latencySamples": latency,
        "mix0BitIdenticalToDelayedInput": bool(np.array_equal(captures[0].output, dry.astype(np.float32))),
        "worstLawNullDb": max(partial), "points": points,
    }


# ---------------------------------------------------------------------------
# 2. Return and Master Volume
# ---------------------------------------------------------------------------

RETURN_DB = (-24, -18, -12, -6, -3, -1, -0.5, 0.5, 1, 3, 6, 12, 18, 24, 7.3)
MASTER_NORMALISED = (0.0, 0.02, 0.1, 0.25, 0.5, 0.6, 0.75, 0.9, 1.0)
MASTER_DB = (-60, -40, -20, -12, -6, -3, -1, 1, 3, 6)
MASTER_FLOOR = (1e-6, 1e-5, 1e-4, 1e-3, 3e-3, 0.01)


def gain_jobs() -> dict:
    jobs = {}
    for value in RETURN_DB:
        for mix in (1.0, 0.5) + ((0.3, 0.8) if value in (-6, 6, 7.3) else ()):
            jobs[("return", ro.normalised("return", value), mix)] = None
    for normalised in MASTER_NORMALISED:
        for mix in (1.0, 0.5):
            jobs[("master", normalised, mix)] = None
    for value in MASTER_DB:
        for mix in (1.0, 0.3):
            jobs[("master", ro.normalised("master", value), mix)] = None
    for normalised in MASTER_FLOOR:
        jobs[("master", normalised, 0.5)] = None
    return jobs


def measure_gains() -> dict:
    stimulus = probe()
    jobs = {key: (stimulus, {key[0]: key[1], "mix": key[2]}, {}) for key in gain_jobs()}
    jobs["wet"] = (stimulus, {}, {})
    captures = capture_all(jobs)
    wet = wide(captures["wet"])
    dry = delayed(stimulus, len(wet), captures["wet"].latency)
    result = {"return": [], "master": []}
    for key, capture in captures.items():
        if key == "wet":
            continue
        control, normalised, mix = key
        output = wide(capture)
        settings = {control: normalised, "mix": mix}
        prediction = outer_shell(stimulus, wet, settings, FS, capture.latency)
        entry = {
            "normalised": rounded(single(normalised), 8), "displayed": capture.meta["readback"][f"id:{ro.CONTROL[control].vst3_id}"],
            "lawDb": rounded(ro.CONTROL[control].display(single(normalised)), 5), "mixPercent": rounded(100 * mix, 2),
            "lawNullDb": rounded(ro.null_db(prediction, output), 2) if ro.rms(output) else None,
            "outputIsSilent": not bool(np.any(output)),
        }
        if ro.rms(output):
            coefficients, fit_null = fit([dry, wet], output)
            entry.update({"dryGainFittedDb": rounded(db(abs(coefficients[0])), 5) if abs(coefficients[0]) > 1e-9 else None,
                          "wetGainFittedDb": rounded(db(abs(coefficients[1])), 5), "fitNullDb": rounded(fit_null, 2)})
        result[control].append(entry)
    for control in result:
        result[control].sort(key=lambda entry: (entry["normalised"], entry["mixPercent"]))
        result[control] = {"worstLawNullDb": max(entry["lawNullDb"] for entry in result[control] if entry["lawNullDb"] is not None),
                           "points": result[control]}
    result["return"]["law"] = "wet gain = 10^(displayed dB / 20), dry untouched"
    result["master"]["law"] = "gain of dry and wet = 10^(displayed dB / 20); normalised 0 (-70 dB) mutes"
    return result


# ---------------------------------------------------------------------------
# 3. Width
# ---------------------------------------------------------------------------

WIDTH_PERCENT = (0, 1, 10, 25, 50, 75, 90, 99, 100, 101, 110, 125, 140, 150)


def measure_width() -> dict:
    stereo = probe()
    left = stereo.copy()
    left[:, 1] = 0.0
    right = np.zeros_like(stereo)
    right[:, 1] = stereo[:, 0]                      # the left waveform on the right input
    stimuli = {"stereo": stereo, "left": left, "right": right}
    jobs = {}
    for percent in WIDTH_PERCENT:
        setting = ro.normalised("width", percent)
        for name in stimuli:
            jobs[(name, percent, 1.0)] = (stimuli[name], {"width": setting, "mix": 1.0}, {})
        jobs[("stereo", percent, 0.5)] = (stereo, {"width": setting, "mix": 0.5}, {})
    for percent in (0, 50, 150):
        jobs[("stereo", percent, 0.8)] = (stereo, {"width": ro.normalised("width", percent), "mix": 0.8}, {})
    captures = capture_all(jobs)
    reference = {name: wide(captures[(name, 100, 1.0)]) for name in stimuli}
    latency = captures[("stereo", 100, 1.0)].latency
    points = []
    for percent in WIDTH_PERCENT:
        normalised = single(ro.normalised("width", percent))
        output = wide(captures[("stereo", percent, 1.0)])
        free, *_ = np.linalg.lstsq(reference["stereo"], output, rcond=None)
        mid_law, side_law = width_gains(normalised)
        left_output = wide(captures[("left", percent, 1.0)])
        _, input_null = fit([reference["left"], reference["right"]], left_output)
        mixed = {}
        for mix in (0.5, 0.8):
            if ("stereo", percent, mix) in captures:
                settings = {"width": normalised, "mix": mix}
                mixed[f"lawNullDbAtMix{int(100 * mix)}"] = rounded(ro.null_db(
                    outer_shell(stereo, reference["stereo"], settings, FS, latency), wide(captures[("stereo", percent, mix)])), 2)
        points.append({
            "widthPercent": percent, "displayed": captures[("stereo", percent, 1.0)].meta["readback"]["id:14"],
            "normalised": rounded(normalised, 8),
            "midGainFitted": rounded(free[0, 0] + free[1, 0], 7), "sideGainFitted": rounded(free[0, 0] - free[1, 0], 7),
            "midGainLaw": rounded(mid_law, 7), "sideGainLaw": rounded(side_law, 7),
            "freeMatrixNullDb": rounded(ro.null_db(reference["stereo"] @ free, output), 2),
            "lawNullDb": rounded(ro.null_db(reference["stereo"] @ width_matrix(normalised), output), 2),
            "lawNullDbLeftOnlyInput": rounded(ro.null_db(reference["left"] @ width_matrix(normalised), left_output), 2),
            "inputMatrixHypothesisNullDb": rounded(input_null, 2), **mixed,
            "outputIsMono": bool(np.array_equal(captures[("stereo", percent, 1.0)].output[:, 0], captures[("stereo", percent, 1.0)].output[:, 1])),
        })
    return {
        "law": "wet mid gain = sqrt(2 / (1 + s)), wet side gain = s sqrt(2 / (1 + s)), s = 2 x = -log2(1 - Width% / 200); dry untouched",
        "worstLawNullDb": max(point["lawNullDb"] for point in points if point["widthPercent"] != 100),
        "bestInputMatrixHypothesisNullDb": min(point["inputMatrixHypothesisNullDb"] for point in points if point["widthPercent"] != 100),
        "points": points,
    }


# ---------------------------------------------------------------------------
# 4 and 5. Pre-delay, also at the other sample rates
# ---------------------------------------------------------------------------

PREDELAY_INTEGER = (48, 480, 4800, 14400)
PREDELAY_FRACTIONAL = (0.5, 0.9, 1.1, 1.25, 1.9, 2.0, 2.1, 2.9, 3.1, 3.9, 10.5, 47.5, 100.5, 240.25, 479.5, 480.5, 1000.75,
                       4799.5, 4800.5, 9600.4, 14399.5, 14400.5)
PREDELAY_EDGES = tuple(480 + offset for offset in (-0.05, -0.01, -0.003, -0.001, -0.0003, 0.0003, 0.001, 0.003, 0.01, 0.05)) \
    + tuple(14400 + offset for offset in (-0.05, -0.01, -0.003, -0.001, 0.001, 0.003, 0.01, 0.05)) \
    + tuple(4800 + offset for offset in (-0.05, -0.01, -0.003, -0.001, 0.001, 0.003, 0.01, 0.05))
PREDELAY_LONG = (24000.5, 48000.5, 95000.5, 96000)
PREDELAY_KERNEL = (100.5, 240.25, 1000.75)
OTHER_RATES = (44100, 88200, 96000)


def predelay_setting(samples: float, sample_rate: int = FS) -> float:
    return single(ro.normalised("predelay", samples / sample_rate * 1000.0))


def predelay_cases(targets, seconds: float, sample_rate: int = FS) -> list:
    """For each target the capture with Pre-delay is compared bit for bit with captures at
    Pre-delay 0 whose stimulus was moved later by floor(target) - 2, - 1 and - 0 samples."""
    stimulus = padded(probe(sample_rate), seconds, sample_rate)
    options = {"sample_rate": sample_rate}
    jobs = {}
    for target in targets:
        jobs[("predelay", target)] = (stimulus, {"predelay": predelay_setting(target, sample_rate)}, options)
        for candidate in range(math.floor(target) - 2, math.floor(target) + 1):
            jobs[("shift", candidate)] = (shifted(stimulus, candidate), {}, options)
    captures = capture_all(jobs)
    cases = []
    for target in targets:
        normalised = predelay_setting(target, sample_rate)
        capture = captures[("predelay", target)]
        matches = [candidate for candidate in range(math.floor(target) - 2, math.floor(target) + 1)
                   if np.array_equal(captures[("shift", candidate)].output, capture.output)]
        exact = ro.CONTROL["predelay"].display(normalised) * sample_rate / 1000.0
        cases.append({
            "sampleRate": sample_rate, "targetSamples": rounded(target, 4), "lawSamples": rounded(exact, 6),
            "displayedMs": capture.meta["readback"]["id:11"], "latency": capture.latency,
            "bitIdenticalToInputShift": matches[0] if len(matches) == 1 else None,
            "lawDelay": predelay_samples(normalised, sample_rate),
            "distanceToIntegerSamples": rounded(abs(exact - round(exact)), 6),
        })
        cases[-1]["lawHolds"] = cases[-1]["bitIdenticalToInputShift"] == cases[-1]["lawDelay"]
    return cases


def predelay_alternatives() -> dict:
    """Two rejected readings: a delay after the network, and an interpolated (fractional) delay."""
    stimulus = padded(probe(), 3.5)
    jobs = {"base": (stimulus, {}, {})}
    for target in PREDELAY_INTEGER + PREDELAY_KERNEL:
        jobs[("predelay", target)] = (stimulus, {"predelay": predelay_setting(target)}, {})
    for target in PREDELAY_KERNEL:
        for candidate in range(math.floor(target) - 4, math.floor(target) + 6):
            jobs[("shift", candidate)] = (shifted(stimulus, candidate), {}, {})
    captures = capture_all(jobs)
    base = wide(captures["base"])
    after_network = {}
    for target in PREDELAY_INTEGER:
        moved = np.zeros_like(base)
        moved[target - 1:] = base[: len(base) - (target - 1)]
        after_network[str(target)] = rounded(ro.null_db(moved, wide(captures[("predelay", target)])), 2)
    kernels = {}
    for target in PREDELAY_KERNEL:
        candidates = list(range(math.floor(target) - 4, math.floor(target) + 6))
        coefficients, fit_null = fit([wide(captures[("shift", candidate)]) for candidate in candidates], wide(captures[("predelay", target)]))
        kernels[str(target)] = {"shifts": candidates, "coefficients": rounded(coefficients, 9), "fitNullDb": rounded(fit_null, 2)}
    return {"nullDbIfTheWetOutputWereDelayedInstead": after_network, "kernelOverNeighbouringShifts": kernels}


def measure_predelay() -> dict:
    clear = predelay_cases(PREDELAY_FRACTIONAL, 3.5) + predelay_cases(PREDELAY_LONG, 5.5)
    edges = predelay_cases(PREDELAY_INTEGER + PREDELAY_EDGES, 3.5)
    rates = []
    for sample_rate in OTHER_RATES:
        rates += predelay_cases((0.5, 1.5, 2.5, 10.4, 100.6, 1000.3, sample_rate * 0.1 + 0.5, sample_rate * 0.3 + 0.5), 3.5, sample_rate)
    misses = [case["distanceToIntegerSamples"] for case in edges if not case["lawHolds"]]
    beyond = [case["lawHolds"] for case in edges if case["distanceToIntegerSamples"] > max(misses)]
    return {
        "law": "network input = input delayed by D = max(0, floor(P fs / 1000) - 1) whole samples, P = displayed ms; no interpolation",
        "mainSet": {"cases": len(clear), "lawHoldsInAll": all(case["lawHolds"] for case in clear), "points": clear},
        "atIntegerBoundaries": {
            "cases": len(edges), "lawHolds": sum(case["lawHolds"] for case in edges),
            "largestDistanceWhereTheLawMisses": max(misses), "casesBeyondThatDistance": len(beyond),
            "lawHoldsInAllBeyondThatDistance": all(beyond), "points": edges,
        },
        "otherSampleRates": {"cases": len(rates), "lawHoldsInAll": all(case["lawHolds"] for case in rates), "points": rates},
        "alternatives": predelay_alternatives(),
    }


def measure_sample_rates() -> dict:
    """Latency of the dry path and the Mix law at each rate."""
    result = {}
    for sample_rate in (44100, 48000, 88200, 96000):
        stimulus = probe(sample_rate)
        captures = capture_all({mix: (stimulus, {"mix": mix}, {"sample_rate": sample_rate}) for mix in (0.0, 0.25, 0.5, 0.75, 1.0)})
        wet = wide(captures[1.0])
        latency = captures[1.0].latency
        dry = delayed(stimulus, len(wet), latency)
        result[str(sample_rate)] = {
            "reportedLatencySamples": latency, "latencyMs": rounded(1000.0 * latency / sample_rate, 4),
            "mix0BitIdenticalToInputDelayedByLatency": bool(np.array_equal(captures[0.0].output, dry.astype(np.float32))),
            "mixLawNullDb": {str(int(100 * mix)): rounded(ro.null_db(mix_gains(mix)[0] * dry + mix_gains(mix)[1] * wet, wide(captures[mix])), 2)
                             for mix in (0.25, 0.5, 0.75)},
        }
    return result


# ---------------------------------------------------------------------------
# 6. Outside the network at the neutral state: bypass, DC, output clipper, ducking
# ---------------------------------------------------------------------------

def combined_state() -> dict:
    """One capture with every outer control off its neutral value, predicted from a neutral capture."""
    stimulus = padded(probe(), 3.5)
    settings = {"predelay": predelay_setting(1000.4), "width": 0.8, "return": ro.normalised("return", -4.5),
                "master": ro.normalised("master", 2.5), "mix": 0.3}
    delay = predelay_samples(settings["predelay"], FS)
    captures = capture_all({"state": (stimulus, settings, {}), "wet": (shifted(stimulus, delay), {}, {}),
                            "mix50": (stimulus, {"predelay": settings["predelay"], "mix": 0.5}, {})})
    wet = wide(captures["wet"])
    latency = captures["wet"].latency
    return {
        "settings": "Pre-delay 20.84 ms, Width normalised 0.8, Return -4.5 dB, Master +2.5 dB, Mix 30 %",
        "predelaySamples": delay,
        "lawNullDb": rounded(ro.null_db(outer_shell(stimulus, wet, settings, FS, latency), wide(captures["state"])), 2),
        "predelayLeavesTheDryUndelayedNullDbAtMix50": rounded(ro.null_db(
            outer_shell(stimulus, wet, {"mix": 0.5}, FS, latency), wide(captures["mix50"])), 2),
    }


def level_stimulus() -> np.ndarray:
    """1.5 s of DC, impulses of 4 and -8, and 100 ms of noise of +-2: all far above full scale."""
    generator = np.random.default_rng(2)
    stimulus = np.zeros((int(4.0 * FS), 2), np.float32)
    stimulus[int(0.1 * FS):int(1.6 * FS), 0] = 0.5
    stimulus[int(0.1 * FS):int(1.6 * FS), 1] = -0.25
    stimulus[int(2.2 * FS), 0] = 4.0
    stimulus[int(2.7 * FS), 1] = -8.0
    start, stop = int(3.2 * FS), int(3.3 * FS)
    stimulus[start:stop] = generator.uniform(-2.0, 2.0, (stop - start, 2)).astype(np.float32)
    return stimulus


def measure_neutral_path() -> dict:
    stimulus = probe()
    loud = level_stimulus()
    quiet = (loud / 64.0).astype(np.float32)
    captures = capture_all({
        "bypass100": (stimulus, {"bypass": 1.0, "mix": 1.0}, {}), "bypass50": (stimulus, {"bypass": 1.0, "mix": 0.5}, {}),
        "loudWet": (loud, {}, {}), "quietWet": (quiet, {}, {}), "loudDry": (loud, {"mix": 0.0}, {}),
    })
    dry = delayed(stimulus, len(captures["bypass100"].output), captures["bypass100"].latency).astype(np.float32)
    wet = wide(captures["loudWet"])
    step = int(0.1 * FS) + 48
    plateau = [wet[step + int(seconds * FS) - 2400:step + int(seconds * FS) + 2400].mean(axis=0) for seconds in (0.75, 1.45)]
    loud_dry = wide(captures["loudDry"])
    burst = slice(int(3.2 * FS) + 48, int(3.3 * FS) + 48)
    segments = {"dcStep": (0.0, 2.15), "impulse4": (2.15, 2.65), "impulse8": (2.65, 3.15), "noise2": (3.15, 4.2)}
    linear = wide(captures["quietWet"]) * 64.0
    return {
        "bypass": {
            "displayed": captures["bypass100"].meta["readback"]["id:0"],
            "outputBitIdenticalToInputDelayedByLatency": bool(np.array_equal(captures["bypass100"].output, dry)
                                                              and np.array_equal(captures["bypass50"].output, dry)),
        },
        "wetPassesDc": {
            "inputDc": [0.5, -0.25], "wetMean0p75sAfterOnset": rounded(plateau[0], 7), "wetMean1p45sAfterOnset": rounded(plateau[1], 7),
            "relativeDriftOver0p7s": rounded(float(np.max(np.abs(plateau[1] - plateau[0]) / np.abs(plateau[0]))), 8),
        },
        "dryAtMix0": {
            "noiseOfPeak2BitIdenticalToInput": bool(np.array_equal(loud_dry[burst], loud[int(3.2 * FS):int(3.3 * FS)].astype(np.float64))),
            "impulseOf4Becomes": rounded(float(loud_dry[int(2.2 * FS) + 48, 0]), 7),
            "impulseOfMinus8Becomes": rounded(float(loud_dry[int(2.7 * FS) + 48, 1]), 7),
        },
        "wetAgainstTheSameStimulus64TimesQuieterNullDb": {
            name: rounded(ro.null_db(wet[int(a * FS):int(b * FS)], linear[int(a * FS):int(b * FS)]), 2) for name, (a, b) in segments.items()},
        "combinedState": combined_state(),
    }


def clip_stimulus() -> np.ndarray:
    """A ramp from -10 to 10 (half as large and inverted on R), noise of +-6 and two loud sines."""
    generator = np.random.default_rng(3)
    ramp = np.linspace(-10.0, 10.0, 40001).astype(np.float32)
    stimulus = np.zeros((int(3.0 * FS), 2), np.float32)
    stimulus[4800:4800 + len(ramp), 0] = ramp
    stimulus[4800:4800 + len(ramp), 1] = -0.5 * ramp
    stimulus[60000:84000] = generator.uniform(-6.0, 6.0, (24000, 2)).astype(np.float32)
    time = np.arange(24000) / FS
    stimulus[90000:114000, 0] = (7.0 * np.sin(2 * np.pi * 100.0 * time)).astype(np.float32)
    stimulus[90000:114000, 1] = (5.0 * np.sin(2 * np.pi * 3000.0 * time)).astype(np.float32)
    return stimulus


def hot_stimulus() -> np.ndarray:
    """Input below the ducking knee whose wet, raised by Return and Master, reaches the clipper."""
    generator = np.random.default_rng(11)
    stimulus = np.zeros((int(3.0 * FS), 2), np.float32)
    stimulus[int(0.1 * FS):int(0.4 * FS)] = generator.uniform(-0.5, 0.5, (int(0.4 * FS) - int(0.1 * FS), 2)).astype(np.float32)
    stimulus[int(1.2 * FS), 0] = 0.5
    stimulus[int(1.7 * FS), 1] = -0.5
    return stimulus


def clipper_position() -> dict:
    """Where the clipper sits: predictions with the clipper last, before Master, and on the wet only."""
    stimulus = hot_stimulus()
    loud = {"return": 1.0, "master": 1.0}
    captures = capture_all({"wet": (stimulus, {}, {}), "mix100": (stimulus, {**loud, "mix": 1.0}, {}),
                            "mix50": (stimulus, {**loud, "mix": 0.5}, {}), "mix50master0": (stimulus, {"return": 1.0, "mix": 0.5}, {})})
    wet = wide(captures["wet"])
    latency = captures["wet"].latency
    dry = delayed(stimulus, len(wet), latency)
    raised = decibel_gain("return", 1.0) * wet
    master = decibel_gain("master", 1.0)
    output = wide(captures["mix50"])
    return {
        "wetPeakAtReturn0": rounded(float(np.abs(wet).max()), 5), "unclippedPeakAtMix50": rounded(float(np.abs(master * (dry + raised)).max()), 4),
        "capturedPeakAtMix50": rounded(float(np.abs(output).max()), 6),
        "lawNullDb": {name: rounded(ro.null_db(outer_shell(stimulus, wet, settings, FS, latency), wide(captures[name])), 2)
                      for name, settings in (("mix100", {**loud, "mix": 1.0}), ("mix50", {**loud, "mix": 0.5}),
                                             ("mix50master0", {"return": 1.0, "mix": 0.5}))},
        "nullDbWithoutClipper": rounded(ro.null_db(master * (dry + raised), output), 2),
        "nullDbIfClipperWereBeforeMaster": rounded(ro.null_db(master * soft_clip(dry + raised), output), 2),
        "nullDbIfClipperWereOnTheWetOnly": rounded(ro.null_db(master * dry + soft_clip(master * raised), output), 2),
    }


def measure_output_clipper() -> dict:
    stimulus = clip_stimulus()
    masters = {"0": ro.BASELINE["master"], "-12": ro.normalised("master", -12.0), "+6": 1.0}
    captures = capture_all({name: (stimulus, {"mix": 0.0, "master": value}, {}) for name, value in masters.items()})
    aligned = {name: wide(capture)[48:48 + len(stimulus)] for name, capture in captures.items()}
    ramp_in = stimulus[4800:44801, 0].astype(np.float64)
    ramp_out = aligned["0"][4800:44801, 0]
    passed = ramp_out == ramp_in
    fitted = least_squares(lambda p: soft_clip(ramp_in, p[0], p[1]) - ramp_out, [2.4, 4.0])
    segments = {"rampBothChannels": slice(4800, 44801), "noisePeak6": slice(60000, 84000), "sines100HzAnd3kHz": slice(90000, 114000)}
    return {
        "law": "y = sign(v) * (|v| if |v| <= T; |v| - (|v| - T)^2 / (4 (C - T)) if T < |v| < 2 C - T; C otherwise), per sample and channel, "
               "applied to the final output after Mix and Master",
        "thresholdRound": rounded(CLIP_THRESHOLD, 7), "ceilingRound": rounded(CLIP_CEILING, 7),
        "thresholdRoundDbfs": 8.0, "ceilingRoundDbfs": 12.0,
        "everyRampValueBitExactBelow": rounded(float(np.abs(ramp_in[~passed]).min()), 6),
        "capturedCeiling": rounded(float(np.abs(aligned["0"]).max()), 7),
        "thresholdFitted": rounded(fitted.x[0], 6), "ceilingFitted": rounded(fitted.x[1], 6),
        "thresholdFittedDbfs": rounded(db(fitted.x[0]), 4), "ceilingFittedDbfs": rounded(db(fitted.x[1]), 4),
        "roundValueNullDb": {name: rounded(ro.null_db(soft_clip(stimulus[segment].astype(np.float64)), aligned["0"][segment]), 2)
                             for name, segment in segments.items()},
        "roundValueNullDbWithMaster": {name: rounded(ro.null_db(soft_clip(decibel_gain("master", single(masters[name])) * stimulus.astype(np.float64)),
                                                               aligned[name]), 2) for name in ("-12", "+6")},
        "linearNullDb": {name: rounded(ro.null_db(stimulus[segment].astype(np.float64), aligned["0"][segment]), 2) for name, segment in segments.items()},
        "position": clipper_position(),
    }


DUCK_SECONDS = 6.5
DUCK_EVENT_SECONDS = 2.4          # the loud event starts here, 0.4 s after the carrier input has ended
DUCK_LOW_DB = (-8, -7, -6, -5.5, -5, -4.5, -4, -3.5, -3, -2, -1, 0, 1, 2, 3, 4)
DUCK_HIGH_DB = (5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 21, 24, 27, 30, 33, 36)
DUCK_SETTINGS = {"predelay": 1.0}  # 2 s of pre-delay keep the event's own wet out of the analysis window


def duck_carrier(sample_rate: int = FS) -> np.ndarray:
    """2 s of quiet noise. With 2 s of pre-delay its wet fills the 2 s that follow, while the input is free for a loud event."""
    generator = np.random.default_rng(5)
    stimulus = np.zeros((int(DUCK_SECONDS * sample_rate), 2), np.float32)
    stimulus[: 2 * sample_rate] = generator.uniform(-0.05, 0.05, (2 * sample_rate, 2)).astype(np.float32)
    return stimulus


def duck_event(kind: str, amplitude: float, channels=(1.0, 1.0), frequency: float = 1000.0, sample_rate: int = FS) -> np.ndarray:
    """0.6 s of a loud sine, square, DC or a single impulse, starting at 2.4 s."""
    length = int(0.6 * sample_rate)
    time = np.arange(length) / sample_rate
    if kind == "sine":
        wave = amplitude * np.sin(2 * np.pi * frequency * time)
    elif kind == "dc":
        wave = amplitude * np.ones(length)
    elif kind == "impulse":
        wave = np.zeros(length)
        wave[0] = amplitude
    else:
        wave = amplitude * np.sign(np.sin(2 * np.pi * frequency * time) + 1e-12)
    event = np.zeros((int(DUCK_SECONDS * sample_rate), 2), np.float32)
    start = int(DUCK_EVENT_SECONDS * sample_rate)
    for channel in (0, 1):
        event[start:start + length, channel] = (channels[channel] * wave).astype(np.float32)
    return event


def duck_staircase(levels_db) -> np.ndarray:
    """DC on both channels in 100 ms steps of rising level."""
    event = np.zeros((int(DUCK_SECONDS * FS), 2), np.float32)
    start, step = int(DUCK_EVENT_SECONDS * FS), int(0.1 * FS)
    for index, level in enumerate(levels_db):
        event[start + index * step:start + (index + 1) * step] = np.float32(10 ** (level / 20))
    return event


def duck_events() -> dict:
    events = {f"sine 1 kHz peak {amplitude}": duck_event("sine", amplitude) for amplitude in (0.5, 0.71, 0.89, 1.0, 1.12, 1.41, 2.0, 2.83, 4.0, 8.0)}
    events.update({f"DC {amplitude}": duck_event("dc", amplitude) for amplitude in (0.71, 1.0, 1.41, 2.0)})
    events["square 1 kHz peak 1.41"] = duck_event("square", 1.41)
    events["sine 1 kHz peak 2, L only"] = duck_event("sine", 2.0, (1.0, 0.0))
    events["sine 1 kHz peak 2, R only"] = duck_event("sine", 2.0, (0.0, 1.0))
    events["sine 1 kHz peak 2, L = -R"] = duck_event("sine", 2.0, (1.0, -1.0))
    events["sine 50 Hz peak 2"] = duck_event("sine", 2.0, frequency=50.0)
    events["sine 10 kHz peak 2"] = duck_event("sine", 2.0, frequency=10000.0)
    events["impulse 8"] = duck_event("impulse", 8.0)
    events["impulse 32"] = duck_event("impulse", 32.0)
    events["DC staircase -8 to +4 dBFS"] = duck_staircase(DUCK_LOW_DB)
    events["DC staircase +5 to +36 dBFS"] = duck_staircase(DUCK_HIGH_DB)
    return events


def window_gain_db(output: np.ndarray, reference: np.ndarray, start: int, stop: int) -> float:
    """Gain of the output relative to the carrier-only capture over a window, common to both channels."""
    return db(float(np.sum(output[start:stop] * reference[start:stop]) / np.sum(reference[start:stop] ** 2)))


def duck_static_curve(captures: dict, reference: np.ndarray, window: slice) -> dict:
    start, step = int(DUCK_EVENT_SECONDS * FS) + 48, int(0.1 * FS)
    levels, gains = [], []
    for name, ladder in (("DC staircase -8 to +4 dBFS", DUCK_LOW_DB), ("DC staircase +5 to +36 dBFS", DUCK_HIGH_DB)):
        output = wide(captures[name])
        for index, level in enumerate(ladder):
            stop = start + (index + 1) * step
            levels.append(float(level))
            gains.append(window_gain_db(output, reference, stop - 960, stop))
    levels, gains = np.array(levels), np.array(gains)
    free = least_squares(lambda p: ducking_curve_db(levels, p[0], p[1], p[2]) - gains, [0.7, 10.0, 0.0])
    knee = least_squares(lambda p: ducking_curve_db(levels, DUCK_SLOPE, p[0], 0.0) - gains, [10.0])
    quiet = wide(captures["sine 1 kHz peak 0.5"])[window]
    return {
        "levelDbfs": rounded(levels, 2), "gainDb": rounded(gains, 5),
        "freeFit": {"slope": rounded(free.x[0], 7), "kneeDb": rounded(free.x[1], 5), "thresholdDbfs": rounded(free.x[2], 6),
                    "largestErrorDb": rounded(float(np.abs(free.fun).max()), 6)},
        "kneeDbWithSlope5Over7AndThreshold0": rounded(knee.x[0], 5),
        "largestErrorDbWithFittedKnee": rounded(float(np.abs(ducking_curve_db(levels) - gains).max()), 6),
        "largestErrorDbWithKnee10": rounded(float(np.abs(ducking_curve_db(levels, knee_db=10.0) - gains).max()), 6),
        "eventOfPeak0p5LeavesTheWetBitIdentical": bool(np.array_equal(quiet, reference[window])),
    }


def duck_onset(output: np.ndarray, reference: np.ndarray) -> dict:
    """Sample-by-sample gain around the start of a DC event of 2.0: when the reduction begins and its first steps."""
    start = int(DUCK_EVENT_SECONDS * FS) + 48
    gains = []
    for index in range(start - 3, start + 6):
        channel = int(np.argmax(np.abs(reference[index])))
        gains.append(db(output[index, channel] / reference[index, channel]))
    target = float(ducking_curve_db(db(2.0)))
    coefficient = gains[3] / target
    return {
        "gainDbFromThreeSamplesBeforeTheEventReachesTheOutput": rounded(gains, 6),
        "firstStepOverTarget": rounded(coefficient, 7),
        "attackMsFromFirstStep": rounded(-1000.0 / (FS * math.log(1.0 - coefficient)), 4),
    }


def duck_release_ms(output: np.ndarray, reference: np.ndarray) -> float:
    """Time constant of the recovery in dB after a DC event of 2.0 has ended."""
    end = int((DUCK_EVENT_SECONDS + 0.6) * FS) + 48
    early = window_gain_db(output, reference, end + 4800, end + 4848)
    late = window_gain_db(output, reference, end + 28800, end + 28848)
    return 500.0 / math.log(early / late)


def duck_other_rates() -> dict:
    result = {}
    for sample_rate in (44100, 96000):
        carrier = duck_carrier(sample_rate)
        event = duck_event("dc", 2.0, sample_rate=sample_rate)
        options = {"sample_rate": sample_rate}
        captures = capture_all({"carrier": (carrier, DUCK_SETTINGS, options), "event": (carrier + event, DUCK_SETTINGS, options)})
        reference, output = wide(captures["carrier"]), wide(captures["event"])
        window = slice(int(2.35 * sample_rate), int(4.25 * sample_rate))
        gain = 10.0 ** (ducking_gain_db(event, sample_rate, captures["event"].latency, len(output)) / 20.0)
        result[str(sample_rate)] = {"modelNullDb": rounded(ro.null_db((gain[:, None] * reference)[window], output[window]), 2),
                                    "nullDbWithoutReduction": rounded(ro.null_db(reference[window], output[window]), 2)}
    return result


def duck_attribution() -> dict:
    """The same DC staircase at four positions of the Ducking control: the threshold moves with it."""
    levels = list(np.arange(-42, 6, 3.0))
    carrier, event = duck_carrier(), duck_staircase(levels)
    jobs = {}
    for ducking in (0.0, 0.25, 0.5, 1.0):
        jobs[("carrier", ducking)] = (carrier, {**DUCK_SETTINGS, "ducking": ducking}, {})
        jobs[("event", ducking)] = (carrier + event, {**DUCK_SETTINGS, "ducking": ducking}, {})
    captures = capture_all(jobs)
    start, step = int(DUCK_EVENT_SECONDS * FS) + 48, int(0.1 * FS)
    table = {}
    for ducking in (0.0, 0.25, 0.5, 1.0):
        reference, output = wide(captures[("carrier", ducking)]), wide(captures[("event", ducking)])
        table[captures[("event", ducking)].meta["readback"]["id:13"]] = rounded(
            [window_gain_db(output, reference, start + (index + 1) * step - 960, start + (index + 1) * step) for index in range(len(levels))], 3)
    return {"dcLevelDbfs": levels, "steadyGainDbByDisplayedDuckingPercent": table}


def measure_ducking() -> dict:
    carrier = duck_carrier()
    events = duck_events()
    jobs = {name: (carrier + event, DUCK_SETTINGS, {}) for name, event in events.items()}
    jobs["carrier"] = (carrier, DUCK_SETTINGS, {})
    captures = capture_all(jobs)
    reference = wide(captures["carrier"])
    latency = captures["carrier"].latency
    window = slice(int(2.35 * FS), int(4.25 * FS))
    nulls = {}
    for name, event in events.items():
        output = wide(captures[name])
        entry = {"nullDbWithoutReduction": rounded(ro.null_db(reference[window], output[window]), 2)}
        for label, knee in (("modelNullDb", DUCK_KNEE_DB), ("modelNullDbWithKnee10", 10.0)):
            gain = 10.0 ** (ducking_gain_db(event, FS, latency, len(output), knee) / 20.0)
            entry[label] = rounded(ro.null_db((gain[:, None] * reference)[window], output[window]), 2)
        nulls[name] = entry
    step = wide(captures["DC 2.0"])
    active = [entry["modelNullDb"] for name, entry in nulls.items() if name != "sine 1 kHz peak 0.5"]
    return {
        "law": "wet gain dB g[n + latency] = g[n - 1 + latency] + c (G(20 log10 max(|xL[n]|, |xR[n]|)) - g[n - 1 + latency]), "
               "c = 1 - exp(-1 / (tau fs)), tau = 5 ms while the reduction grows and 300 ms while it recovers; "
               "G(L) = 0 below -W/2, -(5/7) (L + W/2)^2 / (2 W) inside the knee, -(5/7) L above +W/2; both wet channels share g",
        "kneeDb": DUCK_KNEE_DB, "slope": rounded(DUCK_SLOPE, 7), "ratio": 3.5, "thresholdDbfs": 0.0,
        "firstInputPeakThatReducesTheWet": rounded(10.0 ** (-DUCK_KNEE_DB / 40.0), 5),
        "attackMs": DUCK_ATTACK_MS, "releaseMs": DUCK_RELEASE_MS,
        "staticCurve": duck_static_curve(captures, reference, window),
        "onset": duck_onset(step, reference),
        "releaseMsMeasured": rounded(duck_release_ms(step, reference), 2),
        "worstModelNullDb": max(active), "events": nulls,
        "otherSampleRates": duck_other_rates(),
        "duckingControl": duck_attribution(),
    }


# ---------------------------------------------------------------------------
# The listening point: Macro (Tide) 100 %, Mix 100 %
# ---------------------------------------------------------------------------

MACRO = {"macro": 1.0}
DRY_START, DRY_LENGTH = int(0.1 * FS), 1000       # a burst the wet has not answered yet when it ends
NOISE_START, NOISE_STOP = FS, 19 * FS
REFERENCE_REALISATIONS = 8
MACRO_PREDELAY = (100.5, 480.5, 2400.5, 4800.5, 9600.5, 14400.5)


def macro_noise() -> np.ndarray:
    """A 1000-sample burst (for the dry coefficient), then 18 s of independent stereo noise of +-0.25."""
    generator = np.random.default_rng(7)
    stimulus = np.zeros((20 * FS, 2), np.float32)
    stimulus[DRY_START:DRY_START + DRY_LENGTH] = generator.uniform(-0.25, 0.25, (DRY_LENGTH, 2)).astype(np.float32)
    stimulus[NOISE_START:NOISE_STOP] = generator.uniform(-0.25, 0.25, (NOISE_STOP - NOISE_START, 2)).astype(np.float32)
    return stimulus


def onset_probe() -> np.ndarray:
    """The 1000-sample burst alone."""
    generator = np.random.default_rng(7)
    stimulus = np.zeros((int(3.5 * FS), 2), np.float32)
    stimulus[DRY_START:DRY_START + DRY_LENGTH] = generator.uniform(-0.25, 0.25, (DRY_LENGTH, 2)).astype(np.float32)
    return stimulus


def macro_jobs() -> dict:
    """(control, value, realisation) -> settings; the reference state is Macro 100 %, Mix 100 %."""
    jobs = {("reference", None, index): {} for index in range(REFERENCE_REALISATIONS)}
    jobs[("mix", 0.0, 0)] = {"mix": 0.0}
    for index in (0, 1):
        for mix in (0.25, 0.5, 0.75):
            jobs[("mix", mix, index)] = {"mix": mix}
        for value in (-12.0, 12.0):
            jobs[("return", value, index)] = {"return": ro.normalised("return", value)}
        jobs[("master", -6.0, index)] = {"master": ro.normalised("master", -6.0)}
        for percent in (0.0, 150.0):
            jobs[("width", percent, index)] = {"width": ro.normalised("width", percent)}
    for index in range(4):
        jobs[("width", 50.0, index)] = {"width": ro.normalised("width", 50.0)}
    jobs[("master at mix 50", -6.0, 0)] = {"master": ro.normalised("master", -6.0), "mix": 0.5}
    return jobs


def wet_levels(output: np.ndarray, stimulus: np.ndarray, dry_gain: float) -> dict:
    """Levels in dB of the wet part over the noise, after removing the dry at its known gain."""
    wet = output.astype(np.float64).copy()
    wet[48:48 + len(stimulus)] -= dry_gain * stimulus[: len(wet) - 48]
    segment = wet[NOISE_START + 9600:NOISE_STOP + 48]
    return {"total": db(ro.rms(segment)), "mid": db(ro.rms((segment[:, 0] + segment[:, 1]) / 2.0)),
            "side": db(ro.rms((segment[:, 0] - segment[:, 1]) / 2.0))}


def macro_row(key: tuple, settings: dict, capture: ro.Capture, stimulus: np.ndarray, mean_mid: float, mean_balance: float) -> dict:
    """One capture at Macro 100 % against the laws: the dry exactly, the wet by its level."""
    resolved = ro.resolve({**MACRO, **settings})
    window = wide(capture)[DRY_START + 48:DRY_START + 48 + DRY_LENGTH]
    dry_input = stimulus[DRY_START:DRY_START + DRY_LENGTH].astype(np.float64)
    master = decibel_gain("master", resolved["master"])
    dry_law, wet_law = master * mix_gains(resolved["mix"])[0], master * mix_gains(resolved["mix"])[1] * decibel_gain("return", resolved["return"])
    mid_law, side_law = width_gains(resolved["width"])
    row = {
        "control": key[0], "value": key[1], "realisation": key[2],
        "dryGainLaw": rounded(dry_law, 8), "dryGainFitted": rounded(float(np.sum(window * dry_input) / np.sum(dry_input ** 2)), 8),
        "dryWindowLawNullDb": rounded(ro.null_db(dry_law * dry_input, window), 2) if dry_law else None,
        "dryWindowIsSilent": not bool(np.any(window)),
        "outputIsMono": bool(np.array_equal(capture.output[:, 0], capture.output[:, 1])),
    }
    if wet_law:
        levels = wet_levels(capture.output, stimulus, dry_law)
        row.update({"wetMidDbRelativeToReference": rounded(levels["mid"] - mean_mid, 3), "wetMidDbLaw": rounded(db(wet_law * mid_law), 3)})
        if side_law:
            row.update({"wetSideMinusMidDb": rounded(levels["side"] - levels["mid"] - mean_balance, 3),
                        "wetSideMinusMidDbLaw": rounded(db(side_law / mid_law), 3)})
    else:
        row["outputBitIdenticalToDelayedInput"] = bool(np.array_equal(capture.output, delayed(stimulus, len(capture.output), 48).astype(np.float32)))
    return row


def level_per_two_seconds(output: np.ndarray) -> list:
    return [rounded(db(ro.rms(output[start:start + 2 * FS].astype(np.float64))), 2) for start in range(NOISE_START + 9600, 18 * FS, 2 * FS)]


def macro_level_checks() -> dict:
    stimulus = macro_noise()
    jobs = macro_jobs()
    captures = capture_all({key: (stimulus, {**MACRO, **settings}, {"realisation": key[2]}) for key, settings in jobs.items()})
    references = [wet_levels(captures[("reference", None, index)].output, stimulus, 0.0) for index in range(REFERENCE_REALISATIONS)]
    totals = [levels["total"] for levels in references]
    balance = [levels["side"] - levels["mid"] for levels in references]
    mean_mid = float(np.mean([levels["mid"] for levels in references]))
    rows = [macro_row(key, settings, captures[key], stimulus, mean_mid, float(np.mean(balance)))
            for key, settings in jobs.items() if key[0] != "reference"]
    level_errors = [row["wetMidDbRelativeToReference"] - row["wetMidDbLaw"] for row in rows if "wetMidDbLaw" in row]
    balance_errors = [row["wetSideMinusMidDb"] - row["wetSideMinusMidDbLaw"] for row in rows if "wetSideMinusMidDbLaw" in row]
    return {
        "reference": {
            "realisations": REFERENCE_REALISATIONS, "wetLevelDb": rounded(totals, 3),
            "wetLevelSpreadDb": rounded(float(np.std(totals, ddof=1)), 3),
            "sideMinusMidDb": rounded(balance, 4), "sideMinusMidSpreadDb": rounded(float(np.std(balance, ddof=1)), 4),
            "wetLevelDbPerTwoSeconds": [level_per_two_seconds(captures[("reference", None, index)].output) for index in range(REFERENCE_REALISATIONS)],
            "wetLevelDbPerTwoSecondsAtMacro0": level_per_two_seconds(ro.capture(stimulus, {"decay": DECAY}).output),
            "nullDbBetweenTwoRealisations": rounded(ro.null_db(wide(captures[("reference", None, 1)]), wide(captures[("reference", None, 0)])), 2),
        },
        "summary": {
            "capturesWithWet": len(level_errors),
            "wetLevelMinusLawDbMean": rounded(float(np.mean(level_errors)), 3), "wetLevelMinusLawDbSpread": rounded(float(np.std(level_errors, ddof=1)), 3),
            "wetLevelMinusLawDbLargest": rounded(float(np.max(np.abs(level_errors))), 3),
            "sideMinusMidMinusLawDbLargest": rounded(float(np.max(np.abs(balance_errors))), 3),
            "dryWindowWorstLawNullDb": max(row["dryWindowLawNullDb"] for row in rows if row["dryWindowLawNullDb"] is not None),
            "width0OutputIsMonoInAll": all(row["outputIsMono"] for row in rows if row["control"] == "width" and row["value"] == 0.0),
        },
        "checks": rows,
    }


def first_wet_sample(output: np.ndarray):
    """Samples from the start of the burst to the first non-zero output sample."""
    found = np.flatnonzero(np.abs(output[DRY_START:]).max(axis=1) > 0)
    return int(found[0]) if len(found) else None


def macro_predelay_checks() -> list:
    """The first wet sample is the same in every realisation, so it can stand in for a null:
    Pre-delay P against Pre-delay 0 with the stimulus moved by D - 1, D and D + 1 samples."""
    stimulus = onset_probe()
    jobs = {}
    for target in MACRO_PREDELAY:
        delay = math.floor(target) - 1
        for index in (0, 1):
            jobs[("predelay", target, index)] = (stimulus, {**MACRO, "predelay": predelay_setting(target)}, {"realisation": index})
        for candidate in (delay - 1, delay, delay + 1):
            jobs[("shift", candidate)] = (shifted(stimulus, candidate), dict(MACRO), {})
    captures = capture_all(jobs)
    rows = []
    for target in MACRO_PREDELAY:
        delay = predelay_samples(predelay_setting(target), FS)
        with_predelay = [first_wet_sample(captures[("predelay", target, index)].output) for index in (0, 1)]
        by_shift = {candidate: first_wet_sample(captures[("shift", candidate)].output) for candidate in (delay - 1, delay, delay + 1)}
        rows.append({"targetSamples": target, "lawDelay": delay, "firstWetSampleWithPredelay": with_predelay,
                     "firstWetSampleAtPredelay0ByInputShift": {str(candidate): value for candidate, value in by_shift.items()},
                     "matchingInputShift": [candidate for candidate, value in by_shift.items() if value == with_predelay[0]]})
    return rows


def first_wet_sample_by_macro() -> dict:
    """Where the wet starts at other Macro values, and why that sample is no delay meter by itself."""
    stimulus = onset_probe()
    jobs = {percent: (stimulus, {"macro": percent / 100.0, "predelay": 0.0}, {}) for percent in (0, 25, 50, 75, 100)}
    jobs["predelay"] = (stimulus, {"predelay": predelay_setting(4800.5)}, {})
    captures = capture_all(jobs)
    return {
        "atPredelay0ByMacroPercent": {str(percent): first_wet_sample(captures[percent].output) for percent in (0, 25, 50, 75, 100)},
        "atMacro0WithPredelayOf4799Samples": first_wet_sample(captures["predelay"].output),
    }


def measure_macro100() -> dict:
    return {"state": "Macro 100 %, Mix 100 % unless a row says otherwise, Decay 0.5 s",
            "levels": macro_level_checks(), "predelay": macro_predelay_checks(), "firstWetSample": first_wet_sample_by_macro()}


def main() -> None:
    ro.identity()
    result = {
        "schema": 1,
        "reference": "Arturia Rev OCEAN 1.0.0.5848, Tide, neutral state, Decay 0.5 s, 48 kHz unless stated",
        "score": "20 log10(rms(prediction - capture) / rms(capture)), nothing fitted",
        "mix": measure_mix(),
        **measure_gains(),
        "width": measure_width(),
        "predelay": measure_predelay(),
        "sampleRates": measure_sample_rates(),
        "neutralPath": measure_neutral_path(),
        "outputClipper": measure_output_clipper(),
        "ducking": measure_ducking(),
        "macro100": measure_macro100(),
    }
    DATA.parent.mkdir(parents=True, exist_ok=True)
    DATA.write_text(json.dumps(result, indent=1) + "\n")
    print(f"wrote {DATA}")
    for name in ("mix", "return", "master", "width"):
        print(f"{name:8} worst law null {result[name]['worstLawNullDb']:8.2f} dB")
    print(f"predelay law holds in the main set: {result['predelay']['mainSet']['lawHoldsInAll']}, "
          f"at the other rates: {result['predelay']['otherSampleRates']['lawHoldsInAll']}")
    print(f"clipper  worst round-value null {max(result['outputClipper']['roundValueNullDb'].values()):8.2f} dB")
    print(f"ducking  worst model null {result['ducking']['worstModelNullDb']:8.2f} dB")


if __name__ == "__main__":
    main()
