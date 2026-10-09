#!/usr/bin/env python3
"""Reference targets of the statistical descriptors for the Tide model.

Renders the target stimuli through the reference (`revocean.capture`), computes
the descriptors of `descriptors.py` on every response and writes
`tide_structural_data/targets.json`: per case the stimulus, and per descriptor
the mean and the spread (standard deviation) over the ensemble of responses.

Cases. Macro 0, 50 and 100 %, each over Decay 0.5, 1, 2, 4, 8, 16 s at Size
100 % and over Size 30, 60, 100, 150, 200 % at Decay 2 s. Every other control
is at the neutral baseline of `revocean.BASELINE`: Mix 100 % (wet only),
Brightness 0 %, filters open, Width 100 %.

* Impulse cases. One render holds 8 to 24 impulses of amplitude 0.5, on the
  left, the right and both inputs in turn, at pseudo-random spacings that
  depend only on Decay. A response runs to the next impulse; the spacing lets
  the slowest band fall by more than 50 dB. Above Macro 0 the case is rendered
  several times (realisations). The spacings spread the impulse times over the
  cycle of the reference's 0.6 Hz delay modulation for Decay 1, 2 and 4 s, but
  not for Decay 8 and 16 s nor, per input, for Decay 0.5 s: there the impulses
  fall on a third of the cycle (`phase_coverage`), and the spread of those
  cases at Macro 0 is too small (`self_scores`). A later layout should add a
  uniform 0 to 1.667 s to every spacing.
* Noise cases (Decay 0.5, 2 and 8 s at Size 100 %): 40 s of periodic noise;
  the modulation descriptors use two segments of 17.7 s after 4 s of build-up.

The script also derives the laws reported in findings/stats.md (decay rate
against Decay and Size, level, echo density, spread, the effect of the Macro)
and stores them under "laws", and under "selfScores" what the reference scores
against itself. "impulsePhaseCoverage" and "populationAtDecay0.5" say how well
the few impulse times of a case stand for all impulse times; the second uses
the shared grid of `datasets.py`. Descriptor rows of every response go to the
scratch folder
(Analyzer/Results/RevOceanCharacterization/work/stats/rows.npz) for plots.

Run from this folder; captures are cached, the descriptors take a few minutes:

    python measure_targets.py
"""
from __future__ import annotations

import json
import math
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import datasets
import descriptors
import revocean

HERE = Path(__file__).resolve().parent
TARGETS = HERE / "tide_structural_data/targets.json"
SCRATCH = revocean.RESULTS / "work/stats"
SAMPLE_RATE = revocean.SAMPLE_RATE
LATENCY = 48
MACROS = (0, 50, 100)
DECAYS = (0.5, 1.0, 2.0, 4.0, 8.0, 16.0)        # at Size 100 %
SIZES = (30.0, 60.0, 100.0, 150.0, 200.0)       # at Decay 2 s
NOISE_DECAYS = (0.5, 2.0, 8.0)                  # at Size 100 %
# Decay -> (impulses per render, least spacing in seconds, largest extra spacing in seconds)
IMPULSE_LAYOUT = {0.5: (24, 1.0, 0.25), 1.0: (24, 1.1, 0.25), 2.0: (18, 1.9, 0.3),
                  4.0: (15, 3.6, 0.4), 8.0: (10, 6.4, 0.5), 16.0: (8, 9.6, 0.6)}
IMPULSE_AMPLITUDE = 0.5
NOISE = {"kind": "periodicNoise", "frames": 40 * SAMPLE_RATE, "period": 8192, "seed": 1, "peak": 0.5,
         "steadyFrom": 4 * SAMPLE_RATE, "segmentPeriods": 104}
MID_BANDS = slice(9, 16)                        # 500 Hz to 2 kHz
SHIFT_SECONDS = 0.37                            # of the second draw at Macro 0 (see self_scores)
SECOND_DRAW_DECAYS = (0.5, 1.0, 2.0, 8.0)
MODULATION_HZ = 0.6                             # the delay modulation of the reference (README, item 6)
WORKERS = 6


@dataclass(frozen=True)
class Case:
    name: str
    macro: int
    decay: float
    size: float
    specification: dict
    realisations: int

    def settings(self) -> dict:
        return {"macro": self.macro / 100.0, "decay": revocean.normalised("decay", self.decay),
                "size": revocean.normalised("size", self.size)}


def case_name(macro: int, decay: float, size: float) -> str:
    return f"m{macro:03d}_d{decay:g}_s{size:g}"


