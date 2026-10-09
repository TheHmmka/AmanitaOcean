#!/usr/bin/env python3
"""Fit and check the constants of network_b.py (whole-response model of the network at Macro 0).

    python fit_network_b.py           fit, then all checks; writes tide_structural_data/network_b.json
    python fit_network_b.py check     the checks only, with the constants already on file

Everything is fitted with the host at 44.1 kHz, where the network is seen
without rate conversion. The converters of a 48 kHz host are those of packet
first_order with one addition of this packet (converter_check).

Fit (Gauss-Newton with a finite-difference Jacobian, the tap weights solved
linearly inside the residual): frequency, gain and Q of the two input
sections, the two loop sections and the low-pass, and the sixteen input
gains, on single-impulse responses at Decay 2, 8 and 20 s and on two cached
impulse trains of packet first_order at Decay 0.5 s. Every fitted number but
one lands on a round value; the model keeps the round values and one fitted
number, the input gain of line 1.

Checks: every entry of both feedback matrices from the residual, the tap law
over a Decay sweep, the single-precision expression of the line length, nulls
on captures no constant was fitted on (both host rates, corners of the Size
and Decay ranges), the late response, ablations and round values.

The holdout of score_network.py is never read here. All captures are this
packet's own (programme seed 7302) or cached trains of packet first_order.
"""
from __future__ import annotations

import copy
import json
import sys
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

import datasets
import first_order_model
import network_b as model
import revocean

HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "tide_structural_data" / "network_b.json"
FIRST_ORDER = json.loads((HERE / "tide_structural_data" / "first_order.json").read_text())
RATE = model.INTERNAL_RATE
AMPLITUDE = 0.5
PROGRAMME_SEED = 7302                      # the holdout uses 20261007
TRAIN_SEEDS = (5101, 5102)                 # cached 44.1 kHz trains of packet first_order, impulses 1.0 to 1.25 s apart

# The model with its round values. Only input.gain.first is fitted (fit_scale).
STRUCTURE = {
    "internal_rate_hz": RATE,
    "lines": {"prime": FIRST_ORDER["lines"]["prime"], "width_flat": 0.272, "width_flat_lines": 6},
    "oscillator": {"rate_hz": 0.6, "depth_ms": 0.88, "phase_step_rad": 11.25},
    "input": {"delay_samples": 44,
              "equaliser": [{"kind": "peaking", "frequency_hz": 200.0, "gain_db": 0.5, "q": 0.4},
                            {"kind": "peaking", "frequency_hz": 1750.0, "gain_db": -0.5, "q": 0.4}],
              "gain": {"first": 0.244, "last_over_first": 0.236, "shape": 3.36}},
    "feedback": {"scale": 0.25,
                 "kernel": [{"kind": "low_shelf", "frequency_hz": 1221.0, "gain_db": -0.28, "q": 0.484},
                            {"kind": "high_shelf", "frequency_hz": 12840.0, "gain_db": -0.12, "q": 0.26}]},
    "output": {"delay_samples": 44,
               "low_pass": {"kind": "low_pass", "frequency_hz": 20000.0, "q": 1.0},
               "tap": {"decay_low_seconds": 0.5, "decay_knee_seconds": 6.0, "first": [0.46, 0.228], "last": [0.336, 0.428],
                       "last_decay_shape": -2.6, "early_shape": 4.0, "late_shape": 2.0}},
    # packet first_order's converters; this packet adds the early stop of the output converter's low wing
    "converter": {**FIRST_ORDER["converter"], "output_low_wing_stops_early": True},
}
# The free fit starts from the round values; its own-input gains start on the gain law.
FREE_SECTIONS = (("input", "equaliser", 0), ("input", "equaliser", 1), ("output", "low_pass", None),
                 ("feedback", "kernel", 0), ("feedback", "kernel", 1))
# (name, Decay, seconds used) of the fit set; "train" cases are excerpts of the cached trains.
FIT_CASES = (("impulse L", 2.0, 14.0), ("impulse R", 2.0, 14.0), ("impulse L", 8.0, 20.0), ("impulse R", 8.0, 20.0),
             ("impulse L", 20.0, 30.0), ("train L", 0.5, 12.0), ("train R", 0.5, 12.0))
