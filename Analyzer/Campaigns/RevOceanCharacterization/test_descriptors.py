#!/usr/bin/env python3
"""Tests of the descriptor module on synthetic signals of known decay, density and correlation.

Nothing here needs the reference plug-in:

    python -m unittest test_descriptors
"""
from __future__ import annotations

import math
import unittest

import numpy as np
from scipy import signal

import descriptors

FS = 48000
PERIOD = 8192


def decaying_noise(t60: float, seconds: float, seed: int, floor_db: float | None = None) -> np.ndarray:
    """Stereo Gaussian noise whose level falls 60 dB in `t60` seconds, optionally over a stationary floor."""
    generator = np.random.default_rng(seed)
    frames = int(seconds * FS)
    envelope = 10.0 ** (-3.0 * np.arange(frames) / FS / t60)
    response = generator.standard_normal((frames, 2)) * envelope[:, None]
    if floor_db is not None:
        response += generator.standard_normal((frames, 2)) * 10.0 ** (floor_db / 20.0)
    return response


def broadband_t30(response: np.ndarray, limits: descriptors.DecayLimits | None = None) -> float:
    energy = descriptors.block_energy(response, FS)
    return descriptors.decay_time(descriptors.decay_curve(energy, limits=limits), -5.0, -35.0)


def everything(energy: np.ndarray) -> descriptors.DecayLimits:
    """Plain backward integration of the whole response: no truncation, subtraction or tail."""
    return descriptors.DecayLimits(len(energy), 0.0, 0.0, False)


class Bands(unittest.TestCase):
    def test_centres_match_the_nominal_ones(self):
        centres = descriptors.band_centres()
        self.assertEqual(len(centres), 25)
        self.assertAlmostEqual(centres[12], 1000.0)
        for exact, nominal in zip(centres, descriptors.NOMINAL_CENTRES):
            self.assertAlmostEqual(exact / nominal, 1.0, delta=0.012)

    def test_adjacent_bands_share_an_edge(self):
        centres = descriptors.band_centres()
        for lower, upper in zip(centres[:-1], centres[1:]):
            self.assertAlmostEqual(descriptors.band_edges(lower)[1], descriptors.band_edges(upper)[0])

    def test_band_filter_passes_its_centre_and_rejects_the_next_octave(self):
        time = np.arange(FS) / FS
        inside = descriptors.band_filter(np.sin(2 * np.pi * 1000.0 * time), FS, 1000.0)
        outside = descriptors.band_filter(np.sin(2 * np.pi * 2000.0 * time), FS, 1000.0)
        self.assertAlmostEqual(np.std(inside[4800:-4800]) * math.sqrt(2.0), 1.0, delta=0.01)
        self.assertLess(np.std(outside[4800:-4800]), 0.01)


