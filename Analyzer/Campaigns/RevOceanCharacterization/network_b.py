#!/usr/bin/env python3
"""Whole-response model of the Rev OCEAN network at Macro 0 (packet network_b).

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> array (frames, 2)

is the reference's raw output (Tide mode, Macro 0, Mix 100 %, neutral baseline)
for a stereo stimulus that starts `warmup_seconds` after the first processed
sample, at a 48 kHz or 44.1 kHz host. Every pass of the recirculation is
rendered; nothing is truncated. Evidence: findings/network_b.md.

Signal path (the network runs at 44.1 kHz at every host rate):

    host in -> converter -> 44 samples -> equaliser -> [ 2 x 16 lines, Hadamard loop ] -> low-pass -> converter -> host out

* Converters: the Kaiser-windowed sinc table of packet first_order (17 zero
  crossings, cut-off 0.9, beta 6, 4096 entries per sample, no interpolation)
  on its lattice delays, with one addition: in the output converter the wing
  that takes the lower entry at exact distances stops one entry early. A
  44.1 kHz host has no converter, only 44 samples in front of the lines and 44
  behind them.
* Every filter is a second-order section of the audio-EQ cookbook (R. Bristow-
  Johnson's formulas) at the internal rate.
* Two peaking sections act on the input before it is written into the lines:
  200 Hz, +0.5 dB, Q 0.4 and 1750 Hz, -0.5 dB, Q 0.4. They are not in the loop
  and not behind the lines.
* Line n of group g (group = output channel) receives
      B_n ((1 + w_n)/2 own input + (1 - w_n)/2 other input) + fed-back signal
  with the width weights w of packet first_order and an input gain B_n that
  follows one S-curve over the 16 lines.
* A line is `round(prime * Size)` samples long plus 0.88 ms times the sine of
  its single-precision phase accumulator, read with linear interpolation at
  the length of the sample being read (packets first_order and laws). The
  length is computed in single precision: a sine of period fl32(2 pi), its
  product with the depth 38.808002 and the sum are each rounded.
* The read is multiplied by 10^(-3 prime Size / (44100 Decay)). That signal is
  the output tap of the line and its contribution to the feedback.
* Feedback: the 16 attenuated reads of a group are mixed by a Hadamard matrix
  in natural (Sylvester) order scaled by 1/4 whose rows are taken in reversed
  order (row r feeds line 16 - r, counting rows from 0), then pass one fixed
  loop kernel, the same for every line and both groups: a low shelf (1221 Hz,
  -0.28 dB, Q 0.484) and a high shelf (12840 Hz, -0.12 dB, Q 0.26). Nothing
  passes between the groups.
* Output of a group: sum of tap_n(Decay) times the attenuated reads, then one
  low-pass (20 kHz, Q 1). The tap weights lie on two S-curves over the lines
  (1 to 9 and 9 to 16) whose ends move with Decay up to 6 s.

Constants: tide_structural_data/network_b.json, written by fit_network_b.py.
Apart from the constants of packet first_order (line lengths, width weights,
oscillators, converter) they are round values and one fitted number, the input
gain of line 1. The module renders no capture. The core loop is network_b.c, compiled on first
use into Analyzer/Results/RevOceanCharacterization/work/network_b/.

    python score_network.py network_b.py
"""
from __future__ import annotations

import ctypes
import json
import subprocess
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

HERE = Path(__file__).resolve().parent
DATA = HERE / "tide_structural_data" / "network_b.json"
SOURCE = HERE / "network_b.c"
BUILD = HERE.parents[1] / "Results" / "RevOceanCharacterization" / "work" / "network_b"
INTERNAL_RATE = 44100
LATTICE = 160            # host (48 kHz) and internal samples meet on 1/160 internal sample
HOST_STEP = 147          # one 48 kHz sample on that lattice
LINES = 16

_constants: dict | None = None
_core = None


def constants() -> dict:
    global _constants
    if _constants is None:
        _constants = json.loads(DATA.read_text())
    return _constants


