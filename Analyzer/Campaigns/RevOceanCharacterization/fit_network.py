#!/usr/bin/env python3
"""Constants and evidence of network_model.py, the campaign's model of the network at Macro 0.

    python fit_network.py             constants and evidence -> tide_structural_data/network.json (2 minutes once the captures are cached)
    python fit_network.py captures    render this packet's reference captures only

The structure and its round values are those packets network_a and network_b
agree on, or that this packet's captures decided between them (STRUCTURE). One
number is fitted: the input gain of line 1, as a least-squares scale on five
cached single-impulse captures of packet network_b.

Everything else is evidence on captures neither author used (programme seed
60606, settings of FRESH_CASES, a 100 s tail, Size at a rounding tie) and on
network_a's cached 100 s tails:

* nulls of the model, of network_a and of network_b per case and stretch;
* every element in which network_a differs, swapped into the model one at a time;
* elements both authors agree on, changed one at a time;
* other expressions of the single-precision line length;
* the 100 s tails: null, gain and time shift along the tail;
* a free fit of all constants (one Gauss-Newton step from the round values),
  scored where it was not fitted;
* the display law of Size at a rounding tie;
* the fitted gain against its closed-form candidate.

The locked holdout is not read here; score it with `python score_network.py network_model.py`.
A null is 20 log10(rms(model - reference) / rms(reference)), nothing fitted.
"""
from __future__ import annotations

import copy
import json
import math
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.signal import lfilter, tf2sos

import datasets
import network_a
import network_b
import network_model as model
import revocean

HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "tide_structural_data" / "network.json"
FIRST_ORDER = json.loads((HERE / "tide_structural_data" / "first_order.json").read_text())
RATE = model.INTERNAL_RATE
LINES = model.LINES
PROGRAMME_SEED = 60606                     # the holdout uses 20261007, network_a 777, network_b 7302

# The model. Round values throughout; input.gain.first is the one fitted number (fit_scale).
STRUCTURE = {
    "internal_rate_hz": RATE,
    # display laws the plug-in declares to the host: low + (high - low)(e^(shape x) - 1)/(e^shape - 1)
    "parameters": {"decay": {"low": 0.5, "high": 60.0, "shape": 4.4}, "size": {"low": 30.0, "high": 200.0, "shape": 0.71337}},
    "lines": {"prime": FIRST_ORDER["lines"]["prime"], "width_flat": 0.272, "width_flat_lines": 6},
    "oscillator": {"rate_hz": 0.6, "depth_ms": 0.88, "phase_step_rad": 11.25},
    "input": {"delay_samples": 44,
              "equaliser": [{"kind": "peaking", "frequency_hz": 200.0, "gain_db": 0.5, "q": 0.4},
                            {"kind": "peaking", "frequency_hz": 1750.0, "gain_db": -0.5, "q": 0.4}],
              "gain": {"first": 0.244, "last_over_first": 0.236, "shape": 3.36}},
    "feedback": {"scale": 0.25,
                 "kernel": [{"kind": "low_shelf", "frequency_hz": 1221.0, "gain_db": -0.28, "q": 0.484},
                            {"kind": "high_shelf", "frequency_hz": 12840.0, "gain_db": -0.12, "q": 0.26}]},
    "output": {"low_pass": {"kind": "low_pass", "frequency_hz": 20000.0, "q": 1.0},
               "tap": {"decay_low_seconds": 0.5, "decay_knee_seconds": 6.0, "first": [0.46, 0.228], "last": [0.336, 0.428],
                       "last_decay_shape": -2.6, "early_shape": 4.0, "late_shape": 2.0}},
}
# (host rate, Decay s, Size %, warm-up s): settings neither author nor the holdout used
FRESH_CASES = (
    (44100, 0.65, 100.0, 10.0), (44100, 1.8, 73.0, 10.0), (44100, 3.7, 118.0, 6.1), (44100, 5.2, 181.0, 10.0),
    (44100, 11.0, 100.0, 10.0), (44100, 33.0, 54.0, 14.7), (44100, 0.85, 37.0, 10.0), (44100, 2.2, 134.0, 10.0),
    (48000, 0.65, 100.0, 10.0), (48000, 1.8, 73.0, 10.0), (48000, 3.7, 118.0, 6.1), (48000, 5.2, 181.0, 10.0),
    (48000, 11.0, 100.0, 10.0), (48000, 2.2, 134.0, 8.9),
    (96000, 1.8, 73.0, 10.0), (88200, 3.7, 118.0, 10.0),
)
INTERNAL_CASES = FRESH_CASES[:8]           # host at the internal rate: the network without converters
TAIL_CASE = (44100, 40.0, 140.0, 10.0)     # the programme followed for 100 s
TAIL_SECONDS = 100.0
AUTHOR_TAIL = (20.0, 100.0, 10.0)          # network_a's cached tails: Decay, Size, warm-up; 44.1 and 48 kHz
TIE_DECAY = 1.6
TIE_STEPS = (0, 1, 2, 3, 6)                # single-precision steps of the normalised Size below that of 50 %
SCALE_CASES = ((0, 2.0), (1, 2.0), (0, 8.0), (1, 8.0), (0, 20.0))    # network_b's cached impulses: (input channel, Decay s)
SCALE_SECONDS = {2.0: 14.0, 8.0: 20.0, 20.0: 30.0}
STRETCHES = ((0.25, 1.4), (1.4, 4.0), (4.0, 6.0), (6.0, 14.0))
TAIL_BINS = ((0, 2), (2, 5), (5, 10)) + tuple((start, start + 10) for start in range(10, 100, 10))
# stretches of a 100 s tail used by the free fit: the start and one second in every ten
TAIL_FIT_WINDOWS = ((0.0, 14.0),) + tuple((float(start), start + 1.0) for start in range(20, 100, 10)) + ((99.0, 100.0),)
# the free-fit kernel of network_b (its evidence.fit.freeSections), for comparison
NETWORK_B_FREE_KERNEL = [{"kind": "low_shelf", "frequency_hz": 1220.9960, "gain_db": -0.2800006, "q": 0.4839995},
                         {"kind": "high_shelf", "frequency_hz": 12840.099, "gain_db": -0.1200028, "q": 0.2599962}]
