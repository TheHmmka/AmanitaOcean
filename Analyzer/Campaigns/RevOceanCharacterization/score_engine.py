#!/usr/bin/env python3
"""Score the C++ Fathom engine (Source/dsp/FathomEngine.h) against the reference and against the campaign's model.

    python score_engine.py                 every part
    python score_engine.py holdout fresh   only the parts named: holdout, fresh, shell, clocks, model, tide, fresh_tide,
                                           seeded, statistics, level, plugin, plugin_statistics
    python score_engine.py --tool <path>   another build of AmanitaOceanFathomRender (default: build-ebb)

The engine is rendered by Tools/FathomRender.cpp (target AmanitaOceanFathomRender) from a WAV file. It returns the
reference's wet output a reported latency early, so every render is moved back by that latency before it is
compared: the scores are in the raw time base of `revocean.capture`. A null is
20 log10(|candidate - reference| / |reference|) with no gain, delay or polarity fitted, overall and in the
stretches of score_network.py.

    holdout   score_network.py's locked holdout, through its own `score`. The warm-up is rendered twice, as
              processed silence and through advanceIdle(); the two outputs must be the same bits. So must a
              third render with the Tide layer kept in the circuit at Macro 0 by a prescribed phase.
    fresh     Macro 0 captures the engine was not built on: new ones (programme seed 27182) at other Decay,
              Size and warm-up values at 44.1, 48, 88.2 and 96 kHz, and the fifteen of the specification's
              proof (seed 31415). Engine and model against the reference, engine against the model.
    shell     the outer laws: captures with Pre-delay, Width and Mix moved, quiet and with an input that
              drives the level stage and the clipper; the engine's wet passes its own LevelStage, applyWidth,
              mixGains and clip. A loud case is also rendered without each of the two stages.
    clocks    host rates beside the standard ones: captures at 64 and 384 kHz, whose clock signs joined the
              campaign's measured table on the strength of these captures and equal those of its simulation
              (converters.clock_signs), and the engine against the model at 128 kHz, where both take the
              simulated signs.
    model     engine against reference_render.py where no capture is taken: the other host rates (22.05 to
              384 kHz), the ends of the parameter ranges, Size below the reference's range, the pre-delay
              law at knob positions on whole numbers of frames, and the outer laws at 176.4 and 192 kHz.

Above Macro 0 the reference differs from instance to instance in the phase of its voices, and so does the
engine from seed to seed. Three parts give the engine the phase of a capture through its test hook
(FathomEngine::setVoicePhaseForTesting; `--voice-phase` of the tool), sampled where the engine sets its voices:

    tide        score_tide.py's phase-fitted null on its fifteen cases at Macro 100, 50 and 25 %, by its own
                procedure with the engine as the render function: the curve the campaign found in each capture,
                then steps of the engine's own fit (its weights end at FIT_SILENCE, see there). Beside it
                reference_render.py with the same final phase against the reference, and the engine against
                reference_render.py with that phase, which shows the port alone.
    fresh_tide  the same on the eleven captures of the specification's proof (seed 31415) with the curves it
                stored: Macro 100 % at 44.1, 48, 88.2 and 96 kHz, one of them with Pre-delay, Width and Mix
                moved and the outer laws applied; Macro 50 and 25 %.
    seeded      the engine running free from a seed against reference_render.py driven by `EnginePhase`, this
                file's Python form of the engine's phase generator: the port and the generator together, at
                host rates from 22.05 to 192 kHz and Macro from 3 to 100 %.

Two parts let the engine run free, as a listener hears it, through score_tide.py with this file as candidate:

    statistics  descriptors.compare_to_targets for several seeds at Macro 100 and 50 %, in units of the
                reference's own spread, beside the campaign model's stored figures.
    level       the level cycle under steady noise at Macro 100 %, beside the campaign model's stored figures.

Two parts score the built plug-in itself, rendered by render_candidate.py through the Analyzer that captures
the reference, with the sixth Character selected and Ocean's own controls neutral. Every render is a new
instance of the plug-in, which draws a voice seed of its own that no renderer knows. At Macro 0, where no
voice moves, the plug-in must equal the engine's renderer to the bit; above it the plug-in is an ensemble of
realisations, as the reference is.

    plugin             Macro 0. The locked holdout through score_network.py with the plug-in's knobs at the
                       positions nearest to what the reference works with; captures of its own (programme seed
                       16180) at knob positions where plug-in and reference work with the same single-precision
                       numbers, at 44.1, 48, 88.2 and 96 kHz, quiet and loud, with Pre-delay and Width moved,
                       all at Mix 100 %: nulls against the reference. Below Mix 100 % the plug-in's Mix is
                       Ocean's own linear law and not the reference's: there the shares of dry and wet are
                       read from its output. Every one of these renders is also made by the renderer from the
                       numbers `render_candidate.physical` gives (`--ocean-mix` for the Mix); the two must be
                       the same bits. Above Macro 0 a setting is rendered twice and the two must differ.
    plugin_statistics  descriptors.compare_to_targets at Macro 100 %, Mix 100 % for PLUGIN_INSTANCES renders of
                       every target case, each a new instance, against the reference's realisations in units
                       of the reference's own spread, beside the engine's figures of `statistics` where they
                       are stored. With it, how far the instances of a case lie apart, and one render of the
                       case at Evolution 0 against the renderer, to the bit.

Captures come from `revocean.capture` (cached; a first run renders them). Nothing but the phase is fitted
here. The report is written to Analyzer/Results/RevOceanCharacterization/work/engine/score_engine.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.io import wavfile

import converters
import datasets
import descriptors
import reference_render
import render_candidate
import revocean
import score_network
import score_tide
import tide_model

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = revocean.RESULTS / "work" / "engine"
TOOL = Path(os.environ.get("AMANITA_FATHOM_RENDER", ROOT / "build-ebb" / "AmanitaOceanFathomRender"))
STRETCHES = score_network.STRETCHES
FRESH_SEED = 27182
PROOF_SEED = 31415                     # the captures of work/specification/proof.py
WORKERS = 4
CONSTANTS = HERE / "tide_structural_data" / "engine_constants.json"
MODEL_SCORES = HERE / "tide_structural_data" / "tide_model.json"
PROOF_TIDE = revocean.RESULTS / "work" / "specification" / "proof_tide.json"
DEFAULT_VOICE_SEED = 0x45626245        # FathomEngine::defaultVoiceSeed
REFIT_STEPS = 2                        # steps of the engine's own phase fit after a stored curve, as score_tide.py takes
# An rms below this is left out of the engine's phase fit. The engine's lines hold single-precision samples and
# nothing under 1e-30, so it follows a response down to about -500 dB re full scale and no further; score_tide.py
# weights a fit down to 1e-30, where the engine's last second before silence would steer the phase. The nulls
# are not weighted and cover the whole response.
FIT_SILENCE = 1e-24
STATISTICS_SEEDS = 8
LEVEL_SEEDS = 40                       # as many as the campaign model's stored level cycle
PLUGIN_SEED = 16180                    # programme seed of the captures the plug-in is scored on
PLUGIN_INSTANCES = 8                   # renders of the plug-in per target case, each a new instance with its own voice seed
CLOCK_SEED = 60228                     # programme seed of the captures at 64 and 384 kHz (findings/engine_review.md)

tool = TOOL


# ---------------------------------------------------------------- the engine

def phase_blocks(phase, rate: int, warmup_seconds: float, frames: int) -> np.ndarray:
    """A phase function where the engine sets its voices: rows (left, right) in cycles for every block of 44
    internal samples from the first processed frame to the end of a render of `frames` frames."""
    seconds = (int(round(warmup_seconds * rate)) + frames) / rate
    starts = tide_model.BLOCK * np.arange(int(seconds * tide_model.INTERNAL_RATE / tide_model.BLOCK) + 2)
    return np.ascontiguousarray(np.asarray(phase(starts / tide_model.INTERNAL_RATE), dtype=np.float64).T)


def engine(stimulus, rate: int, warmup_seconds: float, decay_seconds: float, size_scale, predelay_seconds: float = 0.0,
           macro: float = 0.0, *, idle: bool = False, level_stage: bool = False, width_scale=None, mix=None,
           ocean_mix=None, clip: bool = False, phase=None, seed=None) -> np.ndarray:
    """The engine's output for a stimulus, (frames, 2) single precision, in the engine's own time base: frame n
    answers stimulus frame n. Without an outer law it is the wet signal. `mix` is the reference's Mix law,
    `ocean_mix` Ocean's own, which the plug-in applies. `seed` is the engine's voice seed; `phase`, a function
    of the seconds since the first processed frame that returns the phase of voice A of both outputs in
    cycles, shape (2, len(seconds)), takes the place of the engine's own phase."""
    scratch = WORK / "scratch" / uuid.uuid4().hex
    scratch.mkdir(parents=True)
    try:
        wavfile.write(scratch / "in.wav", int(rate), np.ascontiguousarray(stimulus, dtype=np.float32))
        command = [str(tool), "--input", str(scratch / "in.wav"), "--output", str(scratch / "out.wav"),
                   "--warmup-frames", str(int(round(warmup_seconds * rate))), "--decay", repr(float(decay_seconds)),
                   "--size", repr(float(np.float32(size_scale))), "--predelay", repr(float(predelay_seconds)), "--macro", repr(float(macro))]
        if phase is not None:
            phase_blocks(phase, rate, warmup_seconds, len(stimulus)).astype("<f8").tofile(scratch / "phase.f64")
            command += ["--voice-phase", str(scratch / "phase.f64")]
        command += [] if seed is None else ["--voice-seed", str(int(seed))]
        command += ["--idle-warmup"] if idle else []
        command += ["--level-stage"] if level_stage else []
        command += [] if width_scale is None else ["--width", repr(float(width_scale))]
        command += [] if mix is None else ["--mix", repr(float(mix))]
        command += [] if ocean_mix is None else ["--ocean-mix", repr(float(ocean_mix))]
        command += ["--clip"] if clip else []
        finished = subprocess.run(command, capture_output=True, text=True)
        if finished.returncode != 0:
            raise RuntimeError(f"{tool.name} failed: {finished.stderr[-600:]}")
        read_rate, output = wavfile.read(scratch / "out.wav")
        if read_rate != rate or output.dtype != np.float32 or output.shape != np.shape(stimulus):
            raise RuntimeError("unexpected engine output")
        return output
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def raw(output: np.ndarray, rate: int) -> np.ndarray:
    """An engine render moved back by the reported latency: the reference's raw time base."""
    latency = converters.reported_latency(rate)
    result = np.zeros(output.shape, np.float64)
    result[latency:] = output[:len(output) - latency]
    return result


