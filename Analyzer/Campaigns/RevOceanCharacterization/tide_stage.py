#!/usr/bin/env python3
"""The Tide stage of Rev OCEAN (Macro above 0): the input comb and the voice filter, as closed forms.

Everything here runs at the reference's internal rate of 44.1 kHz, between its two rate converters, whatever
the host rate is. `macro` is the Macro knob as a fraction, 0 to 1. Evidence, residuals and what is still
open: findings/tide_stage.md. Constants and scores: tide_structural_data/tide_stage.json, written by
fit_tide_stage.py.

Signal path of one channel (the network is first_order_model.Network, unchanged):

    in ─┬─────────────────── cos(90° macro) ──┐
        └─ comb ──────────── sin(90° macro) ──(+)─ 44 samples ─ lines ─ rest kernel ─ voice A ─ out
                                                                 (network)             (takes the place of the
                                                                                        fixed 20 kHz low-pass)

Comb, one per input channel (`comb`, `comb_delay`):

    v[c]    = lowpass(highpass(read(line, D[c])))     one-pole bilinear sections at 30 Hz and 20 kHz
    line[c] = in[c] - 0.4 v[c]                        both sections sit inside the feedback loop
    D[c]    = 431.2075 + 236.0360 tri((c - 85.7) / (200 s) + channel / 4)   samples, tri(0) = 0 rising;
              held as a single-precision number (here: product and sum rounded to single precision, which
              puts 41 of 44 tested tap changes on the right sample; the exact arithmetic is open)
    read    = first-order all-pass: y = x[c - i - 1] + a (x[c - i] - y[c - 1]),  i = floor(D), d = D - i,
              a = (1 - d) / (1 + d), all per sample

`c` counts the samples of the internal input stream (behind the input converter) from the first processed
sample, warm-up included. The comb does not depend on Macro, Size or Decay.

Voice, one per output channel for the first eight lines (`voice`, `voice_parameters`). It is a two-pole
low-pass realised as a trapezoidal state-variable filter, followed by a gain. Cut-off, Q and gain are set
once per block of 44 samples (1 ms) and held; a block starts where the sample count of the network read is
a multiple of 44. With phi the phase of the voice in cycles, a = 0.88 macro, d = min(1, a / 0.05) and
E(x) = (1 - e^(-3x)) / (1 - e^(-3)):

    cut-off   fc = 20000 - 19990 E(a) E(phi mod 1)                 Hz
    gain      g  = (1 - d) + d sqrt(2) sin^2(pi phi)
    Q target  Qt = 1 + d (K(fc) - 1),  K piecewise linear in fc through
              (0 Hz, 3.365) (1 kHz, 7.01) (3 kHz, 10 - 7.91 E(macro)) (5 kHz, 7 - 5.64 E(macro))
              (15 kHz, the same) (20 kHz, 1)
    Q         one-pole smoothing of Qt with a time constant of 10 blocks: Q += (1 - e^(-0.1)) (Qt - Q)

When phi passes a whole number the cut-off returns to 20 kHz within one block; the gain is zero there from
Macro 5.68 % up and 1 - d below. At Macro 0 the voice is the fixed low-pass of the Macro 0 model (20 kHz,
Q 1, gain 1). The phase itself (rate, start value, wander) is not part of this module: `voice` takes its
value and slope at a reference sample.

`FirstPass` joins the stage with the Macro 0 network and predicts the first-pass window of a unit impulse
at a 44.1 kHz or 48 kHz host once the phase of the voice is given:

    window = FirstPass(time, input_channel, output_channel, macro, host_rate=48000)
    predicted = window.response(phase, rate)          # reference samples: capture.output[window.samples, output_channel]

The two recursions (comb, voice) are compiled from the C source below into the packet's scratch folder on
first use; without a compiler the same recursions run in Python. The module renders nothing.
"""
from __future__ import annotations

import ctypes
import hashlib
import subprocess
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

import first_order_model as network_model

INTERNAL_RATE = network_model.INTERNAL_RATE
BLOCK = 44                               # samples between two updates of the voice (1 ms)

