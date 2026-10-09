#!/usr/bin/env python3
"""Regenerate tide_structural_data/first_order.json, the constants of first_order_model.py, from captures.

    python fit_first_order.py            (about 40 seconds once the captures are cached)

Everything is rendered at the neutral baseline with Macro 0, Mix 100 %, Size
100 % and Decay 0.5 s. The structure is fixed in first_order_model.py and by
the STRUCTURE block below (prime line lengths, 0.88 ms of modulation, the
0.6 Hz single-precision accumulators that start at 11.25 k rad, the width law
of the cross feed, the Kaiser converters and their lattice delays). The script
measures what is left, in two stages.

Stage 1, host at 44.1 kHz: the plug-in does not convert the rate, so the
network is seen directly. Impulses at least 1 s apart (the previous response
is 120 dB down). Alternating linear least squares on the waveforms give

* the fixed filter behind the lines (960 taps),
* the gain of the first pass of lines 1 to 8 for both inputs, and of the six
  second passes that reach the scored window,
* the loop kernel that a second pass carries (40 taps).

The gains are then reduced to the model's constants: one coefficient per line
index (the Decay attenuation removed, both groups averaged), one feedback
magnitude and the output weights of lines 2 and 3. The cross feed is not
fitted: it follows the width law, and the measured ratios are stored next to
it. The first passes of lines 9 to 16, which lie outside the scored window,
are searched for and measured by coherent projection, for the table of lines.

Stage 2, host at 48 kHz: nothing is fitted. The converters are fixed by
STRUCTURE; the script only measures the least-squares gain of the model on two
impulse trains (it must be 1) and the cost of changing each converter constant.

The script then scores the model on captures no fit has seen (fresh impulse
times at 48 kHz, a second train at 44.1 kHz, a capture with another warm-up)
and stores the scores, the round-value tests and the ablations in the data
file. Two further checks are stored with them: the part of the 44.1 kHz
residual that is proportional to the speed of the line length, and a test of
the model at Macro 100 %, Mix 100 % (the owner's listening point) on the
captures of the Tide packet. The locked holdout is not read here: run
`python score_first_order.py first_order_model.py` for that.
"""
from __future__ import annotations

import json
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from scipy import sparse

import datasets
import first_order_model as model
import revocean

AMPLITUDE = 0.5
CORE_RATE = model.INTERNAL_RATE
CORE_WARMUP = 10 * CORE_RATE
CORE_OUTPUT_DELAY = 44            # samples between a line read and the output at 44.1 kHz (the reported latency)
CORE_FRAMES = 2400                # raw samples kept per 44.1 kHz response
CLEAN = (1000, 2060)              # raw 44.1 kHz window with the first passes of lines 1 to 8 only
JOINT = (1000, 2262)              # the same with the second passes that reach the 48 kHz score window
FILTER_TAPS = 960
LOOP_TAPS = 40
FITTED_LINES = 8
ROUNDS = 5

STRUCTURE = {
    "internal_rate_hz": CORE_RATE,
    "decay_seconds": 0.5,
    "tank_predelay_internal_samples": 44,
    "oscillator": {"rate_hz": 0.6, "depth_ms": 0.88, "phase_step_rad": 11.25,
                   "order": "accumulator k = 2 (line - 1) + group; group 0 = left output, 1 = right output"},
    "lines": {
        "prime": [[1031, 1097, 1187, 1289, 1423, 1583, 1783, 2027, 2333, 2699, 3163, 3719, 4409, 5261, 6299, 7589],
                  [1039, 1109, 1193, 1301, 1429, 1597, 1787, 2039, 2333, 2707, 3163, 3719, 4409, 5261, 6299, 7589]],
        # width weight w: own input (1 + w)/2, other input (1 - w)/2 of the line's input gain
        "width": [0.272] * 6 + [1.0 - (line - 7) * 0.728 / 9.0 for line in range(7, 17)],
    },
    "second_pass": {"pairs": [[1, 1], [1, 2], [2, 1], [2, 2], [1, 3], [3, 1]], "sign": [1, 1, -1, 1, 1, -1]},
    "converter": {"cutoff": 0.9, "zero_crossings": 17, "kaiser_beta": 6.0, "table_entries_per_sample": 4096,
                  "exact_entry_low_wing": {"input": 1, "output": -1},
                  "input_delay_internal_samples": 26.0 + 90.0 / 160.0,
                  "output_delay_host_samples": 126.0 + 70.0 / 147.0},
}
CORE_FIT_SEED, CORE_CHECK_SEED = 5101, 5102       # 44.1 kHz trains, impulses 1.0 to 1.25 s apart
HOST_FIT_SEEDS = (2001, 2002)                     # 48 kHz trains, impulses 0.75 to 0.95 s apart
HOST_DEV_SEED = 2003                              # the same kind of train, used for the ablations only
HOST_FRESH_SEEDS = {"clean": (5201, 1.0, 0.3), "dense": (5202, 0.5, 0.25)}
WARMUP_CHECK = (5301, 4.321)                      # seed and warm-up seconds of the time-origin check
SLOPE_KERNEL_TAPS = 24
LINE_SEARCH = (2100, 12000)                       # whole centres scanned for first passes beyond line 8
LINE_SEARCH_FAR = 4600                            # from accumulator 30 on the scan starts here: the strong lines
                                                  # below 4500 leak 0.017 into every other accumulator's projection
