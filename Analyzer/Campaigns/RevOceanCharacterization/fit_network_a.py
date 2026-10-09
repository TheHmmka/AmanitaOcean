#!/usr/bin/env python3
"""Fit and check the constants of the network_a model (network_a.py, network_a.c).

Run from the campaign folder:

    python fit_network_a.py        # about 2 minutes once the captures are cached

It writes tide_structural_data/network_a.json: the constants `network_a.render`
uses, and the evidence quoted in findings/network_a.md. Nothing here reads a
holdout; every number comes from captures of this packet:

* two 100 s trains of impulses 1.0 to 1.25 s apart, left and right input,
  Decay 0.5 s, 44.1 kHz host (88 impulses each; the first 16 are fitted on,
  impulses 41 to 60 are kept aside as a check);
* one 24 s stimulus of eight impulses at 18 Decay values and 8 Sizes (44.1 kHz);
* `datasets.network_programme` with seed 777 at eight settings the holdout does
  not use (48 and 44.1 kHz);
* two impulses at Decay 20 s followed for 100 s (44.1 and 48 kHz).

Method. An impulse response is cut from a train, multiplied by the inverse of
the Decay envelope so that every pass weighs the same, and compared with the
recursion simulated from the impulse. Input gains and output weights enter the
output linearly and are solved exactly; the corner frequencies of the two
filters are refined by Gauss-Newton with finite differences. A null is
20 log10(rms(model - reference) / rms(reference)), nothing fitted.
"""
from __future__ import annotations

import copy
import json
import math
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from scipy.signal import lfilter

import datasets
import network_a
import revocean

RATE = 44100
WARMUP_SECONDS = 10.0
WARMUP_FRAMES = 441000
TRAIN_SEED = 9101
SEGMENT = 40000                      # samples of a response that are compared (0.9 s)
FIRST = 1000                         # nothing arrives earlier
EDGES = (1000, 2000, 3000, 4500, 7000, 11000, 16000, 24000, 40000)
FIT_IMPULSES = slice(0, 16)
CHECK_IMPULSES = slice(40, 60)
DECAYS = (0.5, 0.6, 0.75, 1.0, 1.4, 2.0, 2.7, 3.5, 4.5, 5.5, 5.9, 6.0, 6.1, 7.0, 9.0, 14.0, 25.0, 45.0)
SIZES = (30.0, 45.0, 62.0, 80.0, 88.0, 120.0, 157.0, 200.0)
PROGRAMME_SEED = 777
PROGRAMME_CASES = ((48000, 0.8, 100.0, 10.0), (48000, 3.0, 100.0, 10.0), (48000, 9.0, 100.0, 10.0), (48000, 1.7, 70.0, 10.0),
                   (48000, 2.5, 140.0, 10.0), (44100, 1.5, 100.0, 10.0), (48000, 1.2, 95.0, 7.5), (44100, 4.0, 180.0, 3.3))
POOL = ThreadPoolExecutor(8)
FIRST_ORDER = json.loads((network_a.HERE / "tide_structural_data" / "first_order.json").read_text())


# ------------------------------------------------------------------ captures

