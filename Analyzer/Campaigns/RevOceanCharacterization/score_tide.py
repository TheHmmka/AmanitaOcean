#!/usr/bin/env python3
"""Score a model of Rev OCEAN in Tide mode (Macro above 0) against the reference.

A candidate is a Python module with

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro_percent, seed, phase=None) -> array (frames, 2)

giving the reference's raw output at the neutral baseline, Mix 100 %, for a
stereo stimulus that starts `warmup_seconds` after the first processed sample
(index n is host sample n of the stimulus, the reported latency is still in
it). `seed` selects one instance of the random voice phase. `phase`, when
given, replaces the candidate's own phase generator: it is called with times in
seconds since the first processed sample and returns the phase of voice A of
both outputs in cycles, shape (2, len(seconds)); voice B is half a cycle away.

    python score_tide.py tide_model.py                     all three scores
    python score_tide.py tide_model.py --null              only the phase-fitted null (--refit N, --case K)
    python score_tide.py tide_model.py --statistics        only the free-running statistics (--seeds N, --macro 100 50)
    python score_tide.py tide_model.py --level             only the level cycle (--seeds N)
    python score_tide.py tide_model.py --json report.json  also write the full report

1. Phase-fitted null. Above Macro 0 two instances of the reference differ in
   one thing, the phase of the two voices of each output. For every case of
   NULL_CASES (a 14 s programme or an impulse train at Macro 100, 50 and 25 %)
   the phase of each output is described by a few numbers: in the form of the
   reference's own generator (EaseCurve: a rate and raised cosines between
   knots, 5 to 11 numbers per output for 14 s) or, free of any model of the
   phase, by a cubic B-spline with knots 0.25 or 0.5 s apart (SplineCurve).
   Only those numbers are fitted, by least squares on the candidate's own
   renders (--refit steps), starting from the curve the campaign model found
   in that capture (tide_structural_data/tide_model.json); without a stored
   curve the candidate finds a spline with knots PHASE_STEP apart on its own.
   The score is 20 log10(|candidate - reference| / |reference|) with no gain,
   delay or polarity fitted, overall and per stretch of time as in
   score_network.py. It measures everything except the random phase.
2. Free-running statistics. The candidate renders the stimuli behind
   tide_structural_data/targets.json with its own generator for several seeds;
   descriptors.compare_to_targets gives every descriptor in units of the
   reference's own spread from response to response.
3. Level cycle. Steady noise at Macro 100 %: the wet level per second of the
   candidate (several seeds) against that of reference realisations, after a
   10 s warm-up (8 realisations) and from the start of an instance (the 40
   instances of packet tide_phase).

The reference captures of 1 and 3 are this packet's own (programme seed 8301)
and, for the 40 instances, packet tide_phase's; no locked holdout is read.
Captures are cached; the first run renders them.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.interpolate import BSpline

import datasets
import descriptors
import revocean
import tide_phase

HERE = Path(__file__).resolve().parent
DATA = HERE / "tide_structural_data" / "tide_model.json"
TARGETS = HERE / "tide_structural_data" / "targets.json"
PROGRAMME_SEED = 8301                      # the network holdout uses 20261007, packet network 60606
TRAIN_SEED = 8302
PHASE_STEP = 0.5                           # seconds between the knots of a phase curve
PROGRAMME_STRETCHES = (("0.25-1.4s", 0.25, 1.4), ("1.4-4s", 1.4, 4.0), ("4-6s", 4.0, 6.0), ("6-14s", 6.0, 14.0))
TRAIN_STRETCHES = (("0-3.5s", 0.0, 3.5), ("3.5-7s", 3.5, 7.0), ("7-10.5s", 7.0, 10.5), ("10.5-14s", 10.5, 14.0))
TRAIN_SECONDS = 14.0
SILENCE = 1e-30                            # an rms below this is not part of the response (-600 dB re full scale)
LEVEL_SEED, LEVEL_SECONDS, LEVEL_START, LEVEL_STOP, LEVEL_PEAK = 211, 52.0, 1.0, 51.0, 0.25
LEVEL_RATE = 48000
START_SECONDS = 100.0


# ---------------------------------------------------------------- cases of the phase-fitted null

@dataclass(frozen=True)
class NullCase:
    kind: str                # "programme" or "train"
    sample_rate: int
    decay: float             # seconds
    size: float              # percent
    warmup: float            # seconds
    macro: float             # percent
    realisation: int

    @property
    def key(self) -> str:
        return f"{self.kind}/{self.sample_rate}/{self.decay:g}/{self.size:g}/{self.warmup:g}/{self.macro:g}/{self.realisation}"

    @property
    def stretches(self) -> tuple:
        return PROGRAMME_STRETCHES if self.kind == "programme" else TRAIN_STRETCHES


NULL_CASES = (
    NullCase("programme", 44100, 2.0, 100.0, 10.0, 100.0, 0),
    NullCase("programme", 44100, 3.1, 157.0, 12.5, 100.0, 1),
    NullCase("programme", 44100, 11.0, 100.0, 10.0, 100.0, 2),
    NullCase("programme", 48000, 2.0, 100.0, 10.0, 100.0, 0),
    NullCase("programme", 48000, 7.3, 62.0, 6.1, 100.0, 1),
    NullCase("programme", 96000, 1.8, 73.0, 10.0, 100.0, 0),
    NullCase("train", 44100, 0.5, 100.0, 10.0, 100.0, 0),
    NullCase("train", 48000, 1.0, 134.0, 30.0, 100.0, 1),
    NullCase("programme", 44100, 2.0, 100.0, 10.0, 50.0, 0),
    NullCase("programme", 48000, 0.65, 100.0, 10.0, 50.0, 0),
    NullCase("programme", 88200, 3.7, 118.0, 10.0, 50.0, 0),
    NullCase("train", 48000, 0.5, 100.0, 10.0, 50.0, 0),
    NullCase("programme", 44100, 1.0, 73.0, 10.0, 25.0, 0),
    NullCase("programme", 48000, 5.2, 181.0, 10.0, 25.0, 0),
    NullCase("train", 44100, 0.5, 100.0, 10.0, 25.0, 0),
)


def train(sample_rate: int, seed: int = TRAIN_SEED) -> np.ndarray:
    """Impulses on the left input (0.5), the right input (0.5) and both (0.35) in turn, 0.8 to 1.0 s apart, for 14 s."""
    generator = np.random.default_rng(seed)
    events, time, index = [], 0.25, 0
    while time < TRAIN_SECONDS - 1.0:
        events.append((int(round(time * sample_rate)), (0, 1, None)[index % 3], 0.5 if index % 3 < 2 else 0.35))
        time += 0.8 + 0.2 * generator.random()
        index += 1
    return revocean.impulses(TRAIN_SECONDS, events, sample_rate=sample_rate)


def stimulus_of(case: NullCase) -> np.ndarray:
    if case.kind == "programme":
        return datasets.network_programme(case.sample_rate, PROGRAMME_SEED)
    return train(case.sample_rate)


def settings_of(decay: float, size: float, macro: float) -> dict:
    return {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size), "macro": macro / 100.0}


def reference_of(case: NullCase) -> tuple:
    """(stimulus, raw reference output cut to the stimulus) of one case."""
    stimulus = stimulus_of(case)
    captured = revocean.capture(stimulus, settings_of(case.decay, case.size, case.macro), sample_rate=case.sample_rate,
                                warmup=case.warmup, realisation=case.realisation)
    return stimulus, captured.output[:len(stimulus)].astype(np.float64)


# ---------------------------------------------------------------- phase curves

class SplineCurve:
    """Phase of voice A of both outputs as cubic B-splines on a uniform grid, in cycles: the model-free description.

    `coefficients[output][k]` belongs to the basis function that peaks at `start + (k - 1) step` seconds
    after the first processed sample. Calling the curve with an array of times returns (2, len(times)).
    Outside its grid the curve continues the cubic of its last interval.
    """
    EPSILON = 1e-6             # cycles, step of a difference quotient
    COLOURS = 6                # coefficients this far apart are moved in one render (a basis function is 4 steps wide)
    SMOOTH = 1e-8              # weight of the second differences; only decides coefficients no data reaches

    def __init__(self, start: float, step: float, coefficients):
        self.start, self.step = float(start), float(step)
        self.parameters = [np.array(row, dtype=np.float64) for row in coefficients]
        knots = self.start + self.step * (np.arange(len(self.parameters[0]) + 4) - 3)
        self.splines = [BSpline(knots, row, 3, extrapolate=True) for row in self.parameters]

    def __call__(self, seconds) -> np.ndarray:
        seconds = np.asarray(seconds, dtype=np.float64)
        return np.stack([spline(seconds) for spline in self.splines])

    def with_parameters(self, parameters) -> "SplineCurve":
        return SplineCurve(self.start, self.step, parameters)

    def moves(self) -> list:
        """Groups of parameters that one render can move: per output [(index, step, from seconds, to seconds)]."""
        size = len(self.parameters[0])
        peak = self.start + self.step * (np.arange(size) - 1)
        colour = lambda first: [(k, self.EPSILON, peak[k] - 3 * self.step, peak[k] + 3 * self.step) for k in range(first, size, self.COLOURS)]
        return [[colour(first), colour(first)] for first in range(min(self.COLOURS, size))]

    def penalties(self, samples: int) -> list:
        size = len(self.parameters[0])
        bend = np.diff(np.eye(size), 2, axis=0)
        return [self.SMOOTH * samples / size * bend.T @ bend] * 2

    def as_json(self) -> dict:
        return {"kind": "spline", "start": self.start, "step": self.step,
                "coefficients": [[round(float(v), 9) for v in row] for row in self.parameters]}

    @classmethod
    def through(cls, start: float, step: float, intervals: int, times: np.ndarray, values: np.ndarray) -> "SplineCurve":
        """The smoothest curve of `intervals` intervals through samples values[output] at `times`."""
        blank = cls(start, step, np.zeros((2, intervals + 3)))
        inside = np.clip(times, start, start + intervals * step - 1e-9)
        design = BSpline.design_matrix(inside, blank.splines[0].t, 3).toarray()
        bend = 1e-3 * np.diff(np.eye(intervals + 3), 2, axis=0)
        rows = [np.linalg.lstsq(np.vstack([design, bend]), np.concatenate([row, np.zeros(len(bend))]), rcond=None)[0] for row in values]
        return cls(start, step, rows)


class EaseCurve:
    """Phase of voice A of both outputs in the form of the reference's generator (findings/tide_phase.md).

    Per output: phase(t) = rate t + level(t). The level is `values[0]` until `times[0]`, then moves from
    knot to knot along raised cosines and stays at `values[-1]` after `times[-1]`. A parameter vector is
    [rate, times..., values...]: a handful of numbers for 14 s.
    """
    EPSILON = {"rate": 1e-7, "time": 1e-4, "value": 1e-6}     # cycles/s, seconds, cycles

    def __init__(self, parameters):
        self.parameters = [np.array(vector, dtype=np.float64) for vector in parameters]

    @classmethod
    def of(cls, rate, times, values) -> "EaseCurve":
        """From per-output rates, knot times and knot values."""
        return cls([np.concatenate([[rate[o]], times[o], values[o]]) for o in (0, 1)])

    def parts(self, output: int) -> tuple:
        """(rate, knot times, knot values) of one output."""
        vector = self.parameters[output]
        knots = (len(vector) - 1) // 2
        return vector[0], vector[1:1 + knots], vector[1 + knots:]

    def level(self, output: int, seconds: np.ndarray) -> np.ndarray:
        _, times, values = self.parts(output)
        if len(times) == 1:
            return np.full(seconds.shape, values[0])
        index = np.clip(np.searchsorted(times, seconds, side="right") - 1, 0, len(times) - 2)
        along = np.clip((seconds - times[index]) / (times[index + 1] - times[index]), 0.0, 1.0)
        return values[index] + (values[index + 1] - values[index]) * (0.5 - 0.5 * np.cos(np.pi * along))

    def __call__(self, seconds) -> np.ndarray:
        seconds = np.asarray(seconds, dtype=np.float64)
        return np.stack([self.parameters[o][0] * seconds + self.level(o, seconds) for o in (0, 1)])

    def with_parameters(self, parameters) -> "EaseCurve":
        return EaseCurve(parameters)

    def moves(self) -> list:
        """One parameter of each output per render; a parameter acts on the whole record."""
        def one(output: int, index: int) -> list:
            size = len(self.parameters[output])
            if index >= size:
                return []
            kind = "rate" if index == 0 else "time" if index <= (size - 1) // 2 else "value"
            return [(index, self.EPSILON[kind], -np.inf, np.inf)]
        return [[one(0, index), one(1, index)] for index in range(max(len(vector) for vector in self.parameters))]

    def penalties(self, samples: int) -> list:
        return [np.zeros((len(vector), len(vector))) for vector in self.parameters]

    def as_json(self) -> dict:
        parts = [self.parts(o) for o in (0, 1)]
        return {"kind": "eases", "rate": [round(float(p[0]), 10) for p in parts],
                "times": [[round(float(v), 7) for v in p[1]] for p in parts], "values": [[round(float(v), 9) for v in p[2]] for p in parts]}


def curve_from_json(entry: dict):
    if entry["kind"] == "spline":
        return SplineCurve(entry["start"], entry["step"], entry["coefficients"])
    return EaseCurve.of(entry["rate"], entry["times"], entry["values"])


def stored_curve(case: NullCase):
    """The phase the campaign model found in the reference capture of a case, or None."""
    if not DATA.exists():
        return None
    entry = json.loads(DATA.read_text()).get("phaseCurves", {}).get(case.key)
    return None if entry is None else curve_from_json(entry)


# ---------------------------------------------------------------- the fit of a phase curve

def local_weight(reference: np.ndarray, sample_rate: int, block: float = 0.05, span: int = 5) -> np.ndarray:
    """1 / rms of each channel of the reference over `span` blocks around a sample; 0 where it is silent.

    With it every stretch of time counts alike in a least-squares fit although the response falls by
    hundreds of dB. Block sums and an interpolation of the logarithm keep the tail out of rounding error.
    Silent means an rms below SILENCE: the reference's output stops falling near 1e-36 (findings/network.md).
    """
    size = int(block * sample_rate)
    count = len(reference) // size + 1
    padded = np.concatenate([reference, np.zeros((count * size - len(reference), reference.shape[1]))])
    energy = (padded ** 2).reshape(count, size, -1).mean(axis=1)
    wide = np.stack([energy[max(0, k - span // 2):k + span // 2 + 1].mean(axis=0) for k in range(count)])
    centre = (np.arange(count) + 0.5) * size
    index = np.arange(len(reference))
    weight = np.zeros(reference.shape)
    for channel in range(reference.shape[1]):
        silent = wide[:, channel] <= SILENCE ** 2
        level = np.log(np.where(silent, 1.0, wide[:, channel]))
        weight[:, channel] = np.exp(-0.5 * np.interp(index, centre, level))
        weight[np.interp(index, centre, silent.astype(float)) > 0.0, channel] = 0.0
    return weight


def null_db(candidate: np.ndarray, reference: np.ndarray) -> float:
    difference = float(np.sum((candidate - reference) ** 2))
    return round(float(10.0 * np.log10(max(difference, 1e-300) / max(float(np.sum(reference ** 2)), 1e-300))), 2)


class PhaseFit:
    """Least-squares fit of a phase curve (SplineCurve or EaseCurve) to one reference capture.

    `model(curve)` returns the candidate's output (frames, 2) for a prescribed phase, aligned with
    `reference`; sample 0 is sample `origin` since the first processed sample. Output o must depend on
    the phase of output o only, as it does in the reference's structure. The residual is weighted by
    `local_weight`, so every stretch of time counts alike.
    """

    def __init__(self, model, reference: np.ndarray, sample_rate: int, origin: int):
        self.model, self.reference, self.sample_rate = model, reference, int(sample_rate)
        self.weight = local_weight(reference, sample_rate)
        self.time = (int(origin) + np.arange(len(reference))) / sample_rate

    def residual(self, curve) -> np.ndarray:
        return (np.asarray(self.model(curve), dtype=np.float64)[:len(self.reference)] - self.reference) * self.weight

    def start(self, start: float, step: float, rate: float, levels=np.arange(0.0, 1.0, 0.01), window: float = 0.25) -> SplineCurve:
        """A first curve: per quarter second the constant level (phase minus rate x time) that nulls best,
        joined over time by the path of least cost with a penalty on jumps."""
        size = int(window * self.sample_rate)
        count = len(self.reference) // size
        cost = np.zeros((len(levels), count, 2))
        for index, level in enumerate(levels):
            squared = self.residual(lambda seconds, level=level: np.tile(level + rate * np.asarray(seconds), (2, 1))) ** 2
            cost[index] = squared[:count * size].reshape(count, size, 2).sum(axis=1)
        heard = self.weight[:count * size].reshape(count, size, 2).min(axis=1) > 0.0
        centre = self.time[0] + (np.arange(count) + 0.5) * size / self.sample_rate
        rows = []
        for output in (0, 1):
            use = np.nonzero(heard[:, output])[0]
            path = cheapest_path(cost[:, use, output].T, levels[1] - levels[0])
            level = np.unwrap(2.0 * np.pi * levels[path]) / (2.0 * np.pi)
            rows.append(np.interp(centre, centre[use], level) + rate * centre)
        return SplineCurve.through(start, step, int(np.ceil(len(self.reference) / self.sample_rate / step)), centre, np.array(rows))

    def normal_equations(self, curve, base: np.ndarray) -> tuple:
        """(J'J, J'r) per output for the weighted residual `base` of `curve`, by difference quotients."""
        columns = [{}, {}]
        for move in curve.moves():
            moved = [vector.copy() for vector in curve.parameters]
            for output in (0, 1):
                for index, epsilon, _, _ in move[output]:
                    moved[output][index] += epsilon
            difference = self.residual(curve.with_parameters(moved)) - base
            for output in (0, 1):
                for index, epsilon, low, high in move[output]:
                    first, last = np.searchsorted(self.time, [low, high])
                    if last > first:
                        columns[output][index] = (first, difference[first:last, output] / epsilon)
        normal = [np.zeros((len(vector), len(vector))) for vector in curve.parameters]
        gradient = [np.zeros(len(vector)) for vector in curve.parameters]
        for output in (0, 1):
            for k, (first_k, column_k) in columns[output].items():
                gradient[output][k] = column_k @ base[first_k:first_k + len(column_k), output]
                for j, (first_j, column_j) in columns[output].items():
                    low, high = max(first_k, first_j), min(first_k + len(column_k), first_j + len(column_j))
                    if j >= k and high > low:
                        normal[output][k, j] = normal[output][j, k] = column_k[low - first_k:high - first_k] @ column_j[low - first_j:high - first_j]
        return normal, gradient

    def refine(self, curve, iterations: int = 30, report=None):
        """Levenberg-Marquardt steps on both outputs at once; each output keeps its own damping."""
        penalty = curve.penalties(len(self.reference))
        cost_of = lambda residual, vectors: np.array([np.sum(residual[:, o] ** 2) + vectors[o] @ penalty[o] @ vectors[o] for o in (0, 1)])
        base = self.residual(curve)
        cost = cost_of(base, curve.parameters)
        damping = np.full(2, 1e-3)
        for iteration in range(iterations):
            normal, gradient = self.normal_equations(curve, base)
            trial_vectors = []
            for output in (0, 1):
                matrix = normal[output] + penalty[output]
                matrix = matrix + damping[output] * np.diag(np.diag(matrix) + 1e-12)
                step = np.linalg.solve(matrix, -(gradient[output] + penalty[output] @ curve.parameters[output]))
                trial_vectors.append(curve.parameters[output] + step)
            trial_base = self.residual(curve.with_parameters(trial_vectors))
            better = cost_of(trial_base, trial_vectors) < cost
            vectors = [trial_vectors[o] if better[o] else curve.parameters[o] for o in (0, 1)]
            base = np.where(better[None, :], trial_base, base)
            curve = curve.with_parameters(vectors)
            previous, cost = cost, cost_of(base, vectors)
            damping = np.where(better, np.maximum(damping / 10.0, 1e-9), damping * 10.0)
            if report is not None:
                report(iteration, cost, better)
            if np.all((previous - cost) < 1e-7 * previous) and np.all(better | (damping > 1e3)):
                break
        return curve


def cheapest_path(cost: np.ndarray, spacing: float, tolerated: float = 0.02) -> np.ndarray:
    """Index path through cost[time][level] with the smallest total; a jump of `tolerated` cycles costs
    twice the median best cost and larger jumps quadratically more. The level axis is cyclic."""
    steps = cost.shape[1]
    distance = np.abs(np.arange(steps)[:, None] - np.arange(steps)[None, :])
    distance = np.minimum(distance, steps - distance) * spacing
    penalty = 2.0 * np.median(cost.min(axis=1)) * (distance / tolerated) ** 2
    total = cost[0].copy()
    back = np.zeros(cost.shape, dtype=int)
    for k in range(1, len(cost)):
        options = total[None, :] + penalty
        back[k] = np.argmin(options, axis=1)
        total = options[np.arange(steps), back[k]] + cost[k]
    path = [int(np.argmin(total))]
    for k in range(len(cost) - 1, 0, -1):
        path.append(int(back[k][path[-1]]))
    return np.array(path[::-1])


# ---------------------------------------------------------------- score 1: the phase-fitted null

def stretch_nulls(candidate: np.ndarray, reference: np.ndarray, sample_rate: int, stretches) -> dict:
    result = {"overallDb": null_db(candidate, reference),
              "leftDb": null_db(candidate[:, 0], reference[:, 0]), "rightDb": null_db(candidate[:, 1], reference[:, 1])}
    for name, start, stop in stretches:
        window = slice(int(start * sample_rate), int(stop * sample_rate))
        result[name] = null_db(candidate[window], reference[window])
    return result


def score_null_case(candidate, case: NullCase, refit: int) -> dict:
    """One case: the null with the stored curve and after `refit` steps of the candidate's own fit.

    Without a stored curve the candidate finds a spline from its own level grid and takes at least 12 steps.
    """
    stimulus, reference = reference_of(case)
    origin = int(round(case.warmup * case.sample_rate))
    model = lambda phase: np.asarray(candidate.render(stimulus, case.sample_rate, case.warmup, case.decay, case.size, case.macro, 0,
                                                      phase=phase), dtype=np.float64)[:len(stimulus)]
    fit = PhaseFit(model, reference, case.sample_rate, origin)
    curve = stored_curve(case)
    entry = {"case": case.key, "storedCurve": curve is not None}
    if curve is None:
        curve = fit.start(case.warmup, PHASE_STEP, float(tide_phase.rate(case.macro / 100.0)))
        refit = max(refit, 12)
    else:
        entry["withStoredCurve"] = stretch_nulls(model(curve), reference, case.sample_rate, case.stretches)
    if refit:
        curve = fit.refine(curve, refit)
    entry.update(stretch_nulls(model(curve), reference, case.sample_rate, case.stretches))
    entry["refitSteps"] = refit
    entry["curve"] = curve.as_json()
    return entry


def score_null(candidate, refit: int = 2, cases=None) -> dict:
    result = {"cases": [score_null_case(candidate, NULL_CASES[index], refit) for index in (range(len(NULL_CASES)) if cases is None else cases)]}
    for macro in sorted({entry["case"].split("/")[5] for entry in result["cases"]}, key=float, reverse=True):
        scores = [entry["overallDb"] for entry in result["cases"] if entry["case"].split("/")[5] == macro]
        result[f"macro{macro}"] = {"worstOverallDb": max(scores), "meanOverallDb": round(float(np.mean(scores)), 2), "cases": len(scores)}
    return result


# ---------------------------------------------------------------- score 2: free-running statistics

def statistics_case(job: tuple) -> tuple:
    """One target case rendered for every seed and compared with its targets; job is (candidate path, case name, seeds)."""
    path, name, seeds = job
    candidate = load_candidate(Path(path))
    targets = json.loads(TARGETS.read_text())
    case = targets["cases"][name]
    stimulus = descriptors.build_stimulus(case["stimulus"])
    settings = case["settings"]
    outputs = [np.asarray(candidate.render(stimulus, targets["sampleRate"], revocean.WARMUP_SECONDS, settings["decaySeconds"],
                                           settings["sizePercent"], settings["macroPercent"], seed), dtype=np.float32) for seed in seeds]
    return name, descriptors.compare_to_targets({name: outputs}, targets, latency=targets["latency"])["cases"][name]


def summarise_statistics(cases: dict) -> dict:
    """Per descriptor over all cases, groups and elements: rms and largest difference in the reference's
    spreads, rms in errors of the means, and the median ratio of the candidate's spread to the reference's."""
    collected = {}
    for groups in cases.values():
        for group in groups.values():
            for key, entry in group.items():
                values = {field: np.array([np.nan if v is None else v for v in entry[field]]) for field in ("inSpreads", "inErrors", "spreadRatio")}
                finite = np.isfinite(values["inSpreads"]) & np.isfinite(values["inErrors"])
                collected.setdefault(key, []).append([values[field][finite] for field in ("inSpreads", "inErrors", "spreadRatio")])
    summary = {}
    for key, parts in collected.items():
        spreads, errors, ratio = (np.concatenate(column) for column in zip(*parts))
        if len(spreads):
            ratio = ratio[np.isfinite(ratio)]
            summary[key] = {"rmsInSpreads": round(float(np.sqrt(np.mean(spreads ** 2))), 3), "maxInSpreads": round(float(np.abs(spreads).max()), 3),
                            "rmsInErrors": round(float(np.sqrt(np.mean(errors ** 2))), 3),
                            "medianSpreadRatio": round(float(np.median(ratio)), 3) if len(ratio) else None, "elements": int(len(spreads))}
    return summary


def score_statistics(path: Path, macro: float, seeds: int = 8, workers: int = 4) -> dict:
    """compare_to_targets for every target case at one Macro value, `seeds` instances of the candidate."""
    targets = json.loads(TARGETS.read_text())
    names = [name for name, case in targets["cases"].items() if case["settings"]["macroPercent"] == macro]
    jobs = [(str(path), name, list(range(seeds))) for name in names]
    with ProcessPoolExecutor(workers) as pool:
        cases = dict(pool.map(statistics_case, jobs))
    noise = sum(name.endswith("_noise") for name in cases)
    result = {"macroPercent": macro, "seeds": seeds, "summary": summarise_statistics(cases),
              "byCase": {name: summarise_statistics({name: groups}) for name, groups in cases.items()},
              "impulseCases": len(cases) - noise, "noiseCases": noise}
    reference = targets["selfScores"].get(f"macro{macro:g}")
    if reference is not None:
        result["referenceAgainstItself"] = {key: reference[key]["rmsInSpreads"] for key in reference}
    return result


# ---------------------------------------------------------------- score 3: the level cycle

def level_stimulus() -> np.ndarray:
    """White noise of +-0.25 on both inputs from 1 s to 51 s (the stimulus of findings/io_verification.md, C2)."""
    generator = np.random.default_rng(LEVEL_SEED)
    stimulus = np.zeros((int(LEVEL_SECONDS * LEVEL_RATE), 2), np.float32)
    start, stop = int(LEVEL_START * LEVEL_RATE), int(LEVEL_STOP * LEVEL_RATE)
    stimulus[start:stop] = generator.uniform(-LEVEL_PEAK, LEVEL_PEAK, (stop - start, 2)).astype(np.float32)
    return stimulus


def start_noise() -> np.ndarray:
    """100 s of Gaussian noise, rms 0.1, clipped at 0.5: the stimulus of packet tide_phase's 40 instances."""
    values = 0.1 * np.random.default_rng(1).standard_normal((int(START_SECONDS * LEVEL_RATE), 2))
    return np.clip(values, -0.5, 0.5).astype(np.float32)