class Decay(unittest.TestCase):
    def test_exact_exponential(self):
        # Block energies of an exact exponential with T60 = 1.2 s: every time follows in closed form.
        block = descriptors.ENERGY_BLOCK_SECONDS
        energy = 10.0 ** (-6.0 * np.arange(3000) * block / 1.2)
        curve = descriptors.decay_curve(energy)
        for upper, lower in ((-0.1, -10.0), (-5.0, -25.0), (-5.0, -35.0)):
            self.assertAlmostEqual(descriptors.decay_time(curve, upper, lower), 1.2, delta=0.002)
        self.assertAlmostEqual(descriptors.level_time(curve, -10.0), 0.2, delta=0.001)
        self.assertAlmostEqual(descriptors.level_time(curve, -60.0), 1.2, delta=0.002)

    def test_decay_is_recovered_in_every_band(self):
        # T60 = 0.5 s cut after 0.75 s, the shortest decay of the campaign: B T = 7 in the 63 Hz band.
        rows = [descriptors.decay_times(decaying_noise(0.5, 0.75, seed), FS) for seed in range(12)]
        summary = descriptors.summarise(rows)
        for name, low_bands, high_bands in (("t30", 0.10, 0.04), ("t20", 0.12, 0.04), ("edt", 0.20, 0.08)):
            ratio = summary[name]["mean"] / 0.5
            self.assertLess(np.max(np.abs(ratio[:8] - 1.0)), low_bands, name)       # 63 to 315 Hz
            self.assertLess(np.max(np.abs(ratio[8:] - 1.0)), high_bands, name)      # 400 Hz to 16 kHz
        self.assertAlmostEqual(summary["t30Broadband"]["mean"][0], 0.5, delta=0.005)
        self.assertAlmostEqual(summary["levelTimes"]["mean"][4], 0.5 / 6.0, delta=0.003)   # -10 dB
        # The narrow low bands scatter more from response to response than the wide high bands.
        self.assertGreater(summary["t30"]["sd"][0], 3.0 * summary["t30"]["sd"][-1])

    def test_reversed_filter_does_not_lengthen_a_short_decay(self):
        # 63 Hz band, T60 = 0.3 s (B T = 4.4): forward filtering adds its own ringing, reversed does not.
        centre = descriptors.band_centres()[0]
        sections = signal.butter(3, descriptors.band_edges(centre), btype="bandpass", fs=FS, output="sos")
        reverse, forward = [], []
        for seed in range(24):
            response = decaying_noise(0.3, 1.0, seed)
            for store, band in ((reverse, descriptors.band_filter(response, FS, centre)), (forward, signal.sosfilt(sections, response, axis=0))):
                store.append(descriptors.decay_time(descriptors.decay_curve(descriptors.block_energy(band, FS)), -5.0, -25.0))
        self.assertAlmostEqual(np.mean(reverse), 0.3, delta=0.02)
        self.assertGreater(np.mean(forward), 0.33)

    def test_noise_floor_is_found_and_removed(self):
        response = decaying_noise(1.0, 3.0, 5, floor_db=-45.0)
        energy = descriptors.block_energy(response, FS)
        limits = descriptors.decay_limits(energy)
        self.assertTrue(limits.floor)
        self.assertAlmostEqual(limits.stop * descriptors.ENERGY_BLOCK_SECONDS, 0.75, delta=0.06)   # 45 dB at 60 dB/s
        self.assertAlmostEqual(10.0 * math.log10(limits.noise / (2 * 48)), -45.0, delta=0.5)       # per sample and channel
        self.assertAlmostEqual(broadband_t30(response), 1.0, delta=0.02)
        self.assertGreater(broadband_t30(response, everything(energy)), 2.0)                       # the uncorrected curve

    def test_a_response_cut_while_decaying_is_compensated(self):
        response = decaying_noise(1.0, 0.7, 5)              # ends 42 dB down, as when the next impulse arrives
        energy = descriptors.block_energy(response, FS)
        limits = descriptors.decay_limits(energy)
        self.assertFalse(limits.floor)
        self.assertEqual(limits.stop, len(energy))
        self.assertAlmostEqual(descriptors.decay_curve(energy, limits=limits)[-1], -42.0, delta=0.7)
        self.assertAlmostEqual(broadband_t30(response), 1.0, delta=0.01)
        self.assertLess(broadband_t30(response, everything(energy)), 0.99)

    def test_times_that_need_a_deeper_curve_are_missing(self):
        result = descriptors.decay_times(decaying_noise(1.0, 0.45, 2), FS)     # ends 27 dB down
        self.assertTrue(math.isfinite(result["edtBroadband"][0]))
        self.assertTrue(math.isnan(result["t30Broadband"][0]))
        self.assertTrue(math.isnan(result["levelTimes"][-1]))

    def test_two_slopes_are_told_apart(self):
        fast, slow = decaying_noise(0.4, 3.0, 1), decaying_noise(2.0, 3.0, 2) * 10.0 ** (-20.0 / 20.0)
        result = descriptors.decay_times(fast + slow, FS)
        self.assertLess(result["edtBroadband"][0], 0.6)
        self.assertGreater(result["t30Broadband"][0], 1.2)


