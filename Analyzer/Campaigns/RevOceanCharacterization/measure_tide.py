#!/usr/bin/env python3
"""Tide layer of Rev OCEAN: what the Macro control does to the Macro 0 network.

The script regenerates `tide_structural_data/tide.json` from captures. Every
number in `findings/tide.md` that is not marked "exploration" is computed here.

Method. The Macro 0 network is deterministic, so any stimulus can be rendered
through it. The Tide layer turns out to be

    input channel -> [dry * cos(m pi/2) + comb delay * sin(m pi/2)] -> Macro 0 network
                  -> per output channel: voice A on the early arrivals, voice B on the later ones

with a deterministic delay D(t) inside the comb. A unit impulse at t therefore
leaves the comb at t + D, t + 2D, ... and reaches the unchanged network at those
times. Rendering impulses AT those times through Macro 0 gives references
(`reference_stimuli`); a Macro > 0 response is then a short filter applied to
them, and that filter is fitted per impulse (`fit_voice`) on the part of the
response that holds voice A only (everything before the ninth first-pass
arrival). The quality of the description is the null of that fit.

Run from this folder (about one minute once the captures are cached):

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    $PY measure_tide.py
"""
from __future__ import annotations

import json
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

import datasets
import revocean

FS = revocean.SAMPLE_RATE
AMP = datasets.IMPULSE_AMPLITUDE
OUTPUT = Path(__file__).resolve().parent / "tide_structural_data" / "tide.json"

# Model constants. They are results of this script (see `measure_delay` and
# `voice_laws`); the references below are built with them, so the script checks
# its own constants: a wrong delay shows up as a fractional delay in the fits.
DELAY_MID = 469.34           # samples at 48 kHz
DELAY_SWING = 256.90         # samples
DELAY_PERIOD = 200.0         # seconds, triangle, starts at mid going up when processing starts
RIGHT_INPUT_LEAD = 0.25      # the right input's delay runs a quarter period ahead
COMB_GAIN = 10.0 ** (-8.0 / 20.0)   # the comb feeds back -8 dB, inverted
COMB_HIGH_PASS_HZ = 35.0     # one-pole high-pass in the delayed path
GAIN_MAX = 1.40
VOICE_A_END = 2640           # raw samples after the impulse; the ninth first-pass arrival comes later
LONG_MACROS = ((1.0, 0), (1.0, 1), (1.0, 2), (1.0, 3), (0.9, 0), (0.75, 0), (0.5, 0), (0.25, 0), (0.10, 0))
SHORT_MACROS = (0.002, 0.01, 0.02, 0.05)
NFFT = 4096
OMEGA = 2.0 * np.pi * np.fft.rfftfreq(NFFT)
Z1 = np.exp(-1j * OMEGA)
OCTAVES = (100, 200, 400, 800, 1600, 3200, 6400, 12800)


# ---------------------------------------------------------------- model

def triangle(x):
    """Unit triangle: 0 at x = 0 going up, +1 at 0.25, -1 at 0.75, period 1."""
    x = (np.asarray(x, float) + 0.25) % 1.0
    return 1.0 - 4.0 * np.abs(x - 0.5)


def tide_delay(seconds, channel=0):
    """Comb delay in samples at `seconds` after processing started, for input channel 0 (L) or 1 (R)."""
    return DELAY_MID + DELAY_SWING * triangle(np.asarray(seconds, float) / DELAY_PERIOD + RIGHT_INPUT_LEAD * channel)


def exit_times(time, channel=0, orders=6, warmup=revocean.WARMUP_SECONDS):
    """Times (samples after the warm-up) at which an impulse at `time` leaves the comb: first pass, then each recirculation.

    The delay line is read at the output, so the delay is the one in force at the exit."""
    out, start = [], float(time)
    for _ in range(orders):
        leave = start + tide_delay(warmup + start / FS, channel)
        for _ in range(4):
            leave = start + tide_delay(warmup + leave / FS, channel)
        out.append(leave)
        start = leave
    return np.array(out)


def low_pass(fc, q, z1=Z1):
    """Bilinear (RBJ) two-pole low-pass on an FFT grid (z1 = e^-jw)."""
    w0 = 2.0 * np.pi * fc / FS
    alpha, c = np.sin(w0) / (2.0 * q), np.cos(w0)
    return ((1.0 - c) / 2.0 * (1.0 + z1) ** 2) / ((1.0 + alpha) - 2.0 * c * z1 + (1.0 - alpha) * z1 ** 2)


def high_pass(fh, z1=Z1):
    r = np.exp(-2.0 * np.pi * fh / FS)
    return (1.0 + r) / 2.0 * (1.0 - z1) / (1.0 - r * z1)


def cutoff_curve(phase, low, high, bend):
    """Exponential interpolation from `high` at phase 0 to `low` at phase 1."""
    return low + (high - low) * (np.exp(-bend * phase) - np.exp(-bend)) / (1.0 - np.exp(-bend))


def hann_phase(gain, fc, peak_fc, gain_max):
    """Phase of the gain window g = gain_max sin^2(pi phase); the branch is chosen by the cutoff."""
    half = np.arcsin(np.sqrt(np.clip(gain / gain_max, 0.0, 1.0))) / np.pi
    return np.where(fc > peak_fc, half, 1.0 - half)


# ---------------------------------------------------------------- stimuli and captures

def impulse_train(times, channels, seconds):
    return revocean.impulses(seconds, [(int(t), int(c), AMP) for t, c in zip(times, channels)])


def reference_stimuli(times, channels, seconds):
    """Macro 0 stimuli carrying every impulse to its comb exits: first exit, second exit, later exits with the comb's weights."""
    exits = np.array([exit_times(t, c) for t, c in zip(times, channels)])
    first = impulse_train(np.round(exits[:, 0]), channels, seconds)
    second = impulse_train(np.round(exits[:, 1]), channels, seconds)
    later = revocean.impulses(seconds, [(int(round(e[m])), int(c), AMP * (-COMB_GAIN) ** (m - 2))
                                        for e, c in zip(exits, channels) for m in range(2, 6)])
    return exits, (first, second, later)


def capture(stimulus, macro, realisation=0, warmup=revocean.WARMUP_SECONDS):
    """Unit-impulse-scaled output at Decay 0.5 s, everything else at the baseline."""
    settings = dict(datasets.GRID_SETTINGS)
    if macro > 0.0:
        settings["macro"] = macro
    return revocean.capture(stimulus, settings, realisation=realisation, warmup=warmup).output.astype(np.float64) / AMP


def capture_all(jobs):
    """jobs: (stimulus, macro, realisation) tuples, rendered in parallel."""
    with ThreadPoolExecutor(8) as pool:
        return list(pool.map(lambda job: capture(*job), jobs))


