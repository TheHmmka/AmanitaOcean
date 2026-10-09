#!/usr/bin/env python3
"""Regenerate tide_structural_data/first_order_c.npz, the constants of first_order_c.py, from captures.

    python fit_first_order_c.py            (about ten minutes; every capture is cached)

The fit has two stages, both at the neutral baseline with Macro 0 and Decay 0.5 s.

Stage 1 renders impulse trains with the host at 44.1 kHz. At that rate the
plug-in does not convert the sample rate, so the core is seen directly: every
arrival is a pair of samples (linear interpolation of a delay line) followed
by the response of one fixed filter. With the structure held fixed (prime line
lengths, 0.88 ms of modulation, the float32 phase accumulator at 0.6 Hz) the
stage fits, by alternating linear least squares on the waveforms,

* the fixed filter (960 taps),
* the gain of every line for both inputs and of the six second passes that
  reach the scored window,
* the start phase of the sixteen accumulators (one-dimensional searches),
* the short kernel of the feedback path (40 taps, common to both groups).

Stage 2 renders impulse trains at 48 kHz at times off the 160-sample period of
the rate conversion and fits the three constants of the converters that the
core cannot supply: the delay of the input converter, the total delay and the
gain. The converter kernel itself (Kaiser-windowed sinc, cut-off 0.9, 17
internal samples each side, beta 6) is a fixed hypothesis; findings/first_order_c.md
gives the evidence for it.

The locked holdout is not read here.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor

import numpy as np
from scipy import sparse
from scipy.optimize import minimize, minimize_scalar

import datasets
import first_order_c as model
import revocean

AMPLITUDE = 0.5
CORE_RATE = 44100
CORE_WARMUP = 10 * CORE_RATE
CORE_LATENCY = 44                 # samples the plug-in reports at 44.1 kHz; the lines are read this much before the output
TANK_PREDELAY = 44                # further fixed samples before the lines: raw arrival = 88 + line length
CORE_FRAMES = 2400                # raw samples kept per 44.1 kHz response
CLEAN = (1000, 2060)              # raw window that holds first passes only
JOINT = (1000, 2262)              # raw window with the second passes that reach the 48 kHz score window
FILTER_TAPS = 960
LOOP_TAPS = 40

LINE_PRIME = np.array([[1031, 1097, 1187, 1289, 1423, 1583, 1783, 2027],
                       [1039, 1109, 1193, 1301, 1429, 1597, 1787, 2039]])
MODULATION_DEPTH = 0.88e-3 * CORE_RATE          # 38.808 internal samples
LFO_HZ = 0.6
PHASE_STEP = 4.966812                           # first guess of the start phase of accumulator k: k * PHASE_STEP
PAIRS = np.array([(0, 0), (0, 1), (1, 0), (1, 1), (0, 2), (2, 0)])
CONVERTER = (0.9, 17.0, 6.0)
FIT_SEEDS = (2001, 2002)
VALIDATION_SEEDS = (2003,)


def train_times(seed: int, rate: int, gap: float, jitter: float) -> np.ndarray:
    """Impulse times `gap` to `gap + jitter` seconds apart inside a 100 s render."""
    generator = np.random.default_rng(seed)
    times = [rate + int(generator.integers(0, int(jitter * rate)))]
    while True:
        following = times[-1] + int(gap * rate) + int(generator.integers(0, int(jitter * rate)))
        if following >= 99 * rate:
            return np.array(times)
        times.append(following)


def responses(times: np.ndarray, channel: int, length: int, rate: int) -> np.ndarray:
    stimulus = revocean.impulses(100.0, [(int(t), channel, AMPLITUDE) for t in times], sample_rate=rate)
    output = revocean.capture(stimulus, datasets.GRID_SETTINGS, sample_rate=rate).output
    return np.stack([output[int(t):int(t) + length] for t in times]).astype(np.float64) / AMPLITUDE


def both_inputs(times: np.ndarray, length: int, rate: int) -> np.ndarray:
    """responses[input, impulse, sample, output]"""
    with ThreadPoolExecutor(2) as pool:
        return np.stack(list(pool.map(lambda channel: responses(times, channel, length, rate), (0, 1))))


# ---------------------------------------------------------------- stage 1: the core at 44.1 kHz

def accumulator(start_phase: float) -> tuple:
    increment = np.float32(2.0 * np.pi * LFO_HZ / CORE_RATE)
    return model.accumulator_segments(start_phase, increment, CORE_WARMUP + 101 * CORE_RATE)


class Core:
    """Tap trains of the lines and of the second passes for unit gain, on raw 44.1 kHz indices."""

    def __init__(self, start_phase: np.ndarray):
        self.segments = [accumulator(phase) for phase in start_phase]

    def with_phase(self, index: int, phase: float) -> "Core":
        """A copy in which accumulator `index` starts at `phase`."""
        other = Core([])
        other.segments = list(self.segments)
        other.segments[index] = accumulator(phase)
        return other

    def length(self, side: int, line: int, time: int, raw) -> np.ndarray:
        step = CORE_WARMUP + time + np.asarray(raw) - CORE_LATENCY
        phase = model.accumulator_phase(self.segments[2 * line + side], step)
        return LINE_PRIME[side][line] + MODULATION_DEPTH * np.sin(phase)

    def line(self, side: int, line: int, time: int) -> tuple:
        """(raw indices, weights) of the first pass of a line for an impulse at `time`."""
        centre = LINE_PRIME[side][line] + CORE_LATENCY + TANK_PREDELAY
        raw = np.arange(centre - model.MODULATION_REACH, centre + model.MODULATION_REACH + 1)
        weight = model.triangle(raw - CORE_LATENCY - TANK_PREDELAY - self.length(side, line, time, raw))
        return raw[weight > 0], weight[weight > 0]

    def pair(self, side: int, first: int, second: int, time: int) -> tuple:
        """Second pass: the output of line `first` written into line `second`."""
        fed, signal = self.line(side, first, time)
        centre = fed[0] + LINE_PRIME[side][second]
        raw = np.arange(centre - model.MODULATION_REACH, fed[-1] + LINE_PRIME[side][second] + model.MODULATION_REACH + 1)
        length = self.length(side, second, time, raw)
        weight = model.triangle(raw[:, None] - length[:, None] - fed[None, :]) @ signal
        return raw[weight > 0], weight[weight > 0]

    def trains(self, side: int, time: int) -> np.ndarray:
        """Rows: the 8 lines, then the 6 pairs; columns: raw samples 0 .. CORE_FRAMES."""
        rows = np.zeros((8 + len(PAIRS), CORE_FRAMES))
        for line in range(8):
            raw, weight = self.line(side, line, time)
            keep = raw < CORE_FRAMES
            rows[line, raw[keep]] = weight[keep]
        for index, (first, second) in enumerate(PAIRS):
            raw, weight = self.pair(side, int(first), int(second), time)
            keep = raw < CORE_FRAMES
            rows[8 + index, raw[keep]] = weight[keep]
        return rows


def filtered(rows: np.ndarray, kernel: np.ndarray, window: tuple) -> np.ndarray:
    """Columns = rows convolved with the kernel, cut to the raw window."""
    return np.stack([np.convolve(row, kernel)[window[0]:window[1]] for row in rows], axis=1)


def solve_gains(trains: dict, reference: np.ndarray, times: np.ndarray, core_filter: np.ndarray,
                loop_kernel: np.ndarray, window: tuple, terms: int) -> tuple:
    """Least-squares gain of the first `terms` trains for each path; returns (gains[input][side], null in dB)."""
    gains = np.zeros((2, 2, terms))
    residual = total = 0.0
    for side in (0, 1):
        normal = np.zeros((terms, terms))
        projections = np.zeros((2, terms))
        energy = np.zeros(2)
        for k in range(len(times)):
            rows = trains[side][k][:terms].copy()
            if terms > 8:
                rows[8:] = [np.convolve(row, loop_kernel)[:CORE_FRAMES] for row in rows[8:]]
            design = filtered(rows, core_filter, window)
            normal += design.T @ design
            for channel in (0, 1):
                target = reference[channel, k, window[0]:window[1], side]
                projections[channel] += design.T @ target
                energy[channel] += target @ target
        for channel in (0, 1):
            # lstsq, not solve: a line that never enters the window leaves an empty column
            gains[channel, side] = np.linalg.lstsq(normal, projections[channel], rcond=1e-12)[0]
            fitted = gains[channel, side]
            residual += energy[channel] - 2 * fitted @ projections[channel] + fitted @ normal @ fitted
            total += energy[channel]
    return gains, 10 * np.log10(residual / total)


def solve_filter(trains: dict, reference: np.ndarray, times: np.ndarray, gains: np.ndarray) -> np.ndarray:
    """Least-squares fixed filter on the clean window, given the first-pass gains."""
    start, stop = CLEAN
    blocks, targets = [], []
    for side in (0, 1):
        for channel in (0, 1):
            for k in range(len(times)):
                train = gains[channel, side, :8] @ trains[side][k][:8]
                taps = np.nonzero(train[:stop])[0]
                rows = (taps[:, None] + np.arange(FILTER_TAPS)[None, :]).ravel()
                columns = np.tile(np.arange(FILTER_TAPS), len(taps))
                values = np.repeat(train[taps], FILTER_TAPS)
                keep = (rows >= start) & (rows < stop)
                blocks.append(sparse.csr_matrix((values[keep], (rows[keep] - start, columns[keep])),
                                                shape=(stop - start, FILTER_TAPS)))
                targets.append(reference[channel, k, start:stop, side])
    design = sparse.vstack(blocks).tocsr()
    normal = (design.T @ design).toarray()
    kernel = np.linalg.solve(normal + 1e-14 * np.trace(normal) / FILTER_TAPS * np.eye(FILTER_TAPS),
                             design.T @ np.concatenate(targets))
    return kernel


def refine_phase(start_phase: np.ndarray, reference: np.ndarray, times: np.ndarray, core_filter: np.ndarray,
                 loop_kernel: np.ndarray, gains: np.ndarray) -> np.ndarray:
    """One-dimensional search of each accumulator's start phase on the samples around its line's taps."""
    core = Core(start_phase)
    refined = start_phase.copy()
    short = core_filter[:6]
    for side in (0, 1):
        full = [core.trains(side, int(t)) for t in times]
        for line in range(8):
            others = []
            for k in range(len(times)):
                rows = full[k].copy()
                rows[8:] = [np.convolve(row, loop_kernel)[:CORE_FRAMES] for row in rows[8:]]
                rows[line] = 0.0
                rest = np.convolve(gains[side, side] @ rows, core_filter)[:CORE_FRAMES]
                others.append(reference[side, k, :CORE_FRAMES, side] - rest)

            def cost(phase: float) -> float:
                candidate = core.with_phase(2 * line + side, phase)
                total = 0.0
                for k, t in enumerate(times):
                    raw, weight = candidate.line(side, line, int(t))
                    if raw[-1] + 4 >= JOINT[1]:
                        continue
                    train = np.zeros(raw[-1] - raw[0] + 6)
                    train[2:2 + len(raw)] = weight
                    local = gains[side, side, line] * np.convolve(train, short)[:len(train)]
                    total += np.sum((others[k][raw[0] - 2:raw[0] - 2 + len(train)] - local) ** 2)
                return total

            centre = start_phase[2 * line + side]
            found = minimize_scalar(cost, bounds=(centre - 6e-5, centre + 6e-5), method="bounded",
                                    options={"xatol": 2e-7})
            refined[2 * line + side] = found.x
    return refined


def solve_loop_kernel(trains: dict, reference: np.ndarray, times: np.ndarray, core_filter: np.ndarray,
                      gains: np.ndarray) -> np.ndarray:
    """Common kernel of the feedback path: the second passes regressed on shifted copies of themselves."""
    start, stop = 2040, JOINT[1]
    kernels = []
    for side in (0, 1):
        normal = np.zeros((LOOP_TAPS, LOOP_TAPS))
        projection = np.zeros(LOOP_TAPS)
        for k in range(len(times)):
            first = np.convolve(gains[side, side, :8] @ trains[side][k][:8], core_filter)[:CORE_FRAMES]
            second = np.convolve(gains[side, side, 8:] @ trains[side][k][8:], core_filter)[:CORE_FRAMES]
            design = np.stack([np.concatenate([np.zeros(lag), second[:CORE_FRAMES - lag]])[start:stop]
                               for lag in range(LOOP_TAPS)], axis=1)
            target = (reference[side, k, :CORE_FRAMES, side] - first)[start:stop]
            normal += design.T @ design
            projection += design.T @ target
        kernels.append(np.linalg.solve(normal, projection))
    return np.mean(kernels, axis=0)


def fit_core() -> dict:
    times = train_times(1001, CORE_RATE, 0.5, 0.1)
    reference = both_inputs(times, 3000, CORE_RATE)
    start_phase = (np.arange(16) * PHASE_STEP) % (2 * np.pi)
    core_filter = np.zeros(FILTER_TAPS)
    core_filter[0] = 1.0
    loop_kernel = np.array([1.0])
    for round_index in range(3):
        core = Core(start_phase)
        trains = {side: [core.trains(side, int(t)) for t in times] for side in (0, 1)}
        for _ in range(3):
            gains, _ = solve_gains(trains, reference, times, core_filter, loop_kernel, CLEAN, 7)
            padded = np.zeros((2, 2, 8))
            padded[:, :, :7] = gains
            core_filter = solve_filter(trains, reference, times, padded)
            core_filter /= core_filter[0]
        gains, null = solve_gains(trains, reference, times, core_filter, loop_kernel, JOINT, 8 + len(PAIRS))
        print(f"round {round_index}: joint null at 44.1 kHz over raw {JOINT}: {null:.2f} dB", flush=True)
        if round_index == 2:
            break
        loop_kernel = solve_loop_kernel(trains, reference, times, core_filter, gains)
        gains, _ = solve_gains(trains, reference, times, core_filter, loop_kernel, JOINT, 8 + len(PAIRS))
        start_phase = refine_phase(start_phase, reference, times, core_filter, loop_kernel, gains)
    _, clean_null = solve_gains(trains, reference, times, core_filter, loop_kernel, CLEAN, 8)
    cross = np.array([np.median([gains[1 - side, side, line] / gains[side, side, line] for side in (0, 1)])
                      for line in range(8)])
    lines_one_to_six = np.concatenate([gains[1 - side, side, :6] / gains[side, side, :6] for side in (0, 1)])
    cross[:6] = np.mean(lines_one_to_six)
    cross[6] = 0.0
    print(f"cross-input ratio, lines 1 to 6: {cross[0]:.7f} (spread {np.ptp(lines_one_to_six):.1e}); line 8: {cross[7]:.5f}")
    return {
        "start_phase": start_phase, "core_filter": core_filter, "loop_kernel": loop_kernel,
        "line_gain": np.array([gains[side, side, :8] for side in (0, 1)]),
        "pair_gain": np.array([gains[side, side, 8:] for side in (0, 1)]),
        "cross_ratio": cross, "core_null_joint_db": null, "core_null_clean_db": clean_null,
    }


# ---------------------------------------------------------------- stage 2: the converters at 48 kHz

def host_null(constants: dict, sets: list, stride: int = 1) -> float:
    candidate = model.Model(constants)
    difference = total = 0.0
    window = slice(model.WINDOW_START, model.WINDOW_STOP)
    for times, reference in sets:
        for channel in (0, 1):
            for k in range(0, len(times), stride):
                predicted = candidate.predict(int(times[k]), channel)[window]
                difference += np.sum((predicted - reference[channel, k, window]) ** 2)
                total += np.sum(reference[channel, k, window] ** 2)
    return 10 * np.log10(difference / total)


def host_sets(seeds: tuple) -> list:
    sets = []
    for seed in seeds:
        times = train_times(seed, model.HOST_RATE, 0.75, 0.2)
        sets.append((times, both_inputs(times, 2700, model.HOST_RATE)))
    return sets


def fit_converters(constants: dict) -> dict:
    fit = host_sets(FIT_SEEDS)

    def cost(vector: np.ndarray) -> float:
        trial = dict(constants, input_delay=vector[0], total_delay=vector[1], converter_gain=vector[2])
        return host_null(trial, fit, stride=2)

    start = np.array([26.56, 203.28, 1.0])
    simplex = start + np.vstack([np.zeros(3), np.diag([0.03, 0.002, 3e-4])])
    found = minimize(cost, start, method="Nelder-Mead",
                     options={"initial_simplex": simplex, "xatol": 1e-5, "fatol": 1e-3, "maxfev": 300})
    constants = dict(constants, input_delay=found.x[0], total_delay=found.x[1], converter_gain=found.x[2])
    constants["fit_null_db"] = host_null(constants, fit)
    constants["validation_null_db"] = host_null(constants, host_sets(VALIDATION_SEEDS))
    return constants


def main() -> None:
    revocean.identity()
    constants = fit_core()
    constants.update({
        "line_prime": LINE_PRIME, "modulation_depth": MODULATION_DEPTH, "lfo_hz": LFO_HZ, "pairs": PAIRS,
        "tank_predelay": TANK_PREDELAY, "converter_cutoff": CONVERTER[0], "converter_half_length": CONVERTER[1],
        "converter_beta": CONVERTER[2],
    })
    constants = fit_converters(constants)
    print(f"input delay {constants['input_delay']:.4f} internal samples, total delay {constants['total_delay']:.5f} "
          f"host samples, converter gain {constants['converter_gain']:.6f}")
    print(f"48 kHz null: fit {constants['fit_null_db']:.2f} dB, validation {constants['validation_null_db']:.2f} dB")
    model.DATA.parent.mkdir(parents=True, exist_ok=True)
    np.savez(model.DATA, **{key: np.asarray(value) for key, value in constants.items()})
    print(f"wrote {model.DATA}")


if __name__ == "__main__":
    main()
