#!/usr/bin/env python3
"""Evidence behind tide_model.py, and the phase curves score_tide.py starts from.

    python fit_tide_model.py                      every section -> tide_structural_data/tide_model.json
    python fit_tide_model.py assignment rate      only these sections; the others are kept as they are in the file

tide_model.py has no fitted constant of its own besides the rate law of the phase; everything else comes
from network_model.py, tide_stage.py, converters.py and tide_phase.py. This script measures, on reference
captures of this packet (programme seed 8301, impulse trains, steady noise; no locked holdout is read):

assignment  which lines each voice filters. With the phase of each output as the only fitted quantity:
            the null of the whole response for every place of the split between the voices, the weight of
            every line in each voice by linear regression (own and cross-fed part apart), and the null when
            the cross-fed part follows the phase of its input's channel.
cases       the cases of score_tide.NULL_CASES: null of the whole response with the phase described by a
            spline with knots 0.5 s apart, by one with knots 0.25 s apart, and by the generator's own form
            (a rate, a hold and raised cosines between one knot per cell of tide_phase's grid). The one with
            the smallest weighted residual is stored as the curve the scorer starts from.
rate        the rate of the phase against Macro: captures that start 1 s after the instance, where the
            level of the phase still holds its start value and the phase is a straight line.
ablations   one element of the model changed at a time, phase refitted.
levelStart  the phase of the reference in the first seconds of the level-cycle captures.
levelGivenPhase  the level per second of the model driven with the phase packet tide_phase measured in
            eight of its instances, against those instances.
scores      score_tide.py on tide_model.py: phase-fitted null, free-running statistics, level cycle.

A phase is fitted by score_tide.PhaseFit; a null is 20 log10(rms(model - reference) / rms(reference)) with
nothing else fitted. With the captures cached a full run takes about 20 minutes on four cores.
"""
from __future__ import annotations

import itertools
import json
import sys
from concurrent.futures import ProcessPoolExecutor
from contextlib import contextmanager
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

import datasets
import network_model
import revocean
import score_tide as score
import tide_model as model
import tide_phase
import tide_stage

HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "tide_structural_data" / "tide_model.json"
SECTIONS = ("assignment", "cases", "rate", "ablations", "levelStart", "levelGivenPhase", "scores")
ASSIGNMENT_CASES = (0, 8, 1, 12)                    # indices of score_tide.NULL_CASES: Macro 100, 50, 100 and 25 %
SPLITS = (4, 6, 7, 8, 9, 10, 12)                    # lines 1 to k pass voice A
FINE_STEP = 0.25                                    # seconds between knots of the finer spline
HOLD_RATE, HOLD_WARMUP, HOLD_DECAY, HOLD_SIZE = 44100, 1.0, 2.0, 100.0
HOLD_MACROS = (100.0, 75.0, 50.0, 25.0, 10.0, 5.0, 2.0)
HOLD_REALISATIONS = (0, 1)
HOLD_FROM, HOLD_MARGIN = 3.0, 0.5                   # the straight part: from 3 s to 0.5 s before the first knot
HOLD_STRAIGHT = 2e-5                                # cycles rms; a hold that reads less straight is not used for the law
CELL_SLACK = 0.05                                   # seconds a knot may leave its cell in the first fit to a curve
LEVEL_START_SECONDS = 8.0
LEVEL_START_SEEDS = 40
LEVEL_INSTANCES = range(8)                           # instances of packet tide_phase's main set
WORKERS = 4


# ---------------------------------------------------------------- a capture with the model's parts at hand