FREE_PARAMETERS = (
    ("equaliser 1 frequency", ("input", "equaliser", 0, "frequency_hz")), ("equaliser 1 gain", ("input", "equaliser", 0, "gain_db")),
    ("equaliser 1 Q", ("input", "equaliser", 0, "q")),
    ("equaliser 2 frequency", ("input", "equaliser", 1, "frequency_hz")), ("equaliser 2 gain", ("input", "equaliser", 1, "gain_db")),
    ("equaliser 2 Q", ("input", "equaliser", 1, "q")),
    ("low-pass frequency", ("output", "low_pass", "frequency_hz")), ("low-pass Q", ("output", "low_pass", "q")),
    ("loop low shelf frequency", ("feedback", "kernel", 0, "frequency_hz")), ("loop low shelf gain", ("feedback", "kernel", 0, "gain_db")),
    ("loop low shelf Q", ("feedback", "kernel", 0, "q")),
    ("loop high shelf frequency", ("feedback", "kernel", 1, "frequency_hz")), ("loop high shelf gain", ("feedback", "kernel", 1, "gain_db")),
    ("loop high shelf Q", ("feedback", "kernel", 1, "q")),
    ("input gain of line 1", ("input", "gain", "first")), ("input gain ratio line 16 / line 1", ("input", "gain", "last_over_first")),
    ("input gain curve shape", ("input", "gain", "shape")),
    ("tap of line 1 at Decay 0.5 s", ("output", "tap", "first", 0)), ("tap of line 1 at 6 s", ("output", "tap", "first", 1)),
    ("tap of line 16 at Decay 0.5 s", ("output", "tap", "last", 0)), ("tap of line 16 at 6 s", ("output", "tap", "last", 1)),
    ("tap curve shape, lines 1 to 9", ("output", "tap", "early_shape")), ("tap curve shape, lines 9 to 16", ("output", "tap", "late_shape")),
    ("Decay map shape of line 16", ("output", "tap", "last_decay_shape")), ("Decay knee", ("output", "tap", "decay_knee_seconds")),
)
FREE_STEP = 1e-4                           # relative finite-difference step of the Jacobian
FLOOR = 1e-30                              # -600 dBFS; the reference's output stops falling near 1e-36 (output_floor)
POOL = ThreadPoolExecutor(4)


# ---------------------------------------------------------------- captures

def settings(decay: float, size: float) -> dict:
    return {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}


def reference(stimulus: np.ndarray, setting: dict, rate: int = RATE, warmup: float = 10.0) -> np.ndarray:
    return revocean.capture(stimulus, setting, sample_rate=rate, warmup=warmup).output[:len(stimulus)].astype(np.float64)


def programme(rate: int, seconds: float | None = None) -> np.ndarray:
    """The scorer's kind of programme with this packet's seed, optionally followed by silence."""
    stimulus = datasets.network_programme(rate, PROGRAMME_SEED)
    if seconds is None:
        return stimulus
    padded = np.zeros((int(round(seconds * rate)), 2), np.float32)
    padded[:len(stimulus)] = stimulus
    return padded


def fresh_case(case: tuple) -> tuple:
    rate, decay, size, warmup = case
    stimulus = programme(rate)
    return stimulus, reference(stimulus, settings(decay, size), rate, warmup)


def tail_case() -> tuple:
    rate, decay, size, warmup = TAIL_CASE
    stimulus = programme(rate, TAIL_SECONDS)
    return stimulus, reference(stimulus, settings(decay, size), rate, warmup)


def author_tail(rate: int) -> tuple:
    """network_a's cached capture: two impulses at Decay 20 s followed for 100 s."""
    decay, size, warmup = AUTHOR_TAIL
    stimulus = revocean.impulses(100.0, [(int(0.3 * rate), 0, 0.5), (int(1.4 * rate), 1, 0.5)], rate)
    return stimulus, reference(stimulus, settings(decay, size), rate, warmup)


