"""The campaign's base network (network_model.py, converters.py) with the line oscillators counted from an origin.

    taps = core(driven, at_first, origin, values)      summed output taps for the signal at the line inputs
    host = base_render(stimulus, origin_frame, decay, size, osc_origin)

`origin` is the at-lines internal index (network-input index + 44) at which the 32 oscillators are at their start
phases; 0 is the campaign's own model (oscillators from the instance's first sample).
"""
from __future__ import annotations

import ctypes
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
# This file is the campaign's copy of work/abyss/analysis3/tempo/model/base.py under Analyzer/Results. It differs from
# it in where it finds the campaign (two folders up from abyss/model/) and in where it puts what it makes at run time:
# the compiled abyss_core.c and the table of oscillator states go under Analyzer/Results, which Git ignores, as the
# campaign's other compiled helpers do (network_model.BUILD). Nothing that is computed differs.
CAMPAIGN = HERE.parents[1]
if str(CAMPAIGN) not in sys.path:
    sys.path.insert(0, str(CAMPAIGN))
import converters as cv          # noqa: E402
import network_model as nm       # noqa: E402

SOURCE = HERE / "abyss_core.c"
BUILD = CAMPAIGN.parents[1] / "Results" / "RevOceanCharacterization" / "work" / "abyss" / "model_build"
LIBRARY = BUILD / "abyss_core.dylib"
INTERNAL_RATE = 44100
OSC_PERIOD = 13159175
OSC_SETTLE = 26000000
_library = None


def library():
    global _library
    if _library is None:
        BUILD.mkdir(parents=True, exist_ok=True)
        if not LIBRARY.exists() or LIBRARY.stat().st_mtime < SOURCE.stat().st_mtime:
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-ffp-contract=off", "-o", str(LIBRARY), str(SOURCE)], check=True)
        loaded = ctypes.CDLL(str(LIBRARY))
        real = np.ctypeslib.ndpointer(np.float64, flags="C_CONTIGUOUS")
        single = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")
        loaded.abyss_network.argtypes = [ctypes.c_long, ctypes.c_long, real, np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
                                         ctypes.c_float, single, ctypes.c_float, real, real, real, real, real, ctypes.c_int,
                                         real, ctypes.c_int, real]
        loaded.abyss_network.restype = None
        loaded.abyss_accumulators.argtypes = [ctypes.c_long, single, ctypes.c_float, ctypes.c_int, single]
        loaded.abyss_accumulators.restype = None
        loaded.abyss_comb.argtypes = [ctypes.c_long, ctypes.c_int, ctypes.c_long, ctypes.c_double, real]
        loaded.abyss_comb.restype = None
        loaded.abyss_reader.argtypes = [ctypes.c_long, ctypes.c_long, real, ctypes.c_long, ctypes.c_float, ctypes.c_float, ctypes.c_double,
                                        ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double, real]
        loaded.abyss_reader.restype = None
        loaded.abyss_phasor.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_long, ctypes.c_float, ctypes.c_float, single]
        loaded.abyss_phasor.restype = None
        loaded.abyss_walk.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_long, single, ctypes.c_float, single]
        loaded.abyss_walk.restype = None
        loaded.abyss_network_state.argtypes = [ctypes.c_long, single, real, np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
                                               ctypes.c_float, ctypes.c_float, real, real, real, real, real, ctypes.c_int,
                                               real, ctypes.c_int, real]
        loaded.abyss_network_state.restype = None
        _library = loaded
    return _library


def values_for(decay_seconds: float, size_percent: float, data: dict | None = None) -> dict:
    """Arrays of the network at display values of Decay (s) and Size (%)."""
    data = nm.constants() if data is None else data
    decay = nm.host_value(data["parameters"]["decay"], float(decay_seconds))
    size = nm.host_value(data["parameters"]["size"], float(size_percent))
    return nm.parameters(data, decay, size)


_STRIDE = 4096
_osc_table = None


def _table(values: dict) -> np.ndarray:
    """Accumulator states after 0, 4096, 8192, ... steps (one walk, kept beside the compiled library): the states
    abyss_network reaches by stepping from the start phases, so core() gives the same samples without the walk."""
    global _osc_table
    if _osc_table is None:
        cache = BUILD
        cache.mkdir(parents=True, exist_ok=True)
        path = cache / "osc_table.npy"
        if path.exists():
            _osc_table = np.load(path)
        else:
            entries = (OSC_SETTLE + OSC_PERIOD) // _STRIDE + 2
            table = np.zeros((entries, 32), np.float32)
            library().abyss_walk(0, _STRIDE, entries, np.ascontiguousarray(values["phase"], dtype=np.float32), values["increment"], table)
            np.save(path, table)
            _osc_table = table
    return _osc_table


