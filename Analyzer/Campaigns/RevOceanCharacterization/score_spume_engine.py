#!/usr/bin/env python3
"""The Spume engine against recordings of the reference in its Foam mode.

    python score_spume_engine.py [group ...]      groups: A AP AX AM T90 T90K T90M (default: all)
    python score_spume_engine.py --table          the table from the stored scores, beside the model's
    python score_spume_engine.py --tool <path>    another build of AmanitaOceanFathomRender (default: build-spume-dsp)

The engine is rendered by Tools/FathomRender.cpp (target AmanitaOceanFathomRender) with `--layer spume`. A job of a
recorded session is reproduced from what the session's files say: where its first frame lies in the instance's count
of frames, its Decay, Size and Macro as the host set them, and the origin of the instance's line oscillators (the
sample at which the reference was switched to Foam; foam/findings/FOAM_STATE.md, section 5, and FOAM_T90.md). What is
not in a session's own info.json is in foam/sessions.json. Nothing in Foam follows the host's tempo or position, so
the engine is told of neither.

Null = 10 log10 of the energy of (engine - recording) over the energy of the recording, both channels, the whole
recording, nothing fitted: no gain, no delay (revocean.null_db, formed here in pieces so that a recording of ten
minutes need not be held as double precision). The engine's frame n is the reference's raw frame n plus its reported
latency; the render starts some 480 frames of silence in front of the recording, at a frame where the two rates meet,
so that every recorded frame is covered. A recording is rendered in one piece, however long, from an empty diffuser
and network: every job of the sessions was preceded by ten seconds of silence.

Groups
    A     session A, the cases c01..c16 at fixed controls, Macro 0 to 100 %, among them the noise burst of c15
          (its ring-out, the second recording of c15, has Decay turned down at its start and is not scored)
    AP    session A, the probes that sounded Foam
    AX    session A, the long maps and sweeps x02, x03, x04, x06 (x01 is silence in, and nothing to null against)
    AM    session A, x05: eleven gapless steps of Macro under a running tone, one render
    T90   session T90 (another instance, another tempo): probes, f01..f04, the cases k00, k12..k16, n01, n02
    T90K  session T90, outer controls through the renderer's outer laws: Pre Delay (k01, k02) in the engine, the
          reference's Width (k05, k06) and Mix (k07) laws behind it
    T90M  session T90, m01: Macro in motion under a running tone, nine steps and a staircase of 81 plays, one render

Macro in motion. A value the host sets with the call that begins at host frame h is first used by the block of 44
internal samples foam/model/foam_model.py's first_block(h) names. That rule is the reference's handling of host
blocks and is not part of the engine: the renderer is handed every change with its block (`--macro-at-blocks`).

Not scored: the cases that turn a control of the reference the engine does not have. Those are its high-pass and
low-pass filters in front of the converter (k03, k04; Ocean's Low Cut and High Damping are filters of its own inside
the loop), Brightness (k08, k09), Ducking (k10) and Transients (k11).

The recordings and their stimuli are read from Analyzer/Results/RevOceanCharacterization/work/foam/session_<label>/
(not in Git); the scores are written beside them. The model's own numbers (foam/findings/model_scores.json) stand
beside the engine's in the table.
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
for path in (HERE, HERE / "abyss" / "model", HERE / "foam" / "model"):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

import converters  # noqa: E402
import foam_model  # noqa: E402
import reference_render as rr  # noqa: E402
import revocean  # noqa: E402

RECORDINGS = ROOT / "Analyzer/Results/RevOceanCharacterization/work/foam"       # the sessions; not in Git
FACTS = json.loads((HERE / "foam" / "sessions.json").read_text())
SR = FACTS["sampleRate"]
LEAD = 480                             # frames of silence rendered in front of a recording, at least
MEET = 160                             # host frames after which the two rates meet again at 48 kHz
TOOL = Path(os.environ.get("AMANITA_FATHOM_RENDER", ROOT / "build-spume-dsp" / "AmanitaOceanFathomRender"))
OUT = RECORDINGS / "engine"
SCORES = OUT / "scores.json"
MODEL_SCORES = HERE / "foam" / "findings" / "model_scores.json"
PIECE = 1 << 20                        # frames of a recording taken at a time


# ---------------------------------------------------------------- the sessions

def folder(session: str) -> Path:
    return RECORDINGS / f"session_{session}"


def info(session: str) -> dict:
    return json.loads((folder(session) / FACTS["sessions"][session]["info"]).read_text())


def cases(session: str) -> dict:
    """The cases of a session by name, from the info file its script wrote."""
    return {case["name"]: case for case in info(session)["cases"]}


def load(session: str, name: str):
    """A recording: raw frames from the case's first frame."""
    return np.load(folder(session) / f"{name}.npy", mmap_mode="r")


