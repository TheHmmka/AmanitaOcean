#!/usr/bin/env python3
"""First pass of Rev OCEAN (Tide, Macro 0, Mix 100 %, Size 100 %, Decay 0.5 s, 48 kHz host): structural model.

Packet first_order_b. Everything here was measured through `revocean.capture`; the constants come from
`fit_first_order_b.py` and live in `tide_structural_data/first_order_b.npz`. See `findings/first_order_b.md`.

The reference runs its reverb core at 44.1 kHz whatever the host rate. Signal path of the model:

    host impulse at sample n (48 kHz)
      -> conversion to the 44.1 kHz core: a zero-phase pre-kernel sampled at the core instants
         k * 160/147 - n (host samples). The core lattice has a point on the first processed sample; the
         10 s warm-up is a whole number of lattice periods, so it also has one on the first stimulus sample
      -> two independent networks of eight delay lines, one network per output channel; line lengths are
         prime numbers of core samples. A line is read with linear interpolation at
         length + 0.88 ms * sin(phase); every line owns a single-precision phase accumulator
         (phase += 2 pi 0.6 / 44100 per core sample, minus 2 pi when it reaches 2 pi) that runs from the
         first processed sample
      -> output of a network = sum of its line reads, one gain per line and input channel. Inside the
         scored window the earliest second passes also arrive (line 1 or 2 read, fed back through a short
         loop kernel and read again by line 1 or 2)
      -> post-kernel at the core rate: everything fixed behind the lines (a 20 kHz, Q 1 low-pass, the
         low-frequency filters, the converter back to the host rate, 141 core samples of delay), fitted
         as one kernel
      -> band-limited interpolation to 48 kHz.

Where the 187 core samples of fixed delay sit (46 before the lines here, 141 behind) is a convention: it
only moves the start phases by a common amount.

`predict(time, input_channel)` returns the raw unit-impulse response, shape (2500, 2). Samples before 1200
stay zero: nothing but the tail of earlier sound is there. It renders no captures.
"""
from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import numpy as np
from scipy.signal import fftconvolve
from scipy.special import i0

CORE_RATE = 44100
CORE_STEP, HOST_STEP = 147, 160            # core index = host sample (48 kHz) * 147 / 160
WARMUP_CORE_SAMPLES = 10 * CORE_RATE       # the campaign's warm-up; the accumulators start with it
LFO_INCREMENT = np.float32(2.0 * np.pi * 0.6 / CORE_RATE)
LFO_WRAP = np.float32(2.0 * np.pi)
MODULATION_DEPTH = 0.88e-3 * CORE_RATE     # 38.808 core samples
LINE_LENGTHS = ((1031, 1097, 1187, 1289, 1423, 1583, 1783, 2027),
                (1039, 1109, 1193, 1301, 1429, 1597, 1787, 2039))
SECOND_PASSES = ((0, 0), (0, 1), (1, 0), (1, 1))   # (first line, second line) pairs that reach the window
PRE_CENTRE = 46                            # core samples between the impulse and the centre of the pre-kernel
WINDOW_START, WINDOW_STOP = 1200, 2500     # raw host samples that the model fills
INTERPOLATOR_HALF_WIDTH = 64               # core samples
DATA_FILE = Path(__file__).resolve().parent / "tide_structural_data" / "first_order_b.npz"


def windowed_sinc_table(half_width: int, beta: float, steps_per_sample: int) -> np.ndarray:
    """Kaiser-windowed sinc on [-half_width, half_width], `steps_per_sample` entries per unit."""
    position = np.arange(-half_width * steps_per_sample, half_width * steps_per_sample + 1) / steps_per_sample
    window = i0(beta * np.sqrt(np.clip(1.0 - (position / half_width) ** 2, 0.0, None))) / i0(beta)
    return np.sinc(position) * window


# Band-limited interpolation from the core lattice to continuous time, tabulated every 1/160 core sample.
INTERPOLATOR = windowed_sinc_table(INTERPOLATOR_HALF_WIDTH, 12.0, HOST_STEP)


def table_lookup(table: np.ndarray, index: np.ndarray) -> np.ndarray:
    """table[index + centre], zero outside the table."""
    half = (len(table) - 1) // 2
    inside = (index >= -half) & (index <= half)
    return np.where(inside, table[np.clip(index + half, 0, 2 * half)], 0.0)


