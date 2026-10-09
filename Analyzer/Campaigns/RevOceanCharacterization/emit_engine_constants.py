#!/usr/bin/env python3
"""Constants and golden vectors for a C++ port of the Tide model (SPEC.md).

    python emit_engine_constants.py            tide_structural_data/engine_constants.json
    python emit_engine_constants.py golden     tide_structural_data/engine_golden.npz and engine_golden.json

engine_constants.json holds every constant and table of the chain of reference_render.py in processing
order, taken from the campaign's data files (network.json, tide_stage.json, tide_model.json,
converters.json) and, where no data file holds them, from the modules that define them (converters.py,
tide_phase.py, reference_render.py). Derived tables (filter coefficients, gains per line, start phases,
the feedback matrix) are computed here with the model's own functions, so a generated header needs no
arithmetic of its own. Its "schema" entry describes the layout; SPEC.md gives the meaning and the status
(measured, fitted, unknown) of every number.

engine_golden.npz holds short excerpts of reference captures with the same excerpts rendered by
reference_render.py, vectors of the outer laws and of the two converters; engine_golden.json describes each
(stimulus, exact settings, time base). The captures come from `revocean.capture`: cached, or rendered on
the first run. Above Macro 0 the phase of each output is fitted to the capture as score_tide.py fits it
and stored with the vector. No locked holdout is read.
"""
from __future__ import annotations

import hashlib
import json
import math
import sys
from pathlib import Path

import numpy as np

import converters
import network_model
import reference_render
import tide_model
import tide_phase
import tide_stage

HERE = Path(__file__).resolve().parent
DATA = HERE / "tide_structural_data"
CONSTANTS = DATA / "engine_constants.json"
GOLDEN = DATA / "engine_golden.npz"
GOLDEN_INDEX = DATA / "engine_golden.json"
SOURCES = ("network.json", "tide_stage.json", "tide_model.json", "converters.json")
STANDARD_RATES = (44100, 48000, 88200, 96000, 176400, 192000)
SCHEMA_VERSION = 1


def single(value) -> float:
    """A single-precision number as the double that equals it."""
    return float(np.float32(value))