def first_onset(reference, search=3000):
    level = np.abs(reference[:search])
    return int(np.argmax(level > 0.25 * level.max()))


def db(numerator, denominator):
    return float(10.0 * np.log10((np.sum(np.square(numerator)) + 1e-300) / (np.sum(np.square(denominator)) + 1e-300)))


# ---------------------------------------------------------------- the voice fit

def fit_voice(target, dry, delayed, start=None, comb_high_pass=COMB_HIGH_PASS_HZ):
    """Fit target = LP(fc, Q) * [g_dry dry + HP * sum_m g_m delayed_m(n - delta_m)].

    `dry` (may be None) and the `delayed` list are Macro 0 segments aligned with the target. Gains are solved
    linearly, cutoff, Q and one fractional delay per reference by least squares. Returns a dict with the null in dB."""
    n = len(target)
    references = ([] if dry is None else [dry]) + list(delayed)
    count = len(references)
    spectra = [np.fft.rfft(r, NFFT) for r in references]
    hp = high_pass(comb_high_pass)
    shaping = [1.0 if (dry is not None and m == 0) else hp for m in range(count)]

    def solve(p):
        lp = low_pass(np.exp(p[0]), np.exp(p[1]))
        cols = np.stack([np.fft.irfft(spectra[m] * lp * shaping[m] * np.exp(-1j * OMEGA * p[2 + m]), NFFT)[:n]
                         for m in range(count)], 1)
        gains, *_ = np.linalg.lstsq(cols, target, rcond=None)
        return cols, gains

    def residual(p):
        cols, gains = solve(p)
        return cols @ gains - target

    lower = np.array([np.log(150.0), np.log(0.3)] + [-6.0] * count)
    upper = np.array([np.log(23500.0), np.log(60.0)] + [6.0] * count)
    delays = [0.0] * count if start is None else list(start[2:])
    candidates = [] if start is None else [np.clip(start, lower + 1e-6, upper - 1e-6)]
    candidates += [np.array([np.log(fc), np.log(q)] + delays) for fc in np.geomspace(400.0, 20000.0, 10) for q in (1.4, 5.0)]
    costs = [float(np.sum(residual(p) ** 2)) for p in candidates]
    best = None
    for index in np.argsort(costs)[:2]:
        result = least_squares(residual, candidates[index], bounds=(lower, upper), x_scale=[0.1, 0.2] + [0.5] * count)
        if best is None or result.cost < best.cost:
            best = result
    cols, gains = solve(best.x)
    return {"fc": float(np.exp(best.x[0])), "q": float(np.exp(best.x[1])), "gains": gains, "delays": best.x[2:].copy(),
            "null": db(cols @ gains - target, target), "p": best.x.copy()}


def voice_a_window(first_reference, delay, macro):
    """[begin, end) relative to the impulse: from 60 samples before the first arrival to the end of voice A.

    At Macro 100 % everything is delayed, so the window is that much later; below, the undelayed copies are fitted
    and the window must end before their ninth arrival."""
    onset = first_onset(first_reference, 3800)              # first arrival of the first-exit reference
    if macro >= 1.0:
        return onset - 60, VOICE_A_END + int(delay)
    return onset - int(round(delay)) - 60, VOICE_A_END


def fit_capture(job):
    """Voice A of every impulse and output channel of one capture.

    Row: time (s), fc, Q, null, gains (dry, first exit, second exit, later exits), fractional delays (same four),
    comb delay of the model (samples)."""
    times, channels, seconds, macro, realisation = job
    stimulus = impulse_train(times, channels, seconds)
    _, references = reference_stimuli(times, channels, seconds)
    target = capture(stimulus, macro, realisation)
    first, second, later = (capture(s, 0.0) for s in references)
    dry = capture(stimulus, 0.0) if macro < 1.0 else None
    rows = np.full((2, len(times), 13), np.nan)
    for out in (0, 1):
        previous = None
        for k, (time, channel) in enumerate(zip(times, channels)):
            delay = exit_times(time, channel, orders=1)[0] - time
            begin, end = voice_a_window(first[time:time + 4500, out], delay, macro)
            cut = lambda x: x[time + begin:time + end, out]
            result = fit_voice(cut(target), None if dry is None else cut(dry), [cut(first), cut(second), cut(later)], previous)
            previous = result["p"] if result["null"] < -15.0 else None
            gains, delays = result["gains"], result["delays"]
            if dry is None:
                gains, delays = np.concatenate([[0.0], gains]), np.concatenate([[np.nan], delays])
            rows[out, k] = [time / FS, result["fc"], result["q"], result["null"], *gains, *delays, delay]
    return rows


# ---------------------------------------------------------------- analyses

def measure_delay(grid, fits):
    """Comb delay against plug-in time from the fractional delays of the four Macro 100 % captures."""
    seconds, delay, spread = [], [], []
    stack = np.stack([fits[(1.0, r)] for r in range(4)])            # realisation, channel, impulse, column
    for out in (0, 1):
        rows = stack[:, out]
        good = np.all((rows[:, :, 3] < -30.0) & (rows[:, :, 5] > 0.3) & (rows[:, :, 1] < 6000.0), axis=0)
        measured = np.round(grid + rows[0, :, 12]) - grid + rows[:, :, 9]   # first exit as fitted, per realisation
        seconds.append(revocean.WARMUP_SECONDS + (grid[good] + measured[:, good].mean(0)) / FS)
        delay.append(measured[:, good].mean(0))
        spread.append(measured[:, good].std(0))
    seconds, delay, spread = map(np.concatenate, (seconds, delay, spread))
    # the second exit, fitted on its own, against two delays of the model
    exits = np.array([exit_times(t, orders=2) for t in grid])
    rows = stack[:, 0]
    strong = (rows[:, :, 3] < -30.0) & (rows[:, :, 5] > 0.5)
    second_error = (np.round(exits[:, 1]) + rows[:, :, 10] - exits[:, 1])[strong]
    rising, falling = seconds < 49.0, seconds > 51.0
    up, down = np.polyfit(seconds[rising], delay[rising], 1), np.polyfit(seconds[falling], delay[falling], 1)
    apex = (down[1] - up[1]) / (up[0] - down[0])
    model = tide_delay(seconds)
    return {"points": int(len(seconds)),
            "risingSlopeSamplesPerSecond": float(up[0]), "risingValueAtStart": float(up[1]),
            "risingRmsSamples": float(np.std(delay[rising] - np.polyval(up, seconds[rising]))),
            "fallingSlopeSamplesPerSecond": float(down[0]),
            "fallingRmsSamples": float(np.std(delay[falling] - np.polyval(down, seconds[falling]))),
            "apexSeconds": float(apex), "apexSamples": float(np.polyval(up, apex)),
            "modelRmsSamples": float(np.sqrt(np.mean((delay - model) ** 2))), "modelMeanErrorSamples": float(np.mean(delay - model)),
            "spreadBetweenRealisationsSamples": float(np.median(spread)),
            "secondExitErrorSamples": {"median": float(np.median(second_error)),
                                       "quartiles": [float(v) for v in np.percentile(second_error, [25, 75])]},
            "impliedMid": float(up[1]), "impliedSwing": float((np.polyval(up, apex) - up[1])),
            "impliedPeriodSeconds": float(4.0 * apex)}