def impulse_specification(decay: float) -> dict:
    """Impulse times for a Decay value: 0.5 s of silence, then spacings of least + uniform(0, extra)."""
    count, least, extra = IMPULSE_LAYOUT[decay]
    generator = np.random.default_rng(int(round(decay * 1000)))
    spacings = least + extra * generator.random(count)
    starts = np.round((0.5 + np.concatenate([[0.0], np.cumsum(spacings)])) * SAMPLE_RATE).astype(int)
    return {"kind": "impulses", "frames": int(starts[-1]), "amplitude": IMPULSE_AMPLITUDE,
            "events": [[int(start), "LRM"[index % 3]] for index, start in enumerate(starts[:-1])]}


def impulse_realisations(macro: int, decay: float) -> int:
    """Renders per impulse case: the long, costly decays get fewer; Macro 100 % is the main listening point."""
    if macro == 0:
        return 1
    if macro == 100:
        return 3 if decay <= 2.0 else 2
    return 2 if decay <= 4.0 else 1


def noise_realisations(macro: int, decay: float) -> int:
    if macro == 0 or decay != 2.0:
        return 1
    return 3 if macro == 100 else 2


def plan() -> list:
    cases = []
    for macro in MACROS:
        for decay, size in [(decay, 100.0) for decay in DECAYS] + [(2.0, size) for size in SIZES if size != 100.0]:
            cases.append(Case(case_name(macro, decay, size), macro, decay, size,
                              impulse_specification(decay), impulse_realisations(macro, decay)))
        for decay in NOISE_DECAYS:
            if macro == 50 and decay != 2.0:
                continue
            cases.append(Case(case_name(macro, decay, 100.0) + "_noise", macro, decay, 100.0, NOISE, noise_realisations(macro, decay)))
    return cases


def between_realisations(rows: list, label: str) -> dict:
    """{descriptor: standard deviation of its per-realisation means} for the rows of one group."""
    parts = [descriptors.summarise([row for group, row in part if label in ("all", group)]) for part in rows]
    return {name: np.std([part[name]["mean"] for part in parts], axis=0, ddof=1) for name in parts[0]}


def measure(case: Case) -> tuple:
    """Render one case and summarise it: (JSON entry, descriptor rows per realisation)."""
    stimulus = descriptors.build_stimulus(case.specification)
    captures = [revocean.capture(stimulus, case.settings(), realisation=index) for index in range(case.realisations)]
    if any(capture.latency != LATENCY for capture in captures):
        raise RuntimeError("the reference reports an unexpected latency")
    rows = [descriptors.case_descriptors([capture.output], case.specification, SAMPLE_RATE, LATENCY) for capture in captures]
    groups = descriptors.summarise_groups([row for part in rows for row in part])
    entry = {
        "settings": {"macroPercent": case.macro, "decaySeconds": case.decay, "sizePercent": case.size, "mixPercent": 100},
        "readback": {key: captures[0].meta["readback"][f"id:{revocean.CONTROL[key].vst3_id}"] for key in ("mix", "macro", "decay", "size")},
        "normalised": {key: float(np.float32(value)) for key, value in case.settings().items()},
        "stimulus": case.specification, "realisations": case.realisations,
        "groups": descriptors.groups_as_lists(groups),
    }
    if case.realisations > 1:
        for label, group in entry["groups"].items():
            between = between_realisations(rows, label)
            for name, value in group.items():
                value["sdBetweenRealisations"] = descriptors.as_list(between[name])
    return entry, rows


def flat_rows(name: str, rows: list) -> dict:
    """The descriptor rows of a case as arrays for the scratch file: <case>/<descriptor> is (rows, elements)."""
    flat = {f"{name}/group": np.array([label for part in rows for label, _ in part]),
            f"{name}/realisation": np.array([index for index, part in enumerate(rows) for _ in part])}
    for key in rows[0][0][1]:
        flat[f"{name}/{key}"] = np.stack([row[key] for part in rows for _, row in part]).astype(np.float32)
    return flat


# ----------------------------------------------------------------------------------------------
# Laws derived from the targets


def mean_of(cases: dict, name: str, descriptor: str, group: str = "all") -> np.ndarray:
    return descriptors.as_array(cases[name]["groups"][group][descriptor]["mean"])


def sd_of(cases: dict, name: str, descriptor: str, group: str = "all") -> np.ndarray:
    return descriptors.as_array(cases[name]["groups"][group][descriptor]["sd"])


def impulse_cases(macro: int) -> list:
    """(Decay, Size) of the ten impulse cases of a Macro value: the Decay sweep, then the other sizes."""
    return [(decay, 100.0) for decay in DECAYS] + [(2.0, size) for size in SIZES if size != 100.0]


