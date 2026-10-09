#!/usr/bin/env python3
"""Voice-phase trajectories of Rev OCEAN in Tide mode (packet tide_phase).

Above Macro 0 every output channel has a phase that drives its two voices
(findings/tide.md). This script measures that phase continuously in many
instances and writes tide_structural_data/tide_phase.npz, the data behind
findings/tide_phase.md and the comparison in tide_phase.py.

Method. All instances of a set receive the same noise. In a short-time
transform every bin of every instance is then

    Y_i = G(phase_i) E + G(phase_i + 1/2) L

with E and L (what the network sends into voice A and voice B) common to all
instances and G the voice response of findings/tide_verification.md. Solving
for E, L and the phases frame by frame removes the randomness of the noise:
the phases are relations between captures, not spectral estimates. The phase
axis is then calibrated on the first seconds, where every instance runs at one
fixed rate. The resulting curves are described by one knot per cell of a fixed
grid with raised cosines between the knots (`fit_grid`).

Run:  python measure_tide_phase.py
About 2 minutes once the captures are cached; a second run writes the same
file. Rendering the 368 captures takes another 10 minutes and 2.5 GB of cache.
"""
from __future__ import annotations

import json
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

import revocean

OUTPUT = Path(__file__).resolve().parent / "tide_structural_data" / "tide_phase.npz"

# ---- voice law (tide.md, corrected by tide_verification.md)
F_HIGH, F_LOW_FULL, BEND = 19277.0, 460.3, 2.939      # cut-off curve at Macro 100 %, Hz
REST_FC, REST_Q = 19304.0, 2.154                      # the filter at rest; its poles are the zeros of the moving one
VOICE_FILTER_RATE = 44100.0                           # the biquad is not designed at the host rate (see `filter_rate_test`)
NOMINAL_START = (0.40, 0.22)                          # centre of the start phases, left and right (tide_verification.md)
NOMINAL_RATE = 0.0566                                 # cycles/s; only used to start the tracker

# ---- stimuli and analysis frames
NOISE_RMS, NOISE_PEAK, NOISE_SEED = 0.1, 0.5, 1
NFFT = 4096                                           # at 44.1 and 48 kHz; doubled at 96 kHz
BAND = (300.0, 18000.0)
PHASE_GRID = 4000                                     # table points per cycle

# ---- the grid model
GRID_HZ = (0.136, 0.160)                              # knot grid of the left and right output
WORKERS = 10


# ------------------------------------------------------------------ captures

def noise(seconds: float, sample_rate: int = 48000) -> np.ndarray:
    """Independent Gaussian noise on both inputs, rms 0.1, clipped at the campaign ceiling of 0.5."""
    frames = int(round(seconds * sample_rate))
    values = NOISE_RMS * np.random.default_rng(NOISE_SEED).standard_normal((frames, 2))
    return np.clip(values, -NOISE_PEAK, NOISE_PEAK).astype(np.float32)


def render_set(seconds, count, macro=1.0, warmup=0.0, sample_rate=48000, block_size=512, decay=0.5, size=100.0, first=0):
    """Outputs of `count` instances that receive the same noise."""
    stimulus = noise(seconds, sample_rate)
    settings = {"macro": macro, "decay": revocean.normalised("decay", decay), "size": revocean.normalised("size", size)}
    def one(realisation):
        return revocean.capture(stimulus, settings, sample_rate=sample_rate, block_size=block_size, warmup=warmup,
                                realisation=realisation).output
    with ThreadPoolExecutor(3) as pool:
        return list(pool.map(one, range(first, first + count)))


# ------------------------------------------------------------------ voice response

def low_cutoff(macro):
    return F_HIGH - (F_HIGH - F_LOW_FULL) * (1 - np.exp(-2.55 * macro)) / (1 - np.exp(-2.55))


def plateau_q(macro):
    return 1.411 + 6.015 * (np.exp(-3.14 * macro) - np.exp(-3.14)) / (1 - np.exp(-3.14))


def cutoff(phase, macro):
    low = low_cutoff(macro)
    return low + (F_HIGH - low) * (np.exp(-BEND * phase) - np.exp(-BEND)) / (1 - np.exp(-BEND))


