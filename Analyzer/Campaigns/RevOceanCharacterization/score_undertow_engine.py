#!/usr/bin/env python3
"""The Undertow engine against recordings of the reference in its Abyss mode.

    python score_undertow_engine.py [group ...]      groups: E D FA FB FZ FS1 FS3 FS5 FS6 FS7 FS4 T90 S90 (default: all)
    python score_undertow_engine.py --table          the table from the stored scores, beside the model's
    python score_undertow_engine.py --holdout        session F's three holdout recordings; once, at the very end
    python score_undertow_engine.py --product ...    the engine's own arithmetic in place of the reference's (stored apart)
    python score_undertow_engine.py --tool <path>    another build of AmanitaOceanFathomRender (default: build-undertow-dsp)

The engine is rendered by Tools/FathomRender.cpp (target AmanitaOceanFathomRender) with `--layer undertow` in the
reference's arithmetic. A job of a recorded session is reproduced from what the session's files say: host blocks of 512
frames that begin anew with every step of the job (flush, pre-roll, recording), the tempo, the position the host
reported, whether its transport ran, and the origin of the instance's oscillators, grain phasors and free-running
clock (the sample at which the reference was switched to Abyss; abyss/findings/tempo.md, section 2). What is not in
a session's own info.json is in abyss/sessions.json.

Null = `revocean.null_db` over the whole recording, both channels, nothing fitted: no gain, no delay. The engine's
frame n is the reference's raw frame n plus its reported latency; the render starts some 480 frames of silence in
front of the recording, at a frame where the two rates meet and a host block of the pre-roll began, so that every
recorded frame is covered and the engine hears of the transport from its first frame on. A recording is rendered in
one piece, however long.

The recordings and their stimuli are read from Analyzer/Results/RevOceanCharacterization/work/abyss/session_<label>/
(not in Git). The cases are those of the model's own score (abyss/findings/tempo.md, section 7), whose numbers
(abyss/findings/tempo_scores.json) stand beside the engine's in the table. Group FS4 is not in that score: sixteen steps of Macro under a running tone, one render with
Macro moved at the step boundaries; the model file smooths Macro in front of its ramps, the engine each gain behind
them (abyss/findings/controls.md, section 3), so there the engine is not expected at the model's numbers but above them.

With `--product` the chunk clock runs in the engine's own arithmetic: the position in double precision at every
internal block, on the input's own time. That is another clock than the reference's, whose stamps sit up to a
converter block elsewhere, so single copies arrive up to a millisecond or two apart and the null says how far two
clocks of the same grid are from each other, not how good the engine is.

`session_F/h1*`, `h2*`, `h3*` are a holdout. Nothing was tuned on them; `--holdout` loads them, and only it does.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

import numpy as np
from scipy.io import wavfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import converters  # noqa: E402
import network_model as nm  # noqa: E402
import revocean  # noqa: E402

RECORDINGS = ROOT / "Analyzer/Results/RevOceanCharacterization/work/abyss"      # the sessions; not in Git
FACTS = json.loads((HERE / "abyss" / "sessions.json").read_text())
SR = FACTS["sampleRate"]
BLOCK = FACTS["hostBlockFrames"]       # frames per host block in every session
LEAD = 480                             # frames of silence rendered in front of a recording, at least
HOLDOUT = ("h1", "h2", "h3")           # of session F
TOOL = Path(os.environ.get("AMANITA_FATHOM_RENDER", ROOT / "build-undertow-dsp" / "AmanitaOceanFathomRender"))
OUT = RECORDINGS / "engine"
SCORES = OUT / "scores.json"
MODEL_SCORES = HERE / "abyss" / "findings" / "tempo_scores.json"
REFERENCE_ARITHMETIC = True
KNOBS = nm.constants()["parameters"]


# ---------------------------------------------------------------- the sessions

def folder(session: str) -> Path:
    return RECORDINGS / f"session_{session}"


def cases(session: str) -> dict:
    """The cases of a session by name, from the info file its script wrote."""
    info = json.loads((folder(session) / FACTS["sessions"][session]["info"]).read_text())
    return {case["name"]: case for case in info["cases"]}


def load(session: str, name: str):
    """A recording: raw frames from the case's first frame. The holdout is refused here."""
    if session == "F" and name[:2] in HOLDOUT:
        raise RuntimeError("locked holdout")
    return np.load(folder(session) / f"{name}.npy", mmap_mode="r")