# ---------------------------------------------------------------- comb
COMB_DELAY_MID = 431.2075                # samples at 44.1 kHz (9.77795 ms)
COMB_DELAY_SWING = 236.0360              # samples (5.35229 ms): 195.1715 to 667.2435 samples
COMB_PERIOD_SECONDS = 200.0
COMB_ORIGIN = 85.7                       # samples: where the triangle crosses its middle, rising, for the left input
COMB_RIGHT_LEAD = 0.25                   # the right input is a quarter period ahead
COMB_FEEDBACK = -0.4
COMB_HIGH_PASS_HZ = 30.0
COMB_LOW_PASS_HZ = 20000.0

# ---------------------------------------------------------------- voice
AMOUNT_PER_MACRO = 0.88                  # internal amount a = 0.88 macro
FULL_DEPTH_AMOUNT = 0.05                 # the gain window and the Q law reach full depth at a = 0.05 (Macro 5.68 %)
CURVE = 3.0                              # bend of E(x)
CUTOFF_TOP = 20000.0                     # Hz, also the resting cut-off
CUTOFF_END = 10.0                        # Hz, where the cut-off law would end at a = 1, phi = 1
GAIN_TOP = float(np.sqrt(2.0))
REST_Q = 1.0
Q_KNOT_HZ = (0.0, 1000.0, 3000.0, 5000.0, 15000.0, 20000.0)
Q_LOW = 3.365                            # K at 0 Hz (extrapolated: the cut-off never falls below 463.85 Hz)
Q_1K = 7.01
Q_3K = (10.0, 2.09)                      # K at 3 kHz at Macro 0 and at Macro 1
Q_PLATEAU = (7.0, 1.36)                  # K from 5 to 15 kHz at Macro 0 and at Macro 1
Q_SMOOTHING_BLOCKS = 10.0                # time constant of the Q smoothing
Q_HISTORY = 150                          # blocks of history used to start the smoothing

TANK_PREDELAY = 44                       # internal samples between the input stream and the line write
OUTPUT_DELAY = 44                        # samples between a network read and the output at a 44.1 kHz host
STREAM_FRAMES = 3100                     # internal samples of comb output kept per impulse
RUN_IN = 64                              # internal samples in front of a window


# ---------------------------------------------------------------- laws

def triangle(x):
    """Unit triangle of period 1: 0 at x = 0 going up, +1 at 1/4, -1 at 3/4."""
    x = np.asarray(x, dtype=np.float64)
    x = x - np.floor(x)
    return np.where(x < 0.25, 4.0 * x, np.where(x < 0.75, 2.0 - 4.0 * x, 4.0 * x - 4.0))


def comb_delay(count, channel: int, constants: dict | None = None):
    """Delay of the comb's read, in internal samples, when the input stream has reached sample `count`.

    The reference holds it in single precision; that decides the sample at which the delay passes a whole
    number, and with it the taps of the read. `single_precision` False in the constants gives the exact law.
    """
    constants = constants or comb_constants()
    lfo = triangle((np.asarray(count, dtype=np.float64) - constants["origin"]) / constants["period"] + COMB_RIGHT_LEAD * channel)
    if not constants["single_precision"]:
        return constants["mid"] + constants["swing"] * lfo
    return (np.float32(constants["mid"]) + np.float32(constants["swing"]) * lfo.astype(np.float32)).astype(np.float64)


def curve(x):
    """E(x) = (1 - e^(-3x)) / (1 - e^(-3)): 0 at 0, 1 at 1."""
    return (1.0 - np.exp(-CURVE * np.asarray(x, dtype=np.float64))) / (1.0 - np.exp(-CURVE))


def depth(macro: float) -> float:
    return min(1.0, AMOUNT_PER_MACRO * macro / FULL_DEPTH_AMOUNT)


def lowest_cutoff(macro: float) -> float:
    """Cut-off at the end of a cycle, Hz."""
    return CUTOFF_TOP - (CUTOFF_TOP - CUTOFF_END) * float(curve(AMOUNT_PER_MACRO * macro))


