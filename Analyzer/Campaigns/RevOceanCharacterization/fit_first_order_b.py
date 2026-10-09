#!/usr/bin/env python3
"""Fit the constants of `first_order_b.py` and write `tide_structural_data/first_order_b.npz`.

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    $PY fit_first_order_b.py             # fit and checks; under three minutes once the captures are cached
    $PY fit_first_order_b.py --quick     # fit only
    $PY fit_first_order_b.py --report    # score the stored constants on the working data only

Data (all through `revocean.capture`, neutral baseline, Macro 0, Decay 0.5 s):

* `datasets.grid_responses`: 980 impulses per input on a 100 ms grid. Every time is a multiple of 160
  samples, so all of them see the 44.1 kHz core lattice at the same position. Each response sits on the
  tail of the impulse 0.5 s earlier, 64 dB below the scored window.
* clean captures made here: three renders per input, impulses 1.0 to 1.5 s apart at random times
  (the earlier tail is 139 dB down, every lattice position occurs). The third render is the validation
  split: nothing is fitted on it.

The fit works on the core lattice (the captures are resampled to it with a band-limited interpolator),
where the model is linear in the post-kernel and in the gains:

1. coarse search of the sixteen accumulator start phases on the grid;
2. post-kernel (free taps plus exponential tails) and gains by alternating least squares, with all of
   the fixed kernel after the lines;
3. pre-kernel = zero-phase sqrt(|composite| / |20 kHz, Q 1 low-pass|): the converter in front of the
   lines takes half of the two converters' magnitude, everything dispersive stays behind the lines;
4. start phases by non-linear least squares; ratios of the second passes on a longer window; the short
   kernel between the two reads of a second pass;
5. gains of the two cross paths.

The checks stored next to the constants (`facts` in the data file) are captures with other warm-up
lengths, ideal sinusoids in place of the single-precision oscillator, other placements of the fixed kernel
around the lines, the model without its second passes, and probes at Macro 50 % and 100 %.

The locked holdout is never read here.
"""
from __future__ import annotations

import argparse
import json

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import freqz

import datasets
import first_order_b as model
import revocean

CLEAN_RENDERS = 3                 # the last one is the validation split
CLEAN_LENGTH = 4000
EXTENDED_STOP = 2700              # raw samples; window for the second-pass ratios
LOW_PASS = (20000.0, 1.0)         # corner (Hz) and Q of the biquad behind the lines
PRE_FLAT = 20                     # pre-kernel taps kept untapered on each side of its centre
POST_FIRST, POST_FREE = 92, 300   # free post-kernel taps start here
POST_PEAK = 141                   # lag of the post-kernel's peak: 187 samples of fixed delay less PRE_CENTRE
TAIL_TIMES = (250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0)   # core samples
FIXED_DELAY = model.PRE_CENTRE + POST_PEAK   # 187 core samples outside the lines
LOOP_TAPS = 12                    # taps of the kernel between the two reads of a second pass
LATTICE_FIRST = 700               # the fit lattice starts before any excitation and any free tap
RESAMPLER = model.windowed_sinc_table(64, 12.0, model.CORE_STEP)   # host lattice -> continuous time


def core_window(start: int, stop: int) -> tuple:
    """Core indices [first, stop), relative to the impulse, that lie inside raw samples [start, stop);
    the fit compares model and captures there."""
    return (start * model.CORE_STEP) // model.HOST_STEP + 2, ((stop - 1) * model.CORE_STEP) // model.HOST_STEP


