#!/usr/bin/env python3
"""Structural model of Rev OCEAN in Macro Mode Abyss (black box; built on session E of 8 October 2026).

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro, context=None) -> output

float32 stereo in, raw output (reported latency inside), like network_model.render. `macro` is 0..1 (a number, or
one value per host frame of the stimulus). At macro 0 the result is the campaign's base model (with the oscillator
origin of the context).

    context = {
        "oscillator_origin": int,   # PER SESSION. At-lines internal index (44.1 kHz samples from the instance's
                                    # first sample; network-input index + 44) at which the last change of mode
                                    # restarted the 32 line oscillators. The three grain phasors of the layer
                                    # restart with it. Session E: 497448424, session D: 31574180. Default 0.
        "preceding": array (n, 2),  # optional: host audio processed immediately before the stimulus (no gap)
        "preceding_macro": float or array (n,),   # Macro while `preceding` was processed (default: macro[0])
        "late_chunks": {"R": [k, ...], "U": [...], "D": [...]},
                                    # optional, PER RECORDING: chunk indices of a reversed reader whose boundary
                                    # the reference places one single-precision step of the time later (see
                                    # CONSTANTS["late_ulp"] and model_structural.md, section "late chunks").
                                    # None were found in session E. Not derivable by this model.
        "late_rule": "block_grid",  # optional: fill "late_chunks" by the empirical rule of late_chunks_block_grid
                                    # (read from session D's PROBES; needs the host's block grid: blocks of
                                    # "block_size" (512) frames counted from "block_origin_frame" (default: the first
                                    # frame of the stimulus, which is how the campaign's session host runs a step)
        "constants": {...},         # overrides of CONSTANTS (top level keys), for the fitting scripts

        # ---- added in analysis3/tempo (9 October 2026): the chunk clock as it is read from the host.
        # Without "clock" the module is the one of analysis2, bit for bit (120 BPM, play head = processed frames).
        "clock": "host",            # the three chunk clocks follow the host's play head (hostclock.py). No table of late
                                    # chunks: they come out of the single-precision arithmetic by themselves.
        "bpm": 120.0,               # tempo the host reports
        "playhead_offset_seconds": 0.0,   # reported play-head position minus processed time (T90: 0.25)
        "host_block": 512,          # frames per host block
        "steps": [frame, ...],      # absolute processed frames at which the host began a new run of blocks (the campaign's
                                    # session host starts a grid of its own with every job: flush, pre-roll, recording).
                                    # Default: one run from the first frame of the stimulus.
        "playing": True,            # False: the host reports a stopped transport. The layer then counts internal samples
        "free_run_origin": int,     #   from this network-input index (default: oscillator_origin, as measured on S90).
    }

Structure (everything at the internal rate of 44.1 kHz; m = internal sample index counted from the instance's
first sample, in the numbering of converters.to_internal; x = network input of one channel):

    host in -> converter -> x --+------------------------------------------------(+)-> equaliser -> 44 samples -> base network -> low-pass -> converter -> out
                                |                                                 ^
                                +-> reversed reader, chunk fl32(2/3) s -----------| gR(Macro)       "unison"
                                +-> reversed reader, chunk 1 s -> comb 1 s -> gU(Macro) -> grain reader up -----|   "octave up"
                                +-> reversed reader, chunk fl32(4/3) s ----> gD(Macro) -> grain reader down ----|   "octave down"

* Left and right are processed separately (left voices feed the left network input only). The base path has gain 1
  at every Macro; the voices are added. Buffers, comb and phasors run at every Macro; Macro only scales.
* Reversed reader with chunk length T (seconds, single precision): chunk k has
      b = 44100 * fl32(T) * k,  tick = floor(b / G),  o = b - G * tick,   G = 44100 / 1024
      s = 44 * ceil((G * tick - 42) / 44),   X = 2 s + o + floor(o) - 2
  and delivers  y[m] = w(m - X / 2) * lin(x, X - m)  from m = ceil(X / 2) until the next chunk takes over
  (lin = linear interpolation; w = raised-cosine fade in and out, per voice). These clocks count from the
  instance's first sample and do NOT restart at the mode switch.
* Comb of the octave-up voice: y[m] += 0.198 * y[m - 44100], in front of the grain reader.
* Grain reader: a single-precision phasor p (use p; p += inc; if (p >= 1) p -= 1), equal to 0.5 at internal sample
  oscillator_origin - 2, i.e. restarted by the mode switch together with the base oscillators. Two taps with phases
  p and p + 0.5; a tap with phase q has window sin(pi q) and delay dmin + W q (down) or dmin + W (1 - q) (up),
  read with linear interpolation.
* Macro: three clamped straight ramps, smoothed by a one-pole; the gains of the octave voices act on the streams in
  front of their grain readers.

Every constant is in CONSTANTS with its status (exact / fitted / guessed); model_structural.md has the evidence.
"""
from __future__ import annotations