def resonance(fc, macro):
    q0 = plateau_q(macro)
    return np.interp(fc, [460.0, 500.0, 1000.0, 3000.0, 5000.0, 12000.0, REST_FC], [5.06, 5.2, 7.0, 1.5 * q0, q0, q0, REST_Q])


def poles(fc, q, rate):
    w0 = 2 * np.pi * np.asarray(fc) / rate
    alpha = np.sin(w0) / (2 * np.asarray(q))
    return -2 * np.cos(w0) / (1 + alpha), (1 - alpha) / (1 + alpha)


def voice_response(phase, frequencies, macro=1.0, filter_rate=VOICE_FILTER_RATE):
    """Complex response G[phase, frequency] of one voice: gain window times the moving biquad."""
    phase = np.asarray(phase) % 1.0
    fc = cutoff(phase, macro)
    a1, a2 = poles(fc, resonance(fc, macro), filter_rate)
    n1, n2 = poles(REST_FC, REST_Q, filter_rate)
    z1 = np.exp(-2j * np.pi * np.asarray(frequencies) / filter_rate)[None, :]
    numerator = (1 + n1 * z1 + n2 * z1 ** 2) / (1 + n1 + n2)
    denominator = (1 + a1[:, None] * z1 + a2[:, None] * z1 ** 2) / (1 + a1 + a2)[:, None]
    depth = min(1.0, 17.6 * macro)
    gain = (1 - depth) + depth * np.sqrt(2.0) * np.sin(np.pi * phase) ** 2
    return gain[:, None] * numerator / denominator


# ------------------------------------------------------------------ rough phases from band levels

