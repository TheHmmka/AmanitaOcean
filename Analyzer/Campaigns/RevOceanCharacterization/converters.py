#!/usr/bin/env python3
"""The two rate converters of Rev OCEAN around its 44.1 kHz network, at any host rate.

    first, internal = to_internal(host, host_rate, origin)       host signal -> 44.1 kHz network input
    host = to_host(internal, host_rate, first, start, stop)      44.1 kHz network output -> host signal

All indices count from the first sample the plug-in processes (warm-up included):
`origin` is the index of host[0], `first` the index of internal[0], and `to_host`
returns host samples [start, stop). Internal index m is the step count of the
network's oscillators, so the network of first_order_model.py can be run on the
internal stream as it is (it writes internal sample m into the lines at m + 44).
A mono signal is converted; convert each channel on its own.

What is in here (evidence: findings/converters.md, numbers: tide_structural_data/converters.json):

* The network runs at 44.1 kHz at every host rate. At a 44.1 kHz host there is no
  converter: internal sample = host sample, and the output leaves `latency` = 44
  samples after the network.
* Reported latency: 4 * floor(11 * rate / 44100 + 0.5) host samples (44 internal
  samples expressed at the host rate and rounded to a multiple of 4). The plug-in
  converts in blocks of that many host samples.
* Both converters read one table: cutoff * sinc(cutoff x) under a Kaiser window
  (cutoff 0.9, 17 zero crossings per wing, beta 6), 4096 single-precision entries
  per zero crossing, read without interpolation at the entry at or below the
  wanted distance. x counts periods of the lower of the two rates, which is the
  internal rate at every host rate that could be measured (44.1 kHz and above).
  - Host to internal (down): the table is stepped by dh = 4096 * 44100 / rate per
    host sample, so the kernel spans 17 internal samples per wing, and the sum is
    scaled by 44100 / rate.
  - Internal to host (up): the table is stepped by 4096 per internal sample, gain 1.
  - The wing of inputs after the output sample never uses the last table entry.
* Time base. With rate / 44100 = L / M in lowest terms, host and internal samples
  meet on a lattice of 1 / L internal sample (host sample n at M n, internal
  sample m at L m):
      internal sample m is centred at host time ((m + 1) L - (X_in + 1) M) / M,
      host output sample N reads internal time (M (N - D) - 28 L) / L,
  X_in = floor(18 L / M + 10) host samples, D = latency for a whole ratio (M = 1)
  and 2 * latency otherwise. 18 = zero crossings + 1, and 28 = 18 + 10.
* Exact table entries. Where the wanted distance falls exactly on a table entry
  the result depends on the rounding of the converter's clock, a double-precision
  time that advances by 1 / factor per output sample:
  - a clock that is a hair low takes the entry below on the wing of inputs before
    the output sample, a clock that is a hair high on the wing of inputs after it;
  - a clock that is exact (its step a binary fraction) takes the exact entry in the
    up-converter; in the down-converter the table position itself is accumulated
    in double precision (Ho = phase * dh; Ho += dh), which lowers single taps.
  `clock_signs` returns the pair of signs read off captures where every pair was
  tried (MEASURED_CLOCK_SIGNS: 48, 56, 64, 88.2, 96, 176.4, 192 and 384 kHz; at 64
  and 384 kHz on whole responses of the network, see README.md). At any other
  rate it runs the classic bookkeeping of such a converter in blocks of `latency`
  host samples. That bookkeeping is INFERRED: it gives the measured sign for 121
  of 122 clocks tested and the wrong output sign at 56 kHz
  (findings/converters_verification.md).
* Rates below 44.1 kHz could not be captured (the Analyzer refuses them). There
  the module applies the same rules with the two converters exchanging roles;
  that is INFERRED and unverified.

The module renders nothing. `python converters.py [rate ...]` prints the constants of a rate.
"""
from __future__ import annotations

import sys
from fractions import Fraction
from functools import lru_cache
from math import gcd

import numpy as np