# name: (stimulus, warm-up in seconds, reference realisations, first and last second of steady noise)
LEVEL_SETS = {
    "afterWarmup": (level_stimulus, revocean.WARMUP_SECONDS, 8, (int(LEVEL_START) + 2, int(LEVEL_STOP))),
    "fromInstanceStart": (start_noise, 0.0, 40, (2, int(START_SECONDS))),
}


def level_per_second(output: np.ndarray, sample_rate: int = LEVEL_RATE) -> np.ndarray:
    """Level of both outputs together in each whole second, dB re full scale."""
    seconds = len(output) // sample_rate
    power = (np.asarray(output[:seconds * sample_rate], dtype=np.float64) ** 2).reshape(seconds, -1).mean(axis=1)
    return 10.0 * np.log10(np.maximum(power, 1e-300))


def cycle_descriptors(steady: np.ndarray) -> dict:
    """Numbers of one level curve over its seconds of steady noise.

    Period: lag of the highest autocorrelation between its first zero crossing and 30 s.
    """
    centred = steady - steady.mean()
    correlation = np.correlate(centred, centred, "full")[len(centred) - 1:] / np.sum(centred ** 2)
    negative = np.nonzero(correlation < 0.0)[0]
    lags = np.arange(negative[0], min(len(correlation), 30)) if len(negative) else np.array([0])
    return {"floorDb": float(np.percentile(steady, 5)), "topDb": float(np.percentile(steady, 95)),
            "swingDb": float(np.percentile(steady, 95) - np.percentile(steady, 5)),
            "meanPowerDb": float(10.0 * np.log10(np.mean(10.0 ** (steady / 10.0)))),
            "medianDb": float(np.median(steady)), "periodSeconds": float(lags[np.argmax(correlation[lags])])}