IMPULSE_SECONDS = {2.0: 14.0, 8.0: 20.0, 20.0: 30.0}
SWEEP_DECAYS = (0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 5.75, 6.0, 6.5, 8.0, 12.0, 30.0)
# (sample rate, Decay s, Size %, warm-up s): shaped like the holdout, other seed and settings; never fitted on
WORKING_CASES = ((44100, 1.3, 66.0, 10.0), (44100, 2.7, 149.0, 10.0), (44100, 0.9, 91.0, 11.3), (44100, 6.6, 100.0, 10.0),
                 (48000, 0.6, 100.0, 10.0), (48000, 2.4, 100.0, 10.0), (48000, 6.6, 100.0, 10.0),
                 (48000, 1.3, 66.0, 10.0), (48000, 2.7, 149.0, 10.0), (48000, 0.9, 91.0, 11.3))
# corners of the control ranges, the same programme
CORNER_CASES = ((44100, 1.0, 40.0, 10.0), (44100, 0.7, 30.0, 10.0), (44100, 3.0, 200.0, 10.0), (48000, 45.0, 100.0, 10.0),
                (48000, 1.5, 45.0, 3.7), (48000, 0.5, 50.0, 10.0), (48000, 4.0, 150.0, 10.0))
STRETCHES = ((0.25, 1.4), (1.4, 4.0), (4.0, 6.0), (6.0, 14.0))


# ---------------------------------------------------------------- captures

def settings(decay: float, size: float = 100.0) -> dict:
    return {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}


def capture(stimulus: np.ndarray, decay: float, size: float = 100.0, rate: int = RATE, warmup: float = 10.0) -> np.ndarray:
    output = revocean.capture(stimulus, settings(decay, size), sample_rate=rate, warmup=warmup).output
    return output[:len(stimulus)].astype(np.float64)


def impulse_case(channel: int, decay: float) -> tuple:
    """One impulse 0.3 s into the capture."""
    seconds = IMPULSE_SECONDS[decay]
    stimulus = revocean.impulses(seconds, [(int(round(0.3 * RATE)), channel, AMPLITUDE)], sample_rate=RATE)
    return stimulus, capture(stimulus, decay)


def train_times(seed: int) -> np.ndarray:
    """Impulse times of packet first_order's 44.1 kHz trains (its `train_times(seed, 44100, 1.0, 0.25)`)."""
    generator = np.random.default_rng(seed)
    times = [RATE + int(generator.integers(0, int(0.25 * RATE)))]
    while True:
        following = times[-1] + RATE + int(generator.integers(0, int(0.25 * RATE)))
        if following >= 99 * RATE:
            return np.array(times)
        times.append(following)


def train_case(seed: int, channel: int) -> tuple:
    stimulus = revocean.impulses(100.0, [(int(t), channel, AMPLITUDE) for t in train_times(seed)], sample_rate=RATE)
    return stimulus, capture(stimulus, 0.5)


def fit_case(index: int) -> tuple:
    name, decay, seconds = FIT_CASES[index]
    channel = 0 if name.endswith("L") else 1
    stimulus, reference = train_case(TRAIN_SEEDS[0], channel) if name.startswith("train") else impulse_case(channel, decay)
    frames = int(round(seconds * RATE))
    return stimulus[:frames], reference[:frames], decay


def programme_case(rate: int, decay: float, size: float = 100.0, warmup: float = 10.0) -> tuple:
    stimulus = datasets.network_programme(rate, PROGRAMME_SEED)
    return stimulus, capture(stimulus, decay, size, rate, warmup)


def null_db(candidate: np.ndarray, reference: np.ndarray) -> float | None:
    """20 log10(|candidate - reference| / |reference|); None where the reference is silent."""
    energy = float(np.sum(reference ** 2))
    if energy == 0.0:
        return None
    return float(10.0 * np.log10(max(float(np.sum((candidate - reference) ** 2)), 1e-300) / energy))


def stretch_nulls(candidate: np.ndarray, reference: np.ndarray, rate: int, edges) -> list:
    rows = []
    for lo, hi in edges:
        if int(hi * rate) <= len(reference):
            null = null_db(candidate[int(lo * rate):int(hi * rate)], reference[int(lo * rate):int(hi * rate)])
            rows.append(None if null is None else round(null, 2))
    return rows