class Bench:
    """A reference capture and the model's voice inputs for it; `model(curve)` is the model's output for a phase."""

    def __init__(self, stimulus, reference, sample_rate, warmup, decay, size, macro_percent, taps=None, block_shift: int = 0):
        self.stimulus, self.reference, self.sample_rate, self.warmup = stimulus, reference, int(sample_rate), float(warmup)
        self.decay, self.size, self.macro = float(decay), float(size), macro_percent / 100.0
        self.origin = int(round(warmup * sample_rate))
        self.block_shift = block_shift                      # samples by which the voices' blocks are moved (ablation)
        self.first_read, self.taps = model.voice_inputs(stimulus, sample_rate, warmup, decay, size, self.macro) if taps is None else taps
        self.fit = score.PhaseFit(self.model, reference, sample_rate, self.origin)

    @classmethod
    def of_case(cls, case: score.NullCase) -> "Bench":
        stimulus, reference = score.reference_of(case)
        return cls(stimulus, reference, case.sample_rate, case.warmup, case.decay, case.size, case.macro)

    def with_taps(self, first_read: int, taps: np.ndarray, block_shift: int = 0) -> "Bench":
        return Bench(self.stimulus, self.reference, self.sample_rate, self.warmup, self.decay, self.size, 100.0 * self.macro,
                     (first_read, taps), block_shift)

    def to_host(self, internal: np.ndarray) -> np.ndarray:
        return model.output(internal, self.first_read, self.sample_rate, self.origin, self.origin + len(self.stimulus))

    def model(self, curve) -> np.ndarray:
        return self.to_host(model.voices(self.taps, self.first_read + self.block_shift, curve, self.macro))

    def spline(self, step: float, start=None):
        """The fitted spline with knots `step` apart, started from the curve `start` or else from the level grid."""
        if start is None:
            start = self.fit.start(self.warmup, step, model.phase_rate(self.macro))
        elif not (isinstance(start, score.SplineCurve) and start.step == step):
            seconds = self.warmup + np.arange(0.0, len(self.stimulus) / self.sample_rate, 0.01)
            start = score.SplineCurve.through(self.warmup, step, int(np.ceil(len(self.stimulus) / self.sample_rate / step)), seconds, start(seconds))
        return self.fit.refine(start)

    def nulls(self, curve, stretches=score.PROGRAMME_STRETCHES) -> dict:
        return score.stretch_nulls(self.model(curve), self.reference, self.sample_rate, stretches)


def line_taps(bench: Bench) -> tuple:
    """(first_read, taps[frame][output][line][input]): the output tap of every line, split by the input it came from."""
    first_read, driven = model.network_drive(bench.stimulus, bench.sample_rate, bench.warmup, bench.macro)
    single = [[line] for line in range(1, network_model.LINES + 1)]
    parts = []
    for channel in (0, 1):
        only = np.zeros_like(driven)
        only[:, channel] = driven[:, channel]
        parts.append(model.line_taps(only, first_read, bench.decay, bench.size, single))
    return first_read, np.stack(parts, axis=-1)


# ---------------------------------------------------------------- which lines each voice filters

def split_taps(lines: np.ndarray, first_voice) -> np.ndarray:
    """taps[frame][output][voice] when the lines `first_voice` (indices from 0) pass voice A and the others voice B."""
    both = lines.sum(axis=3)
    chosen = np.zeros(network_model.LINES, dtype=bool)
    chosen[list(first_voice)] = True
    return np.stack([both[:, :, chosen].sum(axis=2), both[:, :, ~chosen].sum(axis=2)], axis=2)


def free_weights(bench: Bench, lines: np.ndarray, curve) -> list:
    """Per output: least-squares weight of every line in each voice, own and cross-fed part apart (64 numbers),
    for the phase `curve`. The model has 1 for lines 1 to 8 in voice A and for lines 9 to 16 in voice B, else 0."""
    expected = np.concatenate([np.ones(8), np.zeros(16), np.ones(8)] * 2)
    result = []
    for output in (0, 1):
        columns = []
        for part in (output, 1 - output):                       # the output's own input, then the other input
            for offset in (0.0, model.VOICE_B_OFFSET):
                for line in range(network_model.LINES):
                    internal = np.zeros((lines.shape[0], 2))
                    internal[:, output] = model.voice(lines[:, output, line, part], bench.first_read,
                                                      lambda seconds: curve(seconds)[output] + offset, bench.macro)
                    columns.append(bench.to_host(internal)[:, output] * bench.fit.weight[:, output])
        design = np.stack(columns, axis=1)
        target = bench.reference[:, output] * bench.fit.weight[:, output]
        solution = np.linalg.lstsq(design, target, rcond=None)[0]
        residual = design @ solution - target
        sigma = np.sqrt(np.diag(np.linalg.inv(design.T @ design)) * np.sum(residual ** 2) / (len(target) - len(solution)))
        result.append({
            "weights": {name: [round(float(v), 6) for v in row] for name, row in zip(("ownA", "ownB", "crossA", "crossB"), solution.reshape(4, -1))},
            "largestDeviationFromModel": float(f"{np.abs(solution - expected).max():.3g}"),
            "largestStandardError": float(f"{sigma.max():.3g}"),
            "weightedResidualDb": {"model": score.null_db(design @ expected, target), "free": score.null_db(design @ solution, target)}})
    return result