def engine_predelay_frames(seconds: float, rate: int) -> int:
    """Whole frames of pre-delay as the engine computes them from its single-precision parameter in seconds."""
    milliseconds = np.float32(1000.0 * float(np.float32(seconds)))
    return max(0, int(math.floor(milliseconds * np.float32(rate) / np.float32(1000.0))) - 1)


def engine_knobs(stimulus, rate: int, warmup_seconds: float, settings: dict, *, shell: bool = False, level_stage: bool = True,
                 clip: bool = True, idle: bool = False, phase=None, seed=None) -> np.ndarray:
    """The engine at normalised host parameters of the reference, raw time base. `shell` applies the outer laws."""
    arguments = reference_render.physical(settings)
    inner = {key: arguments[key] for key in ("decay_seconds", "size_scale", "predelay_seconds", "macro")}
    outer = dict(level_stage=level_stage, width_scale=arguments["width_scale"], mix=arguments["mix"], clip=clip) if shell else {}
    return raw(engine(stimulus, rate, warmup_seconds, idle=idle, phase=phase, seed=seed, **inner, **outer), rate)


def render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro_percent, seed, phase=None) -> np.ndarray:
    """The engine behind the interface of score_tide.py: its raw output at the neutral baseline for display
    values of Decay, Size and Macro, from voice seed `seed` or with the prescribed `phase`."""
    settings = knobs(decay=decay_seconds, size=size_percent, macro=macro_percent / 100.0)
    return engine_knobs(stimulus, int(sample_rate), warmup_seconds, settings, phase=phase, seed=seed)


class EnginePhase:
    """The engine's own voice phase for a seed at a fixed Macro, as a function of the seconds since the first
    processed frame: FathomVoicePhase of Source/dsp/FathomNetwork.cpp, written again in Python.

    Per output a level that holds its start value up to knot 1 and then moves from knot to knot along a
    raised cosine, plus a ramp at `tide_model.phase_rate(macro)`. Knot j lies (j + u) cells of the output's
    grid after the first frame. Every random number is number `index` of the SplitMix64 sequence that starts
    at a key: the keys of the two outputs are numbers 0 and 1 of the seed's sequence, numbers 2 and 3 decide
    the halves the two start levels lie in, and numbers 0, 2j - 1 and 2j of an output's sequence give its
    start level, the place of knot j in its cell and the level at knot j (from knot 2 on; knot 1 ends the hold).
    """
    MASK = (1 << 64) - 1

    def __init__(self, seed: int, macro: float):
        law = json.loads(CONSTANTS.read_text())["phase"]
        self.rate, self.grid = tide_model.phase_rate(macro), law["gridHz"]
        self.low, self.span = law["levelLow"], [high - low for low, high in zip(law["levelLow"], law["levelHigh"])]
        seed &= self.MASK
        self.keys = [self.bits(seed, 0), self.bits(seed, 1)]
        right_high = self.unit(seed, 2) < 0.5
        same_half = self.unit(seed, 3) < law["startSameHalfProbability"]
        high = [right_high == same_half, right_high]
        self.start = [self.low[o] + self.span[o] * (0.5 * ((1.0 if high[o] else 0.0) + self.unit(self.keys[o], 0))) for o in (0, 1)]

    @classmethod
    def bits(cls, key: int, index: int) -> int:
        value = (key + (index + 1) * 0x9E3779B97F4A7C15) & cls.MASK
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & cls.MASK
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & cls.MASK
        return value ^ (value >> 31)

    @classmethod
    def unit(cls, key: int, index: int) -> float:
        return (cls.bits(key, index) >> 11) * 2.0 ** -53

    def knot(self, output: int, index: int) -> tuple:
        """(seconds, level) of knot `index`; knot 0 is the start."""
        if index < 1:
            return 0.0, self.start[output]
        level = self.start[output] if index == 1 else self.low[output] + self.span[output] * self.unit(self.keys[output], 2 * index)
        return (index + self.unit(self.keys[output], 2 * index - 1)) / self.grid[output], level

    def level(self, output: int, seconds) -> np.ndarray:
        seconds = np.atleast_1d(np.asarray(seconds, dtype=np.float64))
        count = int(math.floor(float(seconds.max()) * self.grid[output])) + 3
        times, values = (np.array(column) for column in zip(*(self.knot(output, index) for index in range(count))))
        index = np.clip(np.searchsorted(times, seconds, side="right") - 1, 0, count - 2)
        along = np.clip((seconds - times[index]) / (times[index + 1] - times[index]), 0.0, 1.0)
        return values[index] + (values[index + 1] - values[index]) * (0.5 - 0.5 * np.cos(np.pi * along))

    def __call__(self, seconds) -> np.ndarray:
        seconds = np.asarray(seconds, dtype=np.float64)
        return np.stack([self.level(output, seconds) + self.rate * seconds for output in (0, 1)])


# ---------------------------------------------------------------- scores

def null(candidate, reference) -> float:
    candidate, reference = np.asarray(candidate, dtype=np.float64), np.asarray(reference, dtype=np.float64)
    return float(score_network.null_db(candidate, reference))


def nulls(candidate, reference, rate: int) -> dict:
    result = {"overallDb": null(candidate, reference)}
    for name, start, stop in STRETCHES:
        chosen = slice(int(start * rate), int(stop * rate))
        if chosen.start < len(reference):
            result[name] = null(candidate[chosen], reference[chosen])
    return result


def knobs(**display) -> dict:
    """Normalised host parameters for display values (seconds, percent, milliseconds); macro and mix as fractions."""
    return {key: (value if key in ("macro", "mix") else revocean.normalised(key, value)) for key, value in display.items()}


def captured(stimulus, settings, rate, warmup) -> np.ndarray:
    return revocean.capture(stimulus, settings, sample_rate=rate, warmup=warmup).output[:len(stimulus)].astype(np.float64)


def summary(rows: list, key: str) -> dict:
    values = [row[key]["overallDb"] if isinstance(row[key], dict) else row[key] for row in rows]
    return {"worstDb": max(values), "meanDb": round(float(np.mean(values)), 2), "bestDb": min(values)}


# ---------------------------------------------------------------- (a) the locked holdout

