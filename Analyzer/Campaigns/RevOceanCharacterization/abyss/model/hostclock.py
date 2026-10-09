#!/usr/bin/env python3
"""The chunk clock of Abyss as it follows the host (analysis3/tempo.md, sections 2 and 3).

What the layer reads (measured on sessions D, E, F at 120 BPM and T90 at 90 BPM):

* Once per host block: the play head in QUARTER NOTES, converted to single precision:  p0 = fl32(ppq at block start).
* Its converter works in blocks of 48 host frames (1 ms at 48 kHz) that run through from the instance's first
  processed frame. Each such "tick" gets a position
        stamp = fl32( p0 + fl32(d * bpm / (60 * rate)) )
  p0 of the host block in which the tick's first frame arrived, d = that frame's offset inside the host block.
* A reversed reader with chunk length L quarter notes (single precision: fl32(4/3), 2, fl32(8/3)) has its boundaries
  at k * L exactly. In the tick whose stamp lies less than 43 samples in front of the boundary (the switch needs a
  sample of the 44-sample block behind the boundary) the new chunk starts with the offset
  A = (k L - stamp) * 60 / bpm * 44100  samples; a boundary that was not caught that way is taken in the next tick,
  with A <= 0 (the octave-up reader at 120 BPM always: its boundaries are ticks; the others when the stamp of
  the tick before was a single-precision step low and the offset no longer fitted).
* The chunk starts in the first internal block (44 samples) that begins at or after 44.1 m - a (m = number of the
  tick, a = 42.2; internal samples counted from the first processed frame), at s, with
        X = 2 s - 2 + A + I,   I = floor(A) for A >= 0, -1 for A < 0      (same-pitch and octave-down readers)
                               I = ceil(A)  for A >= 0,  0 for A < 0      (octave-up reader; A < 0 is a guess)
  (X = output index + read index of the reversed read; the old law is the case A = o, I = floor(o)).

At 120 BPM with the play head on the frame count this is the mirror law of analysis2 (tick 1/1024 s, chunk fl32(2/3) s)
wherever the single-precision position has room for it, and it yields the "late chunks" of session D by itself.
"""
from __future__ import annotations

import math

import numpy as np

f32 = np.float32
INTERNAL_RATE = 44100
BLOCK = 44
MIRROR_A = 42.2            # fitted window [42.10, 42.35): E x02/x03, F s3/s5, T90 t04
NOTE_QUARTERS = {"R": float(f32(4.0 / 3.0)), "U": 2.0, "D": float(f32(8.0 / 3.0))}


def tick_frames(sample_rate: int) -> int:
    """Host frames per converter block (the reported latency: 48 at 48 kHz)."""
    return 4 * int(math.floor(11.0 * sample_rate / 44100.0 + 0.5))


def stamps(m_lo: int, m_hi: int, steps, offset_frames: int, bpm: float, sample_rate: int = 48000, block: int = 512,
           variant: str = "start") -> np.ndarray:
    """Single-precision quarter-note position of the ticks m_lo .. m_hi - 1 (as float64 values).
    steps: sorted absolute processed frames at which the host started a new run of blocks (every job of the session
    host starts its own grid of `block` frames). offset_frames: reported play-head frame minus processed frame."""
    sub = tick_frames(sample_rate)
    steps = np.asarray(sorted(steps), dtype=np.int64)
    m = np.arange(m_lo, m_hi, dtype=np.int64)
    frame = sub * m
    anchor = frame if variant == "start" else frame + sub - 1          # frame whose host block gives p0
    idx = np.clip(np.searchsorted(steps, anchor, side="right") - 1, 0, len(steps) - 1)
    origin = steps[idx]
    start = origin + block * np.floor_divide(anchor - origin, block)   # processed frame of the host block start
    seconds = (start + offset_frames).astype(np.float64) / float(sample_rate)      # Session.cpp: reported / sampleRate
    quarters = seconds * bpm / 60.0                                                 # seconds * bpm / 60
    p0 = quarters.astype(np.float32)
    dq = ((frame - start).astype(np.float64) * (bpm / 60.0 / float(sample_rate))).astype(np.float32)
    return (p0 + dq).astype(np.float64)                                              # float32 + float32 -> float32