INTERNAL_RATE = 44100
CUTOFF = 0.9
ZERO_CROSSINGS = 17
KAISER_BETA = 6.0
ENTRIES = 4096                               # table entries per zero crossing
WING = ZERO_CROSSINGS * ENTRIES              # entries in one wing of the kernel
DROPPED_AFTER = 1                            # table entries the wing of inputs after the output sample never uses
LOOKAHEAD_MARGIN = 10                        # samples a converter looks ahead beyond zero crossings + 1 periods
LATENCY_INTERNAL = 44                        # the reported latency, in internal samples before rounding
CLOCK_SETTLE_SECONDS = 1.0                   # the clock simulation runs this long; the sign is fixed within milliseconds
DRIFT = 2.0 ** -33                           # stands for the rounding drift of a clock; only its sign matters
# (input converter, output converter) clock signs measured with all nine pairs tried: -1 low, +1 high, 0 exact
MEASURED_CLOCK_SIGNS = {48000: (-1, 1), 56000: (-1, -1), 64000: (-1, -1), 88200: (0, 0), 96000: (-1, 1), 176400: (0, 0),
                        192000: (1, -1), 384000: (1, -1)}


def lattice(host_rate: int) -> tuple:
    """(L, M) with host_rate / 44100 = L / M in lowest terms: a host sample is M lattice steps, an internal one L."""
    divisor = gcd(int(host_rate), INTERNAL_RATE)
    return int(host_rate) // divisor, INTERNAL_RATE // divisor


def reported_latency(host_rate: int) -> int:
    """Latency the plug-in reports, in host samples; also its conversion block."""
    return 4 * int(np.floor(LATENCY_INTERNAL / 4 * host_rate / INTERNAL_RATE + 0.5))


def lookahead(source_per_target: Fraction) -> int:
    """Samples of its input a converter looks ahead: floor(18 * max(1, source rate / target rate) + 10)."""
    return int((ZERO_CROSSINGS + 1) * max(Fraction(1), source_per_target) + LOOKAHEAD_MARGIN)


def input_delay(host_rate: int) -> int:
    """a, in lattice steps: a host impulse at n is centred at internal time (M n + a) / L."""
    L, M = lattice(host_rate)
    if L == M:
        return 0
    return (lookahead(Fraction(L, M)) + 1) * M - L


def output_blocks(host_rate: int) -> int:
    """Blocks of `reported_latency` host samples the output is held back: one for a whole rate ratio, else two."""
    L, M = lattice(host_rate)
    return 1 if (M == 1 or L == 1) else 2


def output_delay(host_rate: int) -> int:
    """b, in lattice steps: host output sample N reads the network output at internal time (M N - b) / L."""
    L, M = lattice(host_rate)
    held = output_blocks(host_rate) * reported_latency(host_rate) * M
    return held if L == M else lookahead(Fraction(M, L)) * L + held


def fixed_delay_host_samples(host_rate: int, network_samples: float = 44.0) -> float:
    """Host samples between an impulse and the arrival of a line of length 0 (the network's 44 samples included)."""
    L, M = lattice(host_rate)
    return (input_delay(host_rate) + output_delay(host_rate) + network_samples * L) / M


@lru_cache(maxsize=None)
def kernel_table() -> np.ndarray:
    """One wing of the kernel: cutoff * sinc(cutoff i / 4096) * Kaiser, i < 17 * 4096, as single-precision values."""
    index = np.arange(WING)
    window = np.i0(KAISER_BETA * np.sqrt(1.0 - (index / (WING - 1)) ** 2)) / np.i0(KAISER_BETA)
    table = (CUTOFF * np.sinc(CUTOFF * index / ENTRIES) * window).astype(np.float32).astype(np.float64)
    table.setflags(write=False)
    return table


# ---------------------------------------------------------------- the converter clocks