def measure_delay_long():
    """Coarse check of the whole triangle over 170 s of plug-in time from first-arrival onsets (60 s warm-up)."""
    times = np.arange(24000, 109 * FS, 24000)
    stimulus = impulse_train(times, np.zeros(len(times), int), 110.0)
    wet = revocean.capture(stimulus, {**datasets.GRID_SETTINGS, "macro": 1.0}, warmup=60.0).output.astype(np.float64) / AMP
    dry = revocean.capture(stimulus, datasets.GRID_SETTINGS, warmup=60.0).output.astype(np.float64) / AMP
    seconds, shift = [], []
    for time in times:
        for out in (0, 1):
            a, b = np.abs(wet[time:time + 4200, out]), np.abs(dry[time:time + 4200, out])
            if a.max() < 2e-3:
                continue
            onset = int(np.argmax(a > 0.03 * a.max())) - int(np.argmax(b > 0.03 * b.max()))
            if 150 < onset < 800:
                seconds.append(60.0 + time / FS)
                shift.append(onset)
    seconds, shift = np.array(seconds), np.array(shift, float)

    def error(p):
        return p[0] + p[1] * triangle(seconds / p[2]) - shift

    fit = least_squares(error, [470.0, 257.0, 200.0], loss="soft_l1", f_scale=2.0)
    inliers = np.abs(error(fit.x)) < 5.0
    return {"points": int(len(seconds)), "plugInSeconds": [float(seconds.min()), float(seconds.max())],
            "onsetMid": float(fit.x[0]), "onsetSwing": float(fit.x[1]), "periodSeconds": float(fit.x[2]),
            "minimumSamples": float(fit.x[0] - fit.x[1]), "rmsInliersSamples": float(np.std(error(fit.x)[inliers])),
            "note": "onset differences carry a bias of about +1 sample and the 0.6 Hz tap modulation (+-2 samples)"}


def voice_laws(fits):
    """Gain, cutoff and Q of voice A against the wave phase, per Macro value."""
    out = {}
    for macro in sorted({m for m, _ in fits}, reverse=True):
        if macro < 0.2:
            continue
        rows = np.concatenate([fits[key].reshape(-1, 13) for key in fits if key[0] == macro])
        rows = rows[rows[:, 3] < (-27.0 if macro >= 1.0 else -22.0)]
        fc, q, dry, wet = rows[:, 1], rows[:, 2], rows[:, 4], rows[:, 5]
        gain = np.hypot(dry, wet)
        strong = gain > 0.5
        gain_max = float(np.percentile(gain, 99.5))
        peak_fc = float(np.median(fc[gain > 0.997 * gain_max]))
        phase = hann_phase(gain, fc, peak_fc, gain_max)
        use = (gain > 0.03) & (gain < 0.985 * gain_max)
        fit = least_squares(lambda p: np.log(cutoff_curve(phase[use], *p)) - np.log(fc[use]), [fc.min(), 19000.0, 2.9],
                            bounds=([50.0, 12000.0, 0.01], [15000.0, 24000.0, 8.0]))
        centres = np.geomspace(500.0, 16000.0, 31)
        table = []
        for lo, hi in zip(centres[:-1], centres[1:]):
            inside = (fc >= lo) & (fc < hi) & (gain > 0.05)
            if inside.sum() >= 4:
                table.append([round(float(np.median(fc[inside])), 1), round(float(np.median(q[inside])), 3), int(inside.sum())])
        plateau = (fc > max(5200.0, 1.05 * fit.x[0])) & (fc < 12000.0) & (gain > 0.3)
        entry = {"impulses": int(len(rows)), "medianNullDb": float(np.median(rows[:, 3])),
                 "gainMax": gain_max, "cutoffAtGainMax": peak_fc,
                 "cutoffLow": float(fit.x[0]), "cutoffHigh": float(fit.x[1]), "cutoffBend": float(fit.x[2]),
                 "cutoffCurveRmsPercent": float(100.0 * np.sqrt(np.mean(fit.fun ** 2))),
                 "qPlateau": float(np.median(q[plateau])) if plateau.sum() > 3 else None,
                 "qAgainstCutoff": table,
                 "combGain": float(np.median(-rows[strong, 6] / wet[strong])),
                 "combGainQuartiles": [float(v) for v in np.percentile(-rows[strong, 6] / wet[strong], [25, 75])]}
        if macro >= 1.0:
            entry["laterExitGainRatio"] = float(np.median(rows[strong, 7] / wet[strong]))
        else:
            angle = np.degrees(np.arctan2(wet[strong], dry[strong]))
            entry.update({"mixAngleDegrees": float(np.median(angle)), "mixAngleQuartiles": [float(v) for v in np.percentile(angle, [25, 75])],
                          "mixAngleExpected": 90.0 * macro, "dryDelaySamples": float(np.median(rows[strong, 8]))})
        out[f"{macro:g}"] = entry
    return out


def round_value_tests(fits, laws):
    """Score round hypotheses for the Macro 100 % cutoff curve and gain maximum."""
    rows = np.concatenate([fits[key].reshape(-1, 13) for key in fits if key[0] == 1.0])
    rows = rows[(rows[:, 3] < -27.0) & (rows[:, 5] > 0.03)]
    fc, gain = rows[:, 1], rows[:, 5]
    law = laws["1"]
    result = {}
    for name, (low, high, bend, top) in {"fitted": (law["cutoffLow"], law["cutoffHigh"], law["cutoffBend"], law["gainMax"]),
                                         "500 Hz, 20 kHz, bend 3, gain 1.4": (500.0, 20000.0, 3.0, 1.4),
                                         "460 Hz, 19.27 kHz, bend 2.94, gain 1.4": (460.0, 19270.0, 2.94, 1.4)}.items():
        phase = hann_phase(gain, fc, law["cutoffAtGainMax"], top)
        use = gain < 0.985 * top
        result[name] = float(100.0 * np.sqrt(np.mean((np.log(cutoff_curve(phase[use], low, high, bend)) - np.log(fc[use])) ** 2)))
    return {"cutoffCurveRmsPercent": result}


