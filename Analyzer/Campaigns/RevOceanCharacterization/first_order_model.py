#!/usr/bin/env python3
"""The campaign's first-pass model of Rev OCEAN (Tide mode, Macro 0, Mix 100 %, Size 100 %, Decay 0.5 s).

    predict(time, input_channel) -> array (2500, 2)

is the raw unit-impulse response at a 48 kHz host (index = samples after the
impulse, the 48 samples of reported latency included; column = output channel)
for an impulse on `input_channel` at sample `time` after the 10 s warm-up.
Only [1200, 2500) is filled: nothing arrives earlier.

Signal path (findings/first_order.md has the evidence for every element):

    48 kHz in -> converter -> [ 44 samples -> 16 + 16 modulated lines -> fixed filter ] -> converter -> 48 kHz out
                                44.1 kHz network, one group of 16 lines per output

* The network runs at 44.1 kHz at every host rate. Both converters use one
  kernel: a Kaiser-windowed sinc (cut-off 0.9 of the internal Nyquist
  frequency, 17 internal samples each side, beta 6) stored as a table of 4096
  single-precision entries per internal sample and read without
  interpolation, at the entry at or below the wanted distance. Host and
  internal samples meet on a lattice of 1/160 internal sample, so every
  distance is a whole number of lattice steps and nothing is fitted here.
  A distance that falls exactly on an entry takes the entry below it on one
  wing: the earlier inputs in the input converter, the later ones in the
  output converter.
* A host impulse at sample n is centred at internal time 147 n / 160 + 26.5625
  and written into the lines 44 internal samples later; host output sample N
  reads the network at internal time 147 (N - 126 - 70/147) / 160. Internal
  time counts from the first processed sample; it is also the step count of
  the oscillators at a read.
* Line n of group g is `prime + 0.88 ms * sin(theta)` internal samples long;
  the length is a single-precision number and the line is read with linear
  interpolation at the length of the sample being read out.
* theta is a single-precision phase accumulator, one per line. Accumulator k
  (k = 0 for L1, 1 for R1, 2 for L2, ...) starts at 11.25 k rad when
  processing starts and runs
      if theta >= 2 pi: theta -= 2 pi;   theta += fl32(2 pi 0.6 / 44100)
  once per internal sample, the warm-up included.
* The first-pass gain of a line is its coefficient times the Decay attenuation
  10^(-3 prime / (44100 T)). The other input reaches a line with (1 - w)/(1 + w)
  of that, w being the line's width weight.
* The scored window also holds the earliest second passes (line i read again
  by line j of the same group). Their gains follow from one feedback
  magnitude and the output weights of lines 1 to 3; the fed-back signal
  passes a short loop kernel.
* One fixed filter follows the lines, outside the loop.

Constants: tide_structural_data/first_order.json, written by fit_first_order.py.
The module renders nothing. Score it with `python score_first_order.py first_order_model.py`.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "first_order.json"
WINDOW_START = 1200
WINDOW_STOP = 2500
INTERNAL_RATE = 44100
HOST_RATE = 48000
HOST_PER_INTERNAL = HOST_RATE / INTERNAL_RATE       # 160 / 147
LATTICE = 160                                       # host samples and internal samples meet on 1/160 internal sample
HOST_STEP = 147                                     # one host sample on that lattice
TWO_PI32 = np.float32(2.0 * np.pi)
MODULATION_REACH = 41                               # samples around a line's centre that its read can touch
WINDOW_FRAMES = 2176                                # internal samples after the tank write that can reach the window


def oscillator_start(index: int, phase_step: float) -> float:
    """Phase of accumulator `index` at its first sample: index * phase_step after the one wrap test."""
    start = np.float32(phase_step) * np.float32(index)
    return float(np.float32(start - TWO_PI32)) if start >= TWO_PI32 else float(start)


def accumulator_segments(start: float, increment: np.float32, count: int) -> tuple:
    """Piecewise-linear record of theta <- fl32(theta + increment), minus 2 pi when the sum reaches 2 pi.

    Inside one binade every sum is rounded to the same grid, so the phase moves
    by a constant multiple of the grid step. A segment is (first step index,
    phase there, step); steps that change binade or wrap are segments of one.
    A start above 2 pi comes down by 2 pi per step, as the accumulator does.
    """
    theta = np.float32(start)
    starts, phases, steps = [], [], []
    index = 0
    while index < count:
        following = np.float32(np.float64(theta) + np.float64(increment))
        step = float(following) - float(theta)
        starts.append(index)
        phases.append(float(theta))
        steps.append(step)
        if following >= TWO_PI32:
            theta = np.float32(np.float64(following) - np.float64(TWO_PI32))
            index += 1
            continue
        top = min(2.0 ** np.frexp(float(following))[1], float(TWO_PI32))
        same_binade = theta > 0 and np.frexp(float(theta))[1] == np.frexp(float(following))[1]
        run = max(int(np.ceil((top - float(theta)) / step)) - 1, 1) if same_binade else 1
        theta = np.float32(float(theta) + run * step)
        index += run
    return np.array(starts), np.array(phases), np.array(steps)


def accumulator_phase(segments: tuple, index) -> np.ndarray:
    starts, phases, steps = segments
    position = np.searchsorted(starts, index, side="right") - 1
    return phases[position] + (index - starts[position]) * steps[position]


def converter_table(cutoff: float, zero_crossings: int, beta: float, entries: int) -> np.ndarray:
    """One wing of the converter kernel: cutoff * sinc(cutoff x) under a Kaiser window, `entries` values per
    internal sample, x = index / entries, as single-precision numbers."""
    index = np.arange(zero_crossings * entries)
    window = np.i0(beta * np.sqrt(1.0 - (index / (len(index) - 1)) ** 2)) / np.i0(beta)
    return (cutoff * np.sinc(cutoff * index / entries) * window).astype(np.float32).astype(np.float64)


def table_lookup(table: np.ndarray, numerator: np.ndarray, entries: int, low_wing: int) -> np.ndarray:
    """Kernel at a distance of numerator / 160 internal samples (output time minus input time).

    The table is not interpolated: the entry at or below the distance is used.
    A distance that falls exactly on an entry takes the entry below it on the
    wing `low_wing` (+1: inputs before the output sample, -1: inputs after it).
    """
    scaled = np.abs(numerator) * entries
    index = scaled // LATTICE - ((scaled % LATTICE == 0) & (np.sign(numerator) == low_wing))
    return np.where(index < len(table), table[np.minimum(index, len(table) - 1)], 0.0)


def triangle(distance) -> np.ndarray:
    """Weight of linear interpolation at `distance` samples from the read position."""
    return np.clip(1.0 - np.abs(distance), 0.0, None)


class Network:
    """The 44.1 kHz network up to the fixed filter: lines, oscillators, gains, second passes."""

    def __init__(self, constants: dict, frames: int):
        lines, oscillator, second = constants["lines"], constants["oscillator"], constants["second_pass"]
        self.prime = np.array(lines["prime"])                                   # [group][line]
        self.depth = oscillator["depth_ms"] * 1e-3 * INTERNAL_RATE
        increment = np.float32(2.0 * np.pi * oscillator["rate_hz"] / INTERNAL_RATE)
        self.segments = [[accumulator_segments(oscillator_start(2 * line + group, oscillator["phase_step_rad"]),
                                               increment, frames)
                          for line in range(self.prime.shape[1])] for group in (0, 1)]
        decay = 10.0 ** (-3.0 * self.prime / (INTERNAL_RATE * constants["decay_seconds"]))
        self.own_gain = np.array(lines["coefficient"])[None, :] * decay         # [group][line], own input
        width = np.array(lines["width"])
        self.cross_ratio = (1.0 - width) / (1.0 + width)
        self.pairs = [(first - 1, later - 1) for first, later in second["pairs"]]
        weight = np.array(second["output_weight"])
        self.pair_gain = np.array([[sign * second["feedback_gain"] * self.own_gain[group][first] * decay[group][later]
                                    * weight[later] / weight[first]
                                    for (first, later), sign in zip(self.pairs, second["sign"])] for group in (0, 1)])
        self.loop_kernel = np.array(second["loop_kernel"])
        self.fixed_filter = np.array(constants["fixed_filter"])

    def length(self, group: int, line: int, step) -> np.ndarray:
        """Length of a line, in internal samples, when its accumulator has made `step` steps."""
        phase = accumulator_phase(self.segments[group][line], step)
        return (self.prime[group][line] + self.depth * np.sin(phase)).astype(np.float32).astype(np.float64)

    def read(self, group: int, line: int, written: np.ndarray, stream: np.ndarray, stop: int | None = None) -> tuple:
        """(read indices, samples) of line `line` for a `stream` written at the internal indices `written`."""
        prime = int(self.prime[group][line])
        last = written[-1] + prime + MODULATION_REACH + 1
        read = np.arange(written[0] + prime - MODULATION_REACH, last if stop is None else min(last, stop))
        length = self.length(group, line, read)
        return read, triangle(read[:, None] - length[:, None] - written[None, :]) @ stream

    def first_gain(self, input_channel: int, group: int) -> np.ndarray:
        return self.own_gain[group] if input_channel == group else self.own_gain[group] * self.cross_ratio

    def second_gain(self, input_channel: int, group: int) -> np.ndarray:
        if input_channel == group:
            return self.pair_gain[group]
        return self.pair_gain[group] * np.array([self.cross_ratio[first] for first, _ in self.pairs])

    def excitation(self, written: np.ndarray, stream: np.ndarray, input_channel: int, group: int, frames: int) -> np.ndarray:
        """Sum of the line reads before the fixed filter, on internal indices written[0] .. written[0] + frames."""
        base = written[0]
        stop = base + frames
        direct = np.zeros(frames)
        fed_back = np.zeros(frames)
        passes = {}
        for line, gain in enumerate(self.first_gain(input_channel, group)):
            if written[0] + self.prime[group][line] - MODULATION_REACH >= stop:
                continue
            read, samples = self.read(group, line, written, stream, stop)
            passes[line] = (read, samples)
            direct[read - base] += gain * samples
        for (first, later), gain in zip(self.pairs, self.second_gain(input_channel, group)):
            fed, signal = passes[first]
            if fed[0] + self.prime[group][later] - MODULATION_REACH >= stop:
                continue
            read, samples = self.read(group, later, fed, signal, stop)
            fed_back[read - base] += gain * samples
        return direct + np.convolve(fed_back, self.loop_kernel)[:frames]

    def output(self, written: np.ndarray, stream: np.ndarray, input_channel: int, group: int, frames: int) -> np.ndarray:
        return np.convolve(self.excitation(written, stream, input_channel, group, frames), self.fixed_filter)[:frames]


class Model:
    """The network between its two rate converters at a 48 kHz host.

    Times are kept as whole numbers of 1/160 internal sample: host sample n sits
    at 147 n, internal sample m at 160 m. A host impulse at n enters the internal
    stream centred at 147 n + input_offset; host output sample N reads the
    internal stream at 147 N - output_offset.
    """

    def __init__(self, constants: dict, warmup_frames: int = 10 * HOST_RATE, seconds: float = 112.0):
        converter = constants["converter"]
        self.warmup_frames = warmup_frames
        self.network = Network(constants, int((warmup_frames + seconds * HOST_RATE) / HOST_PER_INTERNAL) + 8192)
        self.entries = converter["table_entries_per_sample"]
        self.table = converter_table(converter["cutoff"], converter["zero_crossings"], converter["kaiser_beta"], self.entries)
        self.reach = converter["zero_crossings"] + 1
        self.low_wing = converter["exact_entry_low_wing"]                       # {"input": +1, "output": -1}
        self.input_offset = int(round(converter["input_delay_internal_samples"] * LATTICE))
        self.output_offset = int(round(converter["output_delay_host_samples"] * HOST_STEP))
        self.tank_predelay = constants["tank_predelay_internal_samples"]

    def written_stream(self, time: int) -> tuple:
        """(internal indices, samples) of a host unit impulse at `time` when it is written into the lines."""
        centre = HOST_STEP * (self.warmup_frames + time) + self.input_offset
        source = centre // LATTICE + np.arange(-self.reach, self.reach + 1)
        stream = table_lookup(self.table, LATTICE * source - centre, self.entries, self.low_wing["input"]) / HOST_PER_INTERNAL
        return source + self.tank_predelay, stream

    def predict(self, time: int, input_channel: int) -> np.ndarray:
        response = np.zeros((WINDOW_STOP, 2))
        sample = np.arange(WINDOW_START, WINDOW_STOP)
        position = HOST_STEP * (sample + self.warmup_frames + time) - self.output_offset
        written, stream = self.written_stream(time)
        index = (position // LATTICE)[:, None] + np.arange(-self.reach, self.reach + 1)[None, :]
        weights = table_lookup(self.table, position[:, None] - LATTICE * index, self.entries, self.low_wing["output"])
        index -= written[0]
        valid = (index >= 0) & (index < WINDOW_FRAMES)
        for group in (0, 1):
            internal = self.network.output(written, stream, input_channel, group, WINDOW_FRAMES)
            response[WINDOW_START:, group] = np.sum(weights * np.where(valid, internal[np.clip(index, 0, WINDOW_FRAMES - 1)], 0.0), axis=1)
        return response


def load_constants(path: Path = DATA) -> dict:
    return json.loads(path.read_text())


_model: Model | None = None


def predict(time: int, input_channel: int) -> np.ndarray:
    global _model
    if _model is None:
        _model = Model(load_constants())
    return _model.predict(int(time), int(input_channel))