def impulse_case(channel: int, decay: float) -> tuple:
    """network_b's cached capture: one impulse 0.3 s into a 44.1 kHz capture."""
    stimulus = revocean.impulses(SCALE_SECONDS[decay], [(int(round(0.3 * RATE)), channel, 0.5)], sample_rate=RATE)
    return stimulus, reference(stimulus, settings(decay, 100.0))


def tie_normalised(steps: int) -> float:
    """The normalised Size `steps` single-precision steps below the one the host sends for 50 %."""
    value = np.float32(revocean.normalised("size", 50.0))
    for _ in range(steps):
        value = np.nextafter(value, np.float32(0.0))
    return float(value)


def tie_case(steps: int) -> tuple:
    stimulus = programme(RATE)
    return stimulus, reference(stimulus, {"decay": revocean.normalised("decay", TIE_DECAY), "size": tie_normalised(steps)})


def render_captures() -> None:
    jobs = [lambda c=case: fresh_case(c) for case in FRESH_CASES] + [lambda s=steps: tie_case(s) for steps in TIE_STEPS]
    jobs += [tail_case] + [lambda r=rate: author_tail(r) for rate in (44100, 48000)]
    jobs += [lambda c=channel, d=decay: impulse_case(c, d) for channel, decay in SCALE_CASES]
    with ThreadPoolExecutor(3) as pool:
        list(pool.map(lambda job: job(), jobs))


# ---------------------------------------------------------------- the model in pieces

def null_db(candidate: np.ndarray, target: np.ndarray) -> float | None:
    energy = float(np.sum(target ** 2))
    if energy == 0.0:
        return None
    return round(float(10.0 * np.log10(max(float(np.sum((candidate - target) ** 2)), 1e-300) / energy)), 2)


def stretch_nulls(candidate: np.ndarray, target: np.ndarray, rate: int, edges=STRETCHES) -> list:
    return [null_db(candidate[int(lo * rate):int(hi * rate)], target[int(lo * rate):int(hi * rate)]) for lo, hi in edges]


def render(data: dict, stimulus: np.ndarray, rate: int, warmup: float, decay: float, size: float, *,
           equaliser: tuple | None = None, equaliser_behind: bool = False, lengths=None, plugin_size: float | None = None,
           **replaced) -> np.ndarray:
    """`model.render` with single elements replaced.

    equaliser: (b, a) of another input filter; equaliser_behind: the equaliser behind the lines;
    lengths: function (values, first, frames) -> single-precision lengths [frame][group][line];
    plugin_size: the Size the plug-in works with, in place of the display value `size`;
    replaced: arrays of `model.parameters` (kernel, own, cross, matrix, attenuation) or functions of the
    model's array that return the replacement.
    """
    decay = model.host_value(data["parameters"]["decay"], decay)
    size = model.host_value(data["parameters"]["size"], size) if plugin_size is None else plugin_size
    first, internal = model.network_input(stimulus, rate, warmup)
    values = model.parameters(data, decay, size)
    for key, value in replaced.items():
        values[key] = value(values[key]) if callable(value) else value
    if equaliser is not None:
        driven = lfilter(*equaliser, internal, axis=0)
    else:
        driven = internal if equaliser_behind else model.equalise(data, internal)
    written_at = first + data["input"]["delay_samples"]
    explicit = None if lengths is None else lengths(values, written_at, len(driven))
    taps = model.run_core(values, driven, written_at, values["tap"], lengths=explicit)[:, 0, :]
    if equaliser_behind:
        taps = model.equalise(data, taps)
    origin = int(round(warmup * rate))
    return model.network_output(taps, first, rate, origin, origin + len(stimulus), data)


def with_value(data: dict, path: tuple, value: float) -> dict:
    """A copy of the constants with one number replaced."""
    changed = copy.deepcopy(data)
    target = changed
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = float(value)
    return changed


def value_at(data: dict, path: tuple) -> float:
    for key in path:
        data = data[key]
    return float(data)


# ---------------------------------------------------------------- the fitted number

def scale_on(data: dict, stimulus: np.ndarray, target: np.ndarray, case: tuple) -> float:
    """Least-squares gain of the model on one capture."""
    rate, decay, size, warmup = case
    rendered = render(data, stimulus, rate, warmup, decay, size)
    return float(np.sum(rendered * target) / np.sum(rendered ** 2))


def fit_scale(data: dict) -> tuple:
    """The input gain of line 1: mean least-squares scale of the model on network_b's five single impulses."""
    def one(case):
        channel, decay = case
        stimulus, target = impulse_case(channel, decay)
        return scale_on(data, stimulus, target, (RATE, decay, 100.0, 10.0))
    scales = np.array(list(POOL.map(one, SCALE_CASES)))
    start = data["input"]["gain"]["first"]
    first = float(start * scales.mean())
    evidence = {"casesChannelDecay": [list(case) for case in SCALE_CASES], "gainPerCase": [float(start * value) for value in scales],
                "fitted": first, "relativeSpread": float(scales.std() / scales.mean())}
    print("fitted input gain of line 1", evidence, flush=True)
    return first, evidence