def core(driven: np.ndarray, at_first: int, origin: int, values: dict) -> np.ndarray:
    """Summed output taps (frames, 2) in front of the output low-pass for `driven` (frames, 2), the signal at the
    line inputs; driven[0] is at-lines internal sample `at_first`."""
    driven = np.ascontiguousarray(driven, dtype=np.float64)
    mix = np.ascontiguousarray(values["tap"][None, :], dtype=np.float64)
    output = np.zeros((len(driven), 1, 2))
    packed = [np.ascontiguousarray(values[key], dtype=np.float64) for key in ("attenuation", "own", "cross", "matrix", "kernel")]
    count = int(at_first) - int(origin)
    if count >= 0:                       # fast path of analysis3/tempo: same accumulator states, read from a table
        if count > OSC_SETTLE + OSC_PERIOD:
            count = OSC_SETTLE + (count - OSC_SETTLE) % OSC_PERIOD
        table = _table(values)
        j = count // _STRIDE
        theta = np.zeros((1, 32), np.float32)
        library().abyss_walk(count - j * _STRIDE, 0, 1, np.ascontiguousarray(table[j]), values["increment"], theta)
        library().abyss_network_state(len(driven), np.ascontiguousarray(theta[0]), driven, np.ascontiguousarray(values["length"], dtype=np.int32),
                                      values["depth"], values["increment"], *packed, len(values["kernel"]), mix, 1, output)
        return output[:, 0, :]
    library().abyss_network(len(driven), int(at_first) - int(origin), driven, np.ascontiguousarray(values["length"], dtype=np.int32),
                            values["depth"], np.ascontiguousarray(values["phase"], dtype=np.float32), values["increment"],
                            *packed, len(values["kernel"]), mix, 1, output)
    return output[:, 0, :]


def check_oscillator_period(extra: int = 12345) -> bool:
    """True when the 32 accumulators are in the same state OSC_PERIOD steps apart behind OSC_SETTLE."""
    values = values_for(2.0, 100.0)
    phase = np.ascontiguousarray(values["phase"], dtype=np.float32)
    a, b = np.zeros(32, np.float32), np.zeros(32, np.float32)
    library().abyss_accumulators(OSC_SETTLE + extra, phase, values["increment"], 0, a)
    library().abyss_accumulators(OSC_SETTLE + extra + OSC_PERIOD, phase, values["increment"], 0, b)
    return bool(np.array_equal(a, b))


def to_internal(stimulus: np.ndarray, sample_rate: int, origin_frame: int) -> tuple:
    """(first, internal (n, 2)): the 44.1 kHz network input of a host stimulus whose frame 0 is absolute frame `origin_frame`."""
    stimulus = np.asarray(stimulus, dtype=np.float64)
    first, left = cv.to_internal(stimulus[:, 0], int(sample_rate), int(origin_frame))
    right = cv.to_internal(stimulus[:, 1], int(sample_rate), int(origin_frame))[1]
    return first, np.stack([left, right], axis=1)


def to_host(taps: np.ndarray, first: int, sample_rate: int, start: int, stop: int, data: dict | None = None) -> np.ndarray:
    """Host frames [start, stop) for summed taps whose network input started at internal sample `first`."""
    return nm.network_output(taps, first, int(sample_rate), int(start), int(stop), data)


def base_render(stimulus, origin_frame: int, decay_seconds: float, size_percent: float, osc_origin: int = 0,
                sample_rate: int = 48000, start: int | None = None, stop: int | None = None) -> np.ndarray:
    data = nm.constants()
    values = values_for(decay_seconds, size_percent, data)
    first, internal = to_internal(stimulus, sample_rate, origin_frame)
    driven = nm.equalise(data, internal)
    taps = core(driven, first + data["input"]["delay_samples"], osc_origin, values)
    start = origin_frame if start is None else start
    stop = origin_frame + len(stimulus) if stop is None else stop
    return to_host(taps, first, sample_rate, start, stop, data)


def null_db(candidate, reference) -> float:
    c = np.asarray(candidate, dtype=np.float64)
    r = np.asarray(reference, dtype=np.float64)
    n = min(len(c), len(r))
    return float(10 * np.log10((np.sum((c[:n] - r[:n]) ** 2) + 1e-300) / (np.sum(r[:n] ** 2) + 1e-300)))