def phase_statistics(fits, laws):
    """Wave phase against time from the cutoff, its mean rate, start phases and random wander."""
    seconds = None
    curves = {}
    for (macro, realisation), rows in fits.items():
        if macro < 0.2:
            continue
        law = laws[f"{macro:g}"]
        for out in (0, 1):
            r = rows[out]
            seconds = r[:, 0]
            gain = np.hypot(r[:, 4], r[:, 5])
            x = (r[:, 1] - law["cutoffLow"]) / (law["cutoffHigh"] - law["cutoffLow"]) * (1 - np.exp(-law["cutoffBend"])) + np.exp(-law["cutoffBend"])
            phase = np.where((r[:, 3] < -15.0) & (gain > 0.01), -np.log(np.clip(x, 1e-9, None)) / law["cutoffBend"], np.nan)
            turns, last = 0, None
            for k in range(len(phase)):
                if np.isnan(phase[k]):
                    continue
                if last is not None and phase[k] < last - 0.5:
                    turns += 1
                last, phase[k] = phase[k], phase[k] + turns
            first = phase[np.isfinite(phase)][0]
            curves[(macro, realisation, out)] = phase - np.floor(first - 0.4)
    plug_in = seconds + revocean.WARMUP_SECONDS
    design, values = [], []
    for (macro, realisation, out), phase in curves.items():
        good = np.isfinite(phase)
        design += [[t, 1.0 - out, float(out)] for t in plug_in[good]]
        values += list(phase[good])
    design, values = np.array(design), np.array(values)
    (rate, start_left, start_right), *_ = np.linalg.lstsq(design, values, rcond=None)
    wander = np.stack([curves[key] - (rate * plug_in + (start_right if key[2] else start_left)) for key in curves])
    rates = np.diff(np.stack(list(curves.values())), axis=1) / 0.5
    full = np.array([key[0] == 1.0 for key in curves])
    flat = rates[full][np.isfinite(rates[full])]
    autocorrelation = []
    for lag in range(0, 41):
        a, b = rates[full][:, :rates.shape[1] - lag] - flat.mean(), rates[full][:, lag:] - flat.mean()
        both = np.isfinite(a) & np.isfinite(b)
        autocorrelation.append(float(np.sum(a[both] * b[both]) / both.sum()))
    autocorrelation = (np.array(autocorrelation) / autocorrelation[0]).round(3).tolist()
    pairs = [float(np.corrcoef(np.nan_to_num(rates[i] - flat.mean()), np.nan_to_num(rates[i + 1] - flat.mean()))[0, 1])
             for i in range(0, 8, 2)]
    per_macro = {}
    for macro in sorted({key[0] for key in curves}):
        slopes = [np.polyfit(plug_in[np.isfinite(p)], p[np.isfinite(p)], 1)[0] for key, p in curves.items() if key[0] == macro]
        per_macro[f"{macro:g}"] = float(np.mean(slopes))
    return {"curves": len(curves), "meanRateCyclesPerSecond": float(rate), "meanPeriodSeconds": float(1.0 / rate),
            "meanRatePerMacro": per_macro,
            "phaseAtProcessingStart": {"left": float(start_left % 1.0), "right": float(start_right % 1.0)},
            "wanderSdCycles": float(np.nanstd(wander)), "wanderRangeCycles": [float(np.nanmin(wander)), float(np.nanmax(wander))],
            "wanderSdAgainstPlugInSeconds": [[float(plug_in[i]), float(np.nanstd(wander[:, i]))] for i in range(0, len(plug_in), 20)],
            "rateSdCyclesPerSecond": float(flat.std()),
            "ratePercentiles1_5_25_50_75_95_99": [float(v) for v in np.percentile(flat, [1, 5, 25, 50, 75, 95, 99])],
            "rateNegativeShare": float(np.mean(flat < 0.0)),
            "rateAutocorrelationHalfSecondLags": autocorrelation,
            "leftRightRateCorrelationPerRealisation": pairs}


def voice_b(times, channels, seconds, rows_per_realisation, out, law):
    """Voice B on first-pass arrivals 9 to 11 of output `out` for impulses on the same-side input.

    Its cutoff and gain are compared with voice A's law taken half a cycle later."""
    stimulus = impulse_train(times, channels, seconds)
    _, references = reference_stimuli(times, channels, seconds)
    first, second, later = (capture(s, 0.0) for s in references)
    size, run_in = 8192, 400
    omega = 2.0 * np.pi * np.fft.rfftfreq(size)
    z1 = np.exp(-1j * omega)
    hp = high_pass(COMB_HIGH_PASS_HZ, z1)
    records = []
    for realisation, rows in enumerate(rows_per_realisation):
        target = capture(stimulus, 1.0, realisation)
        for k, time in enumerate(times):
            row = rows[out, k]
            if channels[k] != out or not row[3] < -25.0:
                continue
            delay = int(row[12])
            lo, hi = 2660 + delay - run_in, 3900 + delay
            n = hi - lo
            cut = lambda x: x[time + lo:time + hi, out]
            level = np.abs(cut(first)).copy()
            level[:run_in] = 0.0
            mask = np.zeros(n)
            for _ in range(3):                      # the three strongest first-order arrivals are arrivals 9, 10, 11
                peak = int(np.argmax(level))
                mask[max(0, peak - 45):peak + 14] = 1.0
                level[max(0, peak - 60):peak + 60] = 0.0
            lp_a = low_pass(row[1], row[2], z1) * hp
            parts = (cut(first) * (1.0 - mask), cut(second), cut(later))
            voice_a = sum(gain * np.fft.irfft(np.fft.rfft(part, size) * lp_a * np.exp(-1j * omega * shift), size)[:n]
                          for gain, shift, part in zip(row[5:8], row[9:12], parts))
            rest = (cut(target) - voice_a)[run_in:]
            spectrum = np.fft.rfft(cut(first) * mask, size) * hp * np.exp(-1j * omega * row[9])

            def model(p):
                shape = np.fft.irfft(spectrum * low_pass(np.exp(p[0]), np.exp(p[1]), z1), size)[run_in:n]
                gain = float(np.dot(shape, rest) / np.dot(shape, shape))
                return gain * shape, gain

            starts = [np.array([np.log(f), np.log(q)]) for f in np.geomspace(500.0, 19000.0, 12) for q in (1.4, 4.0)]
            best = starts[int(np.argmin([np.sum((model(p)[0] - rest) ** 2) for p in starts]))]
            fit = least_squares(lambda p: model(p)[0] - rest, best, bounds=([np.log(200.0), np.log(0.5)], [np.log(22000.0), np.log(12.0)]))
            fitted, gain_b = model(fit.x)
            phase_b = (float(hann_phase(np.array(row[5]), np.array(row[1]), law["cutoffAtGainMax"], law["gainMax"])) + 0.5) % 1.0
            records.append([row[5], row[1], gain_b, float(np.exp(fit.x[0])), float(np.exp(fit.x[1])),
                            float(cutoff_curve(phase_b, law["cutoffLow"], law["cutoffHigh"], law["cutoffBend"])),
                            law["gainMax"] * np.sin(np.pi * phase_b) ** 2, db(fitted - rest, cut(target)[run_in:])])
    records = np.array(records)
    clear = (records[:, 6] > 0.5) & (records[:, 2] > 0.3)          # voice B expected and found well above the floor
    ratio = records[clear, 3] / records[clear, 5]
    total = records[clear, 0] + records[clear, 2]
    return {"impulses": int(len(records)), "used": int(clear.sum()),
            "cutoffRatioMeasuredToPredictedMedian": float(np.median(ratio)),
            "cutoffRatioQuartiles": [float(v) for v in np.percentile(ratio, [25, 75])],
            "gainSumMedian": float(np.median(total)), "gainSumQuartiles": [float(v) for v in np.percentile(total, [25, 75])],
            "gainSquareSumMedian": float(np.median(records[clear, 0] ** 2 + records[clear, 2] ** 2)),
            "gainSquareSumQuartiles": [float(v) for v in np.percentile(records[clear, 0] ** 2 + records[clear, 2] ** 2, [25, 75])],
            "gainMeasuredToPredictedMedian": float(np.median(records[clear, 2] / records[clear, 6])),
            "windowNullMedianDb": float(np.median(records[clear, 7])),
            "note": "three isolated arrivals in a window that also holds other voice B arrivals; gains read 5 to 10 % low"}