def scale_check(data: dict) -> dict:
    """The fitted gain on the fresh 44.1 kHz captures, and the closed form it comes close to: the squared
    own and cross gains of a group summing to 0.252."""
    first = data["input"]["gain"]["first"]
    width = model.width_weights(data)
    closed = math.sqrt(0.252 / float(np.sum(model.input_curve(data) ** 2 * (1.0 + width ** 2) / 2.0)))

    def one(case):
        stimulus, target = fresh_case(case)
        rate, decay, size, warmup = case
        rendered = render(data, stimulus, rate, warmup, decay, size)
        scale = float(np.sum(rendered * target) / np.sum(rendered ** 2))
        return scale, null_db(rendered, target), null_db(rendered * closed / first, target)
    rows = list(POOL.map(one, INTERNAL_CASES))
    scales = np.array([row[0] for row in rows])
    result = {"model": first, "freshMean": float(first * scales.mean()), "freshStandardDeviation": float(first * scales.std()),
              "freshPerCase": [float(first * value) for value in scales],
              "closedForm": {"rule": "sum over the 16 lines of own^2 + cross^2 = 0.252", "value": closed,
                             "relativeDifference": closed / first - 1.0,
                             "nullsDbModel": [row[1] for row in rows], "nullsDbClosedForm": [row[2] for row in rows]}}
    print("scale check", {key: result[key] for key in ("model", "freshMean", "freshStandardDeviation")}, "closed form", closed, flush=True)
    return result


# ---------------------------------------------------------------- scores and the elements network_a and network_b disagree on

def fresh_scores(data: dict) -> list:
    """Null of the model and of both authors' models on every fresh capture; nothing is fitted on them."""
    def one(case):
        rate, decay, size, warmup = case
        stimulus, target = fresh_case(case)
        row = {"sampleRate": rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup}
        candidates = {"model": lambda: model.render(stimulus, rate, warmup, decay, size, data)}
        if rate in (44100, 48000):                      # the authors' models cover these two host rates
            candidates["network_a"] = lambda: network_a.render(stimulus, rate, warmup, decay, size)
            candidates["network_b"] = lambda: network_b.render(stimulus, rate, warmup, decay, size)
        for name, function in candidates.items():
            rendered = function()
            row[name] = {"overallDb": null_db(rendered, target), "stretchesDb": stretch_nulls(rendered, target, rate)}
        return row
    rows = [one(case) for case in FRESH_CASES]
    for row in rows:
        print("fresh", row["sampleRate"], row["decaySeconds"], row["sizePercent"], row["warmupSeconds"],
              {name: row[name]["overallDb"] for name in ("model", "network_a", "network_b") if name in row}, row["model"]["stretchesDb"], flush=True)
    return rows


def author_elements() -> dict:
    """The four elements in which network_a differs from the model at the internal rate."""
    author = network_a.model()
    sections = tf2sos(author.loop_b, author.loop_a)
    return {"loop filter": {"kernel": np.array([[row[0], row[1], row[2], row[4], row[5]] for row in sections])},
            "input filter": {"equaliser": (author.input_b, author.input_a)},
            "input gains": {"own": author.own_gain.copy(), "cross": author.cross_gain.copy()},
            "line length": {"lengths": LENGTH_EXPRESSIONS["network_a: (0.00088f * sine) * 44100f, sine folded with fl32(pi) and fl32(2 pi)"]}}


def element_swaps(data: dict) -> dict:
    """One element of network_a in the model at a time, then all four: nulls on the fresh 44.1 kHz captures
    (overall, and the stretch 6 to 14 s)."""
    elements = author_elements()
    rows = [("model", {})] + [(f"network_a's {name}", swap) for name, swap in elements.items()]
    rows.append(("network_a's four elements", {key: value for swap in elements.values() for key, value in swap.items()}))
    table = {name: [] for name, _ in rows}
    table["network_a.render"] = []
    for case in INTERNAL_CASES:
        rate, decay, size, warmup = case
        stimulus, target = fresh_case(case)
        late = slice(int(6 * rate), None)
        renders = list(POOL.map(lambda row: render(data, stimulus, rate, warmup, decay, size, **row[1]), rows))
        for (name, _), rendered in zip(rows, renders):
            table[name].append([null_db(rendered, target), null_db(rendered[late], target[late])])
        rendered = network_a.render(stimulus, rate, warmup, decay, size)
        table["network_a.render"].append([null_db(rendered, target), null_db(rendered[late], target[late])])
    for name, values in table.items():
        print(f"swap: {name}: {[value[0] for value in values]}", flush=True)
    return {"casesRateDecaySizeWarmup": [list(case) for case in INTERNAL_CASES], "overallAndLateDb": table}