class Spectrum(unittest.TestCase):
    def test_unit_impulse_is_zero_db(self):
        impulse = np.zeros((4800, 2))
        impulse[100, 0] = 1.0
        impulse[200, 1] = 0.5
        levels = descriptors.band_levels(impulse, FS)
        self.assertLess(np.max(np.abs(levels[:, 0])), 1e-9)
        self.assertLess(np.max(np.abs(levels[:, 1] - 20.0 * math.log10(0.5))), 1e-9)
        self.assertAlmostEqual(descriptors.total_level(impulse)[1], 20.0 * math.log10(0.5))

    def test_known_filter(self):
        b, a = signal.butter(1, 1000.0, fs=FS)
        impulse = np.zeros((1 << 15, 1))
        impulse[0] = 1.0
        levels = descriptors.band_levels(signal.lfilter(b, a, impulse, axis=0), FS)[:, 0]
        frequencies, response = signal.freqz(b, a, 1 << 15, fs=FS)
        for index, centre in enumerate(descriptors.band_centres()):
            low, high = descriptors.band_edges(centre)
            inside = (frequencies >= low) & (frequencies < high)
            self.assertAlmostEqual(levels[index], 10.0 * math.log10(np.mean(np.abs(response[inside]) ** 2)), delta=0.05)

    def test_response_descriptors_carry_the_sixth_octave_spectrum(self):
        impulse = np.zeros((FS, 2))
        impulse[10, 0] = 1.0
        spectrum = descriptors.response_descriptors(impulse, FS)["spectrum"]
        centres = descriptors.long_term_spectrum(impulse, FS, descriptors.SPECTRUM_FRACTION, descriptors.SPECTRUM_LOW)[0]
        self.assertEqual(len(spectrum), len(centres))
        self.assertAlmostEqual(centres[1] / centres[0], 10.0 ** 0.05, places=9)
        self.assertLess(np.max(np.abs(spectrum)), 1e-9)                 # one channel carries the unit impulse

    def test_long_term_spectrum_grid(self):
        generator = np.random.default_rng(3)
        centres, gains = descriptors.long_term_spectrum(generator.standard_normal((FS, 2)), FS, fraction=3)
        self.assertAlmostEqual(centres[np.argmin(np.abs(centres - 1000.0))], 1000.0)
        self.assertGreaterEqual(centres[0], 20.0)
        self.assertLessEqual(centres[-1], 20000.0)
        # White noise: FS samples of unit variance in two channels give |X|^2 = 2 FS per bin on average.
        self.assertLess(np.max(np.abs(gains[8:] - 10.0 * math.log10(2 * FS))), 1.5)


def poisson_echoes(rate: float, seed: int) -> np.ndarray:
    """One second with `rate` Gaussian-weighted unit echoes at random samples."""
    generator = np.random.default_rng(seed)
    response = np.zeros(FS)
    np.add.at(response, generator.integers(0, FS, int(rate)), generator.standard_normal(int(rate)))
    return response


class EchoDensity(unittest.TestCase):
    def test_gaussian_noise_is_one(self):
        generator = np.random.default_rng(1)
        _, profile, energy = descriptors.echo_density(generator.standard_normal((FS, 2)), FS, hop=0.01)
        self.assertAlmostEqual(energy[2:-2].mean(), 1.0, delta=0.02)
        self.assertAlmostEqual(profile[2:-2].mean(), 1.0, delta=0.02)
        self.assertLess(profile[2:-2].std(), 0.08)

    def test_grows_with_the_echo_rate(self):
        means = [descriptors.echo_density(poisson_echoes(rate, 1), FS, hop=0.01)[1][2:-2].mean() for rate in (50, 1000, 5000, 20000)]
        self.assertLess(means[0], 0.01)
        self.assertTrue(all(lower < upper for lower, upper in zip(means[:-1], means[1:])))
        # For rare echoes the fraction of non-zero samples is rate / FS and nearly all of them exceed
        # sigma only when they are among the larger ones: the profile stays far below 1.
        self.assertAlmostEqual(means[1], 0.058, delta=0.02)
        self.assertAlmostEqual(means[3], 0.58, delta=0.06)

    def test_silence_is_zero(self):
        _, profile, _ = descriptors.echo_density(np.zeros((4800, 2)), FS)
        self.assertEqual(float(np.abs(profile).max()), 0.0)

    def test_build_up_time_of_sparse_then_dense(self):
        # Sparse echoes until 0.25 s, Gaussian noise after: 0.9 is reached where the window is mostly noise.
        generator = np.random.default_rng(4)
        response = np.stack([poisson_echoes(300, seed) for seed in (7, 8)], axis=1)
        response[12000:] = generator.standard_normal((FS - 12000, 2))
        profile, reached = descriptors.echo_density_build_up(response, FS)
        self.assertEqual(len(profile), 80)
        self.assertLess(profile[:20].max(), 0.2)
        self.assertAlmostEqual(profile[40:].mean(), 1.0, delta=0.03)
        self.assertAlmostEqual(reached, 0.25, delta=0.015)

    def test_a_weak_dense_floor_before_the_first_arrival_is_silent(self):
        # Nothing but a floor 60 dB down until 0.1 s (the previous response's tail), then the response.
        generator = np.random.default_rng(6)
        response = 1e-3 * generator.standard_normal((FS, 2))
        response[4800:] += generator.standard_normal((FS - 4800, 2)) * 10.0 ** (-3.0 * np.arange(FS - 4800) / FS / 0.5)[:, None]
        profile, reached = descriptors.echo_density_build_up(response, FS)
        self.assertEqual(float(profile[:8].max()), 0.0)
        self.assertAlmostEqual(reached, 0.1, delta=0.012)
        self.assertAlmostEqual(profile[15:].mean(), 1.0, delta=0.03)       # the decayed tail is not gated

    def test_build_up_time_is_missing_when_never_dense(self):
        response = np.stack([poisson_echoes(300, seed) for seed in (7, 8)], axis=1)
        self.assertTrue(math.isnan(descriptors.echo_density_build_up(response, FS)[1]))