class Candidate:
    """The engine behind the interface of score_network.py. A `phase` keeps the Tide layer in the circuit at Macro 0."""

    def __init__(self, idle: bool, phase=None):
        self.idle, self.phase, self.rendered = idle, phase, []

    def render(self, stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> np.ndarray:
        settings = knobs(decay=decay_seconds, size=size_percent)
        result = engine_knobs(stimulus, sample_rate, warmup_seconds, settings, idle=self.idle, phase=self.phase)
        self.rendered.append(result)
        return result


def holdout() -> dict:
    processed, idled, tided = Candidate(False), Candidate(True), Candidate(False, EnginePhase(DEFAULT_VOICE_SEED, 1.0))
    result = score_network.score(processed)
    again = score_network.score(idled)
    with_tide = score_network.score(tided)
    result["warmupThroughAdvanceIdleIsBitIdentical"] = all(np.array_equal(a, b) for a, b in zip(processed.rendered, idled.rendered))
    result["scoreWithWarmupThroughAdvanceIdle"] = {key: again[key] for key in ("worstOverallDb", "meanOverallDb")}
    result["tideLayerInTheCircuitAtMacro0IsBitIdentical"] = all(np.array_equal(a, b) for a, b in zip(processed.rendered, tided.rendered))
    result["scoreWithTheTideLayerInTheCircuit"] = {key: with_tide[key] for key in ("worstOverallDb", "meanOverallDb")}
    stored = revocean.RESULTS / "work" / "specification" / "score_network_reference_render.json"
    if stored.exists():
        model = json.loads(stored.read_text())
        result["modelStoredScore"] = {"file": str(stored.relative_to(ROOT)), "worstOverallDb": model["worstOverallDb"], "meanOverallDb": model["meanOverallDb"],
                                      "cases": [{key: value for key, value in case.items() if key.endswith("Db") or key.endswith("s")} for case in model["cases"]]}
    for case in result["cases"]:
        print("holdout", json.dumps(case), flush=True)
    print("holdout", json.dumps({key: value for key, value in result.items() if key not in ("cases", "modelStoredScore")}), flush=True)
    return result


# ---------------------------------------------------------------- (b) fresh captures at Macro 0

# (host rate, Decay s, Size %, warm-up s)
FRESH_CASES = (
    (44100, 0.6, 80.1, 10.0), (44100, 5.5, 147.3, 3.0), (44100, 11.0, 30.0, 10.0),
    (48000, 1.7, 80.1, 6.5), (48000, 0.5, 200.0, 10.0), (48000, 20.0, 100.0, 10.0), (48000, 3.0, 55.5, 4.0),
    (88200, 2.6, 80.1, 10.0), (88200, 0.9, 121.0, 5.0),
    (96000, 1.1, 80.1, 10.0), (96000, 7.0, 162.1, 8.0), (96000, 4.0, 36.6, 10.0),
)
PROOF_CASES = (
    (44100, 2.0, 80.0, 10.0), (44100, 2.0, 80.1, 10.0), (44100, 2.0, 80.2, 10.0),
    (44100, 3.3, 162.1, 10.0), (44100, 1.2, 180.1, 10.0), (44100, 0.7, 32.95, 10.0), (44100, 9.0, 43.82, 7.7),
    (48000, 2.5, 80.1, 10.0), (48000, 0.9, 162.1, 10.0), (48000, 4.0, 100.0, 10.0), (48000, 6.6, 57.3, 13.3),
    (88200, 1.7, 180.1, 10.0), (88200, 2.0, 100.0, 10.0),
    (96000, 4.4, 80.1, 10.0), (96000, 0.8, 131.7, 10.0),
)


def fresh_row(job: tuple) -> dict:
    group, seed, (rate, decay, size, warmup) = job
    stimulus = datasets.network_programme(rate, seed)
    settings = knobs(decay=decay, size=size)
    reference = captured(stimulus, settings, rate, warmup)
    rendered = engine_knobs(stimulus, rate, warmup, settings)
    model = reference_render.render_knobs(stimulus, rate, warmup, settings)
    arguments = reference_render.physical(settings)
    row = {"captures": group, "hostRate": rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup,
           "sizeScale": float(arguments["size_scale"]), "engineAgainstReference": nulls(rendered, reference, rate),
           "modelAgainstReference": nulls(model, reference, rate), "engineAgainstModel": nulls(rendered, model, rate)}
    print("fresh", row, flush=True)
    return row


def fresh() -> dict:
    jobs = [("new, programme seed %d" % FRESH_SEED, FRESH_SEED, case) for case in FRESH_CASES]
    jobs += [("specification proof, programme seed %d" % PROOF_SEED, PROOF_SEED, case) for case in PROOF_CASES]
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(fresh_row, jobs))
    result = {"rows": rows}
    for group in dict.fromkeys(row["captures"] for row in rows):
        chosen = [row for row in rows if row["captures"] == group]
        result[group] = {key: summary(chosen, key) for key in ("engineAgainstReference", "modelAgainstReference", "engineAgainstModel")}
        print("fresh", group, result[group], flush=True)
    return result


# ---------------------------------------------------------------- (c) the outer laws

def loud_stimulus(rate: int, seed: int, seconds: float = 8.0) -> np.ndarray:
    """A quiet noise bed with events far above the level stage's threshold of 0.5629 and the clipper's of 2.51
    (the stimulus of the specification's proof for seed 31416)."""
    generator = np.random.default_rng(seed)
    frames = int(seconds * rate)
    signal = generator.uniform(-0.03, 0.03, (frames, 2))
    at = lambda t: int(t * rate)
    burst = generator.uniform(-2.0, 2.0, (at(0.3), 2))
    signal[at(0.5):at(0.5) + len(burst)] += burst
    inside = generator.uniform(-1.0, 1.0, (at(0.3), 2))
    signal[at(1.5):at(1.5) + len(inside)] += inside
    t = np.arange(at(0.4)) / rate
    signal[at(2.5):at(2.5) + len(t), 0] += 0.8 * np.sin(2 * np.pi * 137.0 * t)
    signal[at(2.5):at(2.5) + len(t), 1] += 1.5 * np.sin(2 * np.pi * 3100.0 * t)
    signal[at(3.6), 0] += 3.0
    signal[at(3.7), 1] += -6.0
    signal[at(3.9)] += 1.2
    signal[at(4.4):at(4.7), 0] += 2.0
    return signal.astype(np.float32)


# name: (captures, host rate, loud, display values of the moved controls)
SHELL_CASES = {
    "quiet, 48 kHz, Width 45 %, Mix 30 %": ("new", 48000, False, dict(decay=1.5, size=100.0, predelay=20.0, width=45.0, mix=0.3)),
    "quiet, 44.1 kHz, mono, Mix 50 %": ("new", 44100, False, dict(decay=2.7, size=64.0, predelay=1.7, width=0.0, mix=0.5)),
    "quiet, 88.2 kHz, Width 110 %, Mix 90 %": ("new", 88200, False, dict(decay=0.9, size=80.1, predelay=66.6, width=110.0, mix=0.9)),
    "quiet, 96 kHz, Width 150 %, Mix 100 %": ("new", 96000, False, dict(decay=4.2, size=140.0, predelay=250.0, width=150.0, mix=1.0)),
    "loud, 48 kHz, neutral shell": ("new", 48000, True, dict(decay=1.6, size=100.0)),
    "loud, 44.1 kHz, Width 130 %, Mix 60 %": ("new", 44100, True, dict(decay=0.8, size=118.0, predelay=30.0, width=130.0, mix=0.6)),
    "loud, 96 kHz, Width 60 %, Mix 35 %": ("new", 96000, True, dict(decay=2.4, size=75.0, predelay=8.25, width=60.0, mix=0.35)),
    "loud, 88.2 kHz, Width 100 %, Mix 80 %": ("new", 88200, True, dict(decay=1.2, size=162.1, predelay=4.0, width=100.0, mix=0.8)),
    "proof: quiet, 48 kHz": ("proof", 48000, False, dict(decay=2.0, size=70.0, predelay=37.77, width=62.0, mix=0.81)),
    "proof: quiet, 44.1 kHz": ("proof", 44100, False, dict(decay=1.3, size=162.1, predelay=9.37, width=133.0, mix=0.37)),
    "proof: quiet, 96 kHz": ("proof", 96000, False, dict(decay=3.0, size=80.1, predelay=120.5, width=20.0, mix=0.625)),
    "proof: quiet, 88.2 kHz": ("proof", 88200, False, dict(decay=0.8, size=110.0, predelay=3.3, width=150.0, mix=0.5)),
    "proof: loud, 48 kHz, Mix 100 %": ("proof", 48000, True, dict(decay=1.0, size=130.0, predelay=12.3, width=140.0, mix=1.0)),
    "proof: loud, 48 kHz, Mix 45 %": ("proof", 48000, True, dict(decay=1.0, size=130.0, predelay=12.3, width=140.0, mix=0.45)),
    "proof: loud, 44.1 kHz, Mix 70 %": ("proof", 44100, True, dict(decay=2.2, size=91.0, predelay=51.0, width=75.0, mix=0.7)),
}


def shell_row(name: str) -> dict:
    group, rate, loud, display = SHELL_CASES[name]
    seed = FRESH_SEED if group == "new" else PROOF_SEED
    stimulus = loud_stimulus(rate, seed + 1) if loud else datasets.network_programme(rate, seed)[:int(6.0 * rate)]
    settings = knobs(**display)
    arguments = reference_render.physical(settings)
    reference = captured(stimulus, settings, rate, 10.0)
    rendered = engine_knobs(stimulus, rate, 10.0, settings, shell=True)
    model = reference_render.render_knobs(stimulus, rate, 10.0, settings)
    # the wet part of the null: what is left when the dry signal is taken out of both
    latency = converters.reported_latency(rate)
    dry = np.zeros_like(reference)
    dry[latency:] = stimulus[:len(stimulus) - latency]
    dry *= reference_render.mix_gains(arguments["mix"])[0]
    row = {"case": name, "hostRate": rate, "display": display, "inputPeak": float(np.abs(stimulus).max()),
           "widthScale": arguments["width_scale"], "mix": arguments["mix"],
           "preDelayFramesModel": reference_render.predelay_samples(arguments["predelay_seconds"], rate),
           "preDelayFramesEngine": engine_predelay_frames(arguments["predelay_seconds"], rate),
           "engineAgainstReference": nulls(rendered, reference, rate), "modelAgainstReference": nulls(model, reference, rate),
           "engineAgainstModel": nulls(rendered, model, rate)}
    if not loud:
        row["engineAgainstReferenceWetOnlyDb"] = null(rendered - dry, reference - dry)
    else:
        reduction = reference_render.level_reduction_db(stimulus, rate)
        unclipped = reference_render.render_knobs(stimulus, rate, 10.0, settings, clip=False)
        row.update({
            "largestReductionDb": round(float(-reduction.min()), 3),
            "framesWithReduction": int(np.count_nonzero(reduction)),
            "samplesAboveTheClipperThreshold": int(np.sum(np.abs(unclipped) > reference_render.CLIP_THRESHOLD)),
            "engineWithoutLevelStageAgainstReferenceDb": null(engine_knobs(stimulus, rate, 10.0, settings, shell=True, level_stage=False), reference),
            "engineWithoutClipperAgainstReferenceDb": null(engine_knobs(stimulus, rate, 10.0, settings, shell=True, clip=False), reference)})
    print("shell", row, flush=True)
    return row