# ---------------------------------------------------------------- the model at a 44.1 kHz host, in pieces

def host_441(data: dict, stimulus: np.ndarray, values: dict, warmup: float = 10.0, want_reads: bool = False,
             equaliser_behind: bool = False):
    """Output of the model at a 44.1 kHz host for explicit core arrays; optionally the 32 line signals at
    the output (low-passed and delayed), from which tap weights can be solved linearly."""
    before, after = data["input"]["delay_samples"], data["output"]["delay_samples"]
    first = int(round(warmup * RATE)) + before
    at_lines = stimulus.astype(np.float64) if equaliser_behind else model.input_equaliser(data, stimulus.astype(np.float64))
    result = model.run_core(data, at_lines, first, values, want_reads)

    def to_host(signal):
        signal = model.output_low_pass(data, signal)
        if equaliser_behind:
            signal = model.input_equaliser(data, signal)
        return np.concatenate([np.zeros((before + after, signal.shape[1])), signal[:len(signal) - before - after]])

    return (to_host(result[0]), to_host(result[1])) if want_reads else to_host(result)


def project_taps(lines: np.ndarray, reference: np.ndarray) -> tuple:
    """Least-squares tap weights of each group from the 32 line signals; (weights[2][16], prediction)."""
    weights = np.zeros((2, 16))
    prediction = np.zeros_like(reference)
    for group in (0, 1):
        columns = lines[:, 16 * group:16 * (group + 1)]
        weights[group] = np.linalg.solve(columns.T @ columns, columns.T @ reference[:, group])
        prediction[:, group] = columns @ weights[group]
    return weights, prediction


# ---------------------------------------------------------------- the free fit and its reduction

def free_section(data: dict, where: tuple) -> dict:
    block, key, index = where
    return data[block][key] if index is None else data[block][key][index]


def section_keys(entry: dict) -> list:
    return ["frequency_hz", "gain_db", "q"] if "gain_db" in entry else ["frequency_hz", "q"]


def pack() -> np.ndarray:
    """theta: frequency, gain and Q of every section, then the own-input gains of lines 2 to 16."""
    values = [free_section(STRUCTURE, where)[key] for where in FREE_SECTIONS for key in section_keys(free_section(STRUCTURE, where))]
    return np.array(values + list(model.input_gains(STRUCTURE)[0][1:]))


def unpack(theta: np.ndarray) -> tuple:
    """(data with the sections of theta, own-input gain of each line); the gain of line 1 fixes the scale."""
    data = copy.deepcopy(STRUCTURE)
    position = 0
    for where in FREE_SECTIONS:
        entry = free_section(data, where)
        for key in section_keys(entry):
            entry[key] = float(theta[position])
            position += 1
    return data, np.concatenate([[model.input_gains(STRUCTURE)[0][0]], theta[position:]])


def free_prediction(theta: np.ndarray, stimulus: np.ndarray, reference: np.ndarray, decay: float) -> tuple:
    data, own = unpack(theta)
    values = model.parameters(data, decay, 100.0)
    width = model.width_weights(data["lines"])
    values["own"] = np.tile(own, (2, 1))
    values["cross"] = np.tile(own * (1.0 - width) / (1.0 + width), (2, 1))
    values["tap"] = np.ones((2, 16))
    _, lines = host_441(data, stimulus, values, want_reads=True)
    return project_taps(lines, reference)


def case_normal(arguments: tuple) -> tuple:
    """Normal equations of one fit case (finite-difference Jacobian), its null and its projected taps."""
    index, theta, steps = arguments
    stimulus, reference, decay = fit_case(index)
    scale = 1.0 / np.sqrt(np.sum(reference ** 2))
    taps, prediction = free_prediction(theta, stimulus, reference, decay)
    residual = ((reference - prediction) * scale).ravel()
    jacobian = np.zeros((len(residual), len(theta)), dtype=np.float32)
    for k in range(len(theta)):
        moved = theta.copy()
        moved[k] += steps[k]
        jacobian[:, k] = (((reference - free_prediction(moved, stimulus, reference, decay)[1]) * scale).ravel() - residual) / steps[k]
    jacobian = jacobian.astype(np.float64)
    return jacobian.T @ jacobian, jacobian.T @ residual, float(residual @ residual), taps