def cutoff(phase, macro: float):
    phase = np.asarray(phase, dtype=np.float64)
    return CUTOFF_TOP - (CUTOFF_TOP - lowest_cutoff(macro)) * curve(phase - np.floor(phase))


def gain(phase, macro: float):
    d = depth(macro)
    return (1.0 - d) + d * GAIN_TOP * np.sin(np.pi * np.asarray(phase, dtype=np.float64)) ** 2


def quality_knots(macro: float) -> np.ndarray:
    """K at the knots Q_KNOT_HZ."""
    bend = float(curve(macro))
    at_3k = Q_3K[0] + (Q_3K[1] - Q_3K[0]) * bend
    plateau = Q_PLATEAU[0] + (Q_PLATEAU[1] - Q_PLATEAU[0]) * bend
    return np.array([Q_LOW, Q_1K, at_3k, plateau, plateau, REST_Q])


def quality_target(cutoff_hz, macro: float):
    """Q the voice moves towards at a cut-off."""
    return REST_Q + depth(macro) * (np.interp(cutoff_hz, Q_KNOT_HZ, quality_knots(macro)) - REST_Q)


def voice_parameters(index: np.ndarray, phase: float, rate: float, reference: float, macro: float) -> tuple:
    """(cut-off, Q, gain) per sample for the read counts `index`.

    The phase is `phase + rate (count - reference) / 44100` cycles. Everything is evaluated at the first
    sample of a block of 44 and held; Q follows its target through the one-pole smoothing.
    """
    block = np.asarray(index, dtype=np.int64) // BLOCK
    first = int(block[0]) - Q_HISTORY
    start = np.arange(first, int(block[-1]) + 1) * BLOCK
    phi = phase + rate * (start - reference) / INTERNAL_RATE
    fc = cutoff(phi, macro)
    target = quality_target(fc, macro)
    step = 1.0 - np.exp(-1.0 / Q_SMOOTHING_BLOCKS)
    quality = lfilter([step], [1.0, step - 1.0], target, zi=[target[0] * (1.0 - step)])[0]
    pick = block - first
    return fc[pick], quality[pick], gain(phi, macro)[pick]


def one_pole(corner_hz: float) -> float:
    """k = tan(pi f / rate) of a bilinear one-pole section."""
    return float(np.tan(np.pi * corner_hz / INTERNAL_RATE))


def low_pass(cutoff_hz: float, quality: float) -> tuple:
    """(b, a) of the two-pole low-pass in the audio-EQ bilinear form: the transfer function of the voice."""
    w = 2.0 * np.pi * cutoff_hz / INTERNAL_RATE
    alpha = np.sin(w) / (2.0 * quality)
    b = np.array([0.5, 1.0, 0.5]) * (1.0 - np.cos(w)) / (1.0 + alpha)
    return b, np.array([1.0, -2.0 * np.cos(w) / (1.0 + alpha), (1.0 - alpha) / (1.0 + alpha)])


# ---------------------------------------------------------------- the two recursions