def shell() -> dict:
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(shell_row, SHELL_CASES))
    result = {"rows": rows}
    for label, chosen in (("quiet", [row for row in rows if "largestReductionDb" not in row]), ("loud", [row for row in rows if "largestReductionDb" in row])):
        result[label] = {key: summary(chosen, key) for key in ("engineAgainstReference", "modelAgainstReference", "engineAgainstModel")}
        print("shell", label, result[label], flush=True)
    return result


# ---------------------------------------------------------------- (d) engine against the model

def model_stimulus(rate: int, seed: int, seconds: float = 3.0) -> np.ndarray:
    """Impulses on both sides, a burst of noise and a decaying tone, peak 0.5, then silence."""
    generator = np.random.default_rng(seed)
    at = lambda t: int(round(t * rate))
    signal = np.zeros((at(seconds), 2))
    signal[at(0.02), 0], signal[at(0.11), 1] = 0.5, -0.4
    burst = generator.uniform(-0.5, 0.5, (at(0.05), 2))
    signal[at(0.3):at(0.3) + len(burst)] += burst
    t = np.arange(at(0.25)) / rate
    tone = 0.45 * np.sin(2 * np.pi * 440.0 * t) * np.exp(-t / 0.06)
    signal[at(0.6):at(0.6) + len(t), 0] += tone
    signal[at(0.6):at(0.6) + len(t), 1] -= 0.5 * tone
    return (signal * (0.5 / np.abs(signal).max())).astype(np.float32)


# (host rate, Decay s, Size scale, pre-delay s, warm-up s); the Decay and Size of the reference's own range ends,
# of Ocean's ranges where they reach further down in Size, and rates no capture exists at
MODEL_CASES = (
    (44100, 0.5, 0.3, 0.0, 3.0), (44100, 60.0, 2.0, 0.0, 3.0), (48000, 0.5, 2.0, 0.0, 1.0), (48000, 60.0, 0.3, 0.0, 20.0),
    (48000, 2.0, 0.15, 0.0, 3.0), (96000, 5.0, 0.2, 0.0, 3.0), (44100, 30.0, 0.15, 0.0, 3.0),
    (48000, 3.0, 1.0, 2.0, 0.5), (88200, 1.0, 1.3, 0.25, 3.0),
    (176400, 2.2, 0.801, 0.0123, 3.0), (192000, 1.4, 1.621, 0.0, 6.0),
    (22050, 1.5, 1.0, 0.004, 3.0), (32000, 1.5, 0.9, 0.0, 3.0), (64000, 2.0, 1.1, 0.01, 3.0), (352800, 1.0, 1.0, 0.0, 2.0), (384000, 3.0, 1.801, 0.0, 2.0),
    (47952, 1.5, 1.0, 0.0, 2.0), (44101, 1.5, 1.0, 0.0, 2.0),
)
PREDELAY_RATES = (44100, 48000, 88200, 96000)


def clock_signs(rate: int) -> dict:
    """The rounding signs of the two converter clocks the model works with at a rate, and where they come from."""
    if rate == converters.INTERNAL_RATE:
        return {"signs": [0, 0], "from": "no converters"}
    return {"signs": [int(sign) for sign in converters.clock_signs(rate)],
            "from": "measured" if rate in converters.MEASURED_CLOCK_SIGNS else "simulated"}


def model_row(case: tuple) -> dict:
    rate, decay, size, predelay, warmup = case
    stimulus = model_stimulus(rate, FRESH_SEED + rate)
    size, decay = np.float32(size), float(np.float32(decay))
    predelay = float(np.float32(predelay))
    rendered = raw(engine(stimulus, rate, warmup, decay, size, predelay), rate)
    model = reference_render.render(stimulus, rate, warmup, decay, size, predelay, 0.0, 1.0, 1.0, level_stage=False, clip=False)
    row = {"hostRate": rate, "decaySeconds": decay, "sizeScale": float(size), "preDelaySeconds": predelay, "warmupSeconds": warmup,
           "clockSigns": clock_signs(rate),
           "preDelayFramesModel": reference_render.predelay_samples(predelay, rate), "preDelayFramesEngine": engine_predelay_frames(predelay, rate),
           "engineAgainstModelDb": null(rendered, model), "peak": float(np.abs(model).max())}
    print("model", row, flush=True)
    return row


def predelay_law() -> dict:
    """The engine's whole frames of pre-delay against the model's, by the engine's arithmetic on its
    single-precision parameter in seconds: at every tenth of a millisecond of Ocean's knob (0 to 250 ms), which
    it hands over as milliseconds times 0.001f, and at every knob position of the reference in 2000."""
    result = {}
    for rate in PREDELAY_RATES + (176400, 192000):
        ocean = [(np.float32(tenth) * np.float32(0.1)) for tenth in range(2501)]
        seconds = [float(ms * np.float32(0.001)) for ms in ocean]
        ocean_off = sum(engine_predelay_frames(s, rate) != reference_render.predelay_samples(float(ms) / 1000.0, rate) for s, ms in zip(seconds, ocean))
        positions = [reference_render.knob_predelay_seconds(step / 2000.0) for step in range(2001)]
        knob_off = sum(engine_predelay_frames(s, rate) != reference_render.predelay_samples(s, rate) for s in positions)
        result[str(rate)] = {"oceanKnobPositions": len(ocean), "oceanKnobPositionsOneFrameOff": int(ocean_off),
                             "referenceKnobPositions": len(positions), "referenceKnobPositionsOneFrameOff": int(knob_off)}
    print("model", "pre-delay law", result, flush=True)
    return result


def predelay_rows() -> list:
    """Renders at pre-delays that fall on a whole number of frames, where a rounding of the law shows as one frame."""
    rows = []
    for rate in PREDELAY_RATES:
        for milliseconds in (12.5, 20.0, 137.5):
            stimulus = model_stimulus(rate, FRESH_SEED, 1.0)
            seconds = float(np.float32(milliseconds)) / 1000.0
            rendered = raw(engine(stimulus, rate, 3.0, 1.0, 1.0, seconds), rate)
            model = reference_render.render(stimulus, rate, 3.0, 1.0, np.float32(1.0), seconds, 0.0, 1.0, 1.0, level_stage=False, clip=False)
            rows.append({"hostRate": rate, "preDelayMs": milliseconds, "preDelayFramesModel": reference_render.predelay_samples(seconds, rate),
                         "preDelayFramesEngine": engine_predelay_frames(seconds, rate), "engineAgainstModelDb": null(rendered, model)})
            print("model", rows[-1], flush=True)
    return rows


def shell_model_rows() -> list:
    """The whole chain with loud input where no capture is taken: 176.4 and 192 kHz."""
    rows = []
    for rate, width, mix, predelay in ((176400, 1.4, 0.7, 0.0101), (192000, 0.35, 1.0, 0.0)):
        stimulus = loud_stimulus(rate, FRESH_SEED + 2, 5.0)
        width, mix, predelay = float(np.float32(width)), float(np.float32(mix)), float(np.float32(predelay))
        rendered = raw(engine(stimulus, rate, 3.0, 1.5, 1.0, predelay, level_stage=True, width_scale=width, mix=mix, clip=True), rate)
        model = reference_render.render(stimulus, rate, 3.0, 1.5, np.float32(1.0), predelay, 0.0, width, mix)
        rows.append({"hostRate": rate, "widthScale": width, "mix": mix, "preDelaySeconds": predelay, "inputPeak": float(np.abs(stimulus).max()),
                     "largestReductionDb": round(float(-reference_render.level_reduction_db(stimulus, rate).min()), 3),
                     "engineAgainstModelDb": null(rendered, model)})
        print("model", rows[-1], flush=True)
    return rows


def model() -> dict:
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(model_row, MODEL_CASES))
    result = {"rows": rows, "wholeResponses": summary(rows, "engineAgainstModelDb"),
              "preDelayLaw": predelay_law(), "preDelayOnWholeFrames": predelay_rows(), "shell": shell_model_rows()}
    print("model", result["wholeResponses"], flush=True)
    return result


# ---------------------------------------------------------------- (d2) clock signs beside the standard rates

CLOCK_CAPTURE_RATES = (64000, 384000)  # Decay 2 s, Size 100 %, warm-up 3 s, six seconds of the programme
CLOCK_MODEL_RATES = (128000,)          # no capture: the engine against the model


