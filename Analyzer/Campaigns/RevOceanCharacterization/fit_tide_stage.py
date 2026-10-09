#!/usr/bin/env python3
"""Measurements behind tide_stage.py: scores, round-value tests and checks of the Tide comb and voice filter.

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    cd Analyzer/Campaigns/RevOceanCharacterization
    $PY fit_tide_stage.py            # writes tide_structural_data/tide_stage.json

With the captures cached the run takes about three minutes; the captures themselves are about 0.85 GB
(all at Decay 0.5 s, Size 100 %, baseline otherwise, unless a section says so). Nothing here reads a
holdout. Two kinds of null are computed, both 20 log10(rms(model - reference) / rms(reference)) per impulse
and output with no gain or delay fitted:

* model: tide_stage.FirstPass (Macro 0 network of first_order_model behind the Tide stage) against a
  capture, on the first-pass window. The network model itself stands at -80 dB.
* relation: at a 44.1 kHz host the Macro 0 capture of a stimulus that already went through this module's
  input stage, passed through this module's voice, against the capture of the plain stimulus at Macro
  above 0. Both sides are captures, so the network is not modelled and the null is that of the Tide stage.

Per impulse and output the voice's phase is fitted, alone (its rate at the nominal value) or with its rate.
"""
from __future__ import annotations

import json
from contextlib import contextmanager
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import lfilter

import revocean
import tide_stage as stage

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "tide_stage.json"
RATE = stage.INTERNAL_RATE
AMPLITUDE = 0.5
DECAY = {"decay": revocean.normalised("decay", 0.5)}
TRAIN_SEED, PAIR_SEED = 9101, 9301
OPEN_LEVEL_DB = -30.0             # a voice counts as open when the window stands above this, re the voice at rest
RELATION_RUN_IN = 600
# (host rate, Macro %, realisation, seconds): the model scores
MODEL_CAPTURES = ([(48000, macro, 0, 100.0) for macro in (100, 75, 50, 25, 10, 5)]
                  + [(44100, 100, 0, 100.0), (44100, 100, 1, 100.0), (44100, 90, 0, 50.0), (44100, 75, 0, 100.0), (44100, 60, 0, 50.0),
                     (44100, 50, 0, 100.0), (44100, 35, 0, 50.0), (44100, 25, 0, 100.0), (44100, 15, 0, 50.0), (44100, 10, 0, 100.0),
                     (44100, 5, 0, 100.0), (44100, 3, 0, 50.0), (44100, 1, 0, 50.0)])
# other Size / Decay, relation at Macro 50 %: (Size %, Decay s, seconds between impulses)
PAIRS = {"size 62 %": (62.0, 0.5, 0.9), "size 157 %": (157.0, 0.5, 0.9), "decay 1 s": (100.0, 1.0, 2.0)}
PAIR_SECONDS, PAIR_MACRO = 26.0, 50
TONE_HZ, TONE_SECONDS = 997.0, 30.0


def nominal_rate(macro: float) -> float:
    """Mean rate of the voice phase, cycles/s (findings/tide_verification.md)."""
    return 0.0500 + 0.0064 * macro


# ---------------------------------------------------------------- captures

def train_times(seed: int, rate: int, gap: float, jitter: float, seconds: float) -> np.ndarray:
    generator = np.random.default_rng(seed)
    times = [rate + int(generator.integers(0, int(jitter * rate)))]
    while True:
        following = times[-1] + int(gap * rate) + int(generator.integers(0, int(jitter * rate)))
        if following >= (seconds - 1.0) * rate:
            return np.array(times)
        times.append(following)


def train(rate: int, seconds: float, seed: int = TRAIN_SEED, gap: float = 0.8) -> tuple:
    """Impulses alternating between the inputs, `gap` to `gap + 0.2` s apart: (times, channels, stimulus)."""
    times = train_times(seed, rate, gap, 0.2, seconds)
    channels = np.arange(len(times)) % 2
    events = [(int(t), int(c), AMPLITUDE) for t, c in zip(times, channels)]
    return times, channels, revocean.impulses(seconds, events, sample_rate=rate)


def render(stimulus: np.ndarray, rate: int, macro: int, settings: dict | None = None, **options) -> np.ndarray:
    capture = revocean.capture(stimulus, dict(settings or DECAY, macro=macro / 100.0), sample_rate=rate, **options)
    return capture.output.astype(np.float64)


def driven(stimulus: np.ndarray, macro: int, warmup: float) -> np.ndarray:
    """A 44.1 kHz stimulus passed through this module's input stage."""
    first = int(round(warmup * RATE))
    return np.stack([stage.input_stage(stimulus[:, ch].astype(np.float64), first, ch, macro / 100.0) for ch in (0, 1)],
                    axis=1).astype(np.float32)