def rms(values) -> float:
    return float(np.sqrt(np.nanmean(np.square(values))))


def line_fit(x: np.ndarray, y: np.ndarray) -> dict:
    """y = intercept + slope * x by least squares, with the rms residual."""
    slope, intercept = np.polyfit(x, y, 1)
    return {"intercept": round(float(intercept), 5), "slope": round(float(slope), 6),
            "rmsResidual": round(rms(intercept + slope * x - y), 5)}


def loss_shape(loss: np.ndarray) -> dict:
    """Simple forms fitted to the excess decay rate from 1 kHz up (dB/s, rms residual in dB/s).

    `sineSquared` is c + K sin^2(pi f / fs), the shape of a gentle first-order
    low-pass or of an interpolated delay applied once per pass round a loop;
    `power` is c + K (f / 10 kHz)^g with g free, `powerTwo` the same with g = 2.
    """
    centres = descriptors.band_centres()[12:]
    target = loss[12:]

    def linear(shape: np.ndarray) -> tuple:
        design = np.stack([np.ones_like(shape), shape], axis=1)
        solution = np.linalg.lstsq(design, target, rcond=None)[0]
        return solution, rms(design @ solution - target)

    sine, sine_residual = linear(np.sin(np.pi * centres / SAMPLE_RATE) ** 2)
    exponents = np.arange(1.0, 3.0001, 0.005)
    residuals = [linear((centres / 1e4) ** exponent)[1] for exponent in exponents]
    best = float(exponents[int(np.argmin(residuals))])
    power, power_residual = linear((centres / 1e4) ** best)
    square, square_residual = linear((centres / 1e4) ** 2.0)
    return {"sineSquared": {"c": round(float(sine[0]), 3), "K": round(float(sine[1]), 2), "rmsResidual": round(sine_residual, 3)},
            "power": {"c": round(float(power[0]), 3), "K": round(float(power[1]), 2), "g": round(best, 3), "rmsResidual": round(power_residual, 3)},
            "powerTwo": {"c": round(float(square[0]), 3), "K": round(float(square[1]), 2), "rmsResidual": round(square_residual, 3)}}


def decay_law(cases: dict, macro: int) -> dict:
    """Decay rate against Decay and Size for one Macro value.

    With r = 60 / T30 the decay rate of a band in dB/s, the model is

        r(band, Decay, Size) = 60 / Decay + loss(band) * (100 / Size) ^ exponent.

    `loss` is the mean of r - 60 / Decay over Decay 2, 4, 8 and 16 s at Size
    100 %, `lossSd` its standard deviation over those four (how far the excess
    is independent of Decay). The exponent is fitted to the five sizes at Decay
    2 s in the bands from 5 kHz up, where the loss dominates the rate (least
    squares on the logarithm of the excess); exponent 1 is the round value.
    Residuals are the rms error of the predicted T30 in percent over the bands
    from 250 Hz up (`...AllBands` over all 25).
    """
    t30 = {(decay, size): mean_of(cases, case_name(macro, decay, size), "t30") for decay, size in impulse_cases(macro)}
    excess = {key: 60.0 / value - 60.0 / key[0] for key, value in t30.items()}
    long_decays = np.array([excess[(decay, 100.0)] for decay in DECAYS if decay >= 2.0])
    loss = long_decays.mean(axis=0)
    high = slice(19, 25)
    scale = np.log(100.0 / np.array(SIZES))
    ratio = np.array([np.log(excess[(2.0, size)][high] / excess[(2.0, 100.0)][high]) for size in SIZES])
    exponent = float(np.sum(ratio * scale[:, None]) / (np.sum(scale ** 2) * ratio.shape[1]))

    def error(keys: list, power: float, bands: slice) -> float:
        return round(100.0 * rms([(60.0 / (60.0 / decay + loss * (100.0 / size) ** power) / t30[(decay, size)] - 1.0)[bands]
                                  for decay, size in keys]), 2)

    long_keys = [(decay, 100.0) for decay in DECAYS if decay >= 2.0]
    short_keys = [(decay, 100.0) for decay in DECAYS if decay < 2.0]
    size_keys = [(2.0, size) for size in SIZES]
    body = slice(6, 25)
    ratios = {name: np.array([mean_of(cases, case_name(macro, decay, size), name) / t30[(decay, size)] for decay, size in t30])
              for name in ("t20", "edt")}
    return {
        "lossDbPerSecond": descriptors.as_list(loss, 3), "lossSdDbPerSecond": descriptors.as_list(long_decays.std(axis=0, ddof=1), 3),
        "lossShape": loss_shape(loss), "sizeExponent": round(exponent, 3),
        "t30ErrorPercent": {
            "decay2to16": error(long_keys, 1.0, body), "decay0.5and1": error(short_keys, 1.0, body),
            "sizesFittedExponent": error(size_keys, exponent, body), "sizesExponentOne": error(size_keys, 1.0, body),
            "decay2to16AllBands": error(long_keys, 1.0, slice(0, 25)), "decay0.5and1AllBands": error(short_keys, 1.0, slice(0, 25)),
            "sizesExponentOneAllBands": error(size_keys, 1.0, slice(0, 25))},
        "midBandT30Seconds": {f"{decay:g}": round(float(t30[(decay, 100.0)][MID_BANDS].mean()), 4) for decay in DECAYS},
        "midBandT30SecondsBySize": {f"{size:g}": round(float(t30[(2.0, size)][MID_BANDS].mean()), 4) for size in SIZES},
        "broadbandT30Seconds": {f"{decay:g}": mean_of(cases, case_name(macro, decay, 100.0), "t30Broadband")[0] for decay in DECAYS},
        "t20OverT30": {"from250Hz": round(float(np.nanmean(ratios["t20"][:, body])), 4),
                       "at16kHz": round(float(np.nanmean(ratios["t20"][:, 24])), 4)},
        "edtOverT30": {"midBands": round(float(np.nanmean(ratios["edt"][:, MID_BANDS])), 4),
                       "at16kHz": round(float(np.nanmean(ratios["edt"][:, 24])), 4)},
    }