import math

import numpy as np

import base as B
from base import cv, nm
import hostclock as HC

INTERNAL_RATE = 44100
TICK = INTERNAL_RATE / 1024.0            # 43.06640625 samples (1/1024 s)
BLOCK = 44


def _single(bits: int) -> float:
    return float(np.array([bits], np.uint32).view(np.float32)[0])


CONSTANTS = {
    # ---- reversed readers (mirror law: exact, analysis2/clock.verification.md; checked on x02/x03)
    "mirror_a": 42.0,                    # exact within [41.645, 42.355)
    "chunk_seconds": {"R": float(np.float32(2.0 / 3.0)), "U": 1.0, "D": float(np.float32(4.0 / 3.0))},      # exact
    # fades over the position p = m - X / 2 behind the mirror point (samples): raised cosine over the first `fade_in`
    # samples, raised cosine over the last `fade_out` samples in front of `end`, zero behind `end`           (fitted)
    "window": {
        "R": {"fade_in": 6036.5, "fade_out": 2075.5, "end": 29400.05},
        "U": {"fade_in": 12545.78, "fade_out": 5794.48, "end": 44099.96},
        "D": {"fade_in": 22270.67, "fade_out": 9006.18, "end": 58798.07},
    },
    # a "late" chunk (context["late_chunks"]): o is larger by this many single-precision steps of the time in seconds
    "late_ulp": 1.0,
    # ---- recirculation of the octave-up voice
    "comb_delay": 44100,                 # exact (second-pass clicks: 44100.00 +- 0.02)
    "comb_gain": 0.198,                  # fitted (ring-out 0.1980; clicks 0.1977 / 0.1985)
    # ---- grain readers: inc = single-precision phasor increment, W = delay sweep, dmin = shortest delay  (fitted)
    "phasor_reset_offset": -2,           # phasors are at `phasor_start` at oscillator_origin - 2
    "phasor_start": 0.5,
    "reader": {
        "D": {"inc": _single(0x38C5C3E0), "W": 5313.13373, "dmin": 45.58356, "direction": 0},
        "UL": {"inc": _single(0x39458B10), "W": 5268.90989, "dmin": 44.52724, "direction": 1},
        "UR": {"inc": _single(0x3946A910), "W": 5346.74391, "dmin": 44.53402, "direction": 1},
    },
    # ---- voice gains at full strength and the Macro ramps (clamped straight lines: foot, knee)
    "gain": {"R": 0.6927952, "UL": 0.487907, "UR": 0.487907, "D": 0.707228},
    "ramp": {"R": (0.0, 0.3), "U": (0.1461, 0.4596), "D": (0.6041, 1.0)},
    "macro_smoothing_ms": 10.0,          # fitted on x05: one pole on the Macro value, per internal sample
    "macro_lead": 44,                    # a change of Macro acts this many samples early in this numbering (one block)
    "position_rounding": "exact",        # trials of a single-precision read position ("nearest", "product"): no gain, see report
    # ---- host clock (context["clock"] == "host"; analysis3/tempo.md). Constants given above in samples are the values
    #      at 120 BPM; what follows the tempo is multiplied by 120 / bpm.
    "host_mirror_a": HC.MIRROR_A,        # block offset of the host clock law (fitted window [42.10, 42.35))
    "tempo_scales": {"window": True, "comb": True},     # chunk windows: fractions of the chunk; comb delay: one chunk of the octave-up reader (measured on T90)
}