def train_times(seed: int, seconds: float = 100.0) -> np.ndarray:
    generator = np.random.default_rng(seed)
    times = [int(0.3 * RATE) + int(generator.integers(0, RATE // 10))]
    while True:
        following = times[-1] + int(generator.integers(RATE, int(1.25 * RATE)))
        if following >= int((seconds - 1.25) * RATE):
            return np.array(times)
        times.append(following)


def capture(stimulus: np.ndarray, decay: float, size: float = 100.0, rate: int = RATE, warmup: float = WARMUP_SECONDS) -> np.ndarray:
    settings = {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}
    return revocean.capture(stimulus, settings, sample_rate=rate, warmup=warmup).output[:len(stimulus)].astype(np.float64)


def train(channel: int) -> tuple:
    times = train_times(TRAIN_SEED)
    stimulus = revocean.impulses(100.0, [(int(t), channel, 0.5) for t in times], RATE)
    return times, stimulus, capture(stimulus, 0.5)


def series_stimulus() -> np.ndarray:
    """Eight impulses, inputs alternating, then silence: the stimulus of the Decay and Size series."""
    generator = np.random.default_rng(9201)
    time_, events = int(0.3 * RATE) + int(generator.integers(0, 4000)), []
    for index in range(8):
        events.append((time_, index % 2, 0.5))
        time_ += int(generator.integers(RATE, int(1.25 * RATE)))
    return revocean.impulses(24.0, events, RATE)


# ------------------------------------------------------------------ constants

def starting_constants() -> dict:
    """Round starting values; lines, oscillators and converters are those of the first-pass model."""
    width = [0.272] * 6 + [1.0 - index * 0.728 / 9 for index in range(10)]
    constants = {
        "internal_rate_hz": RATE,
        "predelay_internal_samples": 44,
        "output_delay_internal_samples": 44,
        "decay_parameter": [0.5, 60.0, 4.4],
        "size_parameter": [30.0, 200.0, 0.71337],
        "oscillator": {"rate_hz": 0.6, "depth_seconds": 0.00088, "phase_step_rad": 11.25},
        "lines": {"prime": FIRST_ORDER["lines"]["prime"], "input_gain": [0.0] * 16, "width": width},
        "input_filter": {"low_shelves": [{"pole_hz": pole, "gain_db": sign * 0.83}
                                         for pole, sign in ((105.0, -1), (380.0, 1), (840.0, 1), (3600.0, -1))]},
        "loop": {"matrix_gain": 0.25, "flat_db": -0.06, "high_shelf": {"pole_hz": 19000.0, "gain_db": -0.06},
                 "low_shelves": [{"pole_hz": 4900.0, "gain_db": 0.06}, {"pole_hz": 1600.0, "gain_db": -0.14},
                                 {"pole_hz": 950.0, "gain_db": -0.14}]},
        "output_weights": {"line_1": [0.46, 0.228], "line_16": [0.336, 0.428], "line_16_shape": -2.6, "lower_shape": 4.0,
                           "upper_shape": 2.0, "decay_low_seconds": 0.5, "decay_knee_seconds": 6.0},
        "output_lowpass": {"frequency_hz": 20000.0, "q": 1.0},
        "converter": FIRST_ORDER["converter"],
    }
    # the first-pass model's coefficient is input gain x output weight x first tap of its fixed filter
    model = network_a.Model(constants)
    first_tap = model.lowpass_b[0] * model.input_b[0]
    coefficient = np.array(FIRST_ORDER["lines"]["coefficient"])
    constants["lines"]["input_gain"] = list(coefficient / (first_tap * (1.0 + np.array(width)) * model.output_weights(0.5)))
    return constants


def corners(constants: dict) -> np.ndarray:
    loop = constants["loop"]
    return np.array([part["pole_hz"] for part in constants["input_filter"]["low_shelves"]]
                    + [loop["high_shelf"]["pole_hz"]] + [part["pole_hz"] for part in loop["low_shelves"]])


def with_corners(constants: dict, values, input_gain_db: float | None = None) -> dict:
    """A copy with the eight pole frequencies (and the common gain of the input shelves) replaced."""
    result = copy.deepcopy(constants)
    for part, value in zip(result["input_filter"]["low_shelves"], values[:4]):
        part["pole_hz"] = float(value)
        if input_gain_db is not None:
            part["gain_db"] = math.copysign(input_gain_db, part["gain_db"])
    result["loop"]["high_shelf"]["pole_hz"] = float(values[4])
    for part, value in zip(result["loop"]["low_shelves"], values[5:8]):
        part["pole_hz"] = float(value)
    return result


class State:
    """Constants plus the free linear quantities of a fit: gains for the own and the other input, output weights."""

    def __init__(self, constants: dict, own=None, cross=None, weights=None):
        self.constants = constants
        self.model = network_a.Model(constants)
        self.own = self.model.own_gain if own is None else own
        self.cross = self.model.cross_gain if cross is None else cross
        self.weights = self.model.output_weights(0.5) if weights is None else weights

    def replace(self, **changes) -> "State":
        values = {"constants": self.constants, "own": self.own, "cross": self.cross, "weights": self.weights}
        values.update(changes)
        return State(**values)


# ------------------------------------------------------------------ impulse responses

def decibels(part: np.ndarray, whole: np.ndarray) -> float:
    return 10.0 * math.log10(max(float(np.sum(part ** 2)), 1e-300) / max(float(np.sum(whole ** 2)), 1e-300))


class Segments:
    """Impulse responses of one group cut from the two trains, with the Decay envelope removed."""

    def __init__(self, group: int, impulses: slice, trains: dict, length: int = SEGMENT):
        self.group, self.length = group, length
        self.weight = 10.0 ** (3.0 * np.arange(length) / (RATE * 0.5))
        self.items = []                                   # (own input?, line lengths, weighted reference)
        lengths = network_a.Model(starting_constants()).lengths(group, WARMUP_FRAMES, len(trains[0][1]))
        for channel in (0, 1):
            times, _, output = trains[channel]
            for time_ in times[impulses]:
                reference = output[time_ + 44:time_ + 44 + length, group] * self.weight
                self.items.append((channel == group, lengths[time_:time_ + length].copy(), reference))
        self.reference = np.concatenate([item[2] for item in self.items])
        self.impulse = np.zeros(length)
        self.impulse[0] = 0.5

    def excitation(self, state: State, item: tuple, drive: np.ndarray, **changes):
        model = state.model
        arguments = {"own_gain": state.own if item[0] else state.cross}
        arguments.update(changes)
        return model.group(item[1], drive, None, model.attenuation(self.group, 0.5, 1.0), state.weights, **arguments)

    def output(self, state: State, signals: np.ndarray) -> np.ndarray:
        """Low-pass and envelope weight; time runs along the last axis."""
        return lfilter(state.model.lowpass_b, state.model.lowpass_a, signals, axis=-1) * self.weight

    def model(self, state: State, drive=None, **changes) -> np.ndarray:
        drive = state.model.drive(self.impulse) if drive is None else drive
        return np.concatenate(list(POOL.map(lambda item: self.output(state, self.excitation(state, item, drive, **changes)), self.items)))


def model_of(sets: list, state: State, **arguments) -> np.ndarray:
    return np.concatenate([segments.model(state, **arguments) for segments in sets])


def reference_of(sets: list) -> np.ndarray:
    return np.concatenate([segments.reference for segments in sets])


def null_table(sets: list, modelled: np.ndarray) -> dict:
    """Null over the whole compared stretch and in the stretches between EDGES (samples after the impulse)."""
    reference = reference_of(sets).reshape(-1, SEGMENT)
    residual = reference - modelled.reshape(-1, SEGMENT)
    result = {"allDb": round(decibels(residual[:, FIRST:], reference[:, FIRST:]), 2)}
    for low, high in zip(EDGES[:-1], EDGES[1:]):
        result[f"{low}-{high}"] = round(decibels(residual[:, low:high], reference[:, low:high]), 2)
    return result


def nulls(sets: list, state: State, **arguments) -> dict:
    return null_table(sets, model_of(sets, state, **arguments))


def solve_weights(sets: list, state: State) -> State:
    """Least squares for the sixteen output weights; line 9 is then made the unit."""
    gram, right = np.zeros((16, 16)), np.zeros(16)
    for segments in sets:
        drive = state.model.drive(segments.impulse)
        def normal(item):
            taps = segments.output(state, segments.excitation(state, item, drive, want_taps=True)[1].T)[:, FIRST:]
            return taps @ taps.T, taps @ item[2][FIRST:]
        for part_gram, part_right in POOL.map(normal, segments.items):
            gram += part_gram
            right += part_right
    weights = np.linalg.solve(gram, right)
    scale = weights[8]
    return state.replace(weights=weights / scale, own=state.own * scale, cross=state.cross * scale)


def solve_gains(sets: list, state: State) -> State:
    """Least squares for the input gains: sixteen for the own input, sixteen for the other one."""
    normals = {True: [np.zeros((16, 16)), np.zeros(16)], False: [np.zeros((16, 16)), np.zeros(16)]}
    for segments in sets:
        drive = state.model.drive(segments.impulse)
        def normal(item):
            columns = np.stack([segments.output(state, segments.excitation(state, item, drive, own_gain=np.eye(16)[line]))[FIRST:]
                                for line in range(16)])
            return item[0], columns @ columns.T, columns @ item[2][FIRST:]
        for own, part_gram, part_right in POOL.map(normal, segments.items):
            normals[own][0] += part_gram
            normals[own][1] += part_right
    return state.replace(own=np.linalg.solve(*normals[True]), cross=np.linalg.solve(*normals[False]))


def gauss_newton(sets: list, evaluate, theta, step: float, iterations: int = 2) -> np.ndarray:
    """Least squares over theta for the model vector evaluate(theta). Finite-difference Jacobian."""
    mask = np.tile(np.arange(SEGMENT) >= FIRST, len(reference_of(sets)) // SEGMENT)
    reference = reference_of(sets)[mask]
    theta = np.array(theta, dtype=np.float64)
    for _ in range(iterations):
        base = evaluate(theta)[mask]
        jacobian = np.empty((len(theta), len(reference)))
        for index in range(len(theta)):
            moved = theta.copy()
            moved[index] += step * max(abs(theta[index]), 1.0)
            jacobian[index] = (evaluate(moved)[mask] - base) / (moved[index] - theta[index])
        scale = np.sqrt(np.sum(jacobian ** 2, axis=1))
        jacobian /= scale[:, None]
        theta = theta + np.linalg.solve(jacobian @ jacobian.T + 1e-10 * np.eye(len(theta)), jacobian @ (reference - base)) / scale
    return theta


def refine(sets: list, state: State, rounds: int, free_input_gain: bool) -> State:
    """Alternate: corner frequencies (Gauss-Newton), output weights and input gains (exact)."""
    for _ in range(rounds):
        theta = corners(state.constants)
        if free_input_gain:
            theta = np.append(theta, abs(state.constants["input_filter"]["low_shelves"][0]["gain_db"]))
        moved = lambda values: state.replace(constants=with_corners(state.constants, values[:8], values[8] if free_input_gain else None))
        state = moved(gauss_newton(sets, lambda values: model_of(sets, moved(values)), theta, 1e-6))
        state = solve_gains(sets, solve_weights(sets, state))
    return state


# ------------------------------------------------------------------ the matrix, entry by entry

def matrix_entries(group: int, trains: dict, state: State, impulses: slice = slice(0, 30), length: int = 16000) -> dict:
    """Gain of every second pass (line i read again by line j) relative to the model's, by linear least squares.

    The reference minus everything the model has, plus the model's second passes, is regressed on the 256
    second-pass waveforms of the group (first pass of line i, loop filter, read by line j, output weight j).
    A coefficient of 1 means the entry [j][i] of the matrix has the model's sign and magnitude.
    """
    segments = Segments(group, impulses, trains, length)
    model, signs = state.model, network_a.feedback_signs()
    attenuation = model.attenuation(group, 0.5, 1.0)
    drive = model.drive(segments.impulse)
    nothing = np.zeros((16, 16))
    gram, right = np.zeros((256, 256)), np.zeros(256)

    def normal(item):
        paths = np.empty((16, 16, length))                                                 # [source][destination]
        for source in range(16):
            gain = np.zeros(16)
            gain[source] = (state.own if item[0] else state.cross)[source]
            taps = model.group(item[1], drive, None, attenuation, state.weights, own_gain=gain, matrix=nothing, want_taps=True)[1]
            fed = lfilter(model.loop_b, model.loop_a, taps[:, source])
            second = model.group(item[1], None, None, attenuation, state.weights, matrix=nothing,
                                 inject=np.outer(fed, model.matrix[:, source]), want_taps=True)[1]
            paths[source] = (second * state.weights).T
        paths = segments.output(state, paths.reshape(256, length))[:, FIRST:]
        full = segments.output(state, segments.excitation(state, item, drive))[FIRST:]
        target = item[2][FIRST:] - full + paths.sum(axis=0)
        return paths @ paths.T, paths @ target

    for part_gram, part_right in POOL.map(normal, segments.items):
        gram += part_gram
        right += part_right
    relative = np.linalg.solve(gram, right).reshape(16, 16)                                # [source][destination]
    return {"signMismatches": int(np.sum(relative < 0)), "largestDeviation": float(np.abs(relative - 1).max()),
            "rmsDeviation": float(np.sqrt(np.mean((relative - 1) ** 2))),
            "relativeGain": np.round(relative, 6).tolist(), "sign": (signs.T * np.sign(relative)).astype(int).tolist()}


def first_look(group: int, trains: dict, state: State, impulses: slice = slice(0, 30), length: int = 15500) -> dict:
    """The measurement that found the matrix: all 256 second passes regressed with no feedback in the model.

    Regressors are the sixteen first passes per input and the 256 second-pass waveforms with unit matrix
    entries; later passes are left in as clutter. Returns the signs of the fitted gains [source][destination].
    """
    segments = Segments(group, impulses, trains, length)
    model = state.model
    attenuation = model.attenuation(group, 0.5, 1.0)
    drive = model.drive(segments.impulse)
    nothing = np.zeros((16, 16))
    gram, right = np.zeros((288, 288)), np.zeros(288)

    def normal(item):
        rows = np.zeros((288, length))
        for source in range(16):
            gain = np.zeros(16)
            gain[source] = (state.own if item[0] else state.cross)[source]
            taps = model.group(item[1], drive, None, attenuation, state.weights, own_gain=gain, matrix=nothing, want_taps=True)[1]
            rows[source + (0 if item[0] else 16)] = taps[:, source] * state.weights[source]
            fed = lfilter(model.loop_b, model.loop_a, taps[:, source])
            second = model.group(item[1], None, None, attenuation, state.weights, matrix=nothing,
                                 inject=np.outer(fed, np.full(16, 0.25)), want_taps=True)[1]
            rows[32 + 16 * source:48 + 16 * source] = second.T
        rows = segments.output(state, rows)[:, FIRST:]
        return rows @ rows.T, rows @ item[2][FIRST:]

    for part_gram, part_right in POOL.map(normal, segments.items):
        gram += part_gram
        right += part_right
    solution = np.linalg.solve(gram + 1e-12 * np.trace(gram) / 288 * np.eye(288), right)
    gains = solution[32:].reshape(16, 16)                                                  # entry x weight_j
    rule = network_a.feedback_signs().T
    return {"signsAgreeWithRule": int(np.sum(np.sign(gains) == rule)), "of": 256,
            "diagonalOverWeight": np.round(np.diag(gains) / state.weights, 4).tolist()}


# ------------------------------------------------------------------ checks

def ablations(sets: list, state: State) -> dict:
    """One element changed at a time (no refit); nulls on the impulses kept aside."""
    model = state.model
    result = {"model": nulls(sets, state)}
    impulse = sets[0].impulse
    # loop filter in front of the output tap; the input filter is divided by it so the first pass keeps its transfer
    drive = model.drive(lfilter(model.loop_a, model.loop_b, impulse))
    result["loop filter in front of the output tap"] = nulls(sets, state, drive=drive, output_after_loop=True)
    flat = 10.0 ** (state.constants["loop"]["flat_db"] / 20.0)
    result["no loop shelves (flat -0.06 dB only)"] = nulls(sets, state, loop=(np.array([flat]), np.ones(1)))
    result["loop flat gain 0 dB instead of -0.06 dB"] = nulls(sets, state, loop=(model.loop_b / flat, model.loop_a))
    plain = 0.25 * np.array([[(-1.0) ** bin(a & b).count("1") for a in range(16)] for b in range(16)])
    result["Hadamard matrix without the reversed outputs"] = nulls(sets, state, matrix=plain)
    flipped = model.matrix.copy()
    flipped[15, 15] *= -1
    result["sign of one matrix entry (16 -> 16) flipped"] = nulls(sets, state, matrix=flipped)
    result["output weights all 1"] = nulls(sets, state.replace(weights=np.ones(16)))
    behind = []
    for segments in sets:                                # no input filter in front; it is applied to the output instead
        rows = segments.model(state, drive=np.concatenate([np.zeros(44), impulse[:-44]])).reshape(-1, SEGMENT) / segments.weight
        behind.append((lfilter(model.input_b, model.input_a, rows, axis=1) * segments.weight).ravel())
    result["input filter behind the lines (the first-pass model's place)"] = null_table(sets, np.concatenate(behind))
    return result


def per_line_loop_gain(sets: list, state: State) -> list:
    """Sixteen free gains on the feedback of single lines (columns of the matrix): fitted values."""
    evaluate = lambda gains: model_of(sets, state, matrix=state.model.matrix * gains[None, :])
    return [round(float(value), 7) for value in gauss_newton(sets, evaluate, np.ones(16), 1e-4, iterations=1)]


def whole_null(model_output: np.ndarray, reference: np.ndarray, parts: int = 3) -> dict:
    frames = len(reference)
    result = {"allDb": round(decibels(model_output - reference, reference), 2)}
    for index in range(parts):
        window = slice(index * frames // parts, (index + 1) * frames // parts)
        if np.sum(reference[window] ** 2) > 1e-20:
            result[f"third{index + 1}Db"] = round(decibels(model_output[window] - reference[window], reference[window]), 2)
    return result


def decay_series(model: network_a.Model) -> dict:
    """Free output weights at every Decay against the law, and the null of the model as it stands."""
    stimulus = series_stimulus()
    outputs = list(ThreadPoolExecutor(3).map(lambda decay: capture(stimulus, decay), DECAYS))
    frames = len(stimulus)
    driven = model.drive(stimulus.astype(np.float64))
    result = {}
    for decay, reference in zip(DECAYS, outputs):
        fitted = []
        for group in (0, 1):
            lengths = model.lengths(group, WARMUP_FRAMES, frames)
            taps = model.group(lengths, driven[:, group], driven[:, 1 - group], model.attenuation(group, decay, 1.0),
                               np.ones(16), want_taps=True)[1]
            taps = np.concatenate([np.zeros((44, 16)), model.lowpass(taps)[:frames - 44]])
            fitted.append(np.linalg.lstsq(taps, reference[:, group], rcond=None)[0])
        fitted = np.mean(fitted, axis=0)
        law = model.output_weights(decay)
        rendered = model.render(stimulus, RATE, WARMUP_SECONDS, decay, 100.0)
        result[f"{decay:g}"] = {"line9": round(float(fitted[8]), 6),
                                "largestDeviationFromLaw": float(np.abs(fitted / fitted[8] / law - 1).max()),
                                "weightsOverLine9": np.round(fitted / fitted[8], 6).tolist(), **whole_null(rendered, reference)}
    return result


def size_series(model: network_a.Model) -> dict:
    stimulus = series_stimulus()
    outputs = list(ThreadPoolExecutor(3).map(lambda size: capture(stimulus, 1.0, size), SIZES))
    return {f"{size:g}": whole_null(model.render(stimulus, RATE, WARMUP_SECONDS, 1.0, size), reference)
            for size, reference in zip(SIZES, outputs)}


def programme_checks(model: network_a.Model) -> list:
    """The scorer's kind of programme with another seed and other settings."""
    def one(case):
        rate, decay, size, warmup = case
        stimulus = datasets.network_programme(rate, seed=PROGRAMME_SEED)
        return stimulus, capture(stimulus, decay, size, rate, warmup)
    result = []
    for case, (stimulus, reference) in zip(PROGRAMME_CASES, ThreadPoolExecutor(3).map(one, PROGRAMME_CASES)):
        rate, decay, size, warmup = case
        rendered = model.render(stimulus, rate, warmup, decay, size)
        entry = {"sampleRate": rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup,
                 "overallDb": round(decibels(rendered - reference, reference), 2)}
        for name, start, stop in (("0.25-1.4s", 0.25, 1.4), ("1.4-4s", 1.4, 4.0), ("4-6s", 4.0, 6.0), ("6-14s", 6.0, 14.0)):
            window = slice(int(start * rate), int(stop * rate))
            entry[name] = round(decibels(rendered[window] - reference[window], reference[window]), 2)
        result.append(entry)
    return result


def train_checks(model: network_a.Model, trains: dict) -> dict:
    """Whole 100 s trains, and the null of every single response of them (does anything drift?)."""
    result = {}
    for channel in (0, 1):
        times, stimulus, reference = trains[channel]
        rendered = model.render(stimulus, RATE, WARMUP_SECONDS, 0.5, 100.0)
        each = np.array([decibels(rendered[t + 1000:t + 30000] - reference[t + 1000:t + 30000], reference[t + 1000:t + 30000]) for t in times])
        result[f"input{channel}"] = {"allDb": round(decibels(rendered - reference, reference), 2),
                                     "responseBestDb": round(float(each.min()), 2), "responseMedianDb": round(float(np.median(each)), 2),
                                     "responseWorstDb": round(float(each.max()), 2),
                                     "byTenSecondsDb": [round(float(10 * np.log10(np.mean(10 ** (each[(times >= a * RATE) & (times < (a + 10) * RATE)] / 10)))), 2)
                                                        for a in range(0, 100, 10)]}
    return result


def late_tail(constants: dict) -> dict:
    """Two impulses, Decay 20 s, 100 s: the null and the least-squares gain of the model along the tail.

    The second model has the flat loop gain lowered from -0.06 to -0.060003 dB, which removes the slow drift.
    """
    bins = ((0, 2), (2, 5), (5, 10)) + tuple((start, start + 10) for start in range(10, 100, 10))
    result = {}
    for rate in (44100, 48000):
        stimulus = revocean.impulses(100.0, [(int(0.3 * rate), 0, 0.5), (int(1.4 * rate), 1, 0.5)], rate)
        reference = capture(stimulus, 20.0, rate=rate)
        entry = {"referenceLevelDb": {f"{a}-{b}s": round(10 * math.log10(float(np.mean(reference[a * rate:b * rate] ** 2))), 1) for a, b in bins}}
        for label, flat in (("model", constants["loop"]["flat_db"]), ("flat -0.060003 dB", -0.060003)):
            changed = copy.deepcopy(constants)
            changed["loop"]["flat_db"] = flat
            rendered = network_a.Model(changed).render(stimulus, rate, WARMUP_SECONDS, 20.0, 100.0)
            entry[label] = {
                "overallDb": round(decibels(rendered - reference, reference), 2),
                "nullDb": {f"{a}-{b}s": round(decibels((rendered - reference)[a * rate:b * rate], reference[a * rate:b * rate]), 2) for a, b in bins},
                "gainMinusOne": {f"{a}-{b}s": float(np.sum(rendered[a * rate:b * rate] * reference[a * rate:b * rate])
                                                    / np.sum(rendered[a * rate:b * rate] ** 2) - 1.0) for a, b in bins}}
        result[str(rate)] = entry
    return result


def input_gain_law(beta: np.ndarray) -> dict:
    """One S-curve from line 1 to line 16 fitted to the sixteen input gains: anchors and shape."""
    position = np.arange(16) / 15.0
    best = None
    for shape in np.arange(3.30, 3.42, 0.0005):
        curve = network_a.s_curve(position, shape)
        design = np.stack([1.0 - curve, curve], axis=1) / beta[:, None]
        anchors = np.linalg.lstsq(design, np.ones(16), rcond=None)[0]
        error = float(np.abs(design @ anchors - 1.0).max())
        if best is None or error < best["largestRelativeDeviation"]:
            best = {"shape": round(float(shape), 4), "line1": float(anchors[0]), "line16": float(anchors[1]), "largestRelativeDeviation": error}
    curve = network_a.s_curve(position, 3.36)
    design = np.stack([1.0 - curve, curve], axis=1) / beta[:, None]
    anchors = np.linalg.lstsq(design, np.ones(16), rcond=None)[0]
    best["withShape3.36"] = {"line1": float(anchors[0]), "line16": float(anchors[1]),
                             "largestRelativeDeviation": float(np.abs(design @ anchors - 1.0).max())}
    return best


# ------------------------------------------------------------------ main

def main() -> None:
    started = time.time()
    revocean.identity()
    trains = {channel: train(channel) for channel in (0, 1)}
    constants = starting_constants()
    fit = [Segments(group, FIT_IMPULSES, trains) for group in (0, 1)]
    check = [Segments(group, CHECK_IMPULSES, trains) for group in (0, 1)]
    evidence = {"start": nulls(fit, State(constants))}

    # 1. the measurement that found the matrix, with the starting constants
    start = solve_gains(fit, solve_weights(fit, State(constants)))
    evidence["firstLook"] = {f"group{group}": first_look(group, trains, start) for group in (0, 1)}
    print("first look at the matrix", evidence["firstLook"], round(time.time() - started), flush=True)

    # 2. constants: the common gain of the input shelves free, then fixed at 0.835 dB
    free = refine(fit, start, rounds=6, free_input_gain=True)
    evidence["inputShelfGainFreeDb"] = abs(free.constants["input_filter"]["low_shelves"][0]["gain_db"])
    evidence["inputShelfGainFree"] = {"fit": nulls(fit, free), "check": nulls(check, free)}
    fixed = refine(fit, free.replace(constants=with_corners(free.constants, corners(free.constants), 0.835)), rounds=4, free_input_gain=False)
    evidence["roundValues"] = {"input shelves 0.835 dB": {"fit": nulls(fit, fixed), "check": nulls(check, fixed)}}
    for label, gain in (("input shelves 5/6 dB", 5.0 / 6.0), ("input shelves 0.83 dB", 0.83), ("input shelves 0.84 dB", 0.84)):
        other = refine(fit, fixed.replace(constants=with_corners(fixed.constants, corners(fixed.constants), gain)), rounds=3, free_input_gain=False)
        evidence["roundValues"][label] = {"fit": nulls(fit, other), "check": nulls(check, other)}
    print("constants fitted", round(time.time() - started), flush=True)

    # 3. the fitted linear quantities against their laws
    beta = 0.5 * (fixed.own + fixed.cross)
    width = (fixed.own - fixed.cross) / (fixed.own + fixed.cross)
    law_width = np.array(constants["lines"]["width"])
    evidence["fitted"] = {
        "outputWeights": fixed.weights.tolist(), "outputWeightLaw": fixed.model.output_weights(0.5).tolist(),
        "largestWeightDeviation": float(np.abs(fixed.weights / fixed.model.output_weights(0.5) - 1).max()),
        "width": width.tolist(), "largestWidthDeviation": float(np.abs(width - law_width).max()),
        "inputGainPlusMirror": (beta + beta[::-1]).tolist(), "inputGainLaw": input_gain_law(beta),
    }
    final = copy.deepcopy(fixed.constants)
    final["lines"]["input_gain"] = [float(value) for value in beta]
    state = State(final)
    evidence["withLaws"] = {"fit": nulls(fit, state), "check": nulls(check, state)}
    evidence["freeWeightsAndGains"] = {"fit": nulls(fit, fixed), "check": nulls(check, fixed)}

    # 4. checks of the structure
    evidence["matrixEntries"] = {f"group{group}": matrix_entries(group, trains, state) for group in (0, 1)}
    print("matrix entries", {key: (value["signMismatches"], value["largestDeviation"]) for key, value in evidence["matrixEntries"].items()},
          round(time.time() - started), flush=True)
    evidence["ablations"] = ablations(check, state)
    evidence["perLineLoopGain"] = per_line_loop_gain(fit, state)

    # 5. the model as the module renders it
    model = network_a.Model(final)
    evidence["trains"] = train_checks(model, trains)
    evidence["decaySeries"] = decay_series(model)
    evidence["sizeSeries"] = size_series(model)
    evidence["programme"] = programme_checks(model)
    evidence["lateTail"] = late_tail(final)
    final["evidence"] = evidence
    network_a.DATA.write_text(json.dumps(final, indent=1))
    print(json.dumps({key: evidence[key] for key in ("withLaws", "trains", "programme")}, indent=1))
    print(f"{time.time() - started:.0f} s; wrote {network_a.DATA}")


if __name__ == "__main__":
    main()
