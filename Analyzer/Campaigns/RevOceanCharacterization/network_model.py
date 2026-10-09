#!/usr/bin/env python3
"""The campaign's model of the Rev OCEAN network (Tide mode, Macro 0, Mix 100 %, neutral baseline).

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> array (frames, 2)

is the reference's raw output (reported latency still in it) for a stereo
stimulus that starts `warmup_seconds` after the first processed sample. Every
pass of the recirculation is simulated; nothing is stored or truncated. The
model merges packets network_a and network_b; evidence: findings/network.md.

For the Tide layer, which treats groups of lines separately, the same network
is available in pieces at the internal rate of 44.1 kHz:

    first, internal = network_input(stimulus, sample_rate, warmup_seconds)      input converter
    taps = line_outputs(internal, first, decay_seconds, size_percent, groups)   (frames, 2, groups), in front of the output filter
    host = network_output(taps.sum(axis=2), first, sample_rate, start, stop)    output low-pass and converter

`taps[k]` belongs to internal sample `first + 44 + k` (the lines are written 44
samples behind the network input). With `groups=None` every line is its own
group; `weighted=False` gives the tap signals without the output tap weights.

Signal path (the network runs at 44.1 kHz at every host rate; m counts internal
samples from the first sample the plug-in processes, warm-up included):

    host in -> converter -> equaliser -> 44 samples -> [ 2 x 16 lines, Hadamard loop ] -> low-pass -> converter -> host out

* Converters: converters.py (packet converters). A 44.1 kHz host has none.
* Equaliser, in front of the lines only: two peaking sections (200 Hz, +0.5 dB,
  Q 0.4; 1750 Hz, -0.5 dB, Q 0.4). Every filter is a second-order section of the
  audio-EQ cookbook (R. Bristow-Johnson's formulas) at 44.1 kHz.
* Two independent groups of 16 lines, one per output channel. Line n receives
      B_n ((1 + w_n)/2 own input + (1 - w_n)/2 other input) + fed-back signal.
  B follows one S-curve over the lines, w is 0.272 on lines 1 to 6 and falls
  from 1 to 0.272 over lines 7 to 16.
* A line is floorf(fl(prime * Size) + 0.5f) samples long, rounded in single
  precision, plus 0.88 ms times the sine of a single-precision phase accumulator
  (0.6 Hz, start 11.25 rad times its index), read with linear interpolation at
  the length of the sample being read.
* The read is multiplied by 10^(-3 prime Size / (44100 Decay)). That signal is
  the output tap of the line and its contribution to the feedback.
* Feedback: the 16 attenuated reads of a group are mixed by the Sylvester
  Hadamard matrix of order 16 times 1/4 with its rows in reversed order (row r
  feeds line 16 - r), pass the loop kernel (low shelf 1221 Hz, -0.28 dB,
  Q 0.484; high shelf 12840 Hz, -0.12 dB, Q 0.26) and are written with the
  input, in the same sample. Nothing passes between the groups.
* Output of a group: sum of tap_n(Decay) times the attenuated reads, then a
  low-pass (20 kHz, Q 1). The tap weights lie on two S-curves over the lines
  whose ends move with Decay up to 6 s.

Decay and Size reach the plug-in as single-precision normalised parameters and
it evaluates their display laws in single precision; `host_value` gives the
value it then works with. Size is a factor from 0.3 to 2 that the plug-in forms
with a reciprocal and one fused multiply-add (`size_scale`; evidence:
findings/network_verification.md, section 3); `host_value` returns 100 times
that factor. `render` takes display values and applies it; `line_outputs`
takes the values themselves.

Constants: tide_structural_data/network.json, written by fit_network.py. The
module renders no capture. The core loop is network_model.c, compiled on first
use into Analyzer/Results/RevOceanCharacterization/work/network/.

    python score_network.py network_model.py
"""
from __future__ import annotations

import ctypes
import json
import math
import subprocess
from fractions import Fraction
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

import converters