LINE_SEARCH_ACCUMULATORS = range(16, 40)          # lines 9 to 20 of both groups, were they to exist
# Comb delay in front of the network at Macro above 0, from findings/tide.md: host samples at 48 kHz,
# mid + swing * triangle(seconds since processing started / period), left input.
TIDE_DELAY_MID, TIDE_DELAY_SWING, TIDE_DELAY_PERIOD = 469.34, 256.90, 200.0
TIDE_FILTER_LAGS = np.arange(-8, 57)              # free filter per impulse in the Macro 100 % check


# ---------------------------------------------------------------- captures

def train_times(seed: int, rate: int, gap: float, jitter: float, seconds: float = 100.0) -> np.ndarray:
    """Impulse times `gap` to `gap + jitter` seconds apart inside a render of `seconds`."""
    generator = np.random.default_rng(seed)
    times = [rate + int(generator.integers(0, int(jitter * rate)))]
    while True:
        following = times[-1] + int(gap * rate) + int(generator.integers(0, int(jitter * rate)))
        if following >= (seconds - 1.0) * rate:
            return np.array(times)
        times.append(following)


def responses(times: np.ndarray, channel: int, length: int, rate: int, seconds: float = 100.0,
              warmup: float = revocean.WARMUP_SECONDS) -> np.ndarray:
    stimulus = revocean.impulses(seconds, [(int(t), channel, AMPLITUDE) for t in times], sample_rate=rate)
    output = revocean.capture(stimulus, datasets.GRID_SETTINGS, sample_rate=rate, warmup=warmup).output
    return np.stack([output[int(t):int(t) + length] for t in times]).astype(np.float64) / AMPLITUDE


def both_inputs(times: np.ndarray, length: int, rate: int, **options) -> np.ndarray:
    """responses[input, impulse, sample, output]"""
    with ThreadPoolExecutor(2) as pool:
        return np.stack(list(pool.map(lambda channel: responses(times, channel, length, rate, **options), (0, 1))))


# ---------------------------------------------------------------- stage 1: the network at 44.1 kHz

def unit_network() -> model.Network:
    """A network with unit coefficients, for the unit-gain reads the fits are built from."""
    constants = json.loads(json.dumps(STRUCTURE))
    constants["lines"]["coefficient"] = [1.0] * 16
    constants["second_pass"].update(feedback_gain=1.0, output_weight=[1.0, 1.0, 1.0], loop_kernel=[1.0])
    constants["fixed_filter"] = [1.0]
    return model.Network(constants, CORE_WARMUP + 101 * CORE_RATE)


def unit_trains(network: model.Network, group: int, time: int) -> np.ndarray:
    """Rows: first passes of lines 1 to 8, then the six second passes, each for unit gain; columns: raw samples."""
    rows = np.zeros((FITTED_LINES + len(network.pairs), CORE_FRAMES))
    written = np.array([CORE_WARMUP + time + STRUCTURE["tank_predelay_internal_samples"]])
    origin = CORE_WARMUP + time - CORE_OUTPUT_DELAY                 # raw index = read index - origin
    passes = []
    for line in range(FITTED_LINES):
        read, samples = network.read(group, line, written, np.ones(1))
        passes.append((read, samples))
        keep = read - origin < CORE_FRAMES
        rows[line, read[keep] - origin] = samples[keep]
    for index, (first, later) in enumerate(network.pairs):
        read, samples = network.read(group, later, *passes[first])
        keep = read - origin < CORE_FRAMES
        rows[FITTED_LINES + index, read[keep] - origin] = samples[keep]
    return rows


def with_loop(rows: np.ndarray, loop_kernel: np.ndarray) -> np.ndarray:
    result = rows.copy()
    result[FITTED_LINES:] = [np.convolve(row, loop_kernel)[:CORE_FRAMES] for row in rows[FITTED_LINES:]]
    return result


def filtered(rows: np.ndarray, kernel: np.ndarray, window: tuple) -> np.ndarray:
    return np.stack([np.convolve(row, kernel)[window[0]:window[1]] for row in rows], axis=1)