def level_law(cases: dict, macro: int) -> dict:
    """Response energy against Decay and Size.

    Per case, for an impulse on one input (mean of the L and R groups):
    `levelDb` the energy of both outputs in dB re the unit impulse,
    `crossFeedDb` the opposite output against the same-side output,
    `bothInputsGainDb` the level for an impulse on both inputs minus `levelDb`
    (3.01 dB when the answers to the two inputs are uncorrelated, 6.02 dB when
    they are equal), `inputCorrelation` = 10^(gain / 10) / 2 - 1, and
    `midGainDb`, the mean over 500 Hz to 2 kHz of
    bandLevel - 10 log10(T30 / 13.8): the level of the power the band's
    exponential starts from (13.8 = 6 ln 10 turns T30 into the energy time
    constant). `gainSpectrumDb` is that quantity per band minus `midGainDb`,
    averaged over Decay 1 to 16 s at Size 100 %.

    Fits: `midGainDb` = g0 - p * 10 log10(Size / 100) over the five sizes
    (`sizeFit`, with the residual for p = 1), and its mean and standard
    deviation over Decay 1 to 16 s at Size 100 % (`decayFit`).
    """
    def band_gain(name: str) -> np.ndarray:
        level = 0.5 * (mean_of(cases, name, "bandLevel", "L") + mean_of(cases, name, "bandLevel", "R"))
        return level - 10.0 * np.log10(mean_of(cases, name, "t30") / (6.0 * math.log(10.0)))

    def one(name: str) -> dict:
        level = 0.5 * (mean_of(cases, name, "level", "L")[0] + mean_of(cases, name, "level", "R")[0])
        cross = 0.5 * (mean_of(cases, name, "levelRight", "L")[0] - mean_of(cases, name, "levelLeft", "L")[0]
                       + mean_of(cases, name, "levelLeft", "R")[0] - mean_of(cases, name, "levelRight", "R")[0])
        gain = mean_of(cases, name, "level", "M")[0] - level
        return {"levelDb": round(level, 2), "crossFeedDb": round(cross, 2), "bothInputsGainDb": round(gain, 2),
                "inputCorrelation": round(10.0 ** (gain / 10.0) / 2.0 - 1.0, 3), "midGainDb": round(float(band_gain(name)[MID_BANDS].mean()), 2)}

    by_decay = {f"{decay:g}": one(case_name(macro, decay, 100.0)) for decay in DECAYS}
    by_size = {f"{size:g}": one(case_name(macro, 2.0, size)) for size in SIZES}
    size_db = 10.0 * np.log10(np.array(SIZES) / 100.0)
    gains = np.array([by_size[f"{size:g}"]["midGainDb"] for size in SIZES])
    fit = line_fit(size_db, gains)
    steady = np.array([by_decay[f"{decay:g}"]["midGainDb"] for decay in DECAYS if decay >= 1.0])
    spectrum = np.mean([band_gain(case_name(macro, decay, 100.0)) for decay in DECAYS if decay >= 1.0], axis=0)
    return {"byDecay": by_decay, "bySize": by_size,
            "sizeFit": {"g0Db": fit["intercept"], "p": round(-fit["slope"], 3), "rmsResidualDb": fit["rmsResidual"],
                        "g0DbForPOne": round(float(np.mean(gains + size_db)), 3), "rmsResidualDbForPOne": round(float(np.std(gains + size_db)), 3)},
            "decayFit": {"meanDb": round(float(steady.mean()), 3), "sdDb": round(float(steady.std(ddof=1)), 3)},
            "gainSpectrumDb": descriptors.as_list(spectrum - spectrum[MID_BANDS].mean(), 2)}