HERE = Path(__file__).resolve().parent
DATA = HERE / "tide_structural_data" / "network.json"
SOURCE = HERE / "network_model.c"
BUILD = HERE.parents[1] / "Results" / "RevOceanCharacterization" / "work" / "network"
INTERNAL_RATE = 44100
LINES = 16

_constants: dict | None = None
_library = None


def constants() -> dict:
    global _constants
    if _constants is None:
        _constants = json.loads(DATA.read_text())
    return _constants


def library():
    """The compiled network_model.c; built when the source is newer than the library."""
    global _library
    if _library is None:
        BUILD.mkdir(parents=True, exist_ok=True)
        path = BUILD / "network_model.dylib"
        if not path.exists() or path.stat().st_mtime < SOURCE.stat().st_mtime:
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-ffp-contract=off", "-o", str(path), str(SOURCE)], check=True)
        loaded = ctypes.CDLL(str(path))
        real = np.ctypeslib.ndpointer(np.float64, flags="C_CONTIGUOUS")
        single = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")
        loaded.network_core.argtypes = [ctypes.c_long, ctypes.c_long, real, np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
                                        ctypes.c_float, single, ctypes.c_float, real, real, real, real, real, ctypes.c_int,
                                        real, ctypes.c_int, ctypes.c_void_p, real]
        loaded.network_core.restype = None
        loaded.oscillator_phases.argtypes = [ctypes.c_long, ctypes.c_long, single, ctypes.c_float, single]
        loaded.oscillator_phases.restype = None
        _library = loaded
    return _library


# ---------------------------------------------------------------- filters

def section(entry: dict) -> tuple:
    """(b0, b1, b2), (a1, a2) of one audio-EQ-cookbook section at the internal rate, a0 = 1."""
    omega = 2.0 * math.pi * entry["frequency_hz"] / INTERNAL_RATE
    cosine, alpha = math.cos(omega), math.sin(omega) / (2.0 * entry["q"])
    amplitude = 10.0 ** (entry.get("gain_db", 0.0) / 40.0)
    if entry["kind"] == "peaking":
        numerator = [1.0 + alpha * amplitude, -2.0 * cosine, 1.0 - alpha * amplitude]
        denominator = [1.0 + alpha / amplitude, -2.0 * cosine, 1.0 - alpha / amplitude]
    elif entry["kind"] == "low_pass":
        numerator = [(1.0 - cosine) / 2.0, 1.0 - cosine, (1.0 - cosine) / 2.0]
        denominator = [1.0 + alpha, -2.0 * cosine, 1.0 - alpha]
    else:
        sign = 1.0 if entry["kind"] == "low_shelf" else -1.0             # "high_shelf" mirrors the low shelf
        root = 2.0 * math.sqrt(amplitude) * alpha
        plus, minus = amplitude + 1.0, amplitude - 1.0
        numerator = [amplitude * (plus - sign * minus * cosine + root), sign * 2.0 * amplitude * (minus - sign * plus * cosine),
                     amplitude * (plus - sign * minus * cosine - root)]
        denominator = [plus + sign * minus * cosine + root, -sign * 2.0 * (minus + sign * plus * cosine),
                       plus + sign * minus * cosine - root]
    return np.array(numerator) / denominator[0], np.array(denominator[1:]) / denominator[0]


def apply_sections(entries, signal: np.ndarray) -> np.ndarray:
    for entry in entries:
        numerator, denominator = section(entry)
        signal = lfilter(numerator, np.concatenate([[1.0], denominator]), signal, axis=0)
    return signal


def equalise(data: dict, signal: np.ndarray) -> np.ndarray:
    """The input equaliser: what is written into the lines `input.delay_samples` (44) samples later."""
    return apply_sections(data["input"]["equaliser"], signal)


def output_filter(data: dict, signal: np.ndarray) -> np.ndarray:
    """The fixed low-pass behind the summed output taps of a group."""
    return apply_sections([data["output"]["low_pass"]], signal)


# ---------------------------------------------------------------- laws