def stimulus_of(session: str, name: str):
    """The stimulus of a case; some cases share a file."""
    stem = cases(session).get(name, {}).get("stimulus") or f"{name}.stimulus.npy"
    return np.load(folder(session) / stem, mmap_mode="r")


def null_db(candidate, reference) -> float:
    """revocean.null_db of two arrays of the same length, taken in pieces."""
    assert len(candidate) == len(reference)
    difference_energy = reference_energy = 0.0
    for at in range(0, len(reference), PIECE):
        ours = np.asarray(candidate[at:at + PIECE], dtype=np.float64)
        theirs = np.asarray(reference[at:at + PIECE], dtype=np.float64)
        difference_energy += float(np.sum((ours - theirs) ** 2))
        reference_energy += float(np.sum(theirs ** 2))
    return float(10.0 * np.log10((difference_energy + 1e-300) / (reference_energy + 1e-300)))


def controls(settings: dict | None) -> dict:
    """What the reference works with for the normalised settings of a case; a probe has none and stands at
    Decay 2 s, Size 100 %, Macro 100 %."""
    if settings is None:
        settings = {"decay": revocean.CONTROL["decay"].normalised(2.0), "macro": 1.0}
    physical = rr.physical(settings)
    return {"decay": float(np.float32(physical["decay_seconds"])), "size": float(np.float32(physical["size_scale"])),
            "predelay": float(physical["predelay_seconds"]), "macro": float(np.float32(physical["macro"])),
            "width": float(physical["width_scale"]), "mix": float(physical["mix"])}


def lead_frames(first_frame: int) -> int:
    """Frames of silence rendered in front of a job, LEAD at least: the engine's first frame is one at which the
    two rates meet."""
    return LEAD + (first_frame - LEAD) % MEET


def render(tool: Path, session: str, stimulus, first_frame: int, knobs: dict, macro_blocks=None,
           outer: bool = False):
    """Raw frames [first_frame, first_frame + len(stimulus)) of the engine for a job of a session, as an array on
    disk and the folder that holds it; the caller removes the folder. With `outer` the reference's Width and Mix
    laws follow the engine."""
    frames = len(stimulus)
    lead = lead_frames(first_frame)
    scratch = OUT / "scratch" / uuid.uuid4().hex
    scratch.mkdir(parents=True)
    padded = np.zeros((lead + frames, 2), np.float32)
    padded[lead:] = stimulus
    wavfile.write(scratch / "in.wav", SR, padded)
    del padded
    command = [str(tool), "--input", str(scratch / "in.wav"), "--output", str(scratch / "out.wav"),
               "--layer", "spume", "--decay", repr(knobs["decay"]), "--size", repr(knobs["size"]),
               "--macro", repr(knobs["macro"]), "--predelay", repr(knobs["predelay"]),
               "--first-frame", str(first_frame - lead),
               "--oscillator-origin", str(FACTS["sessions"][session]["oscillatorOrigin"])]
    if macro_blocks:
        command += ["--macro-at-blocks", ",".join(f"{int(block)}:{float(np.float32(value))!r}"
                                                  for block, value in macro_blocks)]
    if outer:
        command += ["--width", repr(knobs["width"]), "--mix", repr(knobs["mix"])]
    subprocess.run(command, check=True)
    (scratch / "in.wav").unlink()
    rate, output = wavfile.read(scratch / "out.wav", mmap=True)
    assert rate == SR and output.shape == (lead + frames, 2)
    latency = converters.reported_latency(SR)
    return output[lead - latency:lead - latency + frames], scratch