def flipped_sign(matrix: np.ndarray) -> np.ndarray:
    changed = matrix.copy()
    changed[4, 10] = -changed[4, 10]
    return changed


def rounded_attenuation(data: dict, decay: float, size: float) -> np.ndarray:
    """The attenuation computed from the whole line length instead of the unrounded one."""
    values = model.parameters(data, model.host_value(data["parameters"]["decay"], decay), model.host_value(data["parameters"]["size"], size))
    return 10.0 ** (-3.0 * values["length"] / (RATE * model.host_value(data["parameters"]["decay"], decay)))


def ablations(data: dict) -> dict:
    """Elements both authors agree on, changed one at a time: nulls of two fresh 44.1 kHz captures."""
    cases = (INTERNAL_CASES[1], INTERNAL_CASES[3])
    table = {}
    for case in cases:
        rate, decay, size, warmup = case
        stimulus, target = fresh_case(case)
        rows = {
            "model": {},
            "Hadamard rows in natural order": {"matrix": lambda matrix: matrix[::-1].copy()},
            "sign of one matrix entry flipped (line 11 into line 5)": {"matrix": flipped_sign},
            "no loop kernel": {"kernel": np.array([[1.0, 0.0, 0.0, 0.0, 0.0]])},
            "no cross feed at the line inputs": {"own": lambda own: own + model.input_gains(data)[1], "cross": lambda cross: 0.0 * cross},
            "equaliser behind the lines": {"equaliser_behind": True},
            "attenuation from the rounded length": {"attenuation": rounded_attenuation(data, decay, size)},
        }
        renders = list(POOL.map(lambda row: render(data, stimulus, rate, warmup, decay, size, **row), rows.values()))
        for name, rendered in zip(rows, renders):
            table.setdefault(name, []).append(null_db(rendered, target))
    for name, values in table.items():
        print(f"ablation: {name}: {values}", flush=True)
    return {"casesRateDecaySizeWarmup": [list(case) for case in cases], "nullsDb": table}


# ---------------------------------------------------------------- the single-precision line length

def reduced_sine(theta: np.ndarray) -> np.ndarray:
    """The model's sine: argument reduced by the nearest multiple of the single-precision pi/2, result rounded."""
    wide = theta.astype(np.float64)
    lag = float(np.float32(np.pi / 2.0)) - np.pi / 2.0
    return np.sin(wide - np.round(wide / (np.pi / 2.0)) * lag).astype(np.float32)


def folded_sine(theta: np.ndarray) -> np.ndarray:
    """network_a's sine: the phase folded into [-pi/2, pi/2] with the single-precision pi and 2 pi."""
    single_pi, single_two_pi = np.float32(np.pi), np.float32(2.0 * np.pi)
    middle = (single_pi - theta).astype(np.float64)
    last = (theta - single_two_pi).astype(np.float64)
    folded = np.where(theta < np.float32(0.5 * np.pi), theta.astype(np.float64), np.where(theta < np.float32(1.5 * np.pi), middle, last))
    return np.sin(folded).astype(np.float32)


def length_rule(sine, depth=None, combine: str = "separate"):
    """A line-length expression as a function for `render`: whole length + depth * sine(theta)."""
    def lengths(values: dict, first: int, frames: int) -> np.ndarray:
        theta = model.oscillator_phases(values, first, frames)
        whole = values["length"].astype(np.float32)[None, :, :]
        used = values["depth"] if depth is None else np.float32(depth)
        if combine == "once":                                        # product and sum in double, one rounding
            return (whole.astype(np.float64) + float(used) * sine(theta).astype(np.float64)).astype(np.float32)
        if combine == "seconds first":                               # (0.00088f * sine) * 44100f
            return whole + (np.float32(0.00088) * sine(theta)) * np.float32(RATE)
        return whole + used * sine(theta)
    return lengths


LENGTH_EXPRESSIONS = {
    "model: fl32(N) + fl32(38.808002 * sine), sine reduced with fl32(pi/2)": length_rule(reduced_sine),
    "depth one step lower, fl32(38.808)": length_rule(reduced_sine, depth=38.808),
    "product and sum rounded once": length_rule(reduced_sine, combine="once"),
    "product (0.00088f * sine) * 44100f": length_rule(reduced_sine, combine="seconds first"),
    "correctly rounded sine of the phase, no reduction": length_rule(lambda theta: np.sin(theta.astype(np.float64)).astype(np.float32)),
    "sine of period fl32(2 pi)": length_rule(
        lambda theta: np.sin(theta.astype(np.float64) * (2.0 * np.pi / float(np.float32(2.0 * np.pi)))).astype(np.float32)),
    "sine folded with fl32(pi) and fl32(2 pi)": length_rule(folded_sine),
    "network_a: (0.00088f * sine) * 44100f, sine folded with fl32(pi) and fl32(2 pi)": length_rule(folded_sine, combine="seconds first"),
}