def to_core(times: np.ndarray, responses: np.ndarray, first: int, stop: int) -> np.ndarray:
    """Resample raw responses [impulse, sample, channel] to core indices [first, stop)."""
    kappa = np.arange(first, stop)
    part = (times * model.CORE_STEP) % model.HOST_STEP
    out = np.empty((len(times), len(kappa), responses.shape[2]))
    for row in range(len(times)):
        position = model.HOST_STEP * kappa - part[row]             # 147 * (host samples after the impulse)
        low = int(position[0] // model.CORE_STEP) - 64
        high = int(position[-1] // model.CORE_STEP) + 66
        sample = np.arange(low, high)
        weights = model.table_lookup(RESAMPLER, position[:, None] - model.CORE_STEP * sample[None, :])
        out[row] = weights @ responses[row, low:high].astype(np.float64)
    return out


def clean_times(render: int) -> np.ndarray:
    generator = np.random.default_rng(7100 + render)
    times = [revocean.SAMPLE_RATE + int(generator.integers(0, 12000))]
    while True:
        following = times[-1] + 48000 + int(generator.integers(0, 24000))
        if following > int(98.8 * revocean.SAMPLE_RATE):
            break
        times.append(following)
    return np.array(times)


def clean_responses(render: int, input_channel: int) -> tuple:
    times = clean_times(render)
    stimulus = revocean.impulses(100.0, [(int(t), input_channel, datasets.IMPULSE_AMPLITUDE) for t in times])
    output = revocean.capture(stimulus, datasets.GRID_SETTINGS).output
    return times, np.stack([output[int(t):int(t) + CLEAN_LENGTH] for t in times]) / datasets.IMPULSE_AMPLITUDE


class Responses:
    """Impulse times with the captures of both inputs on core indices [first, stop): core[input] is
    [impulse, kappa, output]."""

    def __init__(self, times: np.ndarray, raw: list, stop_sample: int):
        self.times = times
        self.first, self.stop = core_window(model.WINDOW_START, stop_sample)
        self.core = [to_core(times, responses, self.first, self.stop) for responses in raw]
        self.lattice = model.Lattice(times, LATTICE_FIRST, self.stop)

    def subset(self, rows) -> "Responses":
        other = object.__new__(Responses)
        other.times = self.times[rows]
        other.first, other.stop = self.first, self.stop
        other.core = [core[rows] for core in self.core]
        other.lattice = model.Lattice(other.times, LATTICE_FIRST, self.stop)
        return other

    def window(self, stop_sample: int) -> "Responses":
        """The same responses cut to raw samples below `stop_sample`."""
        other = object.__new__(Responses)
        other.times = self.times
        other.first, other.stop = core_window(model.WINDOW_START, stop_sample)
        other.core = [core[:, :other.stop - other.first] for core in self.core]
        other.lattice = model.Lattice(other.times, LATTICE_FIRST, other.stop)
        return other

    def on_data(self, lattice_values: np.ndarray) -> np.ndarray:
        return lattice_values[:, self.first - LATTICE_FIRST:]


def load_working_data() -> dict:
    grid_raw = [datasets.grid_responses(channel, 2600) for channel in (0, 1)]
    grid = Responses(grid_raw[0][0], [raw for _, raw in grid_raw], model.WINDOW_STOP)
    clean = []
    for render in range(CLEAN_RENDERS):
        raw = [clean_responses(render, channel) for channel in (0, 1)]
        clean.append((raw[0][0], [responses for _, responses in raw]))

    def joined(renders, stop_sample):
        times = np.concatenate([clean[r][0] for r in renders])
        return Responses(times, [np.concatenate([clean[r][1][c] for r in renders]) for c in (0, 1)], stop_sample)

    fit_renders, validation_renders = range(CLEAN_RENDERS - 1), [CLEAN_RENDERS - 1]
    return {
        "grid_fit": grid.subset(np.arange(0, len(grid.times), 4)),
        "grid_validation": grid.subset(np.arange(2, len(grid.times), 4)),
        "clean_fit": joined(fit_renders, EXTENDED_STOP),
        "clean_validation": joined(validation_renders, model.WINDOW_STOP),
        "clean_validation_raw": (clean[CLEAN_RENDERS - 1][0], clean[CLEAN_RENDERS - 1][1]),
    }


def null_db(candidate: np.ndarray, reference: np.ndarray) -> float:
    return float(10.0 * np.log10(np.sum((candidate - reference) ** 2) / np.sum(reference ** 2)))


def low_pass_response(frequency: np.ndarray) -> np.ndarray:
    """Bilinear biquad low-pass (the usual audio-EQ form) at the core rate."""
    corner, q = LOW_PASS
    omega = 2.0 * np.pi * corner / model.CORE_RATE
    alpha = np.sin(omega) / (2.0 * q)
    b = np.array([(1 - np.cos(omega)) / 2, 1 - np.cos(omega), (1 - np.cos(omega)) / 2])
    a = np.array([1 + alpha, -2 * np.cos(omega), 1 - alpha])
    return freqz(b / a[0], a / a[0], worN=2.0 * np.pi * frequency / model.CORE_RATE)[1]


def split_pre_kernel(pre: np.ndarray, post: np.ndarray) -> np.ndarray:
    """Zero-phase pre-kernel: sqrt(|pre * post| / |low-pass|) above 4 kHz, flat below 2 kHz."""
    size = 8192
    frequency = np.fft.rfftfreq(size, 1.0 / model.CORE_RATE)
    magnitude = np.abs(np.fft.rfft(np.convolve(pre, post[:POST_FIRST + POST_FREE]), size))
    magnitude /= magnitude[(frequency > 3000) & (frequency < 4000)].mean()
    magnitude /= np.maximum(np.abs(low_pass_response(frequency)), 1e-9)
    blend = 0.5 - 0.5 * np.cos(np.pi * np.clip((frequency - 2000.0) / 2000.0, 0.0, 1.0))
    magnitude = np.where(frequency < 4000, 1.0 + (magnitude - 1.0) * blend, magnitude)
    # Above 21 kHz the composite is too small to divide: continue its slope.
    low, high = np.argmin(np.abs(frequency - 20900)), np.argmin(np.abs(frequency - 21100))
    slope = (np.log(magnitude[high]) - np.log(magnitude[low])) / (frequency[high] - frequency[low])
    magnitude = np.where(frequency > frequency[high], magnitude[high] * np.exp(slope * (frequency - frequency[high])), magnitude)
    full = np.fft.irfft(np.sqrt(magnitude), size)
    centre = model.PRE_CENTRE
    taper = np.ones(2 * centre + 1)
    ramp = 0.5 + 0.5 * np.cos(np.pi * np.arange(1, centre - PRE_FLAT + 1) / (centre - PRE_FLAT + 1))
    taper[centre + PRE_FLAT + 1:] = ramp
    taper[:centre - PRE_FLAT] = ramp[::-1]
    kernel = np.concatenate([full[-centre:], full[:centre + 1]]) * taper
    return kernel / kernel.sum()


class State:
    """Constants under estimation. Gains are held for the two same-side paths during the kernel fit."""

    def __init__(self, start_phase: np.ndarray):
        self.pre = np.zeros(2 * model.PRE_CENTRE + 1)
        self.pre[model.PRE_CENTRE] = 1.0
        self.post = None
        self.start_phase = start_phase.copy()
        self.first = np.ones((2, 8))             # [network, line], same-side path
        self.second = np.zeros(2)                # second pass 1 -> 1, same-side path
        self.ratio = np.zeros((2, 3))            # second pass / first-pass gain of its first line, other pairs
        self.loop = np.array([1.0])              # kernel between the two reads of a second pass

    def excitations(self, data: Responses, network: int, start_phase=None) -> tuple:
        phases = self.start_phase if start_phase is None else start_phase
        return model.line_excitations(data.lattice, network, model.pre_kernel_table(self.pre), phases,
                                      loop_kernel=self.loop)

    def mix(self, parts: tuple, first: np.ndarray, second: float, network: int) -> np.ndarray:
        first_pass, second_pass = parts
        total = sum(gain * part for gain, part in zip(first, first_pass)) + second * second_pass[0]
        for index, (earlier, _) in enumerate(model.SECOND_PASSES[1:]):
            total = total + self.ratio[network][index] * first[earlier] * second_pass[index + 1]
        return total

    def columns(self, parts: tuple, data: Responses, network: int) -> np.ndarray:
        """Design matrix of the nine free gains of one path (eight first passes and second pass 1 -> 1)."""
        first_pass, second_pass = parts
        shaped = [data.on_data(model.apply_post(part, self.post)).ravel() for part in first_pass + second_pass]
        columns = shaped[:9]
        for index, (earlier, _) in enumerate(model.SECOND_PASSES[1:]):
            columns[earlier] = columns[earlier] + self.ratio[network][index] * shaped[9 + index]
        return np.stack(columns, 1)

    def prediction(self, data: Responses, network: int, start_phase=None) -> np.ndarray:
        parts = self.excitations(data, network, start_phase)
        return data.on_data(model.apply_post(self.mix(parts, self.first[network], self.second[network], network), self.post))

    def null(self, data: Responses) -> float:
        difference = reference = 0.0
        for network in (0, 1):
            target = data.core[network][:, :, network]
            difference += np.sum((self.prediction(data, network) - target) ** 2)
            reference += np.sum(target ** 2)
        return float(10.0 * np.log10(difference / reference))


def tail_basis(length: int) -> np.ndarray:
    """Exponential tails that take over smoothly where the free taps end."""
    lag = np.arange(length)
    onset = 0.5 - 0.5 * np.cos(np.pi * np.clip((lag - (POST_FIRST + POST_FREE - 40)) / 40.0, 0.0, 1.0))
    return np.stack([np.exp(-(lag - (POST_FIRST + POST_FREE)) / time) * onset for time in TAIL_TIMES], 1)


def fit_post(state: State, data: Responses) -> np.ndarray:
    """Least-squares post-kernel given everything else, both same-side paths."""
    length = data.stop - LATTICE_FIRST
    count = data.stop - data.first
    offset = data.first - LATTICE_FIRST
    tails = tail_basis(length)
    unknowns = POST_FREE + len(TAIL_TIMES)
    normal = np.zeros((unknowns, unknowns))
    right = np.zeros(unknowns)
    for network in (0, 1):
        excitation = state.mix(state.excitations(data, network), state.first[network], state.second[network], network)
        target = data.core[network][:, :, network]
        for row in range(len(data.times)):
            design = np.empty((count, unknowns))
            for tap in range(POST_FREE):
                begin = offset - POST_FIRST - tap
                design[:, tap] = excitation[row, begin:begin + count]
            for index in range(len(TAIL_TIMES)):
                design[:, POST_FREE + index] = np.convolve(excitation[row], tails[:, index])[offset:offset + count]
            normal += design.T @ design
            right += design.T @ target[row]
    solution = np.linalg.solve(normal + 1e-10 * np.trace(normal) / unknowns * np.eye(unknowns), right)
    post = np.zeros(length)
    post[POST_FIRST:POST_FIRST + POST_FREE] = solution[:POST_FREE]
    return post + tails @ solution[POST_FREE:]


def fit_gains(state: State, data: Responses) -> None:
    for network in (0, 1):
        columns = state.columns(state.excitations(data, network), data, network)
        gains = np.linalg.lstsq(columns, data.core[network][:, :, network].ravel(), rcond=None)[0]
        state.first[network], state.second[network] = gains[:8], gains[8]


def coarse_start_phases(data: Responses) -> np.ndarray:
    """Start phase of every line by a scan: the phase whose trajectory collects the largest mean output."""
    candidates = np.arange(0.0, 2.0 * np.pi, 2.0 * np.pi / 720)
    result = np.zeros((2, 8))
    rows = np.arange(len(data.times))
    for network in (0, 1):
        target = data.core[network][:, :, network]
        for line, length in enumerate(model.LINE_LENGTHS[network]):
            scores = []
            for start in candidates:
                # the centre of the arrival is read near core index PRE_CENTRE + length after the impulse
                read_at = data.lattice.whole + model.PRE_CENTRE + length
                delay = length + model.MODULATION_DEPTH * np.sin(model.lfo_phase(start, read_at))
                arrival = np.rint(model.PRE_CENTRE + delay + POST_PEAK).astype(np.int64) - data.first
                scores.append(target[rows, np.clip(arrival, 0, target.shape[1] - 1)].mean())
            result[network, line] = candidates[int(np.argmax(scores))]
    return result


PHASE_STEP = 1e-4     # radians; the accumulator is single precision, its spacing near 2 pi is 5e-7


def fit_start_phases(state: State, data: Responses) -> None:
    for network in (0, 1):
        target = data.core[network][:, :, network]

        def residual(phases):
            trial = state.start_phase.copy()
            trial[network] = phases
            return (state.prediction(data, network, trial) - target).ravel()

        def jacobian(phases):
            base = residual(phases)
            return np.stack([(residual(phases + PHASE_STEP * np.eye(8)[line]) - base) / PHASE_STEP for line in range(8)], 1)

        state.start_phase[network] = least_squares(residual, state.start_phase[network], jac=jacobian,
                                                   x_scale=2e-3, max_nfev=8).x
    state.start_phase %= 2.0 * np.pi


def second_pass_ratios(state: State, data: Responses) -> dict:
    """Gains of every second pass that can reach the longer window, same-side paths; the pairs that reach
    the scored window are kept as ratios to the first-pass gain of their first line."""
    report = {}
    for network in (0, 1):
        lengths = model.LINE_LENGTHS[network]
        reach = data.stop + 45 + 2 * int(model.MODULATION_DEPTH)
        pairs = tuple((a, b) for a in range(8) for b in range(8) if lengths[a] + lengths[b] + FIXED_DELAY < reach)
        first_pass, second_pass = model.line_excitations(data.lattice, network, model.pre_kernel_table(state.pre),
                                                         state.start_phase, pairs, state.loop)
        columns = np.stack([data.on_data(model.apply_post(part, state.post)).ravel() for part in first_pass + second_pass], 1)
        gains = np.linalg.lstsq(columns, data.core[network][:, :, network].ravel(), rcond=None)[0]
        found = {pair: gains[8 + index] / gains[pair[0]] for index, pair in enumerate(pairs)}
        state.ratio[network] = [found[pair] for pair in model.SECOND_PASSES[1:]]
        report["LR"[network]] = {f"{a + 1}->{b + 1}": round(float(value), 5) for (a, b), value in found.items()
                                 if lengths[a] + lengths[b] + FIXED_DELAY + 2 * model.MODULATION_DEPTH + 60 < data.stop}
    return report


def fit_loop_kernel(state: State, data: Responses) -> None:
    """Kernel between the two reads of the second pass 1 -> 1: free taps at lags 0..LOOP_TAPS-1, both networks,
    normalised to its first tap (the gain stays in `second`)."""
    kernels = []
    for network in (0, 1):
        first_pass, second_pass = model.line_excitations(data.lattice, network, model.pre_kernel_table(state.pre),
                                                         state.start_phase, loop_kernel=state.loop)
        columns = [data.on_data(model.apply_post(part, state.post)).ravel() for part in first_pass]
        fixed = sum(state.ratio[network][index] * state.first[network][earlier]
                    * data.on_data(model.apply_post(second_pass[index + 1], state.post)).ravel()
                    for index, (earlier, _) in enumerate(model.SECOND_PASSES[1:]))
        for lag in range(LOOP_TAPS):
            delta = np.zeros(lag + 1)
            delta[lag] = 1.0
            lagged = model.line_excitations(data.lattice, network, model.pre_kernel_table(state.pre),
                                            state.start_phase, ((0, 0),), delta)[1][0]
            columns.append(data.on_data(model.apply_post(lagged, state.post)).ravel())
        gains = np.linalg.lstsq(np.stack(columns, 1), data.core[network][:, :, network].ravel() - fixed, rcond=None)[0]
        kernels.append(gains[8:] / gains[8])
    state.loop = np.mean(kernels, 0)


def path_gains(state: State, data: Responses) -> tuple:
    """Free gains of all four paths: first[input, network, line] and second[input, network, pair]."""
    first = np.zeros((2, 2, 8))
    second = np.zeros((2, 2, len(model.SECOND_PASSES)))
    for network in (0, 1):
        columns = state.columns(state.excitations(data, network), data, network)
        for input_channel in (0, 1):
            gains = np.linalg.lstsq(columns, data.core[input_channel][:, :, network].ravel(), rcond=None)[0]
            first[input_channel, network] = gains[:8]
            second[input_channel, network, 0] = gains[8]
            for index, (earlier, _) in enumerate(model.SECOND_PASSES[1:]):
                second[input_channel, network, index + 1] = state.ratio[network][index] * gains[earlier]
    return first, second


def constants_of(state: State, first: np.ndarray, second: np.ndarray) -> model.Constants:
    return model.Constants(state.pre, state.post, state.start_phase, first, second, state.loop)


def score_core(constants: model.Constants, data: Responses) -> dict:
    """Null of every path on the core lattice."""
    result = {}
    difference = reference = 0.0
    for input_channel in (0, 1):
        for network in (0, 1):
            predicted = data.on_data(model.core_response(data.lattice, input_channel, network, constants))
            target = data.core[input_channel][:, :, network]
            result[f"{'LR'[input_channel]}->{'LR'[network]}"] = round(null_db(predicted, target), 2)
            difference += np.sum((predicted - target) ** 2)
            reference += np.sum(target ** 2)
    result["overall"] = round(float(10.0 * np.log10(difference / reference)), 2)
    return result


def score_host(constants: model.Constants, times: np.ndarray, raw: list) -> dict:
    """The holdout's own measure (raw 48 kHz samples 1200..2500, no gain or delay fit) on working data."""
    lattice = model.model_lattice(times)
    window = slice(model.WINDOW_START, model.WINDOW_STOP)
    result = {}
    difference = reference = 0.0
    worst = -1000.0
    for input_channel in (0, 1):
        predicted = np.stack([model.to_host(lattice, model.core_response(lattice, input_channel, network, constants))
                              for network in (0, 1)], 2)
        target = raw[input_channel][:, window].astype(np.float64)
        for network in (0, 1):
            result[f"{'LR'[input_channel]}->{'LR'[network]}"] = round(null_db(predicted[:, :, network], target[:, :, network]), 2)
        each = 10.0 * np.log10(np.sum((predicted - target) ** 2, axis=(1, 2)) / np.sum(target ** 2, axis=(1, 2)))
        worst = max(worst, float(each.max()))
        difference += np.sum((predicted - target) ** 2)
        reference += np.sum(target ** 2)
    result["overall"] = round(float(10.0 * np.log10(difference / reference)), 2)
    result["worstResponse"] = round(worst, 2)
    return result


def fit(data: dict, log=print) -> tuple:
    grid, clean = data["grid_fit"], data["clean_fit"].window(model.WINDOW_STOP)
    state = State(coarse_start_phases(grid))
    log("coarse start phases (cycles):", np.round(state.start_phase / (2 * np.pi), 4).tolist())
    state.post = fit_post(state, clean)
    for _ in range(2):
        fit_gains(state, clean)
        state.post = fit_post(state, clean)
    fit_start_phases(state, grid)
    log(f"all of the fixed kernel behind the lines: clean {state.null(clean):.2f} dB, grid {state.null(grid):.2f} dB")
    ratios = {}
    for round_index in range(3):
        state.pre = split_pre_kernel(state.pre, state.post)
        for _ in range(2):
            state.post = fit_post(state, clean)
            fit_gains(state, clean)
        fit_start_phases(state, grid)
        extended = data["clean_fit"]
        long_state = State(state.start_phase)
        long_state.pre, long_state.first, long_state.second, long_state.loop = state.pre, state.first, state.second, state.loop
        long_state.post = np.concatenate([state.post, state.post[-1] * np.exp(-np.arange(1, extended.stop - clean.stop + 1) / TAIL_TIMES[-1])])
        ratios = second_pass_ratios(long_state, extended)
        state.ratio = long_state.ratio
        fit_loop_kernel(state, clean)
        state.post = fit_post(state, clean)
        fit_gains(state, clean)
        log(f"round {round_index}: clean {state.null(clean):.2f} dB, grid {state.null(grid):.2f} dB, "
            f"clean validation {state.null(data['clean_validation']):.2f} dB, grid validation {state.null(data['grid_validation']):.2f} dB")
    first, second = path_gains(state, clean)
    return constants_of(state, first, second), {"secondPassRatios": ratios, "ratioPairs": [list(p) for p in model.SECOND_PASSES[1:]],
                                                "ratioValues": state.ratio.tolist()}


def structure(constants: model.Constants) -> dict:
    """Relations between the fitted constants that the findings quote."""
    cycles = constants.start_phase / (2.0 * np.pi)
    interleaved = np.array([cycles[network][line] for line in range(8) for network in (0, 1)])   # L1, R1, L2, ...
    steps = np.diff(interleaved)
    steps -= np.round(steps)
    unwrapped = interleaved[0] + np.concatenate([[0.0], np.cumsum(steps)])
    slope, origin = np.polyfit(np.arange(16), unwrapped, 1)
    lengths = np.array(model.LINE_LENGTHS, dtype=float)
    decay = 10.0 ** (-3.0 * lengths / (model.CORE_RATE * 0.5))            # 60 dB in 0.5 s over the line length
    same = np.stack([constants.first_gain[0, 0], constants.first_gain[1, 1]])
    cross = np.stack([constants.first_gain[1, 0], constants.first_gain[0, 1]])
    return {
        "lineLengthsCoreSamples": [list(row) for row in model.LINE_LENGTHS],
        "startPhaseCycles": np.round(cycles, 6).tolist(),
        "startPhaseStepCyclesL1R1L2": round(float(slope), 7),
        "startPhaseOfL1Cycles": round(float(origin % 1.0), 7),
        "startPhaseProgressionResidualCycles": round(float(np.std(unwrapped - (origin + slope * np.arange(16)))), 8),
        "crossOverSameFirstPassGain": np.round(cross / same, 5).tolist(),
        "sameSideGainOverDecayGain": np.round(same / decay, 5).tolist(),
        "sameSideGainLOverR": np.round(same[0] / same[1], 5).tolist(),
        "decayGainLOverR": np.round(decay[0] / decay[1], 5).tolist(),
        "secondPass11OverFirstPass1": [round(float(constants.second_gain[i, n, 0] / constants.first_gain[i, n, 0]), 5)
                                       for i in (0, 1) for n in (0, 1)],
        "secondPass11OverFirstPass1AndDecayGain": [round(float(constants.second_gain[n, n, 0] / constants.first_gain[n, n, 0] / decay[n][0]), 5)
                                                   for n in (0, 1)],
        "loopKernelSum": round(float(constants.loop_kernel.sum()), 5),
        "fixedDelayCoreSamples": int(np.argmax(np.abs(np.convolve(constants.pre_kernel, constants.post_kernel)))),
    }


def report(constants: model.Constants, data: dict) -> dict:
    times, raw = data["clean_validation_raw"]
    return {
        "coreLattice": {name: score_core(constants, data[name].window(model.WINDOW_STOP) if name == "clean_fit" else data[name])
                        for name in ("clean_fit", "clean_validation", "grid_fit", "grid_validation")},
        "hostSamplesCleanValidation": score_host(constants, times, raw),
    }


def state_of(constants: model.Constants) -> State:
    state = State(constants.start_phase)
    state.pre, state.post, state.loop = constants.pre_kernel.copy(), constants.post_kernel.copy(), constants.loop_kernel
    state.first = np.stack([constants.first_gain[0, 0], constants.first_gain[1, 1]])
    state.second = np.array([constants.second_gain[0, 0, 0], constants.second_gain[1, 1, 0]])
    state.ratio = np.array([[constants.second_gain[n, n, index + 1] / constants.first_gain[n, n, earlier]
                             for index, (earlier, _) in enumerate(model.SECOND_PASSES[1:])] for n in (0, 1)])
    return state


def host_prediction(constants: model.Constants, times: np.ndarray, input_channel: int) -> np.ndarray:
    lattice = model.model_lattice(times)
    return np.stack([model.to_host(lattice, model.core_response(lattice, input_channel, network, constants))
                     for network in (0, 1)], 2)


def check_warmup(constants: model.Constants) -> dict:
    """Captures with other warm-up lengths against the model, the impulse time counted (a) from the first
    processed sample and (b) from the first stimulus sample."""
    times = np.array([300000 + index * 48037 + 11 for index in range(7)])
    stimulus = revocean.impulses(13.5, [(int(t), 0, datasets.IMPULSE_AMPLITUDE) for t in times])
    window = slice(model.WINDOW_START, model.WINDOW_STOP)
    result = {}
    for seconds in (5.0, 9.0, 10.0, 10.5, 20.0, 10.001, 10.0005):
        output = revocean.capture(stimulus, datasets.GRID_SETTINGS, warmup=seconds).output
        reference = np.stack([output[int(t):int(t) + model.WINDOW_STOP] for t in times])[:, window] / datasets.IMPULSE_AMPLITUDE
        shift = int(round(seconds * revocean.SAMPLE_RATE)) - int(revocean.WARMUP_SECONDS * revocean.SAMPLE_RATE)
        result[repr(seconds)] = {"fromFirstProcessedSample": round(null_db(host_prediction(constants, times + shift, 0), reference), 2),
                                 "fromFirstStimulusSample": round(null_db(host_prediction(constants, times, 0), reference), 2)}
    return result


def check_oscillator(constants: model.Constants, data: dict) -> dict:
    """Ideal sinusoids in place of the single-precision accumulator, start phases refitted; and other depths."""
    exact = model.lfo_phase
    grid, validation = data["grid_fit"], data["clean_validation"]
    result = {"singlePrecisionAccumulator0.6Hz": round(state_of(constants).null(validation), 2)}
    middle = np.array([int(np.median(grid.lattice.whole)) + 1500])
    for rate in (0.6, 0.5998781):
        def ideal(start, core_index, rate=rate):
            return start + 2.0 * np.pi * rate * (np.asarray(core_index, dtype=np.float64) + model.WARMUP_CORE_SAMPLES) / model.CORE_RATE
        state = state_of(constants)
        state.start_phase = np.array([[(exact(phase, middle)[0] - ideal(0.0, middle)[0]) % (2.0 * np.pi) for phase in row]
                                      for row in constants.start_phase])
        model.lfo_phase = ideal
        try:
            fit_start_phases(state, grid)
            result[f"idealSinusoid{rate}Hz"] = round(state.null(validation), 2)
        finally:
            model.lfo_phase = exact
    depth = model.MODULATION_DEPTH
    try:
        for trial in (38.800, 38.814):
            model.MODULATION_DEPTH = trial
            result[f"depth{trial:.3f}CoreSamples"] = round(state_of(constants).null(validation), 2)
    finally:
        model.MODULATION_DEPTH = depth
    return result


def check_placement(constants: model.Constants, data: dict) -> dict:
    """Other placements of the fixed kernel around the lines; post-kernel, gains and start phases refitted."""
    clean, grid, validation = data["clean_fit"].window(model.WINDOW_STOP), data["grid_fit"], data["clean_validation"]
    size = 8192
    frequency = np.fft.rfftfreq(size, 1.0 / model.CORE_RATE)
    magnitude = np.abs(np.fft.rfft(np.convolve(constants.pre_kernel, constants.post_kernel[:POST_FIRST + POST_FREE]), size))
    magnitude /= magnitude[(frequency > 3000) & (frequency < 4000)].mean()
    centre = model.PRE_CENTRE
    taper = np.ones(2 * centre + 1)
    ramp = 0.5 + 0.5 * np.cos(np.pi * np.arange(1, centre - PRE_FLAT + 1) / (centre - PRE_FLAT + 1))
    taper[centre + PRE_FLAT + 1:] = ramp
    taper[:centre - PRE_FLAT] = ramp[::-1]

    def zero_phase(power):
        full = np.fft.irfft(np.maximum(magnitude, 1e-6) ** power, size)
        kernel = np.concatenate([full[-centre:], full[:centre + 1]]) * taper
        return kernel / kernel.sum()

    result = {}
    for name, pre in (("allBehindTheLines", zero_phase(0.0)), ("halfOfWholeMagnitudeInFront", zero_phase(0.5)),
                      ("wholeMagnitudeInFront", zero_phase(1.0)), ("stored", constants.pre_kernel)):
        state = state_of(constants)
        state.pre = pre
        for stage in range(2):
            for _ in range(2):
                state.post = fit_post(state, clean)
                fit_gains(state, clean)
            if stage == 0:
                fit_start_phases(state, grid)
        result[name] = round(state.null(validation), 2)
    return result


def check_second_passes(constants: model.Constants, data: dict) -> dict:
    """Clean validation with parts of the second passes removed (nothing refitted)."""
    validation = data["clean_validation"]
    plain = np.array([1.0])

    def without(pairs, loop_kernel):
        second = constants.second_gain.copy()
        second[:, :, list(pairs)] = 0.0
        trial = model.Constants(constants.pre_kernel, constants.post_kernel, constants.start_phase,
                                constants.first_gain, second, loop_kernel)
        return score_core(trial, validation)

    return {"stored": score_core(constants, validation),
            "withoutLoopKernel": without((), plain),
            "withoutPairs12And21And22": without((1, 2, 3), constants.loop_kernel),
            "withoutSecondPasses": without((0, 1, 2, 3), constants.loop_kernel)}


def check_macro() -> dict:
    """Macro 50 % and 100 % (Mix 100 %): where the first-pass pattern of Macro 0 arrives and how it scales.
    Twenty impulses 1 s apart on the left input, three realisations each."""
    times = np.array([48000 + index * 48053 for index in range(20)])
    stimulus = revocean.impulses(21.5, [(int(t), 0, datasets.IMPULSE_AMPLITUDE) for t in times])

    def responses(macro, realisation):
        settings = dict(datasets.GRID_SETTINGS, macro=macro)
        output = revocean.capture(stimulus, settings, realisation=realisation).output
        return np.stack([output[int(t):int(t) + 6000] for t in times])[:, :, 0].astype(np.float64) / datasets.IMPULSE_AMPLITUDE

    def envelope(signal):
        return np.convolve(np.abs(signal), np.ones(5) / 5.0, mode="same")

    plain = responses(0.0, 0)
    pattern = slice(1250, 2330)                    # first passes of the left network at Macro 0
    result = {"impulseTimesSeconds": np.round(times / revocean.SAMPLE_RATE, 3).tolist()}
    for macro in (0.5, 1.0):
        runs = [responses(macro, realisation) for realisation in range(3)]
        entry = {"rmsBeforeFirstArrivalDb": round(revocean.decibels(revocean.rms(runs[0][:, :1100]) / revocean.rms(runs[0][:, 1200:])), 1),
                 "nullBetweenRealisations0And1PerImpulseDb": [round(null_db(runs[1][i, 1700:3100], runs[0][i, 1700:3100]), 1) for i in range(20)],
                 "realisations": []}
        for run in runs:
            delays, gains, undelayed = [], [], []
            for index in range(20):
                reference = envelope(plain[index, pattern])
                whole = envelope(run[index])
                scores = np.array([np.dot(whole[pattern.start + shift:pattern.stop + shift], reference) for shift in range(200, 2500)])
                best = int(np.argmax(scores))
                delays.append(best + 200)
                gains.append(round(float(scores[best] / np.dot(reference, reference)), 2))
                undelayed.append(round(float(np.dot(run[index, pattern], plain[index, pattern]) / np.dot(plain[index, pattern], plain[index, pattern])), 2))
            entry["realisations"].append({"patternDelayHostSamples": delays, "envelopeGainOfDelayedPattern": gains,
                                          "gainOfUndelayedMacro0FirstPass": undelayed})
        result[f"macro{int(macro * 100)}"] = entry
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--report", action="store_true", help="score the stored constants; do not fit")
    parser.add_argument("--quick", action="store_true", help="fit without the checks (warm-up, oscillator, placement, Macro)")
    arguments = parser.parse_args()
    revocean.identity()
    data = load_working_data()
    if arguments.report:
        print(json.dumps(report(model.load_constants(), data), indent=1))
        return
    constants, facts = fit(data)
    facts["nulls"] = report(constants, data)
    facts["structure"] = structure(constants)
    if not arguments.quick:
        facts["checks"] = {"warmup": check_warmup(constants), "oscillator": check_oscillator(constants, data),
                           "placement": check_placement(constants, data),
                           "secondPasses": check_second_passes(constants, data), "macro": check_macro()}
    model.DATA_FILE.parent.mkdir(exist_ok=True)
    np.savez(model.DATA_FILE, pre_kernel=constants.pre_kernel, post_kernel=constants.post_kernel,
             start_phase=constants.start_phase, first_gain=constants.first_gain, second_gain=constants.second_gain,
             loop_kernel=constants.loop_kernel,
             line_lengths=np.array(model.LINE_LENGTHS), second_passes=np.array(model.SECOND_PASSES),
             facts=json.dumps(facts))
    print(json.dumps(facts, indent=1))


if __name__ == "__main__":
    main()