def stimulus_of(session: str, name: str):
    """The stimulus of a case; some cases share a file, and the long maps of session F are stored compressed."""
    stem = cases(session).get(name, {}).get("stimulus") or f"{name}.stimulus.npy"
    path = folder(session) / stem
    if not path.exists():
        path = folder(session) / f"{name}.stimulus.npy"
    if path.suffix == ".npz":
        return np.load(path)["stimulus"]
    return np.load(path, mmap_mode="r")


def null_db(candidate, reference) -> float:
    return float(revocean.null_db(candidate, reference))


def engine_parameters(decay_seconds: float, size_percent: float) -> tuple:
    """What the reference works with at display values of its Decay and Size: the engine's parameters."""
    return (float(np.float32(nm.host_value(KNOBS["decay"], float(decay_seconds)))),
            float(np.float32(nm.host_value(KNOBS["size"], float(size_percent)) / 100.0)))


def lead_frames(first_frame: int, steps) -> int:
    """Frames of silence rendered in front of a job, LEAD at least: the engine's first frame is one at which the
    two rates meet (a multiple of 160 frames at 48 kHz) and at which the host began a block, so that the engine is
    told of the transport from its first frame on, as the reference was."""
    for lead in range(LEAD, LEAD + 2560 + 1):
        start = first_frame - lead
        run = max([int(step) for step in steps if int(step) <= start], default=None)
        if start % 160 == 0 and run is not None and (start - run) % BLOCK == 0:
            return lead
    raise ValueError(f"no frame in front of {first_frame} at which the rates meet and a host block begins")


def render(tool: Path, session: str, stimulus, first_frame: int, decay: float, size: float, macro, steps,
           macro_steps=None) -> np.ndarray:
    """Raw frames [first_frame, first_frame + len(stimulus)) of the engine for a job of a session."""
    host = FACTS["sessions"][session]
    frames = len(stimulus)
    lead = lead_frames(first_frame, steps)
    padded = np.zeros((lead + frames, 2), np.float32)
    padded[lead:] = stimulus
    scratch = OUT / "scratch" / uuid.uuid4().hex
    scratch.mkdir(parents=True)
    try:
        wavfile.write(scratch / "in.wav", SR, padded)
        del padded
        decay_engine, size_engine = engine_parameters(decay, size)
        command = [str(tool), "--input", str(scratch / "in.wav"), "--output", str(scratch / "out.wav"),
                   "--layer", "undertow", *(["--reference-arithmetic"] if REFERENCE_ARITHMETIC else []),
                   "--decay", repr(decay_engine), "--size", repr(size_engine), "--macro", repr(float(np.float32(macro))),
                   "--tempo", repr(host["tempo"]), "--playhead-frames", str(int(round(host["playHeadAheadSeconds"] * SR))),
                   "--host-block", str(BLOCK), "--block-starts", ",".join(str(int(step)) for step in steps),
                   "--first-frame", str(first_frame - lead), "--oscillator-origin", str(host["oscillatorOrigin"]),
                   "--phasor-origin", str(host["oscillatorOrigin"]), "--free-run-origin", str(host["oscillatorOrigin"])]
        if not host["transportRuns"]:
            command.append("--stopped")
        if macro_steps:
            command += ["--macro-steps", ",".join(f"{int(frame) - (first_frame - lead)}:{float(np.float32(value))!r}"
                                                  for frame, value in macro_steps)]
        subprocess.run(command, check=True)
        rate, output = wavfile.read(scratch / "out.wav")
        assert rate == SR and output.shape == (lead + frames, 2)
        latency = converters.reported_latency(SR)
        return np.asarray(output[lead - latency:lead - latency + frames], dtype=np.float64)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def settings_of(name: str, case: dict) -> tuple:
    """(Decay s, Size %, Macro) of a case, as score_tempo.py reads them from its name."""
    decay = 8.0 if "decay8" in name else 2.0 if ("decay2" in name or name.startswith("p") or "probe" in name) else 0.5
    if "decay0p5" in name or name.startswith(("z", "t0")) and "probe" not in name:
        decay = 0.5
    size = 60.0 if "size60" in name else 150.0 if "size150" in name else 100.0
    return decay, size, round(float(case.get("settings", {}).get("macro", 1.0)), 4)