def ensemble(levels: np.ndarray) -> dict:
    """Mean and standard deviation of every cycle descriptor over an ensemble of steady level curves."""
    rows = [cycle_descriptors(level) for level in levels]
    return {key: {"mean": round(float(np.mean([row[key] for row in rows])), 3), "sd": round(float(np.std([row[key] for row in rows], ddof=1)), 3)}
            for key in rows[0]}


def level_curve(job: tuple) -> np.ndarray:
    """The level per second of one reference realisation (path None) or of one seed of the candidate at `path`;
    job is (path, name of the set in LEVEL_SETS, Macro in percent, realisation or seed)."""
    path, name, macro, index = job
    build, warmup, _, _ = LEVEL_SETS[name]
    stimulus = build()
    if path is None:
        output = revocean.capture(stimulus, settings_of(0.5, 100.0, macro), sample_rate=LEVEL_RATE, warmup=warmup, realisation=index).output
    else:
        output = load_candidate(Path(path)).render(stimulus, LEVEL_RATE, warmup, 0.5, 100.0, macro, index)
    return level_per_second(output[:len(stimulus)])


def score_level_set(path: Path, name: str, seeds: int, workers: int) -> dict:
    """One stimulus at Macro 100 %, Decay 0.5 s: the candidate's seeds against the reference's realisations.

    `curveRmsZ` compares the two ensemble means second by second, in standard errors of their difference;
    1 is what two draws of one process give.
    """
    _, _, realisations, (first, last) = LEVEL_SETS[name]
    with ProcessPoolExecutor(workers) as pool:
        reference = np.array(list(pool.map(level_curve, [(None, name, 100.0, index) for index in range(realisations)])))[:, first:last]
        ours = np.array(list(pool.map(level_curve, [(str(path), name, 100.0, seed) for seed in range(seeds)])))[:, first:last]
    result = {"realisations": realisations, "seeds": seeds, "steadySeconds": [first, last], "reference": ensemble(reference), "candidate": ensemble(ours)}
    spread = lambda key: max(result["reference"][key]["sd"], 1e-9) * np.sqrt(1.0 / realisations + 1.0 / seeds)
    result["differenceInStandardErrors"] = {key: round((result["candidate"][key]["mean"] - entry["mean"]) / spread(key), 2)
                                            for key, entry in result["reference"].items()}
    error = np.sqrt(reference.var(axis=0, ddof=1) / realisations + ours.var(axis=0, ddof=1) / seeds)
    result["curveRmsZ"] = round(float(np.sqrt(np.mean(((ours.mean(axis=0) - reference.mean(axis=0)) / error) ** 2))), 2)
    result["curveRmsDifferenceDb"] = round(float(np.sqrt(np.mean((ours.mean(axis=0) - reference.mean(axis=0)) ** 2))), 3)
    result["meanCurveDb"] = {"reference": [round(float(v), 2) for v in reference.mean(axis=0)], "candidate": [round(float(v), 2) for v in ours.mean(axis=0)]}
    result["sdCurveDb"] = {"reference": [round(float(v), 2) for v in reference.std(axis=0, ddof=1)], "candidate": [round(float(v), 2) for v in ours.std(axis=0, ddof=1)]}
    return result


