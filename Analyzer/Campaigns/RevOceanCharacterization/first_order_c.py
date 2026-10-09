#!/usr/bin/env python3
"""Structural model of the first 52 ms of Rev OCEAN (Tide, Macro 0, Size 100 %, Decay 0.5 s) at 48 kHz.

    predict(time, input_channel) -> array (2500, 2)

gives the raw unit-impulse response (index = samples after the impulse, the 48
samples of reported latency included) for an impulse on `input_channel` at
sample `time` after the 10 s warm-up. Only [1200, 2500) is filled.

The model is the signal path measured in findings/first_order_c.md:

    48 kHz input -> rate converter -> 44.1 kHz core -> rate converter -> 48 kHz output

* Both converters are the same Kaiser-windowed sinc (cut-off 0.9 of the
  internal Nyquist frequency, 17 internal samples each side, beta 6).
* The core holds two independent groups of eight delay lines, one per output.
  A line is `prime + 38.808 sin(theta)` internal samples long and is read with
  linear interpolation; theta is a float32 phase accumulator that advances by
  fl32(2 pi 0.6 / 44100) per internal sample from the start of processing
  (the warm-up included) and wraps at 2 pi.
* The scored window also holds the earliest second passes (line i into line j
  of the same group); six pairs per group reach it. A second pass carries the
  short kernel of the feedback path.
* A fixed filter follows the lines.

Constants come from tide_structural_data/first_order_c.npz, written by
fit_first_order_c.py. Nothing here renders a capture.

Run `python score_first_order.py first_order_c.py` to score it.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "first_order_c.npz"
WINDOW_START = 1200
WINDOW_STOP = 2500
INTERNAL_RATE = 44100
HOST_RATE = 48000
HOST_PER_INTERNAL = HOST_RATE / INTERNAL_RATE       # 160 / 147
TWO_PI32 = np.float32(2.0 * np.pi)
MODULATION_REACH = 41                               # samples around a line's centre that its read can touch
INTERNAL_FRAMES = 2420                              # internal samples simulated after the tank write


def accumulator_segments(start: float, increment: np.float32, count: int) -> tuple:
    """Piecewise-linear record of the float32 accumulator theta <- fl32(theta + increment), wrapped at 2 pi.

    Inside one binade every sum is rounded to the same grid, so the phase moves
    by a constant multiple of the grid step. A segment is (first step index,
    phase there, step); steps that change binade or wrap are segments of one.
    """
    theta = np.float32(start)
    step_of = lambda value: np.float32(np.float64(value) + np.float64(increment))
    starts, phases, steps = [], [], []
    index = 0
    while index < count:
        following = step_of(theta)
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
        run = int(np.ceil((top - float(theta)) / step)) - 1 if same_binade else 1
        run = max(run, 1)
        theta = np.float32(float(theta) + run * step)
        index += run
    return np.array(starts), np.array(phases), np.array(steps)


def accumulator_phase(segments: tuple, index) -> np.ndarray:
    starts, phases, steps = segments
    position = np.searchsorted(starts, index, side="right") - 1
    return phases[position] + (index - starts[position]) * steps[position]


def kaiser_sinc(offset, cutoff: float, half_length: float, beta: float) -> np.ndarray:
    """cutoff * sinc(cutoff * offset) under a Kaiser window; offset in internal samples."""
    offset = np.asarray(offset, dtype=np.float64)
    result = np.zeros_like(offset)
    inside = np.abs(offset) < half_length
    window = np.i0(beta * np.sqrt(1.0 - (offset[inside] / half_length) ** 2)) / np.i0(beta)
    result[inside] = cutoff * np.sinc(cutoff * offset[inside]) * window
    return result


def triangle(distance) -> np.ndarray:
    """Weight of linear interpolation at `distance` samples from the read position."""
    return np.clip(1.0 - np.abs(distance), 0.0, None)


class Model:
    """The measured signal path for one set of constants (see fit_first_order_c.py for their origin)."""

    def __init__(self, constants: dict, warmup_frames: int = 10 * HOST_RATE, seconds: float = 112.0):
        self.c = constants
        self.warmup_frames = warmup_frames
        self.increment = np.float32(2.0 * np.pi * float(constants["lfo_hz"]) / INTERNAL_RATE)
        internal_frames = int((warmup_frames + seconds * HOST_RATE) / HOST_PER_INTERNAL) + 8192
        # accumulator k belongs to line k // 2 of output k % 2 (order L1, R1, L2, R2, ...)
        self.segments = [accumulator_segments(phase, self.increment, internal_frames)
                         for phase in constants["start_phase"]]
        self.kernel = (float(constants["converter_cutoff"]), float(constants["converter_half_length"]),
                       float(constants["converter_beta"]))
        self.reach = int(np.ceil(self.kernel[1])) + 1
        self.output_delay = (float(constants["total_delay"])
                             - (float(constants["tank_predelay"]) + float(constants["input_delay"])) * HOST_PER_INTERNAL)

    def line_length(self, side: int, line: int, step) -> np.ndarray:
        """Length of a line, in internal samples, when its accumulator has made `step` steps."""
        phase = accumulator_phase(self.segments[2 * line + side], step)
        return self.c["line_prime"][side][line] + float(self.c["modulation_depth"]) * np.sin(phase)

    def line_gain(self, input_channel: int, side: int) -> np.ndarray:
        """First-pass gain of each line of group `side` for an impulse on `input_channel`."""
        own = np.asarray(self.c["line_gain"][side], dtype=np.float64)
        return own if input_channel == side else own * np.asarray(self.c["cross_ratio"], dtype=np.float64)

    def pair_gain(self, input_channel: int, side: int) -> np.ndarray:
        own = np.asarray(self.c["pair_gain"][side], dtype=np.float64)
        return own if input_channel == side else own * float(self.c["cross_ratio"][0])

    def core(self, time: int, input_channel: int, side: int) -> tuple:
        """(first internal index, core output) of group `side` for a host impulse at `time`."""
        host_index = self.warmup_frames + time
        centre = host_index / HOST_PER_INTERNAL + float(self.c["input_delay"])
        first = int(np.floor(centre))
        source = first + np.arange(-self.reach, self.reach + 1)
        stream = float(self.c["converter_gain"]) * kaiser_sinc(source - centre, *self.kernel) / HOST_PER_INTERNAL
        written = source + int(self.c["tank_predelay"])                              # when the tank receives it
        base = written[0]
        taps = np.zeros(INTERNAL_FRAMES)
        fed_back = np.zeros(INTERNAL_FRAMES)
        passes = []
        gains = self.line_gain(input_channel, side)
        for line in range(8):
            prime = int(self.c["line_prime"][side][line])
            read = np.arange(written[0] + prime - MODULATION_REACH, written[-1] + prime + MODULATION_REACH + 1)
            length = self.line_length(side, line, read)
            out = triangle(read[:, None] - length[:, None] - written[None, :]) @ stream
            passes.append((read, out))
            if gains[line] != 0.0:
                taps[read - base] += gains[line] * out
        for (first_line, second_line), gain in zip(self.c["pairs"], self.pair_gain(input_channel, side)):
            fed, signal = passes[int(first_line)]
            prime = int(self.c["line_prime"][side][int(second_line)])
            read = np.arange(fed[0] + prime - MODULATION_REACH,
                             min(fed[-1] + prime + MODULATION_REACH + 1, base + INTERNAL_FRAMES))
            length = self.line_length(side, int(second_line), read)
            fed_back[read - base] += gain * (triangle(read[:, None] - length[:, None] - fed[None, :]) @ signal)
        taps += np.convolve(fed_back, self.c["loop_kernel"])[:INTERNAL_FRAMES]
        return base, np.convolve(taps, self.c["core_filter"])[:INTERNAL_FRAMES]

    def predict(self, time: int, input_channel: int) -> np.ndarray:
        response = np.zeros((WINDOW_STOP, 2))
        sample = np.arange(WINDOW_START, WINDOW_STOP)
        position = (sample + self.warmup_frames + time - self.output_delay) / HOST_PER_INTERNAL
        for side in (0, 1):
            base, internal = self.core(time, input_channel, side)
            nearest = np.round(position).astype(np.int64) - base
            index = nearest[:, None] + np.arange(-self.reach, self.reach + 1)[None, :]
            valid = (index >= 0) & (index < len(internal))
            values = np.where(valid, internal[np.clip(index, 0, len(internal) - 1)], 0.0)
            response[WINDOW_START:, side] = np.sum(
                kaiser_sinc(position[:, None] - (base + index), *self.kernel) * values, axis=1)
        return response


def load_constants(path: Path = DATA) -> dict:
    with np.load(path) as data:
        return {key: data[key] for key in data.files}


_model: Model | None = None


def predict(time: int, input_channel: int) -> np.ndarray:
    global _model
    if _model is None:
        _model = Model(load_constants())
    return _model.predict(int(time), int(input_channel))