def cross_follows_input(bench: Bench, lines: np.ndarray, curve) -> list:
    """Null per output when the part of an output that came from the other input is filtered with the
    phase of that input's channel (and the own part with the output's phase)."""
    swapped = lambda seconds: curve(seconds)[::-1]
    nulls = []
    for output in (0, 1):
        own = np.zeros(lines.shape[:3] + (2,))
        cross = np.zeros_like(own)
        own[:, output, :, output] = lines[:, output, :, output]
        cross[:, output, :, 1 - output] = lines[:, output, :, 1 - output]
        internal = model.voices(split_taps(own, range(8)), bench.first_read, curve, bench.macro) \
            + model.voices(split_taps(cross, range(8)), bench.first_read, swapped, bench.macro)
        nulls.append(score.null_db(bench.to_host(internal)[:, output], bench.reference[:, output]))
    return nulls


def assignment(index: int) -> dict:
    case = score.NULL_CASES[index]
    bench = Bench.of_case(case)
    first_read, lines = line_taps(bench)
    curve = bench.spline(FINE_STEP)
    per_output = lambda b, c: [score.null_db(b.model(c)[:, o], b.reference[:, o]) for o in (0, 1)]
    result = {"case": case.key, "knotSeconds": FINE_STEP, "nullPerOutputDb": per_output(bench, curve), "splitAfterLine": {}}
    for split in SPLITS:
        other = bench.with_taps(first_read, split_taps(lines, range(split)))
        result["splitAfterLine"][str(split)] = per_output(other, other.spline(FINE_STEP, curve))
    exchanged = bench.with_taps(first_read, split_taps(lines, range(8, 16)))
    result["voicesExchanged"] = per_output(exchanged, exchanged.spline(FINE_STEP))
    single = bench.with_taps(first_read, split_taps(lines, range(16)))
    result["oneVoiceForAllLines"] = per_output(single, single.spline(FINE_STEP))
    result["freeWeights"] = free_weights(bench, lines, curve)
    result["crossFedPartWithPhaseOfItsInput"] = cross_follows_input(bench, lines, curve)
    return result


# ---------------------------------------------------------------- the phase in the generator's own form

def level_basis(times: np.ndarray, seconds: np.ndarray) -> np.ndarray:
    """basis[knot][sample]: a level that eases from knot to knot is sum over knots of value x basis."""
    basis = np.zeros((len(times), len(seconds)))
    if len(times) == 1:
        basis[0] = 1.0
        return basis
    index = np.clip(np.searchsorted(times, seconds, side="right") - 1, 0, len(times) - 2)
    along = np.clip((seconds - times[index]) / (times[index + 1] - times[index]), 0.0, 1.0)
    ease = 0.5 - 0.5 * np.cos(np.pi * along)
    sample = np.arange(len(seconds))
    np.add.at(basis, (index, sample), 1.0 - ease)
    np.add.at(basis, (index + 1, sample), ease)
    return basis