def solve_gains(trains: list, reference: np.ndarray, fixed_filter: np.ndarray, loop_kernel: np.ndarray,
                window: tuple, terms: int) -> tuple:
    """Least-squares gain of the first `terms` trains for every path; returns (gains[input][group], null in dB)."""
    gains = np.zeros((2, 2, terms))
    residual = total = 0.0
    for group in (0, 1):
        normal = np.zeros((terms, terms))
        projections = np.zeros((2, terms))
        energy = np.zeros(2)
        for k, rows in enumerate(trains[group]):
            design = filtered(with_loop(rows, loop_kernel)[:terms], fixed_filter, window)
            normal += design.T @ design
            for channel in (0, 1):
                target = reference[channel, k, window[0]:window[1], group]
                projections[channel] += design.T @ target
                energy[channel] += target @ target
        for channel in (0, 1):
            fitted = np.linalg.lstsq(normal, projections[channel], rcond=1e-12)[0]
            gains[channel, group] = fitted
            residual += energy[channel] - 2 * fitted @ projections[channel] + fitted @ normal @ fitted
            total += energy[channel]
    return gains, 10 * np.log10(residual / total)


def solve_filter(trains: list, reference: np.ndarray, gains: np.ndarray, loop_kernel: np.ndarray, window: tuple) -> np.ndarray:
    """Least-squares fixed filter given the gains of the trains."""
    start, stop = window
    terms = gains.shape[2]
    blocks, targets = [], []
    for group in (0, 1):
        for channel in (0, 1):
            for k, rows in enumerate(trains[group]):
                train = gains[channel, group] @ with_loop(rows, loop_kernel)[:terms]
                taps = np.nonzero(train[:stop])[0]
                row_index = (taps[:, None] + np.arange(FILTER_TAPS)[None, :]).ravel()
                columns = np.tile(np.arange(FILTER_TAPS), len(taps))
                values = np.repeat(train[taps], FILTER_TAPS)
                keep = (row_index >= start) & (row_index < stop)
                blocks.append(sparse.csr_matrix((values[keep], (row_index[keep] - start, columns[keep])),
                                                shape=(stop - start, FILTER_TAPS)))
                targets.append(reference[channel, k, start:stop, group])
    design = sparse.vstack(blocks).tocsr()
    normal = (design.T @ design).toarray()
    ridge = 1e-14 * np.trace(normal) / FILTER_TAPS * np.eye(FILTER_TAPS)
    return np.linalg.solve(normal + ridge, design.T @ np.concatenate(targets))


def solve_loop_kernel(trains: list, reference: np.ndarray, gains: np.ndarray, fixed_filter: np.ndarray) -> np.ndarray:
    """Common kernel of the feedback path: what is left after the first passes, regressed on the second passes."""
    start, stop = 2040, JOINT[1]
    normal = np.zeros((LOOP_TAPS, LOOP_TAPS))
    projection = np.zeros(LOOP_TAPS)
    for group in (0, 1):
        for k, rows in enumerate(trains[group]):
            own = gains[group, group]
            first = np.convolve(own[:FITTED_LINES] @ rows[:FITTED_LINES], fixed_filter)[:CORE_FRAMES]
            second = np.convolve(own[FITTED_LINES:] @ rows[FITTED_LINES:], fixed_filter)[:CORE_FRAMES]
            design = np.stack([np.concatenate([np.zeros(lag), second[:CORE_FRAMES - lag]])[start:stop]
                               for lag in range(LOOP_TAPS)], axis=1)
            target = (reference[group, k, :CORE_FRAMES, group] - first)[start:stop]
            normal += design.T @ design
            projection += design.T @ target
    return np.linalg.solve(normal, projection)


def reduce_gains(gains: np.ndarray, network: model.Network) -> dict:
    """The fitted path gains as the model's constants, with the spread each reduction leaves."""
    decay = 10.0 ** (-3.0 * network.prime[:, :FITTED_LINES] / (CORE_RATE * STRUCTURE["decay_seconds"]))
    own = np.array([gains[group, group, :FITTED_LINES] for group in (0, 1)])
    pair = np.array([gains[group, group, FITTED_LINES:] for group in (0, 1)])
    coefficient = own / decay
    cross = np.array([gains[1 - group, group, :FITTED_LINES] / own[group] for group in (0, 1)])
    feedback, weight2, weight3 = [], [], []
    for group in (0, 1):
        g, d, s = own[group], decay[group], pair[group]
        feedback += [s[0] / (g[0] * d[0]), s[3] / (g[1] * d[1]),
                     np.sqrt(-s[1] * s[2] / (g[0] * g[1] * d[0] * d[1])), np.sqrt(-s[4] * s[5] / (g[0] * g[2] * d[0] * d[2]))]
        weight2.append(np.sqrt(-s[1] / s[2] * (g[1] * d[0]) / (g[0] * d[1])))
        weight3.append(np.sqrt(-s[4] / s[5] * (g[2] * d[0]) / (g[0] * d[2])))
    return {
        "coefficient": coefficient.mean(axis=0), "coefficient_group_spread": float(np.max(np.abs(coefficient[1] / coefficient[0] - 1))),
        "cross_ratio_measured": cross, "feedback_gain": float(np.mean(feedback)), "feedback_estimates": feedback,
        "output_weight": [1.0, float(np.mean(weight2)), float(np.mean(weight3))],
        "pair_cross_ratio": np.array([gains[1 - group, group, FITTED_LINES:] / pair[group] for group in (0, 1)]),
    }


