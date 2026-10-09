#!/usr/bin/env python3
"""The complete model of Rev OCEAN in Tide mode, Macro 0 to 100 % (neutral baseline, Mix 100 %).

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro_percent, seed, phase=None) -> array (frames, 2)

is the reference's raw output (reported latency still in it) for a stereo
stimulus that starts `warmup_seconds` after the first processed sample. `seed`
selects one instance of the random voice phase; an instance of the reference
cannot be reproduced, only its statistics. `phase`, when given, replaces the
generator by a prescribed phase, a function of the time in seconds since the
first processed sample that returns the phase of voice A of both outputs in
cycles, shape (2, len(seconds)); score_tide.py uses it to null a reference
capture with the phase it measured in that capture. At Macro 0 the result is
network_model.render, sample for sample.

The model joins the campaign's parts. Three things are measured in this packet
(findings/tide_model.md): the line that separates the two voices, the place of
the comb behind the input equaliser, and the rate law of the phase.

    host in -> converter -> equaliser -> [ cos(90 deg m) + sin(90 deg m) comb ] -> 44 samples -> 2 x 16 lines, Hadamard loop
            -> per output:  lines 1 to 8 -> voice at phi,  lines 9 to 16 -> voice at phi + 1/2  -> sum -> converter -> host out

* converters.py         the two rate converters; everything between them runs at 44.1 kHz.
* tide_stage.py         the comb (one per input, behind the network's input equaliser and in front of its
                        cross feed) and the voice: a state-variable low-pass and a gain whose cut-off, Q
                        and gain are set once per 44 samples from the phase. It takes the place of the
                        network's fixed output low-pass.
* network_model.py      the network. Its equaliser, loop, taps and gains do not change with Macro.
* tide_phase.py         the level of the phase of each output: value noise on a jittered grid. The phase is
                        that level plus a ramp at `phase_rate(Macro)`, the rate law measured in this packet.

The output taps of lines 1 to 8 of a group, with their Macro 0 tap weights, pass the voice at the phase of
that group's output (voice A); those of lines 9 to 16 pass the same voice law half a cycle away (voice B).
A line's content goes to the voice of the line whatever input it came from.

For work at the internal rate the pieces are available one by one:

    first_read, driven = network_drive(stimulus, sample_rate, warmup_seconds, macro)      # what is written into the lines
    taps = line_taps(driven, first_read, decay_seconds, size_percent)                     # [frame][output][voice]
    internal = voices(taps, first_read, phase, macro)                                     # [frame][output]
    host = output(internal, first_read, sample_rate, start, stop)

(`voice_inputs` does the first two steps.) Sample k of every internal array is read count first_read + k.

Not in the model: the first 3 s of a fresh instance, in which the reference behaves as if Macro came from
about 50 %, a Macro that moves while playing, input peaks above 0.5, any control outside the neutral
baseline. The module renders no capture.
"""
from __future__ import annotations

import numpy as np
from scipy.signal import lfilter

import converters
import network_model
import tide_phase
import tide_stage

INTERNAL_RATE = network_model.INTERNAL_RATE
BLOCK = tide_stage.BLOCK                                   # samples between two updates of a voice
VOICE_LINES = (tuple(range(1, 9)), tuple(range(9, 17)))    # lines whose output taps pass voice A and voice B
VOICE_B_OFFSET = 0.5                                       # cycles between the two voices of an output
Q_RUN_IN = 150                                             # blocks in front of the first sample that start the Q smoothing
# rate of the phase in cycles/s: RATE_LOW at Macro 0, RATE_HIGH at Macro 100 %, on the vendor's Exp(RATE_SHAPE) map
RATE_LOW, RATE_HIGH, RATE_SHAPE = 0.0502375, 0.0564238, 0.63


def tide_input(stream: np.ndarray, first: int, macro: float) -> np.ndarray:
    """Per input channel the equal-power mix of a stream and its comb: cos(90 deg m) stream + sin(90 deg m) comb.

    `stream[k]` is internal sample `first + k`; the comb's delay counts from the first processed sample.
    """
    return np.stack([tide_stage.input_stage(stream[:, channel], first, channel, macro) for channel in (0, 1)], axis=1)


def network_drive(stimulus, sample_rate, warmup_seconds, macro: float) -> tuple:
    """(first_read, driven): the signal written into the lines; driven[k] is written at internal sample
    first_read + k, 44 samples behind the network input.

    Input converter, the network's input equaliser, the Tide input stage.
    """
    data = network_model.constants()
    first, internal = network_model.network_input(stimulus, sample_rate, warmup_seconds)
    return first + data["input"]["delay_samples"], tide_input(network_model.equalise(data, internal), first, macro)