def accumulator_segments(start_phase: float, count: int) -> tuple:
    """Exact single-precision phase accumulator, as runs of constant step.

    Returns (first index, first phase, step) per run. Within one binade an addition of the increment moves
    the phase by a fixed multiple of the binade's spacing, so only binade crossings and wraps are stepped.
    """
    phase = np.float32(start_phase)
    index = 0
    first, value, step = [], [], []
    while index < count:
        following = np.float32(phase + LFO_INCREMENT)
        wrapped = following >= LFO_WRAP
        if wrapped:
            following = np.float32(following - LFO_WRAP)
        run = 1
        increment = float(following) - float(phase)
        if not wrapped and phase >= 1.0 / 512 and np.frexp(phase)[1] == np.frexp(following)[1]:
            limit = min(float(np.ldexp(1.0, int(np.frexp(phase)[1]))), float(LFO_WRAP))
            run = max(1, int((limit - float(phase)) / increment))
            while float(phase) + run * increment >= limit:
                run -= 1
            run = max(run, 1)
            following = np.float32(float(phase) + run * increment)
        first.append(index)
        value.append(float(phase))
        step.append(increment)
        phase = following
        index += run
    return np.array(first), np.array(value), np.array(step)


@lru_cache(maxsize=64)
def _segments(start_phase: float) -> tuple:
    return accumulator_segments(start_phase, WARMUP_CORE_SAMPLES + 112 * CORE_RATE)


def lfo_phase(start_phase: float, core_index: np.ndarray) -> np.ndarray:
    """Accumulator phase at absolute core indices (0 = first stimulus sample) for a line that started at
    `start_phase` when processing began, one warm-up earlier."""
    first, value, step = _segments(float(np.float32(start_phase)))
    sample = np.asarray(core_index, dtype=np.int64) + WARMUP_CORE_SAMPLES
    run = np.searchsorted(first, sample, side="right") - 1
    return value[run] + (sample - first[run]) * step[run]


@dataclass(frozen=True)
class Constants:
    pre_kernel: np.ndarray      # zero-phase pre-kernel on the core lattice, centre at (len - 1) / 2
    post_kernel: np.ndarray     # causal core-rate kernel after the lines
    start_phase: np.ndarray     # [network, line] accumulator phase when processing starts, radians
    first_gain: np.ndarray      # [input, network, line]
    second_gain: np.ndarray     # [input, network, pair of SECOND_PASSES]
    loop_kernel: np.ndarray     # causal core-rate kernel between the two reads of a second pass, tap 0 = 1

    @property
    def pre_table(self) -> np.ndarray:
        return pre_kernel_table(self.pre_kernel)


def load_constants(path: Path = DATA_FILE) -> Constants:
    with np.load(path) as data:
        return Constants(data["pre_kernel"], data["post_kernel"], data["start_phase"],
                         data["first_gain"], data["second_gain"], data["loop_kernel"])


@lru_cache(maxsize=8)
def _pre_table(key: bytes) -> np.ndarray:
    kernel = np.frombuffer(key, dtype=np.float64)
    centre = (len(kernel) - 1) // 2
    half = centre + INTERPOLATOR_HALF_WIDTH
    position = np.arange(-half * HOST_STEP, half * HOST_STEP + 1)
    table = np.zeros(len(position))
    for tap, weight in enumerate(kernel):
        table += weight * table_lookup(INTERPOLATOR, position - HOST_STEP * (tap - centre))
    return table


def pre_kernel_table(kernel: np.ndarray) -> np.ndarray:
    """The pre-kernel as a continuous function, tabulated every 1/160 core sample around its centre."""
    return _pre_table(np.ascontiguousarray(kernel, dtype=np.float64).tobytes())


class Lattice:
    """Core lattice shared by a set of impulses: index kappa counts core samples from floor(n 147/160)."""

    def __init__(self, times, first: int, stop: int):
        self.times = np.atleast_1d(np.asarray(times, dtype=np.int64))
        self.whole = (self.times * CORE_STEP) // HOST_STEP      # floor of the impulse position in core samples
        self.part = (self.times * CORE_STEP) % HOST_STEP        # its fraction, in 1/160 core samples
        self.first, self.stop = first, stop

    def place(self, values: np.ndarray, first: int) -> np.ndarray:
        """values[:, j] sits at kappa = first + j; returns the same on [self.first, self.stop)."""
        out = np.zeros((values.shape[0], self.stop - self.first))
        low, high = max(first, self.first), min(first + values.shape[1], self.stop)
        if high > low:
            out[:, low - self.first:high - self.first] = values[:, low - first:high - first]
        return out


def read_line(lattice: Lattice, source: np.ndarray, source_first: int, length: int, start_phase: float,
              first: int, stop: int) -> np.ndarray:
    """Linear-interpolated read of `source` (kappa = source_first + j) at delay length + depth sin(phase),
    for kappa in [first, stop). The delay is the one of the sample being read out."""
    kappa = np.arange(first, stop)
    delay = length + MODULATION_DEPTH * np.sin(lfo_phase(start_phase, lattice.whole[:, None] + kappa[None, :]))
    whole = np.floor(delay).astype(np.int64)
    fraction = delay - whole
    position = kappa[None, :] - whole - source_first
    row = np.arange(source.shape[0])[:, None]
    count = source.shape[1]

    def tap(at):
        return np.where((at >= 0) & (at < count), source[row, np.clip(at, 0, count - 1)], 0.0)

    return (1.0 - fraction) * tap(position) + fraction * tap(position - 1)