def partition(grid, fits):
    """Which Macro 0 arrivals carry voice B: left-output responses taken while voice A is closed.

    The comb-expanded Macro 0 reference is filtered with the best single low-pass; an arrival that belongs to voice B
    then appears at about +3 dB (gain 1.40), one that belongs to voice A is missing."""
    channels = np.zeros(len(grid), int)
    stimulus = impulse_train(grid, channels, datasets.GRID_SECONDS)
    _, references = reference_stimuli(grid, channels, datasets.GRID_SECONDS)
    first, second, later = (capture(s, 0.0) for s in references)
    size = 16384
    z1 = np.exp(-2j * np.pi * np.fft.rfftfreq(size))
    hp = high_pass(COMB_HIGH_PASS_HZ, z1)
    cases = []
    for realisation in range(4):
        rows = fits[(1.0, realisation)][0]
        closed = np.where(rows[:, 5] < 0.012)[0]
        if not len(closed):
            continue
        k = int(closed[0])
        time, delay = grid[k], int(rows[k, 12])
        lo, hi = time + 2000 + delay, time + 9000 + delay
        target = capture(stimulus, 1.0, realisation)[lo:hi, 0]
        expanded = np.fft.rfft(first[lo:hi, 0] - COMB_GAIN * second[lo:hi, 0] + COMB_GAIN ** 2 * later[lo:hi, 0], size)
        best = None
        for fc in np.geomspace(2500.0, 7000.0, 30):
            for q in (1.4, 1.6, 1.8, 2.0, 2.3):
                shaped = np.fft.irfft(expanded * low_pass(fc, q, z1) * hp, size)[:hi - lo]
                a, b = shaped[700:1900], target[700:1900]                  # raw 2700 to 3900: arrivals 9 to 11
                score = np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b))
                if best is None or score > best[0]:
                    best = (score, fc, q, shaped)
        _, fc, q, shaped = best
        level = np.abs(first[lo:hi, 0]).copy()
        arrivals = []
        while len(arrivals) < 26:
            peak = int(np.argmax(level[:4200]))
            if level[peak] < 0.006:
                break
            arrivals.append([peak + 2000, round(float(first[lo + peak, 0]) * 1000.0, 1),
                             round(db(target[peak - 4:peak + 40], shaped[peak - 4:peak + 40]), 1)])
            level[max(0, peak - 30):peak + 30] = 0.0
        cases.append({"realisation": realisation, "seconds": float(time / FS), "voiceAGain": float(rows[k, 5]),
                      "voiceBCutoff": float(fc), "voiceBQ": q,
                      "columns": ["raw position (samples after the impulse, comb delay removed)", "Macro 0 peak (1e-3)",
                                  "level re filtered Macro 0 (dB)"],
                      "arrivals": sorted(arrivals)})
    return cases


def input_channels():
    """Alternating left and right impulses: which element belongs to the input channel and which to the output."""
    times = np.arange(FS, 59 * FS, 24000)
    channels = np.arange(len(times)) % 2
    rows = fit_capture((times, channels, 60.0, 1.0, 0))
    summary = {}
    for name, picked in (("leftInput", channels == 0), ("rightInput", channels == 1)):
        good = picked[None, :] & (rows[:, :, 3] < -27.0) & (rows[:, :, 5] > 0.3)
        summary[name] = {"impulsesFitted": int(good.sum()), "medianNullDb": float(np.median(rows[:, :, 3][good])),
                         "fractionalDelayMedian": float(np.median(rows[:, :, 9][good])),
                         "fractionalDelayAbsMax": float(np.max(np.abs(rows[:, :, 9][good]))),
                         "combGain": float(np.median(-rows[:, :, 6][good] / rows[:, :, 5][good]))}
    # The filter belongs to the output: its log cutoff read from right-input impulses lies on the curve through the
    # left-input impulses of the same output.
    smooth = {}
    for out, label in ((0, "leftOutput"), (1, "rightOutput")):
        good = (rows[out, :, 3] < -25.0) & (rows[out, :, 5] > 0.05)
        errors = []
        for k in np.where(good & (channels == 1))[0]:
            if 0 < k < len(times) - 1 and good[k - 1] and good[k + 1]:
                middle = 0.5 * (np.log(rows[out, k - 1, 1]) + np.log(rows[out, k + 1, 1]))
                errors.append(np.log(rows[out, k, 1]) - middle)
        smooth[label] = {"points": len(errors), "rmsPercent": float(100.0 * np.sqrt(np.mean(np.square(errors)))),
                         "meanPercent": float(100.0 * np.mean(errors))}
    summary["cutoffOfRightInputAgainstNeighbouringLeftInput"] = smooth
    return summary, (times, channels, 60.0, rows)