def row_of(session: str, name: str, first_frame: int, frames: int, knobs: dict, engine: float, **more) -> dict:
    row = {"session": session, "case": name, "T": round(first_frame / SR, 1), "seconds": round(frames / SR, 1),
           "macro": round(knobs["macro"], 4), "decay": round(knobs["decay"], 3), "size": round(100.0 * knobs["size"], 1),
           "engine": round(engine, 2)}
    row.update(more)
    return row


def score(tool, session, name, stimulus=None, first_frame=None, settings="case", outer=False, note=None) -> dict:
    case = cases(session).get(name, {})
    first_frame = int(case["firstFrame"]) if first_frame is None else int(first_frame)
    recording = load(session, name)
    stimulus = stimulus_of(session, name) if stimulus is None else stimulus
    stimulus = np.asarray(stimulus[:len(recording)], dtype=np.float32)
    if len(stimulus) < len(recording):     # the file holds the sound; what follows is recorded silence
        stimulus = np.concatenate([stimulus, np.zeros((len(recording) - len(stimulus), 2), np.float32)])
    knobs = controls(case.get("settings") if settings == "case" else settings)
    assert outer or (knobs["width"] == 1.0 and knobs["mix"] == 1.0), f"{name} turns an outer control"
    started = time.time()
    output, scratch = render(tool, session, stimulus, first_frame, knobs, outer=outer)
    try:
        more = {"engine_s": round(time.time() - started, 1)}
        if outer:
            more["controls"] = note
        return row_of(session, name, first_frame, len(recording), knobs, null_db(output, recording), **more)
    finally:
        del output
        shutil.rmtree(scratch, ignore_errors=True)


def pieces(session: str, names) -> tuple:
    """Gapless recordings as one: (first frame, the frames each begins at within the whole, the whole)."""
    listed = cases(session)
    first = int(listed[names[0]]["firstFrame"])
    starts, at = [], first
    for name in names:
        assert int(listed[name]["firstFrame"]) == at, f"{name} does not follow the recording in front of it"
        starts.append(at - first)
        at += len(load(session, name))
    return first, starts, np.concatenate([np.asarray(load(session, name), dtype=np.float32) for name in names])


def macro_steps(tool):
    """x05 of session A: eleven gapless steps of Macro, 0 to 100 %, twenty seconds each under a running tone. One
    render with every change handed over at its block; per step the null of the whole step and of its first 0.2 s."""
    listed = cases("A")
    names = [name for name in listed if name.startswith("x05_macro_steps_tone1k_decay2_")]
    first, starts, recording = pieces("A", names)
    tone = np.asarray(stimulus_of("A", "x05_macro_steps_tone1k_decay2"), dtype=np.float32)
    stimulus = np.concatenate([tone[:len(load("A", name))] for name in names])    # every step plays the same file
    values = [float(listed[name]["settings"]["macro"]) for name in names]
    knobs = controls(listed[names[0]]["settings"])
    blocks = [(foam_model.first_block(first + start), value) for start, value in zip(starts[1:], values[1:])]
    output, scratch = render(tool, "A", stimulus, first, knobs, macro_blocks=blocks)
    try:
        moving = SR // 5
        yield row_of("A", "x05_macro_steps_tone1k_decay2", first, len(recording), knobs, null_db(output, recording),
                     note="eleven steps of Macro as one")
        for name, start, value in zip(names, starts, values):
            frames = len(load("A", name))
            piece, theirs = output[start:start + frames], recording[start:start + frames]
            yield row_of("A", name, first + start, frames, {**knobs, "macro": value}, null_db(piece, theirs),
                         engine_first=round(null_db(piece[:moving], theirs[:moving]), 2))
    finally:
        del output
        shutil.rmtree(scratch, ignore_errors=True)