def clocks_row(rate: int) -> dict:
    stimulus = datasets.network_programme(rate, CLOCK_SEED)[:int(6.0 * rate)]
    settings = knobs(decay=2.0, size=100.0)
    rendered = engine_knobs(stimulus, rate, 3.0, settings)
    model = reference_render.render_knobs(stimulus, rate, 3.0, settings)
    row = {"hostRate": rate, "clockSigns": clock_signs(rate), "engineAgainstModel": nulls(rendered, model, rate)}
    if rate in CLOCK_CAPTURE_RATES:
        reference = revocean.capture(stimulus, settings, sample_rate=rate, warmup=3.0, timeout=1800.0).output[:len(stimulus)].astype(np.float64)
        row.update({"engineAgainstReference": nulls(rendered, reference, rate), "modelAgainstReference": nulls(model, reference, rate)})
    print("clocks", row, flush=True)
    return row


def clocks() -> dict:
    rows = [clocks_row(rate) for rate in CLOCK_CAPTURE_RATES + CLOCK_MODEL_RATES]
    captured_rows = [row for row in rows if "engineAgainstReference" in row]
    result = {"rows": rows, "captures": {key: summary(captured_rows, key) for key in ("engineAgainstReference", "modelAgainstReference", "engineAgainstModel")}}
    print("clocks", result["captures"], flush=True)
    return result


# ---------------------------------------------------------------- (e) above Macro 0 with the phase of a capture

def phase_nulls(candidate, reference, rate: int, stretches) -> dict:
    return {key: float(value) for key, value in score_tide.stretch_nulls(np.asarray(candidate, dtype=np.float64), np.asarray(reference, dtype=np.float64),
                                                                         rate, stretches).items()}


def same_phase(row: dict, stimulus, reference, rate: int, warmup: float, settings: dict, curve, stretches, shell: bool = False) -> None:
    """Adds to a row, for one phase curve: reference_render.py against the reference, and the engine against
    reference_render.py. With the same phase in both the second number is the port alone."""
    rendered = engine_knobs(stimulus, rate, warmup, settings, shell=shell, phase=curve)
    # at the neutral baseline the engine gives its wet signal, which the model's level stage and clipper leave as it is
    model = reference_render.render_knobs(stimulus, rate, warmup, settings, phase=curve, level_stage=shell, clip=shell)
    row["modelAgainstReferenceSamePhase"] = phase_nulls(model, reference, rate, stretches)
    row["engineAgainstModelSamePhase"] = phase_nulls(rendered, model, rate, stretches)


def tide_row(index: int) -> dict:
    case = score_tide.NULL_CASES[index]
    row = score_tide.score_null_case(sys.modules[__name__], case, REFIT_STEPS)
    stimulus, reference = score_tide.reference_of(case)
    settings = knobs(decay=case.decay, size=case.size, macro=case.macro / 100.0)
    same_phase(row, stimulus, reference, case.sample_rate, case.warmup, settings, score_tide.curve_from_json(row["curve"]), case.stretches)
    stored = json.loads(MODEL_SCORES.read_text())["scores"]["phaseFittedNull"]["cases"]
    row["modelStoredOverallDb"] = next(entry["overallDb"] for entry in stored if entry["case"] == case.key)
    print("tide", {key: value for key, value in row.items() if key != "curve"}, flush=True)
    return row


def by_macro(rows: list, macro_of) -> dict:
    """Worst and mean of the three nulls of `rows` per Macro value."""
    result = {}
    for macro in sorted({macro_of(row) for row in rows}, reverse=True):
        chosen = [row for row in rows if macro_of(row) == macro]
        result[f"macro{macro:g}"] = {"cases": len(chosen), "engineAgainstReference": summary(chosen, "overallDb"),
                                     "modelAgainstReferenceSamePhase": summary(chosen, "modelAgainstReferenceSamePhase"),
                                     "engineAgainstModelSamePhase": summary(chosen, "engineAgainstModelSamePhase")}
    return result


def tide() -> dict:
    score_tide.SILENCE = FIT_SILENCE
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(tide_row, range(len(score_tide.NULL_CASES))))
    result = {"refitSteps": REFIT_STEPS, "rows": rows, **by_macro(rows, lambda row: float(row["case"].split("/")[5]))}
    print("tide", {key: value for key, value in result.items() if key != "rows"}, flush=True)
    return result


# (host rate, Decay s, Size %, warm-up s, Macro %, realisation, moved outer controls): TIDE_CASES of work/specification/proof.py
FRESH_TIDE_CASES = (
    (44100, 2.4, 100.0, 10.0, 100.0, 40, {}),
    (48000, 1.3, 80.1, 10.0, 100.0, 40, {}),
    (88200, 3.0, 120.0, 10.0, 100.0, 40, {}),
    (96000, 0.9, 64.0, 10.0, 100.0, 40, {}),
    (48000, 2.0, 100.0, 10.0, 100.0, 41, dict(predelay=23.4, width=130.0, mix=0.7)),
    (44100, 5.0, 162.1, 10.0, 50.0, 40, {}),
    (48000, 2.0, 100.0, 12.5, 50.0, 40, {}),
    (96000, 1.5, 90.0, 10.0, 50.0, 40, {}),
    (44100, 1.0, 100.0, 10.0, 25.0, 40, {}),
    (48000, 3.0, 140.0, 10.0, 25.0, 40, {}),
    (88200, 2.0, 100.0, 10.0, 25.0, 40, {}),
)


def fresh_tide_row(index: int) -> dict:
    """One capture of the specification's proof with the phase curve that proof kept for it. At the neutral
    baseline the engine then takes steps of its own fit, as score_tide.py does; with the outer controls
    moved the two outputs share both phases and the stored curve stands."""
    rate, decay, size, warmup, macro, realisation, outer = FRESH_TIDE_CASES[index]
    stored = json.loads(PROOF_TIDE.read_text())["rows"][index]
    if [stored[key] for key in ("hostRate", "decaySeconds", "sizePercent", "warmupSeconds", "macroPercent", "realisation")] != [rate, decay, size, warmup, macro, realisation]:
        raise RuntimeError(f"{PROOF_TIDE.name} does not hold case {index}")
    stimulus = datasets.network_programme(rate, PROOF_SEED)
    settings = knobs(decay=decay, size=size, macro=macro / 100.0, **outer)
    reference = revocean.capture(stimulus, settings, sample_rate=rate, warmup=warmup, realisation=realisation).output[:len(stimulus)].astype(np.float64)
    shell, stretches = bool(outer), score_tide.PROGRAMME_STRETCHES
    render_with = lambda phase: engine_knobs(stimulus, rate, warmup, settings, shell=shell, phase=phase)
    curve = score_tide.curve_from_json(stored["curves"][stored["kept"]])
    row = {"hostRate": rate, "decaySeconds": decay, "sizePercent": size, "warmupSeconds": warmup, "macroPercent": macro, "realisation": realisation,
           "outer": outer, "storedCurve": stored["kept"], "withStoredCurve": phase_nulls(render_with(curve), reference, rate, stretches),
           "refitSteps": 0 if shell else REFIT_STEPS, "modelStoredOverallDb": stored[stored["kept"]]["overallDb"]}
    if not shell:
        curve = score_tide.PhaseFit(render_with, reference, rate, int(round(warmup * rate))).refine(curve, REFIT_STEPS)
    row.update(phase_nulls(render_with(curve), reference, rate, stretches))
    same_phase(row, stimulus, reference, rate, warmup, settings, curve, stretches, shell)
    print("fresh_tide", row, flush=True)
    return row


def fresh_tide() -> dict:
    if not PROOF_TIDE.exists():
        raise SystemExit(f"{PROOF_TIDE} is missing: run work/specification/proof.py tide")
    score_tide.SILENCE = FIT_SILENCE
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(fresh_tide_row, range(len(FRESH_TIDE_CASES))))
    result = {"rows": rows, **by_macro(rows, lambda row: row["macroPercent"])}
    print("fresh_tide", {key: value for key, value in result.items() if key != "rows"}, flush=True)
    return result


# ---------------------------------------------------------------- (f) the engine's own phase against its Python form

# (host rate, Decay s, Size scale, pre-delay s, warm-up s, Macro, voice seed, seconds). After 15 s both levels
# have left their hold and move between knots; Macro 0.03 and 0.0568 lie below and at the full depth of the voices.
SEEDED_CASES = (
    (44100, 2.0, 1.0, 0.0, 3.0, 1.0, DEFAULT_VOICE_SEED, 3.0), (44100, 1.2, 1.0, 0.0, 20.0, 1.0, DEFAULT_VOICE_SEED, 14.0),
    (48000, 1.5, 0.801, 0.0, 16.0, 1.0, 1, 3.0), (48000, 2.0, 1.0, 0.0, 55.0, 1.0, 8, 6.0),
    (88200, 3.0, 1.2, 0.0123, 40.0, 1.0, 2, 3.0), (96000, 0.9, 0.64, 0.0, 16.0, 1.0, 2 ** 63 + 12345, 3.0),
    (48000, 2.0, 1.0, 0.0, 16.0, 0.5, 7, 3.0), (96000, 4.0, 1.621, 0.25, 25.0, 0.5, 5, 3.0),
    (44100, 1.0, 1.621, 0.0, 25.0, 0.25, 3, 3.0), (88200, 0.5, 0.3, 0.0, 9.0, 0.25, 6, 3.0),
    (48000, 2.0, 1.0, 0.0, 16.0, 0.03, 4, 3.0), (44100, 2.0, 1.0, 0.0, 16.0, 0.0568, 9, 3.0), (48000, 60.0, 2.0, 0.0, 30.0, 0.75, 10, 3.0),
    (176400, 2.2, 0.801, 0.0, 12.0, 1.0, 11, 3.0), (192000, 1.4, 1.0, 0.0, 16.0, 1.0, 12, 3.0),
    (22050, 1.5, 1.0, 0.0, 16.0, 1.0, 13, 3.0), (64000, 2.0, 1.1, 0.0, 16.0, 1.0, 14, 3.0), (44101, 1.5, 1.0, 0.0, 16.0, 1.0, 15, 3.0),
)