def core():
    """The compiled network_b.c."""
    global _core
    if _core is None:
        BUILD.mkdir(parents=True, exist_ok=True)
        library = BUILD / "network_b.dylib"
        if not library.exists() or library.stat().st_mtime < SOURCE.stat().st_mtime:
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-ffp-contract=off", "-o", str(library), str(SOURCE)], check=True)
        _core = ctypes.CDLL(str(library)).network_core
        real = np.ctypeslib.ndpointer(np.float64, flags="C_CONTIGUOUS")
        _core.argtypes = [ctypes.c_long, ctypes.c_long, real, np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
                          ctypes.c_float, np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS"), ctypes.c_float,
                          real, real, real, real, real, real, real, ctypes.c_void_p]
        _core.restype = None
    return _core


# ---------------------------------------------------------------- filters

def section(entry: dict) -> tuple:
    """(b0, b1, b2), (a1, a2) of one audio-EQ-cookbook section at the internal rate, a0 = 1."""
    omega = 2.0 * np.pi * entry["frequency_hz"] / INTERNAL_RATE
    cosine, alpha = np.cos(omega), np.sin(omega) / (2.0 * entry["q"])
    amplitude = 10.0 ** (entry.get("gain_db", 0.0) / 40.0)
    if entry["kind"] == "peaking":
        numerator = [1.0 + alpha * amplitude, -2.0 * cosine, 1.0 - alpha * amplitude]
        denominator = [1.0 + alpha / amplitude, -2.0 * cosine, 1.0 - alpha / amplitude]
    elif entry["kind"] == "low_pass":
        numerator = [(1.0 - cosine) / 2.0, 1.0 - cosine, (1.0 - cosine) / 2.0]
        denominator = [1.0 + alpha, -2.0 * cosine, 1.0 - alpha]
    else:
        sign = 1.0 if entry["kind"] == "low_shelf" else -1.0             # "high_shelf" mirrors the low shelf
        root = 2.0 * np.sqrt(amplitude) * alpha
        plus, minus = amplitude + 1.0, amplitude - 1.0
        numerator = [amplitude * (plus - sign * minus * cosine + root), sign * 2.0 * amplitude * (minus - sign * plus * cosine),
                     amplitude * (plus - sign * minus * cosine - root)]
        denominator = [plus + sign * minus * cosine + root, -sign * 2.0 * (minus + sign * plus * cosine),
                       plus + sign * minus * cosine - root]
    return np.array(numerator) / denominator[0], np.array(denominator[1:]) / denominator[0]


def apply_section(entry: dict, signal: np.ndarray) -> np.ndarray:
    numerator, denominator = section(entry)
    return lfilter(numerator, np.concatenate([[1.0], denominator]), signal, axis=0)


def input_equaliser(data: dict, signal: np.ndarray) -> np.ndarray:
    for entry in data["input"]["equaliser"]:
        signal = apply_section(entry, signal)
    return signal


def output_low_pass(data: dict, signal: np.ndarray) -> np.ndarray:
    return apply_section(data["output"]["low_pass"], signal)


# ---------------------------------------------------------------- laws

def s_curve(position: np.ndarray, shape: float) -> np.ndarray:
    """Odd curve on [-1, 1] through (-1, -1), (0, 0), (1, 1): the vendor's Exp(shape) map on each half."""
    return np.sign(position) * np.expm1(shape * np.abs(position)) / np.expm1(shape)


def exp_map(position: float, shape: float) -> float:
    return float(np.expm1(shape * position) / np.expm1(shape))


def width_weights(lines: dict) -> np.ndarray:
    """w = 0.272 on lines 1 to 6, falling from 1 to 0.272 over lines 7 to 16 (packet first_order)."""
    flat, count = lines["width_flat"], lines["width_flat_lines"]
    ramp = 1.0 - np.arange(LINES - count) * (1.0 - flat) / (LINES - count - 1)
    return np.concatenate([np.full(count, flat), ramp])


def input_gains(data: dict) -> tuple:
    """(own, cross) input gain of each line: B_n (1 +- w_n) / 2 with B on one S-curve over the 16 lines."""
    law = data["input"]["gain"]
    line = np.arange(1, LINES + 1)
    first, last = law["first"], law["first"] * law["last_over_first"]
    level = (first + last) / 2.0 - (first - last) / 2.0 * s_curve((line - 8.5) / 7.5, law["shape"])
    width = width_weights(data["lines"])
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
    """[line written][line read]: Sylvester Hadamard matrix, rows reversed, times the scale (1/4)."""
    index = np.arange(LINES)
    bits = index[:, None] & index[None, :]
    hadamard = np.where(np.array([[bin(value).count("1") for value in row] for row in bits]) % 2 == 0, 1.0, -1.0)
    return data["feedback"]["scale"] * hadamard[::-1]


