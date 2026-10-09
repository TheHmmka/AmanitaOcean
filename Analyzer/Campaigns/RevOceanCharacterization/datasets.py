#!/usr/bin/env python3
"""Shared reference datasets of the Rev OCEAN campaign.

Every dataset is a deterministic stimulus rendered through `revocean.capture`,
so calling a loader either reads the cache or reproduces the capture bit for
bit. All of them use the neutral baseline with Macro 0 and Decay 0.5 s, where
a response has fallen by 60 dB after 0.5 s; impulses are therefore at least
0.5 s apart and each response is clean to about -85 dB below its first tap.

`grid_responses` is the working set: unit-impulse responses on a 100 ms grid of
impulse times, 980 per input channel.

`holdout_first_order` is a LOCKED HOLDOUT. It may be read by
`score_first_order.py` only. Nothing may be fitted, tuned or selected on it.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor

import numpy as np

import revocean

IMPULSE_AMPLITUDE = 0.5
GRID_SETTINGS = {"decay": revocean.normalised("decay", 0.5)}
GRID_SECONDS = 100.0
GRID_OFFSETS = (0, 4800, 9600, 14400, 19200)   # five renders, 100 ms apart
GRID_SPACING = 24000                           # 0.5 s between impulses of one render
HOLDOUT_SEED = 20261006


def _responses(times: np.ndarray, channel: int, length: int, settings: dict) -> np.ndarray:
    stimulus = revocean.impulses(GRID_SECONDS, [(int(t), channel, IMPULSE_AMPLITUDE) for t in times])
    output = revocean.capture(stimulus, settings).output
    return np.stack([output[int(t):int(t) + length] for t in times]) / IMPULSE_AMPLITUDE


def grid_times(offset: int) -> np.ndarray:
    return np.arange(revocean.SAMPLE_RATE + offset, int(99 * revocean.SAMPLE_RATE), GRID_SPACING)


def grid_responses(input_channel: int, length: int = 6000) -> tuple:
    """(impulse times in samples, responses[time, sample after the impulse, output channel]).

    Times count from the first sample after the 10 s warm-up. Responses are raw:
    they still contain the 48 samples of reported latency. `length` may be up to
    24000 (0.5 s); only the first 0.5 s is free of the next impulse.
    """
    if input_channel not in (0, 1) or not 0 < length <= GRID_SPACING:
        raise ValueError("input_channel is 0 or 1 and length is at most 24000")
    with ThreadPoolExecutor(len(GRID_OFFSETS)) as pool:
        parts = list(pool.map(lambda offset: _responses(grid_times(offset), input_channel, length, GRID_SETTINGS), GRID_OFFSETS))
    times = np.concatenate([grid_times(offset) for offset in GRID_OFFSETS])
    order = np.argsort(times)
    return times[order], np.concatenate(parts)[order].astype(np.float32)


def holdout_times() -> np.ndarray:
    """Pseudo-random impulse times, 0.5 to 0.75 s apart, off the 100 ms grid."""
    generator = np.random.default_rng(HOLDOUT_SEED)
    times = [revocean.SAMPLE_RATE + int(generator.integers(0, 4800))]
    while True:
        following = times[-1] + GRID_SPACING + int(generator.integers(1, 12000))
        if following >= int(99 * revocean.SAMPLE_RATE):
            break
        times.append(following)
    return np.array(times)


def holdout_first_order(input_channel: int, length: int = 6000) -> tuple:
    """LOCKED HOLDOUT: scoring only (see the module docstring)."""
    times = holdout_times()
    return times, _responses(times, input_channel, length, GRID_SETTINGS).astype(np.float32)


# ---------------------------------------------------------------- whole responses

# (sample rate, Decay in seconds, Size in percent, warm-up in seconds)
NETWORK_HOLDOUT_CASES = (
    (48000, 0.5, 100.0, 10.0),
    (48000, 2.0, 100.0, 10.0),
    (48000, 7.3, 100.0, 10.0),
    (48000, 1.4, 62.0, 10.0),
    (48000, 3.1, 157.0, 10.0),
    (44100, 2.0, 100.0, 10.0),
    (48000, 1.0, 88.0, 12.5),
)
NETWORK_PROGRAMME_SECONDS = 14.0
NETWORK_PROGRAMME_SEED = 20261007


def network_programme(sample_rate: int, seed: int = NETWORK_PROGRAMME_SEED) -> np.ndarray:
    """A short programme of impulses, a noise burst and plucked tones, then silence.

    Its peak stays at or below 0.5, under the level where the reference stops
    being linear (0.5629). The holdout uses the default seed; use another seed
    for working data.
    """
    generator = np.random.default_rng(seed)
    frames = int(round(NETWORK_PROGRAMME_SECONDS * sample_rate))
    programme = np.zeros((frames, 2))
    at = lambda seconds: int(round(seconds * sample_rate))
    programme[at(0.25), 0] = 0.5
    programme[at(0.80), 1] = -0.4
    burst = generator.standard_normal((at(0.03), 2))
    programme[at(1.40):at(1.40) + len(burst)] += 0.5 * burst / np.abs(burst).max()
    for note, (frequency, pan) in enumerate(((220.0, 0.2), (554.37, 0.8), (1318.5, 0.5), (3520.0, 0.35))):
        length = at(0.25)
        time = np.arange(length) / sample_rate
        tone = np.sin(2 * np.pi * frequency * time + generator.uniform(0, 2 * np.pi)) * np.exp(-time / 0.05)
        tone *= np.minimum(1.0, np.arange(length) / at(0.002))
        start = at(2.2 + 0.3 * note)
        programme[start:start + length, 0] += (1 - pan) * tone
        programme[start:start + length, 1] += pan * tone
    programme[at(4.0), :] += 0.3
    programme *= 0.5 / np.abs(programme).max()
    return programme.astype(np.float32)


def holdout_network_reference(case: int) -> tuple:
    """LOCKED HOLDOUT: (stimulus, raw reference output) of one case; scoring only."""
    sample_rate, decay, size, warmup = NETWORK_HOLDOUT_CASES[case]
    stimulus = network_programme(sample_rate)
    settings = {"decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}
    return stimulus, revocean.capture(stimulus, settings, sample_rate=sample_rate, warmup=warmup).output