_C_SOURCE = r"""
#include <math.h>
#include <stdlib.h>

static double triangle(double x) {
    x -= floor(x);
    if (x < 0.25) return 4.0 * x;
    if (x < 0.75) return 2.0 - 4.0 * x;
    return 4.0 * x - 4.0;
}

/* Delayed path of the comb for x[count]; x[0] is sample `first` of the internal input stream. */
void comb(const double *x, double *y, long count, double first, double mid, double swing, double period,
          double origin, double lead, double feedback, double hp_k, double lp_k, int single_precision) {
    double *line = (double *)calloc((size_t)count + 2, sizeof(double));
    double hg = 1.0 / (1.0 + hp_k), hp = (1.0 - hp_k) / (1.0 + hp_k);
    double lg = lp_k / (1.0 + lp_k), lp = (1.0 - lp_k) / (1.0 + lp_k);
    double read = 0.0, hx = 0.0, hy = 0.0, lx = 0.0, ly = 0.0;
    for (long n = 0; n < count; ++n) {
        double exact = triangle((first + n - origin) / period + lead);
        float depth = (float)swing * (float)exact;
        float single = (float)mid + depth;
        double delay = single_precision ? (double)single : mid + swing * exact;
        double whole = floor(delay), fraction = delay - whole;
        long i = (long)whole;
        double early = n - i >= 0 ? line[n - i] : 0.0;
        double late = n - i - 1 >= 0 ? line[n - i - 1] : 0.0;
        read = late + (1.0 - fraction) / (1.0 + fraction) * (early - read);
        hy = hg * (read - hx) + hp * hy; hx = read;
        ly = lg * (hy + lx) + lp * ly; lx = hy;
        line[n] = x[n] + feedback * ly;
        y[n] = ly;
    }
    free(line);
}

/* Trapezoidal state-variable low-pass with per-sample cut-off and Q, then the gain. */
void voice(const double *x, double *y, long count, const double *cutoff, const double *quality,
           const double *gain, double rate) {
    double s1 = 0.0, s2 = 0.0;
    for (long n = 0; n < count; ++n) {
        double g = tan(M_PI * cutoff[n] / rate), k = 1.0 / quality[n];
        double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        double v3 = x[n] - s2;
        double v1 = a1 * s1 + a2 * v3;
        double v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0 * v1 - s1; s2 = 2.0 * v2 - s2;
        y[n] = gain[n] * v2;
    }
}
"""
_SCRATCH = Path(__file__).resolve().parents[2] / "Results/RevOceanCharacterization/work/tide_stage"
_kernels = None


def _compiled():
    """The C recursions, built once into the scratch folder; None when no compiler is available."""
    global _kernels
    if _kernels is None:
        name = "tide_stage_kernels_" + hashlib.sha256(_C_SOURCE.encode()).hexdigest()[:12]
        library = _SCRATCH / (name + ".dylib")
        try:
            if not library.exists():
                _SCRATCH.mkdir(parents=True, exist_ok=True)
                source = _SCRATCH / (name + ".c")
                source.write_text(_C_SOURCE)
                subprocess.run(["cc", "-O2", "-ffp-contract=off", "-shared", "-fPIC", "-o", str(library), str(source)],
                               check=True, capture_output=True)
            loaded = ctypes.CDLL(str(library))
            pointer = ctypes.POINTER(ctypes.c_double)
            loaded.comb.argtypes = [pointer, pointer, ctypes.c_long] + [ctypes.c_double] * 9 + [ctypes.c_int]
            loaded.voice.argtypes = [pointer, pointer, ctypes.c_long, pointer, pointer, pointer, ctypes.c_double]
            loaded.comb.restype = loaded.voice.restype = None
            _kernels = loaded
        except (OSError, subprocess.CalledProcessError):
            _kernels = False
    return _kernels or None


def _pointer(array: np.ndarray):
    return array.ctypes.data_as(ctypes.POINTER(ctypes.c_double))


def comb_python(stream: np.ndarray, first: int, channel: int, constants: dict) -> np.ndarray:
    """The comb recursion in plain Python (definition and fallback of `comb`)."""
    delay = comb_delay(first + np.arange(len(stream)), channel, constants)
    hp_k, lp_k = one_pole(constants["high_pass_hz"]), one_pole(constants["low_pass_hz"])
    hg, hp = 1.0 / (1.0 + hp_k), (1.0 - hp_k) / (1.0 + hp_k)
    lg, lp = lp_k / (1.0 + lp_k), (1.0 - lp_k) / (1.0 + lp_k)
    line = np.zeros(len(stream))
    out = np.zeros(len(stream))
    read = hx = hy = lx = ly = 0.0
    for n in range(len(stream)):
        whole = int(np.floor(delay[n]))
        fraction = delay[n] - whole
        early = line[n - whole] if n - whole >= 0 else 0.0
        late = line[n - whole - 1] if n - whole - 1 >= 0 else 0.0
        read = late + (1.0 - fraction) / (1.0 + fraction) * (early - read)
        hy = hg * (read - hx) + hp * hy
        hx = read
        previous = ly
        ly = lg * (hy + lx) + lp * previous
        lx = hy
        line[n] = stream[n] + constants["feedback"] * ly
        out[n] = ly
    return out


