#!/usr/bin/env python3
"""Objective descriptors of reverberator responses, defined on ensembles.

Where a sample-level null is impossible (the late response, and every setting
with Macro above 0, where two instances of the reference differ) a model is
judged on statistics. This module defines those statistics, and it defines them
on an ensemble: the reference is time-varying, so one response is one draw.
Every descriptor is computed per response, then summarised as mean and standard
deviation over the responses to impulses at several times and, above Macro 0,
over several realisations. The standard deviation is the reference's own
spread; `compare_to_targets` expresses a candidate's deviation in units of it.

Per-response descriptors (a response is a float array (frames, 2): the stereo
output from the impulse on, latency removed, for a unit impulse):

* `decay_times`       EDT, T20, T30 and times to fixed decay levels per
                      third-octave band, 63 Hz to 16 kHz, by backward
                      integration with truncation and tail compensation.
* `band_levels`       energy gain per band and output channel.
* `long_term_spectrum` energy gain on a fractional-octave grid.
* `echo_density`      normalised echo density of Abel and Huang;
  `echo_density_build_up` its stored profile and the time it reaches 0.9.
* `interchannel`      correlation and coherence of the two outputs per band.

Descriptors of a render of steady periodic noise:

* `level_fluctuation` standard deviation of the band levels over time.
* `modulation_rates`  that fluctuation split by modulation frequency.
* `time_variance`     part of the band energy that is not a fixed filter of
                      the input.

Ensemble tools: `response_descriptors`, `summarise`, `compare`, and the
stimulus helpers `build_stimulus`, `case_descriptors`, `compare_to_targets`
that tie a candidate's renders to `tide_structural_data/targets.json`.

Only NumPy and SciPy are used; nothing here touches the reference. Tests:

    python -m unittest test_descriptors
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from scipy import signal
from scipy.special import erfc

NOMINAL_CENTRES = (63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                   2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000)
BAND_EDGE_RATIO = 10.0 ** 0.05                      # half a base-ten third octave
GAUSSIAN_OUTLIER_FRACTION = float(erfc(1.0 / math.sqrt(2.0)))   # 0.3173: |x| > sigma for a Gaussian
DECAY_LEVELS = (-0.1, -1.0, -3.0, -5.0, -10.0, -15.0, -20.0, -25.0, -30.0, -35.0, -40.0, -50.0, -60.0)
ENERGY_BLOCK_SECONDS = 0.001                        # resolution of every decay curve
SPECTRUM_FRACTION = 6                               # the stored long-term spectrum is in sixth octaves
SPECTRUM_LOW = 28.0                                 # from this frequency up to 20 kHz
ECHO_DENSITY_SECONDS = 0.8                          # span of the stored echo density profile
ECHO_DENSITY_STEP = 0.01                            # the stored profile is averaged over this
ECHO_DENSITY_HOP = 0.001                            # the profile is evaluated this often
ECHO_DENSITY_GATE_DB = 40.0                         # windows this far below the loudest, before it, are silent
POOLED = ("edt", "t20", "t30", "edtBroadband", "t20Broadband", "t30Broadband", "levelTimes",
          "echoDensity", "echoDensityTime", "fluctuation", "fluctuationBroadband", "modulationRates", "timeVariance")
MODULATION_RATE_EDGES = (0.04, 0.08, 0.16, 0.32, 0.64, 1.28, 2.56, 5.12)     # Hz, octaves of modulation frequency
MODULATION_REGIONS = (slice(0, 7), slice(7, 16), slice(16, 25))             # 63-250 Hz, 315 Hz-2 kHz, 2.5-16 kHz
INPUT_CHANNELS = {"L": (0,), "R": (1,), "M": (0, 1)}
TINY = 1e-300


def band_centres() -> np.ndarray:
    """Exact third-octave centres 1000 * 10^(n/10) Hz, n = -12 .. 12 (nominal 63 Hz to 16 kHz)."""
    return 1000.0 * 10.0 ** (np.arange(-12, 13) / 10.0)


def band_edges(centre: float) -> tuple:
    """Lower and upper edge of a third-octave band: centre / 10^(1/20) and centre * 10^(1/20)."""
    return centre / BAND_EDGE_RATIO, centre * BAND_EDGE_RATIO


# ----------------------------------------------------------------------------------------------
# Energy decay


def band_filter(response: np.ndarray, sample_rate: float, centre: float, order: int = 3) -> np.ndarray:
    """Third-octave Butterworth band-pass (2 * order poles) run backwards in time.

    Time-reversed filtering puts the filter's ringing before each arrival rather
    than after it, so the filter does not lengthen the decay: the decay time of
    a band of width B is measured correctly down to about B * T = 4 instead of
    B * T = 16 for forward filtering (Jacobsen and Rindel). The squared magnitude
    response is the Butterworth one.
    """
    low, high = band_edges(centre)
    sections = signal.butter(order, [low, high], btype="bandpass", fs=sample_rate, output="sos")
    return signal.sosfilt(sections, np.asarray(response, dtype=np.float64)[::-1], axis=0)[::-1]


def block_energy(response: np.ndarray, sample_rate: float, block: float = ENERGY_BLOCK_SECONDS) -> np.ndarray:
    """Energy of consecutive blocks of `block` seconds, summed over the channels."""
    step = int(round(block * sample_rate))
    squared = np.square(np.asarray(response, dtype=np.float64))
    if squared.ndim == 2:
        squared = squared.sum(axis=1)
    count = len(squared) // step
    return squared[:count * step].reshape(count, step).sum(axis=1)


@dataclass(frozen=True)
class DecayLimits:
    stop: int            # blocks used by the backward integration
    noise: float         # stationary noise energy per block that is subtracted (0 without a floor)
    tail: float          # energy added for the decay beyond `stop`
    floor: bool          # True when a stationary noise floor was found inside the response


def _line(times: np.ndarray, levels: np.ndarray) -> tuple:
    slope, intercept = np.polyfit(times, levels, 1)
    return float(slope), float(intercept)


def _late_line(times: np.ndarray, levels: np.ndarray, peak: int, background: float, slope: float, intercept: float) -> tuple:
    """The decay line refitted between 25 and 5 dB above the background; the given line when that range is too short."""
    chosen = (times > (background + 25.0 - intercept) / slope) & (times < (background + 5.0 - intercept) / slope)
    chosen[:peak] = False
    if np.count_nonzero(chosen) < 3:
        return slope, intercept
    late_slope, late_intercept = _line(times[chosen], levels[chosen])
    return (late_slope, late_intercept) if late_slope < 0.0 else (slope, intercept)


def decay_limits(energy: np.ndarray, block: float = ENERGY_BLOCK_SECONDS, interval: float = 0.03) -> DecayLimits:
    """Where to stop the backward integration and what to add for the missing tail.

    After Lundeby et al. (1995). The block energies are averaged over intervals
    of `interval` seconds and expressed in dB below the largest. The mean of the
    last tenth is the first estimate of the background. A line is fitted from
    the maximum to the last interval 10 dB above the background, then refitted
    between 25 and 5 dB above the background (the late slope); it meets the
    background at the crossing. While the crossing lies inside the first nine
    tenths, the background is re-estimated from the part 10 dB of decay beyond
    the crossing (never less than the last tenth) and the late line is refitted,
    up to five times.

    * Crossing inside the first nine tenths: there is a stationary floor. The
      integration stops at the crossing, the floor's energy per block is
      subtracted from every block (Chu), and the tail beyond the crossing is
      the integral of the late exponential starting at the floor level.
    * Otherwise the response is still decaying at its end (the usual case for
      the reference, which is noiseless: the next impulse cuts the response).
      All of it is integrated, nothing is subtracted, and the tail is the
      integral of the late exponential starting at the line's level at the end.
    """
    energy = np.asarray(energy, dtype=np.float64)
    whole = DecayLimits(len(energy), 0.0, 0.0, False)
    step = max(1, int(round(interval / block)))
    count = len(energy) // step
    if count < 10 or not np.any(energy > 0.0):
        return whole
    coarse = energy[:count * step].reshape(count, step).mean(axis=1)
    times = (np.arange(count) + 0.5) * step * block
    peak = int(np.argmax(coarse))
    levels = 10.0 * np.log10(np.maximum(coarse / coarse[peak], TINY))
    tenth = max(3, count // 10)

    def background_from(start: int) -> float:
        return 10.0 * math.log10(max(float(np.mean(coarse[start:])) / coarse[peak], TINY))

    background = background_from(count - tenth)
    last = int(np.nonzero(levels >= background + 10.0)[0][-1]) + 1
    if last - peak < 3:
        return whole
    slope, intercept = _line(times[peak:last], levels[peak:last])
    if slope >= 0.0:
        return whole
    slope, intercept = _late_line(times, levels, peak, background, slope, intercept)
    for _ in range(5):
        crossing = (background - intercept) / slope
        if crossing >= times[count - tenth]:
            break
        background = background_from(min(count - tenth, int(np.searchsorted(times, crossing - 10.0 / slope))))
        slope, intercept = _late_line(times, levels, peak, background, slope, intercept)
    crossing = (background - intercept) / slope
    time_constant = -10.0 / (slope * math.log(10.0)) / block       # of the energy, in blocks
    if crossing < times[count - tenth]:
        noise = coarse[peak] * 10.0 ** (background / 10.0)
        return DecayLimits(int(min(len(energy), max(1, round(crossing / block)))), noise, noise * time_constant, True)
    end = coarse[peak] * 10.0 ** ((intercept + slope * len(energy) * block) / 10.0)
    return DecayLimits(len(energy), 0.0, end * time_constant, False)


def decay_curve(energy: np.ndarray, block: float = ENERGY_BLOCK_SECONDS, limits: DecayLimits | None = None) -> np.ndarray:
    """Energy decay curve in dB: 10 log10 of the energy still to come at each block boundary.

    Schroeder's backward integration of the block energies up to `limits.stop`,
    with the noise energy subtracted and the tail energy added (`decay_limits`),
    normalised to its value at time zero. Element k belongs to time k * block.
    """
    limits = decay_limits(energy, block) if limits is None else limits
    used = np.asarray(energy[:limits.stop], dtype=np.float64) - limits.noise
    remaining = np.concatenate([np.cumsum(used[::-1])[::-1], [0.0]]) + limits.tail
    remaining = np.maximum(remaining, TINY)
    return 10.0 * np.log10(remaining / remaining[0])


def decay_valid_to(curve: np.ndarray) -> float:
    """Lowest level in dB at which the curve is used: 5 dB above its last value.

    The last value of the curve holds only the extrapolated tail, so the curve
    is as uncertain there as the extrapolation; 5 dB higher the tail is a third
    of the remaining energy.
    """
    return float(curve[-1]) + 5.0


def level_time(curve: np.ndarray, level: float, block: float = ENERGY_BLOCK_SECONDS) -> float:
    """Time at which the decay curve first reaches `level` dB (linear interpolation), NaN if it never does."""
    below = np.nonzero(curve <= level)[0]
    if len(below) == 0:
        return math.nan
    index = int(below[0])
    if index == 0:
        return 0.0
    upper, lower = curve[index - 1], curve[index]
    return (index - 1 + (upper - level) / (upper - lower)) * block


def decay_time(curve: np.ndarray, upper: float, lower: float, block: float = ENERGY_BLOCK_SECONDS) -> float:
    """Time for 60 dB of decay from the least-squares line through the curve between two levels.

    The line is fitted to the samples from the first at or below `upper` dB to
    the last before the curve first passes `lower` dB. EDT is (-0.1, -10), T20
    is (-5, -25) and T30 is (-5, -35). EDT starts at -0.1 dB instead of 0 dB so
    that the silent gap before the first arrival of a wet-only reverberator is
    not part of the fit. NaN when the curve does not reach `lower`.
    """
    start = np.nonzero(curve <= upper)[0]
    stop = np.nonzero(curve < lower)[0]
    if len(start) == 0 or len(stop) == 0 or stop[0] - start[0] < 3:
        return math.nan
    indices = np.arange(start[0], stop[0])
    slope = np.polyfit(indices * block, curve[indices], 1)[0]
    return float(-60.0 / slope) if slope < 0.0 else math.nan


def _decay_summary(energy: np.ndarray, block: float) -> tuple:
    limits = decay_limits(energy, block)
    curve = decay_curve(energy, block, limits)
    valid = decay_valid_to(curve)
    times = [decay_time(curve, upper, lower, block) if lower >= valid else math.nan
             for upper, lower in ((-0.1, -10.0), (-5.0, -25.0), (-5.0, -35.0))]
    return times, [level_time(curve, level, block) if level >= valid else math.nan for level in DECAY_LEVELS]


def decay_times(response: np.ndarray, sample_rate: float, centres: np.ndarray | None = None) -> dict:
    """EDT, T20 and T30 in seconds per third-octave band and broadband, and the broadband decay profile.

    Each band is filtered backwards in time (`band_filter`); the energies of
    both output channels are added, integrated backwards (`decay_curve`) and
    fitted (`decay_time`). `levelTimes` holds, for the unfiltered response, the
    times at which the decay curve reaches each of DECAY_LEVELS; the first of
    them (-0.1 dB, 2.3 % of the energy has arrived) is a robust onset time.
    A value is NaN when the curve does not reach the level it needs.
    """
    centres = band_centres() if centres is None else centres
    response = np.asarray(response, dtype=np.float64)
    bands = [_decay_summary(block_energy(band_filter(response, sample_rate, centre), sample_rate), ENERGY_BLOCK_SECONDS)[0]
             for centre in centres]
    broadband, profile = _decay_summary(block_energy(response, sample_rate), ENERGY_BLOCK_SECONDS)
    bands = np.array(bands)
    return {"edt": bands[:, 0], "t20": bands[:, 1], "t30": bands[:, 2],
            "edtBroadband": np.array([broadband[0]]), "t20Broadband": np.array([broadband[1]]),
            "t30Broadband": np.array([broadband[2]]), "levelTimes": np.array(profile)}


# ----------------------------------------------------------------------------------------------
# Spectrum


def _energy_spectrum(response: np.ndarray, sample_rate: float, minimum_bins: int = 1 << 17) -> tuple:
    """(frequencies, |FFT|^2 per channel); zero-padded so that the lowest bands hold several bins."""
    response = np.asarray(response, dtype=np.float64)
    if response.ndim == 1:
        response = response[:, None]
    size = max(minimum_bins, 1 << int(math.ceil(math.log2(len(response)))))
    spectrum = np.fft.rfft(response, size, axis=0)
    return np.fft.rfftfreq(size, 1.0 / sample_rate), spectrum


def _band_slices(frequencies: np.ndarray, centres: np.ndarray) -> list:
    return [slice(int(np.searchsorted(frequencies, band_edges(centre)[0])),
                  int(np.searchsorted(frequencies, band_edges(centre)[1]))) for centre in centres]


def band_levels(response: np.ndarray, sample_rate: float, centres: np.ndarray | None = None) -> np.ndarray:
    """Energy gain in dB per third-octave band and channel, shape (bands, channels).

    The mean of |H(f)|^2 over the bins of the band, H being the Fourier
    transform of the response to a unit impulse. An identity system gives 0 dB
    in every band; stationary noise through the system gains this many dB of
    band power on average. The value is the response's band energy divided by
    the band energy of the unit impulse.
    """
    centres = band_centres() if centres is None else centres
    frequencies, spectrum = _energy_spectrum(response, sample_rate)
    power = np.abs(spectrum) ** 2
    return np.array([10.0 * np.log10(np.maximum(power[band].mean(axis=0), TINY)) for band in _band_slices(frequencies, centres)])


def total_level(response: np.ndarray) -> np.ndarray:
    """Energy of each channel of the response in dB (0 dB is the energy of the unit impulse)."""
    energy = np.sum(np.square(np.asarray(response, dtype=np.float64)), axis=0)
    return 10.0 * np.log10(np.maximum(energy, TINY))


def long_term_spectrum(response: np.ndarray, sample_rate: float, fraction: int = 12,
                       low: float = 20.0, high: float = 20000.0) -> tuple:
    """(centre frequencies, energy gain in dB) on a 1/`fraction`-octave grid, both channels added.

    Each value is the mean of |H(f)|^2 summed over the channels across one
    fractional-octave band (base ten, anchored at 1 kHz).
    """
    frequencies, spectrum = _energy_spectrum(response, sample_rate, 1 << 18)
    power = (np.abs(spectrum) ** 2).sum(axis=1)
    steps = np.arange(math.ceil(fraction * math.log10(low / 1000.0) / 0.3),
                      math.floor(fraction * math.log10(high / 1000.0) / 0.3) + 1)
    centres = 1000.0 * 10.0 ** (0.3 * steps / fraction)
    half = 10.0 ** (0.15 / fraction)
    gains = []
    for centre in centres:
        band = slice(int(np.searchsorted(frequencies, centre / half)), int(np.searchsorted(frequencies, centre * half)))
        gains.append(10.0 * math.log10(max(float(power[band].mean()), TINY)) if band.stop > band.start else math.nan)
    return centres, np.array(gains)


# ----------------------------------------------------------------------------------------------
# Echo density


def echo_density(response: np.ndarray, sample_rate: float, window: float = 0.02, hop: float = 0.001) -> tuple:
    """Normalised echo density profile of Abel and Huang (2006): (times, profile, window energy), per channel.

    For a window of `window` seconds centred on time t with Hann weights w that
    sum to one, sigma(t) = sqrt(sum w h^2) and

        eta(t) = sum w [|h| > sigma(t)] / erfc(1 / sqrt(2)).

    eta is 1 for Gaussian noise (0.3173 of its samples lie beyond one standard
    deviation), near 0 for a few isolated echoes, and is evaluated every `hop`
    seconds. A window in which the response is entirely zero gives 0. The
    response is taken as zero before its first and after its last sample.
    The third result is sigma(t)^2. The profile ignores scale: any dense
    signal reads 1, however weak, including a noise floor.
    """
    response = np.asarray(response, dtype=np.float64)
    if response.ndim == 1:
        response = response[:, None]
    width = int(round(window * sample_rate)) | 1
    step = int(round(hop * sample_rate))
    weights = signal.windows.hann(width + 2)[1:-1]
    weights /= weights.sum()
    half = width // 2
    padded = np.pad(np.square(response), ((half, half), (0, 0)))
    frames = np.lib.stride_tricks.sliding_window_view(padded, width, axis=0)[::step]     # (times, channels, width)
    profile = np.empty(frames.shape[:2])
    variance = np.empty(frames.shape[:2])
    for first in range(0, len(frames), 1024):                  # bounded memory for long responses
        part = frames[first:first + 1024]
        variance[first:first + 1024] = part @ weights
        profile[first:first + 1024] = ((part > variance[first:first + 1024, :, None]) * weights).sum(axis=-1)
    return np.arange(len(profile)) * step / sample_rate, profile / GAUSSIAN_OUTLIER_FRACTION, variance


def echo_density_time(times: np.ndarray, profile: np.ndarray, threshold: float = 0.9) -> float:
    """First time at which a one-dimensional echo density profile reaches `threshold`; NaN if it never does."""
    reached = np.nonzero(np.asarray(profile) >= threshold)[0]
    return float(times[reached[0]]) if len(reached) else math.nan


def echo_density_build_up(response: np.ndarray, sample_rate: float) -> tuple:
    """(profile, time): the stored echo density profile of a response and the time it reaches 0.9.

    `echo_density` is evaluated every millisecond. Before a channel's loudest
    window, windows more than ECHO_DENSITY_GATE_DB below it count as silent
    (0): in a render with several impulses the tail of the previous response
    is still there, 50 dB or more down but dense, and would read 1 before the
    first arrival. The channels are then averaged. The profile is the mean over
    consecutive ECHO_DENSITY_STEP seconds up to ECHO_DENSITY_SECONDS (element k
    covers k to k + 1 steps after the impulse). The time is the first at which
    a 10 ms running mean reaches 0.9, counted from the impulse, so it contains
    the silent gap before the first arrival; NaN when 0.9 is not reached within
    ECHO_DENSITY_SECONDS. A single response crosses 0.9 early or late by
    chance, which is part of its spread.
    """
    response = np.asarray(response, dtype=np.float64)
    needed = int(round((ECHO_DENSITY_SECONDS + 0.02) * sample_rate))
    if len(response) < needed:
        response = np.pad(response, ((0, needed - len(response)), (0, 0)))
    _, profile, energy = echo_density(response[:needed], sample_rate, hop=ECHO_DENSITY_HOP)
    before_loudest = np.arange(len(energy))[:, None] < np.argmax(energy, axis=0)
    profile[before_loudest & (energy < energy.max(axis=0) * 10.0 ** (-ECHO_DENSITY_GATE_DB / 10.0))] = 0.0
    profile = profile.mean(axis=1)
    step = int(round(ECHO_DENSITY_STEP / ECHO_DENSITY_HOP))
    count = int(round(ECHO_DENSITY_SECONDS / ECHO_DENSITY_STEP))
    running = np.convolve(profile[:count * step], np.full(step, 1.0 / step), mode="valid")
    times = (np.arange(len(running)) + (step - 1) / 2.0) * ECHO_DENSITY_HOP
    return profile[:count * step].reshape(count, step).mean(axis=1), echo_density_time(times, running)


# ----------------------------------------------------------------------------------------------
# Inter-channel relation


def interchannel(response: np.ndarray, sample_rate: float, centres: np.ndarray | None = None,
                 start: float = 0.0, stop: float | None = None) -> dict:
    """Correlation and coherence of the two output channels per third-octave band and broadband.

    With S_xy the sum over the band's bins of X(f) conj(Y(f)), taken over the
    part of the response between `start` and `stop` seconds,

        c = S_LR / sqrt(S_LL S_RR),   correlation = Re c,   coherence = |c|.

    The correlation is the zero-lag correlation coefficient of the two band
    signals (+1 identical, -1 inverted, 0 uncorrelated). The coherence is the
    same coefficient for the analytic band signals: it ignores a constant phase
    offset between the channels and is never smaller than |correlation|. For
    two independent channels both scatter around 0 with a spread of about
    1 / sqrt(2 B T) for bandwidth B and effective duration T, so the coherence
    of short responses has a positive bias in the low bands; the ensemble
    spread of the reference carries the same bias. Broadband is 20 Hz to 20 kHz.
    """
    centres = band_centres() if centres is None else centres
    response = np.asarray(response, dtype=np.float64)
    first = int(round(start * sample_rate))
    last = len(response) if stop is None else int(round(stop * sample_rate))
    frequencies, spectrum = _energy_spectrum(response[first:last], sample_rate)
    cross = spectrum[:, 0] * np.conj(spectrum[:, 1])
    power = np.abs(spectrum) ** 2

    def coefficient(band: slice) -> complex:
        denominator = math.sqrt(float(power[band, 0].sum()) * float(power[band, 1].sum()))
        return complex(cross[band].sum() / denominator) if denominator > 0.0 else complex(math.nan, math.nan)

    bands = np.array([coefficient(band) for band in _band_slices(frequencies, centres)])
    whole = coefficient(slice(int(np.searchsorted(frequencies, 20.0)), int(np.searchsorted(frequencies, 20000.0))))
    return {"correlation": bands.real, "coherence": np.abs(bands),
            "correlationBroadband": np.array([whole.real]), "coherenceBroadband": np.array([abs(whole)])}


# ----------------------------------------------------------------------------------------------
# Modulation, measured with steady periodic noise


def periodic_noise(frames: int, period: int, seed: int, peak: float = 0.5) -> np.ndarray:
    """Stereo noise that repeats every `period` samples: flat magnitude, random phases, independent channels.

    Every window of `period` samples has exactly the same magnitude spectrum
    (every bin except 0 Hz and the Nyquist frequency carries the same energy),
    so the band levels of the input are constant in time and a time-invariant
    system returns constant band levels too. Scaled to the given peak value.
    """
    generator = np.random.default_rng(seed)
    cycle = np.empty((period, 2))
    for channel in range(2):
        spectrum = np.exp(2j * np.pi * generator.random(period // 2 + 1))
        spectrum[0] = 0.0
        spectrum[-1] = 0.0
        cycle[:, channel] = np.fft.irfft(spectrum, period)
    cycle *= peak / np.abs(cycle).max()
    return np.tile(cycle, (frames // period + 1, 1))[:frames].astype(np.float32)


def _frame_spectra(output: np.ndarray, period: int, hop: int) -> np.ndarray:
    frames = np.lib.stride_tricks.sliding_window_view(np.asarray(output, dtype=np.float64), period, axis=0)[::hop]
    return np.fft.rfft(frames, axis=-1)                         # (frames, channels, bins)


def frame_levels(output: np.ndarray, sample_rate: float, period: int, centres: np.ndarray | None = None,
                 hop: int | None = None) -> np.ndarray:
    """Band level in dB of consecutive windows of one noise period, shape (windows, bands, channels).

    The windows are rectangular and `hop` samples apart (a quarter period by
    default). The level is the energy of the window's FFT bins inside the band.
    For the steady part of a time-invariant system's answer to `periodic_noise`
    every window gives the same level, whatever the hop.
    """
    centres = band_centres() if centres is None else centres
    spectra = np.abs(_frame_spectra(output, period, period // 4 if hop is None else hop)) ** 2
    bands = _band_slices(np.fft.rfftfreq(period, 1.0 / sample_rate), centres)
    return np.stack([10.0 * np.log10(np.maximum(spectra[:, :, band].sum(axis=-1), TINY)) for band in bands], axis=1)


def level_fluctuation(output: np.ndarray, sample_rate: float, period: int, centres: np.ndarray | None = None) -> dict:
    """Fluctuation of the band levels over time for a steady noise input.

    `fluctuation` is the standard deviation over time, in dB, of each band's
    level in windows of one noise period a quarter period apart (`frame_levels`),
    averaged over the two channels. `fluctuationBroadband` is the same for the
    level of all bands together. `output` must be the steady part of the render.
    A time-invariant system gives 0 dB for `periodic_noise`; for ordinary
    Gaussian noise the value is about 4.34 / sqrt(B T) dB (band width B, window
    T) from the noise itself, which is why the targets use periodic noise.
    """
    levels = frame_levels(output, sample_rate, period, centres)
    together = 10.0 * np.log10(np.sum(10.0 ** (levels / 10.0), axis=1))
    return {"fluctuation": levels.std(axis=0).mean(axis=-1), "fluctuationBroadband": np.array([together.std(axis=0).mean()])}


def modulation_spectrum(output: np.ndarray, sample_rate: float, period: int, centres: np.ndarray | None = None) -> tuple:
    """(modulation frequencies in Hz, rms level deviation in dB per frequency bin, bands and channels averaged).

    The Hann-windowed amplitude spectrum of each band's level series, scaled so
    that a sinusoidal level modulation of a dB peak reads a / sqrt(2) at its
    frequency. Windows of one noise period smooth modulation faster than about
    half the window rate, so the spectrum is meaningful up to roughly
    sample_rate / (2 * period).
    """
    levels = frame_levels(output, sample_rate, period, centres)
    levels = levels - levels.mean(axis=0)
    window = signal.windows.hann(len(levels), sym=False)
    spectrum = np.abs(np.fft.rfft(levels * window[:, None, None], axis=0)) * math.sqrt(2.0) / window.sum()
    rate = sample_rate / (period // 4)
    return np.fft.rfftfreq(len(levels), 1.0 / rate), np.sqrt(np.mean(spectrum ** 2, axis=(1, 2)))


def modulation_rates(output: np.ndarray, sample_rate: float, period: int) -> np.ndarray:
    """The level fluctuation split by modulation rate: rms dB per audio region and octave of modulation frequency.

    The level series of `frame_levels` (all 25 bands) are Hann-windowed and
    transformed; the power of each series is summed over the bins inside each
    octave of MODULATION_RATE_EDGES and averaged over the bands of each of
    MODULATION_REGIONS and over the channels. The result is flat, region by
    region (3 x 7 values); the squares of a region's values add up to about the
    mean squared `fluctuation` of its bands. It tells a slow tide (below 0.3 Hz)
    from the 0.6 Hz delay modulation and from faster flutter. Windows of one
    noise period damp rates above about 2.5 Hz (by 3 dB at 2.6 Hz for 8192
    samples at 48 kHz), and `output` must span several periods of the slowest
    rate of interest: 18 s resolve 0.056 Hz.
    """
    levels = frame_levels(output, sample_rate, period)
    levels = levels - levels.mean(axis=0)
    window = signal.windows.hann(len(levels), sym=False)
    power = 2.0 * np.abs(np.fft.rfft(levels * window[:, None, None], axis=0)) ** 2 / (len(levels) * np.sum(window ** 2))
    rates = np.fft.rfftfreq(len(levels), (period // 4) / sample_rate)
    octaves = [(rates >= low) & (rates < high) for low, high in zip(MODULATION_RATE_EDGES[:-1], MODULATION_RATE_EDGES[1:])]
    return np.sqrt(np.array([[power[octave][:, region].sum(axis=0).mean() for octave in octaves] for region in MODULATION_REGIONS])).ravel()


def time_variance(output: np.ndarray, sample_rate: float, period: int, centres: np.ndarray | None = None) -> np.ndarray:
    """Part of each band's energy that is not a fixed filter of a periodic input, 0 to 1, channels averaged.

    With X_m(f) the spectrum of the m-th of M consecutive, non-overlapping
    windows of one period,

        v = 1 - sum_f |mean_m X_m(f)|^2 / sum_f mean_m |X_m(f)|^2.

    A time-invariant system repeats the same spectrum in every window, so v = 0.
    A system whose phase wanders by more than a cycle over the M windows gives
    v = 1 - 1/M. A delay modulated by +-d seconds leaves low bands coherent
    (f d << 1) and makes high bands incoherent.
    """
    centres = band_centres() if centres is None else centres
    spectra = _frame_spectra(output, period, period)
    coherent = np.abs(spectra.mean(axis=0)) ** 2
    total = (np.abs(spectra) ** 2).mean(axis=0)
    bands = _band_slices(np.fft.rfftfreq(period, 1.0 / sample_rate), centres)
    return np.array([np.mean(1.0 - coherent[:, band].sum(axis=-1) / np.maximum(total[:, band].sum(axis=-1), TINY)) for band in bands])


# ----------------------------------------------------------------------------------------------
# Ensembles


def response_descriptors(response: np.ndarray, sample_rate: float) -> dict:
    """Every impulse-response descriptor of one stereo response as a dict of one-dimensional arrays.

    edt, t20, t30 (25 bands), their broadband values, levelTimes (DECAY_LEVELS);
    bandLevel (25 bands, both channels added), bandLevelLeft, bandLevelRight,
    level (both channels added), levelLeft, levelRight, all in dB;
    spectrum (`long_term_spectrum` on SPECTRUM_FRACTION-th octaves, 28 Hz to 20 kHz, dB);
    correlation and coherence (25 bands) and their broadband values;
    echoDensity and echoDensityTime (`echo_density_build_up`).
    """
    response = np.asarray(response, dtype=np.float64)
    result = decay_times(response, sample_rate)
    levels = band_levels(response, sample_rate)
    totals = total_level(response)
    result.update({
        "bandLevel": 10.0 * np.log10(np.sum(10.0 ** (levels / 10.0), axis=1)),
        "bandLevelLeft": levels[:, 0], "bandLevelRight": levels[:, 1],
        "level": np.array([10.0 * math.log10(float(np.sum(10.0 ** (totals / 10.0))))]),
        "levelLeft": totals[:1], "levelRight": totals[1:],
        "spectrum": long_term_spectrum(response, sample_rate, SPECTRUM_FRACTION, SPECTRUM_LOW)[1],
    })
    result.update(interchannel(response, sample_rate))
    profile, reached = echo_density_build_up(response, sample_rate)
    result["echoDensity"] = profile
    result["echoDensityTime"] = np.array([reached])
    return result


def noise_descriptors(steady: np.ndarray, sample_rate: float, period: int) -> dict:
    """Modulation descriptors of the steady part of a render of `periodic_noise`."""
    result = level_fluctuation(steady, sample_rate, period)
    result["modulationRates"] = modulation_rates(steady, sample_rate, period)
    result["timeVariance"] = time_variance(steady, sample_rate, period)
    return result


def summarise(rows: list) -> dict:
    """Mean and standard deviation of each descriptor over a list of descriptor dicts (NaN-aware).

    Returns {name: {"mean": array, "sd": array, "n": array}}; `sd` is the sample
    standard deviation (one degree of freedom removed) and is NaN for n < 2;
    `n` counts the finite values per element.
    """
    summary = {}
    for name in rows[0]:
        values = np.stack([np.asarray(row[name], dtype=np.float64) for row in rows])
        finite = np.isfinite(values)
        count = finite.sum(axis=0)
        total = np.where(finite, values, 0.0).sum(axis=0)
        mean = np.where(count > 0, total / np.maximum(count, 1), np.nan)
        squares = np.where(finite, (values - mean) ** 2, 0.0).sum(axis=0)
        sd = np.where(count > 1, np.sqrt(squares / np.maximum(count - 1, 1)), np.nan)
        summary[name] = {"mean": mean, "sd": sd, "n": count}
    return summary


# Smallest spread a difference is divided by: the resolution below which a descriptor is not
# meaningful, so that a nearly constant descriptor does not turn a negligible difference into
# many "spreads". Decay times and level times are relative (a fraction of the target value).
SPREAD_FLOORS = {
    "edt": ("relative", 0.02), "t20": ("relative", 0.01), "t30": ("relative", 0.01),
    "edtBroadband": ("relative", 0.02), "t20Broadband": ("relative", 0.01), "t30Broadband": ("relative", 0.01),
    "levelTimes": ("absolute", 0.002), "echoDensityTime": ("absolute", 0.005), "echoDensity": ("absolute", 0.02),
    "bandLevel": ("absolute", 0.1), "bandLevelLeft": ("absolute", 0.1), "bandLevelRight": ("absolute", 0.1),
    "level": ("absolute", 0.1), "levelLeft": ("absolute", 0.1), "levelRight": ("absolute", 0.1),
    "spectrum": ("absolute", 0.1),
    "correlation": ("absolute", 0.02), "coherence": ("absolute", 0.02),
    "correlationBroadband": ("absolute", 0.02), "coherenceBroadband": ("absolute", 0.02),
    "fluctuation": ("absolute", 0.05), "fluctuationBroadband": ("absolute", 0.05), "modulationRates": ("absolute", 0.05),
    "timeVariance": ("absolute", 0.02),
}


def spread_floor(name: str, target_mean: np.ndarray) -> np.ndarray:
    kind, value = SPREAD_FLOORS[name]
    return np.abs(target_mean) * value if kind == "relative" else np.full_like(target_mean, value, dtype=np.float64)


def compare(candidate: dict, target: dict) -> dict:
    """Difference of two `summarise` results, in units of the target's own spread.

    For each descriptor present in both:

    * `difference`  candidate mean - target mean, in the descriptor's unit;
    * `inSpreads`   difference / max(target sd, floor): how many of the
                    reference's response-to-response standard deviations the
                    candidate's typical response is away. |inSpreads| < 1 means
                    the candidate lies inside what the reference itself does
                    from one impulse to the next;
    * `inErrors`    difference / sqrt(sd_t^2 / n_t + sd_c^2 / n_c + floor^2):
                    the same difference against the uncertainty of the two
                    means. Beyond about +-3 the ensembles resolve a real
                    difference, however small it is in spreads;
    * `spreadRatio` candidate sd / max(target sd, floor): a model that is
                    right on average but too steady or too restless shows here.

    The floors are SPREAD_FLOORS. Elements without a finite target are NaN.
    """
    result = {}
    for name, reference in target.items():
        if name not in candidate or name not in SPREAD_FLOORS:
            continue
        ours = candidate[name]
        reference_mean = np.asarray(reference["mean"], dtype=np.float64)
        reference_sd = np.nan_to_num(np.asarray(reference["sd"], dtype=np.float64))
        ours_sd = np.nan_to_num(np.asarray(ours["sd"], dtype=np.float64))
        floor = spread_floor(name, reference_mean)
        spread = np.maximum(reference_sd, floor)
        difference = np.asarray(ours["mean"], dtype=np.float64) - reference_mean
        error = np.sqrt(reference_sd ** 2 / np.maximum(np.asarray(reference["n"]), 1)
                        + ours_sd ** 2 / np.maximum(np.asarray(ours["n"]), 1) + floor ** 2)
        result[name] = {"difference": difference, "inSpreads": difference / spread,
                        "inErrors": difference / error, "spreadRatio": ours_sd / spread}
    return result


# ----------------------------------------------------------------------------------------------
# Stimuli and targets


def build_stimulus(specification: dict) -> np.ndarray:
    """The stereo float32 stimulus of a target case.

    `{"kind": "impulses", "frames", "amplitude", "events": [[sample, "L" | "R" | "M"], ...]}`
    is silence with one impulse per event on the left, right or both inputs.
    `{"kind": "periodicNoise", "frames", "period", "seed", "peak", "steadyFrom"}`
    is `periodic_noise`; the modulation descriptors use the output from sample
    `steadyFrom` on.
    """
    if specification["kind"] == "impulses":
        stimulus = np.zeros((specification["frames"], 2), np.float32)
        for sample, channel in specification["events"]:
            stimulus[sample, list(INPUT_CHANNELS[channel])] += specification["amplitude"]
        return stimulus
    if specification["kind"] == "periodicNoise":
        return periodic_noise(specification["frames"], specification["period"], specification["seed"], specification["peak"])
    raise ValueError(f"unknown stimulus kind {specification['kind']!r}")


def split_responses(output: np.ndarray, specification: dict, latency: int) -> list:
    """[(input channel label, unit-impulse response)] of a render of an impulse stimulus.

    A response runs from its impulse to the next impulse, or to the end of the
    stimulus for the last one, both shifted by the latency.
    """
    events = specification["events"]
    bounds = [sample + latency for sample, _ in events] + [specification["frames"] + latency]
    return [(channel, np.asarray(output[bounds[i]:bounds[i + 1]], dtype=np.float64) / specification["amplitude"])
            for i, (_, channel) in enumerate(events)]


def noise_segments(output: np.ndarray, specification: dict, latency: int) -> list:
    """The steady part of a noise render cut into the segments that form its ensemble."""
    steady = np.asarray(output[specification["steadyFrom"] + latency:specification["frames"] + latency], dtype=np.float64)
    length = specification["segmentPeriods"] * specification["period"]
    return [steady[start:start + length] for start in range(0, len(steady) - length + 1, length)]


def case_descriptors(outputs: list, specification: dict, sample_rate: float, latency: int) -> list:
    """Descriptor rows of one case: [(group label, descriptor dict)] over all renders (realisations).

    For an impulse stimulus there is one row per response and the group is the
    input channel ("L", "R" or "M"). For a noise stimulus there is one row per
    segment and the group is "N".
    """
    rows = []
    for output in outputs:
        if specification["kind"] == "impulses":
            rows += [(channel, response_descriptors(response, sample_rate))
                     for channel, response in split_responses(output, specification, latency)]
        else:
            rows += [("N", noise_descriptors(segment, sample_rate, specification["period"]))
                     for segment in noise_segments(output, specification, latency)]
    return rows


def summarise_groups(rows: list) -> dict:
    """`summarise` per group of `case_descriptors` rows, plus "all" for every row together.

    "all" holds only the POOLED descriptors (decay, echo density, modulation).
    Levels and the inter-channel relation depend on which input is driven and
    exist in the per-input groups only: pooled, their spread would be the
    difference between the inputs, not the reference's variation.
    """
    groups = {"all": summarise([{name: value for name, value in row.items() if name in POOLED} for _, row in rows])}
    for label in sorted({label for label, _ in rows}):
        if label != "N":
            groups[label] = summarise([row for group, row in rows if group == label])
    return groups


def as_list(values: np.ndarray, digits: int = 4) -> list:
    """A flat list of rounded numbers for JSON; values that are not finite become None."""
    return [None if not math.isfinite(v) else round(float(v), digits) for v in np.asarray(values, dtype=np.float64).ravel()]


def as_array(values: list) -> np.ndarray:
    """The inverse of `as_list`: None becomes NaN."""
    return np.array([math.nan if v is None else v for v in values], dtype=np.float64)


def groups_as_lists(groups: dict) -> dict:
    """`summarise_groups` output as plain lists for JSON: {group: {descriptor: {"mean", "sd", "n"}}}."""
    return {label: {name: {"mean": as_list(value["mean"]), "sd": as_list(value["sd"]), "n": [int(n) for n in value["n"]]}
                    for name, value in summary.items()} for label, summary in groups.items()}


def compare_to_targets(renders: dict, targets: dict, latency: int = 48) -> dict:
    """Score a candidate's renders of the target stimuli against the reference targets.

    `targets` is the content of tide_structural_data/targets.json. `renders`
    maps a case name of `targets["cases"]` to a list of the candidate's stereo
    outputs for `build_stimulus(case["stimulus"])` at the case's settings, one
    per realisation (a deterministic candidate passes one). Outputs are aligned
    like `revocean.capture`: output sample n + latency answers input sample n,
    48 samples for the reference; pass latency=0 for compensated renders.

    Returns {case: {group: {descriptor: `compare` result as lists}}} and, under
    "summary", per descriptor over all scored cases, groups and elements: the
    rms and the largest |inSpreads| and the rms of inErrors. inSpreads is in
    units of the reference's own spread. Another draw of the reference itself
    scores 0.3 to 0.9 rms in spreads and about 1 or less in errors (the
    "selfScores" of targets.json; findings/stats.md names the Macro 0 cases
    whose spread is too small for that to hold).
    """
    report = {"cases": {}, "summary": {}}
    collected = {}
    for name, outputs in renders.items():
        case = targets["cases"][name]
        groups = summarise_groups(case_descriptors(outputs, case["stimulus"], targets["sampleRate"], latency))
        report["cases"][name] = {}
        for label, summary in groups.items():
            if label not in case["groups"]:
                continue
            reference = {key: {field: as_array(entry[field]) for field in ("mean", "sd", "n")}
                         for key, entry in case["groups"][label].items()}
            scored = compare(summary, reference)
            report["cases"][name][label] = {key: {field: as_list(values) for field, values in entry.items()}
                                            for key, entry in scored.items()}
            for key, entry in scored.items():
                finite = np.isfinite(entry["inSpreads"]) & np.isfinite(entry["inErrors"])
                collected.setdefault(key, []).append((entry["inSpreads"][finite], entry["inErrors"][finite]))
    for key, parts in collected.items():
        spreads, errors = (np.concatenate(column) for column in zip(*parts))
        if len(spreads):
            report["summary"][key] = {"rmsInSpreads": round(float(np.sqrt(np.mean(spreads ** 2))), 3),
                                      "maxInSpreads": round(float(np.max(np.abs(spreads))), 3),
                                      "rmsInErrors": round(float(np.sqrt(np.mean(errors ** 2))), 3), "elements": int(len(spreads))}
    return report