def core_null(network: model.Network, times: np.ndarray, reference: np.ndarray, window: tuple) -> float:
    """Null of the complete network model at a 44.1 kHz host."""
    difference = total = 0.0
    for k, time in enumerate(times):
        written = np.array([CORE_WARMUP + int(time) + STRUCTURE["tank_predelay_internal_samples"]])
        first_raw = STRUCTURE["tank_predelay_internal_samples"] + CORE_OUTPUT_DELAY      # raw index of written[0]
        for channel in (0, 1):
            for group in (0, 1):
                predicted = np.zeros(CORE_FRAMES)
                predicted[first_raw:] = network.output(written, np.ones(1), channel, group, CORE_FRAMES - first_raw)
                target = reference[channel, k, window[0]:window[1], group]
                difference += np.sum((predicted[window[0]:window[1]] - target) ** 2)
                total += np.sum(target ** 2)
    return 10 * np.log10(difference / total)


def fit_network() -> tuple:
    times = train_times(CORE_FIT_SEED, CORE_RATE, 1.0, 0.25)
    reference = both_inputs(times, 3000, CORE_RATE)
    network = unit_network()
    trains = [[unit_trains(network, group, int(t)) for t in times] for group in (0, 1)]
    fixed_filter = np.zeros(FILTER_TAPS)
    fixed_filter[0] = 1.0
    loop_kernel = np.ones(1)
    for round_index in range(ROUNDS):
        for _ in range(3):
            gains, _ = solve_gains(trains, reference, fixed_filter, loop_kernel, CLEAN, FITTED_LINES)
            fixed_filter = solve_filter(trains, reference, gains, loop_kernel, CLEAN)
            fixed_filter /= fixed_filter[0]
        gains, null = solve_gains(trains, reference, fixed_filter, loop_kernel, JOINT, FITTED_LINES + len(network.pairs))
        print(f"round {round_index}: free gains, 44.1 kHz raw {JOINT}: {null:.2f} dB", flush=True)
        loop_kernel = solve_loop_kernel(trains, reference, gains, fixed_filter)
    gains, free_null = solve_gains(trains, reference, fixed_filter, loop_kernel, JOINT, FITTED_LINES + len(network.pairs))
    reduced = reduce_gains(gains, network)
    scale = loop_kernel[0]                       # the loop kernel is stored with a first tap of 1
    constants = json.loads(json.dumps(STRUCTURE))
    constants["lines"]["coefficient"] = [float(value) for value in reduced["coefficient"]] + [0.0] * 8
    constants["second_pass"].update(feedback_gain=reduced["feedback_gain"] * scale, output_weight=reduced["output_weight"],
                                    loop_kernel=[float(value) for value in loop_kernel / scale])
    constants["fixed_filter"] = [float(value) for value in fixed_filter]
    evidence = {
        "coreFreeGainsNullDb": free_null,
        "coefficientGroupSpread": reduced["coefficient_group_spread"],
        "crossRatioMeasured": reduced["cross_ratio_measured"].tolist(),
        "pairCrossRatioMeasured": reduced["pair_cross_ratio"].tolist(),
        "feedbackEstimates": [float(value) * scale for value in reduced["feedback_estimates"]],
    }
    return constants, evidence, (times, reference)