def score_level(path: Path, seeds: int = 8, workers: int = 4) -> dict:
    """The level cycle under steady noise at Macro 100 %: after a 10 s warm-up (8 reference realisations of
    this packet) and from the instance start (the 40 cached instances of packet tide_phase)."""
    result = {name: score_level_set(path, name, seeds, workers) for name in LEVEL_SETS}
    _, _, _, (first, last) = LEVEL_SETS["afterWarmup"]
    rest = [level_curve((source, "afterWarmup", 0.0, 0))[first:last].mean() for source in (None, str(path))]
    result["macro0LevelDb"] = {"reference": round(float(rest[0]), 3), "candidate": round(float(rest[1]), 3)}
    return result


# ---------------------------------------------------------------- command line

def load_candidate(path: Path):
    spec = importlib.util.spec_from_file_location("candidate", path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("candidate", type=Path, help="module with render(stimulus, sample_rate, warmup_seconds, decay_seconds, "
                                                     "size_percent, macro_percent, seed, phase=None)")
    parser.add_argument("--null", action="store_true", help="score 1 only")
    parser.add_argument("--statistics", action="store_true", help="score 2 only")
    parser.add_argument("--level", action="store_true", help="score 3 only")
    parser.add_argument("--refit", type=int, default=2, help="steps of the candidate's own phase fit after the stored curve (default 2)")
    parser.add_argument("--case", type=int, action="append", help="score only this case of NULL_CASES (repeatable)")
    parser.add_argument("--seeds", type=int, default=8, help="instances of the candidate for scores 2 and 3 (default 8)")
    parser.add_argument("--macro", type=float, nargs="+", default=[100.0, 50.0], help="Macro values of score 2 (default 100 50)")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--json", type=Path, help="write the full report here")
    arguments = parser.parse_args()
    everything = not (arguments.null or arguments.statistics or arguments.level)
    revocean.identity()
    candidate = load_candidate(arguments.candidate)
    report = {}
    if everything or arguments.null:
        report["phaseFittedNull"] = score_null(candidate, arguments.refit, arguments.case)
    if everything or arguments.statistics:
        report["statistics"] = [score_statistics(arguments.candidate, macro, arguments.seeds, arguments.workers) for macro in arguments.macro]
    if everything or arguments.level:
        report["levelCycle"] = score_level(arguments.candidate, arguments.seeds, arguments.workers)
    if arguments.json:
        arguments.json.write_text(json.dumps(report, indent=1))
    brief = json.loads(json.dumps(report))
    for entry in brief.get("phaseFittedNull", {}).get("cases", []):
        entry.pop("curve")
    for entry in brief.get("statistics", []):
        entry.pop("byCase")
    for entry in brief.get("levelCycle", {}).values():
        for key in ("meanCurveDb", "sdCurveDb"):
            entry.pop(key, None)
    print(json.dumps(brief, indent=1))


if __name__ == "__main__":
    main()
