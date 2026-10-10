"""Foam as measured in session A: the base network (campaign model, oscillator origin of the session) fed with

    u'[m] = cos(pi/2 Macro) u[m] + sin(pi/2 Macro) F(u)[m]

u = output of the input converter (44.1 kHz), per channel.  F is the diffuser of foam_structure.py with the constants of
structure_final.json (28 all-passes in 7 stages of 4, Hadamard mixes), run on the signal itself: nothing is truncated.
`kernel="measured"` uses the measured kernel instead (kernel_long_x03_0.npy, 3.77 s, the solved input of one right-channel
impulse of x03 divided by the Macro 0 pulse, a13_longkernel.py); an array is used as a kernel.

    render(stimulus, first_frame, decay, size, macro, kernel=None, osc_origin=None, wet_gain_in_front=False)

macro: a number; or a list [initial, (host_frame, value), ...] of Macro changes (each at the first frame of a host block),
which move the two gains as the reference does (macro_gains); or a function of the internal index array -> (cos gain, sin gain)."""
from __future__ import annotations
import numpy as np
from scipy.signal import fftconvolve
from fcommon import *
import foam_structure as fs

_kernel = None
_structure = None


def measured_kernel():
    global _kernel
    if _kernel is None:
        _kernel = np.load(HERE / "kernel_long_x03_0.npy")     # 3.77 s; what lies behind it (about -74 dB of the energy, falling 29 dB/s) is missing
    return _kernel


def structure():
    global _structure
    if _structure is None:
        _structure = fs.load("structure_final.json")
    return _structure


def wet(u, kernel=None):
    """u (n, 2) internal -> F(u) (n, 2)."""
    if kernel is None:
        S = structure()
        return np.stack([fs.diffuse(u[:, c], S) for c in (0, 1)], axis=1)
    k = measured_kernel() if isinstance(kernel, str) else kernel
    return np.stack([fftconvolve(u[:, c], k)[:len(u)] for c in (0, 1)], axis=1)


BLOCK = 44                          # the gains change once per block of 44 internal samples
BLOCK_PHASE = 43                    # a block starts at internal index m = 43 (mod 44), numbering of converters.to_internal
SMOOTH = float(np.exp(-44.0 / 441.0))   # per block: gain += (target - gain) (1 - SMOOTH): a one-pole of 10 ms run at the block rate
# When a change given at host frame h (first frame of a host call) is first used: by the first block of 44 that starts
# later than  b - L,  b = internal_time_of_host(h),  L = LEAD0 + (147/160) ((h - CHUNK_PHASE) mod 48).
# Measured: L in (111.56, 115.56] for h = 0 (mod 48) (session A, ten changes), L in (82.76, 90.76] for h = 16 (mod 48)
# (session T90, 89 changes).  Read as: the reference converts in chunks of 48 host frames on a grid of its own, and a block
# takes the value of the host call in which its chunk is completed.  The two alignments allow CHUNK_PHASE 1..16 with
# LEAD0 moving along; 16 and 84.5 are one choice that reproduces both sessions.  Host mechanics, not part of the effect.
CHUNK = 48
CHUNK_PHASE = 16
LEAD0 = 84.5


def first_block(frame):
    """Internal index of the first block of 44 that uses a value set at host frame `frame`."""
    lead = LEAD0 + (147.0 / 160.0) * ((int(frame) - CHUNK_PHASE) % CHUNK)
    lowest = int(np.floor(internal_time_of_host(frame) - lead)) + 1            # smallest integer > b - L
    return lowest + (BLOCK_PHASE - lowest) % BLOCK


def macro_gains(index, changes):
    """(cos gain, sin gain) per internal sample for index (absolute internal indices, ascending) and
    changes = [initial_macro, (host_frame, macro), ...]."""
    index = np.asarray(index)
    cg = np.full(len(index), np.cos(0.5 * np.pi * changes[0])); sg = np.full(len(index), np.sin(0.5 * np.pi * changes[0]))
    for frame, value in changes[1:]:
        j0 = first_block(frame)
        a = int(j0 - index[0])
        if a >= len(index):
            continue
        a = max(a, 0)
        k = (index[a:] - j0) // BLOCK + 1
        for arr, target in ((cg, np.cos(0.5 * np.pi * value)), (sg, np.sin(0.5 * np.pi * value))):
            state = arr[a - 1] if a > 0 else arr[0]
            arr[a:] = target + (state - target) * SMOOTH ** k
    return cg, sg


def foam_input(u, macro, kernel=None, wet_gain_in_front=False):
    """u (n, 2) internal -> u' (n, 2).  macro: scalar, or (cos gain, sin gain) arrays.  The wet gain acts behind the diffuser."""
    if isinstance(macro, tuple):
        cg, sg = macro[0][:, None], macro[1][:, None]
    else:
        cg, sg = np.cos(0.5 * np.pi * float(macro)), np.sin(0.5 * np.pi * float(macro))
    if wet_gain_in_front:
        return cg * u + wet(sg * u, kernel)
    return cg * u + sg * wet(u, kernel)


def render(stimulus, first_frame, decay, size, macro, kernel=None, osc_origin=None, wet_gain_in_front=False):
    stimulus = np.asarray(stimulus, float)
    first, u = to_internal(stimulus, first_frame)
    if callable(macro):
        macro = macro(first + np.arange(len(u)))
    elif isinstance(macro, (list, tuple)):
        macro = macro_gains(first + np.arange(len(u)), list(macro))
    up = foam_input(u, macro, kernel, wet_gain_in_front)
    return response_to_internal(up, first, decay, size, first_frame, first_frame + len(stimulus), osc_origin)