# ---------------------------------------------------------------- one impulse, one output

def fit(model, target: np.ndarray, rate: float, free_rate: bool, extra_starts=()) -> tuple:
    """(phase, rate, null in dB) of model(phase, rate) against the target: phase scan, then least squares.

    `extra_starts` are further start phases (see wrap_starts); the better of start and solution is returned.
    """
    norm = np.sqrt(np.sum(target ** 2))
    starts = list((np.arange(40) + 0.5) / 40) + list(extra_starts)
    errors = [np.sum((model(phase, rate) - target) ** 2) for phase in starts]
    start = starts[int(np.argmin(errors))]
    if free_rate:
        solution = least_squares(lambda p: (model(p[0], p[1]) - target) / norm, [start, rate], x_scale=[1e-3, 1e-2],
                                 xtol=1e-14, ftol=1e-14, gtol=1e-14)
        found = (solution.x[0], solution.x[1], 10 * np.log10(np.sum(solution.fun ** 2)))
    else:
        solution = least_squares(lambda p: (model(p[0], rate) - target) / norm, [start], x_scale=[1e-3], xtol=1e-14, ftol=1e-14, gtol=1e-14)
        found = (solution.x[0], rate, 10 * np.log10(np.sum(solution.fun ** 2)))
    at_start = 10 * np.log10(min(errors) / norm ** 2)
    return found if found[2] <= at_start else (start, rate, at_start)