def echo_density_law(cases: dict, macro: int) -> dict:
    """Echo density build-up and onset per case, and their straight-line laws against Size.

    Per case: the mean and spread over responses of the time the profile
    reaches 0.9, the time the ensemble-mean profile reaches 0.9 (10 ms
    resolution) and the onset (the -0.1 dB level time). Fits over the five
    sizes at Decay 2 s: time = intercept + slope * Size (seconds, Size in
    percent), and for the build-up also the proportional law time = k * Size.
    """
    centres = (np.arange(int(round(descriptors.ECHO_DENSITY_SECONDS / descriptors.ECHO_DENSITY_STEP))) + 0.5) * descriptors.ECHO_DENSITY_STEP

    def one(name: str) -> dict:
        values = (mean_of(cases, name, "echoDensityTime")[0], sd_of(cases, name, "echoDensityTime")[0],
                  descriptors.echo_density_time(centres, mean_of(cases, name, "echoDensity")),
                  mean_of(cases, name, "levelTimes")[0], sd_of(cases, name, "levelTimes")[0])
        return dict(zip(("reaches0.9Seconds", "reaches0.9SdSeconds", "meanProfileReaches0.9Seconds", "onsetSeconds", "onsetSdSeconds"),
                        descriptors.as_list(values)))

    by_size = {f"{size:g}": one(case_name(macro, 2.0, size)) for size in SIZES}
    sizes = np.array(SIZES)
    reached = np.array([by_size[f"{size:g}"]["reaches0.9Seconds"] for size in SIZES])
    slope = float(np.sum(reached * sizes) / np.sum(sizes ** 2))
    return {"byDecay": {f"{decay:g}": one(case_name(macro, decay, 100.0)) for decay in DECAYS}, "bySize": by_size,
            "reaches0.9AgainstSize": line_fit(sizes, reached),
            "reaches0.9ProportionalToSize": {"secondsPerPercent": round(slope, 6), "rmsResidual": round(rms(slope * sizes - reached), 5)},
            "onsetAgainstSize": line_fit(sizes, np.array([by_size[f"{size:g}"]["onsetSeconds"] for size in SIZES]))}


def spread_summary(cases: dict, macro: int) -> dict:
    """Typical spread of each descriptor over the ten impulse cases of a Macro value.

    `medianSd` and `largestSd` are taken over cases and elements of the
    standard deviation over responses (the "all" group for the pooled
    descriptors, the L, R and M groups for the others). Above Macro 0,
    `betweenRealisationsOverExpected` is the rms over cases and elements of the
    standard deviation of the per-realisation means, divided by the rms of
    sd / sqrt(responses per realisation): 1 when a realisation is just another
    stretch of time, above 1 when instances differ by more than that.
    """
    summary = {}
    for descriptor in descriptors.SPREAD_FLOORS:
        spreads, between, expected = [], [], []
        for decay, size in impulse_cases(macro):
            case = cases[case_name(macro, decay, size)]
            for label in (("all",) if descriptor in descriptors.POOLED else ("L", "R", "M")):
                group = case["groups"][label].get(descriptor)
                if group is None:
                    continue
                spread = descriptors.as_array(group["sd"])
                spreads.append(spread)
                if "sdBetweenRealisations" in group:
                    between.append(descriptors.as_array(group["sdBetweenRealisations"]))
                    expected.append(spread / np.sqrt(np.maximum(np.array(group["n"]) / case["realisations"], 1.0)))
        if not spreads:
            continue
        spreads = np.concatenate(spreads)
        summary[descriptor] = {"medianSd": round(float(np.nanmedian(spreads)), 4), "largestSd": round(float(np.nanmax(spreads)), 4)}
        if between:
            both = np.isfinite(np.concatenate(between)) & np.isfinite(np.concatenate(expected))
            summary[descriptor]["betweenRealisationsOverExpected"] = round(
                rms(np.concatenate(between)[both]) / rms(np.concatenate(expected)[both]), 3)
    return summary


