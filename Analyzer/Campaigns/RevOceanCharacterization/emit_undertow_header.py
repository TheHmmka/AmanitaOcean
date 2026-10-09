#!/usr/bin/env python3
"""C++ headers of the Undertow layer, generated from the campaign's model of the reference's Abyss mode.

    python emit_undertow_header.py            write both headers
    python emit_undertow_header.py --check    exit 1 when a header on disk is not what the model gives
    python emit_undertow_header.py --engine [tool]
                                              the engine's renderer (AmanitaOceanFathomRender, default the one in
                                              build-undertow-dsp) against the model on the whole renders the golden
                                              vectors are cut from: with the layer, the base alone, the layer alone

    Source/dsp/UndertowConstants.h   every constant of the layer: those of the model (abyss/model/abyss_model.py and
                                     hostclock.py; abyss/findings/ABYSS_STATE.md, tempo.md and controls.md give their
                                     meaning and status) and Ocean's own, which are set in this file
    Tests/UndertowGoldenVectors.h    renders of the model in the reference's arithmetic: a fresh instance, a host
                                     that reports its transport per block of 512 frames, short stimuli; excerpts
                                     of the model's output where the layer is strongest

The head of each header carries the SHA-256 of the model files it was made from. The output depends on nothing
else. A double-precision number is written with the shortest digits that read back as the same double, a
single-precision number with the shortest digits that read back as the same float and an `f`.

The model is the campaign's own, in abyss/model/, and is imported, not repeated here: `abyss_model.render` with
`context["clock"] = "host"`. What it compiles and caches at run time goes under Analyzer/Results (abyss/model/base.py). Macro is handed to it
as the single-precision number the engine holds. A golden vector never moves Macro: the model file smooths Macro
in front of its ramps, the engine smooths each gain behind them (controls.md, section 3), and the two agree only
while Macro rests.
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MODEL = HERE / "abyss" / "model"
if str(MODEL) not in sys.path:
    sys.path.insert(0, str(MODEL))
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import abyss_model  # noqa: E402
import base  # noqa: E402
import hostclock  # noqa: E402
import converters  # noqa: E402

CONSTANTS_HEADER = ROOT / "Source/dsp/UndertowConstants.h"
GOLDEN_HEADER = ROOT / "Tests/UndertowGoldenVectors.h"
GENERATOR = "Analyzer/Campaigns/RevOceanCharacterization/emit_undertow_header.py"
SOURCES = [MODEL / "abyss_model.py", MODEL / "hostclock.py", MODEL / "abyss_core.c", MODEL / "base.py"]
F = np.float32

# ---------------------------------------------------------------- Ocean's own constants (not measured)

OCEAN = {
    "minimumTempo": 20.0,              # slower tempi are clamped; the layer's memory is sized for this one
    "maximumTempo": 999.0,
    "fallbackTempo": 120.0,            # a host without a tempo: the 2/3, 1 and 4/3 s of the 120 BPM sessions
    "cutShortFadeSeconds": 0.005,      # a chunk that is cut while it sounds fades out over this
    "cutShortFloor": 1.0e-3,           # ... "sounds": its window is above this (-60 dB) at the cut
    "jumpBlocks": 2.0,                 # a reported position this many internal blocks off the expected one is a jump
    "positionRate": 0.04,              # ... or this share of the time since the host's last word, if that is more
    "consistentQuarters": 1.0e-7,      # a reported position this near the expected one is the expected one
    "tempoStep": 0.01,                 # a change of the tempo by more than this share, with none near it, is a step
    "tempoSettleSeconds": 0.25,        # ... "near": within this time; two changes that near are a ramp
    "distrustJumps": 3,                # this many jumps in a row and the layer keeps time itself
    "jumpRowSeconds": 0.25,            # ... "in a row": less than this apart, or at successive host blocks
    "returnSeconds": 1.0,              # a position that has run without a jump for this long is followed again
    "gainFloor": 1.0e-12,              # a gain this near its target has arrived
}


# ---------------------------------------------------------------- literals

def real(value) -> str:
    value = float(value)
    text = repr(value)
    assert float(text) == value and ("." in text or "e" in text or "n" in text)
    return text


def single(value) -> str:
    exact = F(value)
    assert float(exact) == float(value), f"{value!r} is not a single-precision number"
    text = str(exact)
    if "." not in text and "e" not in text:
        text += ".0"
    assert F(text) == exact
    return text + "f"


def bits(value) -> str:
    return f"0x{int(np.array([value], np.float32).view(np.uint32)[0]):08X}"


def rows(items: list, per_line: int, indent: str = "    ") -> str:
    lines = [", ".join(items[start:start + per_line]) for start in range(0, len(items), per_line)]
    return ",\n".join(indent + line for line in lines)


def head(title: list) -> str:
    lines = [f"// Generated by {GENERATOR}.", "// Do not edit: change the model or the generator and run it again.", "//"]
    lines += [f"// {line}" for line in title]
    lines += ["//", "// Made from (SHA-256):"]
    lines += [f"//   {path.relative_to(ROOT)}  {hashlib.sha256(path.read_bytes()).hexdigest()}" for path in SOURCES]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- the constants

def constants_header() -> str:
    c = abyss_model.CONSTANTS
    assert c["chunk_seconds"] == {"R": float(F(2.0 / 3.0)), "U": 1.0, "D": float(F(4.0 / 3.0))}
    assert hostclock.NOTE_QUARTERS == {"R": float(F(4.0 / 3.0)), "U": 2.0, "D": float(F(8.0 / 3.0))}
    assert c["comb_delay"] == abyss_model.INTERNAL_RATE and c["tempo_scales"] == {"window": True, "comb": True}
    assert c["phasor_start"] == 0.5 and c["phasor_reset_offset"] == -2 and c["position_rounding"] == "exact"
    assert c["gain"]["UL"] == c["gain"]["UR"] and c["macro_smoothing_ms"] == 10.0
    assert c["host_mirror_a"] == hostclock.MIRROR_A and hostclock.BLOCK == 44
    assert [c["reader"][key]["direction"] for key in ("D", "UL", "UR")] == [0, 1, 1]
    assert hostclock.free_run_drift(511.0) == 0.0 and hostclock.free_run_drift(513.0) == 4.93e-5
    assert abs(hostclock.free_run_drift(1025.0) - 4.93e-5 * 511.0) < 1e-12

    def window(key: str) -> str:
        w = c["window"][key]
        return "{ " + ", ".join(real(w[name]) for name in ("fade_in", "fade_out", "end")) + " }"

    def reader(key: str) -> str:
        r = c["reader"][key]
        return "{ " + ", ".join([single(r["inc"]), real(r["W"]), real(r["dmin"])]) + " }"

    increments = ", ".join(f"{key} {bits(c['reader'][key]['inc'])}" for key in ("D", "UL", "UR"))
    o = OCEAN
    body = f"""#pragma once

