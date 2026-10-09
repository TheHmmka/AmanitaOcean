#!/usr/bin/env python3
"""Executable specification of Rev OCEAN in Tide mode: the whole chain as one function (see SPEC.md).

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_scale, predelay_seconds, macro,
           width_scale, mix, phase=None, seed=None, level_stage=True, clip=True) -> array (frames, 2)

is the reference's raw output for a stereo stimulus of shape (frames, 2) that starts `warmup_seconds` of
silence after the first processed sample. Raw means the time base of `revocean.capture`: index n is host
sample n of the stimulus and the reported latency is still in the result. Every other control of the
reference is at its neutral value (Brightness 0 %, input filter open, Transients 0 dB, Ducking 0 %, Return
and Master 0 dB).

The arguments are physical, as the plug-in works with them after its parameter laws:

    decay_seconds     Decay, the time of 60 dB of decay
    size_scale        Size / 100 as a single-precision number, 0.3 to 2
    predelay_seconds  Pre-delay; the whole number of host samples it becomes is `predelay_samples`
    macro             Macro (the Tide amount), 0 to 1
    width_scale       travel of the Width knob, 0 to 2: 0 mono, 1 = 100 %, 2 = 150 %
    mix               Mix, 0 to 1
    phase             above Macro 0: function of the seconds since the first processed sample that returns
                      the phase of voice A of both outputs in cycles, shape (2, len(seconds)); voice B is
                      half a cycle away. None runs the campaign's generator for instance `seed`.
    level_stage       the wet reduction the reference applies to loud input although its Ducking reads 0 %
    clip              the reference's output clipper

`physical(settings)` turns normalised host parameters, the `settings` of `revocean.capture`, into these
arguments exactly as the plug-in does; `render_knobs` renders from them directly.

Signal path (everything between the converters runs at 44.1 kHz at every host rate):

    in ── pre-delay (whole host samples) ── converter ── equaliser ── [cos(90° m) + sin(90° m) comb] ── 44 samples
       ── 2 x 16 lines, Hadamard loop ── per output: lines 1-8 -> voice at phi, lines 9-16 -> voice at phi + 1/2
       ── converter ── level stage (keyed by the raw input) ── width ──(x wet gain)──(+)── clipper ── out
    in ── reported latency ──────────────────────────────────────────(x dry gain)───┘

The pieces come from the campaign's modules with every verified correction: converters.py (measured clock
signs), network_model.py (single-precision Size arithmetic), tide_stage.py (comb and voice), tide_model.py
(equaliser in front of the comb, lines 1 to 8 in voice A and 9 to 16 in voice B, rate of the phase) and
tide_phase.py (level of the phase); the outer laws are those of findings/io_verification.md. At Macro 0
the voices are the fixed 20 kHz low-pass of the network model and the result does not depend on `phase`
or `seed`. Evidence for this module: Analyzer/Results/RevOceanCharacterization/work/specification/.

Exact at Macro 0; exact up to the random phase above it; the phase itself is statistical. Not in the
model: the first seconds of a fresh instance, controls that move, Freeze. The module renders no capture.
"""
from __future__ import annotations

import math

import numpy as np

import converters
import network_model
import revocean
import tide_model

VOICE_LINES = tide_model.VOICE_LINES                  # lines of a group whose output taps pass voice A and voice B
DEFAULT_SEED = 0

# level stage: the reference's Ducking compressor at 0 %
LEVEL_KNEE_DB = 9.98306                               # knee width around the threshold of 0 dBFS
LEVEL_SLOPE = 5.0 / 7.0                               # dB of reduction per dB above the knee (ratio 3.5 : 1)
LEVEL_ATTACK_SECONDS = 0.005
LEVEL_RELEASE_SECONDS = 0.300

# output clipper
CLIP_THRESHOLD = 10.0 ** (8.0 / 20.0)                 # unity up to +8 dBFS
CLIP_CEILING = 10.0 ** (12.0 / 20.0)                  # +12 dBFS

PREDELAY_LAW = {"low": 0.0, "high": 2000.0, "shape": 6.0}      # milliseconds, the law the plug-in declares


# ---------------------------------------------------------------- from knob positions to physical arguments

def knob_decay_seconds(normalised: float) -> float:
    """Decay as the plug-in works with it: 0.5 + 59.5 (e^(4.4 x) - 1) / (e^4.4 - 1) s in single precision."""
    return network_model.plugin_value(network_model.constants()["parameters"]["decay"], normalised)


def knob_size_scale(normalised: float) -> np.float32:
    """Size / 100 as the plug-in works with it: fma(1.7f, r, 0.3f), r from the Exp(0.71337) law with a reciprocal."""
    return network_model.size_scale(network_model.constants()["parameters"]["size"], normalised)