def fit_free(iterations: int = 3) -> tuple:
    theta = pack()
    steps = np.maximum(np.abs(theta) * 1e-6, 1e-9)
    with ProcessPoolExecutor(len(FIT_CASES)) as pool:
        for iteration in range(iterations + 1):
            results = list(pool.map(case_normal, [(index, theta, steps) for index in range(len(FIT_CASES))]))
            nulls = [round(10.0 * np.log10(result[2]), 2) for result in results]
            print(f"free fit, iteration {iteration}: nulls {nulls}", flush=True)
            if iteration == iterations:
                break
            normal = sum(result[0] for result in results)
            gradient = sum(result[1] for result in results)
            diagonal = np.sqrt(np.diag(normal))
            theta = theta - np.linalg.solve(normal / np.outer(diagonal, diagonal) + 1e-10 * np.eye(len(theta)),
                                            gradient / diagonal) / diagonal
    return theta, results, nulls


def free_fit_evidence(theta: np.ndarray, results: list, nulls: list) -> dict:
    """What the free fit found, next to the round values the model keeps."""
    data, own = unpack(theta)
    taps = np.array([result[3] for result in results])                        # [case][group][line]
    scale = float(np.mean(taps[:, :, 8]))                                     # the tap of line 9 becomes 1
    first_tap = float(np.prod([model.section(entry)[0][0] for entry in data["input"]["equaliser"]])
                      / np.prod([model.section(entry)[0][0] for entry in STRUCTURE["input"]["equaliser"]]))
    level = own * 2.0 / (1.0 + model.width_weights(data["lines"])) * scale * first_tap
    line = np.arange(1, 17)

    def law(t):
        return (t[0] + t[1]) / 2.0 - (t[0] - t[1]) / 2.0 * model.s_curve((line - 8.5) / 7.5, t[2])

    fitted = least_squares(lambda t: law(t) - level, [0.244, 0.0576, 3.36], xtol=1e-15, ftol=1e-15, gtol=1e-15)
    names = [f"{name}, Decay {decay} s" for name, decay, _ in FIT_CASES]
    return {
        "freeSections": [{"round": free_section(STRUCTURE, where), "fitted": free_section(data, where)} for where in FREE_SECTIONS],
        "freeGainLevel": [float(v) for v in level],
        "freeGainLaw": {"first": float(fitted.x[0]), "last": float(fitted.x[1]), "last_over_first": float(fitted.x[1] / fitted.x[0]),
                        "shape": float(fitted.x[2]), "largestRelativeResidual": float(np.max(np.abs(fitted.fun / level)))},
        # own and cross gains of the 16 lines of one group, squared and summed: lands next to 0.252
        "sumOfSquaredInputGainsOfAGroup": float(np.sum(level ** 2 * (1.0 + model.width_weights(data["lines"]) ** 2) / 2.0)),
        "tapOfLine9Spread": float(np.max(np.abs(taps[:, :, 8] / scale - 1.0))),
        "freeTaps": {name: (taps[k] / taps[k][:, 8:9]).round(7).tolist() for k, name in enumerate(names)},
        "freeFitNullsDb": dict(zip(names, nulls)),
    }


def scale_case(arguments: tuple) -> tuple:
    data, index = arguments
    stimulus, reference, decay = fit_case(index)
    rendered = model.render(stimulus, RATE, 10.0, decay, 100.0, data)
    weight = 1.0 / np.sum(reference ** 2)
    return weight * np.sum(rendered * reference), weight * np.sum(rendered ** 2), null_db(rendered, reference)


def fit_scale() -> tuple:
    """The one fitted constant: the input gain of line 1, by least squares on the fit set with every other
    constant at its round value. Returns (constants, evidence)."""
    data = copy.deepcopy(STRUCTURE)
    with ProcessPoolExecutor(len(FIT_CASES)) as pool:
        sums = list(pool.map(scale_case, [(data, index) for index in range(len(FIT_CASES))]))
        gain = sum(cross for cross, _, _ in sums) / sum(power for _, power, _ in sums)
        per_case = [cross / power for cross, power, _ in sums]
        data["input"]["gain"]["first"] = float(data["input"]["gain"]["first"] * gain)
        nulls = [null for _, _, null in pool.map(scale_case, [(data, index) for index in range(len(FIT_CASES))])]
    names = [f"{name}, Decay {decay} s" for name, decay, _ in FIT_CASES]
    evidence = {"firstPerCase": dict(zip(names, [float(STRUCTURE["input"]["gain"]["first"] * value) for value in per_case])),
                "roundModelNullsDb": dict(zip(names, [round(null, 2) for null in nulls]))}
    print("input gain of line 1:", data["input"]["gain"]["first"], evidence, flush=True)
    return data, evidence