def seeded_row(case: tuple) -> dict:
    rate, decay, size, predelay, warmup, macro, seed, seconds = case
    stimulus = datasets.network_programme(rate, FRESH_SEED) if seconds > 6.0 else model_stimulus(rate, FRESH_SEED + rate, seconds)
    size, decay, predelay, macro = np.float32(size), float(np.float32(decay)), float(np.float32(predelay)), float(np.float32(macro))
    rendered = raw(engine(stimulus, rate, warmup, decay, size, predelay, macro, seed=seed), rate)
    phase = EnginePhase(seed, macro)
    model = reference_render.render(stimulus, rate, warmup, decay, size, predelay, macro, 1.0, 1.0, phase=phase, level_stage=False, clip=False)
    other = raw(engine(stimulus, rate, warmup, decay, size, predelay, macro, seed=seed + 1), rate)
    times = warmup + np.array([0.0, len(stimulus) / rate])
    row = {"hostRate": rate, "decaySeconds": decay, "sizeScale": float(size), "preDelaySeconds": predelay, "warmupSeconds": warmup, "macro": macro,
           "voiceSeed": seed, "clockSigns": clock_signs(rate),
           "phaseLevelAtStartAndEnd": [[round(float(v), 6) for v in phase.level(output, times)] for output in (0, 1)],
           "engineAgainstModelDb": null(rendered, model), "leftDb": null(rendered[:, 0], model[:, 0]), "rightDb": null(rendered[:, 1], model[:, 1]),
           "engineWithTheNextSeedAgainstModelDb": null(other, model)}
    print("seeded", row, flush=True)
    return row


def seeded() -> dict:
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(seeded_row, SEEDED_CASES))
    result = {"rows": rows, "wholeResponses": summary(rows, "engineAgainstModelDb"), "withTheNextSeed": summary(rows, "engineWithTheNextSeedAgainstModelDb")}
    print("seeded", {key: value for key, value in result.items() if key != "rows"}, flush=True)
    return result


# ---------------------------------------------------------------- (g) running free: statistics and the level cycle

def statistics() -> dict:
    """score_tide.py's free-running statistics with the engine as candidate, beside the figures stored for the
    campaign's model (tide_model.json; the same number of seeds, its own generator)."""
    stored = json.loads(MODEL_SCORES.read_text())["scores"]["statistics"]
    result = {}
    for macro in (100.0, 50.0):
        scored = score_tide.score_statistics(Path(__file__).resolve(), macro, STATISTICS_SEEDS, WORKERS)
        model = next(entry for entry in stored if entry["macroPercent"] == macro)
        table = {key: {"engine": entry, "model": model["summary"].get(key), "referenceAgainstItselfRmsInSpreads": scored.get("referenceAgainstItself", {}).get(key)}
                 for key, entry in scored["summary"].items()}
        result[f"macro{macro:g}"] = {"seeds": scored["seeds"], "modelSeeds": model["seeds"], "impulseCases": scored["impulseCases"],
                                     "noiseCases": scored["noiseCases"], "descriptors": table}
        for key, entry in table.items():
            print("statistics", f"Macro {macro:g} %", key, json.dumps(entry), flush=True)
    return result


def level() -> dict:
    """score_tide.py's level cycle under steady noise at Macro 100 % with the engine as candidate, beside the
    figures stored for the campaign's model."""
    result = score_tide.score_level(Path(__file__).resolve(), LEVEL_SEEDS, WORKERS)
    stored = json.loads(MODEL_SCORES.read_text())["scores"]["levelCycle"]
    for name in score_tide.LEVEL_SETS:
        result[name]["model"] = {key: stored[name][key] for key in ("seeds", "candidate", "differenceInStandardErrors", "curveRmsZ", "curveRmsDifferenceDb")}
        print("level", name, json.dumps({key: value for key, value in result[name].items() if key not in ("meanCurveDb", "sdCurveDb")}), flush=True)
    result["macro0LevelDb"]["model"] = stored["macro0LevelDb"]["candidate"]
    print("level", "Macro 0", result["macro0LevelDb"], flush=True)
    return result


# ---------------------------------------------------------------- (h) the built plug-in

def reference_knob(law, target, start: float, span: int = 400):
    """A single-precision normalised value of the reference near `start` at which `law` gives `target`
    exactly, or None when no position within `span` steps does."""
    position = np.float32(start)
    for _ in range(span):
        position = np.nextafter(position, np.float32(0.0))
    for _ in range(2 * span + 1):
        if law(float(position)) == target:
            return float(position)
        position = np.nextafter(position, np.float32(1.0))
    return None


def matched(rate: int, decay: float, size: float, predelay: float = 0.0, width: float = 100.0, mix: float = 1.0, evolution: float = 0.0) -> tuple:
    """(plug-in settings, reference settings, what both work with) near display values of the plug-in's knobs.

    Decay and Size take the nearest knob positions of the plug-in for which the reference has a position with
    the same single-precision value (about every second position has one). Width, Mix and Macro are met
    exactly by the reference's linear travels. The reference's Pre-delay is set to the plug-in's milliseconds
    and must give the same whole frames."""
    exact = None
    start = render_candidate.decay_to_normalised(decay)
    for steps in range(200):
        for direction in ((0.0,) if steps == 0 else (0.0, 1.0)):
            position = start
            for _ in range(steps):
                position = np.nextafter(position, np.float32(direction))
            seconds = render_candidate.physical({"decay": float(position)})["decay_seconds"]
            knob = reference_knob(reference_render.knob_decay_seconds, seconds, revocean.normalised("decay", min(max(seconds, 0.5), 60.0)))
            if knob is not None:
                exact = (float(position), knob)
                break
        if exact is not None:
            break
    if exact is None:
        raise RuntimeError(f"no Decay near {decay} s is met exactly by both knobs")
    sizes = None
    for steps in range(60):
        for tenths in dict.fromkeys((round(10.0 * size) - steps, round(10.0 * size) + steps)):
            if 10.0 * render_candidate.SIZE_LAW_FROM_PERCENT <= tenths <= 2000:
                scale = np.float32(render_candidate.physical({"size": tenths / 2000.0})["size_scale"])
                knob = reference_knob(reference_render.knob_size_scale, scale, revocean.normalised("size", min(max(100.0 * float(scale), 30.0), 200.0)), 60)
                if knob is not None and sizes is None:
                    sizes = (tenths / 2000.0, knob)
        if sizes is not None:
            break
    if sizes is None:
        raise RuntimeError(f"no Size near {size} % is met exactly by both knobs")
    plugin = {"decay": exact[0], "size": sizes[0], **render_candidate.knobs(predelay=predelay, width=width, mix=mix, evolution=evolution)}
    held = render_candidate.physical(plugin)
    reference = {"decay": exact[1], "size": sizes[1], "predelay": revocean.normalised("predelay", 1000.0 * held["predelay_seconds"]),
                 "width": held["width_scale"] / 2.0, "mix": held["mix"], "macro": held["macro"]}
    theirs = reference_render.physical(reference)
    frames = (engine_predelay_frames(held["predelay_seconds"], rate), reference_render.predelay_samples(theirs["predelay_seconds"], rate))
    same = {"decay": held["decay_seconds"] == theirs["decay_seconds"], "size": held["size_scale"] == float(theirs["size_scale"]),
            "preDelayFrames": frames[0] == frames[1], "width": held["width_scale"] == theirs["width_scale"],
            "mix": held["mix"] == theirs["mix"], "macro": held["macro"] == theirs["macro"]}
    if not all(same.values()):
        raise RuntimeError(f"plug-in and reference do not work with the same numbers: {same}")
    works = {"decaySeconds": held["decay_seconds"], "sizeScale": held["size_scale"], "preDelayFrames": frames[0],
             "widthScale": held["width_scale"], "mix": held["mix"], "macro": held["macro"],
             "referenceDisplay": {key: round(revocean.CONTROL[key].display(reference[key]), 4) for key in ("decay", "size", "predelay", "width")}}
    return plugin, reference, works