def macro_effect(cases: dict, macro: int) -> dict:
    """What a Macro value changes against Macro 0, averaged over the ten impulse cases (sd over the cases).

    Band level difference (one input, both outputs), T30 ratio per band,
    onset shift and the ratio of the echo density build-up time; and the rms
    error in percent of the T30 that the decay law predicts with the loss table
    of Macro 0, over the ten cases and the bands from 250 Hz to 10 kHz.
    """
    def against_zero(function) -> np.ndarray:
        return np.array([function(case_name(macro, decay, size)) - function(case_name(0, decay, size)) for decay, size in impulse_cases(macro)])

    level = against_zero(lambda name: 0.5 * (mean_of(cases, name, "bandLevel", "L") + mean_of(cases, name, "bandLevel", "R")))
    decay = against_zero(lambda name: np.log(mean_of(cases, name, "t30")))
    onset = against_zero(lambda name: mean_of(cases, name, "levelTimes")[0])
    density = against_zero(lambda name: np.log(mean_of(cases, name, "echoDensityTime")[0]))
    loss = decay_law(cases, 0)["lossDbPerSecond"]
    predicted = [(60.0 / (60.0 / decay + np.array(loss) * 100.0 / size) / mean_of(cases, case_name(macro, decay, size), "t30") - 1.0)[6:23]
                 for decay, size in impulse_cases(macro)]
    return {"t30ErrorPercentWithMacro0Loss": round(100.0 * rms(predicted), 2),
            "bandLevelDb": descriptors.as_list(level.mean(axis=0), 2), "bandLevelSdDb": descriptors.as_list(level.std(axis=0, ddof=1), 2),
            "t30Ratio": descriptors.as_list(np.exp(decay.mean(axis=0)), 3),
            "t30RatioSd": descriptors.as_list(np.exp(decay.mean(axis=0)) * decay.std(axis=0, ddof=1), 3),
            "onsetShiftSeconds": {f"d{d:g}_s{s:g}": round(float(v), 4) for (d, s), v in zip(impulse_cases(macro), onset)},
            "echoDensityTimeRatio": {f"d{d:g}_s{s:g}": round(float(np.exp(v)), 3) for (d, s), v in zip(impulse_cases(macro), density)}}


def laws(cases: dict) -> dict:
    result = {}
    for macro in MACROS:
        result[f"macro{macro}"] = {"decay": decay_law(cases, macro), "level": level_law(cases, macro),
                                   "echoDensity": echo_density_law(cases, macro), "spread": spread_summary(cases, macro)}
        if macro:
            result[f"macro{macro}"]["againstMacro0"] = macro_effect(cases, macro)
    return result


# ----------------------------------------------------------------------------------------------
# The reference scored against itself


def score(pairs: list) -> dict:
    """Pool `descriptors.compare` over (candidate rows, target rows) pairs.

    Per descriptor, over all pairs, groups and elements: the rms difference in
    the descriptor's own unit, the rms and the 95th percentile of |inSpreads|,
    and the rms of inErrors.
    """
    collected = {}
    for candidate, target in pairs:
        ours, theirs = descriptors.summarise_groups(candidate), descriptors.summarise_groups(target)
        for label in theirs:
            for name, entry in descriptors.compare(ours[label], theirs[label]).items():
                finite = np.isfinite(entry["inSpreads"]) & np.isfinite(entry["inErrors"])
                for field in ("difference", "inSpreads", "inErrors"):
                    collected.setdefault(name, {}).setdefault(field, []).append(entry[field][finite])
    result = {}
    for name, fields in collected.items():
        difference, spreads, errors = (np.concatenate(fields[field]) for field in ("difference", "inSpreads", "inErrors"))
        result[name] = {"rmsDifference": round(rms(difference), 4), "rmsInSpreads": round(rms(spreads), 3),
                        "p95InSpreads": round(float(np.percentile(np.abs(spreads), 95.0)), 2), "rmsInErrors": round(rms(errors), 3)}
    return result


def band_level_split(candidate: list, target: list) -> dict:
    """Rms inSpreads of the band levels of one row set against another, below 1 kHz and from 1 kHz up."""
    ours, theirs = descriptors.summarise_groups(candidate), descriptors.summarise_groups(target)
    scores = np.stack([descriptors.compare(ours[label], theirs[label])["bandLevel"]["inSpreads"] for label in "LRM"])
    return {"below1kHz": round(rms(scores[:, :12]), 2), "from1kHz": round(rms(scores[:, 12:]), 2)}