def length_expressions(data: dict) -> dict:
    """Null of four fresh 44.1 kHz captures under other expressions of the line length (overall; 0.25 to 1.4 s)."""
    cases = INTERNAL_CASES[:4]
    table = {name: [] for name in LENGTH_EXPRESSIONS}
    transcription = []
    for case in cases:
        rate, decay, size, warmup = case
        stimulus, target = fresh_case(case)
        early = slice(int(0.25 * rate), int(1.4 * rate))
        core = render(data, stimulus, rate, warmup, decay, size)
        for index, (name, rule) in enumerate(LENGTH_EXPRESSIONS.items()):
            rendered = render(data, stimulus, rate, warmup, decay, size, lengths=rule)
            table[name].append([null_db(rendered, target), null_db(rendered[early], target[early])])
            if index == 0:                                           # the core's own expression, transcribed
                transcription.append(float(np.abs(rendered - core).max()))
    for name, values in table.items():
        print(f"length: {name}: {values}", flush=True)
    return {"casesRateDecaySizeWarmup": [list(case) for case in cases], "overallAndEarlyDb": table,
            "largestDifferenceCoreAgainstFirstRow": transcription}


# ---------------------------------------------------------------- the 100 s tails

def shift_and_gain(rendered: np.ndarray, target: np.ndarray, rate: int, bins) -> tuple:
    """Per stretch: least-squares gain - 1 and delay (samples) of the reference against the model, and the
    null once both are removed."""
    slope = np.zeros_like(rendered)
    slope[1:-1] = (rendered[2:] - rendered[:-2]) / 2.0
    gains, delays, after = [], [], []
    for lo, hi in bins:
        chosen = slice(lo * rate, hi * rate)
        design = np.stack([rendered[chosen].ravel(), slope[chosen].ravel()], axis=1)
        error = (target[chosen] - rendered[chosen]).ravel()
        solution = np.linalg.lstsq(design, error, rcond=None)[0]
        gains.append(float(solution[0]))
        delays.append(float(-solution[1]))
        after.append(round(float(10 * np.log10(np.sum((error - design @ solution) ** 2) / np.sum(target[chosen] ** 2))), 2))
    return gains, delays, after


def tails(data: dict, free: dict) -> dict:
    """Null along three 100 s tails for the model, for network_a's loop filter in the model, and for two
    free-fit kernels (network_b's, fitted on its own captures of at most 30 s; this packet's free fit)."""
    loop = author_elements()["loop filter"]["kernel"]
    variants = (("model", data, {}), ("network_a's loop filter", data, {"kernel": loop}),
                ("network_b's free-fit kernel", {**data, "feedback": {**data["feedback"], "kernel": NETWORK_B_FREE_KERNEL}}, {}),
                ("this packet's free fit", free, {}))
    decay, size, warmup = AUTHOR_TAIL
    captures = (("network_a's capture, Decay 20 s, Size 100 %, 44.1 kHz", author_tail(44100), (44100, decay, size, warmup)),
                ("network_a's capture, Decay 20 s, Size 100 %, 48 kHz", author_tail(48000), (48000, decay, size, warmup)),
                ("this packet's capture, Decay 40 s, Size 140 %, 44.1 kHz", tail_case(), TAIL_CASE))
    result = {"binsSeconds": [list(edge) for edge in TAIL_BINS]}
    for label, (stimulus, target), (rate, decay, size, warmup) in captures:
        entry = {"referenceLevelDb": [round(float(10 * np.log10(np.mean(target[lo * rate:hi * rate] ** 2))), 1) for lo, hi in TAIL_BINS]}
        renders = list(POOL.map(lambda row: render(row[1], stimulus, rate, warmup, decay, size, **row[2]), variants))
        for (name, _, _), rendered in zip(variants, renders):
            gains, delays, after = shift_and_gain(rendered, target, rate, TAIL_BINS)
            entry[name] = {"overallDb": null_db(rendered, target), "nullDb": stretch_nulls(rendered, target, rate, TAIL_BINS),
                           "gainMinusOne": gains, "delaySamples": delays, "nullWithoutGainAndDelayDb": after}
            print(f"tail: {label}: {name}: {entry[name]['nullDb']}", flush=True)
        result[label] = entry
    return result


# ---------------------------------------------------------------- free fit

def block_weights(target: np.ndarray, rate: int, windows=None) -> np.ndarray:
    """One weight per sample: 1 / rms of the reference in its quarter-second block, so that every block of
    the response counts alike; zero outside `windows` (seconds) and where the reference is below FLOOR."""
    block = rate // 4
    weights = np.zeros(len(target))
    for start in range(0, len(target) - block + 1, block):
        level = float(np.sqrt(np.mean(target[start:start + block] ** 2)))
        if level > FLOOR:
            weights[start:start + block] = 1.0 / level
    if windows is not None:
        keep = np.zeros(len(target), bool)
        for lo, hi in windows:
            keep[int(lo * rate):int(hi * rate)] = True
        weights[~keep] = 0.0
    return weights