class Interchannel(unittest.TestCase):
    def setUp(self):
        generator = np.random.default_rng(11)
        self.left, self.other = generator.standard_normal((2, 2 * FS))

    def test_mixing_coefficient_is_the_correlation(self):
        for wanted in (0.0, 0.3, 0.8):
            right = wanted * self.left + math.sqrt(1.0 - wanted ** 2) * self.other
            result = descriptors.interchannel(np.stack([self.left, right], axis=1), FS)
            self.assertAlmostEqual(result["correlationBroadband"][0], wanted, delta=0.01)
            self.assertLess(np.max(np.abs(result["correlation"][8:] - wanted)), 0.08)
            self.assertTrue(np.all(result["coherence"] >= np.abs(result["correlation"]) - 1e-12))

    def test_inverted_and_quadrature_channels(self):
        inverted = descriptors.interchannel(np.stack([self.left, -self.left], axis=1), FS)
        self.assertLess(np.max(np.abs(inverted["correlation"] + 1.0)), 1e-9)
        quadrature = descriptors.interchannel(np.stack([self.left, np.imag(signal.hilbert(self.left))], axis=1), FS)
        self.assertLess(np.max(np.abs(quadrature["correlation"])), 0.02)
        self.assertGreater(np.min(quadrature["coherence"]), 0.98)

    def test_band_dependent_correlation(self):
        # Shared below 1 kHz, independent above.
        sections = signal.butter(8, 1000.0, fs=FS, output="sos")
        shared = signal.sosfiltfilt(sections, self.left)
        right = shared + (self.other - signal.sosfiltfilt(sections, self.other))
        left = shared + (self.left - shared)
        result = descriptors.interchannel(np.stack([left, right], axis=1), FS)
        self.assertGreater(np.min(result["correlation"][:10]), 0.95)        # up to 500 Hz
        self.assertLess(np.max(np.abs(result["correlation"][15:])), 0.1)    # from 2 kHz

    def test_time_window(self):
        response = np.stack([self.left, np.concatenate([self.left[:FS], self.other[FS:]])], axis=1)
        self.assertGreater(descriptors.interchannel(response, FS, stop=1.0)["correlationBroadband"][0], 0.999)
        self.assertLess(abs(descriptors.interchannel(response, FS, start=1.0)["correlationBroadband"][0]), 0.02)


