#!/usr/bin/env python3
"""Size, Decay and sample-rate laws of the first pass of Rev OCEAN in Tide mode.

Every capture is the neutral baseline at Mix 100 %. Macro (the Tide knob) is 0
except in the last part, which repeats the checks at Macro 100 %.

  size   every trackable line against Size: 18 values at 48 kHz and 7 at
         44.1 kHz. A line is a first arrival whose position follows
         p(t) = D + A sin(2 pi f (t + p)/fs + phi); D, A, f, phi and the gain
         are fitted per line and per Size. The resulting law is then checked
         as an exact relation between captures at 44.1 kHz (a null test).
  decay  the gain of every first arrival against Decay, from exact ratios
         between single-impulse captures (nothing precedes the impulse, so
         the first pass is clean at any Decay).
  rate   the same line fits at 44.1, 48, 88.2 and 96 kHz, the bandwidth of the
         response and the dependence on the impulse time.
  lfo    the modulation waveform against a float32 phase accumulator.
  macro  Macro 100 %: lag and gain of each line of the first pass against the
         Macro 0 response to the same impulse, for Size, Decay and sample rate.

How a line is measured: its arrival is cut out in a window of +-1/6 ms and its
position is the slope of the phase of that window between 250 Hz and 4 kHz
(low-frequency delay); its gain is the sum of the window. An arrival is used
only where no other tracked line lies within 1/3 ms. Lines are found without
start values by a Hough search over (D, A, phi).

Positions are raw: samples after the impulse, including the reported latency.
Times count from the first sample after the 10 s warm-up.

Run (from this folder; captures are cached, a full run then takes minutes):

    $PY measure_laws.py                 # all parts -> tide_structural_data/laws.json
    $PY measure_laws.py decay rate      # these parts only; the others are kept from the file
"""
from __future__ import annotations

import argparse
import functools
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import least_squares

import datasets
import revocean

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "laws.json"
CORE_RATE = 44100                      # the rate the delay lengths are whole numbers at
AMPLITUDE = 0.5
FIRST_IMPULSE_SECONDS = 1.0
SPACING_SECONDS = 0.51                 # 0.306 cycle of the modulation per impulse
CAPTURE_SECONDS = 40.0
WINDOW_SECONDS = 8 / 48000             # half-width of the window an arrival is measured in
GUARD_SECONDS = 16 / 48000             # another tracked line closer than this spoils the measurement
PHASE_FREQUENCIES = np.arange(250.0, 4001.0, 250.0)
SEARCH_RATE_HZ = 0.599878              # modulation rate assumed by the line search (README item 6)
SEARCH_DEPTHS_SECONDS = np.arange(8.0, 130.0, 2.0) / 48000
DEPTH_SECONDS = 0.88e-3                # depth shared by all first-pass lines; used to tell them from later passes
SIZES_48K = (30.0, 34.7, 41.3, 50.0, 58.9, 66.6, 75.0, 87.3, 100.0, 109.1, 120.0, 133.3, 141.4, 150.0, 163.7, 175.0, 188.2, 200.0)
SIZES_44K = (30.0, 50.0, 87.3, 100.0, 133.3, 150.0, 200.0)
NULL_SIZES = (30.0, 41.3, 50.0, 66.6, 75.0, 87.3, 133.3, 163.7, 200.0)
NULL_READ_SAMPLE = 52170               # Size null: impulse time plus line length; the first line then leads the second by 2 ms or more
SAMPLE_RATES = (44100, 48000, 88200, 96000)
DECAYS_WIDE = (0.5, 0.7, 1.0, 1.4, 2.0, 3.0, 4.0, 6.0, 9.0, 13.0, 20.0, 30.0, 45.0)
TIMES_WIDE = (0.10, 0.37, 0.64, 0.91, 1.18, 1.45)
DECAYS_KNEE = (2.5, 3.5, 4.5, 5.0, 5.5, 5.8, 5.9, 6.1, 6.2, 6.5, 7.5)
TIMES_KNEE = (0.91, 1.45)
DECAYS_CHECK = (0.5, 2.0, 6.0, 30.0)
TIMES_CHECK = (0.10, 0.64, 1.18)
WEIGHT_KNEE_SECONDS = 6.0              # the Decay above which the tap weights stop changing
MACRO_TIME_SECONDS = 7.0               # single impulse of the Macro 100 % Decay check (the tide is in by then)
MACRO_REALISATIONS = (0, 1, 2)
MACRO_SIZES = (30.0, 50.0, 100.0, 150.0, 200.0)


# ---------------------------------------------------------------- stimulus and responses

def decay_setting(seconds: float) -> dict:
    return {"decay": revocean.normalised("decay", seconds)}


def size_setting(percent: float) -> dict:
    return {"size": revocean.normalised("size", percent)}


def impulse_events(fs: int, spacing: float = SPACING_SECONDS) -> list:
    """Impulses `spacing` apart from 1 s on, alternating between the left and the right input."""
    events = []
    while True:
        index = int(round((FIRST_IMPULSE_SECONDS + len(events) * spacing) * fs))
        if index + int(0.6 * fs) > int(round(CAPTURE_SECONDS * fs)):
            return events
        events.append((index, len(events) % 2, AMPLITUDE))


def impulse_train(fs: int, settings: dict, realisation: int = 0, spacing: float = SPACING_SECONDS) -> tuple:
    """(capture, {input channel: (impulse times, unit responses[k, sample after the impulse, output])})."""
    events = impulse_events(fs, spacing)
    capture = revocean.capture(revocean.impulses(CAPTURE_SECONDS, events, fs), settings, sample_rate=fs, realisation=realisation)
    length = int(0.33 * fs)
    group = {}
    for channel in (0, 1):
        times = np.array([event[0] for event in events if event[1] == channel])
        group[channel] = (times, np.stack([capture.output[t:t + length] for t in times]).astype(np.float64) / AMPLITUDE)
    return capture, group


def single_response(fs: int, seconds_before: float, channel, settings: dict, length_seconds: float,
                    realisation: int = 0) -> np.ndarray:
    """Unit response to one impulse `seconds_before` after the warm-up; `channel` None feeds both inputs."""
    index = int(round(seconds_before * fs))
    stimulus = revocean.impulses(seconds_before + length_seconds + 0.075, [(index, channel, AMPLITUDE)], fs)
    output = revocean.capture(stimulus, settings, sample_rate=fs, realisation=realisation).output
    return output[index:index + int(length_seconds * fs)].astype(np.float64) / AMPLITUDE


# ---------------------------------------------------------------- one arrival

def delay_at(times, line, fs: int):
    """Raw position of a line's arrival for impulses at `times`: p = D + A sin(2 pi f (t + p)/fs + phi)."""
    centre, depth, rate, phase = line[:4]
    position = np.full(np.shape(times), centre, float)
    for _ in range(8):
        position = centre + depth * np.sin(2 * np.pi * rate * (times + position) / fs + phase)
    return position


def arrival(x: np.ndarray, centre: float, fs: int) -> tuple:
    """(low-frequency delay in samples, DC gain) of the arrival nearest to `centre`."""
    half = int(round(WINDOW_SECONDS * fs))
    omega = 2 * np.pi * PHASE_FREQUENCIES / fs
    position, total = float(centre), np.nan
    for _ in range(4):
        middle = int(round(position))
        if middle - half < 0 or middle + half + 1 > len(x):
            return np.nan, np.nan
        n = np.arange(middle - half, middle + half + 1)
        total = x[n].sum()
        spectrum = np.exp(-1j * np.outer(omega, n - middle)) @ x[n]
        phase = np.unwrap(np.angle(spectrum * np.sign(total)))
        position = middle - float(np.dot(omega, phase) / np.dot(omega, omega))
        if int(round(position)) == middle:
            break
    return position, float(total)


