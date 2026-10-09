#!/usr/bin/env python3
"""Whole-response model of the Rev OCEAN network (Tide mode, Macro 0, Mix 100 %): packet network_a.

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> array (frames, 2)

gives the reference's raw output (reported latency still in it) at the neutral
baseline for a stereo stimulus that starts `warmup_seconds` of silence after
the first processed sample, at a host rate of 44.1 or 48 kHz. The recursion is
simulated sample by sample at the internal rate of 44.1 kHz by network_a.c,
which this module compiles on first use into the packet's scratch folder.
It renders no captures.

Signal path (findings/network_a.md has the evidence for every element):

    host in -> [converter] -> input filter -> 44 samples -> 16 + 16 lines <-> feedback -> low-pass -> [converter] -> host out

* The two converters exist only at a 48 kHz host and are those of
  first_order_model.py (Kaiser sinc table, lattice delays). At 44.1 kHz the
  low-pass is followed by 44 more samples.
* Input filter: four first-order shelves (-g, +g, +g, -g) in front of the lines.
* Two independent groups of sixteen lines, one per output channel. Line k
  takes `beta_k (1 + w_k)` of its own input channel and `beta_k (1 - w_k)` of
  the other one.
* A line is `floor(prime * size + 0.5)` samples long plus 0.88 ms times the
  sine of a single-precision phase accumulator (0.6 Hz, start 11.25 k rad,
  counted from the first processed sample). It is read with linear
  interpolation at the length of the sample being read.
* Per sample: read all lines; multiply by the Decay attenuation
  `10^(-3 prime size / (44100 T))`; sum with the output weights (the output is
  taken here); pass the loop filter (-0.06 dB and four first-order shelves);
  mix with the matrix; add the filtered input; write.
* The matrix is a Hadamard matrix whose outputs feed the lines in reverse
  order: entry [destination j][source k] is -1 where k has a bit that j lacks
  an odd number of times, else +1, times 1/4.
* Output weights follow three anchors (line 1, line 9, line 16) joined by two
  exponential S-curves; the anchors of lines 1 and 16 move with Decay up to 6 s.
* The output passes a two-pole low-pass at 20 kHz, Q 1.

Constants: tide_structural_data/network_a.json, written by fit_network_a.py.
Score with `python score_network.py network_a.py`.
"""
from __future__ import annotations

import ctypes
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

HERE = Path(__file__).resolve().parent
DATA = HERE / "tide_structural_data" / "network_a.json"
SOURCE = HERE / "network_a.c"
SCRATCH = HERE.parents[1] / "Results" / "RevOceanCharacterization" / "work" / "network_a"
LIBRARY = SCRATCH / "network_a.dylib"
INTERNAL_RATE = 44100
LINES = 16
LATTICE = 160                    # internal sample on the common lattice of a 48 kHz host
HOST_STEP = 147                  # host sample on that lattice
DOUBLES = ctypes.POINTER(ctypes.c_double)
FLOATS = ctypes.POINTER(ctypes.c_float)
INTS = ctypes.POINTER(ctypes.c_int)

_library = None


def library():
    """The compiled core; built when network_a.c is newer than the library."""
    global _library
    if _library is None:
        if not LIBRARY.exists() or LIBRARY.stat().st_mtime < SOURCE.stat().st_mtime:
            SCRATCH.mkdir(parents=True, exist_ok=True)
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-ffp-contract=off", "-o", str(LIBRARY), str(SOURCE)], check=True)
        loaded = ctypes.CDLL(str(LIBRARY))
        loaded.line_lengths.argtypes = [ctypes.c_long, ctypes.c_long, INTS, DOUBLES, ctypes.c_double, ctypes.c_double,
                                        ctypes.c_double, ctypes.c_double, FLOATS]
        loaded.network_group.argtypes = [ctypes.c_long, FLOATS] + [DOUBLES] * 8 + [ctypes.c_int, DOUBLES, ctypes.c_int,
                                                                                  ctypes.c_int, DOUBLES, DOUBLES, DOUBLES]
        converter = [DOUBLES, ctypes.c_long, ctypes.c_long, ctypes.c_long, DOUBLES, ctypes.c_long, ctypes.c_long,
                     ctypes.c_int, ctypes.c_int, ctypes.c_long, ctypes.c_long, DOUBLES]
        loaded.convert_in.argtypes = converter
        loaded.convert_out.argtypes = converter
        _library = loaded
    return _library