def knob_predelay_seconds(normalised: float) -> float:
    """Pre-delay as the plug-in works with it: 2000 (e^(6 x) - 1) / (e^6 - 1) ms in single precision."""
    one, shape = np.float32(1.0), np.float32(PREDELAY_LAW["shape"])
    milliseconds = np.float32(PREDELAY_LAW["high"]) * (np.exp(shape * np.float32(normalised)) - one) / (np.exp(shape) - one)
    return float(milliseconds) / 1000.0


def knob_width_scale(normalised: float) -> float:
    """Travel of the Width knob: twice its normalised value (Width % = 200 (1 - 4^-x))."""
    return 2.0 * float(np.float32(normalised))


def physical(settings: dict | None) -> dict:
    """The arguments of `render` for normalised host parameters: the `settings` of `revocean.capture`,
    merged with its neutral baseline and rounded to single precision as the host interface carries them."""
    resolved = revocean.resolve(settings)
    return {"decay_seconds": knob_decay_seconds(resolved["decay"]), "size_scale": knob_size_scale(resolved["size"]),
            "predelay_seconds": knob_predelay_seconds(resolved["predelay"]), "macro": resolved["macro"],
            "width_scale": knob_width_scale(resolved["width"]), "mix": resolved["mix"]}


def predelay_samples(seconds: float, sample_rate: int) -> int:
    """Whole host samples of pre-delay: max(0, floor(P fs / 1000) - 1) with P in milliseconds, product and
    quotient in single precision. No interpolation; everything below two samples gives none."""
    milliseconds = np.float32(1000.0 * seconds)
    return max(0, int(math.floor(milliseconds * np.float32(sample_rate) / np.float32(1000.0))) - 1)


# ---------------------------------------------------------------- the wet path

def voice_inputs(stimulus, sample_rate: int, warmup_seconds: float, decay_seconds: float, size_scale: float,
                 predelay_seconds: float, macro: float) -> tuple:
    """(first_read, taps): what the voices filter, taps[frame][output][voice] at 44.1 kHz; taps[k] belongs
    to internal sample first_read + k, counted from the first processed sample.

    Pre-delay, input converter, equaliser, Tide input stage, the 44 samples in front of the lines, and the
    network with the output taps of lines 1 to 8 and of lines 9 to 16 summed apart, each line with its tap weight.
    """
    data = network_model.constants()
    stimulus = np.asarray(stimulus, dtype=np.float64)
    shift = predelay_samples(predelay_seconds, sample_rate)
    delayed = np.zeros_like(stimulus)
    delayed[shift:] = stimulus[:max(0, len(stimulus) - shift)]
    first, internal = network_model.network_input(delayed, sample_rate, warmup_seconds)
    driven = tide_model.tide_input(network_model.equalise(data, internal), first, float(macro))
    first_read = first + data["input"]["delay_samples"]
    values = network_model.parameters(data, float(decay_seconds), 100.0 * float(np.float32(size_scale)))
    weights = np.zeros((len(VOICE_LINES), network_model.LINES))
    for voice, lines in enumerate(VOICE_LINES):
        index = np.asarray(lines, dtype=int) - 1
        weights[voice, index] = values["tap"][index]
    return first_read, np.ascontiguousarray(network_model.run_core(values, driven, first_read, weights).transpose(0, 2, 1))


def at_rest(seconds) -> np.ndarray:
    """A phase for Macro 0, where the voices do not depend on it."""
    return np.zeros((2, np.size(seconds)))


def wet(taps: np.ndarray, first_read: int, sample_rate: int, warmup_seconds: float, frames: int, macro: float,
        phase=None, seed=None) -> np.ndarray:
    """The reference's wet signal in the raw time base, (frames, 2): Width 100 %, in front of the level stage.

    The two voices of each output on `taps` (as `voice_inputs` returns them), then the output converter.
    """
    macro = float(macro)
    if macro == 0.0:
        phase = at_rest
    elif phase is None:
        phase = tide_model.generator(DEFAULT_SEED if seed is None else seed, macro)
    origin = int(round(warmup_seconds * sample_rate))
    return tide_model.output(tide_model.voices(taps, first_read, phase, macro), first_read, sample_rate, origin, origin + frames)


# ---------------------------------------------------------------- the outer laws