def shifted_rows(case: Case) -> list:
    """Descriptor rows of an impulse case rendered with every impulse SHIFT_SECONDS later: another draw at Macro 0."""
    shift = int(round(SHIFT_SECONDS * SAMPLE_RATE))
    specification = dict(case.specification, frames=case.specification["frames"] + shift,
                         events=[[sample + shift, channel] for sample, channel in case.specification["events"]])
    capture = revocean.capture(descriptors.build_stimulus(specification), case.settings())
    return descriptors.case_descriptors([capture.output], specification, SAMPLE_RATE, LATENCY)


def self_scores(cases: list, measured: list) -> dict:
    """What a perfect model scores, and what a model without the Macro scores.

    `macro0`: Decay 0.5, 1, 2 and 8 s at Size 100 % rendered again with every
    impulse 0.37 s later (0.22 of a period of the 0.6 Hz modulation), scored
    against the targets; `macro0ByDecay` gives each of the four separately
    and `macro0BandLevelByDecay` splits the band level score at 1 kHz.
    `macro50`: realisation 0 against realisation 1.
    `macro100`: realisation 0 against the other two, for the cases with three.
    These are the scores of another draw of the reference itself.
    `macro0AgainstMacro100` and `macro50AgainstMacro100` score the reference at
    the lower Macro value, same stimuli, against the Macro 100 % rows: the
    reading of a model that leaves the Macro out or halves it.
    """
    rows = {case.name: parts for case, (_, parts) in zip(cases, measured)}
    by_macro = {macro: [case for case in cases if case.macro == macro] for macro in MACROS}

    def pooled(name: str) -> list:
        return [row for part in rows[name] for row in part]

    second = {case.decay: (shifted_rows(case), pooled(case.name)) for case in by_macro[0]
              if case.specification["kind"] == "impulses" and case.size == 100.0 and case.decay in SECOND_DRAW_DECAYS}
    result = {"macro0": score(list(second.values())), "macro0ByDecay": {f"{decay:g}": score([pair]) for decay, pair in second.items()},
              "macro0BandLevelByDecay": {f"{decay:g}": band_level_split(*pair) for decay, pair in second.items()}}
    for macro, wanted in ((50, 2), (100, 3)):
        result[f"macro{macro}"] = score([(rows[case.name][0], [row for part in rows[case.name][1:] for row in part])
                                         for case in by_macro[macro] if case.realisations == wanted])
    for other in (0, 50):
        result[f"macro{other}AgainstMacro100"] = score([(pooled(case.name), pooled(case.name.replace(f"m{other:03d}", "m100")))
                                                        for case in by_macro[other] if case.name.replace(f"m{other:03d}", "m100") in rows])
    return result


# ----------------------------------------------------------------------------------------------
# How well the impulse times of a case represent all impulse times


def phase_coverage() -> dict:
    """Per Decay and input: the resultant length of the impulse times as phases of the 0.6 Hz modulation.

    |mean exp(2 pi i 0.6 t)| over the impulses: 0 when they are spread evenly
    over the modulation cycle, 1 when they all fall on the same phase.
    """
    coverage = {}
    for decay in DECAYS:
        events = impulse_specification(decay)["events"]
        phases = {label: np.array([sample for sample, channel in events if label in ("all", channel)]) * MODULATION_HZ / SAMPLE_RATE
                  for label in ("all", "L", "R", "M")}
        coverage[f"{decay:g}"] = {label: round(float(abs(np.mean(np.exp(2j * np.pi * values)))), 2) for label, values in phases.items()}
    return coverage


def grid_rows(responses: np.ndarray) -> list:
    return [descriptors.response_descriptors(response[LATENCY:], SAMPLE_RATE) for response in responses]