def ease_start(curve, start: float, stop: float, output: int, rate: float) -> tuple:
    """(rate, knot times, knot values, rms in cycles): the generator's form closest to one output of a fitted
    curve on [start, stop]. One knot per cell of tide_phase's grid. First a search over knot positions at
    the model's `rate` with the values solved linearly, then a least-squares fit of everything."""
    cell = 1.0 / tide_phase.GRID_HZ[output]
    seconds = np.arange(start, stop, 0.01)
    phase = curve(seconds)[output]
    cells = np.arange(max(1, int(np.floor(start / cell)) - 1), int(np.floor(stop / cell)) + 2)
    seen = [(k * cell < stop) and ((k + 1) * cell > start) for k in cells]
    grids = [(0.08, 0.25, 0.42, 0.58, 0.75, 0.92) if inside else (0.5,) for inside in seen]
    best = None
    for jitter in itertools.product(*grids):
        times = (cells + np.array(jitter)) * cell
        basis = level_basis(times, seconds).T
        values = np.linalg.lstsq(basis, phase - rate * seconds, rcond=None)[0]
        cost = float(np.sum((basis @ values + rate * seconds - phase) ** 2))
        if best is None or cost < best[0]:
            best = (cost, times, values)
    _, times, values = best
    count = len(times)
    centre = 0.5 * (tide_phase.LEVEL_LOW[output] + tide_phase.LEVEL_HIGH[output])

    def residual(vector):
        # the weak pull to the middle of the range only decides knots the window does not show
        difference = vector[0] * seconds + level_basis(vector[1:1 + count], seconds).T @ vector[1 + count:] - phase
        return np.concatenate([difference, 1e-4 * (vector[1 + count:] - centre)])
    first = np.concatenate([[rate], times, np.clip(values, centre - 0.4, centre + 0.4)])
    lower = np.concatenate([[0.98 * rate], cells * cell - CELL_SLACK, np.full(count, -np.inf)])
    upper = np.concatenate([[1.02 * rate], (cells + 1) * cell + CELL_SLACK, np.full(count, np.inf)])
    fitted = least_squares(residual, first, bounds=(lower, upper), x_scale="jac")
    times, values = fitted.x[1:1 + count], fitted.x[1 + count:]
    keep = slice(max(0, int(np.searchsorted(times, start, side="right")) - 1), int(np.searchsorted(times, stop)) + 1)
    return fitted.x[0], times[keep], values[keep], float(np.sqrt(np.mean(fitted.fun[:len(seconds)] ** 2)))


def eases(bench: Bench, spline) -> tuple:
    """(fitted EaseCurve, rms of its first form against the spline per output, in cycles).

    The first form follows the spline where the reference is heard; later a spline carries no information.
    """
    heard = [bench.fit.time[np.nonzero(bench.fit.weight[:, output] > 0.0)[0][[0, -1]]] for output in (0, 1)]
    parts = [ease_start(spline, heard[output][0], heard[output][1], output, model.phase_rate(bench.macro)) for output in (0, 1)]
    first = score.EaseCurve.of([p[0] for p in parts], [p[1] for p in parts], [p[2] for p in parts])
    return bench.fit.refine(first, 40), [float(f"{p[3]:.3g}") for p in parts]


def fitted_case(case: score.NullCase) -> tuple:
    """(evidence entry, curve the scorer starts from) of one case."""
    bench = Bench.of_case(case)
    coarse = bench.spline(score.PHASE_STEP)
    fine = bench.spline(FINE_STEP, coarse)
    eased, closeness = eases(bench, fine)
    curves = {"spline, knots 0.5 s": coarse, "spline, knots 0.25 s": fine, "generator form": eased}
    nulls = {name: bench.nulls(curve, case.stretches) for name, curve in curves.items()}
    # the scorer starts from the curve with the smallest weighted residual, the quantity every fit minimises
    stored = min(curves, key=lambda name: float(np.sum(bench.fit.residual(curves[name]) ** 2)))
    entry = {"case": case.key, "nulls": nulls, "generatorFormAgainstFineSplineRmsCycles": closeness,
             "numbersPerOutput": {name: [len(vector) for vector in curve.parameters] for name, curve in curves.items()},
             "generatorForm": eased.as_json(), "stored": stored}
    return entry, curves[stored].as_json()


# ---------------------------------------------------------------- the rate of the phase

def hold_bench(macro_percent: float, realisation: int) -> Bench:
    stimulus = datasets.network_programme(HOLD_RATE, score.PROGRAMME_SEED)
    captured = revocean.capture(stimulus, score.settings_of(HOLD_DECAY, HOLD_SIZE, macro_percent), sample_rate=HOLD_RATE,
                                warmup=HOLD_WARMUP, realisation=realisation)
    return Bench(stimulus, captured.output[:len(stimulus)].astype(np.float64), HOLD_RATE, HOLD_WARMUP, HOLD_DECAY, HOLD_SIZE, macro_percent)