def plugin_value(parameter: dict, normalised: float) -> float:
    """The value the plug-in works with at a normalised host parameter: its declared Exp(shape) display
    law, low + (high - low)(e^(shape x) - 1)/(e^shape - 1), evaluated in single precision."""
    low, high, shape = (np.float32(parameter[key]) for key in ("low", "high", "shape"))
    one = np.float32(1.0)
    return float(low + (high - low) * ((np.exp(shape * np.float32(normalised)) - one) / (np.exp(shape) - one)))


def fused_multiply_add(a: np.float32, b: np.float32, c: np.float32) -> np.float32:
    """a * b + c of single-precision numbers with one rounding (nearest, ties to even), as an fma instruction gives it."""
    exact = Fraction(float(a)) * Fraction(float(b)) + Fraction(float(c))
    guess = np.float32(float(exact))
    neighbours = (np.nextafter(guess, np.float32(-np.inf)), guess, np.nextafter(guess, np.float32(np.inf)))
    return min(neighbours, key=lambda value: (abs(Fraction(float(value)) - exact), int(value.view(np.uint32)) & 1))


def size_scale(parameter: dict, normalised: float) -> np.float32:
    """Size as the factor the plug-in multiplies the line lengths by (Size / 100) at a normalised host value:

        r = fl(fl(expf(fl(shape x)) - 1) * fl(1 / fl(expf(shape) - 1)))      a reciprocal, not a division
        s = fma(1.7f, r, 0.3f)                                               one rounding of 0.3 + 1.7 r

    The display law in percent, however it is rounded, gives another whole length for one or two lines at
    one normalised value in 200 (display values 80.1, 162.1 and 180.1 % among them).
    """
    shape, one = np.float32(parameter["shape"]), np.float32(1.0)
    ratio = (np.exp(shape * np.float32(normalised)) - one) * (one / (np.exp(shape) - one))
    return fused_multiply_add(np.float32((parameter["high"] - parameter["low"]) / 100.0), ratio, np.float32(parameter["low"] / 100.0))


def host_value(parameter: dict, display: float) -> float:
    """A display value as the plug-in works with it: normalised by the host in single precision, then
    `plugin_value`. Size comes back as 100 times its factor `size_scale`, which is exact in double precision."""
    low, high, shape = parameter["low"], parameter["high"], parameter["shape"]
    normalised = math.log1p((display - low) / (high - low) * math.expm1(shape)) / shape
    if parameter == constants()["parameters"]["size"]:
        return 100.0 * float(size_scale(parameter, normalised))
    return plugin_value(parameter, normalised)


def s_curve(position: np.ndarray, shape: float) -> np.ndarray:
    """Odd curve on [-1, 1] through (-1, -1), (0, 0), (1, 1): the vendor's Exp(shape) map on each half."""
    return np.sign(position) * np.expm1(shape * np.abs(position)) / math.expm1(shape)


def exp_map(position: float, shape: float) -> float:
    return math.expm1(shape * position) / math.expm1(shape)


def width_weights(data: dict) -> np.ndarray:
    """w = 0.272 on lines 1 to 6, falling linearly from 1 to 0.272 over lines 7 to 16."""
    flat, count = data["lines"]["width_flat"], data["lines"]["width_flat_lines"]
    ramp = 1.0 - np.arange(LINES - count) * (1.0 - flat) / (LINES - count - 1)
    return np.concatenate([np.full(count, flat), ramp])


def input_curve(data: dict) -> np.ndarray:
    """B_n / B_1: one S-curve over the 16 lines from 1 to `last_over_first`."""
    law = data["input"]["gain"]
    line = np.arange(1, LINES + 1)
    return (1.0 + law["last_over_first"]) / 2.0 - (1.0 - law["last_over_first"]) / 2.0 * s_curve((line - 8.5) / 7.5, law["shape"])


def input_gains(data: dict) -> tuple:
    """(own, cross) input gain of each line: B_n (1 +- w_n) / 2."""
    level = data["input"]["gain"]["first"] * input_curve(data)
    width = width_weights(data)
    return level * (1.0 + width) / 2.0, level * (1.0 - width) / 2.0