def dry_gain_range():
    """Range of the undelayed first-arrival gain (0.3 to 1 kHz) over 11 s at very low Macro values.

    With g = (1 - depth) + depth * gain_max * sin^2(pi phase), scaled by cos(m pi/2), the minimum gives the depth."""
    times = np.arange(FS, 11 * FS + 1, 24000)
    stimulus = impulse_train(times, np.zeros(len(times), int), 12.0)
    dry, *wets = capture_all([(stimulus, 0.0, 0)] + [(stimulus, m, 0) for m in SHORT_MACROS])
    frequencies = np.fft.rfftfreq(2048, 1.0 / FS)
    band = (frequencies > 300.0) & (frequencies < 1000.0)
    out = {}
    for macro, wet in zip(SHORT_MACROS, wets):
        gains = []
        for time in times:
            for channel in (0, 1):
                begin = time + first_onset(dry[time:time + 3000, channel]) - 50
                a = np.abs(np.fft.rfft(wet[begin:begin + 380, channel], 2048))[band]
                b = np.abs(np.fft.rfft(dry[begin:begin + 380, channel], 2048))[band]
                gains.append(np.sqrt(np.sum(a ** 2) / np.sum(b ** 2)) / np.cos(macro * np.pi / 2.0))
        out[f"{macro:g}"] = {"gainMin": float(min(gains)), "gainMax": float(max(gains)), "depth": float(1.0 - min(gains)),
                             "depthPerMacro": float((1.0 - min(gains)) / macro), "nullAgainstMacro0Db": revocean.null_db(wet, dry)}
    return out


def gain_maximum():
    """Second estimate of the voice gain maximum below Macro 100 %: undelayed first arrivals, 0.3 to 1 kHz.

    It needs no filter model, but it includes the low-pass's own rise below its peak (1 to 5 %)."""
    grid = datasets.grid_times(0)
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    macros = [m for m, r in LONG_MACROS if m < 1.0]
    dry, *wets = capture_all([(stimulus, 0.0, 0)] + [(stimulus, m, 0) for m in macros])
    frequencies = np.fft.rfftfreq(4096, 1.0 / FS)
    band = (frequencies > 300.0) & (frequencies < 1000.0)
    taper = np.ones(400)
    taper[-64:] = np.cos(np.linspace(0.0, np.pi / 2.0, 64)) ** 2
    out = {}
    for macro, wet in zip(macros, wets):
        gains = []
        for time in grid:
            for channel in (0, 1):
                begin = time + first_onset(dry[time:time + 3000, channel]) - 50
                a = np.abs(np.fft.rfft(wet[begin:begin + 400, channel] * taper, 4096))[band]
                b = np.abs(np.fft.rfft(dry[begin:begin + 400, channel] * taper, 4096))[band]
                gains.append(np.sqrt(np.sum(a ** 2) / np.sum(b ** 2)) / np.cos(macro * np.pi / 2.0))
        out[f"{macro:g}"] = {"percentile99": float(np.percentile(gains, 99)), "minimum": float(min(gains))}
    return out


def undelayed_at_full(grid):
    """Level of whatever sits at the undelayed first-arrival positions at Macro 100 %, relative to Macro 0."""
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    dry, wet = capture_all([(stimulus, 0.0, 0), (stimulus, 1.0, 0)])
    levels = []
    for time in grid[1:]:
        for channel in (0, 1):
            begin = time + first_onset(dry[time:time + 3000, channel]) - 50
            levels.append(db(wet[begin:begin + 380, channel], dry[begin:begin + 380, channel]))
    return {"medianDb": float(np.median(levels)), "maxDb": float(np.max(levels)),
            "note": "380 samples from the first Macro 0 arrival; the comb delay is at least 425 samples in these captures"}


def compact_laws(fits, laws):
    """Closed forms: Q against cutoff at Macro 100 % (piecewise linear), cutoff range and Q plateau against Macro."""
    rows = np.concatenate([fits[key].reshape(-1, 13) for key in fits if key[0] == 1.0])
    rows = rows[(rows[:, 3] < -27.0) & (rows[:, 5] > 0.05) & (rows[:, 1] > 560.0) & (rows[:, 1] < 12000.0)]
    fc, q = rows[:, 1], rows[:, 2]

    def q_curve(p, f):
        knots = [500.0, p[0], p[1], p[2], 12000.0]         # Hz, rising
        values = [p[3], p[4], p[5], p[6], p[6]]
        return np.interp(f, knots, values)

    fit = least_squares(lambda p: q_curve(p, fc) - q, [1000.0, 3000.0, 4950.0, 5.0, 7.0, 2.1, 1.4])
    macros = np.array(sorted((float(m) for m in laws), reverse=True))
    low = np.array([laws[f"{m:g}"]["cutoffLow"] for m in macros])
    plateau = np.array([laws[f"{m:g}"]["qPlateau"] for m in macros])
    high = laws["1"]["cutoffHigh"]

    def low_curve(bend, m):
        return high - (high - low[0]) * (1.0 - np.exp(-bend * m)) / (1.0 - np.exp(-bend))

    def plateau_curve(p, m):
        return p[0] + (p[1] - p[0]) * (np.exp(-p[2] * m) - np.exp(-p[2])) / (1.0 - np.exp(-p[2]))

    unified = {}
    for m, top in zip(macros, plateau):                     # knots at 0.5, 1, 3, 5 kHz: 5.2, 7.0, 1.5 plateau, plateau
        other = np.concatenate([fits[key].reshape(-1, 13) for key in fits if key[0] == m])
        other = other[(other[:, 3] < -27.0) & (np.hypot(other[:, 4], other[:, 5]) > 0.05) & (other[:, 1] > 560.0) & (other[:, 1] < 12000.0)]
        error = np.interp(other[:, 1], [500.0, 1000.0, 3000.0, 5000.0], [5.2, 7.0, 1.5 * top, top]) - other[:, 2]
        unified[f"{m:g}"] = {"points": int(len(error)), "rms": float(np.sqrt(np.mean(error ** 2))), "maxAbs": float(np.max(np.abs(error)))}
    low_fit = least_squares(lambda p: np.log(low_curve(p[0], macros)) - np.log(low), [2.5])
    plateau_fit = least_squares(lambda p: plateau_curve(p, macros) - plateau, [1.4, 7.0, 3.0])
    round_knots = np.array([1000.0, 3000.0, 5000.0, 5.2, 7.0, 2.1, 1.4])
    return {"qAgainstCutoffAtMacro100": {"knotsHz": [500.0] + [float(v) for v in fit.x[:3]],
                                         "q": [float(v) for v in fit.x[3:]], "aboveLastKnot": "constant",
                                         "rms": float(np.sqrt(np.mean(fit.fun ** 2))), "points": int(len(fc)),
                                         "rmsWithRoundKnots_500_1000_3000_5000Hz_q_5.2_7_2.1_1.4":
                                             float(np.sqrt(np.mean((q_curve(round_knots, fc) - q) ** 2)))},
            "qAgainstCutoffAllMacros": {"form": "piecewise linear in cutoff: 5.2 at 500 Hz, 7.0 at 1 kHz, 1.5 plateau at 3 kHz, plateau from 5 kHz",
                                        "errorPerMacro": unified},
            "cutoffLowAgainstMacro": {"form": "high - (high - low(1)) (1 - exp(-bend m)) / (1 - exp(-bend))", "high": high,
                                      "lowAtMacro1": float(low[0]), "bend": float(low_fit.x[0]),
                                      "macros": macros.tolist(), "measured": low.tolist(),
                                      "errorPercent": (100.0 * (low_curve(low_fit.x[0], macros) / low - 1.0)).round(2).tolist()},
            "qPlateauAgainstMacro": {"form": "q1 + (q0 - q1) (exp(-bend m) - exp(-bend)) / (1 - exp(-bend))",
                                     "q1": float(plateau_fit.x[0]), "q0": float(plateau_fit.x[1]), "bend": float(plateau_fit.x[2]),
                                     "macros": macros.tolist(), "measured": plateau.tolist(),
                                     "residual": plateau_fit.fun.round(3).tolist(),
                                     "residualWithRoundValues_1.4_7_3": (plateau_curve([1.4, 7.0, 3.0], macros) - plateau).round(3).tolist()}}