def renderer_twin(stimulus, rate: int, warmup: float, settings: dict) -> np.ndarray:
    """The engine's renderer with the plug-in's outer chain (the level stage, the reference's Width law, Ocean's
    Mix and the clipper) at the numbers the plug-in works with at `settings`, in the engine's time base: what
    the plug-in itself must return at Macro 0. Above it the plug-in runs from a voice seed of its own."""
    held = render_candidate.physical(settings)
    if held["macro"] != 0.0:
        raise ValueError("above Macro 0 no renderer knows the voice seed of an instance of the plug-in")
    return engine(stimulus, rate, warmup, held["decay_seconds"], held["size_scale"], held["predelay_seconds"], held["macro"],
                  level_stage=True, width_scale=held["width_scale"], ocean_mix=held["mix"], clip=True)


def differing_samples(first, second) -> int:
    return int(np.count_nonzero(np.asarray(first) != np.asarray(second)))


class PluginCandidate:
    """The built plug-in behind the interface of score_network.py, its knobs at the positions nearest to what the
    reference works with. Keeps, per render, how far those positions are off and the samples in which the
    plug-in differs from the engine's renderer."""

    def __init__(self):
        self.rows = []

    def render(self, stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent) -> np.ndarray:
        wanted = reference_render.physical(render_candidate.reference_settings(decay_seconds, size_percent))
        settings = render_candidate.nearest(wanted["decay_seconds"], wanted["size_scale"])
        held = render_candidate.physical(settings)
        rendered = render_candidate.capture(stimulus, settings, sample_rate=sample_rate, warmup=warmup_seconds)
        output = rendered.output[:len(stimulus)]
        lengths = lambda scale: reference_render.network_model.parameters(reference_render.network_model.constants(), 1.0, 100.0 * float(np.float32(scale)))["length"]
        self.rows.append({"decayOffRelative": held["decay_seconds"] / wanted["decay_seconds"] - 1.0,
                          "sizeOffRelative": held["size_scale"] / float(wanted["size_scale"]) - 1.0,
                          "lineLengthsEqual": bool(np.array_equal(lengths(held["size_scale"]), lengths(wanted["size_scale"]))),
                          "samplesDifferingFromTheRenderer": differing_samples(output, renderer_twin(stimulus, sample_rate, warmup_seconds, settings)),
                          "pluginBinarySha256": rendered.meta["pluginBinarySha256"]})
        return render_candidate.raw(output, sample_rate)


# (name, host rate, loud, display values of the plug-in's knobs) at Macro 0 and Mix 100 %, where plug-in and
# reference follow the same laws. Width is Ocean's travel: 200 % is the reference's 150 %. The Sizes are
# positions that both knobs meet exactly.
PLUGIN_CASES = (
    ("48 kHz, Decay 2 s, Size 100 %", 48000, False, dict(decay=2.0, size=100.0)),
    ("48 kHz, Decay 5.5 s, Size 147.2 %", 48000, False, dict(decay=5.5, size=147.2)),
    ("44.1 kHz, Decay 2 s, Size 100 %", 44100, False, dict(decay=2.0, size=100.0)),
    ("44.1 kHz, Decay 5.5 s, Size 147.2 %", 44100, False, dict(decay=5.5, size=147.2)),
    ("88.2 kHz, Decay 1.1 s, Size 80 %", 88200, False, dict(decay=1.1, size=80.0)),
    ("96 kHz, Decay 3 s, Size 62 %", 96000, False, dict(decay=3.0, size=62.0)),
    ("48 kHz, Pre-delay 20 ms, Width 45 %", 48000, False, dict(decay=1.5, size=100.0, predelay=20.0, width=45.0)),
    ("44.1 kHz, Pre-delay 33.3 ms, Width 200 %", 44100, False, dict(decay=2.7, size=64.2, predelay=33.3, width=200.0)),
    ("48 kHz, loud, neutral shell", 48000, True, dict(decay=1.6, size=100.0)),
    ("44.1 kHz, loud, Pre-delay 30 ms, Width 130 %", 44100, True, dict(decay=0.8, size=117.9, predelay=30.0, width=130.0)),
)
# (name, host rate, loud, display values) at Macro 0 below Mix 100 %, where the plug-in's Mix is Ocean's own
# and the plug-in is compared with the renderer alone
PLUGIN_MIX_CASES = (
    ("48 kHz, Pre-delay 20 ms, Width 45 %, Mix 30 %", 48000, False, dict(decay=1.5, size=100.0, predelay=20.0, width=45.0, mix=0.3)),
    ("44.1 kHz, Pre-delay 33.3 ms, Width 200 %, Mix 70 %", 44100, False, dict(decay=2.7, size=64.2, predelay=33.3, width=200.0, mix=0.7)),
    ("96 kHz, Mix 35 %", 96000, False, dict(decay=3.0, size=62.0, mix=0.35)),
    ("88.2 kHz, Mix 0 %", 88200, False, dict(decay=1.1, size=80.0, mix=0.0)),
    ("44.1 kHz, loud, Pre-delay 30 ms, Width 130 %, Mix 60 %", 44100, True, dict(decay=0.8, size=117.9, predelay=30.0, width=130.0, mix=0.6)),
    ("48 kHz, loud, Mix 10 %", 48000, True, dict(decay=1.6, size=100.0, mix=0.1)),      # the dry signal alone reaches the clipper
)
# (host rate, display values) above Macro 0, where two renders of the plug-in are two realisations
PLUGIN_TIDE_CASES = (
    (48000, dict(decay=2.0, size=100.0, evolution=1.0)), (44100, dict(decay=3.0, size=147.3, evolution=1.0, predelay=33.3, width=130.0, mix=0.7)),
    (88200, dict(decay=0.9, size=80.1, evolution=0.5, predelay=66.6, width=110.0, mix=0.9)), (96000, dict(decay=4.2, size=140.0, evolution=0.25, predelay=250.0, width=200.0)),
)


def plugin_row(case: tuple) -> dict:
    name, rate, loud, display = case
    stimulus = loud_stimulus(rate, PLUGIN_SEED + 1) if loud else datasets.network_programme(rate, PLUGIN_SEED)
    settings, reference_settings, works = matched(rate, **display)
    reference = captured(stimulus, reference_settings, rate, 10.0)
    rendered = render_candidate.capture(stimulus, settings, sample_rate=rate, warmup=10.0)
    output = rendered.output[:len(stimulus)]
    model = reference_render.render_knobs(stimulus, rate, 10.0, reference_settings)
    row = {"case": name, "hostRate": rate, "display": display, "inputPeak": float(np.abs(stimulus).max()), "bothWorkWith": works,
           "readback": {key: rendered.meta["readback"][key] for key in render_candidate.MOVABLE}, "reportedLatency": rendered.latency,
           "pluginAgainstReference": nulls(render_candidate.raw(output, rate), reference, rate), "modelAgainstReference": nulls(model, reference, rate),
           "samplesDifferingFromTheRenderer": differing_samples(output, renderer_twin(stimulus, rate, 10.0, settings)), "samples": int(output.size),
           "pluginBinarySha256": rendered.meta["pluginBinarySha256"]}
    print("plugin", row, flush=True)
    return row


def plugin_mix_row(case: tuple) -> dict:
    """One capture at Macro 0 below Mix 100 %: the plug-in against the renderer with Ocean's Mix, and the shares
    of the dry signal and of the wet in its output, by least squares over the samples under the clipper's
    threshold. The wet is the renderer's in front of the Mix; the dry signal enters under the bound of 4."""
    name, rate, loud, display = case
    stimulus = loud_stimulus(rate, PLUGIN_SEED + 1) if loud else datasets.network_programme(rate, PLUGIN_SEED)
    settings = render_candidate.knobs(**display)
    held = render_candidate.physical(settings)
    rendered = render_candidate.capture(stimulus, settings, sample_rate=rate, warmup=10.0)
    output = rendered.output[:len(stimulus)]
    wet = engine(stimulus, rate, 10.0, held["decay_seconds"], held["size_scale"], held["predelay_seconds"], held["macro"],
                 level_stage=True, width_scale=held["width_scale"]).astype(np.float64)
    dry = np.clip(stimulus.astype(np.float64), -4.0, 4.0)
    untouched = np.abs(output) <= reference_render.CLIP_THRESHOLD
    shares = np.linalg.lstsq(np.column_stack([dry[untouched], wet[untouched]]), output[untouched].astype(np.float64), rcond=None)[0]
    linear = (1.0 - held["mix"]) * dry + held["mix"] * wet
    row = {"case": name, "hostRate": rate, "display": display, "inputPeak": float(np.abs(stimulus).max()), "worksWith": held,
           "readback": {key: rendered.meta["readback"][key] for key in render_candidate.MOVABLE},
           "dryShare": float(shares[0]), "wetShare": float(shares[1]), "linearLaw": {"dryShare": 1.0 - held["mix"], "wetShare": held["mix"]},
           "referenceLaw": dict(zip(("dryShare", "wetShare"), reference_render.mix_gains(held["mix"]))),
           "largestDistanceFromTheLinearLaw": float(np.abs(output - linear)[untouched].max()),
           "samplesAboveTheClipperThreshold": int(np.count_nonzero(~untouched)),
           "samplesDifferingFromTheRenderer": differing_samples(output, renderer_twin(stimulus, rate, 10.0, settings)), "samples": int(output.size),
           "pluginBinarySha256": rendered.meta["pluginBinarySha256"]}
    print("plugin", "below Mix 100 %", row, flush=True)
    return row