def schedule(note_quarters: float, stamp: np.ndarray, m_lo: int, bpm: float, sample_rate: int = 48000, a: float = MIRROR_A,
             window: float = float(BLOCK - 1), ceiling: bool = False) -> dict:
    """Chunks started inside the ticks m_lo ... : {"k", "X", "m", "A", "I", "s"} (arrays, in order of start).
    ceiling: the octave-up reader takes I = ceil(A) (0 for A < 0) where the other two take floor(A) (-1 for A < 0)."""
    L = float(note_quarters)
    spq = 60.0 / bpm * INTERNAL_RATE
    per_tick = tick_frames(sample_rate) * INTERNAL_RATE / float(sample_rate)        # internal samples per tick (44.1)
    out = {key: [] for key in ("k", "X", "m", "A", "I", "s")}
    last = None
    for i, sigma in enumerate(stamp):
        k = int(sigma // L)
        while (k + 1) * L <= sigma:
            k += 1
        while k * L > sigma:
            k -= 1
        if last is None:
            last = k
            continue
        m = m_lo + i
        started = None
        if k > last:                                    # a boundary lies behind this tick and was not caught
            A = (k * L - sigma) * spq
            started = (k, A, (int(math.ceil(A)) if A >= 0.0 else 0) if ceiling else (int(math.floor(A)) if A >= 0.0 else -1))
        else:
            A = ((k + 1) * L - sigma) * spq
            if A < window and k + 1 > last:
                started = (k + 1, A, int(math.ceil(A)) if ceiling else int(math.floor(A)))
        if started is not None:
            kk, A, I = started
            s = BLOCK * math.ceil((per_tick * m - a) / BLOCK)
            out["k"].append(kk); out["A"].append(A); out["I"].append(I); out["m"].append(m); out["s"].append(s)
            out["X"].append(2.0 * s - 2.0 + A + I)
            last = kk
    return {key: np.asarray(value) for key, value in out.items()}


def chunk_schedule(voice: str, first: int, count: int, steps, offset_seconds: float, bpm: float, sample_rate: int = 48000,
                   block: int = 512, a: float = MIRROR_A, history_chunks: float = 3.0, note: float | None = None) -> dict:
    """Schedule of the reversed reader `voice` ("R", "U", "D") covering internal samples first .. first + count,
    with `history_chunks` chunks of lead."""
    L = NOTE_QUARTERS[voice] if note is None else float(note)
    per_tick = tick_frames(sample_rate) * INTERNAL_RATE / float(sample_rate)
    chunk_samples = L * 60.0 / bpm * INTERNAL_RATE
    m_lo = max(int(math.floor((first - history_chunks * chunk_samples) / per_tick)) - 2, 0)
    m_hi = int(math.ceil((first + count) / per_tick)) + 4
    offset_frames = int(round(offset_seconds * sample_rate))
    st = stamps(m_lo, m_hi, steps, offset_frames, bpm, sample_rate, block)
    return schedule(L, st, m_lo, bpm, sample_rate, a, ceiling=(voice == "U"))


# ---------------------------------------------------------------- transport stopped (session S90)

def stopped_chunk_samples(voice: str, bpm: float) -> float:
    """Chunk length in internal samples while the host says "not playing": the same single-precision note lengths as
    with a running transport (fl32(4/3), 2, fl32(8/3) quarter notes). Measured on S90: the octave-up reader 58800
    exactly (a chunk of 58800.0018, the millisecond form, costs 15 dB); the same-pitch reader 39200.00117 per chunk
    over the first 323 chunks and 39200.00120 over the next 540 (not perfectly even: fl32(4/3) quarters gives
    39200.001168, fl32(8000/9) ms 39200.001196; the mirror points stay within 0.02 sample of either)."""
    return NOTE_QUARTERS[voice] * 60.0 / bpm * INTERNAL_RATE


def free_run_drift(quarters: float) -> float:
    """Samples by which the free-running position (transport stopped) lags the exact count. FITTED on S90's same-pitch
    reader (t20_s90drift.py): nothing up to 512 quarter notes, +4.93e-5 sample per quarter note from 512 to 1024,
    -4.93e-5 from 1024 to the last copy read (1150). The sign changes where the single-precision step of the
    position doubles; the arithmetic behind it is not known. Beyond 1150 quarter notes: a guess (alternating)."""
    rate = 4.93e-5
    if quarters < 512.0:
        return 0.0
    total, lo, sign = 0.0, 512.0, 1.0
    while quarters >= 2.0 * lo:
        total += sign * rate * lo
        lo, sign = 2.0 * lo, -sign
    return total + sign * rate * (quarters - lo)


def stopped_schedule(voice: str, first: int, count: int, context: dict, bpm: float, sample_rate: int = 48000, a: float = MIRROR_A) -> dict:
    """Transport stopped: the position does not come from the host. It counts internal samples from zero at
    context["free_run_origin"] (network-input index; in S90 the oscillator origin, i.e. the mode switch, to the sample)
    at the host's tempo, exactly per sample: boundary b = origin + k * chunk, block s = 44 floor(b / 44), A = b - s."""
    zero = int(context.get("free_run_origin", context.get("oscillator_origin", 0)))
    chunk = stopped_chunk_samples(voice, bpm)
    k_lo = int(math.floor((first - zero) / chunk)) - 3
    k_hi = int(math.ceil((first + count - zero) / chunk)) + 2
    out = {key: [] for key in ("k", "X", "m", "A", "I", "s")}
    drift = bool(context.get("free_run_drift", True))
    for k in range(max(k_lo, 0), k_hi):
        b = zero + k * chunk
        if drift:
            b += free_run_drift(k * NOTE_QUARTERS[voice])
        s = BLOCK * math.floor(b / BLOCK)
        A = b - s
        I = int(math.ceil(A)) if voice == "U" else int(math.floor(A))
        out["k"].append(k); out["A"].append(A); out["I"].append(I); out["m"].append(-1); out["s"].append(s)
        out["X"].append(2.0 * s - 2.0 + A + I)
    return {key: np.asarray(value) for key, value in out.items()}