class Envelope:
    """Phase of single frames from the band levels of one instance; starts the tracker.

    Band power = |G(phase)|^2 S_A + |G(phase + 1/2)|^2 S_B with two source spectra learned from the set.
    The half-cycle ambiguity is resolved by the nominal line, which every instance stays within 0.27 cycle of.
    """

    def __init__(self, sample_rate, nfft, macro, bands=40):
        frequencies = np.fft.rfftfreq(nfft, 1 / sample_rate)
        edges = np.geomspace(350.0, 17500.0, bands + 1)
        self.bins = np.flatnonzero((frequencies >= edges[0]) & (frequencies < edges[-1]))
        band_of = np.searchsorted(edges, frequencies[self.bins], side="right") - 1
        self.members = [np.flatnonzero(band_of == b) for b in range(bands) if np.any(band_of == b)]
        power = np.abs(voice_response(np.arange(PHASE_GRID) / PHASE_GRID, frequencies[self.bins], macro)) ** 2
        self.table = np.stack([power[:, m].mean(1) for m in self.members], 1)
        self.count = np.array([len(m) for m in self.members], float)

    def band_power(self, spectra):
        """spectra [frames, bins on self.bins] -> [frames, bands]."""
        power = np.abs(spectra) ** 2
        return np.stack([power[:, m].mean(1) for m in self.members], 1)

    def phases(self, power, line, source_a, source_b, half_width=0.27):
        """Whittle-likelihood phase per frame inside line +- half_width."""
        model = self.table * source_a + np.roll(self.table, -PHASE_GRID // 2, axis=0) * source_b
        offsets = np.arange(-int(half_width * PHASE_GRID), int(half_width * PHASE_GRID) + 1)
        centre = np.round(np.asarray(line) * PHASE_GRID).astype(int)
        index = (centre[:, None] + offsets[None, :]) % PHASE_GRID
        cost = ((np.log(model)[index] + power[:, None, :] / model[index]) * self.count).sum(2)
        return (centre + offsets[np.argmin(cost, axis=1)]) / PHASE_GRID

    def sources(self, power, phase, source_a, source_b, rounds=30):
        """Itakura-Saito updates of the two source spectra with the phases held."""
        index = np.round(phase * PHASE_GRID).astype(int) % PHASE_GRID
        a, b = self.table[index], self.table[(index + PHASE_GRID // 2) % PHASE_GRID]
        for _ in range(rounds):
            model = a * source_a + b * source_b
            source_a = source_a * (a * power / model ** 2).sum(0) / (a / model).sum(0)
            model = a * source_a + b * source_b
            source_b = source_b * (b * power / model ** 2).sum(0) / (b / model).sum(0)
        return source_a, source_b

    def rough(self, power, line):
        """power [instances, frames, bands], line [frames] -> phases [instances, frames]."""
        source_a = source_b = power.mean((0, 1)) / (self.table.mean(0) * 2)
        for _ in range(4):
            phase = np.stack([self.phases(p, line, source_a, source_b) for p in power])
            source_a, source_b = self.sources(power.reshape(-1, power.shape[-1]), phase.reshape(-1), source_a, source_b)
        return phase


# ------------------------------------------------------------------ joint tracker

class Tracker:
    """Phases of all instances of one frame by Gauss-Newton, with E and L solved in closed form."""

    def __init__(self, sample_rate, nfft, macro=1.0, filter_rate=VOICE_FILTER_RATE):
        frequencies = np.fft.rfftfreq(nfft, 1 / sample_rate)
        self.bins = np.flatnonzero((frequencies >= BAND[0]) & (frequencies < BAND[1]))[::2]
        table = voice_response(np.arange(PHASE_GRID) / PHASE_GRID, frequencies[self.bins], macro, filter_rate)
        self.table = table.astype(np.complex64)
        self.slope = ((np.roll(table, -1, 0) - np.roll(table, 1, 0)) * (PHASE_GRID / 2.0)).astype(np.complex64)

    def lookup(self, phase):
        """G and dG/dphase at fractional phases [N] -> two arrays [N, bins]."""
        position = (np.asarray(phase) % 1.0) * PHASE_GRID
        low = np.floor(position).astype(int) % PHASE_GRID
        fraction = (position - np.floor(position))[:, None].astype(np.float32)
        high = (low + 1) % PHASE_GRID
        return (self.table[low] * (1 - fraction) + self.table[high] * fraction,
                self.slope[low] * (1 - fraction) + self.slope[high] * fraction)

    @staticmethod
    def sources(a, b, spectra):
        """Least-squares E and L per bin for voice responses a, b [N, bins]."""
        aa, bb, ab = (np.abs(a) ** 2).sum(0), (np.abs(b) ** 2).sum(0), (a.conj() * b).sum(0)
        ya, yb = (a.conj() * spectra).sum(0), (b.conj() * spectra).sum(0)
        determinant = np.maximum(aa * bb - np.abs(ab) ** 2, 1e-20 * aa * bb + 1e-30)
        return (ya * bb - yb * ab) / determinant, (yb * aa - ya * ab.conj()) / determinant

    def frame(self, spectra, phase, rounds=20, tolerance=2e-5):
        """spectra [N, bins], start phases [N] -> (phases, relative residual per instance)."""
        weights = 1.0 / np.maximum((np.abs(spectra) ** 2).mean(0), 1e-30)
        phase = np.array(phase, float)
        for _ in range(rounds):
            a, da = self.lookup(phase)
            b, db = self.lookup(phase + 0.5)
            early, late = self.sources(a, b, spectra)
            error = spectra - a * early - b * late
            gradient = da * early + db * late
            step = ((gradient.conj() * error).real * weights).sum(1) / np.maximum(((np.abs(gradient) ** 2) * weights).sum(1), 1e-30)
            phase += np.clip(step, -0.01, 0.01)
            if np.max(np.abs(step)) < tolerance:
                break
        a, _ = self.lookup(phase)
        b, _ = self.lookup(phase + 0.5)
        early, late = self.sources(a, b, spectra)
        error = spectra - a * early - b * late
        return phase, ((np.abs(error) ** 2) * weights).sum(1) / ((np.abs(spectra) ** 2) * weights).sum(1)

    def frame_two_phases(self, spectra, phase_a, phase_b, rounds=30, tolerance=2e-5):
        """As `frame`, with an independent phase for voice B; tests voice B = voice A + 1/2."""
        weights = 1.0 / np.maximum((np.abs(spectra) ** 2).mean(0), 1e-30)
        pa, pb = np.array(phase_a, float), np.array(phase_b, float)
        for _ in range(rounds):
            a, da = self.lookup(pa)
            b, db = self.lookup(pb)
            early, late = self.sources(a, b, spectra)
            error = spectra - a * early - b * late
            ga, gb = da * early, db * late
            haa, hbb = ((np.abs(ga) ** 2) * weights).sum(1), ((np.abs(gb) ** 2) * weights).sum(1)
            hab = ((ga.conj() * gb).real * weights).sum(1)
            ra, rb = ((ga.conj() * error).real * weights).sum(1), ((gb.conj() * error).real * weights).sum(1)
            determinant = np.maximum(haa * hbb - hab ** 2, 1e-30)
            step_a = np.clip((ra * hbb - rb * hab) / determinant, -0.01, 0.01)
            step_b = np.clip((rb * haa - ra * hab) / determinant, -0.01, 0.01)
            pa += step_a
            pb += step_b
            if max(np.max(np.abs(step_a)), np.max(np.abs(step_b))) < tolerance:
                break
        return pa, pb


def frame_spectra(outputs, channel, frame, hop, nfft, bins):
    window = np.hanning(nfft + 1)[:-1].astype(np.float32)
    segment = np.stack([o[frame * hop:frame * hop + nfft, channel] for o in outputs]) * window
    return np.fft.rfft(segment, axis=1)[:, bins].astype(np.complex64)


def track(outputs, sample_rate=48000, macro=1.0, start_time=0.0, anchor=(1.0, 4.0), hop=None, filter_rate=VOICE_FILTER_RATE):
    """Raw phases [channel, instance, frame], residuals and frame times (instance time) of a set.

    `start_time` is the instance time of the first output sample (the warm-up). The tracker starts
    from the band-level estimate in the anchor stretch (seconds after the first sample) and follows
    the phases frame by frame in both directions.
    """
    nfft = NFFT * (2 if sample_rate > 50000 else 1)
    hop = hop or nfft // 2
    tracker, envelope = Tracker(sample_rate, nfft, macro, filter_rate), Envelope(sample_rate, nfft, macro)
    count = (len(outputs[0]) - nfft) // hop + 1
    t = start_time + (np.arange(count) * hop + nfft / 2) / sample_rate
    phases = np.full((2, len(outputs), count), np.nan)
    residuals = np.full_like(phases, np.nan)
    stretch = np.arange(int(anchor[0] * sample_rate / hop), int(anchor[1] * sample_rate / hop))
    middle = int(stretch[len(stretch) // 2])
    for channel in (0, 1):
        power = np.stack([envelope.band_power(frame_spectra(outputs, channel, f, hop, nfft, envelope.bins)) for f in stretch], 1)
        rough = envelope.rough(power, NOMINAL_START[channel] + NOMINAL_RATE * t[stretch])
        guess = np.median(rough - NOMINAL_RATE * t[stretch], axis=1) + NOMINAL_RATE * t[middle]
        spectra = frame_spectra(outputs, channel, middle, hop, nfft, tracker.bins)
        phases[channel, :, middle], residuals[channel, :, middle] = tracker.frame(spectra, guess, rounds=60)
        for direction, frames in ((1, range(middle + 1, count)), (-1, range(middle - 1, -1, -1))):
            previous, before = phases[channel, :, middle], None
            for frame in frames:
                spectra = frame_spectra(outputs, channel, frame, hop, nfft, tracker.bins)
                if not np.any(np.abs(spectra) > 0):
                    continue
                predicted = previous + (previous - before if before is not None else direction * NOMINAL_RATE * hop / sample_rate)
                phases[channel, :, frame], residuals[channel, :, frame] = tracker.frame(spectra, predicted)
                before, previous = previous, phases[channel, :, frame]
    return t, phases, residuals


# ------------------------------------------------------------------ calibration of the phase axis

CALIBRATION_BINS = 50                                 # per half cycle


def calibrate(t, raw, window=(0.6, 7.9), half=7):
    """Correction of the tracker's phase axis from a stretch where the true phase is linear in time.

    The raw phase advances at a local rate s(raw) that should be constant. With U' = r / s and
    U(raw + 1/2) = U(raw) + 1/2 the phase U(raw) is linear in time; r follows from closing the cycle.
    Returns (U - raw on CALIBRATION_BINS + 1 nodes over half a cycle, r).
    """
    frames = np.flatnonzero((t >= window[0]) & (t <= window[1]))[half:-half]
    offsets = np.arange(-half, half + 1)
    lever = (t[1] - t[0]) * offsets
    slopes = ((raw[..., frames[:, None] + offsets[None, :]] * lever).sum(-1) / (lever ** 2).sum()).ravel()
    which = np.floor((raw[..., frames].ravel() % 0.5) * 2 * CALIBRATION_BINS).astype(int) % CALIBRATION_BINS
    local = np.array([np.median(slopes[which == b]) for b in range(CALIBRATION_BINS)])
    rate = 0.5 / np.sum((0.5 / CALIBRATION_BINS) / local)
    nodes = np.concatenate([[0.0], np.cumsum(rate / local * (0.5 / CALIBRATION_BINS))])
    correction = nodes - np.linspace(0, 0.5, CALIBRATION_BINS + 1)
    return correction - np.mean(0.5 * (correction[:-1] + correction[1:])), float(rate)


def apply_calibration(raw, correction):
    position = (np.asarray(raw) % 0.5) * 2 * CALIBRATION_BINS
    return raw + np.interp(position, np.arange(CALIBRATION_BINS + 1), correction)


# ------------------------------------------------------------------ grid model of the level (phase - rate * t)

def ease(u):
    return 0.5 - 0.5 * np.cos(np.pi * np.clip(u, 0.0, 1.0))


def grid_curve(t, period, jitter, level):
    """Level with knot k at (k + jitter[k-1]) period: level[0] until knot 1, then eases to level[k] at knot k + 1.

    Defined up to the last knot; NaN beyond.
    """
    times = (np.arange(1, len(jitter) + 1) + jitter) * period
    passed = np.searchsorted(times, t, side="right")
    out = np.full(len(t), level[0], float)
    moving = (passed >= 1) & (passed < len(jitter))
    k = passed[moving]
    out[moving] = level[k - 1] + (level[k] - level[k - 1]) * ease((t[moving] - times[k - 1]) / (times[k] - times[k - 1]))
    out[passed >= len(jitter)] = np.nan
    return out


def ease_costs(x, smooth, max_frames):
    """costs[L][i]: squared error of an ease from (i, smooth[i]) to (i + L, smooth[i + L]) over frames i..i+L."""
    cumulative = np.concatenate([[0.0], np.cumsum(x)])
    squares = np.concatenate([[0.0], np.cumsum(x * x)])
    costs = {}
    for length in range(1, min(max_frames, len(x) - 1) + 1):
        shape = ease(np.arange(length + 1) / length)
        starts = np.arange(0, len(x) - length)
        crossed = np.correlate(x, shape, "valid")
        total = cumulative[starts + length + 1] - cumulative[starts]
        energy = squares[starts + length + 1] - squares[starts]
        a, d = smooth[starts], smooth[starts + length] - smooth[starts]
        costs[length] = energy - 2 * a * total + (length + 1) * a * a - 2 * d * (crossed - a * shape.sum()) + d * d * (shape ** 2).sum()
    return costs


def grid_search(t, x, period):
    """Best knot frame per cell (cells 1..n that end inside the record) by dynamic programming; (frames, cost)."""
    smooth = np.convolve(np.pad(x, 1, mode="edge"), np.ones(3) / 3, "valid")
    cells = int(np.floor(t[-1] / period)) - 1
    members = [np.flatnonzero((t >= k * period) & (t < (k + 1) * period)) for k in range(1, cells + 1)]
    costs = ease_costs(x, smooth, int(2 * period / (t[1] - t[0])) + 2)
    cumulative = np.concatenate([[0.0], np.cumsum(x)])
    squares = np.concatenate([[0.0], np.cumsum(x * x)])
    first = members[0]
    best = [squares[first] - 2 * smooth[first] * cumulative[first] + first * smooth[first] ** 2]     # the hold before knot 1
    back = []
    for k in range(1, cells):
        previous, current = members[k - 1], members[k]
        table = np.full((len(previous), len(current)), np.inf)
        for column, j in enumerate(current):
            lengths = j - previous
            valid = lengths >= 1
            table[valid, column] = [costs[length][i] for length, i in zip(lengths[valid], previous[valid])]
        total = best[-1][:, None] + table
        back.append(np.argmin(total, axis=0))
        best.append(np.min(total, axis=0))
    position = int(np.argmin(best[-1]))
    frames = [members[-1][position]]
    for k in range(cells - 1, 0, -1):
        position = back[k - 1][position]
        frames.append(members[k - 1][position])
    return np.array(frames[::-1]), float(np.min(best[-1]))


def fit_grid(t, x, period):
    """Least-squares jitters and levels of one curve: (jitter, level, rms, cells).

    Knots 1..cells lie in cells that end inside the record. Two more knots are fitted beyond them so
    that the tail is described; they are not data.
    """
    frames, _ = grid_search(t, x, period)
    cells = len(frames)
    jitter = np.concatenate([np.clip(t[frames] / period - np.arange(1, cells + 1), 0.0, 1.0 - 1e-6), [0.5, 0.5]])
    level = np.concatenate([[np.mean(x[:frames[0]])], x[frames[1:]], [x[-1], x[-1]]])
    m = len(jitter)
    def residual(p):
        value = grid_curve(t, period, p[:m], p[m:])
        return np.where(np.isnan(value), 0.0, value - x)
    bounds = (np.concatenate([np.zeros(m), np.full(m, -1.0)]), np.concatenate([np.ones(m), np.full(m, 2.0)]))
    result = least_squares(residual, np.concatenate([jitter, level]), bounds=bounds,
                           x_scale=np.concatenate([np.full(m, 0.05), np.full(m, 0.01)]))
    return result.x[:m], result.x[m:], float(np.sqrt(np.mean(result.fun ** 2))), cells


def _search_cost(job):
    t, curves, period = job
    return sum(grid_search(t, x, period)[1] for x in curves)


def _fit_job(job):
    return fit_grid(*job)


def scan(pool, t, phase, rates, periods):
    """Cost of the grid description of one channel's curves [instances, frames] for each (rate, period) pair."""
    return np.array(list(pool.map(_search_cost, [(t, phase - rate * t, period) for rate, period in zip(rates, periods)])))


def best_rate(pool, t, phase, channel, centre, half_width=0.0006, step=0.0001):
    """Rate that minimises the cost of the grid description (parabola through the minimum)."""
    rates = np.arange(centre - half_width, centre + half_width + step / 2, step)
    cost = scan(pool, t, phase, rates, [1 / GRID_HZ[channel]] * len(rates))
    k = int(np.clip(np.argmin(cost), 2, len(cost) - 3))
    a, b, _ = np.polyfit(rates[k - 2:k + 3], cost[k - 2:k + 3], 2)
    return float(-b / (2 * a))


def period_window(pool, t, phase, rate, channel, ratios=np.arange(0.90, 1.101, 0.01)):
    """Ratios period / nominal whose cost is within a factor 1.5 of the smallest: (low, high, cost at 0.92, at 1.00, at 1.09)."""
    cost = scan(pool, t, phase, [rate] * len(ratios), ratios / GRID_HZ[channel])
    good = ratios[cost < 1.5 * cost.min()]
    pick = lambda value: float(cost[np.argmin(np.abs(ratios - value))])
    return float(good.min()), float(good.max()), pick(0.92), pick(1.00), pick(1.09)


# ------------------------------------------------------------------ measurements

def measure_main(pool):
    """40 instances, 100 s, Macro 100 %, no warm-up: phases, calibration, rate, grid fits, voice B test."""
    outputs = render_set(100.0, 40)
    t, raw, residual = track(outputs)
    correction, hold_rate = calibrate(t, raw)
    phase = apply_calibration(raw, correction)
    used = (t > 0.8) & (t < 100.0)
    rates = [best_rate(pool, t[used], phase[channel][:, used], channel, 0.0565) for channel in (0, 1)]
    rate = float(np.mean(rates))
    jobs = [(t[used], phase[channel, i, used] - rate * t[used], 1 / GRID_HZ[channel]) for channel in (0, 1) for i in range(40)]
    fits = list(pool.map(_fit_job, jobs))
    cells = [fits[0][3], fits[40][3]]
    jitter = np.full((2, 40, max(cells)), np.nan)
    level = np.full((2, 40, max(cells)), np.nan)
    rms = np.array([f[2] for f in fits]).reshape(2, 40)
    for index, (u, a, _, n) in enumerate(fits):
        jitter[index // 40, index % 40, :n] = u[:n]
        level[index // 40, index % 40, :n] = a[:n]
    summary = {
        "residualDb": float(10 * np.log10(np.nanmedian(residual))), "rateFromHold": hold_rate,
        "rateFromGrid": rates, "rate": rate, "gridFitRmsMedian": float(np.median(rms)), "gridFitRmsMax": float(rms.max()),
        "periodBounds": [period_bounds(jitter[channel], rms[channel], 1 / GRID_HZ[channel]) for channel in (0, 1)],
        "voiceB": voice_b_test(outputs, t, raw, rate),
        "filterRate": filter_rate_test(outputs, raw, 48000),
    }
    arrays = {"main_t": t, "main_phase": phase.astype(np.float32), "main_rate": rate, "main_correction": correction,
              "main_jitter": jitter.astype(np.float32), "main_level": level.astype(np.float32), "main_fit_rms": rms.astype(np.float32)}
    return arrays, summary


def period_bounds(jitter, rms, period):
    """Interval of periods allowed by the knots that the fit left strictly inside their cells."""
    low, high = 0.0, np.inf
    for u, error in zip(jitter, rms):
        if error > 0.002:
            continue
        k = np.arange(1, len(u) + 1)
        inside = np.isfinite(u) & (u > 0.003) & (u < 0.997)
        times = (k + u)[inside] * period
        low, high = max(low, np.max(times / (k[inside] + 1))), min(high, np.min(times / k[inside]))
    return [float(low), float(high)]


def voice_b_test(outputs, t, raw, rate, stride=4):
    """Phase of voice B minus phase of voice A minus 1/2, both fitted independently."""
    tracker = Tracker(48000, NFFT)
    frames = np.arange(30, len(t) - 2, stride)
    a = np.empty((2, len(outputs), len(frames)))
    b = np.empty_like(a)
    for channel in (0, 1):
        for n, frame in enumerate(frames):
            spectra = frame_spectra(outputs, channel, frame, NFFT // 2, NFFT, tracker.bins)
            a[channel, :, n], b[channel, :, n] = tracker.frame_two_phases(spectra, raw[channel, :, frame], raw[channel, :, frame] + 0.5)
    delta = b - a - 0.5
    joint = raw[:, :, frames] % 1.0
    open_both = ((joint > 0.15) & (joint < 0.35)) | ((joint > 0.65) & (joint < 0.85))       # both gains between 0.29 and 1.12
    velocity = np.gradient(raw[:, :, frames] - rate * t[frames], t[frames], axis=-1)
    slope = np.polyfit(velocity[open_both], delta[open_both], 1)[0]
    slope_error = delta[open_both].std() / (velocity[open_both].std() * np.sqrt(open_both.sum()))
    return {"mean": float(delta.mean()), "sd": float(delta.std()), "meanBothOpen": float(delta[open_both].mean()),
            "sdBothOpen": float(delta[open_both].std()), "readings": int(open_both.sum()),
            "lagSeconds": float(-slope), "lagStandardError": float(slope_error)}


def filter_rate_test(outputs, raw, sample_rate, stride=25):
    """Median residual of the joint fit (dB) with the voice biquad designed at 44.1 kHz, 48 kHz and 96 kHz."""
    nfft = NFFT * (2 if sample_rate > 50000 else 1)
    frames = np.arange(40, raw.shape[2] - 2, stride)
    out = {}
    for rate in (44100.0, 48000.0, 96000.0):
        tracker = Tracker(sample_rate, nfft, filter_rate=rate)
        residual = [tracker.frame(frame_spectra(outputs, channel, f, nfft // 2, nfft, tracker.bins), raw[channel, :, f], rounds=60)[1]
                    for channel in (0, 1) for f in frames]
        out[f"{rate:g}"] = float(10 * np.log10(np.median(residual)))
    return out


def measure_starts(correction, rate):
    """Hold levels (phase - rate * t before the first knot) of 200 instances of 1.2 s, and of 24 each after 0, 1, 2 s of warm-up."""
    def hold(outputs, warmup):
        t, raw, _ = track(outputs, start_time=warmup, anchor=(0.4, 1.1), hop=1024)
        steady = t - warmup > 0.35
        return np.median((apply_calibration(raw, correction) - rate * t)[:, :, steady], axis=-1)
    arrays = {"start_levels": hold(render_set(1.2, 200, first=1000), 0.0)}
    for warmup in (0.0, 1.0, 2.0):
        arrays[f"start_levels_warmup_{warmup:g}"] = hold(render_set(1.2, 24, warmup=warmup, first=2000 + int(100 * warmup)), warmup)
    return arrays


def measure_small_set(pool, name, guess, calibration_window=(0.8, 6.2), **settings):
    """A set of 8 instances of 40 s: rate from the grid description, admissible grid periods, level range."""
    macro = settings.get("macro", 1.0)
    sample_rate = settings.get("sample_rate", 48000)
    outputs = render_set(40.0, 8, **settings)
    t, raw, residual = track(outputs, sample_rate, macro)
    correction, hold_rate = calibrate(t, raw, calibration_window)
    phase = apply_calibration(raw, correction)
    used = (t > 0.8) & (t < t[-1] - 0.2)
    rates = [best_rate(pool, t[used], phase[channel][:, used], channel, guess, half_width=0.0015, step=0.0003) for channel in (0, 1)]
    rate = float(np.mean(rates))
    windows = [period_window(pool, t[used], phase[channel][:, used], rate, channel) for channel in (0, 1)]
    fits = list(pool.map(_fit_job, [(t[used], phase[channel, i, used] - rate * t[used], 1 / GRID_HZ[channel])
                                    for channel in (0, 1) for i in range(8)]))
    levels = [np.concatenate([a[:n - 1] for u, a, _, n in fits[8 * channel:8 * channel + 8]]) for channel in (0, 1)]
    summary = {"residualDb": float(10 * np.log10(np.nanmedian(residual))), "rateFromHold": hold_rate, "rateFromGrid": rates, "rate": rate,
               "periodRatioWindow": [w[:2] for w in windows], "costAtRatio0.92_1.00_1.09": [w[2:] for w in windows],
               "levelRange": [[float(v.min()), float(v.max())] for v in levels], "levels": [int(len(v)) for v in levels],
               "gridFitRms": [float(np.median([f[2] for f in fits[8 * channel:8 * channel + 8]])) for channel in (0, 1)]}
    return {f"{name}_t": t, f"{name}_phase": phase.astype(np.float32), f"{name}_rate": rate}, summary


def measure_warmup(correction, rate):
    """8 instances after 30 s of silent warm-up: the level range with t counted from the start of the instance."""
    outputs = render_set(25.0, 8, warmup=30.0)
    t, raw, residual = track(outputs, start_time=30.0)
    phase = apply_calibration(raw, correction)
    level = (phase - rate * t)[:, :, t > 31.0]
    summary = {"residualDb": float(10 * np.log10(np.nanmedian(residual))),
               "levelRange": [[float(level[channel].min()), float(level[channel].max())] for channel in (0, 1)]}
    return {"warmup30_t": t, "warmup30_phase": phase.astype(np.float32)}, summary


def measure_96k():
    """8 instances of 14 s at a 96 kHz host: residual by filter design rate and the rate during the hold."""
    outputs = render_set(14.0, 8, sample_rate=96000, first=200)
    t, raw, residual = track(outputs, 96000)
    correction, hold_rate = calibrate(t, raw, (0.8, 6.2))
    return {"residualDb": float(10 * np.log10(np.nanmedian(residual))), "rateFromHold": hold_rate,
            "filterRate": filter_rate_test(outputs, raw, 96000, stride=10)}


def main() -> None:
    summary, arrays = {}, {}
    with ProcessPoolExecutor(WORKERS) as pool:
        part, summary["main"] = measure_main(pool)
        arrays.update(part)
        rate, correction = part["main_rate"], part["main_correction"]
        print("main set:", json.dumps(summary["main"]), flush=True)
        arrays.update(measure_starts(correction, rate))
        for name, guess, settings in (("macro75", 0.0545, {"macro": 0.75}), ("macro50", 0.0530, {"macro": 0.5}),
                                      ("macro25", 0.0516, {"macro": 0.25}), ("rate44", 0.0565, {"sample_rate": 44100}),
                                      ("size60_decay2_block128", 0.0565, {"size": 60.0, "decay": 2.0, "block_size": 128})):
            part, summary[name] = measure_small_set(pool, name, guess, **settings)
            arrays.update(part)
            print(name + ":", json.dumps(summary[name]), flush=True)
    part, summary["warmup30"] = measure_warmup(correction, rate)
    arrays.update(part)
    summary["host96k"] = measure_96k()
    print("warm-up 30 s:", json.dumps(summary["warmup30"]), "\n96 kHz:", json.dumps(summary["host96k"]), flush=True)
    summary["identity"] = revocean.identity()
    OUTPUT.parent.mkdir(exist_ok=True)
    np.savez_compressed(OUTPUT, summary=json.dumps(summary, indent=1), **arrays)
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