def structure(grid, fits):
    """Is the filter inside the loop? Band decay and late energy at Macro 0 and 100 %."""
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    exits, references = reference_stimuli(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    dry, wet, shifted = capture_all([(stimulus, 0.0, 0), (stimulus, 1.0, 0), (references[0], 0.0, 0)])
    frequencies = np.fft.rfftfreq(3000, 1.0 / FS)
    window = np.hanning(3000)

    def stages(signal, start, out):
        spectra = [np.abs(np.fft.rfft(signal[start + a:start + a + 3000, out] * window)) ** 2 for a in (0, 6000, 12000)]
        return np.array([[s[(frequencies >= lo) & (frequencies < hi)].sum() for lo, hi in zip(OCTAVES[:-1], OCTAVES[1:])] for s in spectra])

    rows = fits[(1.0, 0)]
    decay = {}
    for out, label in ((0, "left"), (1, "right")):
        open_voice = rows[out, :, 5] > 0.7
        zero = np.array([stages(dry, t + 1250, out) for t in grid])
        full = np.array([stages(wet, t + 1250 + int(d), out) for t, d in zip(grid, rows[out, :, 12])])[open_voice]
        decay[label] = {"macro0_dB_after_6000_and_12000_samples": (10 * np.log10(zero[:, 1:].mean(0) / zero[:, 0].mean(0))).round(1).tolist(),
                        "macro100_dB_after_6000_and_12000_samples": (10 * np.log10(full[:, 1:].mean(0) / full[:, 0].mean(0))).round(1).tolist()}
    # late energy against the gain of voice A (left output): the late field holds both voices in about equal parts
    late = np.array([db(wet[t + int(d) + 4840:t + int(d) + 12040, 0], shifted[t + int(d) + 4840:t + int(d) + 12040, 0])
                     for t, d in zip(grid, rows[0, :, 12])])
    early = 20 * np.log10(np.clip(rows[0, :, 5], 1e-4, None))
    return {"octaveEdgesHz": list(OCTAVES), "bandDecay": decay,
            "lateEnergyReMacro0Db": {"mean": float(late.mean()), "sd": float(late.std()), "min": float(late.min()), "max": float(late.max())},
            "voiceAGainRangeDb": [float(early.min()), float(early.max())],
            "lateEnergyWhenVoiceAClosedDb": float(late[rows[0, :, 5] < 0.05].mean()),
            "lateEnergyWhenVoiceAFullDb": float(late[rows[0, :, 5] > 1.3].mean())}


def levels(grid):
    """Wet energy of the impulse-train capture against Macro, total and per octave, relative to Macro 0."""
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    signals = capture_all([(stimulus, 0.0, 0)] + [(stimulus, m, r) for m, r in LONG_MACROS])
    edges = (0,) + OCTAVES + (24000,)
    size = 1 << 16
    frequencies = np.fft.rfftfreq(size, 1.0 / FS)
    window = np.hanning(size)[:, None]

    def bands(signal):
        total = np.zeros((len(edges) - 1, 2))
        for start in range(FS, len(signal) - size, size):
            power = np.abs(np.fft.rfft(signal[start:start + size] * window, axis=0)) ** 2
            total += np.stack([power[(frequencies >= lo) & (frequencies < hi)].sum(0) for lo, hi in zip(edges[:-1], edges[1:])])
        return total

    reference = bands(signals[0])
    out = []
    for (macro, realisation), signal in zip(LONG_MACROS, signals[1:]):
        energy = bands(signal)
        out.append({"macro": macro, "realisation": realisation,
                    "totalDb": (10 * np.log10(energy.sum(0) / reference.sum(0))).round(2).tolist(),
                    "leftBandsDb": (10 * np.log10(energy[:, 0] / reference[:, 0])).round(2).tolist(),
                    "rightBandsDb": (10 * np.log10(energy[:, 1] / reference[:, 1])).round(2).tolist()})
    return {"bandEdgesHz": list(edges), "stimulus": "left impulses 0.5 s apart, 98 s, Decay 0.5 s", "captures": out}


def high_pass_scan(grid, references, fits):
    """Null of the fit against the corner of the high-pass, with the high-pass on the delayed path only."""
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    dry, first, second, later = capture_all([(stimulus, 0.0, 0)] + [(s, 0.0, 0) for s in references])
    out = {}
    for macro in (1.0, 0.5):
        target = capture(stimulus, macro, 0)
        rows = fits[(macro, 0)][0]
        picked = np.where((rows[:, 3] < np.percentile(rows[:, 3], 40)) & (np.hypot(rows[:, 4], rows[:, 5]) > 0.5))[0][:20]
        scan = {}
        for corner in (0.5, 20.0, 28.0, 35.0, 42.0, 50.0):
            nulls = []
            for k in picked:
                time, delay = grid[k], rows[k, 12]
                begin, end = voice_a_window(first[time:time + 4500, 0], delay, macro)
                cut = lambda x: x[time + begin:time + end, 0]
                nulls.append(fit_voice(cut(target), None if macro >= 1.0 else cut(dry), [cut(first), cut(second), cut(later)],
                                       comb_high_pass=corner)["null"])
            scan[f"{corner:g}"] = float(np.median(nulls))
        out[f"{macro:g}"] = scan
    return out


def filter_forms(grid, references, fits):
    """Null of other two-pole forms on the same impulses (Macro 100 %, first exits only)."""
    stimulus = impulse_train(grid, np.zeros(len(grid), int), datasets.GRID_SECONDS)
    target, first = capture_all([(stimulus, 1.0, 0), (references[0], 0.0, 0)])
    rows = fits[(1.0, 0)][0]
    picked = np.where((rows[:, 3] < -30.0) & (rows[:, 5] > 0.5))[0][::3]
    hp = high_pass(COMB_HIGH_PASS_HZ)

    def all_pole(fc, q):
        w0 = 2.0 * np.pi * fc / FS
        radius, angle = np.exp(-w0 / (2.0 * q)), w0 * np.sqrt(max(1.0 - 1.0 / (4.0 * q * q), 1e-9))
        a1, a2 = -2.0 * radius * np.cos(angle), radius * radius
        return (1.0 + a1 + a2) / (1.0 + a1 * Z1 + a2 * Z1 ** 2)

    def analogue(fc, q):
        s = 1j * OMEGA * FS / (2.0 * np.pi * fc)
        return 1.0 / (s * s + s / q + 1.0)

    nulls = {"bilinear": [], "all-pole": [], "unwarped analogue": []}
    for k in picked:
        time, delay = grid[k], rows[k, 12]
        begin = first_onset(first[time:time + 4500, 0], 3800) - 50
        end = begin + int(delay) - 80           # only first exits of the first arrivals: before any comb echo
        y, u = target[time + begin:time + end, 0], np.fft.rfft(first[time + begin:time + end, 0], NFFT)
        for name, form in (("bilinear", low_pass), ("all-pole", all_pole), ("unwarped analogue", analogue)):
            def error(p):
                m = np.fft.irfft(u * form(np.exp(p[0]), np.exp(p[1])) * hp * np.exp(-1j * OMEGA * p[2]), NFFT)[:len(y)]
                return m * np.dot(m, y) / np.dot(m, m) - y
            fit = least_squares(error, [np.log(rows[k, 1]), np.log(rows[k, 2]), rows[k, 9]],
                                bounds=([np.log(150.0), np.log(0.3), -6.0], [np.log(23500.0), np.log(60.0), 6.0]))
            nulls[name].append(db(fit.fun, y))
    return {"impulses": int(len(picked)), "medianNullDb": {name: float(np.median(v)) for name, v in nulls.items()},
            "window": "first arrivals up to their first comb echo"}


def operating_point(stimulus):
    """What the plug-in displays for the main listening point of the campaign: Macro 100 %, Mix 100 %."""
    shown = revocean.capture(stimulus, {**datasets.GRID_SETTINGS, "macro": 1.0}).meta["readback"]
    names = {f"id:{control.vst3_id}": control.name for control in revocean.CONTROLS}
    return {names[key]: value for key, value in sorted(shown.items(), key=lambda item: int(item[0][3:]))}


def main():
    revocean.identity()
    grid = datasets.grid_times(0)
    channels = np.zeros(len(grid), int)
    stimulus = impulse_train(grid, channels, datasets.GRID_SECONDS)
    _, references = reference_stimuli(grid, channels, datasets.GRID_SECONDS)
    capture_all([(stimulus, 0.0, 0)] + [(s, 0.0, 0) for s in references] + [(stimulus, m, r) for m, r in LONG_MACROS])
    jobs = [(grid, channels, datasets.GRID_SECONDS, macro, realisation) for macro, realisation in LONG_MACROS]
    with ProcessPoolExecutor(len(jobs)) as pool:
        fits = dict(zip(LONG_MACROS, pool.map(fit_capture, jobs)))
    laws = voice_laws(fits)
    channel_summary, alternating = input_channels()
    trajectories = {f"macro {m:g} realisation {r}": {
        "columns": ["seconds", "cutoff L", "Q L", "gain L", "null L", "cutoff R", "Q R", "gain R", "null R"],
        "rows": np.column_stack([fits[(m, r)][0, :, 0]] + [column for out in (0, 1) for column in (
            fits[(m, r)][out, :, 1], fits[(m, r)][out, :, 2], np.hypot(fits[(m, r)][out, :, 4], fits[(m, r)][out, :, 5]),
            fits[(m, r)][out, :, 3])]).round(3).tolist()} for m, r in LONG_MACROS}
    result = {
        "reference": "Arturia Rev OCEAN 1.0.0.5848, Tide, 48 kHz, Decay 0.5 s, Mix 100 %, other controls at the campaign baseline",
        "operatingPointReadBack": operating_point(stimulus),
        "model": {"delayMidSamples": DELAY_MID, "delaySwingSamples": DELAY_SWING, "delayPeriodSeconds": DELAY_PERIOD,
                  "rightInputLeadPeriods": RIGHT_INPUT_LEAD, "combGain": COMB_GAIN, "combHighPassHz": COMB_HIGH_PASS_HZ,
                  "gainMax": GAIN_MAX, "voiceAEndRawSamples": VOICE_A_END},
        "fitsPerCapture": {f"macro {m:g} realisation {r}": {"impulsesBelowMinus20Db": int(np.sum(fits[(m, r)][:, :, 3] < -20.0)),
                                                            "impulses": int(fits[(m, r)][:, :, 3].size),
                                                            "medianNullDb": float(np.median(fits[(m, r)][:, :, 3])),
                                                            "medianNullOpenVoiceDb": float(np.median(fits[(m, r)][:, :, 3][
                                                                np.hypot(fits[(m, r)][:, :, 4], fits[(m, r)][:, :, 5]) > 0.3]))}
                           for m, r in LONG_MACROS},
        "delay": measure_delay(grid, fits),
        "delayOver170Seconds": measure_delay_long(),
        "voiceLaws": laws,
        "roundValues": round_value_tests(fits, laws),
        "phase": phase_statistics(fits, laws),
        "voiceB": {"leftOutputLeftInput": voice_b(grid, channels, datasets.GRID_SECONDS, [fits[(1.0, r)] for r in range(4)], 0, laws["1"]),
                   "rightOutputRightInput": voice_b(*alternating[:3], [alternating[3]], 1, laws["1"])},
        "partition": partition(grid, fits),
        "inputChannels": channel_summary,
        "compactLaws": compact_laws(fits, laws),
        "dryGainRange": dry_gain_range(),
        "gainMaximumCheck": gain_maximum(),
        "undelayedAtMacro100": undelayed_at_full(grid),
        "structure": structure(grid, fits),
        "levels": levels(grid),
        "highPassScan": high_pass_scan(grid, references, fits),
        "filterForms": filter_forms(grid, references, fits),
        "trajectories": trajectories,
    }
    OUTPUT.parent.mkdir(exist_ok=True)
    OUTPUT.write_text(json.dumps(result, indent=1))
    print(json.dumps({key: result[key] for key in ("fitsPerCapture", "delay", "roundValues", "compactLaws", "gainMaximumCheck")}, indent=1))
    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