def population_check() -> dict:
    """The Decay 0.5 s, Macro 0 targets against the whole population of impulse times.

    `datasets.grid_responses` holds 980 responses per input on a 100 ms grid of
    impulse times, 0.5 s long. Their descriptors give the population mean and
    standard deviation over all impulse times (`mean`, `sd`; the echo density
    profile is left out because the next impulse arrives after 0.5 s). The 8
    target responses per input, cut to the same 0.5 s, are compared with it:

    * `targetBiasRms`     rms over the elements of
                          (target mean - population mean) / population sd;
                          1 / sqrt(8) = 0.35 for eight independent draws;
    * `targetSpreadRatio` median of target sd / population sd;
    * `periodicShare`     median and largest share of an element's variance
                          that is a function of the phase of the 0.6 Hz
                          modulation at the impulse (three harmonics fitted).
    """
    case = next(case for case in plan() if case.name == case_name(0, 0.5, 100.0))
    capture = revocean.capture(descriptors.build_stimulus(case.specification), case.settings())
    window = datasets.GRID_SPACING - LATENCY
    targets = descriptors.split_responses(capture.output, case.specification, LATENCY)
    result = {}
    with ProcessPoolExecutor(WORKERS) as pool:
        for channel, label in enumerate("LR"):
            times, responses = datasets.grid_responses(channel, datasets.GRID_SPACING)
            rows = [row for part in pool.map(grid_rows, np.array_split(responses, 4 * WORKERS)) for row in part]
            population = descriptors.summarise(rows)
            ours = descriptors.summarise([descriptors.response_descriptors(response[:window], SAMPLE_RATE)
                                          for channel_label, response in targets if channel_label == label])
            angle = 2.0 * np.pi * MODULATION_HZ * times / SAMPLE_RATE
            design = np.stack([np.ones_like(angle)] + [function(order * angle) for order in (1, 2, 3) for function in (np.cos, np.sin)], axis=1)
            result[label] = {"n": len(rows)}
            for name, entry in population.items():
                if name == "echoDensity":
                    continue
                values = np.stack([row[name] for row in rows])
                usable = np.all(np.isfinite(values), axis=0)
                residual = values[:, usable] - design @ np.linalg.lstsq(design, values[:, usable], rcond=None)[0]
                share = 1.0 - residual.var(axis=0) / np.maximum(values[:, usable].var(axis=0), descriptors.TINY)
                bias = (ours[name]["mean"] - entry["mean"]) / entry["sd"]
                result[label][name] = {
                    "mean": descriptors.as_list(entry["mean"]), "sd": descriptors.as_list(entry["sd"]),
                    "targetBiasRms": round(rms(bias), 3), "targetSpreadRatio": round(float(np.nanmedian(ours[name]["sd"] / entry["sd"])), 3),
                    "periodicShare": {"median": round(float(np.median(share)), 3), "largest": round(float(np.max(share)), 3)}}
    return result


def main() -> None:
    revocean.identity()
    cases = plan()
    with ProcessPoolExecutor(WORKERS) as pool:
        measured = list(pool.map(measure, cases))
    entries = {case.name: entry for case, (entry, _) in zip(cases, measured)}
    document = {
        "schema": 1,
        "reference": "Arturia Rev OCEAN 1.0.0.5848, Tide, neutral baseline, Mix 100 %, 48 kHz, block 512, warm-up 10 s",
        "sampleRate": SAMPLE_RATE, "latency": LATENCY,
        "bandsHz": list(descriptors.NOMINAL_CENTRES), "decayLevelsDb": list(descriptors.DECAY_LEVELS),
        "spectrumHz": descriptors.as_list(descriptors.long_term_spectrum(
            np.zeros((16, 2)), SAMPLE_RATE, descriptors.SPECTRUM_FRACTION, descriptors.SPECTRUM_LOW)[0], 1),
        "modulationRates": {"regionsHz": ["63-250", "315-2000", "2500-16000"], "rateEdgesHz": list(descriptors.MODULATION_RATE_EDGES)},
        "echoDensity": {"stepSeconds": descriptors.ECHO_DENSITY_STEP, "spanSeconds": descriptors.ECHO_DENSITY_SECONDS},
        "units": {"edt, t20, t30, levelTimes, echoDensityTime": "s", "bandLevel*, level*, spectrum, fluctuation*, modulationRates": "dB",
                  "correlation, coherence, echoDensity, timeVariance": "1"},
        "groups": "all = every response (decay, echo density and modulation descriptors only); "
                  "L, R, M = impulse on the left, right or both inputs",
        "cases": entries, "laws": laws(entries), "selfScores": self_scores(cases, measured),
        "impulsePhaseCoverage": phase_coverage(), "populationAtDecay0.5": population_check(),
    }
    TARGETS.parent.mkdir(parents=True, exist_ok=True)
    TARGETS.write_text(json.dumps(document, separators=(",", ":")))
    SCRATCH.mkdir(parents=True, exist_ok=True)
    flat = {}
    for case, (_, rows) in zip(cases, measured):
        flat.update(flat_rows(case.name, rows))
    np.savez_compressed(SCRATCH / "rows.npz", **flat)
    print(f"{len(cases)} cases, {TARGETS.stat().st_size / 1e6:.2f} MB -> {TARGETS}")


if __name__ == "__main__":
    main()