def score(tool, session, name, stimulus=None, recording=None, first_frame=None, steps=None, setting=None) -> dict:
    case = cases(session).get(name, {})
    first_frame = int(case["firstFrame"]) if first_frame is None else first_frame
    stimulus = stimulus_of(session, name) if stimulus is None else stimulus
    recording = load(session, name) if recording is None else recording
    stimulus = np.asarray(stimulus[:len(recording)], dtype=np.float32)
    decay, size, macro = settings_of(name, case) if setting is None else setting
    steps = [first_frame - 10 * SR, first_frame - 2 * SR, first_frame] if steps is None else steps
    started = time.time()
    output = render(tool, session, stimulus, first_frame, decay, size, macro, steps)
    reference = np.asarray(recording, dtype=np.float64)
    return {"session": session, "case": name, "seconds": round(len(reference) / SR, 1), "T": round(first_frame / SR, 1),
            "macro": macro, "decay": decay, "size": size, "engine": round(null_db(output, reference), 2),
            "engine_s": round(time.time() - started, 1)}


def tone_steps(tool, session: str):
    """t03 of T90 and S90: eight gapless steps and a tail under one tone; every step starts its own grid of blocks."""
    listed = cases(session)
    parts = [name for name in listed if name.startswith("t03")]
    first = int(listed[parts[0]]["firstFrame"])
    stimulus = np.asarray(stimulus_of(session, parts[0]), dtype=np.float32)
    total = int(listed[parts[-1]]["firstFrame"]) + int(listed[parts[-1]]["frames"]) - first
    if len(stimulus) < total:              # the file holds the tone; the tail is recorded silence
        stimulus = np.concatenate([stimulus, np.zeros((total - len(stimulus), 2), np.float32)])
    steps = [first - 10 * SR, first - 2 * SR] + [int(listed[name]["firstFrame"]) for name in parts]
    output = render(tool, session, stimulus[:total], first, 0.5, 100.0, 1.0, steps)
    for name in parts:
        at = int(listed[name]["firstFrame"]) - first
        reference = np.asarray(load(session, name), dtype=np.float64)
        yield {"session": session, "case": name, "seconds": round(len(reference) / SR, 1), "T": round((first + at) / SR, 1),
               "macro": 1.0, "decay": 0.5, "size": 100.0, "engine": round(null_db(output[at:at + len(reference)], reference), 2)}


def macro_steps(tool):
    """Part S4 of session F: sixteen steps of Macro under a running tone, 20.25 s each, one render. Per step the
    null of the whole step and of its first 0.25 s (the gains in motion)."""
    listed = cases("F")
    parts = [name for name in listed if name.startswith("s4_steps_") and "macro" in name]
    first = int(listed[parts[0]]["firstFrame"])
    # every step plays the same file, a whole number of cycles of the tone long
    stimulus = np.concatenate([np.asarray(stimulus_of("F", name)[:int(listed[name]["frames"])], dtype=np.float32)
                               for name in parts])
    starts = [int(listed[name]["firstFrame"]) for name in parts]
    values = [float(listed[name]["settings"]["macro"]) for name in parts]
    steps = [first - 10 * SR, first - 2 * SR] + starts
    output = render(tool, "F", stimulus, first, 0.5, 100.0, values[0], steps, macro_steps=list(zip(starts, values)))
    for name, start, value in zip(parts, starts, values):
        at = start - first
        reference = np.asarray(load("F", name), dtype=np.float64)
        piece = output[at:at + len(reference)]
        moving = SR // 4
        yield {"session": "F", "case": name, "seconds": round(len(reference) / SR, 1), "T": round(start / SR, 1),
               "macro": round(value, 4), "decay": 0.5, "size": 100.0, "engine": round(null_db(piece, reference), 2),
               "engine_first_250ms": round(null_db(piece[:moving], reference[:moving]), 2)}