namespace amanita::dsp::undertow
{{
// Raised-cosine fades of a chunk over the position behind its mirror point:
// in over the first `fadeIn` samples, out over the last `fadeOut` samples in
// front of `end`, nothing from `end` on. Internal samples at 120 BPM; the
// three numbers follow the tempo with the chunk, so they are fixed shares of it.
struct Window
{{
    double fadeIn, fadeOut, end;
}};

// A grain reader: the increment of its single-precision phasor per internal
// sample, the sweep of its delay and its shortest delay in internal samples.
struct GrainReader
{{
    float increment;
    double sweepSamples, shortestDelaySamples;
}};

// A voice gain against Macro: nothing up to `foot`, the full gain from `knee`,
// a straight line between.
struct Ramp
{{
    double foot, knee;
}};

// The layer runs at the engine's internal rate in blocks of this many samples.
inline constexpr int blockSamples = {hostclock.BLOCK};

// Reversed readers (ABYSS_STATE.md, section 2; tempo.md, section 4). A chunk
// is a note value, held as a single-precision number of quarter notes.
inline constexpr float unisonChunkQuarters = {single(hostclock.NOTE_QUARTERS["R"])};
inline constexpr float octaveUpChunkQuarters = {single(hostclock.NOTE_QUARTERS["U"])};
inline constexpr float octaveDownChunkQuarters = {single(hostclock.NOTE_QUARTERS["D"])};
// Tempo at which the windows below and the recirculation delay are given.
inline constexpr double windowTempo = 120.0;
inline constexpr Window unisonWindow {window("R")};
inline constexpr Window octaveUpWindow {window("U")};
inline constexpr Window octaveDownWindow {window("D")};

// Recirculation of the octave-up voice in front of its grain reader: one chunk
// of its reader, in whole internal samples, and the gain of one pass.
inline constexpr double recirculationSamplesAtWindowTempo = {real(c["comb_delay"])};
inline constexpr double recirculationGain = {real(c["comb_gain"])};

// Grain readers. The phasors are single precision and stand at `phasorStart`
// `phasorLeadSamples` in front of their origin. Bit patterns of the
// increments: {increments}.
inline constexpr float phasorStart = {single(c["phasor_start"])};
inline constexpr int phasorLeadSamples = {-c["phasor_reset_offset"]};
inline constexpr GrainReader octaveDownReader {reader("D")};
inline constexpr GrainReader octaveUpLeftReader {reader("UL")};
inline constexpr GrainReader octaveUpRightReader {reader("UR")};

// Macro (ABYSS_STATE.md, section 4; controls.md, section 3): the gain of each
// voice at full strength, its ramp, and the one-pole on each gain.
inline constexpr double unisonGain = {real(c["gain"]["R"])};
inline constexpr double octaveUpLeftGain = {real(c["gain"]["UL"])};
inline constexpr double octaveUpRightGain = {real(c["gain"]["UR"])};
inline constexpr double octaveDownGain = {real(c["gain"]["D"])};
inline constexpr Ramp unisonRamp {{ {real(c["ramp"]["R"][0])}, {real(c["ramp"]["R"][1])} }};
inline constexpr Ramp octaveUpRamp {{ {real(c["ramp"]["U"][0])}, {real(c["ramp"]["U"][1])} }};
inline constexpr Ramp octaveDownRamp {{ {real(c["ramp"]["D"][0])}, {real(c["ramp"]["D"][1])} }};
inline constexpr double gainSmoothingSeconds = {real(c["macro_smoothing_ms"] * 1e-3)};

// The reference's arithmetic (tempo.md, sections 3 and 5); a test hook of the
// engine. Converter block m stamps the first internal block that begins at or
// behind the place of m converter blocks, in internal samples, less
// `tickBlockOffset`; a boundary is taken in the block whose stamp lies less
// than `tickCatchSamples` in front of it. With a stopped transport the
// boundaries fall later by a fitted curve whose arithmetic is not known:
// `freeRunDriftSamplesPerQuarter` from `freeRunDriftFromQuarters` on, with the
// sign turning at every doubling of the position.
inline constexpr double tickBlockOffset = {real(hostclock.MIRROR_A)};
inline constexpr double tickCatchSamples = {real(float(hostclock.BLOCK - 1))};
inline constexpr double freeRunDriftSamplesPerQuarter = 4.93e-05;
inline constexpr double freeRunDriftFromQuarters = 512.0;
// The 32 accumulators of the base repeat after `oscillatorPeriodSteps` steps
// once `oscillatorSettleSteps` have been made.
inline constexpr long long oscillatorSettleSteps = {base.OSC_SETTLE};
inline constexpr long long oscillatorPeriodSteps = {base.OSC_PERIOD};

// Ocean's own (nothing of this was measured).
inline constexpr double minimumTempo = {real(o["minimumTempo"])};
inline constexpr double maximumTempo = {real(o["maximumTempo"])};
inline constexpr double fallbackTempo = {real(o["fallbackTempo"])};
// A chunk that is cut while its window is above the floor fades out.
inline constexpr double cutShortFadeSeconds = {real(o["cutShortFadeSeconds"])};
inline constexpr double cutShortFloor = {real(o["cutShortFloor"])};
// The reported position against the one the tempo lets expect. It has jumped
// when it lies `jumpBlocks` internal blocks off, or `positionRate` of the time
// since the host's last word if that is more; it is where it was expected
// within `consistentQuarters`; in between it has drifted and is followed.
inline constexpr double jumpBlocks = {real(o["jumpBlocks"])};
inline constexpr double positionRate = {real(o["positionRate"])};
inline constexpr double consistentQuarters = {real(o["consistentQuarters"])};
// The tempo. A change by more than `tempoStep` of it that has no other change
// within `tempoSettleSeconds`, nor at the host block before or behind it, is a
// step; changes that near to each other are a ramp.
inline constexpr double tempoStep = {real(o["tempoStep"])};
inline constexpr double tempoSettleSeconds = {real(o["tempoSettleSeconds"])};
// A position that tells nothing. `distrustJumps` jumps in a row, each less
// than `jumpRowSeconds` behind the one before or at the next host block, and
// the layer keeps time itself; the position is followed again once it has run
// for `returnSeconds` without a jump.
inline constexpr int distrustJumps = {o["distrustJumps"]};
inline constexpr double jumpRowSeconds = {real(o["jumpRowSeconds"])};
inline constexpr double returnSeconds = {real(o["returnSeconds"])};
// A gain this near its target has arrived.
inline constexpr double gainFloor = {real(o["gainFloor"])};
}} // namespace amanita::dsp::undertow
"""
    title = ["Constants of the Undertow layer: the campaign's model of the reference's Abyss mode",
             "(Analyzer/Campaigns/RevOceanCharacterization/abyss/findings: ABYSS_STATE.md, tempo.md and",
             "controls.md give their meaning and status) and Ocean's own."]
    return head(title) + body


# ---------------------------------------------------------------- the golden vectors

BLOCK = 512
EXCERPT_FRAMES = 160
EXCERPTS = 3                  # per vector: where the layer is strongest, each a good way from the others
EXCERPT_SPACING_SECONDS = 0.3
EXCERPT_LAYER_SHARE_DB = -25.0


def noise(frames: int, seed: int) -> np.ndarray:
    """White noise in single precision from a linear congruential generator, the same on every machine."""
    state = np.uint64(seed)
    out = np.zeros(frames, np.float32)
    for index in range(frames):
        state = (state * np.uint64(1664525) + np.uint64(1013904223)) & np.uint64(0xFFFFFFFF)
        out[index] = F(np.int64(state) - (1 << 31)) / F(2147483648.0)
    return out


def stimulus(kind: str, rate: int, frames: int) -> tuple:
    """(impulses [(frame, channel, amplitude)], programme first frame, programme (n, 2) float32)."""
    impulses, first, programme = [], 0, np.zeros((0, 2), np.float32)
    if kind == "impulses":
        impulses = [(int(0.040 * rate), 0, 0.5), (int(0.230 * rate) + 7, 1, -0.375), (int(0.520 * rate) + 3, 0, 0.25)]
    elif kind == "burst":
        first, count = int(0.050 * rate), 400
        fade = np.hanning(2 * 32)[:32].astype(np.float32)
        envelope = np.ones(count, np.float32)
        envelope[:32], envelope[-32:] = fade, fade[::-1]
        programme = np.stack([F(0.4) * envelope * noise(count, 11), F(0.4) * envelope * noise(count, 23)], axis=1).astype(np.float32)
    elif kind == "tone":
        first, count = int(0.030 * rate), 360
        t = np.arange(count) / rate
        envelope = np.sin(np.pi * np.arange(count) / count) ** 2
        programme = np.stack([0.4 * envelope * np.sin(2 * np.pi * 1000.0 * t), 0.3 * envelope * np.sin(2 * np.pi * 3000.0 * t)],
                             axis=1).astype(np.float32)
    else:
        raise ValueError(kind)
    return impulses, first, programme


def signal(impulses, first, programme, frames: int) -> np.ndarray:
    x = np.zeros((frames, 2), np.float32)
    for frame, channel, amplitude in impulses:
        x[frame, channel] += F(amplitude)
    x[first:first + len(programme)] += programme
    return x


# name, stimulus, host rate, tempo, Macro, playing, seconds, first frame of the instance's count,
# play head ahead of the processed frames (s), oscillator origin
VECTORS = (
    ("impulses48000", "impulses", 48000, 120.0, 1.0, True, 3.3, 0, 0.0, 0),
    ("burst48000", "burst", 48000, 120.0, 0.6, True, 3.3, 0, 0.0, 0),
    ("tone48000", "tone", 48000, 90.0, 1.0, True, 4.2, 0, 0.25, 0),
    ("stopped48000", "impulses", 48000, 90.0, 0.3, False, 4.2, 0, 0.0, 0),
    ("late48000", "burst", 48000, 120.0, 1.0, True, 3.3, 28800000, 0.0, 1323432),
    ("impulses44100", "impulses", 44100, 120.0, 0.6, True, 3.3, 0, 0.0, 0),
    ("burst44100", "burst", 44100, 90.0, 1.0, False, 4.2, 0, 0.0, 0),
    ("tone44100", "tone", 44100, 120.0, 0.3, True, 3.3, 0, 0.0, 0),
)
# Display values of the reference's knobs, and what the reference works with at them: the engine's parameters.
DECAY_SECONDS, SIZE_PERCENT = 2.0, 100.0
_KNOBS = base.nm.constants()["parameters"]
ENGINE_DECAY_SECONDS = F(base.nm.host_value(_KNOBS["decay"], DECAY_SECONDS))
ENGINE_SIZE_SCALE = F(base.nm.host_value(_KNOBS["size"], SIZE_PERCENT) / 100.0)


def golden_vectors() -> list:
    out = []
    for name, kind, rate, bpm, macro, playing, seconds, first_frame, ahead, origin in VECTORS:
        latency = converters.reported_latency(rate)
        frames = int(seconds * rate)
        impulses, first, programme = stimulus(kind, rate, frames)
        x = signal(impulses, first, programme, frames + latency)
        context = {"oscillator_origin": origin, "clock": "host", "bpm": bpm, "host_block": BLOCK, "steps": [first_frame],
                   "playhead_offset_seconds": ahead, "playing": playing, "free_run_origin": origin}
        macro_single = float(F(macro))
        model = abyss_model.render(x, rate, first_frame / rate, DECAY_SECONDS, SIZE_PERCENT, macro_single, context)
        plain = abyss_model.render(x, rate, first_frame / rate, DECAY_SECONDS, SIZE_PERCENT, 0.0, context)
        # the engine's frame n is the model's raw frame n plus the reported latency
        model, plain = model[latency:], plain[latency:]
        layer = np.sum((model - plain) ** 2, axis=1)
        energy = np.convolve(layer, np.ones(EXCERPT_FRAMES), mode="valid")
        starts = []
        spacing = int(EXCERPT_SPACING_SECONDS * rate)
        for _ in range(EXCERPTS):
            start = int(np.argmax(energy))
            starts.append(start)
            energy[max(0, start - spacing):start + spacing] = -1.0
        excerpts = []
        for start in sorted(starts):
            piece = model[start:start + EXCERPT_FRAMES]
            share = 10 * np.log10(np.sum((piece - plain[start:start + EXCERPT_FRAMES]) ** 2) / np.sum(piece ** 2))
            assert share > EXCERPT_LAYER_SHARE_DB, f"{name}: the layer is {share:.1f} dB of the excerpt at frame {start}"
            excerpts.append((start, piece.astype(np.float32)))
            if "--verbose" in sys.argv:
                print(f"{name:16} excerpt at {start / rate:6.3f} s: layer {share:6.1f} dB of it, level {10 * np.log10(np.mean(piece ** 2)):7.1f} dBFS")
        out.append({"name": name, "rate": rate, "bpm": bpm, "macro": F(macro), "playing": playing, "firstFrame": first_frame,
                    "aheadFrames": int(round(ahead * rate)), "origin": origin, "impulses": impulses, "programmeFirst": first,
                    "programme": programme, "excerpts": excerpts})
    return out


def golden_header() -> str:
    vectors = golden_vectors()
    parts = []
    entries = []
    for v in vectors:
        name = v["name"]
        if v["impulses"]:
            items = [f"{{ {frame}, {channel}, {single(amplitude)} }}" for frame, channel, amplitude in v["impulses"]]
            parts.append(f"inline constexpr Impulse {name}Impulses[] {{\n{rows(items, 4)}\n}};\n")
        if len(v["programme"]):
            parts.append(f"inline constexpr float {name}Programme[] {{\n{rows([single(s) for s in v['programme'].reshape(-1)], 8)}\n}};\n")
        for index, (start, piece) in enumerate(v["excerpts"]):
            parts.append(f"inline constexpr float {name}Model{index}[] {{\n{rows([single(s) for s in piece.reshape(-1)], 8)}\n}};\n")
        impulses = f"{name}Impulses" if v["impulses"] else "{}"
        programme = f"{name}Programme" if len(v["programme"]) else "{}"
        excerpts = ", ".join(f"Excerpt {{ {start}, {name}Model{index} }}" for index, (start, _) in enumerate(v["excerpts"]))
        entries.append(f"""    {{ "{name}", {v['rate']}, {real(v['bpm'])}, {single(v['macro'])}, {str(v['playing']).lower()},
      {v['firstFrame']}LL, {v['aheadFrames']}LL, {v['origin']}LL,
      {impulses}, {v['programmeFirst']}, {programme},
      {{ {excerpts} }} }}""")
    body = f"""#pragma once