def line_excitations(lattice: Lattice, network: int, pre_table: np.ndarray, start_phase: np.ndarray,
                     pairs: tuple = SECOND_PASSES, loop_kernel: np.ndarray | None = None) -> tuple:
    """Unit-gain excitations before the post-kernel: eight first passes and one second pass per pair
    (first line, second line), each an array [impulse, kappa] on the lattice. `loop_kernel` filters what
    is fed back between the two reads (causal, core rate); None is a plain wire."""
    reach = ((len(pre_table) - 1) // 2) // HOST_STEP
    span = np.arange(PRE_CENTRE - reach, PRE_CENTRE + reach + 1)
    arrived = table_lookup(pre_table, HOST_STEP * (span[None, :] - PRE_CENTRE) - lattice.part[:, None])
    swing = int(np.ceil(MODULATION_DEPTH)) + 2
    lengths = LINE_LENGTHS[network]
    reads, firsts = [], []
    for line in range(8):
        first = span[0] + lengths[line] - swing
        stop = span[-1] + lengths[line] + swing + 1
        reads.append(read_line(lattice, arrived, span[0], lengths[line], start_phase[network][line], first, stop))
        firsts.append(first)
    first_pass = [lattice.place(read, first) for read, first in zip(reads, firsts)]
    second_pass = []
    for earlier, later in pairs:
        fed_back = reads[earlier] if loop_kernel is None else fftconvolve(reads[earlier], loop_kernel[None, :], axes=1)
        first = firsts[earlier] + lengths[later] - swing
        stop = firsts[earlier] + fed_back.shape[1] + lengths[later] + swing
        again = read_line(lattice, fed_back, firsts[earlier], lengths[later], start_phase[network][later], first, stop)
        second_pass.append(lattice.place(again, first))
    return first_pass, second_pass


def apply_post(excitation: np.ndarray, post_kernel: np.ndarray) -> np.ndarray:
    """Causal core-rate filtering along the lattice; nothing precedes the lattice."""
    return fftconvolve(excitation, post_kernel[None, :], axes=1)[:, :excitation.shape[1]]


def core_response(lattice: Lattice, input_channel: int, network: int, constants: Constants) -> np.ndarray:
    """Output of one network on the core lattice, [impulse, kappa]."""
    first_pass, second_pass = line_excitations(lattice, network, constants.pre_table, constants.start_phase,
                                               loop_kernel=constants.loop_kernel)
    excitation = sum(gain * part for gain, part in zip(constants.first_gain[input_channel][network], first_pass))
    excitation = excitation + sum(gain * part for gain, part in
                                  zip(constants.second_gain[input_channel][network], second_pass))
    return apply_post(excitation, constants.post_kernel)


def model_lattice(times, start: int = WINDOW_START, stop: int = WINDOW_STOP) -> Lattice:
    """Lattice wide enough to interpolate raw samples [start, stop) and to hold every excitation."""
    earliest = PRE_CENTRE + min(min(LINE_LENGTHS)) - 256
    latest = (stop * CORE_STEP) // HOST_STEP + INTERPOLATOR_HALF_WIDTH + 2
    return Lattice(times, earliest, latest)


def to_host(lattice: Lattice, core: np.ndarray, start: int = WINDOW_START, stop: int = WINDOW_STOP) -> np.ndarray:
    """Band-limited interpolation of [impulse, kappa] to raw host samples [start, stop)."""
    sample = np.arange(start, stop)
    kappa = np.arange(lattice.first, lattice.stop)
    out = np.empty((core.shape[0], stop - start))
    for row in range(core.shape[0]):
        weights = table_lookup(INTERPOLATOR, CORE_STEP * sample[:, None] + lattice.part[row] - HOST_STEP * kappa[None, :])
        out[row] = weights @ core[row]
    return out


_constants: Constants | None = None


def predict(time: int, input_channel: int) -> np.ndarray:
    """Raw unit-impulse response, shape (2500, 2), for an impulse on `input_channel` at sample `time`
    counted from the first sample after the 10 s warm-up."""
    global _constants
    if _constants is None:
        _constants = load_constants()
    lattice = model_lattice(int(time))
    response = np.zeros((WINDOW_STOP, 2))
    for network in (0, 1):
        response[WINDOW_START:, network] = to_host(lattice, core_response(lattice, input_channel, network, _constants))[0]
    return response