def fit_law(times, positions, fs: int, start, harmonics=(), warp=None, fixed=None) -> tuple:
    """Least-squares (D, A, f, phi[, cos and sin amplitude per harmonic]) and the residuals in samples.

    `warp` is (phase grid, phase error) of a non-uniform oscillator: the sine
    is then taken at angle + error(angle). `fixed` maps a parameter index to a
    value that is not fitted.
    """
    fixed = fixed or {}
    full = np.array(list(start[:4]) + [0.0] * (2 * len(harmonics)))
    scale = np.array([1.0, 1.0, 1e-5, 0.01] + [0.01] * (2 * len(harmonics)))
    for index, value in fixed.items():
        full[index] = value
    free = [index for index in range(len(full)) if index not in fixed]

    def expand(x):
        q = full.copy()
        q[free] = x
        return q

    def residual(x):
        q = expand(x)
        angle = 2 * np.pi * q[2] * (times + positions) / fs + q[3]
        if warp is not None:
            angle = angle + np.interp(angle % (2 * np.pi), warp[0], warp[1], period=2 * np.pi)
        value = positions - q[0] - q[1] * np.sin(angle)
        for i, harmonic in enumerate(harmonics):
            value = value - q[4 + 2 * i] * np.cos(harmonic * angle) - q[5 + 2 * i] * np.sin(harmonic * angle)
        return value
    solution = least_squares(residual, full[free], x_scale=scale[free])
    q = expand(solution.x)
    if q[1] < 0 and warp is None and not fixed:            # the same curve with a positive depth
        q[1], q[3] = -q[1], q[3] + np.pi
        q[4:] = [value * (-1) ** harmonics[i // 2] for i, value in enumerate(q[4:])]
    q[3] %= 2 * np.pi
    return q, residual(solution.x)


# ---------------------------------------------------------------- finding and tracking lines

def detect(x: np.ndarray, threshold: float, fs: int) -> list:
    """Coarse arrival positions: maxima of the smoothed |x| above the threshold, log-parabola refined."""
    magnitude = np.abs(gaussian_filter1d(x, 1.5 * fs / 48000, mode="constant"))
    peaks = np.flatnonzero((magnitude[1:-1] > threshold) & (magnitude[1:-1] >= magnitude[:-2]) & (magnitude[1:-1] > magnitude[2:])) + 1
    found = []
    for i in peaks:
        a, b, c = np.log(magnitude[i - 1:i + 2] + 1e-30)
        found.append(i + 0.5 * (a - c) / (a - 2 * b + c))
    return found


def hough_best(points: np.ndarray, fs: int) -> tuple:
    """(votes, D, A, phi) of the best cell: every point votes for D = p - A sin(2 pi f (t + p)/fs + phi)."""
    width = 2 * fs / 48000
    depths = SEARCH_DEPTHS_SECONDS * fs
    low = points[:, 1].min() - 1.1 * depths.max()
    bins = int((points[:, 1].max() + 1.1 * depths.max() - low) / width) + 3
    angle = 2 * np.pi * SEARCH_RATE_HZ * (points[:, 0] + points[:, 1]) / fs
    best = (0, 0.0, 0.0, 0.0)
    for depth in depths:
        for phase in np.arange(90) * 2 * np.pi / 90:
            cell = ((points[:, 1] - depth * np.sin(angle + phase) - low) / width).astype(int)
            votes = np.bincount(cell, minlength=bins)
            votes = votes[:-1] + votes[1:]                 # a line may straddle two bins
            k = int(np.argmax(votes))
            if votes[k] > best[0]:
                best = (int(votes[k]), low + (k + 1) * width, float(depth), float(phase))
    return best


def peel_lines(points: np.ndarray, fs: int, count: int) -> list:
    """Lines (D, A, f, phi) among detected points (time, position), found one at a time, strongest first.

    The best Hough cell is refined by least squares on the points it explains;
    those points are removed and the search repeats until the best cell holds
    a vote from fewer than 45 % of the impulses of one input.
    """
    lines = []
    scale = fs / 48000
    while len(points) > 12:
        votes, centre, depth, phase = hough_best(points, fs)
        if votes < 0.45 * count:
            break
        line = np.array([centre, depth, SEARCH_RATE_HZ, phase])
        for tolerance in (4.0, 2.0, 1.5):
            near = np.abs(points[:, 1] - delay_at(points[:, 0], line, fs)) < tolerance * scale
            if near.sum() < 8:
                break
            refined = fit_law(points[near, 0], points[near, 1], fs, line)[0]
            line = np.array([refined[0], refined[1], SEARCH_RATE_HZ, refined[3]])   # the rate stays fixed in the search
        lines.append(tuple(line))
        points = points[np.abs(points[:, 1] - delay_at(points[:, 0], line, fs)) > 2.5 * scale]
    return sorted(lines)


def discover(group: dict, output: int, fs: int) -> list:
    """Start values of the lines of one output channel, from the impulses of both inputs together."""
    points = []
    for times, data in group.values():
        level = np.abs(gaussian_filter1d(data[:, :, output], 1.5 * fs / 48000, axis=1, mode="constant")).max()
        for k, t in enumerate(times):
            points += [(t, position) for position in detect(data[k, :, output], 0.12 * level, fs)]
    return peel_lines(np.array(points), fs, max(len(times) for times, _ in group.values()))


def measure_line(group: dict, output: int, line, others: list, fs: int) -> np.ndarray:
    """Rows (impulse time, position, DC gain, input channel) of one line where it is clear of the others."""
    guard = GUARD_SECONDS * fs
    rows = []
    for channel, (times, data) in group.items():
        predicted = delay_at(times, line, fs)
        clear = np.ones(len(times), bool)
        for other in others:
            clear &= np.abs(delay_at(times, other, fs) - predicted) > guard
        for k in np.flatnonzero(clear):
            position, gain = arrival(data[k, :, output], predicted[k], fs)
            if np.isfinite(position) and abs(position - predicted[k]) < 0.5 * guard:
                rows.append((times[k], position, gain, channel))
    return np.array(rows).reshape(-1, 4)


def fit_line(rows: np.ndarray, line, fs: int):
    """Robust law fit on the rows of the inputs that feed the line; None when too few rows remain."""
    if len(rows) < 12:
        return None
    level = {channel: abs(np.median(rows[rows[:, 3] == channel, 2])) for channel in np.unique(rows[:, 3])}
    fed = [channel for channel in level if level[channel] >= 0.25 * max(level.values())]
    use = rows[np.isin(rows[:, 3], fed)]
    if len(use) < 12:
        return None
    q, r = fit_law(use[:, 0], use[:, 1], fs, line)
    keep = np.abs(r) < max(0.15 * fs / 48000, 5 * 1.4826 * np.median(np.abs(r - np.median(r))))
    if keep.sum() < 12:
        return None
    q, r = fit_law(use[keep, 0], use[keep, 1], fs, q)
    gains = [float(np.median(rows[rows[:, 3] == channel, 2])) if (rows[:, 3] == channel).any() else None for channel in (0, 1)]
    return {"fit": q, "rms": float(np.std(r)), "count": int(keep.sum()), "gains": gains, "used": use[keep]}


def track(group: dict, fs: int) -> dict:
    """{output: fitted lines}; a line that cannot be fitted still keeps the others away from it."""
    result = {}
    for output in (0, 1):
        current = discover(group, output, fs)
        fitted = []
        for _ in range(3):
            fitted = [fit_line(measure_line(group, output, line, current[:i] + current[i + 1:], fs), line, fs)
                      for i, line in enumerate(current)]
            current = [tuple(entry["fit"]) if entry else line for entry, line in zip(fitted, current)]
        result[output] = [entry for entry in fitted if entry]
    return result


def is_first_pass(entry: dict, fs: int) -> bool:
    """A clean line with the depth all first arrivals share (later passes add two modulations)."""
    strongest = max(abs(gain) for gain in entry["gains"] if gain is not None)
    return bool(abs(entry["fit"][1] / fs / DEPTH_SECONDS - 1) < 0.01 and entry["rms"] < 0.12 * fs / 48000 and strongest > 0.01)


def line_record(entry: dict, output: int, fs: int) -> dict:
    centre, depth, rate, phase = entry["fit"]
    return {"output": "LR"[output], "centreSamples": round(float(centre), 4), "depthSamples": round(float(depth), 4),
            "rateHz": round(float(rate), 7), "phaseRad": round(float(phase), 5), "lawRmsSamples": round(entry["rms"], 4),
            "arrivals": entry["count"], "gainFromL": rounded(entry["gains"][0], 6), "gainFromR": rounded(entry["gains"][1], 6),
            "firstPass": is_first_pass(entry, fs)}


def rounded(value, digits: int):
    return None if value is None or not np.isfinite(value) else round(float(value), digits)


@functools.lru_cache(maxsize=None)
def tracked(fs: int, size: float = 100.0) -> tuple:
    """(capture, group, records of the well tracked lines, their fits) of the impulse train at Macro 0, Decay 0.5 s."""
    capture, group = impulse_train(fs, {**decay_setting(0.5), **size_setting(size)})
    result = track(group, fs)
    kept = [(output, entry) for output in (0, 1) for entry in result[output] if entry["rms"] < 0.25 * fs / 48000 and entry["count"] >= 20]
    return capture, group, [line_record(entry, output, fs) for output, entry in kept], [entry for _, entry in kept]


def phase_distance(a: float, b: float) -> float:
    return abs((a - b + np.pi) % (2 * np.pi) - np.pi)


def match(record: dict, reference: list):
    """The reference line with the same output and modulation phase (the phase does not move with Size)."""
    same = [line for line in reference if line["output"] == record["output"] and phase_distance(line["phaseRad"], record["phaseRad"]) < 0.03]
    return same[0] if len(same) == 1 else None


# ---------------------------------------------------------------- Size

def size_sweep(fs: int, sizes) -> list:
    with ThreadPoolExecutor(3) as pool:                    # render what is not cached yet, three at a time
        list(pool.map(lambda size: impulse_train(fs, {**decay_setting(0.5), **size_setting(size)}), sizes))
    sweep = []
    for size in sizes:
        capture, _, lines, _ = tracked(fs, size)
        normal = capture.meta["settings"]["size"]
        sweep.append({"sizePercent": revocean.CONTROL["size"].display(normal), "sizeNormalised": normal,
                      "sizeReadback": capture.meta["readback"]["id:4"], "mixReadback": capture.meta["readback"]["id:1"],
                      "macroReadback": capture.meta["readback"]["id:6"], "lines": lines})
    return sweep


def reference_lines(sweep: list) -> list:
    """First-pass lines of the Size 100 % capture; they name the lines of every other capture."""
    entry = next(entry for entry in sweep if abs(entry["sizePercent"] - 100.0) < 0.01)
    return [line for line in entry["lines"] if line["firstPass"]]


def size_rows(sweep: list) -> list:
    """(reference line, size record, line record) for every first-pass line that is identified by its phase."""
    base = reference_lines(sweep)
    rows = []
    for entry in sweep:
        for line in entry["lines"]:
            origin = match(line, base) if line["firstPass"] else None
            if origin is not None:
                rows.append((origin, entry, line))
    return rows


def lattice_offset(rows: list, unit: float) -> float:
    """The fixed part a of D = a + u N: the mean intercept of D against Size, moved onto the lattice of whole N."""
    intercepts = []
    for origin in {id(row[0]): row[0] for row in rows}.values():
        mine = np.array([(entry["sizePercent"], line["centreSamples"]) for o, entry, line in rows if o is origin])
        if len(mine) >= 6:
            intercepts.append(np.polyfit(mine[:, 0], mine[:, 1], 1)[1])
    lattice = (np.array([line["centreSamples"] for _, _, line in rows]) - np.mean(intercepts)) / unit
    return float(np.mean(intercepts) + unit * np.mean(lattice - np.round(lattice)))


def size_law(sweep: list, fs: int) -> dict:
    """D = a + u N with whole N: the lattice, the lengths P at 100 %, and how N follows from P and Size."""
    unit = fs / CORE_RATE
    rows = size_rows(sweep)
    fixed = lattice_offset(rows, unit)
    centre = np.array([line["centreSamples"] for _, _, line in rows])
    measured = (centre - fixed) / unit
    size = np.array([entry["sizePercent"] for _, entry, _ in rows])
    length = np.array([round((origin["centreSamples"] - fixed) / unit) for origin, _, _ in rows], float)
    exact = length * size / 100
    clean = np.array([line["lawRmsSamples"] < 0.03 * fs / 48000 for _, _, line in rows])
    off = measured - np.round(measured)

    def residual(whole):
        value = centre - unit * whole
        return {"fixedSamples": round(float(value.mean()), 4), "rmsSamples": round(float(value.std()), 4),
                "maxSamples": round(float(np.abs(value - value.mean()).max()), 4)}
    free = np.linalg.lstsq(np.stack([np.ones(len(rows)), np.floor(exact + 0.5)], axis=1), centre, rcond=None)[0]
    proportional = centre - np.sum(centre * exact) / np.sum(exact * exact) * exact
    table = []
    for (origin, _, line), p, s, m, e in zip(rows, length, size, measured, exact):
        own, cross = (line["gainFromL"], line["gainFromR"]) if line["output"] == "L" else (line["gainFromR"], line["gainFromL"])
        table.append({"output": origin["output"], "lengthAt100": int(p), "sizePercent": round(float(s), 4),
                      "wholeSamples": int(round(m)), "measured": round(float(m), 3), "exactProduct": round(float(e), 3),
                      "lawRmsSamples": line["lawRmsSamples"], "gainOwnInput": own, "gainOtherInput": cross})
    return {
        "fixedSamples": round(fixed, 4), "fixedMs": round(1000 * fixed / fs, 5), "unitSamples": unit,
        "rows": len(rows), "cleanRows": int(clean.sum()),
        "distanceToLatticeRms": round(float(off.std()), 4), "distanceToLatticeMax": round(float(np.abs(off).max()), 4),
        "distanceToLatticeRmsClean": round(float(off[clean].std()), 4), "distanceToLatticeMaxClean": round(float(np.abs(off[clean]).max()), 4),
        "roundHalfUpMismatches": int(np.sum(np.round(measured) != np.floor(exact + 0.5))),
        "halfwayCases": int(np.sum(np.abs(exact % 1 - 0.5) < 1e-3)),
        "freeFit": {"fixedSamples": round(float(free[0]), 4), "unitSamples": round(float(free[1]), 7)},
        "residualByRule": {"floor(P s + 0.5)": residual(np.floor(exact + 0.5)), "floor(P s)": residual(np.floor(exact)),
                           "ceil(P s)": residual(np.ceil(exact)), "P s unrounded": residual(exact)},
        "proportionalRmsSamples": round(float(proportional.std()), 2),
        "table": table,
    }


def size_constancy(sweep: list, fs: int) -> dict:
    """Depth, rate and phase of the first-pass lines against Size (lines with 60 or more clean arrivals)."""
    best = [(origin, entry, line) for origin, entry, line in size_rows(sweep)
            if line["lawRmsSamples"] < 0.03 * fs / 48000 and line["arrivals"] >= 60]
    depth = np.array([line["depthSamples"] for _, _, line in best])
    rate = np.array([line["rateHz"] for _, _, line in best])
    phase = np.array([phase_distance(line["phaseRad"], origin["phaseRad"]) for origin, _, line in best])
    by_size = []
    for entry in sweep:
        mine = [line for _, e, line in best if e is entry]
        if mine:
            by_size.append({"sizePercent": round(entry["sizePercent"], 4), "lines": len(mine),
                            "depthSamples": round(float(np.mean([line["depthSamples"] for line in mine])), 4),
                            "rateHz": round(float(np.mean([line["rateHz"] for line in mine])), 7)})
    return {"rowsUsed": len(best), "depthSamplesMean": round(float(depth.mean()), 4), "depthSamplesStd": round(float(depth.std()), 4),
            "depthMsMean": round(float(1000 * depth.mean() / fs), 6), "rateHzMean": round(float(rate.mean()), 7),
            "rateHzStd": round(float(rate.std()), 7), "phaseMaxChangeRad": round(float(phase.max()), 5), "bySize": by_size}


def tap_gains(table: list) -> list:
    """Per line: gain with the Decay attenuation of its length removed, across Size, and the cross-feed ratio."""
    result = []
    for output, length in sorted({(row["output"], row["lengthAt100"]) for row in table}):
        mine = [row for row in table if (row["output"], row["lengthAt100"]) == (output, length)
                and row["lawRmsSamples"] < 0.03 and row["gainOwnInput"] is not None]
        if len(mine) < 5:
            continue
        raw = np.array([row["gainOwnInput"] for row in mine])
        tap = raw / 10 ** (-3 * np.array([row["exactProduct"] for row in mine]) / (CORE_RATE * 0.5))
        cross = np.array([row["gainOtherInput"] / row["gainOwnInput"] for row in mine if row["gainOtherInput"] is not None])
        result.append({"output": output, "lengthAt100": length, "sizes": len(mine), "tapGainMean": round(float(tap.mean()), 5),
                       "tapGainSpreadPercent": round(float(100 * tap.std() / tap.mean()), 2), "rawGainMin": round(float(raw.min()), 5),
                       "rawGainMax": round(float(raw.max()), 5), "crossFeedRatio": rounded(cross.mean() if len(cross) else None, 4)})
    return result


def size_null(length: int) -> list:
    """Exact form of the Size law at 44.1 kHz, on the first arrival of the left output.

    The impulse is placed `whole` samples before a fixed instant, so that at
    every Size the line is read at the same instant and with the same
    modulation. The law then says: the response at Size s is the response at
    100 %, moved by whole(s) - whole(100 %) samples and scaled by
    10^(-3 P (s - 1) / (44100 T)). Nothing is fitted.
    """
    def response(size):
        scale = revocean.CONTROL["size"].display(revocean.resolve(size_setting(size))["size"]) / 100
        whole = int(np.floor(length * scale + 0.5))
        settings = {**decay_setting(0.5), **size_setting(size)}
        return scale, whole, single_response(CORE_RATE, (NULL_READ_SAMPLE - whole) / CORE_RATE, 0, settings, 0.2)[:, 0]
    unity, base_whole, base = response(100.0)
    first = int(np.flatnonzero(np.abs(base) > 0.3 * np.abs(base).max())[0])
    window = slice(first - 3, first + 20)
    result = []
    for size in NULL_SIZES:
        scale, whole, moved = response(size)
        candidate = moved[window.start - base_whole + whole:window.stop - base_whole + whole]
        law = 10 ** (-3 * length * (scale - unity) / (CORE_RATE * 0.5))
        rounded_length = 10 ** (-3 * (whole - base_whole) / (CORE_RATE * 0.5))
        result.append({"sizePercent": round(100 * scale, 4), "wholeSamples": whole, "gainByLaw": round(law, 7),
                       "gainFitted": round(scaled_null(candidate, base[window])[0], 7),
                       "nullDb": round(revocean.null_db(law * base[window], candidate), 1),
                       "nullWithWholeLengthInTheGainDb": round(revocean.null_db(rounded_length * base[window], candidate), 1),
                       "nullOneSampleOffDb": round(revocean.null_db(law * base[window.start + 1:window.stop + 1], candidate), 1)})
    return result


def measure_size() -> dict:
    result = {}
    for fs, sizes in ((48000, SIZES_48K), (44100, SIZES_44K)):
        sweep = size_sweep(fs, sizes)
        law = size_law(sweep, fs)
        result[str(fs)] = {"law": law, "constancy": size_constancy(sweep, fs), "tapGains": tap_gains(law["table"]), "sweep": sweep}
    first_line = min(row["lengthAt100"] for row in result["44100"]["law"]["table"] if row["output"] == "L")
    result["exactNullAt44100"] = {"lengthAt100": first_line, "readSample": NULL_READ_SAMPLE, "sizes": size_null(first_line)}
    return result


def named_lines(size_result: dict, fs: int) -> list:
    """Reference lines of a rate with their length at Size 100 % in core samples."""
    part = size_result[str(fs)]
    return [{**line, "lengthAt100": int(round((line["centreSamples"] - part["law"]["fixedSamples"]) / part["law"]["unitSamples"]))}
            for line in reference_lines(part["sweep"])]


# ---------------------------------------------------------------- Decay

def response_seconds(size: float) -> float:
    """Length of a single-impulse response that holds the first pass at this Size."""
    return 0.125 * max(1.0, size / 100)


def clear_arrivals(lines: list, time_samples: int, fs: int, first_pass: bool = True) -> list:
    """(line record, window) of the first-pass (or the other) lines whose arrival has no tracked neighbour within the guard."""
    scale = fs / 48000
    position = [float(delay_at(np.array([float(time_samples)]), (line["centreSamples"], line["depthSamples"], line["rateHz"], line["phaseRad"]), fs)[0])
                for line in lines]
    found = []
    for i, line in enumerate(lines):
        apart = min(abs(position[j] - position[i]) for j in range(len(lines)) if j != i and lines[j]["output"] == line["output"])
        if line["firstPass"] == first_pass and apart > GUARD_SECONDS * fs:
            centre = int(round(position[i]))
            found.append((line, slice(centre - int(round(6 * scale)), centre + int(round(9 * scale)))))
    return found


def scaled_null(candidate: np.ndarray, reference: np.ndarray) -> tuple:
    """(least-squares gain of candidate against reference, residual of the scaled reference in dB)."""
    gain = float(np.dot(candidate, reference) / np.dot(reference, reference))
    error = np.sum((candidate - gain * reference) ** 2) / np.sum(candidate ** 2)
    return gain, 10 * np.log10(error + 1e-30)


def decay_ratios(fs: int, size: float, decays, times, label, first_pass: bool = True) -> dict:
    """{label of a line: (gain ratios against Decay 0.5 s, worst null in dB, largest move, spread, arrivals)}.

    A ratio is the least-squares gain between the windows of one arrival at two
    Decay settings. An arrival counts when the scaled window nulls below -45 dB
    at every Decay, that is when nothing but the gain differs; of those, the
    ones within 10 dB of the line's best null are averaged. The move is the
    largest change of the arrival's position against Decay 0.5 s, the spread
    the largest difference of a ratio between impulse times, in dB.
    `label(line)` names the line, or is None for a line that is not wanted.
    """
    lines = tracked(fs, size)[2]
    jobs = [(decay, t) for decay in sorted(set(decays) | {0.5}) for t in times]
    with ThreadPoolExecutor(4) as pool:
        data = dict(zip(jobs, pool.map(lambda job: single_response(fs, job[1], None, {**decay_setting(job[0]), **size_setting(size)},
                                                                    response_seconds(size)), jobs)))
    result = {}
    for t in times:
        for line, window in clear_arrivals(lines, int(round(t * fs)), fs, first_pass):
            key = label(line)
            if key is None:
                continue
            output = "LR".index(line["output"])
            pairs = [scaled_null(data[(decay, t)][window, output], data[(0.5, t)][window, output]) for decay in decays]
            worst = max(null for (_, null), decay in zip(pairs, decays) if decay != 0.5)
            if worst < -45.0 and min(gain for gain, _ in pairs) > 0:
                centre = (window.start + window.stop) / 2
                position = [arrival(data[(decay, t)][:, output], centre, fs)[0] for decay in sorted(set(decays) | {0.5})]
                result.setdefault(key, []).append((np.array([gain for gain, _ in pairs]), worst, float(np.ptp(position))))
    return {key: best_rows(rows) for key, rows in result.items()}


def best_rows(rows: list) -> tuple:
    best = [row for row in rows if row[1] <= min(row[1] for row in rows) + 10.0]
    ratios = np.array([row[0] for row in best])
    return ratios.mean(axis=0), max(row[1] for row in best), max(row[2] for row in best), float(np.ptp(20 * np.log10(ratios), axis=0).max()), len(best)


def by_length(names: list):
    """Label for decay_ratios: (output, length at Size 100 %) of a first-pass line."""
    def label(line):
        origin = match(line, names)
        return None if origin is None else (line["output"], origin["lengthAt100"])
    return label


def attenuation_length(ratios: np.ndarray, decays) -> float:
    """Core samples N in gain ~ 10^(-3 N / (44100 T)), from the Decay values where the tap weight is constant."""
    decays = np.array(decays)
    flat = decays >= WEIGHT_KNEE_SECONDS
    slope = np.polyfit(1 / decays[flat], 20 * np.log10(ratios[flat]), 1)[0]
    return float(-slope * CORE_RATE / 60)


def weights(ratios: np.ndarray, decays, length_samples: float) -> np.ndarray:
    """Ratio with the attenuation 10^(-3 N / (44100 T)) removed: the tap weight relative to Decay 0.5 s."""
    return ratios * 10 ** (3 * length_samples / CORE_RATE * (1 / np.array(decays) - 1 / 0.5))


def knee_fit(weight: np.ndarray, decays) -> dict:
    """Straight line through the weights below the knee, the constant above it, and where the two meet.

    `lawErrorMax` is the largest deviation of the measured weights from
    w(T) = 1 + slope (min(T, 6 s) - 0.5 s), with the slope rounded to six places.
    """
    decays = np.array(decays)
    below = decays < WEIGHT_KNEE_SECONDS - 0.05
    slope, offset = np.polyfit(decays[below], weight[below], 1)
    flat = float(weight[decays > WEIGHT_KNEE_SECONDS + 0.05].mean())
    law = 1 + round(float(slope), 6) * (np.minimum(decays, WEIGHT_KNEE_SECONDS) - 0.5)
    return {"decays": [float(d) for d in decays], "weight": [round(float(v), 5) for v in weight],
            "slopePerSecond": round(float(slope), 6), "valueAtHalfSecond": round(float(slope * 0.5 + offset), 6),
            "linearRms": float(np.std(weight[below] - slope * decays[below] - offset)), "flatValue": round(flat, 6),
            "meetsFlatAtSeconds": rounded((flat - offset) / slope if abs(slope) > 1e-4 else None, 4),
            "lawErrorMax": float(np.abs(weight - law).max())}


def decay_main(names: list) -> list:
    """Per line at Size 100 %, 48 kHz: attenuation length and tap weight against Decay."""
    wide = decay_ratios(48000, 100.0, DECAYS_WIDE, TIMES_WIDE, by_length(names))
    knee_decays = tuple(sorted(set(DECAYS_KNEE) | {0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 9.0}))
    knee = decay_ratios(48000, 100.0, knee_decays, TIMES_KNEE, by_length(names))
    result = []
    for key in sorted(wide):
        ratio, null, move, spread, count = wide[key]
        entry = {"output": key[0], "lengthAt100": key[1], "arrivals": count,
                 "attenuationLengthSamples": round(attenuation_length(ratio, DECAYS_WIDE), 2),
                 "ratioDbAgainstDecayHalfSecond": [round(float(v), 4) for v in 20 * np.log10(ratio)],
                 "weight": [round(float(v), 5) for v in weights(ratio, DECAYS_WIDE, key[1])],
                 "worstScaledNullDb": round(null, 1), "maxPositionChangeSamples": round(move, 5),
                 "ratioSpreadOverImpulseTimesDb": round(spread, 5)}
        if key in knee:
            entry["knee"] = knee_fit(weights(knee[key][0], knee_decays, key[1]), knee_decays)
        result.append(entry)
    return result


def decay_check(fs: int, size: float, names: list) -> list:
    """Attenuation length (from 6 s against 30 s, where the weights are flat) and weights at another Size or rate."""
    result = []
    for key, (ratio, null, _, _, count) in sorted(decay_ratios(fs, size, DECAYS_CHECK, TIMES_CHECK, by_length(names)).items()):
        product = key[1] * size / 100
        six, thirty = ratio[DECAYS_CHECK.index(6.0)], ratio[DECAYS_CHECK.index(30.0)]
        measured = float(-np.log10(thirty / six) / 3 * CORE_RATE / (1 / 30 - 1 / 6))
        result.append({"output": key[0], "lengthAt100": key[1], "productSamples": product, "wholeSamples": int(np.floor(product + 0.5)),
                       "attenuationLengthSamples": round(measured, 2), "arrivals": count, "worstScaledNullDb": round(null, 1),
                       "weight": {str(decay): round(float(w), 5) for decay, w in zip(DECAYS_CHECK, weights(ratio, DECAYS_CHECK, product))}})
    return result


def decay_second_pass(law: dict) -> list:
    """Later-pass lines at Size 100 %, 48 kHz: the length their attenuation follows and their weight above the knee.

    A later-pass line sits at fixed + unit * (sum of the lengths it passed);
    `levelAboveKneeDb` is its ratio against Decay 0.5 s with the attenuation
    of that sum removed.
    """
    def label(line):
        return line["output"], int(round((line["centreSamples"] - law["fixedSamples"]) / law["unitSamples"]))
    result = []
    for key, (ratio, null, _, _, count) in sorted(decay_ratios(48000, 100.0, DECAYS_WIDE, TIMES_WIDE, label, first_pass=False).items()):
        level = 20 * np.log10(weights(ratio, DECAYS_WIDE, key[1]))[np.array(DECAYS_WIDE) >= WEIGHT_KNEE_SECONDS]
        result.append({"output": key[0], "sumOfLengths": key[1], "attenuationLengthSamples": round(attenuation_length(ratio, DECAYS_WIDE), 2),
                       "levelAboveKneeDb": round(float(level.mean()), 4), "arrivals": count, "worstScaledNullDb": round(null, 1)})
    return result


def cross_feed_against_decay(names: list) -> list:
    """Left-only against both-input impulses: the share of an arrival that comes from the left input."""
    result = []
    for decay in (0.5, 4.0, 30.0):
        shares = {}
        for t in TIMES_WIDE:
            both = single_response(48000, t, None, decay_setting(decay), response_seconds(100.0))
            left = single_response(48000, t, 0, decay_setting(decay), response_seconds(100.0))
            for line, window in clear_arrivals(tracked(48000)[2], int(round(t * 48000)), 48000):
                origin = match(line, names)
                output = "LR".index(line["output"])
                gain, null = scaled_null(left[window, output], both[window, output])
                if origin is not None and null < -45.0:
                    shares.setdefault((line["output"], origin["lengthAt100"]), []).append(gain)
        result.append({"decaySeconds": decay, "leftInputShare": [{"output": key[0], "lengthAt100": key[1], "share": round(float(np.mean(v)), 5)}
                                                                  for key, v in sorted(shares.items())]})
    return result


def measure_decay(size_result: dict) -> dict:
    names = named_lines(size_result, 48000)
    main_lines = decay_main(names)
    exact = [entry for entry in main_lines if entry["worstScaledNullDb"] < -60.0]
    length = np.array([entry["lengthAt100"] for entry in exact], float)
    measured = np.array([entry["attenuationLengthSamples"] for entry in exact])
    slope, offset = np.polyfit(length, measured, 1)
    return {
        "decays": list(DECAYS_WIDE), "impulseTimesSeconds": list(TIMES_WIDE), "lines": main_lines,
        "attenuationLengthAgainstLength": {"slope": round(float(slope), 6), "offsetSamples": round(float(offset), 3),
                                           "maxDeviationSamples": round(float(np.abs(measured - length).max()), 3)},
        "otherSizes": {str(size): decay_check(48000, size, names) for size in (50.0, 200.0)},
        "at44100": decay_check(44100, 100.0, named_lines(size_result, 44100)),
        "secondPass": decay_second_pass(size_result["48000"]["law"]), "crossFeed": cross_feed_against_decay(names),
    }


# ---------------------------------------------------------------- sample rate

def band_levels(group: dict, fs: int) -> dict:
    """Mean power of the left-to-left responses per band, in dB against 100 Hz to 1 kHz."""
    data = group[0][1][:, :, 0]
    power = (np.abs(np.fft.rfft(data, axis=1)) ** 2).mean(axis=0)
    frequency = np.fft.rfftfreq(data.shape[1], 1 / fs)
    reference = power[(frequency >= 100) & (frequency < 1000)].mean()
    edges = (1000, 10000, 18000, 20000, 20500, 21000, 21500, 22050, 24000, 30000, 44100, 48000)
    return {f"{low}-{high}": round(float(10 * np.log10(power[(frequency >= low) & (frequency < high)].mean() / reference)), 1)
            for low, high in zip(edges[:-1], edges[1:]) if high <= fs / 2}


def rate_entry(fs: int, names: list) -> dict:
    """Line fits at one sample rate; the fixed part is what is left of D after the core length in host samples.

    Uses the first-pass lines with 60 or more clean arrivals. They are fitted
    a second time with the phase error of the float32 accumulator (see the lfo
    part), which removes the bias the plain sinusoid leaves in D and A.
    """
    capture, group, lines, fits = tracked(fs)
    scale = fs / 48000
    fixed, phase, exact = [], [], []
    for line, entry in zip(lines, fits):
        origin = match(line, names)
        if origin is None or not line["firstPass"] or line["lawRmsSamples"] > 0.03 * scale or line["arrivals"] < 60:
            continue
        fixed.append(line["centreSamples"] - fs / CORE_RATE * origin["lengthAt100"])
        phase.append((line["phaseRad"] - origin["phaseRad"] + np.pi) % (2 * np.pi) - np.pi)
        q, r = fit_law(entry["used"][:, 0], entry["used"][:, 1], fs, entry["fit"], warp=accumulator_warp())
        exact.append((q[0] - line["centreSamples"], q[1], q[2], np.std(r), line["depthSamples"], line["rateHz"]))
    exact = np.array(exact)
    return {"reportedLatencySamples": capture.latency, "linesUsed": len(fixed), "fixedSamples": round(float(np.mean(fixed)), 4),
            "fixedSpreadSamples": round(float(np.ptp(fixed)), 4), "fixedMs": round(float(1000 * np.mean(fixed) / fs), 5),
            "fixedBeyondLatencyMs": round(float(1000 * (np.mean(fixed) - capture.latency) / fs), 5),
            "depthMs": round(float(1000 * exact[:, 4].mean() / fs), 6), "rateHz": round(float(exact[:, 5].mean()), 7),
            "phaseAgainst48000Rad": round(float(np.mean(phase)), 5), "phaseAgainst48000SpreadRad": round(float(np.ptp(phase)), 5),
            "accumulatorModel": {"fixedSamples": round(float(np.mean(fixed) + exact[:, 0].mean()), 4),
                                 "depthMs": round(float(1000 * exact[:, 1].mean() / fs), 6), "depthSpreadMs": round(float(1000 * np.ptp(exact[:, 1]) / fs), 6),
                                 "depthCoreSamples": round(float(exact[:, 1].mean() * CORE_RATE / fs), 4),
                                 "rateHz": round(float(exact[:, 2].mean()), 7), "lawRmsSamples": round(float(exact[:, 3].mean()), 4)},
            "bandLevelsDb": band_levels(group, fs), "lines": lines}


def impulse_time_dependence() -> dict:
    """48 kHz impulses spread over the 160-sample cycle of the 160:147 rate ratio, against impulses that all sit on it."""
    result = {}
    for name, spacing in (("multiplesOf160", SPACING_SECONDS), ("allResidues", 24487 / 48000)):
        _, group = impulse_train(48000, decay_setting(0.5), spacing=spacing)
        entries = [entry for output in (0, 1) for entry in track(group, 48000)[output] if is_first_pass(entry, 48000) and entry["count"] >= 60]
        residues, rms = set(), []
        for entry in entries:
            used = entry["used"]
            residues |= {int(t) % 160 for t in used[:, 0]}
            rms.append(float(np.std(fit_law(used[:, 0], used[:, 1], 48000, entry["fit"], warp=accumulator_warp())[1])))
        result[name] = {"lines": len(entries), "distinctResiduesMod160": len(residues),
                        "lawRmsAccumulatorSamples": round(float(np.mean(rms)), 4), "worstLineRmsSamples": round(float(np.max(rms)), 4),
                        "centresSamples": sorted(round(float(entry["fit"][0]), 4) for entry in entries)}
    return result


def measure_rate(size_result: dict) -> dict:
    names = named_lines(size_result, 48000)
    rates = {str(fs): rate_entry(fs, names) for fs in SAMPLE_RATES}
    for entry in rates.values():
        entry["laterThanAt44100Ms"] = round(entry["fixedMs"] - rates[str(CORE_RATE)]["fixedMs"], 5)
    return {"rates": rates, "impulseTime": impulse_time_dependence()}


# ---------------------------------------------------------------- modulation waveform

def accumulator(nominal_hz: float, seconds: float = 60.0) -> dict:
    """A float32 phase that grows by float32(2 pi f / 44100) per core sample and wraps at 2 pi.

    Returns its mean rate, its phase error against a uniformly advancing phase
    (as a function of that phase) and harmonics 2 and 3 of its sine.
    """
    full = np.float32(2 * np.pi)
    step = np.float32(2 * np.pi * nominal_hz / CORE_RATE)
    phase = np.empty(int(seconds * CORE_RATE), np.float32)
    value = np.float32(0.0)
    for i in range(len(phase)):
        phase[i] = value
        value = np.float32(value + step)
        if value >= full:
            value = np.float32(value - full)
    turns = np.concatenate([[0], np.cumsum(np.diff(phase) < 0)])
    unwrapped = phase.astype(np.float64) + 2 * np.pi * turns
    slope, offset = np.polyfit(np.arange(len(phase)), unwrapped, 1)
    uniform = slope * np.arange(len(phase)) + offset
    grid = (np.arange(720) + 0.5) * 2 * np.pi / 720
    cell = ((uniform % (2 * np.pi)) / (2 * np.pi) * 720).astype(int)
    error = np.bincount(cell, unwrapped - uniform, 720) / np.bincount(cell, minlength=720)
    rest = np.sin(unwrapped) - np.sin(uniform) * np.mean(np.cos(unwrapped - uniform)) - np.cos(uniform) * np.mean(np.sin(unwrapped - uniform))
    return {"rateHz": float(slope * CORE_RATE / (2 * np.pi)), "grid": grid, "error": error,
            "harmonics": {str(h): [round(float(2 * np.mean(rest * np.cos(h * uniform))), 7), round(float(2 * np.mean(rest * np.sin(h * uniform))), 7)]
                          for h in (2, 3)}}


@functools.lru_cache(maxsize=None)
def accumulator_warp() -> tuple:
    """(phase grid, phase error) of the 0.6 Hz accumulator, for fit_law."""
    model = accumulator(0.6)
    return model["grid"], model["error"]


def measure_lfo() -> dict:
    """The modulation on two never-crossed lines of the shared grid against a float32 phase accumulator.

    Each line is fitted with a pure sinusoid, with free harmonics 2 and 3, and
    with the accumulator's phase error in place of the harmonics; round values
    of the rate and the depth are then tested with the accumulator model.
    """
    times, responses = datasets.grid_responses(0)
    times = times.astype(float)
    model = accumulator(0.6)
    warp = accumulator_warp()
    result = {"float32Accumulator": {"nominalHz": 0.6, "rateHz": round(model["rateHz"], 7), "harmonicsRelativeToDepth": model["harmonics"],
                                     "phaseErrorPeakRad": round(float(np.abs(model["error"]).max()), 6)},
              "lines": []}
    for start in ((1752.19, 42.247, SEARCH_RATE_HZ, 2.0186), (2144.03, 42.247, SEARCH_RATE_HZ, 3.0347)):
        predicted = delay_at(times, start, 48000)
        position = np.array([arrival(responses[k, :, 0].astype(np.float64), predicted[k], 48000)[0] for k in range(len(times))])
        rms = lambda fit: round(float(np.std(fit[1])), 5)
        plain = fit_law(times, position, 48000, start)
        free = fit_law(times, position, 48000, start, harmonics=(2, 3))
        warped = fit_law(times, position, 48000, start, warp=warp)
        result["lines"].append({
            "arrivals": len(times), "centreSamples": round(float(warped[0][0]), 4), "depthSamples": round(float(warped[0][1]), 5),
            "depthMs": round(float(warped[0][1] / 48), 6), "depthCoreSamples": round(float(warped[0][1] * CORE_RATE / 48000), 4),
            "rateHz": round(float(warped[0][2]), 7), "accumulatorPhaseAtStimulusStartRad": round(float(warped[0][3]), 5),
            "rmsSamples": {"sinusoid": rms(plain), "sinusoidWithFreeHarmonics": rms(free), "accumulator": rms(warped),
                           "accumulatorRateAsSimulated": rms(fit_law(times, position, 48000, warped[0], warp=warp, fixed={2: model["rateHz"]})),
                           "accumulatorDepth0.88ms": rms(fit_law(times, position, 48000, warped[0], warp=warp, fixed={1: 0.88e-3 * 48000})),
                           "sinusoidRate0.6Hz": rms(fit_law(times, position, 48000, plain[0], fixed={2: 0.6}))},
            "freeHarmonicsRelativeToDepth": {"2": [round(float(free[0][4] / free[0][1]), 7), round(float(free[0][5] / free[0][1]), 7)],
                                             "3": [round(float(free[0][6] / free[0][1]), 7), round(float(free[0][7] / free[0][1]), 7)]}})
    return result


# ---------------------------------------------------------------- Macro 100 %

def lag_and_gain(candidate: np.ndarray, reference: np.ndarray, window: slice, lags) -> tuple:
    """(lag, gain, correlation) of the best match of the windowed reference inside the candidate."""
    piece = reference[window]
    energy = float(np.dot(piece, piece))
    best = (0, 0.0, 0.0)
    for lag in lags:
        other = candidate[window.start + lag:window.stop + lag]
        if len(other) < len(piece):
            break
        product = float(np.dot(other, piece))
        correlation = product / np.sqrt(energy * float(np.dot(other, other)) + 1e-30)
        if abs(correlation) > abs(best[2]):
            best = (lag, product / energy, correlation)
    return best


def robust_line(x: np.ndarray, y: np.ndarray, tolerance: float) -> tuple:
    """(slope, offset, inlier mask): median of pairwise slopes, then least squares on the points within tolerance."""
    i, j = np.triu_indices(len(x), 1)
    slope = np.median((y[j] - y[i]) / (x[j] - x[i]))
    inside = np.abs(y - slope * x - np.median(y - slope * x)) < tolerance
    slope, offset = np.polyfit(x[inside], y[inside], 1)
    return float(slope), float(offset), inside


def pattern_lag(before: np.ndarray, after: np.ndarray, times: np.ndarray, lines: list, fs: int) -> dict:
    """Lag of the Macro 100 % responses `after` against the Macro 0 responses `before` of one input-output path.

    The pattern is the first eight first-pass lines of the output. An impulse
    counts when the shifted Macro 0 pattern correlates above 0.3 with the
    Macro 100 % response (in between the tide has taken the response away).
    The lag is then fitted as a straight line in the impulse time, and every
    clear single line is matched within +-3 samples of the pattern's lag.
    """
    first = sorted(line["centreSamples"] for line in lines if line["firstPass"])
    window = slice(int(first[0] - 60 * fs / 48000), int(first[min(7, len(first) - 1)] + 60 * fs / 48000))
    lags = range(int(0.008 * fs), int(0.024 * fs))
    rows = [(k, t) + lag_and_gain(after[k], before[k], window, lags) for k, t in enumerate(times)]
    rows = [row for row in rows if abs(row[4]) > 0.3]
    seconds = np.array([row[1] for row in rows]) / fs
    lag_ms = np.array([row[2] for row in rows]) * 1000 / fs
    slope, offset, inside = robust_line(seconds, lag_ms, 0.15)
    apart, single = [], {}
    for (k, t, lag, gain, _), on_line in zip(rows, inside):
        if on_line:
            for line, piece in clear_arrivals(lines, int(t), fs):
                found = lag_and_gain(after[k], before[k], piece, range(lag - 3, lag + 4))
                single.setdefault(line["centreSamples"], []).append((found[1] / gain, abs(found[2])))
                if abs(found[2]) > 0.7:
                    apart.append(found[0] - lag)
    apart = np.array(apart)
    return {"impulses": len(times), "impulsesWithTheTideIn": len(rows), "impulsesOnTheLine": int(inside.sum()),
            "lagMsAtOneSecond": round(slope + offset, 4), "lagSlopeMsPerSecond": round(slope, 5),
            "lagFitRmsMs": round(float(np.std(lag_ms[inside] - slope * seconds[inside] - offset)), 4),
            "correlationMedian": round(float(np.median([abs(row[4]) for row in rows])), 3),
            "singleLines": {"arrivals": int(len(apart)), "medianLagDifferenceSamples": rounded(np.median(apart) if len(apart) else None, 1),
                            "withinOneSample": rounded(np.mean(np.abs(apart) <= 1) if len(apart) else None, 3)},
            "lineGainAgainstPatternGain": [{"centreSamples": centre, "arrivals": len(values),
                                            "median": round(float(np.median([value[0] for value in values])), 3),
                                            "correlationMedian": round(float(np.median([value[1] for value in values])), 3)}
                                           for centre, values in sorted(single.items()) if len(values) >= 5]}


def macro_lags(fs: int, size: float, realisation: int = 0) -> dict:
    """Macro 100 % against Macro 0 for the same impulses, for the four input-output paths."""
    _, plain, lines, _ = tracked(fs, size)
    capture, tide = impulse_train(fs, {**decay_setting(0.5), **size_setting(size), "macro": 1.0}, realisation=realisation)
    result = {"macroReadback": capture.meta["readback"]["id:6"], "mixReadback": capture.meta["readback"]["id:1"],
              "sizeReadback": capture.meta["readback"]["id:4"]}
    for channel in (0, 1):
        for output in (0, 1):
            mine = [line for line in lines if line["output"] == "LR"[output]]
            times, before = plain[channel]
            result[f"{'LR'[channel]}->{'LR'[output]}"] = pattern_lag(before[:, :, output], tide[channel][1][:, :, output], times, mine, fs)
    return result


def macro_decay() -> list:
    """Per Decay and realisation: gain of each clear line at Macro 100 % against the same line at Macro 0.

    One impulse on the left input, 7 s after the warm-up. The tide scales a
    whole response by an unknown, slowly varying filter, so only the ratio
    between lines of one response is meaningful: it is constant over Decay
    exactly when the Decay law of Macro 0 also holds at Macro 100 %.
    """
    fs = 48000
    arrivals = clear_arrivals(tracked(fs)[2], int(round(MACRO_TIME_SECONDS * fs)), fs)
    result = []
    for decay in DECAYS_CHECK:
        plain = single_response(fs, MACRO_TIME_SECONDS, 0, decay_setting(decay), 0.25)
        for realisation in MACRO_REALISATIONS:
            tide = single_response(fs, MACRO_TIME_SECONDS, 0, {**decay_setting(decay), "macro": 1.0}, 0.25, realisation)
            entry = {"decaySeconds": decay, "realisation": realisation, "lines": []}
            for output in (0, 1):
                mine = [(line, window) for line, window in arrivals if line["output"] == "LR"[output]]
                span = slice(min(w.start for _, w in mine), max(w.stop for _, w in mine))
                whole = lag_and_gain(tide[:, output], plain[:, output], span, range(int(0.008 * fs), int(0.024 * fs)))
                for line, window in mine:
                    lag, gain, correlation = lag_and_gain(tide[:, output], plain[:, output], window, range(whole[0] - 2, whole[0] + 3))
                    entry["lines"].append({"output": line["output"], "centreSamples": line["centreSamples"], "lagSamples": lag,
                                           "gain": round(gain, 5), "correlation": round(correlation, 3)})
            result.append(entry)
    return result


def macro_decay_summary(entries: list) -> dict:
    """Spread over Decay of each left-output line's gain relative to the mean gain of its own response.

    Only the lines of the left output are used (the cross-fed path to the right
    output changes from one instance to the next), and only where the Macro 0
    arrival is found with a correlation above 0.7.
    """
    relative = {}
    for entry in entries:
        mine = [line for line in entry["lines"] if line["output"] == "L" and line["correlation"] > 0.7]
        if len(mine) >= 3:
            mean = np.mean([line["gain"] for line in mine])
            for line in mine:
                relative.setdefault(line["centreSamples"], {}).setdefault(entry["decaySeconds"], []).append(line["gain"] / mean)
    lines = []
    for centre, by_decay in sorted(relative.items()):
        if len(by_decay) == len(DECAYS_CHECK):
            means = np.array([np.mean(by_decay[decay]) for decay in DECAYS_CHECK])
            lines.append({"output": "L", "centreSamples": centre, "decays": list(DECAYS_CHECK),
                          "relativeGainByDecay": [round(float(v), 4) for v in means],
                          "spreadDb": round(float(20 * np.log10(means.max() / means.min())), 3)})
    return {"lines": lines, "worstSpreadDb": max((line["spreadDb"] for line in lines), default=None)}


def measure_macro() -> dict:
    decay = macro_decay()
    return {"sizeAt48000": {str(size): macro_lags(48000, size) for size in MACRO_SIZES},
            "secondRealisationAt48000": macro_lags(48000, 100.0, realisation=1),
            "at44100": [macro_lags(44100, 100.0, realisation=r) for r in (0, 1)],
            "decay": decay, "decaySummary": macro_decay_summary(decay)}


# ---------------------------------------------------------------- the laws in one place

def line_table(result: dict) -> list:
    """One row per first-pass line: what a model needs, gathered from the parts."""
    taps = {(entry["output"], entry["lengthAt100"]): entry for entry in result["size"]["48000"]["tapGains"]}
    decay = {(entry["output"], entry["lengthAt100"]): entry for entry in result["decay"]["lines"]}
    share = {(entry["output"], entry["lengthAt100"]): entry["share"] for entry in result["decay"]["crossFeed"][0]["leftInputShare"]}
    table = []
    for output in "LR":
        mine = sorted((line for line in named_lines(result["size"], 48000) if line["output"] == output), key=lambda line: line["lengthAt100"])
        for index, line in enumerate(mine, 1):
            key = (output, line["lengthAt100"])
            weight = decay[key]["weight"] if key in decay else None
            linear = key in decay and decay[key].get("knee", {}).get("linearRms", 1.0) < 1e-4
            table.append({"output": output, "index": index, "lengthAt100": line["lengthAt100"], "phaseAt48000Rad": line["phaseRad"],
                          "tapGainAtDecayHalfSecond": taps.get(key, {}).get("tapGainMean"), "leftInputShare": share.get(key),
                          "weightByDecay": weight, "weightIsLinearBelowTheKnee": linear,
                          "weightSlopePerSecond": round((weight[DECAYS_WIDE.index(30.0)] - 1) / (WEIGHT_KNEE_SECONDS - 0.5), 6) if linear else None})
    return table


def phase_progression(table: list) -> dict:
    """phi_n = offset + step n over the lines in the order L1, R1, L2, R2, ... (n from 0), by a scan of the step."""
    index = np.array([2 * (row["index"] - 1) + (row["output"] == "R") for row in table], float)
    phase = np.array([row["phaseAt48000Rad"] for row in table])
    steps = np.arange(0.0, 2 * np.pi, 1e-5)
    turned = np.exp(1j * (phase[None, :] - steps[:, None] * index[None, :]))
    best = int(np.argmax(np.abs(turned.mean(axis=1))))
    offset = float(np.angle(turned[best].mean()))
    residual = np.angle(turned[best] * np.exp(-1j * offset))
    return {"stepRad": round(float(steps[best]), 5), "stepCycles": round(float(steps[best] / (2 * np.pi)), 6),
            "offsetRad": round(offset % (2 * np.pi), 4), "lines": len(table),
            "residualRmsRad": round(float(residual.std()), 5), "residualMaxRad": round(float(np.abs(residual).max()), 5)}


def laws(result: dict) -> dict:
    """The measured laws as formulas and constants (Macro 0, Mix 100 %); the evidence is in the parts."""
    rates = result["rate"]["rates"]
    table = line_table(result)
    return {
        "delay": "D[samples] = fixed(fs) + (fs/44100) floor(P s + 0.5); s = Size/100, P = lengthAt100 (whole 44.1 kHz samples)",
        "modulation": "position p = D + (fs/44100) A sin(theta) with theta taken when the arrival is read; A = 0.88 ms; theta is a "
                      "float32 phase that grows by float32(2 pi 0.6/44100) per 44.1 kHz sample and wraps at 2 pi (0.599878 Hz)",
        "gain": "gain = tap w(T) 10^(-3 P s / (44100 T)); T = Decay in seconds; w(T) = 1 + slope (min(T, 6) - 0.5)",
        "fixedSamples": {fs: entry["fixedSamples"] for fs, entry in rates.items()},
        "fixedSamplesAccumulatorModel": {fs: entry["accumulatorModel"]["fixedSamples"] for fs, entry in rates.items()},
        "fixedMs": {fs: entry["fixedMs"] for fs, entry in rates.items()},
        "depthMs": round(float(np.mean([line["depthMs"] for line in result["lfo"]["lines"]])), 5),
        "rateHz": result["lfo"]["float32Accumulator"]["rateHz"], "weightKneeSeconds": WEIGHT_KNEE_SECONDS,
        "phaseProgressionAt48000": phase_progression(table), "weightDecays": list(DECAYS_WIDE), "lines": table,
    }


# ---------------------------------------------------------------- entry point

PARTS = {"size": lambda result: measure_size(), "decay": lambda result: measure_decay(result["size"]),
         "rate": lambda result: measure_rate(result["size"]), "lfo": lambda result: measure_lfo(),
         "macro": lambda result: measure_macro()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("parts", nargs="*", metavar="part", help=f"parts to measure again, of {', '.join(PARTS)} (default: all)")
    arguments = parser.parse_args()
    unknown = set(arguments.parts) - set(PARTS)
    if unknown:
        parser.error(f"unknown part {sorted(unknown)}")
    result = json.loads(DATA.read_text()) if DATA.exists() else {}
    result["reference"] = {**revocean.EXPECTED_PLUGIN, "identity": revocean.identity(), "mode": "Tide",
                           "state": "neutral baseline, Mix 100 %, Macro 0 except in the macro part (Macro 100 %)"}
    result["method"] = {"windowHalfWidthSeconds": WINDOW_SECONDS, "guardSeconds": GUARD_SECONDS,
                        "phaseFitHz": [float(PHASE_FREQUENCIES[0]), float(PHASE_FREQUENCIES[-1])],
                        "impulseSpacingSeconds": SPACING_SECONDS, "captureSeconds": CAPTURE_SECONDS, "coreRate": CORE_RATE}
    for name in PARTS:
        if not arguments.parts or name in arguments.parts:
            result[name] = PARTS[name](result)
            print(f"measured {name}")
    if all(name in result for name in PARTS):
        result["laws"] = laws(result)
    DATA.parent.mkdir(exist_ok=True)
    DATA.write_text(json.dumps(result, indent=1) + "\n")
    print(f"wrote {DATA} ({DATA.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