def far_line_coefficients(constants: dict, sets: list) -> tuple:
    """First-pass coefficient of lines 9 to 16 (Decay attenuation removed) by coherent projection.

    The response of each impulse is projected on the line's two interpolation
    samples; passes of other lines move differently from impulse to impulse and
    average out. The projection is calibrated on lines 1 to 8, whose
    coefficients are known from the waveform fit.
    """
    network = model.Network(constants, CORE_WARMUP + 101 * CORE_RATE)
    decay = 10.0 ** (-3.0 * network.prime / (CORE_RATE * STRUCTURE["decay_seconds"]))
    projection = np.zeros((2, 2, 16))                                  # [input, group, line]
    for group in (0, 1):
        for line in range(16):
            numerator = np.zeros(2)
            denominator = 0.0
            for times, reference in sets:
                for k, time in enumerate(times):
                    written = np.array([CORE_WARMUP + int(time) + STRUCTURE["tank_predelay_internal_samples"]])
                    read, samples = network.read(group, line, written, np.ones(1))
                    raw = read - (CORE_WARMUP + int(time)) + CORE_OUTPUT_DELAY
                    numerator += reference[:, k, raw, group] @ samples
                    denominator += samples @ samples
            projection[:, group, line] = numerator / denominator
    own = np.array([projection[group, group] for group in (0, 1)]) / decay
    known = np.array(constants["lines"]["coefficient"][:FITTED_LINES])
    calibration = float(np.mean(own[:, :FITTED_LINES] / known))
    cross = np.array([projection[1 - group, group] / projection[group, group] for group in (0, 1)])
    return own / calibration, cross


def line_search(sets: list) -> list:
    """Which accumulators own a line beyond the eighth: for accumulator k, the whole centre in LINE_SEARCH
    whose two interpolation samples collect the most of the own-input response over all impulses.

    Returns one row per accumulator: (k, best centre, coefficient there with the Decay attenuation removed,
    ratio of that coefficient to the largest one more than 2 samples away from the best centre). From
    accumulator 30 on only centres above LINE_SEARCH_FAR compete.
    """
    low, high = LINE_SEARCH
    oscillator = STRUCTURE["oscillator"]
    depth = oscillator["depth_ms"] * 1e-3 * CORE_RATE
    increment = np.float32(2.0 * np.pi * oscillator["rate_hz"] / CORE_RATE)
    predelay = STRUCTURE["tank_predelay_internal_samples"]
    raw = np.arange(predelay + CORE_OUTPUT_DELAY + low - model.MODULATION_REACH, predelay + CORE_OUTPUT_DELAY + high + model.MODULATION_REACH)
    centres = np.arange(low, high + 1)
    decay = 10.0 ** (-3.0 * centres / (CORE_RATE * STRUCTURE["decay_seconds"]))
    rows = []
    for k in LINE_SEARCH_ACCUMULATORS:
        segments = model.accumulator_segments(model.oscillator_start(k, oscillator["phase_step_rad"]), increment,
                                              CORE_WARMUP + 101 * CORE_RATE)
        numerator = np.zeros(len(centres) + 2)
        denominator = np.zeros(len(centres) + 2)
        for times, reference in sets:
            for index, time in enumerate(times):
                phase = model.accumulator_phase(segments, CORE_WARMUP + int(time) + raw - CORE_OUTPUT_DELAY)
                position = raw - predelay - CORE_OUTPUT_DELAY - depth * np.sin(phase)       # centre this sample would belong to
                whole = np.floor(position).astype(np.int64)
                part = position - whole
                samples = reference[k % 2, index, raw, k % 2]
                for centre, weight in ((whole, 1.0 - part), (whole + 1, part)):
                    keep = (centre >= low) & (centre <= high)
                    numerator += np.bincount(centre[keep] - low, weights=weight[keep] * samples[keep], minlength=len(numerator))
                    denominator += np.bincount(centre[keep] - low, weights=weight[keep] ** 2, minlength=len(denominator))
        coefficient = numerator[:len(centres)] / np.maximum(denominator[:len(centres)], 1e-30) / decay
        if k >= 30:
            coefficient[centres < LINE_SEARCH_FAR] = 0.0
        best = int(np.argmax(np.abs(coefficient)))
        elsewhere = np.abs(coefficient[np.abs(centres - centres[best]) > 2]).max()
        rows.append([int(k), int(centres[best]), float(coefficient[best]), float(abs(coefficient[best]) / elsewhere)])
    return rows


# ---------------------------------------------------------------- stage 2: the converters at 48 kHz

def host_sets(seed: int, gap: float, jitter: float, seconds: float = 100.0, warmup: float = revocean.WARMUP_SECONDS) -> tuple:
    """(impulse times, responses[input, impulse, sample, output]) of one 48 kHz train."""
    times = train_times(seed, model.HOST_RATE, gap, jitter, seconds)
    return times, both_inputs(times, model.WINDOW_STOP, model.HOST_RATE, seconds=seconds, warmup=warmup)


def host_products(candidate: model.Model, sets: list) -> tuple:
    """(<prediction, reference>, <prediction, prediction>, <reference, reference>) over the scored window."""
    window = slice(model.WINDOW_START, model.WINDOW_STOP)
    cross = own = energy = 0.0
    for times, reference in sets:
        for channel in (0, 1):
            for k, time in enumerate(times):
                predicted = candidate.predict(int(time), channel)[window]
                target = reference[channel, k, window]
                cross += np.sum(predicted * target)
                own += np.sum(predicted ** 2)
                energy += np.sum(target ** 2)
    return cross, own, energy