# ---------------------------------------------------------------- checks

def programme_nulls(data: dict, cases: tuple) -> list:
    rows = []
    for rate, decay, size, warmup in cases:
        stimulus, reference = programme_case(rate, decay, size, warmup)
        rendered = model.render(stimulus, rate, warmup, decay, size, data)
        rows.append({"sampleRate": rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup,
                     "overallDb": round(null_db(rendered, reference), 2),
                     "stretchesDb": stretch_nulls(rendered, reference, rate, STRETCHES)})
        print("programme case", rows[-1], flush=True)
    return rows


def late_response(data: dict) -> dict:
    """How the null runs into the late response: single impulses at Decay 2, 8 and 20 s."""
    edges = [(0.25, 0.5), (0.5, 1), (1, 2), (2, 4), (4, 8), (8, 14), (14, 20), (20, 30)]
    table = {"edgesSeconds": edges}
    for decay in (2.0, 8.0, 20.0):
        stimulus, reference = impulse_case(0, decay)
        rendered = model.render(stimulus, RATE, 10.0, decay, 100.0, data)
        table[f"Decay {decay} s"] = {"overallDb": round(null_db(rendered, reference), 2),
                                     "left": stretch_nulls(rendered[:, 0], reference[:, 0], RATE, edges),
                                     "right": stretch_nulls(rendered[:, 1], reference[:, 1], RATE, edges)}
        print("late response", decay, table[f"Decay {decay} s"], flush=True)
    return table


def tap_sweep(data: dict) -> dict:
    """Tap weights solved freely at 14 Decay values (programme captures) against the law."""
    rows = []
    for decay in SWEEP_DECAYS:
        stimulus, reference = programme_case(RATE, decay)
        values = model.parameters(data, decay, 100.0)
        law = values["tap"][0].copy()
        values["tap"] = np.ones((2, 16))
        _, lines = host_441(data, stimulus, values, want_reads=True)
        weights, prediction = project_taps(lines, reference)
        relative = weights / weights[:, 8:9]
        rows.append({"decaySeconds": decay, "line1": relative[:, 0].round(6).tolist(), "line10": relative[:, 9].round(6).tolist(),
                     "line16": relative[:, 15].round(6).tolist(), "line9Absolute": weights[:, 8].round(6).tolist(),
                     "largestDeviationFromLaw": float(np.max(np.abs(relative - law[None, :]))),
                     "lawLine1": round(float(law[0]), 6), "lawLine16": round(float(law[15]), 6),
                     "nullFreeTapsDb": round(null_db(prediction, reference), 2)})
        print("tap sweep", rows[-1], flush=True)
    return {"rows": rows, "largestDeviationFromLaw": max(row["largestDeviationFromLaw"] for row in rows)}


def matrix_group(arguments: tuple) -> dict:
    """Every entry of one group's feedback matrix: its deviation from +-scale fitted on the residual, which is
    linear in a small change of one entry, and the null with its sign flipped."""
    data, group = arguments
    stimulus, reference = impulse_case(group, 2.0)
    frames = 3 * RATE
    stimulus, reference = stimulus[:frames], reference[:frames, group]
    values = model.parameters(data, 2.0, 100.0)
    base = host_441(data, stimulus, values)[:, group]
    residual = reference - base
    deviation = np.zeros((16, 16))
    flipped = np.zeros((16, 16))
    step = 1e-4
    for written in range(16):
        for read in range(16):
            entry = values["matrix"][group, written, read]
            values["matrix"][group, written, read] = entry * (1.0 + step)
            slope = (host_441(data, stimulus, values)[:, group] - base) / step
            deviation[written, read] = (residual @ slope) / (slope @ slope)
            values["matrix"][group, written, read] = -entry
            flipped[written, read] = null_db(host_441(data, stimulus, values)[:, group], reference)
            values["matrix"][group, written, read] = entry
    return {"baseNullDb": round(null_db(base, reference), 2), "largestRelativeDeviation": float(np.max(np.abs(deviation))),
            "rmsRelativeDeviation": float(np.sqrt(np.mean(deviation ** 2))),
            "bestNullWithOneSignFlippedDb": round(float(flipped.min()), 2), "worstNullWithOneSignFlippedDb": round(float(flipped.max()), 2)}