class Clock:
    """Double-precision time of a block converter: starts at its look-ahead and advances by `step` per output.

    A call that brings `count` new input samples produces outputs while the time is
    below start + count, then moves the time back by the samples consumed. Whole
    samples by which the time has crept past its look-ahead are removed and taken
    off the next call.
    """

    def __init__(self, step: Fraction, start: int):
        self.exact_step, self.step, self.start = step, float(step), start
        self.time = float(start)
        self.outputs = self.consumed = self.pending = 0

    def call(self, count: int) -> int:
        """Feed `count` input samples; returns the number of output samples."""
        self.pending += count
        if self.pending <= 0:
            return 0
        count, self.pending = self.pending, 0
        times = np.cumsum(np.concatenate([[self.time], np.full(int(count / self.step) + 2, self.step)]))
        produced = int(np.searchsorted(times, self.time + count, side="left"))
        self.time = float(times[produced]) - count
        creep = int(self.time) - self.start
        self.time -= creep
        self.pending = -creep
        self.outputs += produced
        self.consumed += count + creep
        return produced

    def error_sign(self) -> int:
        """Sign of the rounded time minus the exact time."""
        exact = self.start + self.outputs * self.exact_step - self.consumed
        return int(np.sign(Fraction(self.time) - exact))


@lru_cache(maxsize=None)
def clock_signs(host_rate: int, block: int | None = None, seconds: float = CLOCK_SETTLE_SECONDS) -> tuple:
    """(input converter, output converter): -1 clock a hair low, +1 a hair high, 0 exact.

    The measured pair where there is one, else the simulated one. The plug-in converts in blocks of
    `reported_latency` host samples; a `block` runs the simulation with that size, to show that no
    other block size gives the measured signs.
    """
    L, M = lattice(host_rate)
    if L == M:
        return 0, 0
    if block is None and host_rate in MEASURED_CLOCK_SIGNS:
        return MEASURED_CLOCK_SIGNS[host_rate]
    block = reported_latency(host_rate) if block is None else block
    into = Clock(Fraction(L, M), lookahead(Fraction(L, M)))
    back = Clock(Fraction(M, L), lookahead(Fraction(M, L)))
    for _ in range(int(seconds * host_rate / block)):
        back.call(into.call(block))
    return into.error_sign(), back.error_sign()


# ---------------------------------------------------------------- one converter

def _phases(numerator: int, period: int, sign: int) -> tuple:
    """(left phase, right phase, shift of the base sample) of a clock at numerator / period with a rounding sign."""
    if sign < 0 and numerator == 0:
        left, shift = 1.0 - DRIFT, -1
    else:
        left, shift = numerator / period + sign * DRIFT, 0
    return left, 1.0 - left, shift


def _wing_up(phase: float, limit: int) -> list:
    """Table entries of one wing of an up-converter: the truncated phase, then every 4096th entry."""
    first = int(phase * ENTRIES)
    return list(range(first, limit, ENTRIES))


def _wing_down(phase: float, step: float, limit: int) -> list:
    """Table entries of one wing of a down-converter: the table position is accumulated in double precision."""
    position = phase * step
    entries = []
    while int(position) < limit:
        entries.append(int(position))
        position += step
    return entries


@lru_cache(maxsize=None)
def _branches(source: int, target: int, sign: int) -> tuple:
    """Polyphase branches of a converter from rate `source` to rate `target` (both in lattice steps per sample:
    a source sample is `source` steps long). Branch j serves output samples whose time is j steps past a source
    sample; it is (offsets from that source sample, coefficients)."""
    table = kernel_table()
    down = source < target                         # a source sample is shorter than a target sample
    factor = source / target                       # target rate / source rate
    step = min(float(ENTRIES), factor * ENTRIES)
    branches = []
    for numerator in range(source):
        left, right, shift = _phases(numerator, source, sign)
        before = _wing_down(left, step, WING) if down else _wing_up(left, WING)
        after = _wing_down(right, step, WING - DROPPED_AFTER) if down else _wing_up(right, WING - DROPPED_AFTER)
        offsets = np.concatenate([shift - np.arange(len(before)), shift + 1 + np.arange(len(after))]).astype(np.int64)
        coefficients = table[np.array(before + after, dtype=np.int64)] * (factor if down else 1.0)
        branches.append((offsets, coefficients))
    return tuple(branches)