def line_taps(driven: np.ndarray, first_read: int, decay_seconds, size_percent, groups=VOICE_LINES) -> np.ndarray:
    """taps[frame][output][group]: the output taps of groups of lines (line numbers 1 to 16), each line with
    its Macro 0 tap weight, in front of the voices. Decay and Size are display values."""
    data = network_model.constants()
    values = network_model.parameters(data, network_model.host_value(data["parameters"]["decay"], float(decay_seconds)),
                                      network_model.host_value(data["parameters"]["size"], float(size_percent)))
    mix = np.zeros((len(groups), network_model.LINES))
    for row, members in enumerate(groups):
        index = np.asarray(members, dtype=int) - 1
        mix[row, index] = values["tap"][index]
    return np.ascontiguousarray(network_model.run_core(values, driven, int(first_read), mix).transpose(0, 2, 1))


def voice_inputs(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro: float) -> tuple:
    """(first_read, taps): what the voices filter, taps[frame][output][voice], at the internal rate.

    `taps[k]` belongs to read count first_read + k, counted from the first processed sample.
    """
    first_read, driven = network_drive(stimulus, sample_rate, warmup_seconds, macro)
    return first_read, line_taps(driven, first_read, decay_seconds, size_percent)


def voice_parameters(phase: np.ndarray, macro: float) -> tuple:
    """(cut-off in Hz, Q, gain) of a voice for its phase at the start of consecutive blocks.

    Cut-off and gain follow the phase at once. Q follows its target through a one-pole per block with a
    time constant of 10 blocks, started at the first value given.
    """
    cutoff = tide_stage.cutoff(phase, macro)
    target = tide_stage.quality_target(cutoff, macro)
    step = 1.0 - np.exp(-1.0 / tide_stage.Q_SMOOTHING_BLOCKS)
    quality = lfilter([step], [1.0, step - 1.0], target, zi=[target[0] * (1.0 - step)])[0]
    return cutoff, quality, tide_stage.gain(phase, macro)


def voice(signal: np.ndarray, first_read: int, phase, macro: float) -> np.ndarray:
    """One voice on `signal` (sample k is read count first_read + k); `phase` maps seconds to cycles.

    A block starts where the read count is a multiple of 44; its parameters are those of the phase at
    that sample and are held for the block.
    """
    block = (int(first_read) + np.arange(len(signal))) // BLOCK
    first_block = int(block[0]) - Q_RUN_IN
    starts = np.arange(first_block, int(block[-1]) + 1) * BLOCK
    cutoff, quality, gain = voice_parameters(np.asarray(phase(starts / INTERNAL_RATE), dtype=np.float64), macro)
    pick = block - first_block
    return tide_stage.state_variable(signal, cutoff[pick], quality[pick], gain[pick])


def voices(taps: np.ndarray, first_read: int, phase, macro: float) -> np.ndarray:
    """Both voices of both outputs: taps[frame][output][voice] -> [frame][output].

    `phase(seconds)` returns the phase of voice A of both outputs, shape (2, len(seconds)).
    """
    result = np.zeros(taps.shape[:2])
    for group in (0, 1):
        for index, offset in enumerate((0.0, VOICE_B_OFFSET)):
            result[:, group] += voice(taps[:, group, index], first_read, lambda seconds: phase(seconds)[group] + offset, macro)
    return result


def output(internal: np.ndarray, first_read: int, sample_rate: int, start: int, stop: int) -> np.ndarray:
    """Host samples [start, stop) of the voices' output: the output converter (44 samples at a 44.1 kHz host)."""
    return np.stack([converters.to_host(internal[:, channel], int(sample_rate), int(first_read), start, stop) for channel in (0, 1)], axis=1)


def phase_rate(macro: float) -> float:
    """Rate of the voice phase in cycles per second: 0.0431885 + 0.0070490 e^(0.63 Macro)."""
    return RATE_LOW + (RATE_HIGH - RATE_LOW) * float(np.expm1(RATE_SHAPE * macro) / np.expm1(RATE_SHAPE))


def generator(seed: int, macro: float):
    """The free-running phase of one instance: phase(seconds) -> (2, len(seconds)), voice A of both outputs."""
    levels, rate = tide_phase.TidePhase(int(seed), macro).levels, phase_rate(macro)
    return lambda seconds: levels(seconds) + rate * np.asarray(seconds, dtype=np.float64)


def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro_percent, seed, phase=None) -> np.ndarray:
    macro = float(macro_percent) / 100.0
    if macro == 0.0:
        return network_model.render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent)
    first_read, taps = voice_inputs(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro)
    internal = voices(taps, first_read, generator(seed, macro) if phase is None else phase, macro)
    origin = int(round(warmup_seconds * sample_rate))
    return output(internal, first_read, sample_rate, origin, origin + len(stimulus))
