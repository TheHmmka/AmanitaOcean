#!/usr/bin/env python3
"""Fit the constants of first_order_a.py and write tide_structural_data/first_order_a.npz.

Run from this folder (about three minutes; every capture comes from the cache or is rendered again):

    /Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python fit_first_order_a.py

The structure is fixed in first_order_a.py (prime line lengths, 187 samples of
common latency, 0.88 ms of modulation, the float32 accumulator of a 0.6 Hz LFO).
This script measures what is left, on the shared 100 ms grid of impulse times
(datasets.grid_responses, Macro 0, Decay 0.5 s):

1. the pre kernel (44.1 kHz lattice, before the lines) and the post kernel
   (48 kHz, after the lines), by Gauss-Newton on raw samples [1200, 2300), where
   only the first passes of lines 1 to 7 of each network arrive;
2. the two phase constants of the LFO bank (phase of line L1 and the step from
   line to line), by least squares on the same window;
3. the gain of every first pass and of every second pass, by linear least
   squares on raw samples [1200, 2800).

The fits use every twelfth (kernels, LFO) and every tenth (gains) grid time.
Validation uses grid times that no fit has seen and own captures at impulse
times off the 160-sample conversion period. The locked holdout is never read
here. Besides the data file the script writes first_order_a.json, a readable
summary of the constants and of the validation nulls.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy.signal import fftconvolve

import datasets
import first_order_a as model_module
import revocean
from first_order_a import LATENCY, PRIMES, RATIO, Model

HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "tide_structural_data" / "first_order_a.npz"
SUMMARY = HERE / "tide_structural_data" / "first_order_a.json"

PRE_HALF = 44                  # pre kernel taps -44..44 on the internal lattice
POST_FIRST_LAG = -48           # post kernel lags -48..1000 host samples
POST_FREE_STOP = 160           # every lag below this is free; later lags follow a piecewise-linear tail
POST_LAST_LAG = 1000
POST_KNOT = 40
HOST_START = 1100              # nothing reaches the output before raw sample 1235
KERNEL_WINDOW = (1200, 2300)   # first passes of lines 1 to 7 only
GAIN_WINDOW = (1200, 2800)
KERNEL_LINES = 7
GAIN_LINES = 9
SECOND_PASS_LIMIT = 2500       # prime sum up to which a second pass is a regressor in the gain fit
SECOND_PASS_KEPT = 2340        # prime sum up to which its gain is well determined and stored
PRE_RIDGE = 1e-6               # weight of |pre - impulse|^2 relative to the reference energy
STOPBAND_HZ = 22200.0          # the post kernel is pinned to zero above this frequency
STOPBAND_WEIGHT = 1e-2         # relative to the mean energy of a post-kernel regressor
# Starting values of the LFO bank, from per-arrival delay fits (findings, section 4).
LFO_START = 6.28315
LFO_STEP = 1.31637
LFO_ORIGIN = model_module.WARMUP_INTERNAL - 116
OFF_GRID_SEED = 7741


def post_basis() -> np.ndarray:
    """Columns that span the post kernel: one per free lag, then linear hats along the smooth tail."""
    lags = np.arange(POST_FIRST_LAG, POST_LAST_LAG + 1)
    free = POST_FREE_STOP - POST_FIRST_LAG
    knots = np.arange(POST_FREE_STOP, POST_LAST_LAG + 1, POST_KNOT)
    basis = np.zeros((len(lags), free + len(knots)))
    basis[np.arange(free), np.arange(free)] = 1.0
    for column, knot in enumerate(knots):
        hat = np.maximum(0.0, 1.0 - np.abs(lags - knot) / POST_KNOT)
        if column == 0:
            hat[lags < knot] = 0.0
        basis[:, free + column] = hat
    return basis


def host_axis(window: tuple) -> np.ndarray:
    return np.arange(HOST_START, window[1] - POST_FIRST_LAG)


def apply_post(ideal: np.ndarray, post: np.ndarray, window: tuple) -> np.ndarray:
    """Convolve host-rate signals (last axis, first sample HOST_START) with a post kernel; keep `window`."""
    full = fftconvolve(ideal, post.reshape((1,) * (ideal.ndim - 1) + (-1,)), mode="full", axes=-1)
    start = window[0] - (HOST_START + POST_FIRST_LAG)
    return full[..., start:start + window[1] - window[0]]


def triangle(position: np.ndarray, taps: np.ndarray) -> np.ndarray:
    """Linear-interpolation weights of reads at `position` on the lattice points `taps`."""
    return np.maximum(0.0, 1.0 - np.abs(position[:, None] - taps[None, :]))


def first_pass_matrix(model: Model, time: int, output: int, line: int, host: np.ndarray) -> np.ndarray:
    """E[j, host]: ideally converted output of one line for a unit lattice impulse at tap j of the pre kernel."""
    origin = time * model_module.INTERNAL_RATE // model_module.HOST_RATE
    taps = np.arange(-PRE_HALF, PRE_HALF + 1)
    centre = LATENCY + PRIMES[output, line]
    lattice = np.arange(int(centre - model.depth) - PRE_HALF - 2, int(centre + model.depth) + PRE_HALF + 3)
    weights = triangle(lattice - LATENCY - model.delay(output, line, lattice, origin), taps)
    return weights.T @ np.sinc(host[None, :] * RATIO - lattice[:, None])


def second_pass_matrix(model: Model, time: int, output: int, source: int, target: int, host: np.ndarray) -> np.ndarray:
    origin = time * model_module.INTERNAL_RATE // model_module.HOST_RATE
    taps = np.arange(-PRE_HALF, PRE_HALF + 1)
    centre = LATENCY + PRIMES[output, source]
    inner = np.arange(int(centre - model.depth) - PRE_HALF - 2, int(centre + model.depth) + PRE_HALF + 3)
    first = triangle(inner - LATENCY - model.delay(output, source, inner, origin), taps)
    centre += PRIMES[output, target]
    outer = np.arange(int(centre - 2 * model.depth) - PRE_HALF - 3, int(centre + 2 * model.depth) + PRE_HALF + 4)
    second = triangle(outer - model.delay(output, target, outer, origin), inner)
    return (second @ first).T @ np.sinc(host[None, :] * RATIO - outer[:, None])


def null_db(residual: np.ndarray, reference: np.ndarray) -> float:
    return float(10.0 * np.log10(np.sum(residual ** 2) / np.sum(reference ** 2)))


def observed(responses: list, rows: np.ndarray, window: tuple) -> np.ndarray:
    """Reference samples [response, input, output, sample] of the window."""
    return np.stack([responses[i][rows, window[0]:window[1], :].astype(float).transpose(0, 2, 1) for i in (0, 1)], 1)


def kernel_tensor(model: Model, times: np.ndarray, gains: np.ndarray) -> np.ndarray:
    """E[response, input, output, pre tap, host] of the first passes of lines 1..KERNEL_LINES with their gains."""
    host = host_axis(KERNEL_WINDOW)
    tensor = np.zeros((len(times), 2, 2, 2 * PRE_HALF + 1, len(host)))
    for a, time in enumerate(times):
        for output in (0, 1):
            for line in range(KERNEL_LINES):
                matrix = first_pass_matrix(model, int(time), output, line, host)
                tensor[a, :, output] += gains[output, line, :, None, None] * matrix[None]
    return tensor


def stopband_rows() -> np.ndarray:
    """Rows that read the content of a post kernel above the internal Nyquist frequency.

    The ideally converted signal holds nothing above 22.05 kHz, so the data leave that part of
    the post kernel free; these rows (an orthonormal cosine/sine set on the kernel's lags) pin it to zero.
    """
    lags = np.arange(POST_FIRST_LAG, POST_LAST_LAG + 1)
    count = len(lags)
    bins = np.arange(int(np.ceil(STOPBAND_HZ / revocean.SAMPLE_RATE * count)), count // 2 + 1)
    angle = 2.0 * np.pi * bins[:, None] * (lags - POST_FIRST_LAG)[None, :] / count
    return np.sqrt(2.0 / count) * np.concatenate([np.cos(angle), np.sin(angle)])


def fit_kernels(tensor: np.ndarray, reference: np.ndarray, pre: np.ndarray, iterations: int = 14) -> tuple:
    """Gauss-Newton on (pre, post) for out = post * convert(lines(pre)); post is solved exactly after every step.

    Only the motion of the delays tells the two kernels apart, so the split is weakly determined. A ridge
    (PRE_RIDGE times the reference energy) on the distance of the pre kernel from a unit impulse selects
    the smallest pre kernel that explains the data.
    """
    basis = post_basis()
    y = reference.ravel()
    taps = tensor.shape[3]
    impulse = np.zeros(taps)
    impulse[taps // 2] = 1.0
    ridge = PRE_RIDGE * (y @ y)
    stopband = stopband_rows() @ basis

    def post_design(pre):
        ideal = np.einsum("aiojv,j->aiov", tensor, pre)
        return np.stack([apply_post(ideal, basis[:, c], KERNEL_WINDOW).ravel() for c in range(basis.shape[1])], 1)

    def solve_post(design):
        weight = np.sqrt(STOPBAND_WEIGHT * np.sum(design ** 2) / design.shape[1])
        theta = np.linalg.lstsq(np.concatenate([design, weight * stopband]), np.concatenate([y, np.zeros(len(stopband))]), rcond=None)[0]
        return theta, weight

    def penalised(residual, pre, theta, weight):
        return residual @ residual + ridge * np.sum((pre - impulse) ** 2) + np.sum((weight * stopband @ theta) ** 2)

    design = post_design(pre)
    theta, weight = solve_post(design)
    residual = design @ theta - y
    cost = penalised(residual, pre, theta, weight)
    damping = 1e-3
    for _ in range(iterations):
        post = basis @ theta
        pre_design = np.stack([apply_post(tensor[:, :, :, j, :], post, KERNEL_WINDOW).ravel() for j in range(taps)], 1)
        jacobian = np.concatenate([pre_design, design], 1)
        normal = jacobian.T @ jacobian
        gradient = jacobian.T @ residual
        normal[:taps, :taps] += ridge * np.eye(taps)
        gradient[:taps] += ridge * (pre - impulse)
        normal[taps:, taps:] += weight ** 2 * stopband.T @ stopband
        gradient[taps:] += weight ** 2 * stopband.T @ (stopband @ theta)
        while damping < 1e3:
            step = np.linalg.solve(normal + damping * np.diag(np.diag(normal)), -gradient)
            trial_pre = pre + step[:taps]
            trial_design = post_design(trial_pre)
            trial_theta, trial_weight = solve_post(trial_design)
            trial_residual = trial_design @ trial_theta - y
            trial_cost = penalised(trial_residual, trial_pre, trial_theta, trial_weight)
            if trial_cost < cost:
                pre, design, theta, weight = trial_pre, trial_design, trial_theta, trial_weight
                residual, cost = trial_residual, trial_cost
                damping = max(damping / 4.0, 1e-9)
                break
            damping *= 10.0
    return pre, basis @ theta, null_db(residual, y)


def fit_first_gains(model: Model, times: np.ndarray, reference: np.ndarray) -> np.ndarray:
    """Gains [output, line, input] of lines 1..KERNEL_LINES on the kernel window, for the model's kernels."""
    host = host_axis(KERNEL_WINDOW)
    gains = np.zeros((2, GAIN_LINES, 2))
    for output in (0, 1):
        columns = np.stack([np.stack([apply_post(model.pre @ first_pass_matrix(model, int(time), output, line, host),
                                                 model.post, KERNEL_WINDOW) for line in range(KERNEL_LINES)], 1)
                            for time in times]).reshape(-1, KERNEL_LINES)
        for i in (0, 1):
            gains[output, :KERNEL_LINES, i] = np.linalg.lstsq(columns, reference[:, i, output].ravel(), rcond=None)[0]
    return gains


def second_pass_candidates(output: int) -> list:
    return [(output, source, target) for target in range(9) for source in range(9)
            if PRIMES[output, source] + PRIMES[output, target] <= SECOND_PASS_LIMIT]


def fit_gains(model: Model, times: np.ndarray, reference: np.ndarray) -> tuple:
    """First-pass gains [output, line, input] and second-pass (pairs, gains[pair, input]) on the gain window."""
    host = host_axis(GAIN_WINDOW)
    first = np.zeros((2, GAIN_LINES, 2))
    pairs, second, nulls = [], [], {}
    for output in (0, 1):
        candidates = second_pass_candidates(output)
        columns = []
        for time in times:
            passes = [model.pre @ first_pass_matrix(model, int(time), output, line, host) for line in range(GAIN_LINES)]
            passes += [model.pre @ second_pass_matrix(model, int(time), *pair, host) for pair in candidates]
            columns.append(apply_post(np.stack(passes), model.post, GAIN_WINDOW).T)
        design = np.concatenate(columns)
        solution = []
        for i in (0, 1):
            y = reference[:, i, output].ravel()
            solution.append(np.linalg.lstsq(design, y, rcond=None)[0])
            nulls[f"{'LR'[i]}->{'LR'[output]}"] = round(null_db(design @ solution[-1] - y, y), 2)
        solution = np.stack(solution, 1)
        first[output] = solution[:GAIN_LINES]
        for pair, gain in zip(candidates, solution[GAIN_LINES:]):
            if PRIMES[output, pair[1]] + PRIMES[output, pair[2]] <= SECOND_PASS_KEPT:
                pairs.append(pair)
                second.append(gain)
    return first, np.array(pairs), np.array(second), nulls


def fit_lfo(model: Model, times: np.ndarray, reference: np.ndarray, start: float, step: float) -> tuple:
    """Phase of line L1 and the step from line to line: Gauss-Newton with central differences on the kernel window."""
    host = host_axis(KERNEL_WINDOW)
    probe = np.array([2e-5, 3e-6])                  # well above the float32 grain of a phase near 2 pi

    def residual(x):
        trial = Model({**vars_of(model), "lfo_start": x[0], "lfo_step": x[1]})
        out = np.zeros_like(reference)
        for a, time in enumerate(times):
            for output in (0, 1):
                passes = np.stack([trial.pre @ first_pass_matrix(trial, int(time), output, line, host)
                                   for line in range(KERNEL_LINES)])
                out[a, :, output] = np.einsum("li,lv->iv", trial.first_gain[output, :KERNEL_LINES],
                                              apply_post(passes, trial.post, KERNEL_WINDOW))
        return (out - reference).ravel()

    x = np.array([start, step])
    for _ in range(4):
        jacobian = np.stack([(residual(x + probe * unit) - residual(x - probe * unit)) / (2.0 * probe @ unit)
                             for unit in np.eye(2)], 1)
        x = x - np.linalg.lstsq(jacobian, residual(x), rcond=None)[0]
    return float(x[0]), float(x[1])


def vars_of(model: Model) -> dict:
    return {"pre": model.pre, "post": model.post, "post_first_lag": model.post_first_lag, "first_gain": model.first_gain,
            "second_pairs": model.second_pairs, "second_gain": model.second_gain, "lfo_start": model.lfo_start,
            "lfo_step": model.lfo_step, "lfo_origin": model.lfo_origin, "depth": model.depth}


def off_grid_times(variant: int) -> np.ndarray:
    """Own validation times: 0.55 s apart with a random offset, so every phase of the 160-sample period occurs."""
    generator = np.random.default_rng(OFF_GRID_SEED + variant)
    base = np.arange(revocean.SAMPLE_RATE + 1000, int(98.5 * revocean.SAMPLE_RATE), 26500)
    return base + generator.integers(0, 2400, len(base))


def off_grid_responses(variant: int, channel: int, length: int = 2500) -> tuple:
    times = off_grid_times(variant)
    stimulus = revocean.impulses(datasets.GRID_SECONDS, [(int(t), channel, datasets.IMPULSE_AMPLITUDE) for t in times])
    output = revocean.capture(stimulus, datasets.GRID_SETTINGS).output
    return times, np.stack([output[int(t):int(t) + length] for t in times]) / datasets.IMPULSE_AMPLITUDE


def validate(model: Model, times: np.ndarray, responses: list, window: tuple = (1200, 2500)) -> dict:
    """Null of the model on reference responses[input][time, sample, output], per path and overall."""
    difference = np.zeros((2, 2))
    energy = np.zeros((2, 2))
    worst = -1000.0
    for row, time in enumerate(times):
        predicted = model.responses(int(time))[:, window[0]:window[1], :]
        reference = np.stack([responses[i][row, window[0]:window[1], :].astype(float) for i in (0, 1)])
        d = np.sum((predicted - reference) ** 2, axis=1)
        r = np.sum(reference ** 2, axis=1)
        difference += d
        energy += r
        worst = max(worst, 10.0 * np.log10(d.sum() / r.sum()))
    result = {f"{'LR'[i]}->{'LR'[o]}": round(float(10.0 * np.log10(difference[i, o] / energy[i, o])), 2) for i in (0, 1) for o in (0, 1)}
    result["overallDb"] = round(float(10.0 * np.log10(difference.sum() / energy.sum())), 2)
    result["worstResponseDb"] = round(float(worst), 2)
    result["responses"] = int(len(times))
    return result


def main() -> None:
    times, left = datasets.grid_responses(0, 3000)
    _, right = datasets.grid_responses(1, 3000)
    grid = [left, right]
    fit_rows = np.arange(2, len(times), 12)
    gain_rows = np.arange(3, len(times), 10)
    check_rows = np.arange(7, len(times), 20)       # never used by a fit

    pre = np.zeros(2 * PRE_HALF + 1)
    pre[PRE_HALF] = 1.0
    gains = np.zeros((2, GAIN_LINES, 2))
    for output in (0, 1):                           # rough start: unit direct gains, the cross feed of lines 1 to 6
        gains[output, :KERNEL_LINES, output] = 1.0
        gains[output, :6, 1 - output] = 0.57
    constants = {"pre": pre, "post": np.zeros(POST_LAST_LAG - POST_FIRST_LAG + 1), "post_first_lag": POST_FIRST_LAG,
                 "first_gain": gains, "second_pairs": np.zeros((0, 3), int), "second_gain": np.zeros((0, 2)),
                 "lfo_start": LFO_START, "lfo_step": LFO_STEP, "lfo_origin": LFO_ORIGIN}
    model = Model(constants)
    reference = observed(grid, fit_rows, KERNEL_WINDOW)
    report = {"kernelWindowNullDb": []}
    for round_index in range(3):
        tensor = kernel_tensor(model, times[fit_rows], model.first_gain)
        pre, post, null = fit_kernels(tensor, reference, model.pre, iterations=14 if round_index == 0 else 6)
        del tensor
        model = Model({**vars_of(model), "pre": pre, "post": post})
        model = Model({**vars_of(model), "first_gain": fit_first_gains(model, times[fit_rows], reference)})
        if round_index < 2:
            start, step = fit_lfo(model, times[fit_rows[::2]], reference[::2], model.lfo_start, model.lfo_step)
            model = Model({**vars_of(model), "lfo_start": start, "lfo_step": step})
        report["kernelWindowNullDb"].append(round(null, 2))
        print(f"round {round_index}: kernel window null {null:.2f} dB, LFO start {model.lfo_start:.7f} step {model.lfo_step:.7f}", flush=True)

    first, pairs, second, nulls = fit_gains(model, times[gain_rows], observed(grid, gain_rows, GAIN_WINDOW))
    # Normalisation: line L1 from the left input has gain 1 and the pre kernel sums to 1; the post kernel carries the level.
    unit = first[0, 0, 0]
    model = Model({**vars_of(model), "first_gain": first / unit, "second_pairs": pairs, "second_gain": second / unit,
                   "pre": model.pre / model.pre.sum(), "post": model.post * unit * model.pre.sum()})
    report["gainWindowNullDb"] = nulls

    OUTPUT.parent.mkdir(exist_ok=True)
    np.savez(OUTPUT, **vars_of(model))
    report["gridValidation"] = validate(model, times[check_rows], [grid[0][check_rows], grid[1][check_rows]])
    for variant in (0, 1):
        off_times, off_left = off_grid_responses(variant, 0)
        _, off_right = off_grid_responses(variant, 1)
        rows = np.arange(0, len(off_times), 3)
        report[f"offGridValidation{variant}"] = validate(model, off_times[rows], [off_left[rows], off_right[rows]])
    report["constants"] = {
        "lfoStartRad": model.lfo_start, "lfoStepRad": model.lfo_step, "lfoOriginSteps": model.lfo_origin,
        "depthInternalSamples": model.depth, "latencyInternalSamples": LATENCY,
        "primesLeft": PRIMES[0].tolist(), "primesRight": PRIMES[1].tolist(),
        "firstGain": np.round(model.first_gain, 5).tolist(),
        "secondPass": [{"output": int(o), "source": int(s) + 1, "target": int(t) + 1, "gainFromLeft": round(float(g[0]), 5),
                        "gainFromRight": round(float(g[1]), 5)} for (o, s, t), g in zip(model.second_pairs, model.second_gain)],
    }
    SUMMARY.write_text(json.dumps(report, indent=1))
    print(json.dumps({key: value for key, value in report.items() if key != "constants"}, indent=1))


if __name__ == "__main__":
    main()