def _convert(signal: np.ndarray, signal_first: int, source: int, target: int, delay: int, sign: int,
             first: int, stop: int) -> np.ndarray:
    """Target samples [first, stop): target sample k sits at lattice time target * k - delay, source sample i at source * i."""
    signal = np.asarray(signal, dtype=np.float64)
    branches = _branches(source, target, sign)
    reach = max(int(np.abs(offsets).max()) for offsets, _ in branches) + 1
    padded = np.concatenate([np.zeros(reach), signal, np.zeros(reach)])
    index = np.arange(first, stop)
    position = target * index - delay
    base = position // source - signal_first + reach
    numerator = position % source
    result = np.zeros(len(index))
    for value in np.unique(numerator):
        offsets, coefficients = branches[value]
        chosen = np.nonzero(numerator == value)[0]
        taps = base[chosen, None] + offsets[None, :]
        inside = (taps >= 0) & (taps < len(padded))
        result[chosen] = np.where(inside, padded[np.clip(taps, 0, len(padded) - 1)], 0.0) @ coefficients
    return result


# ---------------------------------------------------------------- the two converters of the plug-in

def to_internal(host: np.ndarray, host_rate: int, origin: int = 0) -> tuple:
    """(first, internal): the 44.1 kHz network input for host samples host[0] = sample `origin`.

    `internal[k]` is internal sample first + k. Every internal sample the given
    host samples can reach is returned; host samples outside the array count as zero.
    """
    L, M = lattice(host_rate)
    host = np.asarray(host, dtype=np.float64)
    if L == M:
        return int(origin), host.copy()
    delay = input_delay(host_rate)
    span = ZERO_CROSSINGS + 2
    first = (M * origin + delay) // L - span
    stop = (M * (origin + len(host) - 1) + delay) // L + span + 1
    return first, _convert(host, origin, M, L, delay, clock_signs(host_rate)[0], first, stop)


def to_host(internal: np.ndarray, host_rate: int, first: int = 0, start: int = 0, stop: int | None = None) -> np.ndarray:
    """Host output samples [start, stop) for the network output internal[0] = internal sample `first`.

    Internal samples outside the array count as zero. By default the result
    starts at host sample 0 and ends with the last sample the array can reach.
    """
    L, M = lattice(host_rate)
    internal = np.asarray(internal, dtype=np.float64)
    delay = output_delay(host_rate)
    if stop is None:
        stop = (L * (first + len(internal) + ZERO_CROSSINGS) + delay) // M + 1
    if L == M:
        result = np.zeros(stop - start)
        source = np.arange(start, stop) - delay - first
        inside = (source >= 0) & (source < len(internal))
        result[inside] = internal[source[inside]]
        return result
    return _convert(internal, first, L, M, delay, clock_signs(host_rate)[1], start, stop)


def describe(host_rate: int) -> dict:
    """The constants of one host rate, as stored in tide_structural_data/converters.json."""
    L, M = lattice(host_rate)
    signs = clock_signs(host_rate)
    return {
        "hostRateHz": int(host_rate),
        "latticeHostPerInternal": [L, M],
        "reportedLatencySamples": reported_latency(host_rate),
        "inputLookaheadHostSamples": None if L == M else lookahead(Fraction(L, M)),
        "outputLookaheadInternalSamples": None if L == M else lookahead(Fraction(M, L)),
        "outputBlocks": output_blocks(host_rate),
        "inputDelayLatticeSteps": input_delay(host_rate),
        "outputDelayLatticeSteps": output_delay(host_rate),
        "inputDelayInternalSamples": input_delay(host_rate) / L,
        "outputDelayInternalSamples": output_delay(host_rate) / L,
        "fixedDelayHostSamples": fixed_delay_host_samples(host_rate),
        "fixedDelayMs": 1e3 * fixed_delay_host_samples(host_rate) / host_rate,
        "inputGain": 1.0 if L <= M else M / L,
        "outputGain": 1.0 if L >= M else L / M,
        "clockSigns": {"input": signs[0], "output": signs[1]},
    }


if __name__ == "__main__":
    for argument in sys.argv[1:] or ("44100", "48000", "88200", "96000", "176400", "192000"):
        print(describe(int(argument)))