def tone_steps(tool):
    """f04 of session T90: four gapless plays of one tone at Macro 100 % and the tail, as one."""
    names = [f"f04_tone_macro100_decay2_{index:02d}" for index in range(4)] + ["f04_tone_macro100_decay2_tail"]
    first, _, recording = pieces("T90", names)
    tone = np.asarray(np.load(folder("T90") / "f04_tone_macro100_decay2.stimulus.npy", mmap_mode="r"), dtype=np.float32)
    stimulus = np.concatenate([tone, np.zeros((len(recording) - len(tone), 2), np.float32)])
    knobs = controls(cases("T90")[names[0]]["settings"])
    output, scratch = render(tool, "T90", stimulus, first, knobs)
    try:
        yield row_of("T90", "f04_tone_macro100_decay2", first, len(recording), knobs, null_db(output, recording),
                     note="four plays and the tail as one")
    finally:
        del output
        shutil.rmtree(scratch, ignore_errors=True)


def macro_motion(tool):
    """m01 of session T90: Macro in motion under a running tone. Nine steps of 6.25 s, a staircase 0 -> 100 -> 0 %
    in 81 plays of 50 ms, six seconds of silence at Macro 0. One render; the null of the whole, of every step and
    of its first 0.25 s, of the staircase and of the tail."""
    plays = info("T90")["events"]["m01_macro_motion_decay2"]["plays"]
    names = [play["name"] for play in plays] + ["m01_90_tail"]
    first, starts, recording = pieces("T90", names)
    tone = np.asarray(np.load(folder("T90") / "m01_tone.stimulus.npy", mmap_mode="r"), dtype=np.float32)
    stimulus = np.concatenate([tone, np.zeros((len(recording) - len(tone), 2), np.float32)])
    assert [play["startFrameInTone"] for play in plays] == starts[:-1]
    values = [float(play["macro"]) for play in plays]
    knobs = controls(cases("T90")[names[0]]["settings"])
    blocks = [(foam_model.first_block(first + start), value) for start, value in zip(starts[1:-1], values[1:])]
    output, scratch = render(tool, "T90", stimulus, first, knobs, macro_blocks=blocks)
    try:
        moving = SR // 4
        yield row_of("T90", "m01_macro_motion_decay2", first, len(recording), knobs, null_db(output, recording),
                     note=f"{len(plays)} plays and the tail as one")
        steps = [index for index, play in enumerate(plays) if play["kind"] == "step"]
        for index in steps:
            start, frames = starts[index], int(plays[index]["frames"])
            piece, theirs = output[start:start + frames], recording[start:start + frames]
            yield row_of("T90", names[index], first + start, frames, {**knobs, "macro": values[index]},
                         null_db(piece, theirs), engine_first=round(null_db(piece[:moving], theirs[:moving]), 2))
        begin, end = starts[steps[-1] + 1], starts[-1]
        yield row_of("T90", "m01_staircase", first + begin, end - begin, knobs,
                     null_db(output[begin:end], recording[begin:end]), note="81 plays of 50 ms, 0 -> 100 -> 0 %")
        yield row_of("T90", "m01_90_tail", first + end, len(recording) - end, knobs,
                     null_db(output[end:], recording[end:]))
    finally:
        del output
        shutil.rmtree(scratch, ignore_errors=True)


def foam_probes(session: str) -> list:
    """Indices of the probes of a session that sounded Foam and were kept."""
    return [index for index in sorted(int(path.stem[1:]) for path in folder(session).glob("p[0-9][0-9][0-9].npy"))
            if index >= FACTS["sessions"][session]["firstFoamProbe"]]


