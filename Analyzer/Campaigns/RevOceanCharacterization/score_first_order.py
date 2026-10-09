#!/usr/bin/env python3
"""Score a first-order tap model on the locked holdout.

A candidate is a Python module with

    predict(time: int, input_channel: int) -> numpy array of shape (WINDOW_STOP, 2)

giving the unit-impulse response of the reference at the neutral baseline with
Decay 0.5 s for an impulse on `input_channel` at sample `time` (counted from the
first sample after the 10 s warm-up). The array is raw: index n is the output
n samples after the impulse, including the 48 samples of reported latency, and
column c is output channel c.

Only [WINDOW_START, WINDOW_STOP) is scored. The window holds the first passes
of the eight shortest lines of each group and, from about sample 2350 on, the
earliest second passes.

The score is 20 log10(|candidate - reference| / |reference|), with no gain,
delay or polarity fit. The holdout times lie off the 100 ms working grid. The
holdout is for scoring: a candidate that was fitted or selected on it is void.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

import numpy as np

import datasets
import revocean

WINDOW_START = 1200
WINDOW_STOP = 2500


def load_candidate(path: Path):
    spec = importlib.util.spec_from_file_location("candidate", path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(module)
    return module


def score(candidate) -> dict:
    names = ("L", "R")
    result = {"window": [WINDOW_START, WINDOW_STOP], "paths": {}, "worstResponseDb": -1000.0}
    total_difference = 0.0
    total_reference = 0.0
    for input_channel in (0, 1):
        times, reference = datasets.holdout_first_order(input_channel, WINDOW_STOP)
        predicted = np.stack([np.asarray(candidate.predict(int(time), input_channel), dtype=np.float64)[:WINDOW_STOP]
                              for time in times])
        if predicted.shape != reference.shape:
            raise ValueError(f"predict returned {predicted.shape[1:]}, expected {reference.shape[1:]}")
        window = slice(WINDOW_START, WINDOW_STOP)
        difference = predicted[:, window] - reference[:, window].astype(np.float64)
        for output_channel in (0, 1):
            d = float(np.sum(difference[:, :, output_channel] ** 2))
            r = float(np.sum(reference[:, window, output_channel].astype(np.float64) ** 2))
            result["paths"][f"{names[input_channel]}->{names[output_channel]}"] = round(10.0 * np.log10(max(d, 1e-300) / r), 2)
            total_difference += d
            total_reference += r
        each = 10.0 * np.log10(np.maximum(np.sum(difference ** 2, axis=(1, 2)), 1e-300)
                               / np.sum(reference[:, window].astype(np.float64) ** 2, axis=(1, 2)))
        result["worstResponseDb"] = round(max(result["worstResponseDb"], float(each.max())), 2)
        result[f"responses{names[input_channel]}"] = int(len(times))
    result["overallDb"] = round(10.0 * np.log10(max(total_difference, 1e-300) / total_reference), 2)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("candidate", type=Path, help="module with predict(time, input_channel)")
    arguments = parser.parse_args()
    revocean.identity()
    print(json.dumps(score(load_candidate(arguments.candidate)), indent=1))


if __name__ == "__main__":
    main()