def host_null(constants: dict, sets: list, **options) -> float:
    cross, own, energy = host_products(model.Model(constants, **options), sets)
    return 10 * np.log10((energy - 2 * cross + own) / energy)


def converter_gain(constants: dict) -> float:
    """Least-squares gain of the model on the 48 kHz fit trains; 1 when the converters are right."""
    cross, own, _ = host_products(model.Model(constants), [host_sets(seed, 0.75, 0.2) for seed in HOST_FIT_SEEDS])
    return cross / own


# ---------------------------------------------------------------- scores and the data file

def changed(constants: dict, path: tuple, value) -> dict:
    """A copy of the constants with one entry replaced."""
    trial = json.loads(json.dumps(constants))
    target = trial
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    return trial


def ablations(constants: dict) -> dict:
    """Null on a 48 kHz train no fit has used (seed 2003) with one constant changed at a time."""
    sets = [host_sets(HOST_DEV_SEED, 0.75, 0.2)]
    converter = constants["converter"]
    variants = {
        "model": constants,
        "feedback gain 1/4": changed(constants, ("second_pass", "feedback_gain"), 0.25),
        "no loop kernel": changed(constants, ("second_pass", "loop_kernel"), [1.0]),
        "no second passes": changed(changed(constants, ("second_pass", "pairs"), []), ("second_pass", "sign"), []),
        "output weights 1": changed(constants, ("second_pass", "output_weight"), [1.0, 1.0, 1.0]),
        "depth 0.8803 ms": changed(constants, ("oscillator", "depth_ms"), 0.8803),
        "rate 0.599878 Hz": changed(constants, ("oscillator", "rate_hz"), 0.599878),
        "phase step 11.2499 rad": changed(constants, ("oscillator", "phase_step_rad"), 11.2499),
        "width of lines 1 to 6: 0.27": changed(constants, ("lines", "width"), [0.27] * 6 + constants["lines"]["width"][6:]),
        "cross feed 1/sqrt(3) on lines 1 to 6": changed(constants, ("lines", "width"),
                                                         [(1 - 3 ** -0.5) / (1 + 3 ** -0.5)] * 6 + constants["lines"]["width"][6:]),
        "table 2048 entries": changed(constants, ("converter", "table_entries_per_sample"), 2048),
        "table 8192 entries": changed(constants, ("converter", "table_entries_per_sample"), 8192),
        "table 262144 entries (as good as exact kernel)": changed(constants, ("converter", "table_entries_per_sample"), 262144),
        "exact entries not lowered": changed(constants, ("converter", "exact_entry_low_wing"), {"input": 2, "output": 2}),
        "exact entries lowered on the other wing": changed(constants, ("converter", "exact_entry_low_wing"), {"input": -1, "output": 1}),
        "input delay + 1/160": changed(constants, ("converter", "input_delay_internal_samples"),
                                       converter["input_delay_internal_samples"] + 1 / 160),
        "output delay + 1/147": changed(constants, ("converter", "output_delay_host_samples"),
                                        converter["output_delay_host_samples"] + 1 / 147),
        "kaiser beta 5.9": changed(constants, ("converter", "kaiser_beta"), 5.9),
        "cutoff 0.899": changed(constants, ("converter", "cutoff"), 0.899),
        "16 zero crossings": changed(constants, ("converter", "zero_crossings"), 16),
        "feedback sign of 2 -> 1 flipped": changed(constants, ("second_pass", "sign"), [1, 1, 1, 1, 1, -1]),
    }
    return {name: host_null(trial, sets) for name, trial in variants.items()}