def group(tool, tag: str):
    if tag == "A":
        for name in [n for n in cases("A") if n.startswith("c") and "ringout" not in n]:
            yield score(tool, "A", name)
        burst = np.load(folder("A") / "c15_ringout.stimulus.npy", mmap_mode="r")
        yield score(tool, "A", "c15_ringout_0", stimulus=burst, settings=None)
    elif tag == "AP":
        probe = np.load(folder("A") / "probe.stimulus.npy", mmap_mode="r")
        for index in foam_probes("A"):
            yield score(tool, "A", f"p{index:03d}", stimulus=probe, first_frame=(10 + 30 * index) * SR, settings=None)
    elif tag == "AX":
        for name in ("x02_impulse_map_left_macro100_decay0p5", "x03_impulse_map_right_macro100_decay0p5",
                     "x04_tone1k_120s_macro100_decay2", "x06_sweep_50_to_12k_macro100_decay2"):
            yield score(tool, "A", name)
    elif tag == "AM":
        yield from macro_steps(tool)
    elif tag == "T90":
        probe = np.load(folder("T90") / "probe.stimulus.npy", mmap_mode="r")
        first_frames = {entry["index"]: int(entry["firstFrame"]) for entry in info("T90")["probes"]}
        for index in foam_probes("T90"):
            yield score(tool, "T90", f"p{index:03d}", stimulus=probe, first_frame=first_frames[index], settings=None)
        for name in ("f01_macro0_decay0p5_train", "f02_macro100_decay0p5_train", "f03_scan_macro100_decay0p5"):
            yield score(tool, "T90", name)
        yield from tone_steps(tool)
        for name in ("k00_neutral", "k12_size30", "k13_size200", "k14_decay20", "k15_macro50", "k16_neutral_again",
                     "n01_noise_macro100_decay2", "n02_noise_macro0_decay2", "end_probe"):
            yield score(tool, "T90", name)
    elif tag == "T90K":
        for name, note in (("k01_predelay50ms", "Pre Delay 50 ms"), ("k02_predelay200ms", "Pre Delay 200 ms"),
                           ("k05_width0", "Width 0 %, the reference's law"),
                           ("k06_width150", "Width 150 %, the reference's law"),
                           ("k07_mix50", "Mix 50 %, the reference's law")):
            yield score(tool, "T90", name, outer=True, note=note)
    elif tag == "T90M":
        yield from macro_motion(tool)
    else:
        raise SystemExit(f"unknown group {tag}")


def stored() -> dict:
    return json.loads(SCORES.read_text()) if SCORES.exists() else {}


def table() -> None:
    rows = stored()
    model = json.loads(MODEL_SCORES.read_text())["rows"]
    print("| Session | Recording | Processed time s | Length s | Macro % | Decay s | Size % | model | engine "
          "| model, onset | engine, onset | |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for key in sorted(rows, key=lambda k: (rows[k]["session"], rows[k]["T"], -rows[k]["seconds"], rows[k]["case"])):
        row = rows[key]
        theirs = model.get(key, {})
        ours_first = row.get("engine_first")
        theirs_first = theirs.get("first200ms", theirs.get("first250ms"))
        print(f"| {row['session']} | {row['case']} | {row['T']:.0f} | {row['seconds']:.1f} | {100 * row['macro']:.1f} "
              f"| {row['decay']:.1f} | {row['size']:.0f} | {'' if 'model' not in theirs else format(theirs['model'], '.1f')} "
              f"| {row['engine']:.1f} | {'' if theirs_first is None else format(theirs_first, '.1f')} "
              f"| {'' if ours_first is None else format(ours_first, '.1f')} | {row.get('controls') or row.get('note') or ''} |")


def main() -> int:
    arguments = sys.argv[1:]
    tool = TOOL
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
    for tag in arguments or ["A", "AP", "AX", "AM", "T90", "T90K", "T90M"]:
        for row in group(tool, tag):
            print(json.dumps(row), flush=True)
            rows = stored()
            rows[f"{row['session']}/{row['case']}"] = row
            SCORES.write_text(json.dumps(rows, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