def group(tool, tag: str):
    if tag == "E":
        for name in ("c01_macro0_decay0p5_train", "c02_macro100_decay0p5_train", "c03_macro100_decay2_imp3",
                     "c05_macro100_decay2_noise", "c06_macro100_decay2_sine1k", "c10_macro75_decay2_imp3",
                     "c12_macro100_decay2_size60_imp3", "c14_macro100_decay8_imp3",
                     "x03_impulse_map_right_macro100_decay0p5", "x04_tone1k_120s_macro100_decay2"):
            yield score(tool, "E", name)
    elif tag in ("D", "FB"):
        session = "D" if tag == "D" else "F"
        for name in [n for n in cases(session) if n.startswith("c") and "ringout_1" not in n]:
            if name == "c15_ringout_0":
                ringout = np.load(folder(session) / "c15_ringout.stimulus.npy", mmap_mode="r")
                yield score(tool, session, name, stimulus=ringout, setting=(2.0, 100.0, 1.0))
            else:
                yield score(tool, session, name)
    elif tag == "FA":
        probe = np.load(folder("D") / "probe.stimulus.npy", mmap_mode="r")
        for number in sorted(int(path.stem[1:]) for path in folder("F").glob("p[0-9][0-9][0-9].npy")):
            if number == 0:
                continue                   # the instance was still in Tide
            yield score(tool, "F", f"p{number:03d}", stimulus=probe, first_frame=(10 + 30 * number) * SR,
                        setting=(2.0, 100.0, 1.0))
    elif tag == "FZ":
        for name in [n for n in cases("F") if n.startswith("z")]:
            yield score(tool, "F", name)
    elif tag in ("FS1", "FS3", "FS5", "FS6", "FS7"):
        for name in [n for n in cases("F") if n.startswith(tag[1:].lower() + "_")]:
            yield score(tool, "F", name)
    elif tag == "FS4":
        yield from macro_steps(tool)
    elif tag in ("T90", "S90"):
        for name in ("t01_macro0_decay0p5_train", "t02_macro100_decay0p5_train"):
            yield score(tool, tag, name)
        yield from tone_steps(tool, tag)
        yield score(tool, tag, "t04_scan_macro100_decay0p5")
        probe = np.load(folder(tag) / "probe.stimulus.npy", mmap_mode="r")
        yield score(tool, tag, "t05_end_probe", stimulus=probe, setting=(2.0, 100.0, 1.0))
    else:
        raise SystemExit(f"unknown group {tag}")


def holdout(tool):
    """Session F's holdout: three programme recordings nothing was tuned on. Loaded here and nowhere else."""
    listed = cases("F")
    for name in [n for n in listed if n[:2] in HOLDOUT]:
        case = listed[name]
        decay = {"decay2": 2.0, "decay4": 4.0, "decay1": 1.0}[name.split("_")[3]]
        macro = round(float(case["settings"]["macro"]), 4)
        session = folder("F")
        row = score(tool, "F", name, stimulus=np.load(session / case["stimulus"], mmap_mode="r"),
                    recording=np.load(session / f"{name}.npy", mmap_mode="r"), setting=(decay, 100.0, macro))
        row["holdout"] = True
        yield row


def stored() -> dict:
    return json.loads(SCORES.read_text()) if SCORES.exists() else {}


def table() -> None:
    rows = stored()
    model = json.loads(MODEL_SCORES.read_text()) if MODEL_SCORES.exists() else {}
    print("| Session | Recording | Processed time s | Length s | Macro | model | engine | engine, first 0.25 s |")
    print("|---|---|---|---|---|---|---|---|")
    for key in sorted(rows, key=lambda k: (rows[k]["session"], rows[k]["T"], rows[k]["case"], k)):
        row = rows[key]
        theirs = model.get(key, {}).get("host")
        note = " (holdout)" if row.get("holdout") else " (engine's own arithmetic)" if row.get("arithmetic") else ""
        print(f"| {row['session']} | {row['case']}{note} | {row['T']:.0f} | {row['seconds']:.0f} "
              f"| {row['macro']:.2f} | {'' if theirs is None else f'{theirs:.2f}'} | {row['engine']:.2f} "
              f"| {row.get('engine_first_250ms', '')} |")


def main() -> int:
    global REFERENCE_ARITHMETIC
    arguments = sys.argv[1:]
    tool = TOOL
    if "--product" in arguments:
        arguments.remove("--product")
        REFERENCE_ARITHMETIC = False
    if "--tool" in arguments:
        at = arguments.index("--tool")
        tool = Path(arguments[at + 1])
        del arguments[at:at + 2]
    if "--table" in arguments:
        table()
        return 0
    if not tool.exists():
        raise SystemExit(f"{tool} is missing: build the target AmanitaOceanFathomRender")
    OUT.mkdir(parents=True, exist_ok=True)
    if "--holdout" in arguments:
        jobs = [holdout(tool)]
    else:
        tags = arguments or ["E", "D", "FA", "FB", "FZ", "FS1", "FS3", "FS5", "FS6", "FS7", "FS4", "T90", "S90"]
        jobs = [group(tool, tag) for tag in tags]
    for job in jobs:
        for row in job:
            if not REFERENCE_ARITHMETIC:
                row["arithmetic"] = "product"
            print(json.dumps(row), flush=True)
            rows = stored()
            rows[f"{row['session']}/{row['case']}" + ("" if REFERENCE_ARITHMETIC else "#product")] = row
            SCORES.write_text(json.dumps(rows, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