def free_fit_cases() -> list:
    cases = [(fresh_case(case), case, None) for case in INTERNAL_CASES]
    cases.append((tail_case(), TAIL_CASE, TAIL_FIT_WINDOWS))
    cases.append((author_tail(RATE), (RATE,) + AUTHOR_TAIL[:2] + (AUTHOR_TAIL[2],), TAIL_FIT_WINDOWS))
    return cases


def free_fit(data: dict) -> tuple:
    """One Gauss-Newton step from the round values, all constants free at once, on the eight fresh 44.1 kHz
    captures and stretches of the two 44.1 kHz tails. Returns (constants with the free values, evidence)."""
    count = len(FREE_PARAMETERS)
    start = np.array([value_at(data, path) for _, path in FREE_PARAMETERS])
    steps = FREE_STEP * np.abs(start)
    normal, right = np.zeros((count, count)), np.zeros(count)
    cases = free_fit_cases()
    for (stimulus, target), (rate, decay, size, warmup), windows in cases:
        weights = block_weights(target, rate, windows)
        used = weights > 0.0
        base = render(data, stimulus, rate, warmup, decay, size)
        residual = ((target - base)[used] * weights[used, None]).ravel()

        def column(index):
            changed = with_value(data, FREE_PARAMETERS[index][1], start[index] + steps[index])
            moved = render(changed, stimulus, rate, warmup, decay, size)
            return (((moved - base)[used] * weights[used, None]).ravel() / steps[index]).astype(np.float32)
        jacobian = np.stack(list(POOL.map(column, range(count))), axis=1)
        for begin in range(0, len(residual), 1 << 18):
            part = jacobian[begin:begin + (1 << 18)].astype(np.float64)
            normal += part.T @ part
            right += part.T @ residual[begin:begin + (1 << 18)]
        print("free fit: case", (rate, decay, size, warmup), "rows", len(residual), flush=True)
    scale = np.sqrt(np.diag(normal))
    change = np.linalg.solve(normal / np.outer(scale, scale), right / scale) / scale
    free = copy.deepcopy(data)
    for (_, path), value in zip(FREE_PARAMETERS, start + change):
        free = with_value(free, path, value)
    rows = [{"constant": name, "round": float(value), "free": float(value + delta), "relativeDifference": float(delta / value)}
            for (name, _), value, delta in zip(FREE_PARAMETERS, start, change)]
    for row in rows:
        print("free fit:", row, flush=True)
    return free, {"rows": rows, "conditionNumber": float(np.linalg.cond(normal / np.outer(scale, scale))),
                  "kernelResponse": kernel_difference(data, free)}


def kernel_difference(data: dict, free: dict) -> dict:
    """Loop kernel of the free fit over the round one: gain - 1 and phase (rad) per pass at a few frequencies."""
    frequencies = [100.0, 500.0, 1250.0, 1750.0, 2500.0, 3500.0, 6000.0, 12000.0]

    def response(constants):
        z = np.exp(-2j * np.pi * np.array(frequencies) / RATE)
        total = np.ones(len(frequencies), complex)
        for entry in constants["feedback"]["kernel"]:
            numerator, denominator = model.section(entry)
            total *= (numerator[0] + numerator[1] * z + numerator[2] * z * z) / (1.0 + denominator[0] * z + denominator[1] * z * z)
        return total
    ratio = response(free) / response(data)
    return {"frequenciesHz": frequencies, "gainMinusOne": [float(abs(value) - 1.0) for value in ratio],
            "phaseRad": [float(np.angle(value)) for value in ratio]}


def free_fit_scores(data: dict, free: dict) -> dict:
    """Round against free values: the fit set, and captures the free fit did not see (other host rates)."""
    def pair(stimulus, target, case, edges):
        rate, decay, size, warmup = case
        rendered = [render(constants, stimulus, rate, warmup, decay, size) for constants in (data, free)]
        return {"caseRateDecaySizeWarmup": list(case), "roundDb": null_db(rendered[0], target), "freeDb": null_db(rendered[1], target),
                "roundStretchesDb": stretch_nulls(rendered[0], target, rate, edges), "freeStretchesDb": stretch_nulls(rendered[1], target, rate, edges)}
    fitted = list(POOL.map(lambda case: pair(*fresh_case(case), case, STRETCHES), INTERNAL_CASES))
    unseen = list(POOL.map(lambda case: pair(*fresh_case(case), case, STRETCHES), FRESH_CASES[8:]))
    for row in fitted + unseen:
        print("free against round:", row["caseRateDecaySizeWarmup"], row["roundDb"], row["freeDb"], row["roundStretchesDb"], row["freeStretchesDb"], flush=True)
    return {"fitSet": fitted, "notFittedOn": unseen}


# ---------------------------------------------------------------- Size at a rounding tie