def comb_constants(**changes) -> dict:
    constants = {"mid": COMB_DELAY_MID, "swing": COMB_DELAY_SWING, "period": COMB_PERIOD_SECONDS * INTERNAL_RATE,
                 "origin": COMB_ORIGIN, "feedback": COMB_FEEDBACK, "high_pass_hz": COMB_HIGH_PASS_HZ,
                 "low_pass_hz": COMB_LOW_PASS_HZ, "single_precision": True}
    constants.update(changes)
    return constants


def comb(stream: np.ndarray, first: int, channel: int, **changes) -> np.ndarray:
    """Delayed path of the comb for the internal input stream that starts at sample count `first`.

    Keyword arguments replace constants of `comb_constants` (used by the fit script for its ablations).
    """
    constants = comb_constants(**changes)
    stream = np.ascontiguousarray(stream, dtype=np.float64)
    kernels = _compiled()
    if kernels is None:
        return comb_python(stream, first, channel, constants)
    out = np.zeros_like(stream)
    kernels.comb(_pointer(stream), _pointer(out), len(stream), float(first), constants["mid"], constants["swing"],
                 constants["period"], constants["origin"], COMB_RIGHT_LEAD * channel, constants["feedback"],
                 one_pole(constants["high_pass_hz"]), one_pole(constants["low_pass_hz"]), int(constants["single_precision"]))
    return out


def input_stage(stream: np.ndarray, first: int, channel: int, macro: float, **changes) -> np.ndarray:
    """What the network receives: the equal-power mix of the stream and the comb's delayed path."""
    angle = 0.5 * np.pi * macro
    mixed = np.cos(angle) * np.asarray(stream, dtype=np.float64)
    if macro > 0.0:
        mixed = mixed + np.sin(angle) * comb(stream, first, channel, **changes)
    return mixed


def state_variable_python(signal: np.ndarray, cutoff_hz: np.ndarray, quality: np.ndarray, level: np.ndarray) -> np.ndarray:
    """The voice recursion in plain Python (definition and fallback of `state_variable`)."""
    out = np.zeros(len(signal))
    s1 = s2 = 0.0
    g = np.tan(np.pi * cutoff_hz / INTERNAL_RATE)
    k = 1.0 / quality
    for n in range(len(signal)):
        a1 = 1.0 / (1.0 + g[n] * (g[n] + k[n]))
        a2 = g[n] * a1
        v3 = signal[n] - s2
        v1 = a1 * s1 + a2 * v3
        v2 = s2 + a2 * s1 + g[n] * a2 * v3
        s1, s2 = 2.0 * v1 - s1, 2.0 * v2 - s2
        out[n] = level[n] * v2
    return out


def state_variable(signal: np.ndarray, cutoff_hz: np.ndarray, quality: np.ndarray, level: np.ndarray) -> np.ndarray:
    """Trapezoidal state-variable low-pass with per-sample cut-off and Q, followed by the gain."""
    arrays = [np.ascontiguousarray(np.broadcast_to(a, np.shape(signal)), dtype=np.float64) for a in (signal, cutoff_hz, quality, level)]
    kernels = _compiled()
    if kernels is None:
        return state_variable_python(*arrays)
    out = np.zeros_like(arrays[0])
    kernels.voice(_pointer(arrays[0]), _pointer(out), len(out), _pointer(arrays[1]), _pointer(arrays[2]), _pointer(arrays[3]),
                  float(INTERNAL_RATE))
    return out


def voice(signal: np.ndarray, index: np.ndarray, phase: float, rate: float, reference: float, macro: float) -> np.ndarray:
    """The voice applied to `signal`, the network output in front of the resting low-pass, on read counts `index`."""
    return state_variable(signal, *voice_parameters(index, phase, rate, reference, macro))