def wrap_starts(window, rate: float) -> list:
    """Start phases that put the phase's whole number into each block near the window. Below full depth the gain
    is not zero there, the cut-off jumps back in one block, and a phase scan alone does not find the block."""
    blocks = np.arange(int(window.index[0]) // stage.BLOCK - 60, int(window.index[-1]) // stage.BLOCK + 2)
    return list(1.0 - rate * ((blocks + 0.5) * stage.BLOCK - window.reference) / RATE)


def summary(nulls, energy) -> dict:
    nulls, energy = np.asarray(nulls), np.asarray(energy)
    if not len(nulls):
        return {"cases": 0}
    return {"cases": int(len(nulls)), "medianDb": round(float(np.median(nulls)), 2),
            "quartilesDb": [round(float(v), 2) for v in np.percentile(nulls, [25, 75])],
            "worstTenthDb": round(float(np.percentile(nulls, 90)), 2),
            "energyWeightedDb": round(float(10 * np.log10(np.sum(10 ** (nulls / 10) * energy) / np.sum(energy))), 2)}


# ---------------------------------------------------------------- model scores

def model_cases(rate: int, macro: int, realisation: int, seconds: float, warmup: float = 10.0, step: int = 1, **render_options):
    """(time, input, output, window, target) for the impulses of the main train."""
    times, channels, stimulus = train(rate, seconds)
    output = render(stimulus, rate, macro, realisation=realisation, warmup=warmup, **render_options) / AMPLITUDE
    for time, channel in list(zip(times, channels))[::step]:
        for group in (0, 1):
            window = stage.FirstPass(time, channel, group, macro / 100.0, host_rate=rate, warmup_seconds=warmup)
            yield int(time), int(channel), group, window, output[window.samples, group]


def score_capture(rate: int, macro: int, realisation: int, seconds: float, **options) -> dict:
    records = []
    for time, channel, group, window, target in model_cases(rate, macro, realisation, seconds, **options):
        level = 10 * np.log10(np.sum(target ** 2) / np.sum(window.at_rest() ** 2))
        starts = wrap_starts(window, nominal_rate(macro / 100.0)) if stage.depth(macro / 100.0) < 1.0 else ()
        phase, slope, both = fit(window.response, target, nominal_rate(macro / 100.0), True, starts)
        _, _, alone = fit(window.response, target, nominal_rate(macro / 100.0), False, starts)
        seconds_in = (options.get("warmup", 10.0) * rate + time) / rate
        records.append([round(seconds_in, 4), channel, group, round(phase % 1.0, 5), round(slope, 5), round(both, 2), round(alone, 2),
                        round(level, 2), float(np.sum(target ** 2))])
    table = np.array(records)
    is_open = table[:, 7] > OPEN_LEVEL_DB
    return {"hostRate": rate, "macroPercent": macro, "realisation": realisation,
            "phaseAndRate": summary(table[is_open, 5], table[is_open, 8]), "phaseOnly": summary(table[is_open, 6], table[is_open, 8]),
            "allCasesPhaseAndRateMedianDb": round(float(np.median(table[:, 5])), 2),
            "meanFittedRate": round(float(np.mean(table[is_open, 4])), 5),
            "columns": ["seconds since processing started", "input", "output", "phase (cycles) at the first window sample",
                        "rate (cycles/s)", "null phase and rate (dB)", "null phase only (dB)", "level re voice at rest (dB)"],
            "records": [row[:8] for row in records]}


# ---------------------------------------------------------------- the relation between two captures

def window_limits(size: float, delay: float) -> tuple:
    """Raw samples after the impulse that hold voice A only: from the first line to just before the ninth."""
    return (88 + int(np.floor(1031 * size / 100 + 0.5)) - 45 + int(delay), 88 + int(np.floor(2333 * size / 100 + 0.5)) - 45 + int(delay))


def relation_cases(tide: np.ndarray, rest: np.ndarray, times, channels, macro: int, size: float = 100.0, warmup: float = 10.0):
    """(voice input, read counts, reference count, target, level) per impulse and output."""
    warm = int(round(warmup * RATE))
    for time, channel in zip(times, channels):
        delay = float(stage.comb_delay(warm + int(time), channel)) if macro >= 100 else 0.0
        low, high = window_limits(size, delay)
        segment = np.arange(int(time) + low - RELATION_RUN_IN, int(time) + high)
        index = segment + warm - stage.OUTPUT_DELAY
        for group in (0, 1):
            target = tide[segment, group][RELATION_RUN_IN:]
            level = 10 * np.log10(np.sum(target ** 2) / np.sum(rest[segment, group][RELATION_RUN_IN:] ** 2))
            yield stage.without_rest(rest[segment, group]), index, float(index[RELATION_RUN_IN]), target, level


def relation_model(signal, index, reference, macro: int, voice=None):
    voice = stage.voice if voice is None else voice
    return lambda phase, rate: voice(signal, index, phase, rate, reference, macro / 100.0)[RELATION_RUN_IN:]


def score_relation(cases: list, macro: int, voice=None, free_rate: bool = True) -> np.ndarray:
    """Null per open case."""
    return np.array([fit(relation_model(signal, index, reference, macro, voice), target, nominal_rate(macro / 100.0), free_rate)[2]
                     for signal, index, reference, target, level in cases])


def main_relation(macro: int = 100, block_sizes=(512, 64, 2048)) -> list:
    """Open cases of the 50 s train at 44.1 kHz: three Macro `macro` instances (rendered with three host block
    sizes) against the one driven Macro 0 capture."""
    times, channels, stimulus = train(RATE, 50.0)
    rest = render(driven(stimulus, macro, 10.0), RATE, 0)
    cases = []
    for block_size in block_sizes:
        tide = render(stimulus, RATE, macro, block_size=block_size)
        cases += [case for case in relation_cases(tide, rest, times, channels, macro) if case[4] > OPEN_LEVEL_DB]
    return cases


def pair_relation(name: str) -> list:
    size, decay, gap = PAIRS[name]
    times, channels, stimulus = train(RATE, PAIR_SECONDS, seed=PAIR_SEED, gap=gap)
    settings = {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}
    tide = render(stimulus, RATE, PAIR_MACRO, settings)
    rest = render(driven(stimulus, PAIR_MACRO, 10.0), RATE, 0, settings)
    return [case for case in relation_cases(tide, rest, times, channels, PAIR_MACRO, size) if case[4] > OPEN_LEVEL_DB]


# ---------------------------------------------------------------- variants of the voice (round values, other forms)

@contextmanager
def changed(**constants):
    """Module constants of tide_stage replaced for the duration of a test."""
    saved = {name: getattr(stage, name) for name in constants}
    try:
        for name, value in constants.items():
            setattr(stage, name, value)
        yield
    finally:
        for name, value in saved.items():
            setattr(stage, name, value)


def per_sample_parameters(index, phase, rate, reference, macro):
    """No hold: cut-off and gain follow the phase per sample, Q lags by the smoothing's 9.5 ms."""
    phi = phase + rate * (index - reference) / RATE
    lag = (stage.Q_SMOOTHING_BLOCKS - 0.5) * stage.BLOCK / RATE
    return stage.cutoff(phi, macro), stage.quality_target(stage.cutoff(phi - rate * lag, macro), macro), stage.gain(phi, macro)


def direct_form_one(signal, cutoff_hz, quality, level):
    """Direct form I with the coefficients of each block applied to the outputs of the block before."""
    driven_by = lfilter([1.0, 2.0, 1.0], [1.0], signal)
    edges = np.concatenate([[0], np.flatnonzero((np.diff(cutoff_hz) != 0) | (np.diff(quality) != 0)) + 1, [len(signal)]])
    out = np.zeros(len(signal))
    y1 = y2 = 0.0
    for a, b in zip(edges[:-1], edges[1:]):
        num, den = stage.low_pass(cutoff_hz[a], quality[a])
        out[a:b] = lfilter([num[0]], den, driven_by[a:b], zi=[-den[1] * y1 - den[2] * y2, -den[2] * y1])[0]
        y1, y2 = out[b - 1], (out[b - 2] if b - a > 1 else y1)
    return level * out


def voice_variant(hold: bool = True, offset: int = 0, recursion=None, gain_first: bool = False):
    """A voice with one structural element changed; the signature of tide_stage.voice."""
    def voice(signal, index, phase, rate, reference, macro):
        if hold:
            cutoff_hz, quality, level = stage.voice_parameters(index - offset, phase, rate, reference - offset, macro)
        else:
            cutoff_hz, quality, level = per_sample_parameters(index, phase, rate, reference, macro)
        if gain_first:
            signal, level = signal * level, np.ones(len(signal))
        return (recursion or stage.state_variable)(signal, cutoff_hz, quality, level)
    return voice


def voice_tests(cases: list) -> dict:
    """Median null of the Macro 100 % relation with one element of the voice changed at a time."""
    median = lambda **options: round(float(np.median(score_relation(cases, 100, **options))), 2)
    results = {"as published": median(), "as published, phase only (rate nominal)": median(free_rate=False)}
    results["parameters per sample instead of held for 44 samples"] = median(voice=voice_variant(hold=False))
    results["block starts one sample later"] = median(voice=voice_variant(offset=1))
    results["block starts one sample earlier"] = median(voice=voice_variant(offset=-1))
    results["block starts 22 samples later"] = median(voice=voice_variant(offset=22))
    results["direct form I instead of the state-variable filter"] = median(voice=voice_variant(recursion=direct_form_one))
    results["gain in front of the filter"] = median(voice=voice_variant(gain_first=True))
    for name, constants in (("Q not smoothed", {"Q_SMOOTHING_BLOCKS": 1e-3}), ("Q smoothing 9 blocks", {"Q_SMOOTHING_BLOCKS": 9.0}),
                            ("Q smoothing 11 blocks", {"Q_SMOOTHING_BLOCKS": 11.0}), ("Q at 1 kHz 7.00", {"Q_1K": 7.0}),
                            ("Q at 3 kHz 2.04 (1.5 times the plateau)", {"Q_3K": (10.0, 2.04)}), ("Q plateau 1.40", {"Q_PLATEAU": (7.0, 1.40)}),
                            ("Q at 0 Hz 3.40", {"Q_LOW": 3.40}), ("cut-off law ends at 20 Hz", {"CUTOFF_END": 20.0}),
                            ("cut-off law ends at 0 Hz", {"CUTOFF_END": 0.0}), ("cut-off law starts at 19.3 kHz", {"CUTOFF_TOP": 19300.0}),
                            ("curve bend 2.94", {"CURVE": 2.94}), ("gain top 1.40", {"GAIN_TOP": 1.40}),
                            ("amount 0.87 per unit Macro", {"AMOUNT_PER_MACRO": 0.87})):
        with changed(**constants):
            results[name] = median()
    return results


# ---------------------------------------------------------------- variants of the comb

def comb_variant(read: str = "all-pass", filters: str = "inside", offset: float = 0.0, rate: float = RATE, **changes):
    """A comb with another read rule or with its two filters behind the loop; a replacement for tide_stage.comb."""
    constants = stage.comb_constants(**changes)
    scale = rate / RATE

    def delayed(stream, first, channel):
        count = first + np.arange(len(stream))
        delay = scale * (constants["mid"] + constants["swing"] * stage.triangle((count / scale - constants["origin"]) / constants["period"]
                                                                              + stage.COMB_RIGHT_LEAD * channel)) + offset
        hp_k, lp_k = (np.tan(np.pi * constants[key] / rate) for key in ("high_pass_hz", "low_pass_hz"))
        line, out = np.zeros(len(stream) + 4), np.zeros(len(stream))
        state = hx = hy = lx = ly = 0.0
        for n in range(len(stream)):
            whole = int(np.floor(delay[n]))
            d = delay[n] - whole
            tap = lambda k: line[n - k] if 0 <= n - k else 0.0
            if read == "linear":
                value = (1.0 - d) * tap(whole) + d * tap(whole + 1)
            elif read == "lagrange":
                e = d + 1.0
                value = (-(e - 1) * (e - 2) * (e - 3) / 6 * tap(whole - 1) + e * (e - 2) * (e - 3) / 2 * tap(whole)
                         - e * (e - 1) * (e - 3) / 2 * tap(whole + 1) + e * (e - 1) * (e - 2) / 6 * tap(whole + 2))
            else:
                if read == "all-pass from 0.618" and d < 0.618:
                    d, whole = d + 1.0, whole - 1
                value = state = tap(whole + 1) + (1.0 - d) / (1.0 + d) * (tap(whole) - state)
            hy, hx = (value - hx) / (1.0 + hp_k) + (1.0 - hp_k) / (1.0 + hp_k) * hy, value
            ly, lx = lp_k / (1.0 + lp_k) * (hy + lx) + (1.0 - lp_k) / (1.0 + lp_k) * ly, hy
            line[n] = stream[n] + constants["feedback"] * (ly if filters == "inside" else value)
            out[n] = ly
        return out
    return delayed


def host_rate_comb(time: int, warmup: float = 10.0):
    """The comb run at a 48 kHz host rate in front of the input converter, as a delayed path of the internal stream."""
    host_sample = int(round(warmup * 48000)) + int(time)
    at_host = comb_variant(rate=48000.0)

    def delayed(stream, first, channel):
        impulse = np.zeros(3400)
        impulse[0] = 1.0
        out = np.zeros(len(stream))
        for n, value in enumerate(at_host(impulse, host_sample, channel)):
            if value != 0.0:
                start, taps = stage.converters().impulse(host_sample + n)
                keep = (start - first + np.arange(len(taps)) < len(out))
                out[start - first + np.flatnonzero(keep)] += value * taps[keep]
        return out
    return delayed


def comb_tests() -> dict:
    """Median model null at Macro 50 % with one element of the comb changed at a time (every third impulse)."""
    def median(rate=RATE, per_case=None, **options):
        times, channels, stimulus = train(rate, 100.0)
        output = render(stimulus, rate, 50) / AMPLITUDE
        nulls = []
        for time, channel in list(zip(times, channels))[::3]:
            extra = dict(options, delayed_path=per_case(time)) if per_case else options
            for group in (0, 1):
                window = stage.FirstPass(time, channel, group, 0.5, host_rate=rate, **extra)
                nulls.append(fit(window.response, output[window.samples, group], nominal_rate(0.5), True)[2])
        return round(float(np.median(nulls)), 2)
    results = {"as published": median(), "feedback -8 dB (-0.398107)": median(feedback=-10 ** (-8 / 20)), "feedback -0.3986": median(feedback=-0.3986),
               "high-pass 35 Hz": median(high_pass_hz=35.0), "high-pass 25 Hz": median(high_pass_hz=25.0),
               "low-pass 19 kHz": median(low_pass_hz=19000.0), "low-pass 21 kHz": median(low_pass_hz=21000.0),
               "origin 44 samples": median(origin=44.0), "origin 88 samples": median(origin=88.0),
               "delay 0.01 sample longer": median(mid=stage.COMB_DELAY_MID + 0.01),
               "period 200.005 s": median(period=200.005 * RATE)}
    results["filters behind the loop instead of inside"] = median(delayed_path=comb_variant(filters="after"))
    for read in ("linear", "lagrange", "all-pass from 0.618"):
        results["read: %s, best of five delay offsets" % read] = min(median(delayed_path=comb_variant(read=read, offset=offset))
                                                                      for offset in (-0.1, -0.05, 0.0, 0.05, 0.1))
    results["48 kHz host, as published (comb inside at 44.1 kHz)"] = median(rate=48000)
    results["48 kHz host, comb at the host rate in front of the converter"] = median(rate=48000, per_case=host_rate_comb)
    return results


# ---------------------------------------------------------------- crossfade, depth, delay law

def crossfade() -> dict:
    """Gain of the delayed path relative to sin / cos of 90 degrees x Macro, fitted per impulse with the phase."""
    results = {}
    for macro, seconds in ((1, 50.0), (3, 50.0), (5, 100.0), (10, 100.0), (25, 100.0), (50, 100.0), (75, 100.0), (90, 50.0)):
        ratios = []
        for time, channel, group, window, target in model_cases(RATE, macro, 0, seconds, step=2):
            norm = np.sqrt(np.sum(target ** 2))
            angle = 0.5 * np.pi * macro / 100.0
            mixed = lambda p: window.response(p[0], p[1], np.cos(angle) * window.undelayed + p[2] * np.sin(angle) * window.delayed)
            phase, slope, null = fit(window.response, target, nominal_rate(macro / 100.0), True)
            solution = least_squares(lambda p: (mixed(p) - target) / norm, [phase, slope, 1.0], x_scale=[1e-3, 1e-2, 1e-2],
                                     xtol=1e-14, ftol=1e-14, gtol=1e-14)
            if null < -70.0:
                ratios.append(solution.x[2])
        results[str(macro)] = {"cases": len(ratios), "medianRatio": round(float(np.median(ratios)), 5),
                               "quartiles": [round(float(v), 5) for v in np.percentile(ratios, [25, 75])]}
    return results


def depth_slope() -> dict:
    """Median model null at Macro 3 % against the amount per unit Macro (depth = 20 x amount x Macro)."""
    cases = list(model_cases(RATE, 3, 0, 50.0, step=2))
    results = {}
    for amount in (0.8750, 0.8790, 0.8795, 0.8800, 0.8805, 0.8810, 0.8850):
        with changed(AMOUNT_PER_MACRO=amount):
            nulls = [fit(window.response, target, nominal_rate(0.03), True)[2] for _, _, _, window, target in cases]
        results["%.4f (slope %.2f)" % (amount, amount / stage.FULL_DEPTH_AMOUNT)] = round(float(np.median(nulls)), 2)
    return results


def triangle_law(counts, channels, mid, swing, origin, period_seconds):
    """The comb delay in double precision."""
    return mid + swing * stage.triangle((counts - origin) / (period_seconds * RATE) + stage.COMB_RIGHT_LEAD * channels)


def delay_law() -> dict:
    """Delay of the all-pass read per impulse (Macro 50 %, warm-ups 10 and 60 s), then the triangle refitted."""
    published = [stage.COMB_DELAY_MID, stage.COMB_DELAY_SWING, stage.COMB_ORIGIN, stage.COMB_PERIOD_SECONDS]
    counts, channels, measured = [], [], []
    for warmup in (10.0, 60.0):
        times, inputs, stimulus = train(RATE, 100.0)
        output = render(stimulus, RATE, 50, warmup=warmup) / AMPLITUDE
        for time, channel in list(zip(times, inputs))[::2]:
            for group in (0, 1):
                base = stage.FirstPass(time, channel, group, 0.5, host_rate=RATE, warmup_seconds=warmup)
                target = output[base.samples, group]
                norm = np.sqrt(np.sum(target ** 2))
                phase, slope, null = fit(base.response, target, nominal_rate(0.5), True)
                if null > -74.0:
                    continue
                def residual(p):
                    window = stage.FirstPass(time, channel, group, 0.5, host_rate=RATE, warmup_seconds=warmup, single_precision=False,
                                             mid=stage.COMB_DELAY_MID + p[2])
                    return (window.response(p[0], p[1]) - target) / norm
                solution = least_squares(residual, [phase, slope, 0.0], x_scale=[1e-3, 1e-2, 1e-3], xtol=1e-13, ftol=1e-13, gtol=1e-13)
                exit_count = int(round(warmup * RATE)) + int(time) + base.delay
                counts.append(exit_count); channels.append(channel)
                measured.append(float(triangle_law(exit_count, channel, *published)) + solution.x[2])
    counts, channels, measured = np.array(counts), np.array(channels), np.array(measured)
    law = lambda p: triangle_law(counts, channels, *p)
    free = least_squares(lambda p: law(p) - measured, published, x_scale=[1e-3, 1e-3, 1.0, 1e-3])
    held = least_squares(lambda p: law([p[0], p[1], p[2], 200.0]) - measured, published[:3], x_scale=[1e-3, 1e-3, 1.0])
    error = law(published) - measured
    return {"impulsesTimesOutputs": int(len(measured)), "secondsCovered": [round(float(counts.min() / RATE), 1), round(float(counts.max() / RATE), 1)],
            "publishedLawRmsSamples": round(float(np.sqrt(np.mean(error ** 2))), 6), "publishedLawLargestSamples": round(float(np.abs(error).max()), 6),
            "freeFit": {"mid": round(float(free.x[0]), 5), "swing": round(float(free.x[1]), 5), "originSamples": round(float(free.x[2]), 2),
                        "periodSeconds": round(float(free.x[3]), 5), "rmsSamples": round(float(np.sqrt(np.mean(free.fun ** 2))), 6)},
            "period200Fit": {"mid": round(float(held.x[0]), 5), "swing": round(float(held.x[1]), 5), "originSamples": round(float(held.x[2]), 2),
                             "rmsSamples": round(float(np.sqrt(np.mean(held.fun ** 2))), 6)}}


CROSSING_DESIGN_ORIGIN = 85.8     # the origin in use when the stimulus of crossing_test was designed


def crossing_events(seconds: float = 50.0, warmup: float = 10.0) -> list:
    """(impulse sample, whole number) for right-input impulses that leave the comb 100 samples before its delay
    falls through a whole number, picked where the exact delay is within 0.75 single-precision steps below it."""
    warm = int(round(warmup * RATE))
    exact = lambda count: triangle_law(np.asarray(count, dtype=np.float64), 1, stage.COMB_DELAY_MID, stage.COMB_DELAY_SWING,
                                       CROSSING_DESIGN_ORIGIN, stage.COMB_PERIOD_SECONDS)
    count = warm + np.arange(int(1.2 * RATE), int((seconds - 1.5) * RATE))
    delay = exact(count)
    chosen, last = [], -10 ** 9
    for step in np.flatnonzero(np.diff(np.floor(delay)) < 0) + 1:
        whole = np.floor(delay[step - 1])
        if (whole - delay[step]) / float(np.spacing(np.float32(whole))) < 0.75 and count[step] - last > 0.8 * RATE:
            exit_count = count[step] - 100
            chosen.append((int(round(exit_count - exact(exit_count))) - warm, float(whole)))
            last = count[step]
    return chosen


def crossing_test() -> dict:
    """Is the comb delay a single-precision number?  The sample at which the read changes its taps decides between
    a null near -70 dB and one near -37 dB for each event; counted for the published comb and for a comb whose
    delay is the same law in double precision (Macro 100 %, 44.1 kHz, both outputs, the better one counts)."""
    events = crossing_events()
    stimulus = revocean.impulses(50.0, [(time, 1, AMPLITUDE) for time, _ in events], sample_rate=RATE)
    output = render(stimulus, RATE, 100) / AMPLITUDE
    rows = []
    for time, whole in events:
        nulls = {True: [], False: []}
        for single in (True, False):
            for group in (0, 1):
                window = stage.FirstPass(time, 1, group, 1.0, host_rate=RATE, single_precision=single)
                target = output[window.samples, group]
                if np.sum(target ** 2) > 10 ** (OPEN_LEVEL_DB / 10) * np.sum(window.at_rest() ** 2):
                    nulls[single].append(fit(window.response, target, nominal_rate(1.0), True)[2])
        if nulls[True]:                                                    # at least one output with an open voice
            rows.append([round((10.0 * RATE + time) / RATE, 3), whole, round(min(nulls[True]), 1), round(min(nulls[False]), 1)])
    table = np.array(rows)
    return {"events": int(len(rows)), "matchedSinglePrecision": int(np.sum(table[:, 2] < -60.0)), "matchedDoublePrecision": int(np.sum(table[:, 3] < -60.0)),
            "columns": ["seconds since processing started", "whole number crossed", "null single precision (dB)", "null double precision (dB)"],
            "records": rows}


# ---------------------------------------------------------------- a steady tone

def tone(macro: int, amplitude: float = 0.2) -> np.ndarray:
    count = np.arange(int(TONE_SECONDS * RATE))
    wave = amplitude * np.sin(2 * np.pi * TONE_HZ * count / RATE) * np.minimum(1.0, count / 2000.0)
    return render(np.stack([wave, wave], axis=1).astype(np.float32), RATE, macro)


def tone_test() -> dict:
    """Lines that a parameter held for N samples adds around a steady tone: their spacing and their levels."""
    def spectrum(output):
        segment = output[5 * RATE:5 * RATE + 2 ** 20, 0]
        return np.abs(np.fft.rfft(segment * np.kaiser(len(segment), 38.0))), np.fft.rfftfreq(len(segment), 1 / RATE)
    magnitude, frequency = spectrum(tone(100))
    step = frequency[1]
    low = int(np.searchsorted(frequency, 975.0))
    base = magnitude[low:low + int(44.0 / step)]
    spacing = []
    for order in (1, 2, 3):
        shifts = np.arange(int(order * 985 / step), int(order * 1020 / step))
        match = [np.sum(base * magnitude[low + s:low + s + len(base)]) / np.sqrt(np.sum(magnitude[low + s:low + s + len(base)] ** 2)) for s in shifts]
        spacing.append(round(float(shifts[int(np.argmax(match))] * step / order), 3))
    families = {}
    for macro, amplitude in ((0, 0.2), (100, 0.05), (100, 0.2), (100, 0.45), (50, 0.45), (10, 0.45)):
        magnitude, frequency = spectrum(tone(macro, amplitude))
        power = lambda centre, half: np.sum(magnitude[np.abs(frequency - centre) < half] ** 2)
        families["Macro %d %%, tone amplitude %.2f" % (macro, amplitude)] = [
            round(float(10 * np.log10(power(TONE_HZ + order * RATE / stage.BLOCK, 40.0) / power(TONE_HZ, 40.0))), 1) for order in (1, 2, 3, 4)]
    return {"toneHz": TONE_HZ, "lineSpacingHz": spacing, "blockSamples": [round(RATE / s, 3) for s in spacing],
            "linesAtToneGivenOrderDbReTone": families}


# ---------------------------------------------------------------- everything

def constants() -> dict:
    return {
        "internalRateHz": RATE, "voiceBlockSamples": stage.BLOCK,
        "comb": {"delayMidSamples": stage.COMB_DELAY_MID, "delaySwingSamples": stage.COMB_DELAY_SWING,
                 "delayMidMs": stage.COMB_DELAY_MID / 44.1, "delaySwingMs": stage.COMB_DELAY_SWING / 44.1,
                 "periodSeconds": stage.COMB_PERIOD_SECONDS, "originSamples": stage.COMB_ORIGIN, "rightInputLeadPeriods": stage.COMB_RIGHT_LEAD,
                 "feedback": stage.COMB_FEEDBACK, "highPassHz": stage.COMB_HIGH_PASS_HZ, "lowPassHz": stage.COMB_LOW_PASS_HZ,
                 "read": "first-order all-pass, fraction in [0, 1)", "delayPrecision": "single",
                 "filters": "one-pole bilinear, inside the feedback loop",
                 "mix": "cos / sin of 90 degrees x Macro"},
        "voice": {"amountPerMacro": stage.AMOUNT_PER_MACRO, "fullDepthAmount": stage.FULL_DEPTH_AMOUNT, "curveBend": stage.CURVE,
                  "cutoffTopHz": stage.CUTOFF_TOP, "cutoffEndHz": stage.CUTOFF_END, "gainTop": stage.GAIN_TOP, "restQ": stage.REST_Q,
                  "qKnotHz": list(stage.Q_KNOT_HZ), "qAt0Hz": stage.Q_LOW, "qAt1kHz": stage.Q_1K, "qAt3kHzMacro0And1": list(stage.Q_3K),
                  "qPlateauMacro0And1": list(stage.Q_PLATEAU), "qSmoothingBlocks": stage.Q_SMOOTHING_BLOCKS,
                  "filter": "two-pole low-pass, trapezoidal state-variable form, gain behind it",
                  "lowestCutoffHzByMacroPercent": {str(m): round(stage.lowest_cutoff(m / 100.0), 2) for m in (5, 10, 25, 50, 75, 90, 100)},
                  "qKnotsByMacroPercent": {str(m): [round(float(v), 5) for v in stage.quality_knots(m / 100.0)] for m in (10, 25, 50, 75, 90, 100)}},
    }


def block_size_check() -> dict:
    """The model null at Macro 100 % for three host block sizes (the voice's 44-sample block is internal)."""
    results = {}
    for block in (64, 512, 2048):
        nulls = [fit(window.response, target, nominal_rate(1.0), True)[2]
                 for _, _, _, window, target in model_cases(RATE, 100, 0, 50.0, block_size=block)
                 if np.sum(target ** 2) > 10 ** (OPEN_LEVEL_DB / 10) * np.sum(window.at_rest() ** 2)]
        results[str(block)] = round(float(np.median(nulls)), 2)
    return results


def main() -> None:
    result = {"constants": constants(), "kernelsCompiled": stage._compiled() is not None}
    result["modelScores"] = [score_capture(*capture) for capture in MODEL_CAPTURES]
    for score in result["modelScores"]:
        print("model  %5d Hz  Macro %3d %%  open %3d  phase and rate %7.2f dB  phase only %7.2f dB" % (
            score["hostRate"], score["macroPercent"], score["phaseAndRate"]["cases"], score["phaseAndRate"]["medianDb"], score["phaseOnly"]["medianDb"]))
    result["otherWarmups"] = {"60 s": score_capture(RATE, 50, 0, 100.0, warmup=60.0), "4.321 s": score_capture(RATE, 50, 0, 50.0, warmup=4.321)}
    result["hostBlockSizeModelMedianDb"] = block_size_check()
    cases = main_relation()
    nulls = score_relation(cases, 100)
    energy = [np.sum(case[3] ** 2) for case in cases]
    result["relationMacro100"] = {"phaseAndRate": summary(nulls, energy), "phaseOnly": summary(score_relation(cases, 100, free_rate=False), energy)}
    print("relation Macro 100 %", result["relationMacro100"])
    result["combDelayCrossings"] = crossing_test()
    print("crossings", {key: result["combDelayCrossings"][key] for key in ("events", "matchedSinglePrecision", "matchedDoublePrecision")})
    result["relationOtherSizeDecayMacro50"] = {}
    for name in PAIRS:
        pairs = pair_relation(name)
        result["relationOtherSizeDecayMacro50"][name] = summary(score_relation(pairs, PAIR_MACRO), [np.sum(case[3] ** 2) for case in pairs])
        print("relation", name, result["relationOtherSizeDecayMacro50"][name])
    result["voiceTestsRelationMacro100MedianDb"] = voice_tests(cases[::2])
    result["combTestsModelMacro50MedianDb"] = comb_tests()
    result["crossfade"] = crossfade()
    result["depthSlopeModelMacro3MedianDb"] = depth_slope()
    result["delayLaw"] = delay_law()
    result["tone"] = tone_test()
    DATA.write_text(json.dumps(result, indent=1) + "\n")
    print("wrote", DATA, DATA.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