def matrix_check(data: dict) -> dict:
    with ProcessPoolExecutor(2) as pool:
        left, right = pool.map(matrix_group, [(data, 0), (data, 1)])
    print("matrix check", left, right, flush=True)
    return {"left": left, "right": right}


def ablations(data: dict) -> dict:
    """One element changed at a time; nulls on two working cases at a 44.1 kHz host that no constant was fitted on."""
    cases = [programme_case(RATE, 6.6) + (6.6, 100.0), programme_case(RATE, 2.7, 149.0) + (2.7, 149.0)]

    def nulls(mutate=None, **options) -> list:
        row = []
        for stimulus, reference, decay, size in cases:
            changed = copy.deepcopy(data)
            values = model.parameters(changed, decay, size)
            if mutate is not None:
                mutate(changed, values, decay, size)
            row.append(round(null_db(host_441(changed, stimulus, values, **options), reference), 2))
        return row

    def plain_order(changed, values, decay, size):
        values["matrix"] = values["matrix"][:, ::-1].copy()

    def direct_gain_only(changed, values, decay, size):
        values["kernel"] = np.array([[values["kernel"][0, 0] * values["kernel"][1, 0], 0, 0, 0, 0], [1.0, 0, 0, 0, 0]])

    def no_kernel(changed, values, decay, size):
        values["kernel"] = np.array([[1.0, 0, 0, 0, 0], [1.0, 0, 0, 0, 0]])

    def single_precision_kernel(changed, values, decay, size):
        values["kernel"] = values["kernel"].astype(np.float32).astype(np.float64)

    def kernel_value(index, key, value):
        def mutate(changed, values, decay, size):
            changed["feedback"]["kernel"][index][key] = value
            values["kernel"] = model.parameters(changed, decay, size)["kernel"]
        return mutate

    def equaliser_value(index, key, value):
        def mutate(changed, values, decay, size):
            changed["input"]["equaliser"][index][key] = value
        return mutate

    def low_pass_value(key, value):
        def mutate(changed, values, decay, size):
            changed["output"]["low_pass"][key] = value
        return mutate

    def taps_of_shortest_decay(changed, values, decay, size):
        values["tap"] = np.tile(model.tap_weights(changed, 0.5), (2, 1))

    def rounded_length_in_attenuation(changed, values, decay, size):
        values["attenuation"] = 10.0 ** (-3.0 * values["length"] / (RATE * decay))

    def gain_value(key, value):
        def mutate(changed, values, decay, size):
            changed["input"]["gain"][key] = value
            own, cross = model.input_gains(changed)
            values["own"], values["cross"] = np.tile(own, (2, 1)), np.tile(cross, (2, 1))
        return mutate

    def one_tap_curve(changed, values, decay, size):
        tap = changed["output"]["tap"]
        tap["late_shape"] = tap["early_shape"]
        values["tap"] = np.tile(model.tap_weights(changed, decay), (2, 1))

    def no_cross_feed(changed, values, decay, size):
        values["cross"] = np.zeros((2, 16))

    table = {
        "model": nulls(),
        "Hadamard rows in natural order (not reversed)": nulls(plain_order),
        "loop kernel reduced to its direct gain 0.9911": nulls(direct_gain_only),
        "no loop kernel (gain 1)": nulls(no_kernel),
        "loop kernel coefficients rounded to single precision": nulls(single_precision_kernel),
        "low shelf at 1220 Hz": nulls(kernel_value(0, "frequency_hz", 1220.0)),
        "low shelf -0.3 dB": nulls(kernel_value(0, "gain_db", -0.3)),
        "low shelf Q 0.5": nulls(kernel_value(0, "q", 0.5)),
        "high shelf at 12800 Hz": nulls(kernel_value(1, "frequency_hz", 12800.0)),
        "high shelf -0.1 dB": nulls(kernel_value(1, "gain_db", -0.1)),
        "high shelf Q 0.25": nulls(kernel_value(1, "q", 0.25)),
        "equaliser behind the lines": nulls(equaliser_behind=True),
        "first peaking section at 201 Hz": nulls(equaliser_value(0, "frequency_hz", 201.0)),
        "second peaking section Q 0.41": nulls(equaliser_value(1, "q", 0.41)),
        "low-pass at 19900 Hz": nulls(low_pass_value("frequency_hz", 19900.0)),
        "low-pass Q 0.99": nulls(low_pass_value("q", 0.99)),
        "tap weights of Decay 0.5 s": nulls(taps_of_shortest_decay),
        "attenuation from the rounded length": nulls(rounded_length_in_attenuation),
        "input gain: last / first 0.235": nulls(gain_value("last_over_first", 0.235)),
        "input gain: shape 3.35": nulls(gain_value("shape", 3.35)),
        "input gain of line 1: 0.244": nulls(gain_value("first", 0.244)),
        "late tap curve with the shape of the early one": nulls(one_tap_curve),
        "no cross feed at the line inputs": nulls(no_cross_feed),
    }
    for name, row in table.items():
        print(f"ablation: {name}: {row}", flush=True)
    return {"casesDecaySize": [[6.6, 100.0], [2.7, 149.0]], "nullsDb": table}