def level_reduction_db(stimulus, sample_rate: int) -> np.ndarray:
    """Reduction in dB (zero or negative) the level stage asks for at every input frame.

    Key: the larger magnitude of the two raw input samples, in dB. Static curve: none up to half the knee
    below 0 dBFS (0.5629), a parabola through the knee, 5/7 dB per dB above it. The reduction follows that
    curve through a one-pole in dB, 5 ms while it grows and 300 ms while it recovers, in single precision.
    """
    key = np.max(np.abs(np.asarray(stimulus, dtype=np.float64)), axis=1)
    with np.errstate(divide="ignore"):
        level = 20.0 * np.log10(key)
    half = LEVEL_KNEE_DB / 2.0
    target = np.where(level <= -half, 0.0, np.where(level >= half, -LEVEL_SLOPE * level,
                                                    -LEVEL_SLOPE * (level + half) ** 2 / (2.0 * LEVEL_KNEE_DB)))
    reduction = np.zeros(len(key))
    active = np.nonzero(target)[0]
    if len(active):
        target = target.astype(np.float32)
        attack = np.float32(math.exp(-1.0 / (LEVEL_ATTACK_SECONDS * sample_rate)))
        release = np.float32(math.exp(-1.0 / (LEVEL_RELEASE_SECONDS * sample_rate)))
        state = np.float32(0.0)
        for index in range(int(active[0]), len(key)):
            wanted = target[index]
            state = wanted + (attack if wanted < state else release) * (state - wanted)
            reduction[index] = state
    return reduction


def apply_width(wet: np.ndarray, width_scale: float) -> np.ndarray:
    """Mid/side on the wet: mid gain sqrt(2 / (1 + s)), side gain s sqrt(2 / (1 + s)), s = width_scale."""
    mid_gain = math.sqrt(2.0 / (1.0 + width_scale))
    side_gain = width_scale * mid_gain
    mid, side = 0.5 * (wet[:, 0] + wet[:, 1]), 0.5 * (wet[:, 0] - wet[:, 1])
    return np.stack([mid_gain * mid + side_gain * side, mid_gain * mid - side_gain * side], axis=1)


def mix_gains(mix: float) -> tuple:
    """(dry gain, wet gain): the dry stays at unity up to 50 %, the wet reaches unity at 50 %."""
    return min(1.0, 2.0 * (1.0 - mix)), min(1.0, 2.0 * mix)


def output_clip(signal: np.ndarray) -> np.ndarray:
    """Memoryless, per sample and channel: unity up to +8 dBFS, a quadratic knee, the ceiling of +12 dBFS."""
    size = np.abs(signal)
    knee = size - (size - CLIP_THRESHOLD) ** 2 / (4.0 * (CLIP_CEILING - CLIP_THRESHOLD))
    return np.sign(signal) * np.where(size <= CLIP_THRESHOLD, size, np.where(size >= 2.0 * CLIP_CEILING - CLIP_THRESHOLD, CLIP_CEILING, knee))


def outer_shell(stimulus, wet: np.ndarray, sample_rate: int, width_scale: float, mix: float,
                level_stage: bool = True, clip: bool = True) -> np.ndarray:
    """The reference's output for its wet signal (raw time base, Width 100 %, in front of the level stage).

    The level stage turns the wet down by the reduction its input frame n asks for at output frame n plus the
    reported latency; then width, the mix with the input delayed by the reported latency, and the clipper.
    """
    stimulus = np.asarray(stimulus, dtype=np.float64)
    frames, latency = len(wet), converters.reported_latency(sample_rate)
    held = max(0, min(len(stimulus), frames - latency))
    dry = np.zeros((frames, 2))
    dry[latency:latency + held] = stimulus[:held]
    shaped = np.asarray(wet, dtype=np.float64)
    if level_stage:
        reduction = np.zeros(frames)
        reduction[latency:latency + held] = level_reduction_db(stimulus[:held], sample_rate)
        shaped = shaped * (10.0 ** (reduction / 20.0))[:, None]
    dry_gain, wet_gain = mix_gains(float(mix))
    result = dry_gain * dry + wet_gain * apply_width(shaped, float(width_scale))
    return output_clip(result) if clip else result


# ---------------------------------------------------------------- the whole chain

def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_scale, predelay_seconds, macro, width_scale, mix,
           phase=None, seed=None, level_stage=True, clip=True) -> np.ndarray:
    sample_rate = int(sample_rate)
    first_read, taps = voice_inputs(stimulus, sample_rate, warmup_seconds, decay_seconds, size_scale, predelay_seconds, macro)
    signal = wet(taps, first_read, sample_rate, warmup_seconds, len(stimulus), macro, phase, seed)
    return outer_shell(stimulus, signal, sample_rate, width_scale, mix, level_stage, clip)


def render_knobs(stimulus, sample_rate, warmup_seconds, settings: dict | None = None, phase=None, seed=None,
                 level_stage=True, clip=True) -> np.ndarray:
    """`render` at normalised host parameters: what `revocean.capture(stimulus, settings, ...)` returns."""
    return render(stimulus, sample_rate, warmup_seconds, **physical(settings), phase=phase, seed=seed,
                  level_stage=level_stage, clip=clip)