def without_rest(signal: np.ndarray) -> np.ndarray:
    """A Macro 0 signal with its resting low-pass (20 kHz, Q 1) divided out: the input the voice sees.

    The inverse has a double pole at the Nyquist frequency; it is exact in exact arithmetic and usable over
    a few thousand samples, because every low-pass that follows has its zeros there.
    """
    b, a = low_pass(CUTOFF_TOP, REST_Q)
    return lfilter(a / b[0], [1.0, 2.0, 1.0], signal)


# ---------------------------------------------------------------- joined with the Macro 0 network

_shared = {}


def network(seconds: float = 175.0) -> network_model.Network:
    """The Macro 0 network of first_order_model with its oscillators run for `seconds` of processing."""
    if "network" not in _shared:
        constants = network_model.load_constants()
        _shared["constants"] = constants
        _shared["network"] = network_model.Network(constants, int(seconds * INTERNAL_RATE) + 8192)
    return _shared["network"]


def rest_kernel() -> np.ndarray:
    """The fixed filter of the Macro 0 model divided by its resting low-pass: what stays in front of the voice."""
    if "rest_kernel" not in _shared:
        kernel = without_rest(np.array(network().fixed_filter))
        count = np.arange(len(kernel))
        sign = (-1.0) ** count
        ramp = np.polyfit(count[300:], (kernel * sign)[300:], 1)      # the fitted taps leave a small alternating ramp
        _shared["rest_kernel"] = kernel - np.polyval(ramp, count) * sign
    return _shared["rest_kernel"]


def take(stream: np.ndarray, index: np.ndarray) -> np.ndarray:
    inside = (index >= 0) & (index < len(stream))
    return np.where(inside, stream[np.clip(index, 0, len(stream) - 1)], 0.0)


def line_read(group: int, line: int, stream: np.ndarray, first: int, read: np.ndarray) -> np.ndarray:
    """Output of a line at the read counts `read` for a stream written from count `first` on."""
    length = network().length(group, line, read)
    whole = np.floor(length).astype(np.int64)
    fraction = length - whole
    index = read - whole - first
    return (1.0 - fraction) * take(stream, index) + fraction * take(stream, index - 1)


def voice_a_input(written: np.ndarray, first: int, input_channel: int, group: int, start: int, stop: int) -> np.ndarray:
    """Input of voice A on read counts [start, stop): first passes of lines 1 to 8 and the earliest second
    passes of the Macro 0 model, through the rest kernel. `written` enters the lines from count `first` on."""
    net = network()
    read = np.arange(start, stop)
    passes = [line_read(group, line, written, first, read) for line in range(8)]
    total = net.first_gain(input_channel, group)[:8] @ np.array(passes)
    fed = np.zeros(stop - start)
    for (early, late), pair_gain in zip(net.pairs, net.second_gain(input_channel, group)):
        fed += pair_gain * line_read(group, late, passes[early], start, read)
    total = total + np.convolve(fed, net.loop_kernel)[:stop - start]
    return np.convolve(total, rest_kernel())[:stop - start]