def length_expression(data: dict) -> dict:
    """The line length at the first two samples of 2784 first arrivals (lines 1 to 8, own input, Decay 0.5 s).

    In front of the low-pass the first sample of an arrival is gain (1 - f) q0 and the second gain (f q0 + (1 - f) q1),
    f the fraction of the length at that sample and q the equaliser's first two taps. The residual of the model
    there, in units of the float32 step of the length, is 0 where the model has the reference's length and +-1
    where it is one step off.
    """
    prime = np.array(data["lines"]["prime"])
    pulse = np.zeros(32)
    pulse[0] = 1.0
    low_pass = model.output_low_pass(data, pulse[:, None])[:, 0]
    equalised = model.input_equaliser(data, pulse[:, None])[:2, 0]
    own, _ = model.input_gains(data)
    tap = model.tap_weights(data, 0.5)
    attenuation = model.parameters(data, 0.5, 100.0)["attenuation"]
    depth = float(model.modulation_depth(data["oscillator"]))
    increment = np.float32(2.0 * np.pi * data["oscillator"]["rate_hz"] / RATE)
    warm, before, after = 10 * RATE, data["input"]["delay_samples"], data["output"]["delay_samples"]
    half_pi = np.pi / 2.0
    half_pi_single = float(np.float32(half_pi))
    errors, plain_differs = [], []
    for seed in TRAIN_SEEDS:
        for channel in (0, 1):
            stimulus, reference = train_case(seed, channel)
            residual = reference - model.render(stimulus, RATE, 10.0, 0.5, 100.0, data)
            for line in range(8):
                segments = first_order_model.accumulator_segments(
                    first_order_model.oscillator_start(2 * line + channel, data["oscillator"]["phase_step_rad"]), increment, warm + 101 * RATE)
                gain = AMPLITUDE * own[line] * attenuation[channel, line] * tap[line]
                for time in train_times(seed):
                    written = warm + int(time) + before
                    sample = np.arange(written + prime[channel, line] - 42, written + prime[channel, line] + 44)
                    theta = first_order_model.accumulator_phase(segments, sample).astype(np.float32)
                    quadrant = np.round(theta.astype(float) / half_pi)
                    sine = np.sin(theta.astype(float) - quadrant * (half_pi_single - half_pi)).astype(np.float32)
                    length = (np.float32(prime[channel, line]) + np.float32(depth) * sine).astype(float)
                    first = int(np.flatnonzero(sample - np.floor(length) - written >= 0)[0])
                    raw = sample[first] - warm + after
                    design = np.zeros((24, 6))
                    for lag in range(6):
                        design[lag:, lag] = low_pass[:24 - lag]
                    front = np.linalg.lstsq(design, residual[raw:raw + 24, channel], rcond=None)[0]
                    plain = (prime[channel, line] + 38.808 * np.sin(theta.astype(float))).astype(np.float32).astype(float)
                    for offset, error in ((0, -front[0] / (gain * equalised[0])), (1, front[1] / (gain * (equalised[0] - equalised[1])))):
                        step = float(np.spacing(np.float32(length[first + offset])))
                        errors.append(error / step)
                        plain_differs.append(plain[first + offset] != length[first + offset] + round(error / step) * step)
    errors, plain_differs = np.array(errors), np.array(plain_differs)
    whole = np.round(errors)
    clear = np.abs(errors - whole) < 0.25                      # the rest sit under another arrival
    result = {"samples": int(clear.sum()), "unclear": int(np.sum(~clear)), "oneStepOff": int(np.sum(whole[clear] != 0)),
              "rmsDistanceFromWholeSteps": float(np.sqrt(np.mean((errors - whole)[clear] ** 2))),
              "oneStepOffWithFl32OfDoubleExpression": int(np.sum(plain_differs[clear]))}
    print("length expression", result, flush=True)
    return result