def constants(context: dict | None = None) -> dict:
    c = dict(CONSTANTS)
    if context and context.get("constants"):
        c.update(context["constants"])
    return c


# ---------------------------------------------------------------- reversed readers

def time_ulp_samples(b: float) -> float:
    """One single-precision step of the time in seconds at internal sample b, in samples."""
    seconds = max(b / INTERNAL_RATE, 1e-9)
    return INTERNAL_RATE * 2.0 ** (math.floor(math.log2(seconds)) - 23)


def mirror_x(k, chunk_seconds: float, a: float | None = None, late=None, late_ulp: float = 1.0):
    """X of chunk k: output index + read index of its reversed read (the mirror point is X / 2).
    `late`: boolean (array) marking chunks whose o is one single-precision time step larger."""
    a = CONSTANTS["mirror_a"] if a is None else a
    b = INTERNAL_RATE * chunk_seconds * np.asarray(k, dtype=np.float64)
    tick = np.floor(b / TICK)
    o = b - TICK * tick
    s = BLOCK * np.ceil((TICK * tick - a) / BLOCK)
    if late is not None:
        step = late_ulp * np.vectorize(time_ulp_samples)(np.maximum(b, 1.0))
        o = o + np.where(late, step, 0.0)
    return 2.0 * s + o + np.floor(o) - 2.0


def chunk_window(position: np.ndarray, w: dict) -> np.ndarray:
    out = np.ones_like(position, dtype=np.float64)
    lead = position < w["fade_in"]
    out[lead] = 0.5 - 0.5 * np.cos(np.pi * np.clip(position[lead], 0.0, None) / w["fade_in"])
    tail = position > w["end"] - w["fade_out"]
    out[tail] = 0.5 + 0.5 * np.cos(np.pi * np.clip((position[tail] - (w["end"] - w["fade_out"])) / w["fade_out"], 0.0, 1.0))
    return out


def reversed_stream(x: np.ndarray, first: int, voice: str, c: dict | None = None, late_chunks=(), weight=None,
                    schedule: dict | None = None, scale: float = 1.0) -> np.ndarray:
    """The reversed, faded stream of one channel. x[0], result[0] are internal sample `first`; x counts as zero outside.
    `weight(position)` replaces the fade (calibration). `schedule` (hostclock.chunk_schedule) replaces the mirror law of
    analysis2; `scale` (120 / bpm) stretches the chunk and, if the constants say so, its window."""
    c = CONSTANTS if c is None else c
    chunk = c["chunk_seconds"][voice]
    window = c["window"][voice]
    if scale != 1.0 and c.get("tempo_scales", {}).get("window", True):
        window = {key: value * scale for key, value in window.items()}
    late_set = set(int(k) for k in late_chunks)
    n = len(x)
    y = np.zeros(n)
    length = INTERNAL_RATE * chunk * scale
    if schedule is not None:
        X = np.append(np.asarray(schedule["X"], dtype=np.float64), 2.0 * (first + n) + 4.0 * length)      # sentinel behind the end
        ks = np.append(np.asarray(schedule["k"]), -1)
    else:
        ks = np.arange(int(math.floor(first / length)) - 2, int(math.ceil((first + n) / length)) + 2)
        late = np.array([int(k) in late_set for k in ks]) if late_set else None
        X = mirror_x(ks, chunk, c["mirror_a"], late, c["late_ulp"])
    starts = np.ceil(X / 2.0).astype(np.int64)
    for i in range(len(ks) - 1):
        lo, hi = max(int(starts[i]), first), min(int(starts[i + 1]), first + n)
        if hi <= lo:
            continue
        m = np.arange(lo, hi, dtype=np.float64)
        position = X[i] - m
        mode = c.get("position_rounding", "exact")
        if mode != "exact":                 # trial: the position behind the mirror point held in single precision
            half = X[i] / 2.0
            if mode == "nearest":
                position = half - (m - half).astype(np.float32).astype(np.float64)
            elif mode == "product":         # position = fl32(fl32(phase) * length), phase = position / length
                length32 = np.float32(length)
                position = half - ((m - half) / length).astype(np.float32) * length32
                position = position.astype(np.float64)
        whole = np.floor(position)
        fraction = position - whole
        index = whole.astype(np.int64) - first
        inside = (index >= 0) & (index + 1 < n)
        index = np.clip(index, 0, n - 2)
        value = np.where(inside, (1.0 - fraction) * x[index] + fraction * x[index + 1], 0.0)
        p = m - X[i] / 2.0
        y[lo - first: hi - first] = (chunk_window(p, window) if weight is None else weight(p)) * value
    return y