def plugin_tide_row(case: tuple) -> dict:
    """One setting above Macro 0 rendered by two new instances of the plug-in: two voice seeds, two realisations."""
    rate, display = case
    stimulus = datasets.network_programme(rate, PLUGIN_SEED)[:int(8.0 * rate)]
    settings = render_candidate.knobs(**display)
    first, second = (render_candidate.capture(stimulus, settings, sample_rate=rate, warmup=10.0).output[:len(stimulus)] for _ in range(2))
    row = {"hostRate": rate, "display": display, "worksWith": render_candidate.physical(settings), "peak": float(np.abs(first).max()),
           "smallestSample": float(np.abs(first[first != 0]).min()),
           "samplesDifferingBetweenTwoInstances": differing_samples(first, second), "samples": int(first.size),
           "secondInstanceAgainstTheFirstDb": null(second, first)}
    print("plugin", "above Macro 0", row, flush=True)
    return row


def plugin() -> dict:
    candidate = PluginCandidate()
    held_out = score_network.score(candidate)
    for case, row in zip(held_out["cases"], candidate.rows):
        case.update(row)
        print("plugin", "holdout", json.dumps(case), flush=True)
    with ThreadPoolExecutor(WORKERS) as pool:
        rows = list(pool.map(plugin_row, PLUGIN_CASES))
        mix_rows = list(pool.map(plugin_mix_row, PLUGIN_MIX_CASES))
        tide_rows = list(pool.map(plugin_tide_row, PLUGIN_TIDE_CASES))
    quiet, loud = [row for row in rows if row["inputPeak"] <= 0.5], [row for row in rows if row["inputPeak"] > 0.5]
    at_macro_0 = candidate.rows + rows + mix_rows
    result = {"plugin": str(render_candidate.PLUGIN.relative_to(ROOT)), "pluginBinarySha256": sorted({row["pluginBinarySha256"] for row in rows + mix_rows}),
              "holdoutAtTheNearestKnobPositions": held_out, "rows": rows, "belowMix100": mix_rows, "aboveMacro0": tide_rows,
              "quiet": {key: summary(quiet, key) for key in ("pluginAgainstReference", "modelAgainstReference")},
              "loud": {key: summary(loud, key) for key in ("pluginAgainstReference", "modelAgainstReference")},
              "rendersComparedWithTheRenderer": len(at_macro_0),
              "rendersThatDifferFromTheRenderer": sum(row["samplesDifferingFromTheRenderer"] > 0 for row in at_macro_0),
              "settingsRenderedTwiceAboveMacro0": len(tide_rows),
              "settingsWhoseTwoRendersAreTheSame": sum(row["samplesDifferingBetweenTwoInstances"] == 0 for row in tide_rows)}
    print("plugin", {key: value for key, value in result.items() if key not in ("rows", "belowMix100", "aboveMacro0", "holdoutAtTheNearestKnobPositions")}, flush=True)
    print("plugin", "holdout", json.dumps({key: held_out[key] for key in ("worstOverallDb", "meanOverallDb")}), flush=True)
    return result


def plugin_statistics_case(name: str) -> tuple:
    """One target case at Macro 100 %: PLUGIN_INSTANCES renders of the plug-in, each a new instance, scored
    together against the targets; how far the renders lie apart; and one render at Evolution 0, where no voice
    seed is heard, against the engine's renderer."""
    targets = json.loads(score_tide.TARGETS.read_text())
    case = targets["cases"][name]
    stimulus = descriptors.build_stimulus(case["stimulus"])
    rate, warmup, settings = targets["sampleRate"], revocean.WARMUP_SECONDS, case["settings"]
    wanted = reference_render.physical(render_candidate.reference_settings(settings["decaySeconds"], settings["sizePercent"], settings["macroPercent"]))
    knobs_of_plugin = render_candidate.nearest(wanted["decay_seconds"], wanted["size_scale"], macro=wanted["macro"])
    renders, digests, apart, first = [], set(), [], None
    for _ in range(PLUGIN_INSTANCES):
        output = render_candidate.capture(stimulus, knobs_of_plugin, sample_rate=rate, warmup=warmup).output[:len(stimulus)]
        digests.add(hashlib.sha256(output.tobytes()).hexdigest())
        if first is None:
            first = output
        else:
            apart.append(null(output, first))
        renders.append(render_candidate.raw(output, rate).astype(np.float32))
    scored = descriptors.compare_to_targets({name: renders}, targets, latency=targets["latency"])["cases"][name]
    at_rest = dict(knobs_of_plugin, evolution=0.0)
    resting = render_candidate.capture(stimulus, at_rest, sample_rate=rate, warmup=warmup).output[:len(stimulus)]
    facts = {"instances": len(renders), "rendersThatDifferFromEveryOther": len(digests),
             "othersAgainstTheFirstDb": {"closest": round(min(apart), 2), "farthest": round(max(apart), 2)},
             "samplesDifferingFromTheRendererAtEvolution0": differing_samples(resting, renderer_twin(stimulus, rate, warmup, at_rest)),
             "samples": int(resting.size)}
    print("plugin_statistics", name, facts, flush=True)
    return name, scored, facts


def plugin_statistics() -> dict:
    """compare_to_targets for PLUGIN_INSTANCES instances of the plug-in at Macro 100 %, Mix 100 %, summarised
    as score_tide.py summarises a candidate, beside the engine's stored figures for its seeds and the reference
    against itself."""
    targets = json.loads(score_tide.TARGETS.read_text())
    names = [name for name, case in targets["cases"].items() if case["settings"]["macroPercent"] == 100.0]
    with ThreadPoolExecutor(WORKERS) as pool:
        scored = list(pool.map(plugin_statistics_case, names))
    cases = {name: groups for name, groups, _ in scored}
    summary_of_plugin = score_tide.summarise_statistics(cases)
    stored = json.loads((WORK / "score_engine.json").read_text()).get("statistics", {}).get("macro100", {}) if (WORK / "score_engine.json").exists() else {}
    engine_stored = stored.get("descriptors", {})
    itself = targets["selfScores"].get("macro100", {})
    table = {key: {"plugin": entry, "engine": engine_stored.get(key, {}).get("engine"),
                   "referenceAgainstItselfRmsInSpreads": itself.get(key, {}).get("rmsInSpreads")} for key, entry in summary_of_plugin.items()}
    noise = sum(name.endswith("_noise") for name in names)
    facts = {name: entry for name, _, entry in scored}
    result = {"macroPercent": 100.0, "instances": PLUGIN_INSTANCES, "engineSeeds": stored.get("seeds"),
              "impulseCases": len(names) - noise, "noiseCases": noise, "descriptors": table, "cases": facts,
              "casesWhoseInstancesAllDiffer": sum(entry["rendersThatDifferFromEveryOther"] == entry["instances"] for entry in facts.values()),
              "closestTwoInstancesDb": min(entry["othersAgainstTheFirstDb"]["closest"] for entry in facts.values()),
              "rendersAtEvolution0ComparedWithTheRenderer": len(facts),
              "rendersAtEvolution0ThatDifferFromTheRenderer": sum(entry["samplesDifferingFromTheRendererAtEvolution0"] > 0 for entry in facts.values())}
    for key, entry in table.items():
        print("plugin_statistics", key, json.dumps(entry), flush=True)
    print("plugin_statistics", {key: value for key, value in result.items() if key not in ("descriptors", "cases")}, flush=True)
    return result


PARTS = {"holdout": holdout, "fresh": fresh, "shell": shell, "clocks": clocks, "model": model, "tide": tide, "fresh_tide": fresh_tide,
         "seeded": seeded, "statistics": statistics, "level": level, "plugin": plugin, "plugin_statistics": plugin_statistics}


def main() -> None:
    global tool
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("parts", nargs="*", help="parts to run: " + ", ".join(PARTS) + " (default: all)")
    parser.add_argument("--tool", type=Path, default=TOOL, help="the AmanitaOceanFathomRender executable")
    arguments = parser.parse_args()
    unknown = [name for name in arguments.parts if name not in PARTS]
    if unknown:
        parser.error(f"unknown part {unknown[0]!r}")
    tool = arguments.tool
    if not tool.exists():
        raise SystemExit(f"{tool} is missing: build the target AmanitaOceanFathomRender")
    os.environ["AMANITA_FATHOM_RENDER"] = str(tool)     # for the processes score_tide.py starts with this file as candidate
    revocean.identity()
    WORK.mkdir(parents=True, exist_ok=True)
    path = WORK / "score_engine.json"
    report = json.loads(path.read_text()) if path.exists() else {}
    headers = subprocess.run([sys.executable, str(HERE / "emit_engine_header.py"), "--check"], capture_output=True, text=True)
    report["generatedHeaders"] = {"upToDate": headers.returncode == 0, "check": headers.stdout.strip().splitlines()}
    print("generated headers", report["generatedHeaders"], flush=True)
    for name in arguments.parts or PARTS:
        report[name] = PARTS[name]()
        path.write_text(json.dumps(report, indent=1) + "\n")
    print(f"written {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