def hold_rates(job: tuple) -> dict:
    """The straight part of the hold of both outputs of one capture: rate, straightness, and where the first second lies."""
    macro_percent, realisation = job
    bench = hold_bench(macro_percent, realisation)
    spline = bench.spline(FINE_STEP)
    eased, _ = eases(bench, spline)
    outputs = []
    for output in (0, 1):
        knot = float(eased.parts(output)[1][0])
        seconds = np.arange(HOLD_FROM, knot - HOLD_MARGIN, 0.01)
        phase = spline(seconds)[output]
        line = np.polyfit(seconds, phase, 1)
        outputs.append({"rate": round(float(line[0]), 8), "level": round(float(line[1]), 6), "firstKnotSeconds": round(knot, 3),
                        "rmsCycles": float(f"{np.sqrt(np.mean((np.polyval(line, seconds) - phase) ** 2)):.3g}"),
                        "at1.5sCycles": float(f"{spline(np.array([1.5]))[output, 0] - np.polyval(line, 1.5):.3g}")})
    return {"macroPercent": macro_percent, "realisation": realisation, "outputs": outputs,
            "nullDb": {"spline, knots 0.25 s": bench.nulls(spline), "generator form": bench.nulls(eased)}}


def rate_law(captures: list) -> dict:
    """rate = A + B e^(k Macro) through the mean rate per Macro value, free and with the round k of the model."""
    table = {}
    for entry in captures:
        straight = [out["rate"] for out in entry["outputs"] if out["rmsCycles"] < HOLD_STRAIGHT]
        table.setdefault(entry["macroPercent"], []).extend(straight)
    macro = np.array(sorted(table)) / 100.0
    mean = np.array([np.mean(table[m]) for m in sorted(table)])
    spread = [float(f"{np.ptp(table[m]):.2g}") for m in sorted(table)]
    form = lambda p, m: p[0] + p[1] * np.exp(p[2] * m)
    free = least_squares(lambda p: (form(p, macro) - mean) * 1e7, [0.043, 0.007, 0.6]).x
    design = np.vstack([np.ones_like(macro), np.exp(model.RATE_SHAPE * macro)]).T
    fixed = np.linalg.lstsq(design, mean, rcond=None)[0]
    ours = np.array([model.phase_rate(m) for m in macro])
    return {"macroPercent": [float(m) for m in sorted(table)], "meanRate": [round(float(v), 8) for v in mean],
            "readings": [len(table[m]) for m in sorted(table)], "rangeOfReadings": spread,
            "free": {"A": round(float(free[0]), 8), "B": round(float(free[1]), 8), "k": round(float(free[2]), 5),
                     "residual": [float(f"{v:.2g}") for v in form(free, macro) - mean]},
            "roundShape": {"k": model.RATE_SHAPE, "A": round(float(fixed[0]), 8), "B": round(float(fixed[1]), 8),
                           "atMacro0": round(float(fixed.sum()), 8), "atMacro100": round(float(fixed[0] + fixed[1] * np.exp(model.RATE_SHAPE)), 8),
                           "residual": [float(f"{v:.2g}") for v in design @ fixed - mean]},
            "modelMinusMeasured": [float(f"{v:.2g}") for v in ours - mean],
            "tidePhaseLawMinusMeasured": [float(f"{v:.2g}") for v in tide_phase.rate(macro) - mean]}


# ---------------------------------------------------------------- one element changed at a time

@contextmanager
def changed(module, name: str, value):
    original = getattr(module, name)
    setattr(module, name, value)
    try:
        yield
    finally:
        setattr(module, name, original)


def comb_first(bench: Bench) -> np.ndarray:
    """Voice inputs with the comb in front of the network's input equaliser instead of behind it."""
    data = network_model.constants()
    first, internal = network_model.network_input(bench.stimulus, bench.sample_rate, bench.warmup)
    driven = network_model.equalise(data, model.tide_input(internal, first, bench.macro))
    return model.line_taps(driven, bench.first_read, bench.decay, bench.size)