# ---------------------------------------------------------------- grain readers and comb

def grain_reader(y: np.ndarray, first: int, key: str, origin: int, c: dict | None = None, shift: float = 0.0) -> np.ndarray:
    """The two-grain reader `key` ("D", "UL", "UR") on the stream y (y[0] = internal sample `first`)."""
    c = CONSTANTS if c is None else c
    r = c["reader"][key]
    y = np.ascontiguousarray(y, dtype=np.float64)
    out = np.zeros(len(y))
    reset = int(origin) + int(c["phasor_reset_offset"])
    if first < reset:
        raise ValueError("the stimulus starts before the mode switch: the Abyss layer is not defined there")
    B.library().abyss_reader(len(y), int(first), y, reset, float(c["phasor_start"]), float(r["inc"]), float(r["W"]),
                             float(r["dmin"]), int(r["direction"]), float(shift), float(r.get("wscale", 1.0)), float(r.get("woff", 0.0)), out)
    return out


def comb(y: np.ndarray, c: dict | None = None, scale: float = 1.0) -> np.ndarray:
    c = CONSTANTS if c is None else c
    y = np.ascontiguousarray(y, dtype=np.float64).copy()
    delay = int(c["comb_delay"])
    if scale != 1.0 and c.get("tempo_scales", {}).get("comb", True):
        delay = int(round(delay * scale))
    B.library().abyss_comb(len(y), 1, delay, float(c["comb_gain"]), y)
    return y


# ---------------------------------------------------------------- Macro

def macro_at_internal(macro: np.ndarray, sample_rate: int, begin: int, first: int, count: int, c: dict | None = None):
    """Macro per internal sample first .. first + count - 1: the value of the host frame in conversion `macro_lead`
    samples later, smoothed by a one-pole. macro[0] belongs to absolute host frame `begin`. A constant stays a number."""
    c = CONSTANTS if c is None else c
    if np.all(macro == macro[0]):
        return float(macro[0])
    from scipy.signal import lfilter
    index = first + np.arange(count) + int(c["macro_lead"])
    host = np.floor(index * (sample_rate / INTERNAL_RATE)).astype(np.int64) - begin
    stepped = macro[np.clip(host, 0, len(macro) - 1)]
    pole = math.exp(-1.0 / (c["macro_smoothing_ms"] * 1e-3 * INTERNAL_RATE))
    return lfilter([1.0 - pole], [1.0, -pole], stepped - stepped[0]) + stepped[0]


def ramp(macro, foot: float, knee: float):
    return np.clip((np.asarray(macro, dtype=np.float64) - foot) / (knee - foot), 0.0, 1.0)


def voice_gains(macro, channel: int, c: dict | None = None) -> tuple:
    """(gR, gU, gD) of one channel at Macro 0..1 (scalars or arrays)."""
    c = CONSTANTS if c is None else c
    g, r = c["gain"], c["ramp"]
    return (g["R"] * ramp(macro, *r["R"]), g["UL" if channel == 0 else "UR"] * ramp(macro, *r["U"]), g["D"] * ramp(macro, *r["D"]))


# ---------------------------------------------------------------- the layer