def tap_weights(data: dict, decay_seconds: float) -> np.ndarray:
    """Output tap weight of each line; line 9 is 1.

    Lines 1 to 9 lie on an S-curve between the weight of line 1 and 1, lines 9 to
    16 on another between 1 and the weight of line 16. The end weights move with
    Decay from 0.5 s to 6 s (line 1 linearly, line 16 on an Exp(-2.6) map) and
    stay put above 6 s.
    """
    law = data["output"]["tap"]
    position = (min(decay_seconds, law["decay_knee_seconds"]) - law["decay_low_seconds"]) \
        / (law["decay_knee_seconds"] - law["decay_low_seconds"])
    first = law["first"][0] + (law["first"][1] - law["first"][0]) * position
    last = law["last"][0] + (law["last"][1] - law["last"][0]) * exp_map(position, law["last_decay_shape"])
    line = np.arange(1, LINES + 1)
    early = (first + 1.0) / 2.0 + (1.0 - first) / 2.0 * s_curve((line - 5.0) / 4.0, law["early_shape"])
    late = (1.0 + last) / 2.0 + (last - 1.0) / 2.0 * s_curve((line - 12.5) / 3.5, law["late_shape"])
    return np.where(line <= 9, early, late)


def feedback_matrix(data: dict) -> np.ndarray:
    """[line written][line read]: Sylvester Hadamard matrix of order 16, rows reversed, times the scale (1/4)."""
    index = np.arange(LINES)
    parity = np.array([[bin(row & column).count("1") % 2 for column in index] for row in index])
    return data["feedback"]["scale"] * np.where(parity == 0, 1.0, -1.0)[::-1]


def modulation_depth(data: dict) -> np.float32:
    """0.88 ms in internal samples as a single-precision product: 0.88f * 0.001f * 44100f = 38.808002."""
    return np.float32(np.float32(np.float32(data["oscillator"]["depth_ms"]) * np.float32(0.001)) * np.float32(INTERNAL_RATE))


def parameters(data: dict, decay_seconds: float, size_percent: float) -> dict:
    """Every array the core needs at one Decay (seconds) and Size (percent), both as the plug-in works with them."""
    prime = np.array(data["lines"]["prime"], dtype=np.float64)
    scale = np.float32(size_percent / 100.0)
    own, cross = input_gains(data)
    oscillator = data["oscillator"]
    return {
        # product, sum and floor in single precision
        "length": np.floor(prime.astype(np.float32) * scale + np.float32(0.5)).astype(np.int32),
        # 60 dB per Decay seconds of the unrounded length
        "attenuation": 10.0 ** (-3.0 * prime * float(scale) / (INTERNAL_RATE * decay_seconds)),
        "own": own,
        "cross": cross,
        "tap": tap_weights(data, decay_seconds),
        "matrix": feedback_matrix(data),
        "kernel": np.array([np.concatenate(section(entry)) for entry in data["feedback"]["kernel"]]),
        "depth": modulation_depth(data),
        "phase": (np.float32(oscillator["phase_step_rad"]) * np.arange(2 * LINES, dtype=np.float32)).astype(np.float32),
        "increment": np.float32(2.0 * math.pi * oscillator["rate_hz"] / INTERNAL_RATE),
    }


# ---------------------------------------------------------------- the 44.1 kHz network

def oscillator_phases(values: dict, first: int, frames: int) -> np.ndarray:
    """Single-precision phase [frame][group][line] every line uses at internal samples first .. first + frames - 1."""
    theta = np.zeros((frames, 2, LINES), np.float32)
    library().oscillator_phases(frames, int(first), values["phase"], values["increment"], theta)
    return theta