#include <array>
#include <span>

namespace amanita::dsp::undertowgolden
{{
struct Impulse
{{
    int frame;
    int channel;
    float amplitude;
}};

// Frames `firstFrame` and on of the engine's wet output, as the campaign's
// model renders them: left and right interleaved.
struct Excerpt
{{
    int firstFrame;
    std::span<const float> model;
}};

// A fresh instance of the engine with the Undertow layer in the reference's
// arithmetic, at the reference's Decay {real(DECAY_SECONDS)} s and Size {real(SIZE_PERCENT)} %, under a host that works in blocks
// of {BLOCK} frames at `tempo` and reports, while `playing`, the position of
// frame n of the instance's count as (n + aheadFrames) / rate seconds; stopped,
// it reports the position of `aheadFrames` at every block. The engine's first
// frame is frame `firstFrame` of that count, and the oscillators of the base,
// the grain phasors and the free-running clock have the origin `origin`. The
// stimulus: impulses, or a programme block (left and right interleaved) that
// starts at `programmeFirstFrame`; silence elsewhere. Frames count from the
// engine's first.
struct Vector
{{
    const char* name;
    int hostRate;
    double tempo;
    float macro;
    bool playing;
    long long firstFrame;
    long long aheadFrames;
    long long origin;
    std::span<const Impulse> impulses;
    int programmeFirstFrame;
    std::span<const float> programme;
    std::array<Excerpt, {EXCERPTS}> excerpts;
}};

inline constexpr int hostBlockFrames = {BLOCK};
// What the reference works with at those knob positions.
inline constexpr float decaySeconds = {single(ENGINE_DECAY_SECONDS)};
inline constexpr float sizeScale = {single(ENGINE_SIZE_SCALE)};

{chr(10).join(parts)}
inline constexpr Vector vectors[] {{
{("," + chr(10)).join(entries)}
}};
}} // namespace amanita::dsp::undertowgolden
"""
    title = ["Golden vectors of the Undertow layer, from the campaign's model of the reference's Abyss mode",
             "in the reference's arithmetic (abyss_model.render with the host clock)."]
    return head(title) + body


def engine_nulls(tool: Path) -> int:
    """Whole renders of the golden cases through the engine's renderer against the model, in decibels."""
    import subprocess
    import tempfile

    from scipy.io import wavfile

    def null(candidate, reference) -> float:
        return float(10 * np.log10((np.sum((candidate - reference) ** 2) + 1e-300) / (np.sum(reference ** 2) + 1e-300)))

    if not tool.exists():
        raise SystemExit(f"{tool} is missing: build the target AmanitaOceanFathomRender")
    worst = -400.0
    with tempfile.TemporaryDirectory() as folder:
        source, target = Path(folder) / "in.wav", Path(folder) / "out.wav"
        for name, kind, rate, bpm, macro, playing, seconds, first_frame, ahead, origin in VECTORS:
            latency = converters.reported_latency(rate)
            frames = int(seconds * rate)
            impulses, first, programme = stimulus(kind, rate, frames)
            x = signal(impulses, first, programme, frames + latency)
            context = {"oscillator_origin": origin, "clock": "host", "bpm": bpm, "host_block": BLOCK, "steps": [first_frame],
                       "playhead_offset_seconds": ahead, "playing": playing, "free_run_origin": origin}
            wavfile.write(source, rate, x)
            renders = {}
            for label, value in (("layer", float(F(macro))), ("base", 0.0)):
                model = abyss_model.render(x, rate, first_frame / rate, DECAY_SECONDS, SIZE_PERCENT, value, context)[latency:]
                command = [str(tool), "--input", str(source), "--output", str(target), "--layer", "undertow",
                           "--reference-arithmetic", "--decay", repr(float(ENGINE_DECAY_SECONDS)),
                           "--size", repr(float(ENGINE_SIZE_SCALE)), "--macro", repr(value), "--tempo", repr(bpm),
                           "--host-block", str(BLOCK), "--first-frame", str(first_frame),
                           "--playhead-frames", str(int(round(ahead * rate))), "--oscillator-origin", str(origin),
                           "--phasor-origin", str(origin), "--free-run-origin", str(origin)]
                subprocess.run(command + ([] if playing else ["--stopped"]), check=True)
                renders[label] = (wavfile.read(target)[1].astype(np.float64)[:frames], model)
            whole = null(*renders["layer"])
            base_alone = null(*renders["base"])
            layer_alone = null(renders["layer"][0] - renders["base"][0], renders["layer"][1] - renders["base"][1])
            worst = max(worst, whole, base_alone, layer_alone)
            print(f"{name:16} with the layer {whole:8.2f} dB   the base alone {base_alone:8.2f} dB   the layer alone {layer_alone:8.2f} dB")
    print(f"worst {worst:.2f} dB")
    return 0


def main() -> int:
    if "--engine" in sys.argv[1:]:
        at = sys.argv.index("--engine")
        given = sys.argv[at + 1] if len(sys.argv) > at + 1 else None
        return engine_nulls(Path(given) if given else ROOT / "build-undertow-dsp" / "AmanitaOceanFathomRender")
    check = "--check" in sys.argv[1:]
    only = [a for a in sys.argv[1:] if a in ("--constants", "--golden")]
    jobs = []
    if not only or "--constants" in only:
        jobs.append((CONSTANTS_HEADER, constants_header))
    if not only or "--golden" in only:
        jobs.append((GOLDEN_HEADER, golden_header))
    stale = 0
    for path, make in jobs:
        text = make()
        if check:
            same = path.exists() and path.read_text() == text
            print(f"{'ok   ' if same else 'STALE'} {path.relative_to(ROOT)}")
            stale += not same
        else:
            path.write_text(text)
            print(f"wrote {path.relative_to(ROOT)} ({len(text.splitlines())} lines)")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