def voices(x: np.ndarray, first: int, channel: int, origin: int, gains=(1.0, 1.0, 1.0), c: dict | None = None, late: dict | None = None,
           schedules: dict | None = None, scale: float = 1.0) -> tuple:
    """(unison, octave up, octave down) of one channel. `gains` (numbers or one value per internal sample) are the
    Macro gains; those of the octave voices are applied to the streams in front of the grain readers.
    `schedules` ({"R", "U", "D"}: hostclock.chunk_schedule) and `scale` (120 / bpm): the host clock."""
    c = CONSTANTS if c is None else c
    late = {} if late is None else late
    sch = {} if schedules is None else schedules
    g_r, g_u, g_d = gains
    key = "UL" if channel == 0 else "UR"
    zero = np.zeros(len(x))
    unison = g_r * reversed_stream(x, first, "R", c, late.get("R", ()), None, sch.get("R"), scale) if np.any(np.asarray(g_r) != 0) else zero
    up = grain_reader(g_u * comb(reversed_stream(x, first, "U", c, late.get("U", ()), None, sch.get("U"), scale), c, scale), first, key, origin, c) if np.any(np.asarray(g_u) != 0) else zero
    down = grain_reader(g_d * reversed_stream(x, first, "D", c, late.get("D", ()), None, sch.get("D"), scale), first, "D", origin, c) if np.any(np.asarray(g_d) != 0) else zero
    return unison, up, down


def layer(internal: np.ndarray, first: int, macro_internal, origin: int, c: dict | None = None, late: dict | None = None,
          schedules: dict | None = None, scale: float = 1.0) -> np.ndarray:
    """What Abyss adds to the network input: (frames, 2). `macro_internal` is a number or one value per internal sample."""
    c = CONSTANTS if c is None else c
    out = np.zeros_like(internal)
    if np.all(np.asarray(macro_internal) <= 0.0):
        return out
    for channel in (0, 1):
        x = np.ascontiguousarray(internal[:, channel])
        if not np.any(x):
            continue
        unison, up, down = voices(x, first, channel, origin, voice_gains(macro_internal, channel, c), c, late, schedules, scale)
        out[:, channel] = unison + up + down
    return out


def host_schedules(first: int, count: int, sample_rate: int, start: int, context: dict, c: dict | None = None) -> tuple:
    """({"R", "U", "D"}: schedule, scale) of the host clock for internal samples first .. first + count.
    Context: "bpm" (120), "playhead_offset_seconds" (0: the play head reports the processed frames), "host_block" (512),
    "steps" (absolute processed frames at which the host began a new run of blocks; default: the stimulus alone),
    "playing" (True); when False: "stopped_position_seconds" and "free_run_origin_frame" (section 3 of tempo.md)."""
    c = CONSTANTS if c is None else c
    bpm = float(context.get("bpm", 120.0))
    steps = list(context.get("steps", [start]))
    block = int(context.get("host_block", 512))
    offset = float(context.get("playhead_offset_seconds", 0.0))
    out = {}
    notes = dict(HC.NOTE_QUARTERS, **c.get("note_quarters", {}))
    for voice in ("R", "U", "D"):
        if context.get("playing", True):
            out[voice] = HC.chunk_schedule(voice, first, count, steps, offset, bpm, int(sample_rate), block, float(c["host_mirror_a"]), note=notes[voice])
        else:
            out[voice] = HC.stopped_schedule(voice, first, count, context, bpm, int(sample_rate), float(c["host_mirror_a"]))
    return out, 120.0 / bpm


# ---------------------------------------------------------------- late chunks (empirical, see model_structural.md)

# (exponent of the time in seconds, host block index modulo 375): a boundary in the middle of such a host block is late.
# READ FROM SESSION D's PROBES (not from session E, where no late chunk exists): 512..1024 s and 2048..4096 s only.
LATE_BLOCKS = {9: 62, 11: 312}