def size_ties(data: dict) -> dict:
    """Size 50 % puts every line on a rounding tie (prime / 2). Captures with the normalised Size a few
    single-precision steps lower show which whole lengths the plug-in uses, and with that how it evaluates
    its display law."""
    law = data["parameters"]["size"]
    prime = np.array(data["lines"]["prime"])

    def double_law(normalised):
        return law["low"] + (law["high"] - law["low"]) * math.expm1(law["shape"] * normalised) / math.expm1(law["shape"])
    rows = []
    for steps in TIE_STEPS:
        normalised = tie_normalised(steps)
        stimulus, target = tie_case(steps)
        candidates = {"single-precision display law (model)": model.plugin_value(law, normalised),
                      "display law in double precision": double_law(normalised),
                      "double-precision law rounded to single": float(np.float32(double_law(normalised)))}
        row = {"stepsBelow": steps, "normalised": normalised, "sizes": candidates, "nullDb": {}, "lengthOfLine1": {}}
        for name, size in candidates.items():
            rendered = render(data, stimulus, RATE, 10.0, TIE_DECAY, 50.0, plugin_size=size)
            row["nullDb"][name] = null_db(rendered, target)
            row["lengthOfLine1"][name] = int(math.floor(prime[0, 0] * size / 100.0 + 0.5))
        row["identicalToTheCaptureAt50Percent"] = bool(np.array_equal(target, tie_case(0)[1]))
        rows.append(row)
        print("size tie:", row, flush=True)
    return {"decaySeconds": TIE_DECAY, "rows": rows}


def output_floor(data: dict) -> dict:
    """Where the reference stops following the model: block levels and nulls of the fastest decay in the set."""
    case = INTERNAL_CASES[0]
    rate, decay, size, warmup = case
    stimulus, target = fresh_case(case)
    rendered = model.render(stimulus, rate, warmup, decay, size, data)
    rows = []
    for start in np.arange(8.0, 14.0, 0.5):
        chosen = slice(int(start * rate), int((start + 0.25) * rate))
        rows.append({"startSeconds": float(start), "referenceLevelDb": round(float(10 * np.log10(np.mean(target[chosen] ** 2))), 1),
                     "nullDb": null_db(rendered[chosen], target[chosen])})
    last = target[int(13.0 * rate):]
    return {"caseRateDecaySizeWarmup": list(case), "blocks": rows, "meanOfLastSecond": [float(value) for value in last.mean(axis=0)],
            "spreadOfLastSecond": [float(value) for value in last.std(axis=0)]}


# ---------------------------------------------------------------- outputs of line groups

def line_groups(data: dict) -> dict:
    """`line_outputs` against `render` on one fresh capture: the 16 lines one by one, and two groups of eight."""
    rate, decay, size, warmup = FRESH_CASES[9]
    stimulus, target = fresh_case(FRESH_CASES[9])
    first, internal = model.network_input(stimulus, rate, warmup)
    decay_used = model.host_value(data["parameters"]["decay"], decay)
    size_used = model.host_value(data["parameters"]["size"], size)
    single = model.line_outputs(internal, first, decay_used, size_used, data=data)
    halves = model.line_outputs(internal, first, decay_used, size_used, groups=[list(range(1, 9)), list(range(9, 17))], data=data)
    origin = int(round(warmup * rate))
    rendered = model.render(stimulus, rate, warmup, decay, size, data)
    from_lines = model.network_output(single.sum(axis=2), first, rate, origin, origin + len(stimulus), data)
    energy = np.sum(single ** 2, axis=(0, 1))
    return {"caseRateDecaySizeWarmup": list(FRESH_CASES[9]),
            "sumOfLinesAgainstRenderDb": null_db(from_lines, rendered), "sumOfLinesAgainstReferenceDb": null_db(from_lines, target),
            "largestDifferenceLinesAgainstHalves": float(np.abs(single.sum(axis=2) - halves.sum(axis=2)).max()),
            "shareOfTapEnergyPerLine": [round(float(value), 4) for value in energy / energy.sum()],
            "shareOfTapEnergyLines1To8": round(float(np.sum(halves[:, :, 0] ** 2) / np.sum(halves ** 2)), 4)}


# ---------------------------------------------------------------- main

def main() -> None:
    revocean.identity()
    model.library()
    render_captures()
    if sys.argv[1:] == ["captures"]:
        return
    data = copy.deepcopy(STRUCTURE)
    data["input"]["gain"]["first"], scale = fit_scale(data)
    free, free_evidence = free_fit(data)
    evidence = {
        "captures": {"programmeSeed": PROGRAMME_SEED, "freshCases": [list(case) for case in FRESH_CASES], "tailCase": list(TAIL_CASE)},
        "fittedGain": scale,
        "fresh": fresh_scores(data),
        "elementSwaps": element_swaps(data),
        "ablations": ablations(data),
        "lengthExpressions": length_expressions(data),
        "freeFit": {**free_evidence, "scores": free_fit_scores(data, free)},
        "tails": tails(data, free),
        "sizeTies": size_ties(data),
        "scaleCheck": scale_check(data),
        "outputFloor": output_floor(data),
        "lineGroups": line_groups(data),
    }
    OUTPUT.write_text(json.dumps({**data, "evidence": evidence}, indent=1) + "\n")
    print("wrote", OUTPUT, flush=True)


if __name__ == "__main__":
    main()