def ablations(job: tuple) -> dict:
    """Null of a capture that starts 1 s after the instance with one element of the model changed and the phase refitted."""
    macro_percent, realisation = job
    bench = hold_bench(macro_percent, realisation)
    curve = bench.spline(FINE_STEP)
    refitted = lambda other: other.nulls(other.spline(FINE_STEP, curve))["overallDb"]
    result = {"macroPercent": macro_percent, "realisation": realisation, "model": bench.nulls(curve)["overallDb"]}
    _, driven = model.network_drive(bench.stimulus, bench.sample_rate, bench.warmup, bench.macro)
    result["comb in front of the input equaliser"] = refitted(bench.with_taps(bench.first_read, comb_first(bench)))
    for shift in (1, -1):
        result[f"voice blocks {shift:+d} sample"] = refitted(bench.with_taps(bench.first_read, bench.taps, shift))
    with changed(tide_stage, "Q_SMOOTHING_BLOCKS", 1e-3):
        result["Q follows its target at once"] = refitted(bench)
    for offset in (0.499, 0.501):
        with changed(model, "VOICE_B_OFFSET", offset):
            result[f"voice B {offset} cycle from voice A"] = refitted(bench)
    for name, lines in (("line 8 in voice B", (tuple(range(1, 8)), tuple(range(8, 17)))), ("line 9 in voice A", (tuple(range(1, 10)), tuple(range(10, 17))))):
        result[name] = refitted(bench.with_taps(bench.first_read, model.line_taps(driven, bench.first_read, bench.decay, bench.size, lines)))
    return result


# ---------------------------------------------------------------- the first seconds of the level-cycle captures

def level_start() -> dict:
    """The first seconds of noise in the level-cycle captures: the output level of every reference
    realisation and of 40 seeds of the model in the second after the noise starts, the level of the phase
    (phase minus rate x time) of every realisation 1.5 s after the noise starts, and the model's output
    level for a constant level of the phase."""
    frames = int(LEVEL_START_SECONDS * score.LEVEL_RATE)
    stimulus = score.level_stimulus()[:frames]
    settings = score.settings_of(0.5, 100.0, 100.0)
    rate = model.phase_rate(1.0)
    at = revocean.WARMUP_SECONDS + score.LEVEL_START + 1.5
    span = np.array(tide_phase.LEVEL_HIGH) - np.array(tide_phase.LEVEL_LOW)
    first_second = int(score.LEVEL_START)
    rows, heard, bench = [], [], None
    for realisation in range(score.LEVEL_SETS["afterWarmup"][2]):
        reference = revocean.capture(score.level_stimulus(), settings, sample_rate=score.LEVEL_RATE, realisation=realisation).output[:frames]
        heard.append(float(score.level_per_second(reference)[first_second]))
        taps = None if bench is None else (bench.first_read, bench.taps)
        bench = Bench(stimulus, reference.astype(np.float64), score.LEVEL_RATE, revocean.WARMUP_SECONDS, 0.5, 100.0, 100.0, taps)
        curve = bench.spline(score.PHASE_STEP)
        level = curve(np.array([at]))[:, 0] - rate * at
        rows.append({"realisation": realisation, "nullDb": bench.nulls(curve, ())["overallDb"], "level": [round(float(v), 4) for v in level],
                     "placeInGeneratorRange": [round(float(v), 3) for v in (level - np.array(tide_phase.LEVEL_LOW)) / span]})
    window = slice(int(2.0 * score.LEVEL_RATE), int(4.0 * score.LEVEL_RATE))
    table = []
    for level in np.arange(-0.05, 0.651, 0.05):
        out = bench.model(lambda seconds, level=level: np.tile(level + rate * np.asarray(seconds), (2, 1)))[window]
        table.append([round(float(level), 2)] + [round(float(10.0 * np.log10(np.mean(out[:, o] ** 2))), 2) for o in (0, 1)])
    seeds = [float(score.level_per_second(bench.model(model.generator(seed, 1.0)))[first_second]) for seed in range(LEVEL_START_SEEDS)]
    second = {"reference": [round(v, 2) for v in heard], "modelSeedsPercentiles5to95": [round(float(v), 2) for v in np.percentile(seeds, [5, 25, 50, 75, 95])],
              "modelSeedsMoreThan1DbAboveTheHighestReference": int(np.sum(np.array(seeds) > max(heard) + 1.0)), "modelSeeds": LEVEL_START_SEEDS}
    return {"instanceSeconds": at, "realisations": rows, "levelDbInTheFirstSecondOfNoise": second,
            "modelLevelDbAgainstPhaseLevel": {"columns": ["level", "left", "right"], "rows": table}}