def slope_kernel(constants: dict, core_fit: tuple) -> dict:
    """The part of the 44.1 kHz residual that is proportional to the speed of the line length.

    residual = (first passes weighted by d length / d sample) * kernel * fixed filter, the kernel fitted on
    the fit train and applied to the unseen train. It is evidence, not part of the model.
    """
    network = model.Network(constants, CORE_WARMUP + 101 * CORE_RATE)
    start, stop = CLEAN
    predelay = STRUCTURE["tank_predelay_internal_samples"]
    first_raw = predelay + CORE_OUTPUT_DELAY
    rate = 2.0 * np.pi * STRUCTURE["oscillator"]["rate_hz"] / CORE_RATE

    def system(times: np.ndarray, reference: np.ndarray) -> tuple:
        designs, residuals, energy = [], [], 0.0
        for k, time in enumerate(times):
            written = np.array([CORE_WARMUP + int(time) + predelay])
            origin = CORE_WARMUP + int(time) - CORE_OUTPUT_DELAY
            for group in (0, 1):
                for channel in (0, 1):
                    predicted = np.zeros(CORE_FRAMES)
                    predicted[first_raw:] = network.output(written, np.ones(1), channel, group, CORE_FRAMES - first_raw)
                    weighted = np.zeros(CORE_FRAMES)
                    for line, gain in enumerate(network.first_gain(channel, group)[:FITTED_LINES]):
                        read, samples = network.read(group, line, written, np.ones(1))
                        speed = network.depth * rate * np.cos(model.accumulator_phase(network.segments[group][line], read))
                        weighted[read - origin] += gain * samples * speed
                    shaped = np.convolve(weighted, network.fixed_filter)[:CORE_FRAMES]
                    designs.append(np.stack([np.concatenate([np.zeros(lag), shaped[:CORE_FRAMES - lag]])[start:stop]
                                             for lag in range(SLOPE_KERNEL_TAPS)], axis=1))
                    residuals.append(reference[channel, k, start:stop, group] - predicted[start:stop])
                    energy += np.sum(reference[channel, k, start:stop, group] ** 2)
        return np.concatenate(designs), np.concatenate(residuals), energy

    design, residual, energy = system(*core_fit)
    kernel = np.linalg.lstsq(design, residual, rcond=None)[0]
    times = train_times(CORE_CHECK_SEED, CORE_RATE, 1.0, 0.25)
    check_design, check_residual, check_energy = system(times, both_inputs(times, 3000, CORE_RATE))
    return {
        "kernel": kernel.tolist(),
        "fitTrainDb": [10 * np.log10(np.sum(residual ** 2) / energy), 10 * np.log10(np.sum((residual - design @ kernel) ** 2) / energy)],
        "unseenTrainDb": [10 * np.log10(np.sum(check_residual ** 2) / check_energy),
                          10 * np.log10(np.sum((check_residual - check_design @ kernel) ** 2) / check_energy)],
    }


def tide_exit(time: float) -> float:
    """Host time at which an impulse at `time` leaves the Tide comb delay (the delay in force at the exit)."""
    leave = float(time)
    for _ in range(6):
        phase = ((revocean.WARMUP_SECONDS + leave / model.HOST_RATE) / TIDE_DELAY_PERIOD + 0.25) % 1.0
        leave = time + TIDE_DELAY_MID + TIDE_DELAY_SWING * (1.0 - 4.0 * abs(phase - 0.5))
    return leave


def tide_check(constants: dict) -> dict:
    """The model at Macro 100 %, Mix 100 %: each response of the reference is regressed, with one free short
    filter per impulse, on the model's response to an impulse at the first exit of the Tide comb delay.

    The window ends before the comb's second exit arrives. Controls: the model evaluated at the impulse time
    and merely shifted, and the model with its oscillators one second late. A free filter cannot repair either.
    """
    candidate = model.Model(constants)
    grid = datasets.grid_times(0)
    stimulus = revocean.impulses(datasets.GRID_SECONDS, [(int(t), 0, datasets.IMPULSE_AMPLITUDE) for t in grid])
    settings = {**datasets.GRID_SETTINGS, "macro": 1.0}
    result = {"impulses": 120, "filterTaps": len(TIDE_FILTER_LAGS)}

    def source(time: int, shift: int) -> np.ndarray:
        padded = np.zeros(6000)
        padded[shift:shift + model.WINDOW_STOP] = candidate.predict(time, 0)[:, 0]
        return padded

    def null(target: np.ndarray, regressor: np.ndarray, begin: int, end: int) -> float:
        design = np.stack([np.roll(regressor, lag)[begin:end] for lag in TIDE_FILTER_LAGS], axis=1)
        solution = np.linalg.lstsq(design, target[begin:end], rcond=None)[0]
        return 10 * np.log10(np.sum((target[begin:end] - design @ solution) ** 2) / np.sum(target[begin:end] ** 2))

    for realisation in (0, 1):
        captured = revocean.capture(stimulus, settings, realisation=realisation)
        output = captured.output.astype(np.float64) / datasets.IMPULSE_AMPLITUDE
        rows = []
        for time in grid[:result["impulses"]]:
            first = int(round(tide_exit(time)))
            second = int(round(tide_exit(tide_exit(time))))
            begin, end = first - time + model.WINDOW_START, min(first - time + model.WINDOW_STOP, second - time + 1240)
            target = output[time:time + 6000, 0]
            rows.append([null(target, source(first, first - time), begin, end),
                         null(target, source(int(time), first - time), begin, end),
                         null(target, source(first + model.HOST_RATE, first - time), begin, end)])
        medians = np.median(np.array(rows), axis=0)
        result[f"realisation{realisation}"] = {
            "readback": {"mix": captured.meta["readback"]["id:1"], "macro": captured.meta["readback"]["id:6"]},
            "modelAtCombExitMedianDb": float(medians[0]), "modelAtCombExitBestDb": float(np.min(np.array(rows)[:, 0])),
            "modelAtImpulseTimeMedianDb": float(medians[1]), "oscillatorsOneSecondLateMedianDb": float(medians[2])}
    return result