def parameters(data: dict, decay_seconds: float, size_percent: float) -> dict:
    """Every array the core needs at one Decay and Size."""
    prime = np.array(data["lines"]["prime"])
    own, cross = input_gains(data)
    return {
        "length": np.floor(prime * (size_percent / 100.0) + 0.5).astype(np.int32),
        # 60 dB per Decay seconds of the unrounded length (packet laws)
        "attenuation": 10.0 ** (-3.0 * prime * (size_percent / 100.0) / (INTERNAL_RATE * decay_seconds)),
        "own": np.tile(own, (2, 1)),
        "cross": np.tile(cross, (2, 1)),
        "tap": np.tile(tap_weights(data, decay_seconds), (2, 1)),
        "matrix": np.tile(feedback_matrix(data), (2, 1, 1)),
        "kernel": np.array([np.concatenate(section(entry)) for entry in data["feedback"]["kernel"]]),
    }


# ---------------------------------------------------------------- the 44.1 kHz network

def modulation_depth(oscillator: dict) -> np.float32:
    """0.88 ms in internal samples as the reference's single-precision product gives it: 38.808002."""
    return np.float32(np.float32(np.float32(oscillator["depth_ms"]) * np.float32(0.001)) * np.float32(INTERNAL_RATE))


def run_core(data: dict, at_lines: np.ndarray, first: int, values: dict, want_reads: bool = False):
    """Sum of the taps of each group (and the 32 attenuated reads) for the signal at the line inputs.

    Index k of every array is absolute internal sample first + k, counted from
    the first processed sample: the oscillators start there.
    """
    oscillator = data["oscillator"]
    phase = (np.float32(oscillator["phase_step_rad"]) * np.arange(2 * LINES, dtype=np.float32)).astype(np.float32)
    increment = np.float32(2.0 * np.pi * oscillator["rate_hz"] / INTERNAL_RATE)
    at_lines = np.ascontiguousarray(at_lines, dtype=np.float64)
    output = np.zeros((len(at_lines), 2))
    reads = np.zeros((len(at_lines), 2 * LINES)) if want_reads else None
    packed = [np.ascontiguousarray(values[key], dtype=np.float64) for key in ("attenuation", "own", "cross", "tap", "matrix", "kernel")]
    core()(len(at_lines), int(first), at_lines, np.ascontiguousarray(values["length"], dtype=np.int32),
           modulation_depth(oscillator), phase, increment, *packed, output,
           None if reads is None else reads.ctypes.data)
    return (output, reads) if want_reads else output


def network(data: dict, written: np.ndarray, first: int, decay_seconds: float, size_percent: float) -> np.ndarray:
    """Group outputs behind the low-pass for the signal `written` in front of the equaliser; indices as in run_core."""
    values = parameters(data, decay_seconds, size_percent)
    return output_low_pass(data, run_core(data, input_equaliser(data, np.asarray(written, dtype=np.float64)), first, values))


# ---------------------------------------------------------------- converters of a 48 kHz host

def converter_table(converter: dict) -> np.ndarray:
    """One wing of the converter kernel as single-precision entries (packet first_order, section 4.1)."""
    entries, crossings = converter["table_entries_per_sample"], converter["zero_crossings"]
    index = np.arange(crossings * entries)
    window = np.i0(converter["kaiser_beta"] * np.sqrt(1.0 - (index / (len(index) - 1)) ** 2)) / np.i0(converter["kaiser_beta"])
    return (converter["cutoff"] * np.sinc(converter["cutoff"] * index / entries) * window).astype(np.float32).astype(np.float64)


