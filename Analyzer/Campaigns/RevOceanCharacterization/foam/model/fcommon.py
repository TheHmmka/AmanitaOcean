"""Helpers of the Foam first look: session A, the base with an oscillator origin (campaign's abyss/model/base.py).

This file is the campaign's copy of work/foam/analysis/fcommon.py under Analyzer/Results. It differs from it in three
places: where it finds the campaign (two folders up from foam/model/), where the recordings are (under Analyzer/Results,
which is not in Git) and where session A's oscillator origin comes from (foam/sessions.json in place of the analysis
folder's origin.json). Nothing that is computed differs."""
from __future__ import annotations
import json, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
CAMPAIGN = HERE.parents[1]
FOAM = CAMPAIGN.parents[1] / "Results" / "RevOceanCharacterization" / "work" / "foam"
SESSION = FOAM / "session_A"
for p in (CAMPAIGN, CAMPAIGN / "abyss" / "model"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))
import base as B                    # noqa: E402
from base import cv, nm             # noqa: E402
import revocean as ro               # noqa: E402

SR, FI = 48000, 44100
DATA = nm.constants()
DELAY = DATA["input"]["delay_samples"]
ORIGIN_FILE = HERE.parent / "sessions.json"


def info():
    return json.loads((SESSION / "info.json").read_text())


def cases():
    return {c["name"]: c for c in info()["cases"]}


def load(name):
    return np.load(SESSION / f"{name}.npy", mmap_mode="r")


def stim(name):
    return np.load(SESSION / f"{name}.stimulus.npy", mmap_mode="r")


def origin():
    return int(json.loads(ORIGIN_FILE.read_text())["sessions"]["A"]["oscillatorOrigin"])


def null_db(a, b):
    return float(ro.null_db(a, b))


def db(x):
    return 10 * np.log10(np.sum(np.asarray(x, float) ** 2) + 1e-300)


_vals = {}


def values_for(decay, size):
    k = (float(decay), float(size))
    if k not in _vals:
        _vals[k] = B.values_for(decay, size)
    return _vals[k]


def scan_origins(stimulus_segment, recording_segment, first_frame, decay, size, origins):
    values = values_for(decay, size)
    first, internal = B.to_internal(stimulus_segment, SR, first_frame)
    driven = np.ascontiguousarray(nm.equalise(DATA, internal), dtype=np.float64)
    at = first + DELAY
    origins = np.asarray(sorted(origins), dtype=np.int64)
    counts = at - origins
    assert counts.min() >= 0
    lib = B.library()
    phase = np.ascontiguousarray(values["phase"], dtype=np.float32)
    cmin = int(counts.min())
    diffs = np.unique(np.diff(np.sort(counts)))
    stride = int(np.gcd.reduce(diffs)) if len(diffs) else 1
    entries = int((counts.max() - cmin) // stride) + 1
    table = np.zeros((entries, 32), np.float32)
    lib.abyss_walk(cmin, stride, entries, phase, values["increment"], table)
    mix = np.ascontiguousarray(values["tap"][None, :], dtype=np.float64)
    packed = [np.ascontiguousarray(values[key], dtype=np.float64) for key in ("attenuation", "own", "cross", "matrix", "kernel")]
    length = np.ascontiguousarray(values["length"], dtype=np.int32)
    rec = np.asarray(recording_segment, dtype=np.float64)
    out = []
    output = np.zeros((len(driven), 1, 2))
    for o, count in zip(origins, counts):
        theta = np.ascontiguousarray(table[(int(count) - cmin) // stride])
        lib.abyss_network_state(len(driven), theta, driven, length, values["depth"], values["increment"], *packed, len(values["kernel"]), mix, 1, output)
        host = B.to_host(output[:, 0, :], first, SR, first_frame, first_frame + len(rec), DATA)
        out.append((int(o), null_db(host, rec)))
    return out


def base(stimulus, first_frame, decay, size, osc_origin=None, start=None, stop=None):
    """Base (Macro 0) host output for a host stimulus whose frame 0 is absolute frame first_frame."""
    osc_origin = origin() if osc_origin is None else osc_origin
    return B.base_render(np.asarray(stimulus, float), int(first_frame), decay, size, osc_origin, SR, start, stop)


def to_internal(stimulus, first_frame):
    return B.to_internal(np.asarray(stimulus, float), SR, int(first_frame))


def response_to_internal(u, ufirst, decay, size, start, stop, osc_origin=None, equalise=True):
    """Host frames [start, stop) of the base's answer to an internal network-input signal u (n, 2), u[0] at index ufirst."""
    osc_origin = origin() if osc_origin is None else osc_origin
    values = values_for(decay, size)
    u = np.asarray(u, dtype=np.float64)
    driven = nm.equalise(DATA, u) if equalise else u
    taps = B.core(driven, ufirst + DELAY, osc_origin, values)
    return B.to_host(taps, ufirst, SR, start, stop, DATA)


def host_time_of_internal(m):
    return ((m + 1) * 160 - 30 * 147) / 147.0


def internal_time_of_host(h):
    return (h * 147 + 30 * 147) / 160.0 - 1