def digest(array: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()


# ---------------------------------------------------------------- constants

def filter_section(entry: dict) -> dict:
    numerator, denominator = network_model.section(entry)
    return {**entry, "b": [float(v) for v in numerator], "a": [float(v) for v in denominator]}


def one_pole(corner_hz: float) -> dict:
    k = tide_stage.one_pole(corner_hz)
    return {"cornerHz": corner_hz, "k": k, "highPass": {"gain": 1.0 / (1.0 + k), "pole": (1.0 - k) / (1.0 + k)},
            "lowPass": {"gain": k / (1.0 + k), "pole": (1.0 - k) / (1.0 + k)}}


def converter_rate(rate: int) -> dict:
    """The constants of one host rate and digests of its two branch tables (`converters._branches`)."""
    L, M = converters.lattice(rate)
    described = converters.describe(rate)
    entry = {"latticeStepsPerInternalSample": L, "latticeStepsPerHostSample": M, "reportedLatencySamples": described["reportedLatencySamples"]}
    if L == M:
        entry["converters"] = False
        entry["outputDelayInternalSamples"] = converters.output_delay(rate)
        entry["engineOutputDelayInternalSamples"] = converters.output_delay(rate) - described["reportedLatencySamples"]
        return entry
    signs = converters.clock_signs(rate)
    entry.update({
        "converters": True,
        "inputLookaheadHostSamples": described["inputLookaheadHostSamples"],
        "outputBlocks": described["outputBlocks"],
        "inputDelayLatticeSteps": described["inputDelayLatticeSteps"],
        "outputDelayLatticeSteps": described["outputDelayLatticeSteps"],
        "engineOutputDelayLatticeSteps": described["outputDelayLatticeSteps"] - described["reportedLatencySamples"] * M,
        "inputGain": described["inputGain"], "outputGain": described["outputGain"],
        "clockSigns": {"input": signs[0], "output": signs[1],
                       "source": "measured" if rate in converters.MEASURED_CLOCK_SIGNS else "simulated (inferred)"},
    })
    for name, source, target, sign in (("inputBranches", M, L, signs[0]), ("outputBranches", L, M, signs[1])):
        branches = converters._branches(source, target, sign)
        coefficients = np.concatenate([row for _, row in branches])
        offsets = np.concatenate([row for row, _ in branches])
        entry[name] = {"count": len(branches), "tapsPerBranch": [min(len(row) for _, row in branches), max(len(row) for _, row in branches)],
                       "firstOffset": int(offsets.min()), "lastOffset": int(offsets.max()),
                       "coefficientSum": float(coefficients.sum()), "coefficientAbsoluteSum": float(np.abs(coefficients).sum()),
                       "coefficientsSha256": digest(coefficients)}
    return entry


def build() -> dict:
    network = network_model.constants()
    stage = json.loads((DATA / "tide_stage.json").read_text())["constants"]
    tide = json.loads((DATA / "tide_model.json").read_text())["model"]
    kernel = json.loads((DATA / "converters.json").read_text())["kernel"]
    rate = network["internal_rate_hz"]
    table = converters.kernel_table()
    own, cross = network_model.input_gains(network)
    oscillator = network["oscillator"]
    start_phase = np.float32(oscillator["phase_step_rad"]) * np.arange(2 * network_model.LINES, dtype=np.float32)
    tap = network["output"]["tap"]
    comb, voice = stage["comb"], stage["voice"]
    matrix = network_model.feedback_matrix(network)
    assert kernel["entriesPerCrossing"] == converters.ENTRIES and kernel["zeroCrossings"] == converters.ZERO_CROSSINGS
    assert comb["originSamples"] == tide_stage.COMB_ORIGIN and voice["qAt1kHz"] == tide_stage.Q_1K
    assert tide["phaseRateCyclesPerSecond"]["atMacro100"] == tide_model.RATE_HIGH
    return {
        "schema": {
            "version": SCHEMA_VERSION,
            "generatedBy": "emit_engine_constants.py",
            "sources": {name: hashlib.sha256((DATA / name).read_bytes()).hexdigest() for name in SOURCES},
            "note": [
                "Sections follow the processing order of SPEC.md: timing and converters, preDelay, equaliser, comb, lines "
                "(input gains, oscillators, attenuation), feedback, taps, voices, phase, outer.",
                "Numbers are double precision. A key ending in Single holds a single-precision number as the double that "
                "equals it; the engine must hold it as a float.",
                "Filter sections give the audio-EQ-cookbook parameters and the coefficients at 44.1 kHz: "
                "y[n] = b[0] x[n] + b[1] x[n-1] + b[2] x[n-2] - a[0] y[n-1] - a[1] y[n-2].",
                "Arrays over the lines have 16 entries, line 1 first; arrays [2][16] are [group][line], group 0 = left output.",
                "converters.rates holds the six standard host rates; any other rate follows converters.rule (SPEC.md, section 3).",
                "referenceKnobs holds the reference's own parameter laws; only a scorer that sets the reference's knobs needs them.",
            ],
        },
        "internalRateHz": rate,
        "timing": {
            "reportedLatencyRule": "4 * floor(11 * hostRate / 44100 + 0.5) host samples",
            "reportedLatencyInternalSamples": converters.LATENCY_INTERNAL,
            "lineWriteDelaySamples": network["input"]["delay_samples"],
            "voiceBlockSamples": stage["voiceBlockSamples"],
        },
        "converters": {
            "rule": {
                "cutoff": kernel["cutoff"], "zeroCrossings": kernel["zeroCrossings"], "kaiserBeta": kernel["kaiserBeta"],
                "entriesPerCrossing": kernel["entriesPerCrossing"], "tableEntries": converters.WING,
                "entriesDroppedOnTheWingAfter": converters.DROPPED_AFTER, "lookaheadMarginSamples": converters.LOOKAHEAD_MARGIN,
                "clockDrift": converters.DRIFT,
                "table": "fl32(cutoff * sinc(cutoff * i / 4096) * I0(beta * sqrt(1 - (i / 69631)^2)) / I0(beta)), i = 0 .. 69631",
            },
            "table": {"entries": len(table), "sum": float(table.sum()), "float32LittleEndianSha256": digest(table.astype("<f4")),
                      "samples": {str(index): float(table[index]) for index in (0, 1, 2, 4095, 4096, 8191, 34816, 65536, 69630, 69631)}},
            "measuredClockSigns": {str(key): list(value) for key, value in converters.MEASURED_CLOCK_SIGNS.items()},
            "rates": {str(host): converter_rate(host) for host in STANDARD_RATES},
        },
        "preDelay": {"law": "max(0, floor(milliseconds * hostRate / 1000) - 1) whole host samples in front of the input converter"},
        "equaliser": {"place": "per input channel, behind the input converter and in front of the comb",
                      "sections": [filter_section(entry) for entry in network["input"]["equaliser"]]},
        "comb": {
            "delayMidSamples": comb["delayMidSamples"], "delaySwingSamples": comb["delaySwingSamples"],
            "delayMidSingle": single(comb["delayMidSamples"]), "delaySwingSingle": single(comb["delaySwingSamples"]),
            "periodSamples": comb["periodSeconds"] * rate, "originSamples": comb["originSamples"],
            "rightInputLeadPeriods": comb["rightInputLeadPeriods"], "feedback": comb["feedback"],
            "highPass": one_pole(comb["highPassHz"]), "lowPass": one_pole(comb["lowPassHz"]),
            "mix": "undelayed path cos(pi/2 * macro), delayed path sin(pi/2 * macro)",
        },
        "lines": {
            "prime": network["lines"]["prime"],
            "widthWeight": [float(v) for v in network_model.width_weights(network)],
            "inputGainFirst": network["input"]["gain"]["first"],
            "inputGainLastOverFirst": network["input"]["gain"]["last_over_first"], "inputGainShape": network["input"]["gain"]["shape"],
            "inputGain": [float(v) for v in network["input"]["gain"]["first"] * network_model.input_curve(network)],
            "ownGain": [float(v) for v in own], "crossGain": [float(v) for v in cross],
            "length": "floorf(fl32(prime * sizeScale) + 0.5f), single precision",
            "attenuation": "10^(-3 * prime * sizeScale / (44100 * decaySeconds)), unrounded length",
        },
        "oscillator": {
            "rateHz": oscillator["rate_hz"], "depthMs": oscillator["depth_ms"],
            "depthSamplesSingle": float(network_model.modulation_depth(network)),
            "incrementSingle": single(2.0 * math.pi * oscillator["rate_hz"] / rate),
            "twoPiSingle": single(2.0 * math.pi), "halfPiSingle": single(0.5 * math.pi), "halfPi": 0.5 * math.pi,
            "phaseStepRad": oscillator["phase_step_rad"],
            "startPhaseSingle": [[float(start_phase[2 * line + group]) for line in range(network_model.LINES)] for group in (0, 1)],
        },
        "feedback": {
            "scale": network["feedback"]["scale"],
            "matrixSign": [[int(np.sign(v)) for v in row] for row in matrix],
            "matrix": "matrixSign[line written][line read] = (-1)^popcount((15 - written) & read), lines counted from 0",
            "kernel": [filter_section(entry) for entry in network["feedback"]["kernel"]],
        },
        "taps": {
            "decayLowSeconds": tap["decay_low_seconds"], "decayKneeSeconds": tap["decay_knee_seconds"],
            "first": tap["first"], "last": tap["last"], "lastDecayShape": tap["last_decay_shape"],
            "earlyShape": tap["early_shape"], "lateShape": tap["late_shape"],
            "weightAtHalfSecond": [float(v) for v in network_model.tap_weights(network, 0.5)],
            "weightFromSixSeconds": [float(v) for v in network_model.tap_weights(network, 6.0)],
        },
        "voices": {
            "lines": tide["voiceLines"], "voiceBOffsetCycles": tide["voiceBOffsetCycles"],
            "amountPerMacro": voice["amountPerMacro"], "fullDepthAmount": voice["fullDepthAmount"], "curveBend": voice["curveBend"],
            "cutoffTopHz": voice["cutoffTopHz"], "cutoffEndHz": voice["cutoffEndHz"], "gainTop": voice["gainTop"], "restQ": voice["restQ"],
            "qKnotHz": voice["qKnotHz"], "qAt0Hz": voice["qAt0Hz"], "qAt1kHz": voice["qAt1kHz"],
            "qAt3kHzMacro0And1": voice["qAt3kHzMacro0And1"], "qPlateauMacro0And1": voice["qPlateauMacro0And1"],
            "qSmoothingBlocks": voice["qSmoothingBlocks"], "qSmoothingCoefficient": 1.0 - math.exp(-1.0 / voice["qSmoothingBlocks"]),
            "restLowPass": filter_section(network["output"]["low_pass"]),
        },
        "phase": {
            "rateAtMacro0": tide["phaseRateCyclesPerSecond"]["atMacro0"], "rateAtMacro100": tide["phaseRateCyclesPerSecond"]["atMacro100"],
            "rateShape": tide["phaseRateCyclesPerSecond"]["expShape"],
            "gridHz": list(tide_phase.GRID_HZ), "levelLow": list(tide_phase.LEVEL_LOW), "levelHigh": list(tide_phase.LEVEL_HIGH),
            "startSameHalfProbability": tide_phase.START_SAME_HALF,
        },
        "outer": {
            "width": "mid gain sqrt(2 / (1 + s)), side gain s * sqrt(2 / (1 + s)), s = widthScale",
            "mix": "dry gain min(1, 2 * (1 - mix)), wet gain min(1, 2 * mix)",
            "clipThreshold": reference_render.CLIP_THRESHOLD, "clipCeiling": reference_render.CLIP_CEILING,
            "levelStage": {"kneeDb": reference_render.LEVEL_KNEE_DB, "slope": reference_render.LEVEL_SLOPE, "thresholdDbfs": 0.0,
                           "attackSeconds": reference_render.LEVEL_ATTACK_SECONDS, "releaseSeconds": reference_render.LEVEL_RELEASE_SECONDS,
                           "firstInputPeakThatReducesTheWet": 10.0 ** (-reference_render.LEVEL_KNEE_DB / 40.0)},
        },
        "referenceKnobs": {
            "decaySeconds": {**network["parameters"]["decay"], "law": "low + (high - low) * (expf(shape * x) - 1) / (expf(shape) - 1), single precision"},
            "sizePercent": {**network["parameters"]["size"],
                            "law": "sizeScale = fma(1.7f, r, 0.3f), r = fl32((expf(fl32(shape * x)) - 1) * fl32(1 / (expf(shape) - 1)))"},
            "preDelayMilliseconds": {**reference_render.PREDELAY_LAW, "law": "high * (expf(shape * x) - 1) / (expf(shape) - 1), single precision"},
            "width": "widthScale = 2 x; Width in percent = 200 * (1 - 4^-x)",
            "macro": "x", "mix": "x",
        },
    }


def emit_constants() -> None:
    CONSTANTS.write_text(json.dumps(build(), indent=1) + "\n")
    print(f"{CONSTANTS.name}: {CONSTANTS.stat().st_size} bytes")


# ---------------------------------------------------------------- golden vectors

SECONDS_BEFORE = 10.0                      # warm-up of every capture
EXCERPT = 512                              # frames of an excerpt
GOLDEN_SEED = 27182
TIDE_REALISATION = 60

# name: (host rate, display values, stimulus). "impulses": (seconds, channel, amplitude) events in 0.5 s of silence;
# "programme": a burst and two decaying tones of 1024 frames, stored with the vector.
CAPTURE_VECTORS = {
    "impulses_44100": (44100, dict(decay=0.5, size=100.0), "impulses"),
    "impulses_48000": (48000, dict(decay=2.0, size=80.1), "impulses"),
    "impulses_88200": (88200, dict(decay=1.0, size=162.1), "impulses"),
    "impulses_96000": (96000, dict(decay=4.0, size=57.3), "impulses"),
    "impulses_176400": (176400, dict(decay=0.7, size=130.0), "impulses"),
    "impulses_192000": (192000, dict(decay=3.3, size=180.1, predelay=12.3), "impulses"),
    "programme_44100": (44100, dict(decay=1.5, size=100.0, predelay=5.5), "programme"),
    "programme_48000": (48000, dict(decay=0.9, size=118.0), "programme"),
    "tide_44100": (44100, dict(decay=1.2, size=100.0, macro=1.0), "train"),
    "tide_48000": (48000, dict(decay=1.2, size=100.0, macro=1.0), "train"),
}
IMPULSES = ((0.045, 0, 0.5), (0.2, 1, -0.375))                # amplitudes that single precision holds exactly
IMPULSE_SECONDS = 0.5
TRAIN_SECONDS, TRAIN_SPACING = 6.0, 0.37
PROGRAMME_AT, PROGRAMME_FRAMES, PROGRAMME_SECONDS = 1000, 1024, 0.4


def events_of(kind: str, rate: int) -> list:
    """[frame, channel, amplitude] of an impulse stimulus."""
    if kind == "impulses":
        return [[int(round(seconds * rate)), channel, amplitude] for seconds, channel, amplitude in IMPULSES]
    count = int((TRAIN_SECONDS - 1.5) / TRAIN_SPACING)
    return [[int(round((0.05 + TRAIN_SPACING * index) * rate)), index % 2, 0.5] for index in range(count)]


def programme_block(rate: int) -> np.ndarray:
    """A noise burst of 256 frames, then decaying tones of 880 Hz (left) and 1320 Hz (right); peak 0.5."""
    generator = np.random.default_rng(GOLDEN_SEED)
    block = np.zeros((PROGRAMME_FRAMES, 2))
    block[:256] = generator.uniform(-1.0, 1.0, (256, 2))
    time = np.arange(PROGRAMME_FRAMES - 256) / rate
    block[256:, 0] = 0.8 * np.sin(2.0 * np.pi * 880.0 * time) * np.exp(-time / 0.004)
    block[256:, 1] = 0.8 * np.sin(2.0 * np.pi * 1320.0 * time) * np.exp(-time / 0.004)
    return (0.5 * block / np.abs(block).max()).astype(np.float32)


def stimulus_of(kind: str, rate: int) -> np.ndarray:
    import revocean
    if kind == "programme":
        stimulus = revocean.silence(PROGRAMME_SECONDS, rate)
        stimulus[PROGRAMME_AT:PROGRAMME_AT + PROGRAMME_FRAMES] = programme_block(rate)
        return stimulus
    seconds = IMPULSE_SECONDS if kind == "impulses" else TRAIN_SECONDS
    return revocean.impulses(seconds, [(frame, channel, amplitude) for frame, channel, amplitude in events_of(kind, rate)], sample_rate=rate)


def null_db(candidate, reference) -> float:
    difference = float(np.sum((np.asarray(candidate, dtype=np.float64) - np.asarray(reference, dtype=np.float64)) ** 2))
    return round(10.0 * math.log10(max(difference, 1e-300) / max(float(np.sum(np.asarray(reference, dtype=np.float64) ** 2)), 1e-300)), 2)


def fitted_phase(stimulus, reference, rate: int, arguments: dict):
    """The phase of both outputs in one capture, in the generator's form, fitted as fit_tide_model.fitted_case fits it."""
    import fit_tide_model as author
    import score_tide
    inner = {key: arguments[key] for key in ("decay_seconds", "size_scale", "predelay_seconds", "macro")}
    first_read, taps = reference_render.voice_inputs(stimulus, rate, SECONDS_BEFORE, **inner)
    bench = author.Bench(stimulus, reference, rate, SECONDS_BEFORE, arguments["decay_seconds"], 100.0 * float(arguments["size_scale"]),
                         100.0 * arguments["macro"], taps=(first_read, taps))
    fine = bench.spline(author.FINE_STEP, bench.spline(score_tide.PHASE_STEP))
    return author.eases(bench, fine)[0]


def excerpt_starts(kind: str, rate: int, model: np.ndarray, events: list) -> list:
    """Raw frames where the excerpts start: the first arrivals of the first event, and a later stretch of dense response."""
    first = PROGRAMME_AT if kind == "programme" else events[0][0]
    early = model[first:first + int(0.06 * rate)]
    onset = first + int(np.argmax(np.max(np.abs(early), axis=1) > 0.1 * np.abs(early).max()))
    if kind == "train":
        return [events[5][0] + int(0.05 * rate), events[11][0] + int(0.05 * rate)]
    last = PROGRAMME_AT + PROGRAMME_FRAMES if kind == "programme" else events[-1][0]
    return [onset - 64, last + int(0.09 * rate)]


def capture_vector(name: str, arrays: dict) -> dict:
    import revocean
    rate, display, kind = CAPTURE_VECTORS[name]
    settings = {key: (value if key in ("macro", "mix") else revocean.normalised(key, value)) for key, value in display.items()}
    stimulus = stimulus_of(kind, rate)
    realisation = TIDE_REALISATION if kind == "train" else 0
    captured = revocean.capture(stimulus, settings, sample_rate=rate, warmup=SECONDS_BEFORE, realisation=realisation)
    reference = captured.output[:len(stimulus)]
    arguments = reference_render.physical(settings)
    events = [] if kind == "programme" else events_of(kind, rate)
    entry = {
        "kind": "capture", "hostRate": rate, "reportedLatencySamples": captured.latency,
        "warmupFrames": int(round(SECONDS_BEFORE * rate)), "stimulusFrames": len(stimulus),
        "referenceKnobsDisplay": display,
        "referenceKnobsNormalised": {key: revocean.resolve(settings)[key] for key in ("decay", "size", "predelay", "macro")},
        "parameters": {"decaySeconds": arguments["decay_seconds"], "sizeScale": float(arguments["size_scale"]),
                       "preDelaySeconds": arguments["predelay_seconds"], "macro": arguments["macro"]},
        "preDelaySamples": reference_render.predelay_samples(arguments["predelay_seconds"], rate),
    }
    if kind == "programme":
        arrays[f"{name}/stimulus"] = programme_block(rate)
        entry["stimulus"] = {"array": f"{name}/stimulus", "firstFrame": PROGRAMME_AT, "note": "silence elsewhere"}
    else:
        entry["stimulus"] = {"impulses": events, "note": "[frame, channel, amplitude]; silence elsewhere"}
    phase = None
    if arguments["macro"] > 0.0:
        import score_tide
        curve = fitted_phase(stimulus, reference.astype(np.float64), rate, arguments).as_json()
        phase = score_tide.curve_from_json(curve)                # the curve as the index gives it
        entry["phase"] = {"rateCyclesPerSecond": curve["rate"], "knotSeconds": curve["times"], "knotCycles": curve["values"],
                          "realisation": realisation,
                          "note": "voice A of each output: rate * t + level(t), t in seconds since the first processed frame; the level is "
                                  "knotCycles[0] before knotSeconds[0], moves from knot to knot along (1 - cos(pi x)) / 2 and stays at the last "
                                  "value; voice B is half a cycle later"}
    model = reference_render.render(stimulus, rate, SECONDS_BEFORE, **arguments, phase=phase)
    entry["modelAgainstReferenceDb"] = null_db(model, reference)
    entry["excerpts"] = []
    for index, start in enumerate(excerpt_starts(kind, rate, model, events)):
        chosen = slice(start, start + EXCERPT)
        arrays[f"{name}/reference{index}"] = reference[chosen].astype(np.float32)
        arrays[f"{name}/model{index}"] = model[chosen]
        entry["excerpts"].append({"rawFirstFrame": start, "frames": EXCERPT, "engineFirstFrame": start - captured.latency,
                                  "reference": f"{name}/reference{index}", "model": f"{name}/model{index}",
                                  "modelAgainstReferenceDb": null_db(model[chosen], reference[chosen])})
    return entry


def law_vectors(arrays: dict) -> dict:
    """The outer laws on small inputs; model only, they are measured to -136 dB or better (SPEC.md, section 14)."""
    values = np.concatenate([np.linspace(-6.0, 6.0, 49), [2.5, 2.52, 3.0, 5.4, 5.45, 5.5, -2.52, -5.45]])
    arrays["clip/input"], arrays["clip/output"] = values, reference_render.output_clip(values)
    scales = np.array([0.0, 0.25, 0.5353317260742188, 1.0, 1.5, 2.0])
    probe = np.array([[1.0, 0.0], [0.0, 1.0], [0.3, -0.7]])
    arrays["width/scale"], arrays["width/input"] = scales, probe
    arrays["width/output"] = np.stack([reference_render.apply_width(probe, float(scale)) for scale in scales])
    mixes = np.array([0.0, 0.125, 0.37, 0.5, 0.625, 0.81, 1.0])
    arrays["mix/mix"], arrays["mix/dryAndWetGain"] = mixes, np.array([reference_render.mix_gains(float(mix)) for mix in mixes])
    rate = 48000
    key = np.zeros((2048, 2), np.float32)
    key[50:600, 0], key[800:1100, 1], key[1200:1206, 0], key[1400:1800] = 2.0, -0.8, 6.0, 1.2
    arrays["levelStage/input"] = key
    arrays["levelStage/reductionDb"] = reference_render.level_reduction_db(key, rate).astype(np.float32)
    return {
        "clip": {"kind": "law", "input": "clip/input", "output": "clip/output"},
        "width": {"kind": "law", "widthScale": "width/scale", "input": "width/input", "output": "width/output",
                  "note": "output[scale][frame][channel] of the frames in input"},
        "mix": {"kind": "law", "mix": "mix/mix", "dryAndWetGain": "mix/dryAndWetGain"},
        "levelStage": {"kind": "law", "hostRate": rate, "input": "levelStage/input", "reductionDb": "levelStage/reductionDb",
                       "note": "reduction in dB after each input frame, single precision, state zero at the first frame; "
                               "the wet of the same engine frame is multiplied by 10^(reductionDb / 20)"},
    }


def converter_vectors(arrays: dict) -> dict:
    """Both converters on a unit impulse and on noise at every standard rate with converters (model only: the converters
    are measured between captures at -128 dB or better, findings/converters_verification.md)."""
    generator = np.random.default_rng(GOLDEN_SEED + 1)
    host_noise, internal_noise = generator.uniform(-1.0, 1.0, 96), generator.uniform(-1.0, 1.0, 96)
    arrays["converters/hostNoise"], arrays["converters/internalNoise"] = host_noise, internal_noise
    entry = {"kind": "converters", "hostNoise": "converters/hostNoise", "internalNoise": "converters/internalNoise",
             "note": "frames count from the first processed frame; a signal is zero outside its array. toInternal: host frames "
                     "hostFirstFrame .. give the internal samples internalFirstSample ..; toHost: internal samples "
                     "internalFirstSample .. give the raw host frames hostFirstFrame .. (the engine's frame is that minus the reported latency)",
             "rates": {}}
    for rate in STANDARD_RATES[1:]:
        origin, first_internal = 1000, 900
        L, M = converters.lattice(rate)
        reach = converters.ZERO_CROSSINGS + 1
        first, internal = converters.to_internal(host_noise, rate, origin)
        start = (L * (first_internal - reach) + converters.output_delay(rate)) // M
        stop = (L * (first_internal + len(internal_noise) + reach) + converters.output_delay(rate)) // M + 1
        host = converters.to_host(internal_noise, rate, first_internal, start, stop)
        arrays[f"converters/{rate}/toInternal"], arrays[f"converters/{rate}/toHost"] = internal, host
        entry["rates"][str(rate)] = {
            "toInternal": {"hostFirstFrame": origin, "internalFirstSample": int(first), "output": f"converters/{rate}/toInternal"},
            "toHost": {"internalFirstSample": first_internal, "hostFirstFrame": int(start), "output": f"converters/{rate}/toHost"}}
    return entry


def emit_golden() -> None:
    arrays, vectors = {}, {}
    for name in CAPTURE_VECTORS:
        vectors[name] = capture_vector(name, arrays)
        print(name, vectors[name]["modelAgainstReferenceDb"], [excerpt["modelAgainstReferenceDb"] for excerpt in vectors[name]["excerpts"]], flush=True)
    vectors.update(law_vectors(arrays))
    vectors["converters"] = converter_vectors(arrays)
    np.savez_compressed(GOLDEN, **arrays)
    index = {
        "schema": {
            "version": SCHEMA_VERSION, "generatedBy": "emit_engine_constants.py golden", "arrays": GOLDEN.name,
            "note": [
                "capture vectors: a fresh instance, `warmupFrames` of silence, then the stimulus (single-precision samples), with "
                "`parameters` set from the start (Width 100 %, Mix 100 %, level stage idle: every input sample is at or below 0.5).",
                "`reference` is the excerpt of the reference's raw output (float32), `model` the same excerpt from reference_render.py "
                "(float64). Raw frame N counts from the first stimulus frame and holds the reported latency.",
                "An engine with the contract of EbbEngine.h returns raw frame N as its wet output for input frame N minus the reported "
                "latency (`engineFirstFrame` of an excerpt).",
                "`modelAgainstReferenceDb` is 20 log10(rms(model - reference) / rms(reference)); a port should meet the model far "
                "more closely than the model meets the reference.",
                "Vectors above Macro 0 belong to one realisation of the reference; they need its phase, given under `phase`.",
            ],
        },
        "vectors": vectors,
    }
    GOLDEN_INDEX.write_text(json.dumps(index, indent=1) + "\n")
    print(f"{GOLDEN.name}: {GOLDEN.stat().st_size} bytes, {len(arrays)} arrays; {GOLDEN_INDEX.name}: {GOLDEN_INDEX.stat().st_size} bytes")


if __name__ == "__main__":
    if sys.argv[1:] == ["golden"]:
        emit_golden()
    elif not sys.argv[1:]:
        emit_constants()
    else:
        raise SystemExit(__doc__)