class Converters:
    """The two rate converters of first_order_model at a 48 kHz host, for streams instead of single impulses."""

    def __init__(self):
        network()
        constants = _shared["constants"]["converter"]
        self.table = network_model.converter_table(constants["cutoff"], constants["zero_crossings"], constants["kaiser_beta"],
                                                    constants["table_entries_per_sample"])
        self.entries = constants["table_entries_per_sample"]
        self.reach = np.arange(-constants["zero_crossings"] - 1, constants["zero_crossings"] + 2)
        self.low_wing = constants["exact_entry_low_wing"]
        self.input_offset = int(round(constants["input_delay_internal_samples"] * network_model.LATTICE))
        self.output_offset = int(round(constants["output_delay_host_samples"] * network_model.HOST_STEP))

    def impulse(self, host_sample: int) -> tuple:
        """(first internal count, internal stream) of a unit impulse at an absolute host sample."""
        centre = network_model.HOST_STEP * host_sample + self.input_offset
        source = centre // network_model.LATTICE + self.reach
        weights = network_model.table_lookup(self.table, network_model.LATTICE * source - centre, self.entries, self.low_wing["input"])
        return int(source[0]), weights / network_model.HOST_PER_INTERNAL

    def output_taps(self, host_samples: np.ndarray) -> tuple:
        """(internal counts, weights) with host[k] = sum(weights[k] * internal[counts[k]])."""
        position = network_model.HOST_STEP * host_samples - self.output_offset
        counts = (position // network_model.LATTICE)[:, None] + self.reach[None, :]
        weights = network_model.table_lookup(self.table, position[:, None] - network_model.LATTICE * counts, self.entries,
                                             self.low_wing["output"])
        return counts, weights


def converters() -> Converters:
    if "converters" not in _shared:
        _shared["converters"] = Converters()
    return _shared["converters"]


FIRST_PASS_WINDOW = {44100: (1080, 2255), 48000: (1200, 2500)}      # raw samples after the impulse


class FirstPass:
    """First-pass window of a unit impulse at Macro above 0, up to the phase of the voice.

    `time` is the impulse's sample in the stimulus, `group` the output channel. At Macro 1 the window moves
    with the comb delay, because nothing arrives undelayed. `samples` indexes the capture's output;
    `reference` is the read count at which `response` takes its phase. `undelayed` and `delayed` are the
    voice's input for the two paths of the input stage, `signal` their equal-power mix. `delayed_path`
    replaces the comb by another function of (stream, first count, channel).
    """

    def __init__(self, time: int, input_channel: int, group: int, macro: float, host_rate: int = 48000,
                 warmup_seconds: float = 10.0, delayed_path=None, **comb_changes):
        if host_rate not in FIRST_PASS_WINDOW:
            raise ValueError("the first-pass model covers hosts at 44.1 and 48 kHz")
        self.macro, self.group = macro, group
        warmup = int(round(warmup_seconds * host_rate))
        host_sample = warmup + int(time)
        if host_rate == INTERNAL_RATE:
            first, impulse = host_sample, np.ones(1)
        else:
            first, impulse = converters().impulse(host_sample)
        stream = np.zeros(STREAM_FRAMES)
        stream[:len(impulse)] = impulse
        self.delay = float(comb_delay(first, input_channel))
        shift = int(self.delay * host_rate / INTERNAL_RATE) if macro >= 1.0 else 0
        low, high = FIRST_PASS_WINDOW[host_rate]
        host = host_sample + shift + np.arange(low, high)
        self.samples = host - warmup
        if host_rate == INTERNAL_RATE:
            counts, self.weights = (host - OUTPUT_DELAY)[:, None], None
        else:
            counts, self.weights = converters().output_taps(host)
        start, stop = int(counts.min()) - RUN_IN, int(counts.max()) + 1
        self.taps = counts - start
        self.index = np.arange(start, stop)
        self.reference = float(counts.min())
        through = lambda written: voice_a_input(written, first + TANK_PREDELAY, input_channel, group, start, stop)
        self.undelayed = through(stream)
        self.delayed = np.zeros(stop - start)
        if macro > 0.0:
            path = comb(stream, first, input_channel, **comb_changes) if delayed_path is None else delayed_path(stream, first, input_channel)
            self.delayed = through(path)
        self.signal = np.cos(0.5 * np.pi * macro) * self.undelayed + np.sin(0.5 * np.pi * macro) * self.delayed

    def to_host(self, internal: np.ndarray) -> np.ndarray:
        if self.weights is None:
            return internal[self.taps[:, 0]]
        return np.sum(self.weights * internal[self.taps], axis=1)

    def at_rest(self) -> np.ndarray:
        """The window with the voice parked (20 kHz, Q 1, gain 1): the Macro 0 network behind the input stage."""
        b, a = low_pass(CUTOFF_TOP, REST_Q)
        return self.to_host(lfilter(b, a, self.signal))

    def response(self, phase: float, rate: float, signal: np.ndarray | None = None) -> np.ndarray:
        """Predicted window for a voice at `phase` cycles at the first window sample, moving at `rate` cycles/s."""
        signal = self.signal if signal is None else signal
        return self.to_host(voice(signal, self.index, phase, rate, self.reference, self.macro))