def late_chunks_block_grid(first: int, count: int, sample_rate: int, grid_origin_frame: int, block_size: int = 512, c: dict | None = None) -> dict:
    """{"R": [...], "U": [...], "D": [...]}: chunks whose boundary the reference places one single-precision step of
    the time in seconds late, by the empirical rule of LATE_BLOCKS: the boundary lies in the middle (0.4..0.6) of a
    host block whose index, counted from the start of the step that is being processed (`grid_origin_frame`, blocks of
    `block_size` frames), is LATE_BLOCKS[exponent] modulo 375 (375 blocks of 512 frames at 48 kHz are 4 s)."""
    c = CONSTANTS if c is None else c
    out = {}
    period = int(round(4.0 * sample_rate / block_size))
    for voice in ("R", "U", "D"):
        seconds = c["chunk_seconds"][voice]
        ks = np.arange(int(math.floor(first / (INTERNAL_RATE * seconds))) - 1, int(math.ceil((first + count) / (INTERNAL_RATE * seconds))) + 2)
        late = []
        for k in ks:
            b = seconds * float(k)
            if b <= 0.0:
                continue
            residue = LATE_BLOCKS.get(int(math.floor(math.log2(b))))
            if residue is None:
                continue
            blocks = (b * sample_rate - grid_origin_frame) / block_size
            if blocks < 0.0:
                continue
            j = math.floor(blocks)
            if j % period == residue and 0.4 < blocks - j < 0.6:
                late.append(int(k))
        out[voice] = late
    return out


SESSION_F = {"oscillator_origin": 1323432, "clock": "host", "bpm": 120.0}                                    # + "steps" per recording
SESSION_T90 = {"oscillator_origin": 2286108, "clock": "host", "bpm": 90.0, "playhead_offset_seconds": 0.25}      # + "steps"
SESSION_S90 = {"oscillator_origin": 1147036, "clock": "host", "bpm": 90.0, "playing": False, "free_run_origin": 1147036}
SESSION_D_HOST = {"oscillator_origin": 31574180, "clock": "host", "bpm": 120.0}                              # + "steps"; replaces the late-chunk table
SESSION_E_HOST = {"oscillator_origin": 497448424, "clock": "host", "bpm": 120.0}                             # + "steps"
SESSION_E = {"oscillator_origin": 497448424}
SESSION_D_STRICT = {"oscillator_origin": 31574180}                                  # nothing from session D but the anchor
SESSION_D = {"oscillator_origin": 31574180, "late_rule": "block_grid"}              # plus the late chunks seen in D's probes


# ---------------------------------------------------------------- render

def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro, context=None) -> np.ndarray:
    context = {} if context is None else context
    c = constants(context)
    origin = int(context.get("oscillator_origin", 0))
    stimulus = np.asarray(stimulus, dtype=np.float64)
    frames = len(stimulus)
    start = int(round(warmup_seconds * sample_rate))
    macro = np.broadcast_to(np.asarray(macro, dtype=np.float64), (frames,))
    before = context.get("preceding")
    if before is not None and len(before):
        before = np.asarray(before, dtype=np.float64)
        macro_before = np.broadcast_to(np.asarray(context.get("preceding_macro", macro[0]), dtype=np.float64), (len(before),))
        stimulus = np.concatenate([before, stimulus])
        macro = np.concatenate([macro_before, macro])
    begin = start + frames - len(stimulus)                      # absolute host frame of stimulus[0]
    data = nm.constants()
    values = B.values_for(decay_seconds, size_percent, data)
    first, internal = B.to_internal(stimulus, sample_rate, begin)
    if np.any(macro > 0.0):
        macro_internal = macro_at_internal(macro, int(sample_rate), begin, first, len(internal), c)
        late = context.get("late_chunks")
        if late is None and context.get("late_rule") == "block_grid":
            late = late_chunks_block_grid(first, len(internal), int(sample_rate), int(context.get("block_origin_frame", start)),
                                          int(context.get("block_size", 512)), c)
        if context.get("clock") == "host":                    # analysis3/tempo.md: the chunk clock read from the host
            schedules, scale = host_schedules(first, len(internal), int(sample_rate), start, context, c)
            internal = internal + layer(internal, first, macro_internal, origin, c, None, schedules, scale)
        else:                                                 # analysis2: 120 BPM, play head = processed frames (unchanged)
            internal = internal + layer(internal, first, macro_internal, origin, c, late)
    driven = nm.equalise(data, internal)
    taps = B.core(driven, first + data["input"]["delay_samples"], origin, values)
    return B.to_host(taps, first, sample_rate, start, start + frames, data)