def level_given_phase(index: int) -> float:
    """One of packet tide_phase's 40 instances, the model driven with the phase that packet measured in it:
    rms over 2 to 100 s of the model's level per second minus the reference's, in dB."""
    data = np.load(tide_phase.DATA)
    seconds, measured = data["main_t"], data["main_phase"][:, index].astype(np.float64)
    curve = lambda t: np.stack([np.interp(t, seconds, row) for row in measured])
    ours = score.level_per_second(model.render(score.start_noise(), score.LEVEL_RATE, 0.0, 0.5, 100.0, 100.0, 0, phase=curve))
    theirs = score.level_curve((None, "fromInstanceStart", 100.0, index))
    return round(float(np.sqrt(np.mean((ours[2:] - theirs[2:]) ** 2))), 3)


# ---------------------------------------------------------------- the scores of the model

def scores() -> dict:
    null = score.score_null(model, refit=2)
    for entry in null["cases"]:
        entry.pop("curve")
    statistics = [score.score_statistics(HERE / "tide_model.py", macro) for macro in (100.0, 50.0)]
    return {"phaseFittedNull": null, "statistics": statistics, "levelCycle": score.score_level(HERE / "tide_model.py", seeds=40)}


def main() -> None:
    wanted = sys.argv[1:] or list(SECTIONS)
    unknown = [name for name in wanted if name not in SECTIONS]
    if unknown:
        raise SystemExit(f"unknown section {unknown[0]!r}; sections are {', '.join(SECTIONS)}")
    revocean.identity()
    data = json.loads(OUTPUT.read_text()) if OUTPUT.exists() else {}
    data["model"] = {"voiceLines": [list(lines) for lines in model.VOICE_LINES], "voiceBOffsetCycles": model.VOICE_B_OFFSET,
                     "phaseRateCyclesPerSecond": {"atMacro0": model.RATE_LOW, "atMacro100": model.RATE_HIGH, "expShape": model.RATE_SHAPE},
                     "programmeSeed": score.PROGRAMME_SEED, "trainSeed": score.TRAIN_SEED}
    with ProcessPoolExecutor(WORKERS) as pool:
        if "assignment" in wanted:
            data["assignment"] = list(pool.map(assignment, ASSIGNMENT_CASES))
        if "cases" in wanted:
            fitted = list(pool.map(fitted_case, score.NULL_CASES))
            data["cases"] = [entry for entry, _ in fitted]
            data["phaseCurves"] = {case.key: curve for case, (_, curve) in zip(score.NULL_CASES, fitted)}
        if "rate" in wanted:
            captures = list(pool.map(hold_rates, [(macro, realisation) for macro in HOLD_MACROS for realisation in HOLD_REALISATIONS]))
            data["rate"] = {"captures": captures, "law": rate_law(captures)}
        if "ablations" in wanted:
            data["ablations"] = list(pool.map(ablations, [(100.0, 0), (50.0, 0)]))
        if "levelGivenPhase" in wanted:
            data["levelGivenPhase"] = {"instances": list(LEVEL_INSTANCES), "rmsDb": list(pool.map(level_given_phase, LEVEL_INSTANCES))}
    if "levelStart" in wanted:
        data["levelStart"] = level_start()
    if "scores" in wanted:
        OUTPUT.write_text(json.dumps(data, indent=1))            # the scorer reads the phase curves from the file
        data["scores"] = scores()
    OUTPUT.write_text(json.dumps(data, indent=1))


if __name__ == "__main__":
    main()
