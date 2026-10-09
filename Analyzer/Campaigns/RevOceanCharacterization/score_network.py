#!/usr/bin/env python3
"""Score a whole-response model of the network on the locked holdout.

A candidate is a Python module with

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> array (frames, 2)

giving the reference's output at the neutral baseline (Macro 0, Mix 100 %) for
a stereo stimulus of shape (frames, 2) that starts `warmup_seconds` of silence
after the first processed sample. The result is raw like `revocean.capture`
output: index n is host sample n of the stimulus, the reported latency is still
in it, and it covers at least the length of the stimulus.

The holdout is `datasets.NETWORK_HOLDOUT_CASES` rendered with
`datasets.network_programme`: impulses, a noise burst and plucked tones in the
first 4 s, then 10 s in which the response dies away. Scores are
20 log10(|candidate - reference| / |reference|) with no gain, delay or polarity
fit, overall and in four stretches of time, so one can see how far into the
recirculating response a model holds.

The holdout is for scoring: a candidate that was fitted or selected on it is
void. Use `datasets.network_programme(rate, seed)` with another seed and other
settings for working data.
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

STRETCHES = (("0.25-1.4s", 0.25, 1.4), ("1.4-4s", 1.4, 4.0), ("4-6s", 4.0, 6.0), ("6-14s", 6.0, 14.0))


def load_candidate(path: Path):
    spec = importlib.util.spec_from_file_location("candidate", path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(module)
    return module


def null_db(candidate: np.ndarray, reference: np.ndarray) -> float:
    difference = float(np.sum((candidate - reference) ** 2))
    return round(10.0 * np.log10(max(difference, 1e-300) / max(float(np.sum(reference ** 2)), 1e-300)), 2)


def score(candidate, cases=None) -> dict:
    result = {"cases": []}
    for index in (range(len(datasets.NETWORK_HOLDOUT_CASES)) if cases is None else cases):
        sample_rate, decay, size, warmup = datasets.NETWORK_HOLDOUT_CASES[index]
        stimulus, reference = datasets.holdout_network_reference(index)
        reference = reference[:len(stimulus)].astype(np.float64)
        rendered = np.asarray(candidate.render(stimulus, sample_rate, warmup, decay, size), dtype=np.float64)
        if rendered.ndim != 2 or rendered.shape[1] != 2 or rendered.shape[0] < len(stimulus):
            raise ValueError(f"render returned {rendered.shape}, expected at least {stimulus.shape}")
        rendered = rendered[:len(stimulus)]
        entry = {"sampleRate": sample_rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup,
                 "overallDb": null_db(rendered, reference)}
        for name, start, stop in STRETCHES:
            window = slice(int(start * sample_rate), int(stop * sample_rate))
            entry[name] = null_db(rendered[window], reference[window])
        result["cases"].append(entry)
    result["worstOverallDb"] = max(entry["overallDb"] for entry in result["cases"])
    result["meanOverallDb"] = round(float(np.mean([entry["overallDb"] for entry in result["cases"]])), 2)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("candidate", type=Path, help="module with render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent)")
    parser.add_argument("--case", type=int, action="append", help="score only this case index (repeatable)")
    arguments = parser.parse_args()
    revocean.identity()
    print(json.dumps(score(load_candidate(arguments.candidate), arguments.case), indent=1))


if __name__ == "__main__":
    main()
