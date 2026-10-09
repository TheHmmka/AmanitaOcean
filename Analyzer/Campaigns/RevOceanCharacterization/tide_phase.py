#!/usr/bin/env python3
"""Generator of the Tide voice phase of Rev OCEAN (packet tide_phase).

Above Macro 0 each output channel of the reference has one phase that drives its
two voices (findings/tide.md). Measured on 40 instances of 100 s at Macro 100 %
and on smaller sets (findings/tide_phase.md), that phase is

    phase_o(t) = level_o(t) + integral of rate(Macro) dt        cycles, voice A
    voice B    = phase_o(t) + 1/2                                at all times

with t counted from the first processed sample of the instance. `level_o` is
value noise on a jittered grid, one generator per output channel:

    knot k of channel o sits at  (k + u_k) / GRID_HZ[o],  k = 1, 2, ...,  u_k uniform in [0, 1)
    the level holds its start value until knot 1
    between knot k and knot k + 1 it moves from its value at knot k to a new target
    along a raised cosine; targets are uniform in [LEVEL_LOW[o], LEVEL_HIGH[o]]

Deterministic and the same in every instance: the rate law, the two grid
frequencies, the grid origin (start of processing), the level bounds, the
raised-cosine shape, the half-cycle offset of voice B. Random: the start
levels (the two channels are tied, see `start_levels`), every jitter and every
target. Nothing depends on the host rate, the block size, Size or Decay.

Use:

    generator = TidePhase(seed=7, macro=1.0)
    phase = generator.phase(times_in_seconds)        # array [2, len(times)], voice A; voice B is phase + 0.5
    stream = TidePhaseStream(seed=7, sample_rate=48000)
    block = stream.process(512, macro=1.0)           # the next 512 samples, [2, 512]

Run `python tide_phase.py` to compare generated with measured trajectories
(tide_structural_data/tide_phase.npz, written by measure_tide_phase.py).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from scipy import stats

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "tide_phase.npz"

GRID_HZ = (0.136, 0.160)             # knot grid of the left and right output (periods 7.353 s and 6.25 s)
LEVEL_LOW = (0.184, -0.007)          # cycles; bounds of the level, left and right output
LEVEL_HIGH = (0.591, 0.433)
RATE_LOW, RATE_HIGH = 0.0508, 0.05645    # cycles/s at Macro 0 (extrapolated) and at Macro 100 %
START_SAME_HALF = 0.83               # share of instances whose two start levels lie in the same half of their ranges


def rate(macro):
    """Phase rate in cycles per second; Macro is 0..1. Measured at 25, 50, 75 and 100 %."""
    return RATE_LOW + (RATE_HIGH - RATE_LOW) * np.expm1(np.asarray(macro, float)) / np.expm1(1.0)


def ease(u):
    """Raised cosine from 0 to 1."""
    return 0.5 - 0.5 * np.cos(np.pi * np.clip(u, 0.0, 1.0))


def start_levels(generator: np.random.Generator) -> np.ndarray:
    """Normalised start values (left, right), each uniform in [0, 1); both usually fall in the same half."""
    right_high = generator.random() < 0.5
    left_high = right_high if generator.random() < START_SAME_HALF else not right_high
    return np.array([(left_high + generator.random()) / 2.0, (right_high + generator.random()) / 2.0])


class LevelNoise:
    """The level of one output channel: nodes (time, value) joined by raised cosines, extended on demand."""

    def __init__(self, generator: np.random.Generator, channel: int, start: float):
        self.generator, self.period = generator, 1.0 / GRID_HZ[channel]
        self.low, self.span = LEVEL_LOW[channel], LEVEL_HIGH[channel] - LEVEL_LOW[channel]
        first = self.low + self.span * start
        self.times = [0.0, self._knot(1)]          # the level holds its start value until knot 1
        self.values = [first, first]

    def _knot(self, index: int) -> float:
        return (index + self.generator.random()) * self.period

    def _extend(self, until: float) -> None:
        while self.times[-1] <= until:
            self.times.append(self._knot(len(self.times)))
            self.values.append(self.low + self.span * self.generator.random())

    def __call__(self, t) -> np.ndarray:
        t = np.asarray(t, float)
        self._extend(float(t.max()) if t.size else 0.0)
        times, values = np.array(self.times), np.array(self.values)
        index = np.clip(np.searchsorted(times, t, side="right") - 1, 0, len(times) - 2)
        u = (t - times[index]) / (times[index + 1] - times[index])
        return values[index] + (values[index + 1] - values[index]) * ease(u)


class TidePhase:
    """Voice A phase of both outputs for one instance, as a function of time at a fixed Macro."""

    def __init__(self, seed: int, macro: float = 1.0):
        generator = np.random.default_rng(seed)
        start = start_levels(generator)
        self.macro = macro
        self.level = [LevelNoise(np.random.default_rng(generator.integers(2 ** 63)), channel, start[channel]) for channel in (0, 1)]

    def levels(self, t) -> np.ndarray:
        """phase - rate * t for both outputs, [2, len(t)]."""
        return np.stack([self.level[channel](t) for channel in (0, 1)])

    def phase(self, t) -> np.ndarray:
        """Unwrapped phase of voice A in cycles, [2, len(t)]; voice B is this plus 0.5."""
        return self.levels(t) + rate(self.macro) * np.asarray(t, float)

    def render(self, frames: int, sample_rate: float) -> np.ndarray:
        return self.phase(np.arange(frames) / sample_rate)


class TidePhaseStream:
    """Block-wise generator for a processor: the rate follows the Macro given with each block.

    What the reference does when Macro moves during playback was not measured; here the
    rate is integrated, so the phase stays continuous. The level noise does not depend on Macro.
    """

    def __init__(self, seed: int, sample_rate: float):
        self.source = TidePhase(seed)
        self.sample_rate, self.position, self.ramp = float(sample_rate), 0, 0.0

    def process(self, frames: int, macro: float = 1.0) -> np.ndarray:
        step = float(rate(macro)) / self.sample_rate
        t = (self.position + np.arange(frames)) / self.sample_rate
        out = self.source.levels(t) + self.ramp + step * np.arange(frames)
        self.position += frames
        self.ramp += step * frames
        return out


# ------------------------------------------------------------------ comparison with the measurement

LAGS = (0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0)                    # seconds
RATE_LAGS = (0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 20.0)
BANDS = ((0.02, 0.04), (0.04, 0.08), (0.08, 0.16), (0.16, 0.32), (0.32, 0.64), (0.64, 1.28))    # Hz
SETTLED = 16.0                                                    # every instance has left its start hold by then
SMALL_SETS = {"macro75": 0.75, "macro50": 0.5, "macro25": 0.25, "rate44": 1.0, "size60_decay2_block128": 1.0}


def generated_levels(count: int, t: np.ndarray, first_seed: int) -> np.ndarray:
    """Levels of `count` generated instances on the frame times t: [2, count, len(t)]."""
    return np.stack([TidePhase(first_seed + k).levels(t) for k in range(count)], axis=1)


def usable(lags, t: np.ndarray) -> list:
    """The lags a record is long enough for: at most 40 % of what follows the start hold."""
    return [lag for lag in lags if lag <= 0.4 * (t[-1] - SETTLED)]


def increments(levels: np.ndarray, t: np.ndarray, lag: float) -> np.ndarray:
    """Level changes over `lag` seconds after the start hold, pooled over channels and instances."""
    step = int(round(lag / (t[1] - t[0])))
    settled = levels[..., t >= SETTLED]
    return (settled[..., step:] - settled[..., :-step]).ravel()


def descriptors(levels: np.ndarray, t: np.ndarray) -> dict:
    """Model-free statistics of an ensemble of level curves [2, instances, frames]."""
    dt = t[1] - t[0]
    out = {}
    for lag in usable(LAGS, t):
        d = increments(levels, t, lag)
        out[f"increment sd, lag {lag:g} s"] = float(d.std())
        out[f"increment |.| 90 %, lag {lag:g} s"] = float(np.percentile(np.abs(d), 90))
    half = increments(levels, t, 0.5) / 0.5
    for q in (1, 5, 25, 50, 75, 95, 99):
        out[f"level rate percentile {q}, cycles/s"] = float(np.percentile(half, q))
    out["level rate share below 0.002 cycles/s"] = float(np.mean(np.abs(half) < 0.002))
    settled = levels[..., t >= SETTLED]
    velocity = np.diff(settled, axis=-1) / dt
    velocity = velocity - velocity.mean()
    for lag in usable(RATE_LAGS, t):
        k = int(round(lag / dt))
        out[f"rate autocorrelation, lag {lag:g} s"] = float(np.mean(velocity[..., k:] * velocity[..., :-k]) / np.mean(velocity ** 2))
    centred = settled - settled.mean(axis=-1, keepdims=True)
    for lag in usable((2.0, 5.0, 10.0, 20.0), t):
        k = int(round(lag / dt))
        out[f"level autocorrelation, lag {lag:g} s"] = float(np.mean(centred[..., k:] * centred[..., :-k]) / np.mean(centred ** 2))
    spectrum = np.abs(np.fft.rfft(centred * np.hanning(centred.shape[-1]), axis=-1)) ** 2
    frequency = np.fft.rfftfreq(centred.shape[-1], dt)
    bands = [band for band in BANDS if band[0] >= 1.5 / (t[-1] - SETTLED)]
    total = spectrum[..., frequency >= bands[0][0]].sum(-1).mean()
    for low, high in bands:
        inside = (frequency >= low) & (frequency < high)
        out[f"level spectrum {low:g}-{high:g} Hz, dB re total"] = float(10 * np.log10(spectrum[..., inside].sum(-1).mean() / total))
    for channel, name in ((0, "left"), (1, "right")):
        out[f"level mean, {name}"] = float(settled[channel].mean())
        out[f"level sd, {name}"] = float(settled[channel].std())
        out[f"level percentile 0.5, {name}"] = float(np.percentile(settled[channel], 0.5))
        out[f"level percentile 99.5, {name}"] = float(np.percentile(settled[channel], 99.5))
    out["left-right level correlation"] = float(np.mean([np.corrcoef(a, b)[0, 1] for a, b in zip(centred[0], centred[1])]))
    start = levels[..., (t > 1.5) & (t < 5.5)].mean(-1)
    out["left-right start correlation"] = float(np.corrcoef(start)[0, 1])
    out["start sd, left"], out["start sd, right"] = float(start[0].std()), float(start[1].std())
    moved = (np.abs(levels - start[..., None]) > 0.01) & (t > 5.5)
    first = t[np.argmax(moved, axis=-1)]
    for q in (5, 50, 95):
        out[f"time of the first 0.01 cycle move, percentile {q}, s"] = float(np.percentile(first, q))
    return out


def compare(name: str = "main", trials: int = 60, first_seed: int = 1000) -> dict:
    """One measured set against ensembles of the same size from the generator.

    For every descriptor: the measured value, the mean and sd over `trials` generated
    ensembles, and z = (measured - mean) / sd. For the increment distributions also a
    two-sample Kolmogorov-Smirnov distance with its rank among generated-against-generated
    distances (a one-sided p value that respects the dependence inside a trajectory).
    """
    data = np.load(DATA)
    t = data[f"{name}_t"]
    measured = data[f"{name}_phase"] - float(data[f"{name}_rate"]) * t
    ensembles = [generated_levels(measured.shape[1], t, first_seed + 1000 * trial) for trial in range(trials)]
    reference = descriptors(measured, t)
    simulated = [descriptors(ensemble, t) for ensemble in ensembles]
    table = {}
    for key, value in reference.items():
        values = np.array([s[key] for s in simulated])
        table[key] = {"measured": value, "generated": float(values.mean()), "sd": float(values.std(ddof=1)),
                      "z": float((value - values.mean()) / values.std(ddof=1))}
    for lag in usable(LAGS, t):
        pooled = np.concatenate([increments(e, t, lag) for e in ensembles[:trials // 2]])
        distance = stats.ks_2samp(increments(measured, t, lag), pooled).statistic
        null = np.array([stats.ks_2samp(increments(e, t, lag), pooled).statistic for e in ensembles[trials // 2:]])
        table[f"KS distance of increments, lag {lag:g} s"] = {
            "measured": float(distance), "generated": float(null.mean()), "sd": float(null.std(ddof=1)),
            "p": float((np.sum(null >= distance) + 1) / (len(null) + 1))}
    return table


def compare_starts(count: int = 20000, seed: int = 5) -> dict:
    """The 200 measured start pairs against the start law."""
    data = np.load(DATA)
    span = np.array(LEVEL_HIGH) - np.array(LEVEL_LOW)
    measured = (data["start_levels"] - np.array(LEVEL_LOW)[:, None]) / span[:, None]
    generator = np.random.default_rng(seed)
    generated = np.array([start_levels(generator) for _ in range(count)]).T
    return {
        "instances": int(measured.shape[1]),
        "KS p, left start against uniform": float(stats.kstest(np.clip(measured[0], 0, 1), "uniform").pvalue),
        "KS p, right start against uniform": float(stats.kstest(np.clip(measured[1], 0, 1), "uniform").pvalue),
        "left-right correlation, measured": float(np.corrcoef(measured)[0, 1]),
        "left-right correlation, generated": float(np.corrcoef(generated)[0, 1]),
        "KS p, left minus right, measured against generated": float(
            stats.ks_2samp(measured[0] - measured[1], generated[0] - generated[1]).pvalue),
        "same half, measured": float(np.mean((measured[0] > 0.5) == (measured[1] > 0.5))),
        "same half, generated": float(np.mean((generated[0] > 0.5) == (generated[1] > 0.5))),
    }


def compare_rates() -> dict:
    """Measured phase rate of every set against the rate law, cycles/s."""
    data = np.load(DATA)
    sets = {"main": 1.0, **SMALL_SETS}
    return {name: {"macro": macro, "measured": float(data[f"{name}_rate"]), "law": float(rate(macro))} for name, macro in sets.items()}


def summarise(table: dict) -> str:
    z = np.array([row["z"] for row in table.values() if "z" in row])
    p = np.array([row["p"] for row in table.values() if "p" in row])
    return (f"{len(z)} descriptors: {np.sum(np.abs(z) > 2)} beyond 2 sd, {np.sum(np.abs(z) > 3)} beyond 3 sd, "
            f"rms z {np.sqrt(np.mean(z ** 2)):.2f}; smallest KS p {p.min():.2f}")


def main() -> None:
    report = {"main": compare("main")}
    print(f"{'main set, 40 instances of 100 s at Macro 100 %':58s} {'measured':>10s} {'generated':>10s} {'sd':>9s} {'z or p':>8s}")
    for key, row in report["main"].items():
        last = f"p {row['p']:.2f}" if "p" in row else f"{row['z']:+.2f}"
        print(f"{key:58s} {row['measured']:10.4f} {row['generated']:10.4f} {row['sd']:9.4f} {last:>8s}")
    print(summarise(report["main"]))
    for name in SMALL_SETS:
        report[name] = compare(name)
        print(f"{name}: {summarise(report[name])}")
    report["starts"] = compare_starts()
    for key, value in report["starts"].items():
        print(f"{key:58s} {value:10.4f}")
    report["rates"] = compare_rates()
    for name, row in report["rates"].items():
        print(f"rate, {name:24s} Macro {row['macro']:.2f}: measured {row['measured']:.5f}, law {row['law']:.5f} cycles/s")
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