def readback(seed: int) -> dict:
    """What the plug-in reports for Mix and Macro in one of the fit captures."""
    times = train_times(seed, model.HOST_RATE, 0.75, 0.2)
    stimulus = revocean.impulses(100.0, [(int(t), 0, AMPLITUDE) for t in times])
    meta = revocean.capture(stimulus, datasets.GRID_SETTINGS).meta["readback"]
    return {"mix": meta["id:1"], "macro": meta["id:6"], "decay": meta["id:3"], "size": meta["id:4"], "width": meta["id:14"]}


def validation(constants: dict, core_fit: tuple) -> dict:
    scores = {}
    network = model.Network(constants, CORE_WARMUP + 101 * CORE_RATE)
    scores["core44k1FitTrainDb"] = core_null(network, *core_fit, JOINT)
    times = train_times(CORE_CHECK_SEED, CORE_RATE, 1.0, 0.25)
    scores["core44k1UnseenTrainDb"] = core_null(network, times, both_inputs(times, 3000, CORE_RATE), JOINT)
    scores["host48kFitTrainsDb"] = host_null(constants, [host_sets(seed, 0.75, 0.2) for seed in HOST_FIT_SEEDS])
    for name, (seed, gap, jitter) in HOST_FRESH_SEEDS.items():
        scores[f"host48kFresh{name.capitalize()}Db"] = host_null(constants, [host_sets(seed, gap, jitter)])
    seed, warmup = WARMUP_CHECK
    sets = [host_sets(seed, 1.0, 0.3, seconds=13.0, warmup=warmup)]
    scores["host48kOtherWarmupDb"] = host_null(constants, sets, warmup_frames=int(round(warmup * model.HOST_RATE)))
    scores["host48kOtherWarmupWrongOriginDb"] = host_null(constants, sets)
    return scores


def main() -> None:
    revocean.identity()
    constants, evidence, core_fit = fit_network()
    check = train_times(CORE_CHECK_SEED, CORE_RATE, 1.0, 0.25)
    long_sets = [(core_fit[0], both_inputs(core_fit[0], 12200, CORE_RATE)), (check, both_inputs(check, 12200, CORE_RATE))]
    evidence["lineSearch"] = line_search(long_sets)
    far, far_cross = far_line_coefficients(constants, long_sets)
    constants["lines"]["coefficient"][FITTED_LINES:] = [float(value) for value in far.mean(axis=0)[FITTED_LINES:]]
    evidence["projectedCoefficient"] = far.tolist()
    evidence["projectedCrossRatio"] = far_cross.tolist()
    evidence["converterGainLeastSquares"] = converter_gain(constants)
    constants["operating_point"] = {"macro_percent": 0, "mix_percent": 100, "size_percent": 100, "decay_seconds": 0.5,
                                    "host_rate_hz": model.HOST_RATE, "warmup_seconds": revocean.WARMUP_SECONDS,
                                    "readback": readback(HOST_FIT_SEEDS[0])}
    constants["scores"] = validation(constants, core_fit)
    constants["ablations48kDb"] = ablations(constants)
    evidence["slopeKernel"] = slope_kernel(constants, core_fit)
    constants["tide100Check"] = tide_check(constants)
    constants["evidence"] = evidence
    for name, value in {**constants["scores"], **constants["ablations48kDb"]}.items():
        print(f"{name}: {value:.2f} dB")
    for k, centre, coefficient, ratio in evidence["lineSearch"]:
        print(f"accumulator {k}: best centre {centre}, coefficient {coefficient:+.4f}, {ratio:.1f} times the next candidate")
    print(f"least-squares converter gain: {evidence['converterGainLeastSquares']:.6f}")
    print(f"slope kernel, unseen 44.1 kHz train: {evidence['slopeKernel']['unseenTrainDb'][0]:.2f} -> "
          f"{evidence['slopeKernel']['unseenTrainDb'][1]:.2f} dB")
    print(f"Macro 100 %, Mix 100 %: {json.dumps(constants['tide100Check'])}")
    model.DATA.parent.mkdir(parents=True, exist_ok=True)
    model.DATA.write_text(json.dumps(constants, indent=1))
    print(f"wrote {model.DATA}")


if __name__ == "__main__":
    main()