class Modulation(unittest.TestCase):
    def setUp(self):
        self.noise = descriptors.periodic_noise(20 * FS, PERIOD, 3).astype(np.float64)
        b, a = signal.butter(2, 3000.0, fs=FS)
        self.steady = signal.lfilter(b, a, self.noise, axis=0)[4 * PERIOD:]      # a fixed filter, past its transient
        self.time = np.arange(len(self.steady)) / FS

    def test_periodic_noise(self):
        self.assertTrue(np.array_equal(self.noise[:PERIOD], self.noise[5 * PERIOD:6 * PERIOD]))
        self.assertAlmostEqual(float(np.abs(self.noise).max()), 0.5, places=6)
        magnitude = np.abs(np.fft.rfft(self.noise[1234:1234 + PERIOD, 0]))
        self.assertLess(magnitude[1:-1].std() / magnitude[1:-1].mean(), 1e-5)
        self.assertLess(abs(np.corrcoef(self.noise[:PERIOD, 0], self.noise[:PERIOD, 1])[0, 1]), 0.05)

    def test_a_fixed_filter_does_not_fluctuate(self):
        result = descriptors.noise_descriptors(self.steady[:36 * PERIOD], FS, PERIOD)
        self.assertLess(result["fluctuation"].max(), 1e-9)
        self.assertLess(result["fluctuationBroadband"][0], 1e-9)
        self.assertLess(result["modulationRates"].max(), 1e-9)
        self.assertLess(result["timeVariance"].max(), 1e-9)

    def test_amplitude_modulation_of_known_depth(self):
        modulated = self.steady * (1.0 + 0.1 * np.sin(2 * np.pi * 0.6 * self.time))[:, None]
        result = descriptors.level_fluctuation(modulated, FS, PERIOD)
        # 20 log10(1 + 0.1 sin) has 0.614 dB rms; a window of 171 ms averages 0.6 Hz down by 1.6 %.
        self.assertLess(np.max(np.abs(result["fluctuation"] - 0.605)), 0.01)
        self.assertAlmostEqual(result["fluctuationBroadband"][0], 0.605, delta=0.01)
        frequencies, spectrum = descriptors.modulation_spectrum(modulated, FS, PERIOD)
        self.assertAlmostEqual(frequencies[np.argmax(spectrum)], 0.6, delta=0.06)

    def test_modulation_rates_place_a_known_rate(self):
        # 0.45 Hz is the middle of the 0.32 to 0.64 Hz octave, 2 Hz of the 1.28 to 2.56 Hz octave. A window of
        # one period averages a rate f down by sinc(f * period / FS). The narrow low bands add ripple of their
        # own at fast rates, so 2 Hz is checked in the two upper regions.
        for rate, octave, regions in ((0.45, 3, slice(0, 3)), (2.0, 5, slice(1, 3))):
            modulated = self.steady * (1.0 + 0.1 * np.sin(2 * np.pi * rate * self.time))[:, None]
            rates = descriptors.modulation_rates(modulated, FS, PERIOD).reshape(3, 7)[regions]
            expected = 0.614 * np.sinc(rate * PERIOD / FS)
            self.assertLess(np.max(np.abs(rates[:, octave] - expected)), 0.01, rate)
            self.assertLess(np.delete(rates, octave, axis=1).max(), 0.06, rate)
            fluctuation = descriptors.level_fluctuation(modulated, FS, PERIOD)["fluctuation"]
            self.assertAlmostEqual(math.sqrt(np.sum(rates[-1] ** 2)), fluctuation[16:].mean(), delta=0.01)

    def test_delay_modulation_shows_as_time_variance_of_the_high_bands(self):
        # A delay swinging +-0.88 ms at 0.6 Hz: 0.35 rad of phase at 63 Hz, 44 rad at 8 kHz.
        position = np.arange(len(self.steady)) - 0.00088 * FS * np.sin(2 * np.pi * 0.6 * self.time)
        moved = np.stack([np.interp(position, np.arange(len(self.steady)), self.steady[:, channel]) for channel in range(2)], axis=1)
        variance = descriptors.time_variance(moved[:(len(moved) // PERIOD) * PERIOD], FS, PERIOD)
        self.assertLess(variance[0], 0.15)
        self.assertGreater(variance[15:].min(), 0.9)

    def test_gaussian_noise_fluctuates_by_its_own_statistics(self):
        generator = np.random.default_rng(9)
        result = descriptors.level_fluctuation(generator.standard_normal((20 * FS, 2)), FS, PERIOD)["fluctuation"]
        width = np.array([high - low for low, high in map(descriptors.band_edges, descriptors.band_centres())])
        expected = 10.0 / math.log(10.0) / np.sqrt(width * PERIOD / FS)
        self.assertLess(np.max(np.abs(result[4:] / expected[4:] - 1.0)), 0.15)


class Ensemble(unittest.TestCase):
    def rows(self, mean: float, sd: float, count: int, seed: int) -> list:
        generator = np.random.default_rng(seed)
        return [{"level": mean + sd * generator.standard_normal(1), "t30": 2.0 + 0.05 * generator.standard_normal(25)} for _ in range(count)]

    def test_summarise_ignores_missing_values(self):
        rows = [{"t30": np.array([1.0, math.nan])}, {"t30": np.array([3.0, math.nan])}, {"t30": np.array([math.nan, 5.0])}]
        summary = descriptors.summarise(rows)["t30"]
        self.assertEqual(summary["n"].tolist(), [2, 1])
        self.assertEqual(summary["mean"].tolist(), [2.0, 5.0])
        self.assertAlmostEqual(summary["sd"][0], math.sqrt(2.0))
        self.assertTrue(math.isnan(summary["sd"][1]))

    def test_same_distribution_scores_inside_the_spread(self):
        scored = descriptors.compare(descriptors.summarise(self.rows(-10.0, 2.0, 400, 1)), descriptors.summarise(self.rows(-10.0, 2.0, 400, 2)))
        self.assertLess(abs(scored["level"]["inSpreads"][0]), 0.2)
        self.assertLess(abs(scored["level"]["inErrors"][0]), 3.0)
        self.assertAlmostEqual(scored["level"]["spreadRatio"][0], 1.0, delta=0.15)
        self.assertLess(np.max(np.abs(scored["t30"]["inSpreads"])), 0.35)

    def test_a_shift_reads_in_units_of_the_spread(self):
        target = descriptors.summarise(self.rows(-10.0, 2.0, 400, 2))
        scored = descriptors.compare(descriptors.summarise(self.rows(-6.0, 0.5, 400, 1)), target)
        self.assertAlmostEqual(scored["level"]["difference"][0], 4.0, delta=0.3)
        self.assertAlmostEqual(scored["level"]["inSpreads"][0], 2.0, delta=0.2)
        self.assertGreater(scored["level"]["inErrors"][0], 20.0)
        self.assertAlmostEqual(scored["level"]["spreadRatio"][0], 0.25, delta=0.05)

    def test_a_constant_descriptor_uses_the_floor(self):
        target = descriptors.summarise([{"level": np.array([-10.0])}] * 5)
        scored = descriptors.compare(descriptors.summarise([{"level": np.array([-9.95])}] * 5), target)
        self.assertAlmostEqual(scored["level"]["inSpreads"][0], 0.5, delta=1e-6)      # 0.05 dB against the 0.1 dB floor


def synthetic_render(specification: dict, t60: float, gain: float, seed: int, latency: int) -> np.ndarray:
    """A stand-in reverberator: every impulse starts decaying noise on its own output channel(s)."""
    generator = np.random.default_rng(seed)
    output = np.zeros((specification["frames"] + latency, 2))
    bounds = [sample for sample, _ in specification["events"]] + [specification["frames"]]
    for index, (sample, channel) in enumerate(specification["events"]):
        frames = bounds[index + 1] - sample
        envelope = gain * specification["amplitude"] * 10.0 ** (-3.0 * np.arange(frames) / FS / t60)
        for target in descriptors.INPUT_CHANNELS[channel]:
            output[sample + latency:sample + latency + frames, target] += generator.standard_normal(frames) * envelope
    return output


class Targets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        starts = [int(0.1 * FS) + index * int(0.6 * FS) for index in range(12)]
        cls.impulses = {"kind": "impulses", "frames": starts[-1] + int(0.6 * FS), "amplitude": 0.5,
                        "events": [[start, "LRM"[index % 3]] for index, start in enumerate(starts)]}
        cls.noise = {"kind": "periodicNoise", "frames": 20 * PERIOD, "period": PERIOD, "seed": 5, "peak": 0.5,
                     "steadyFrom": 4 * PERIOD, "segmentPeriods": 8}
        reference = [synthetic_render(cls.impulses, 0.3, 1.0, seed, 48) for seed in (1, 2)]
        b, a = signal.butter(2, 3000.0, fs=FS)
        cls.filtered = np.pad(signal.lfilter(b, a, descriptors.build_stimulus(cls.noise).astype(np.float64), axis=0), ((48, 0), (0, 0)))
        cls.targets = {"sampleRate": FS, "cases": {
            "impulses": {"stimulus": cls.impulses, "groups": descriptors.groups_as_lists(
                descriptors.summarise_groups(descriptors.case_descriptors(reference, cls.impulses, FS, 48)))},
            "noise": {"stimulus": cls.noise, "groups": descriptors.groups_as_lists(
                descriptors.summarise_groups(descriptors.case_descriptors([cls.filtered], cls.noise, FS, 48)))}}}

    def test_stimulus_and_responses(self):
        stimulus = descriptors.build_stimulus(self.impulses)
        self.assertEqual(stimulus.dtype, np.float32)
        self.assertEqual(stimulus.shape, (self.impulses["frames"], 2))
        first, second, third = (sample for sample, _ in self.impulses["events"][:3])
        self.assertEqual(stimulus[first].tolist(), [0.5, 0.0])
        self.assertEqual(stimulus[second].tolist(), [0.0, 0.5])
        self.assertEqual(stimulus[third].tolist(), [0.5, 0.5])
        self.assertEqual(float(np.abs(stimulus).sum()), 0.5 * 16)
        responses = descriptors.split_responses(synthetic_render(self.impulses, 0.3, 1.0, 1, 48), self.impulses, 48)
        self.assertEqual([channel for channel, _ in responses], list("LRMLRMLRMLRM"))
        self.assertEqual(float(np.abs(responses[0][1][:, 1]).max()), 0.0)       # an impulse on the left only
        self.assertEqual(len(descriptors.noise_segments(self.filtered, self.noise, 48)), 2)

    def test_groups(self):
        groups = self.targets["cases"]["impulses"]["groups"]
        self.assertEqual(sorted(groups), ["L", "M", "R", "all"])
        self.assertEqual(groups["all"]["t30"]["n"][12], 24)
        self.assertEqual(groups["M"]["level"]["n"], [8])
        self.assertAlmostEqual(groups["all"]["t30Broadband"]["mean"][0], 0.3, delta=0.005)
        self.assertNotIn("level", groups["all"])               # levels depend on the driven input
        self.assertEqual(sorted(self.targets["cases"]["noise"]["groups"]), ["all"])

    def test_another_draw_of_the_reference_is_inside_the_spread(self):
        renders = {"impulses": [synthetic_render(self.impulses, 0.3, 1.0, seed, 48) for seed in (3, 4)], "noise": [self.filtered]}
        report = descriptors.compare_to_targets(renders, self.targets)
        # Two means of n responses differ by sqrt(2 / n) spreads on average: 0.5 for the 8 per input, 0.29 for all 24.
        for name in ("t30", "t20", "bandLevel", "level", "levelTimes", "echoDensity", "fluctuation", "timeVariance"):
            self.assertLess(report["summary"][name]["rmsInSpreads"], 0.8, name)
        self.assertEqual(report["summary"]["fluctuation"]["maxInSpreads"], 0.0)
        self.assertLess(report["summary"]["t30"]["rmsInErrors"], 1.5)       # 1 for two draws of one distribution
        self.assertEqual(sorted(report["cases"]["impulses"]), ["L", "M", "R", "all"])

    def test_a_wrong_candidate_is_far_outside(self):
        renders = {"impulses": [synthetic_render(self.impulses, 0.36, 10.0 ** (3.0 / 20.0), seed, 0) for seed in (3, 4)]}
        report = descriptors.compare_to_targets(renders, self.targets, latency=0)
        scored = report["cases"]["impulses"]["all"]
        self.assertAlmostEqual(scored["t30Broadband"]["difference"][0], 0.06, delta=0.006)
        self.assertGreater(scored["t30Broadband"]["inSpreads"][0], 5.0)
        # 3 dB of gain and 20 % more decay time: 3 + 10 log10(1.2) = 3.8 dB more energy.
        self.assertAlmostEqual(report["cases"]["impulses"]["L"]["level"]["difference"][0], 3.8, delta=0.15)
        self.assertGreater(report["summary"]["t30"]["rmsInSpreads"], 2.0)
        self.assertGreater(report["summary"]["t30"]["rmsInErrors"], 4.0)
        self.assertLess(report["summary"]["correlation"]["rmsInSpreads"], 0.8)     # the stereo image is unchanged


if __name__ == "__main__":
    unittest.main()