def run_core(values: dict, at_lines: np.ndarray, first: int, mix: np.ndarray, lengths: np.ndarray | None = None) -> np.ndarray:
    """Outputs [frame][row of mix][group] for the signal `at_lines` written into the lines: row r is the
    sum over the lines of mix[r][line] times the attenuated read. Index k of every array is absolute
    internal sample first + k. `lengths` [frame][group][line] replaces the line lengths the core computes."""
    at_lines = np.ascontiguousarray(at_lines, dtype=np.float64)
    mix = np.ascontiguousarray(np.atleast_2d(mix), dtype=np.float64)
    output = np.zeros((len(at_lines), len(mix), 2))
    lengths = None if lengths is None else np.ascontiguousarray(lengths, dtype=np.float32)
    packed = [np.ascontiguousarray(values[key], dtype=np.float64) for key in ("attenuation", "own", "cross", "matrix", "kernel")]
    library().network_core(len(at_lines), int(first), at_lines, np.ascontiguousarray(values["length"], dtype=np.int32),
                           values["depth"], values["phase"], values["increment"], *packed, len(values["kernel"]),
                           mix, len(mix), None if lengths is None else lengths.ctypes.data, output)
    return output


def line_outputs(internal: np.ndarray, first: int, decay_seconds: float, size_percent: float, groups=None,
                 weighted: bool = True, data: dict | None = None) -> np.ndarray:
    """Output taps of groups of lines in front of the output low-pass: array (frames, 2, groups).

    `internal` is the stereo network input at 44.1 kHz with internal[0] at internal sample `first`;
    result[k] belongs to internal sample first + 44 + k (`input.delay_samples`). `groups` is a list of lists of line
    numbers (1 to 16); None gives the 16 lines one by one. Each group is the sum of its lines' attenuated
    reads times their tap weights (`weighted=False`: the plain sum). Summed over a partition of the lines
    and passed through `network_output`, the weighted result is the model's output. Decay and Size are the
    values the plug-in works with (see `host_value`).
    """
    data = constants() if data is None else data
    values = parameters(data, float(decay_seconds), float(size_percent))
    groups = [[line] for line in range(1, LINES + 1)] if groups is None else groups
    mix = np.zeros((len(groups), LINES))
    for row, members in enumerate(groups):
        index = np.asarray(members, dtype=int) - 1
        mix[row, index] = values["tap"][index] if weighted else 1.0
    driven = equalise(data, np.asarray(internal, dtype=np.float64))
    return np.ascontiguousarray(run_core(values, driven, first + data["input"]["delay_samples"], mix).transpose(0, 2, 1))


def network_input(stimulus: np.ndarray, sample_rate: int, warmup_seconds: float) -> tuple:
    """(first, internal): the stereo 44.1 kHz network input of a host stimulus that starts after the warm-up."""
    origin = int(round(warmup_seconds * sample_rate))
    stimulus = np.asarray(stimulus, dtype=np.float64)
    first, left = converters.to_internal(stimulus[:, 0], int(sample_rate), origin)
    return first, np.stack([left, converters.to_internal(stimulus[:, 1], int(sample_rate), origin)[1]], axis=1)


def network_output(taps: np.ndarray, first: int, sample_rate: int, start: int, stop: int, data: dict | None = None) -> np.ndarray:
    """Host samples [start, stop) for summed output taps (frames, 2) as `line_outputs` returns them for
    a network input whose first internal sample is `first`: output low-pass, then the output converter."""
    data = constants() if data is None else data
    behind = output_filter(data, np.asarray(taps, dtype=np.float64))
    written_at = first + data["input"]["delay_samples"]
    return np.stack([converters.to_host(behind[:, channel], int(sample_rate), written_at, start, stop) for channel in (0, 1)], axis=1)


def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, data: dict | None = None) -> np.ndarray:
    data = constants() if data is None else data
    decay = host_value(data["parameters"]["decay"], float(decay_seconds))
    size = host_value(data["parameters"]["size"], float(size_percent))
    first, internal = network_input(stimulus, sample_rate, warmup_seconds)
    taps = line_outputs(internal, first, decay, size, groups=[list(range(1, LINES + 1))], data=data)[:, :, 0]
    origin = int(round(warmup_seconds * sample_rate))
    return network_output(taps, first, sample_rate, origin, origin + len(stimulus), data)