def _doubles(array):
    return None if array is None else array.ctypes.data_as(DOUBLES)


def _contiguous(array):
    return None if array is None else np.ascontiguousarray(array, dtype=np.float64)


def exp_map(normalised: float, low: float, high: float, shape: float) -> float:
    """The vendor's Exp(shape) law: low + (high - low)(e^(shape x) - 1)/(e^shape - 1)."""
    return low + (high - low) * math.expm1(shape * normalised) / math.expm1(shape)


def host_value(value: float, low: float, high: float, shape: float) -> float:
    """A display value as the plug-in receives it: through the single-precision normalised parameter."""
    normalised = np.float32(math.log1p((value - low) / (high - low) * math.expm1(shape)) / shape)
    return exp_map(float(normalised), low, high, shape)


def s_curve(position, shape: float) -> np.ndarray:
    """Symmetric exponential S-curve from 0 at position 0 to 1 at position 1."""
    offset = 2.0 * np.asarray(position, dtype=np.float64) - 1.0
    return 0.5 + 0.5 * np.sign(offset) * np.expm1(shape * np.abs(offset)) / math.expm1(shape)


def low_shelf(pole_hz: float, gain_db: float) -> tuple:
    """(b, a) of a first-order shelf with `gain_db` at 0 Hz and unit gain at Nyquist; bilinear, pole at `pole_hz`."""
    gain = 10.0 ** (gain_db / 20.0)
    tangent = math.tan(math.pi * pole_hz / INTERNAL_RATE)
    pole = (1.0 - tangent) / (1.0 + tangent)
    zero = (1.0 - gain * tangent) / (1.0 + gain * tangent)
    return (1.0 + pole) / (1.0 + zero) * np.array([1.0, -zero]), np.array([1.0, -pole])


def high_shelf(pole_hz: float, gain_db: float) -> tuple:
    """(b, a) of a first-order shelf with unit gain at 0 Hz and `gain_db` at Nyquist; bilinear, pole at `pole_hz`."""
    gain = 10.0 ** (gain_db / 20.0)
    tangent = math.tan(math.pi * pole_hz / INTERNAL_RATE)
    pole = (1.0 - tangent) / (1.0 + tangent)
    zero = (gain - tangent) / (gain + tangent)
    return (1.0 - pole) / (1.0 - zero) * np.array([1.0, -zero]), np.array([1.0, -pole])


def cascade(parts) -> tuple:
    """(b, a) of filters in series."""
    b, a = np.ones(1), np.ones(1)
    for part_b, part_a in parts:
        b, a = np.convolve(b, part_b), np.convolve(a, part_a)
    return b, a


def feedback_signs() -> np.ndarray:
    """[destination][source]: -1 where the source index has a bit the destination lacks an odd number of times."""
    return np.array([[-1.0 if bin(source & ~destination & 15).count("1") % 2 else 1.0 for source in range(LINES)]
                     for destination in range(LINES)])


def converter_table(cutoff: float, zero_crossings: int, beta: float, entries: int) -> np.ndarray:
    """One wing of the converter kernel as single-precision numbers (first_order_model.converter_table)."""
    index = np.arange(zero_crossings * entries)
    window = np.i0(beta * np.sqrt(1.0 - (index / (len(index) - 1)) ** 2)) / np.i0(beta)
    return (cutoff * np.sinc(cutoff * index / entries) * window).astype(np.float32).astype(np.float64)