def converter_check(data: dict) -> dict:
    """The output converter at a 48 kHz host by class of output sample, with and without the early stop of its
    low wing. Phase is the lattice position of the output sample modulo one internal sample."""
    rate, decay, size, warmup = WORKING_CASES[5]
    stimulus, reference = programme_case(rate, decay, size, warmup)
    host = int(round(warmup * rate)) + np.arange(len(stimulus))
    phase = (model.HOST_STEP * host - int(round(data["converter"]["output_delay_host_samples"] * model.HOST_STEP))) % model.LATTICE
    classes = {"on an internal sample (phase 0)": phase == 0, "other exact table entries (phase a multiple of 5)": (phase % 5 == 0) & (phase != 0),
               "all other output samples": phase % 5 != 0}
    table = {}
    for name, early in (("model", True), ("low wing uses its last entry", False)):
        changed = copy.deepcopy(data)
        changed["converter"]["output_low_wing_stops_early"] = early
        rendered = model.render(stimulus, rate, warmup, decay, size, changed)
        table[name] = {"overallDb": round(null_db(rendered, reference), 2),
                       **{label: round(null_db(rendered[chosen], reference[chosen]), 2) for label, chosen in classes.items()}}
    print("converter check", table, flush=True)
    return {"caseRateDecaySize": [rate, decay, size], "nullsDb": table}


def checks(data: dict) -> dict:
    return {
        "workingCases": programme_nulls(data, WORKING_CASES),
        "cornerCases": programme_nulls(data, CORNER_CASES),
        "converter": converter_check(data),
        "lateResponse": late_response(data),
        "tapSweep": tap_sweep(data),
        "matrix": matrix_check(data),
        "ablations": ablations(data),
        "lengthExpression": length_expression(data),
    }


def render_captures() -> None:
    """Render (or find in the cache) every capture of this packet, three at a time."""
    jobs = [lambda c=channel, d=decay: impulse_case(c, d) for decay in (2.0, 8.0) for channel in (0, 1)] + [lambda: impulse_case(0, 20.0)]
    jobs += [lambda d=decay: programme_case(RATE, d) for decay in SWEEP_DECAYS]
    jobs += [lambda case=case: programme_case(case[0], case[1], case[2], case[3]) for case in WORKING_CASES + CORNER_CASES]
    jobs += [lambda s=seed, c=channel: train_case(s, c) for seed in TRAIN_SEEDS for channel in (0, 1)]
    with ThreadPoolExecutor(3) as pool:
        list(pool.map(lambda job: job(), jobs))


def main() -> None:
    revocean.identity()
    render_captures()
    if sys.argv[1:] == ["check"]:
        stored = json.loads(OUTPUT.read_text())
        data = copy.deepcopy(STRUCTURE)
        data["input"]["gain"]["first"] = stored["input"]["gain"]["first"]
        fit_evidence = stored["evidence"]["fit"]
    else:
        fit_evidence = free_fit_evidence(*fit_free())
        print(json.dumps({key: fit_evidence[key] for key in ("freeSections", "freeGainLaw", "sumOfSquaredInputGainsOfAGroup")}, indent=1), flush=True)
        data, scale_evidence = fit_scale()
        fit_evidence.update(scale_evidence)
    evidence = {"fit": fit_evidence, **checks(data)}
    OUTPUT.write_text(json.dumps({**data, "evidence": evidence}, indent=1))
    print("wrote", OUTPUT)


if __name__ == "__main__":
    main()