def table_lookup(table: np.ndarray, numerator: np.ndarray, entries: int, low_wing: int, low_wing_stops_early: bool = False) -> np.ndarray:
    """Kernel at numerator / 160 internal samples: the entry at or below the distance, and the entry
    below an exact one on the wing `low_wing` (packet first_order). With `low_wing_stops_early` that
    wing does not use the last entry of the table, which only an exact distance of 17 samples reaches."""
    scaled = np.abs(numerator) * entries
    lowered = (scaled % LATTICE == 0) & (np.sign(numerator) == low_wing)
    index = scaled // LATTICE - lowered
    usable = index < len(table) - (lowered & low_wing_stops_early)
    return np.where(usable, table[np.minimum(index, len(table) - 1)], 0.0)


def polyphase(signal: np.ndarray, position: np.ndarray, step: int, reach: int, kernel_of) -> np.ndarray:
    """sum_j signal[position // step + j] * K(position % step - step * j) for every lattice position."""
    centre, phase = position // step, position % step
    offsets = np.arange(-reach, reach + 1)
    bank = kernel_of(np.arange(step)[:, None] - step * offsets[None, :])        # [phase, offset]
    padded = np.concatenate([np.zeros((reach, signal.shape[1])), signal, np.zeros((reach + 1, signal.shape[1]))])
    result = np.zeros((len(position), signal.shape[1]))
    for column, offset in enumerate(offsets):
        index = centre + offset + reach
        valid = (index >= 0) & (index < len(padded))
        result[valid] += bank[phase[valid], column][:, None] * padded[index[valid]]
    return result


def render_48k(data: dict, stimulus: np.ndarray, warmup_frames: int, decay_seconds: float, size_percent: float) -> np.ndarray:
    """Lattice positions are whole numbers of 1/160 internal sample: host sample h sits at 147 h, internal
    sample m at 160 m. Host input h is centred at 147 h + input_offset; host output h reads 147 h - output_offset."""
    converter = data["converter"]
    table, entries = converter_table(converter), converter["table_entries_per_sample"]
    span = converter["zero_crossings"] * LATTICE
    input_offset = int(round(converter["input_delay_internal_samples"] * LATTICE))
    output_offset = int(round(converter["output_delay_host_samples"] * HOST_STEP))
    frames = len(stimulus)
    start = (HOST_STEP * warmup_frames + input_offset - span) // LATTICE         # first internal sample the stimulus reaches
    stop = (HOST_STEP * (warmup_frames + frames) + input_offset + span) // LATTICE + 1
    internal = np.arange(start, stop)
    converted = polyphase(stimulus, LATTICE * internal - input_offset - HOST_STEP * warmup_frames, HOST_STEP,
                          span // HOST_STEP + 2,
                          lambda q: table_lookup(table, q, entries, converter["exact_entry_low_wing"]["input"])) \
        * (HOST_STEP / LATTICE)
    written_at = start + data["input"]["delay_samples"]
    behind = network(data, converted, written_at, decay_seconds, size_percent)   # behind[k] is internal sample written_at + k
    host = np.arange(warmup_frames, warmup_frames + frames)
    return polyphase(behind, HOST_STEP * host - output_offset - LATTICE * written_at, LATTICE, span // LATTICE + 2,
                     lambda q: table_lookup(table, q, entries, converter["exact_entry_low_wing"]["output"],
                                            converter["output_low_wing_stops_early"]))


def render_44k1(data: dict, stimulus: np.ndarray, warmup_frames: int, decay_seconds: float, size_percent: float) -> np.ndarray:
    before, after = data["input"]["delay_samples"], data["output"]["delay_samples"]
    behind = network(data, stimulus, warmup_frames + before, decay_seconds, size_percent)
    return np.concatenate([np.zeros((before + after, 2)), behind[:len(stimulus) - before - after]])


def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, data: dict | None = None) -> np.ndarray:
    data = constants() if data is None else data
    stimulus = np.asarray(stimulus, dtype=np.float64)
    warmup_frames = int(round(warmup_seconds * sample_rate))
    if sample_rate == INTERNAL_RATE:
        return render_44k1(data, stimulus, warmup_frames, float(decay_seconds), float(size_percent))
    if sample_rate == 48000:
        return render_48k(data, stimulus, warmup_frames, float(decay_seconds), float(size_percent))
    raise ValueError("the model covers host rates of 48 kHz and 44.1 kHz")