class Model:
    """The network with its constants; every method works at the internal rate unless it says host."""

    def __init__(self, constants: dict):
        self.constants = constants
        lines, oscillator, loop = constants["lines"], constants["oscillator"], constants["loop"]
        self.prime = np.array(lines["prime"], dtype=np.float64)                  # [group][line]
        beta, width = np.array(lines["input_gain"]), np.array(lines["width"])
        self.own_gain = beta * (1.0 + width)
        self.cross_gain = beta * (1.0 - width)
        self.oscillator = oscillator
        self.input_b, self.input_a = cascade(low_shelf(part["pole_hz"], part["gain_db"]) for part in constants["input_filter"]["low_shelves"])
        shelves = cascade([high_shelf(loop["high_shelf"]["pole_hz"], loop["high_shelf"]["gain_db"])]
                          + [low_shelf(part["pole_hz"], part["gain_db"]) for part in loop["low_shelves"]])
        self.loop_b, self.loop_a = shelves[0] * 10.0 ** (loop["flat_db"] / 20.0), shelves[1]
        self.matrix = loop["matrix_gain"] * feedback_signs()
        self.weights = constants["output_weights"]
        lowpass = constants["output_lowpass"]
        omega = 2.0 * math.pi * lowpass["frequency_hz"] / INTERNAL_RATE
        alpha = math.sin(omega) / (2.0 * lowpass["q"])
        self.lowpass_b = np.array([(1 - math.cos(omega)) / 2, 1 - math.cos(omega), (1 - math.cos(omega)) / 2]) / (1 + alpha)
        self.lowpass_a = np.array([1.0, -2 * math.cos(omega) / (1 + alpha), (1 - alpha) / (1 + alpha)])
        self.predelay = constants["predelay_internal_samples"]
        self.output_delay = constants["output_delay_internal_samples"]
        converter = constants["converter"]
        self.converter = converter
        self.table = converter_table(converter["cutoff"], converter["zero_crossings"], converter["kaiser_beta"],
                                     converter["table_entries_per_sample"])

    # ------------------------------------------------------------------ laws

    def whole_lengths(self, group: int, size: float) -> np.ndarray:
        return np.floor(self.prime[group] * size + 0.5)

    def attenuation(self, group: int, decay: float, size: float) -> np.ndarray:
        return 10.0 ** (-3.0 * self.prime[group] * size / (INTERNAL_RATE * decay))

    def output_weights(self, decay: float) -> np.ndarray:
        """Weights of the sixteen output taps at a Decay in seconds (line 9 is the unit)."""
        law = self.weights
        travel = (min(decay, law["decay_knee_seconds"]) - law["decay_low_seconds"]) / (law["decay_knee_seconds"] - law["decay_low_seconds"])
        first = law["line_1"][0] + (law["line_1"][1] - law["line_1"][0]) * travel
        last = law["line_16"][0] + (law["line_16"][1] - law["line_16"][0]) * math.expm1(law["line_16_shape"] * travel) / math.expm1(law["line_16_shape"])
        line = np.arange(1, LINES + 1)
        lower = first + (1.0 - first) * s_curve((line - 1) / 8.0, law["lower_shape"])
        upper = 1.0 + (last - 1.0) * s_curve((line - 9) / 7.0, law["upper_shape"])
        return np.where(line <= 9, lower, upper)

    # ------------------------------------------------------------------ core

    def lengths(self, group: int, start: int, frames: int, size: float = 1.0) -> np.ndarray:
        """Single-precision lengths [frame][line] while internal samples start .. start + frames - 1 are read."""
        accumulator = (2 * np.arange(LINES) + group).astype(np.int32)
        whole = self.whole_lengths(group, size)
        result = np.empty((frames, LINES), np.float32)
        library().line_lengths(start, frames, accumulator.ctypes.data_as(INTS), _doubles(whole),
                               self.oscillator["depth_seconds"], float(INTERNAL_RATE), self.oscillator["phase_step_rad"],
                               2.0 * math.pi * self.oscillator["rate_hz"] / INTERNAL_RATE, result.ctypes.data_as(FLOATS))
        return result

    def group(self, lengths: np.ndarray, own, cross, attenuation, weights, *, own_gain=None, cross_gain=None,
              matrix=None, loop=None, output_after_loop=False, inject=None, want_taps=False):
        """One group: excitation in front of the low-pass (and the sixteen tap signals when asked for).

        `own` and `cross` are the filtered, delayed input channels. The keyword
        arguments replace single elements of the model; fit_network_a.py uses them.
        """
        frames = len(lengths)
        own_gain = _contiguous(self.own_gain if own_gain is None else own_gain)
        cross_gain = _contiguous(self.cross_gain if cross_gain is None else cross_gain)
        matrix = _contiguous(self.matrix if matrix is None else matrix)
        loop_b, loop_a = (self.loop_b, self.loop_a) if loop is None else loop
        loop_b, loop_a = _contiguous(loop_b), _contiguous(loop_a)
        own, cross, inject = _contiguous(own), _contiguous(cross), _contiguous(inject)
        attenuation, weights = _contiguous(attenuation), _contiguous(weights)
        excitation = np.empty(frames)
        taps = np.empty((frames, LINES)) if want_taps else None
        library().network_group(frames, lengths.ctypes.data_as(FLOATS), _doubles(own), _doubles(cross), _doubles(own_gain),
                                _doubles(cross_gain), _doubles(attenuation), _doubles(weights), _doubles(matrix),
                                _doubles(loop_b), len(loop_b), _doubles(loop_a), len(loop_a), int(output_after_loop),
                                _doubles(inject), _doubles(excitation), _doubles(taps))
        return (excitation, taps) if want_taps else excitation

    def drive(self, internal: np.ndarray) -> np.ndarray:
        """Input filter and the samples in front of the lines, for an array (frames, channels) or (frames,)."""
        filtered = lfilter(self.input_b, self.input_a, internal, axis=0)
        delayed = np.zeros_like(filtered)
        delayed[self.predelay:] = filtered[:len(filtered) - self.predelay]
        return delayed

    def lowpass(self, excitation: np.ndarray) -> np.ndarray:
        return lfilter(self.lowpass_b, self.lowpass_a, excitation, axis=0)

    def network(self, internal: np.ndarray, start: int, decay: float, size: float) -> np.ndarray:
        """Both groups for a stereo internal stream whose first sample is internal sample `start`."""
        driven = self.drive(internal)
        weights = self.output_weights(decay)
        result = np.empty_like(driven)
        for group in (0, 1):
            lengths = self.lengths(group, start, len(driven), size)
            excitation = self.group(lengths, driven[:, group], driven[:, 1 - group], self.attenuation(group, decay, size), weights)
            result[:, group] = self.lowpass(excitation)
        return result

    # ------------------------------------------------------------------ host

    def render(self, stimulus: np.ndarray, sample_rate: int, warmup_seconds: float, decay_seconds: float,
               size_percent: float) -> np.ndarray:
        stimulus = np.asarray(stimulus, dtype=np.float64)
        decay = host_value(decay_seconds, *self.constants["decay_parameter"])
        size = host_value(size_percent, *self.constants["size_parameter"]) / 100.0
        warmup = int(round(warmup_seconds * sample_rate))
        if sample_rate == INTERNAL_RATE:
            output = np.zeros_like(stimulus)
            network = self.network(stimulus, warmup, decay, size)
            output[self.output_delay:] = network[:len(stimulus) - self.output_delay]
            return output
        if sample_rate == 48000:
            return self.render_converted(stimulus, warmup, decay, size)
        raise ValueError("the model covers host rates of 44100 and 48000 Hz")

    def render_converted(self, stimulus: np.ndarray, warmup: int, decay: float, size: float) -> np.ndarray:
        """48 kHz host: input converter, network, output converter (host sample n sits at 147 n on the lattice)."""
        converter = self.converter
        entries, reach = converter["table_entries_per_sample"], converter["zero_crossings"] + 1
        input_offset = int(round(converter["input_delay_internal_samples"] * LATTICE))
        output_offset = int(round(converter["output_delay_host_samples"] * HOST_STEP))
        frames = len(stimulus)
        first = max((HOST_STEP * warmup) // LATTICE - 2 * reach, 0)
        count = (HOST_STEP * (warmup + frames)) // LATTICE + 2 * reach - first
        internal = np.empty((count, 2))
        for channel in (0, 1):
            host = np.ascontiguousarray(stimulus[:, channel])
            column = np.empty(count)
            library().convert_in(_doubles(host), frames, warmup, input_offset, _doubles(self.table), len(self.table), entries,
                                 reach, converter["exact_entry_low_wing"]["input"], first, count, _doubles(column))
            internal[:, channel] = column
        network = self.network(internal, first, decay, size)
        output = np.empty((frames, 2))
        for channel in (0, 1):
            column = np.ascontiguousarray(network[:, channel])
            host = np.empty(frames)
            library().convert_out(_doubles(column), first, count, output_offset, _doubles(self.table), len(self.table), entries,
                                  reach, converter["exact_entry_low_wing"]["output"], warmup, frames, _doubles(host))
            output[:, channel] = host
        return output


def load_constants(path: Path = DATA) -> dict:
    return json.loads(path.read_text())


_model: Model | None = None


def model() -> Model:
    global _model
    if _model is None:
        _model = Model(load_constants())
    return _model


def render(stimulus: np.ndarray, sample_rate: int, warmup_seconds: float, decay_seconds: float, size_percent: float) -> np.ndarray:
    return model().render(stimulus, int(sample_rate), float(warmup_seconds), float(decay_seconds), float(size_percent))
