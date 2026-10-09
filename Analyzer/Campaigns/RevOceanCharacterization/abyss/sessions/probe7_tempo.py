#!/usr/bin/env python3
"""Attended TEMPO session with the reference's Audio Unit: are the chunks of Abyss seconds or note values, and do its
chunk clocks count processed frames or follow the host's play head?

    probe7_tempo.py <label> <bpm> [transport offset in seconds] [--block N]
    probe7_tempo.py --list            prints the cases with durations and the wall-time budget; opens nothing
    probe7_tempo.py <label> <bpm> [offset] --unattended
                                      dry run that needs nobody: the host starts by itself after 3 s, the sound stays
                                      Tide, and the script takes the third probe for Abyss so that the whole programme
                                      runs once against the real host (about a minute; the recordings are Tide's)

One instance has one tempo, so every tempo is a session of its own. Every session so far told the plug-in 120 BPM and
a play head at "frames processed": there 2/3 s, 1 s and 4/3 s (the three chunk lengths of Abyss) are also 1/3, 1/2 and
2/3 of a bar. At 90 BPM a chunk that is a note value becomes 8/9 s, 4/3 s and 16/9 s; one that is seconds stays.
The transport offset is the play head's position when the instance has processed nothing: a clock that follows the
play head moves its chunk boundaries by it, a clock that counts processed frames does not.

What the owner does (the window shows INSTRUCTION): Demo, Start, then the mode. A mode chosen BEFORE Start does not
reach the sound (session E), so the script runs session D's probe over and over from Start on and waits until it hears
Abyss (the 400..600 Hz share of the left channel under the 1 kHz tone rises from -85 dB to -5 dB; threshold -40 dB,
as in probe5.py) in two probes in a row. While it waits, the job names in the window end in HINT after a while.

Then, at Decay 0.5 s (the reference's minimum, so the network is short and the layer is read almost directly):

  t01  Macro 0, 20 left impulses 1 s apart (c01 of sessions D and E): the base alone. Gives the oscillator origin and
       shows whether tempo or offset touch the base.
  t02  the same at Macro 100 % (c02 of sessions D and E).
  t03  a steady tone, 1 kHz left and 3 kHz right, 128 s at Macro 100 % in eight steps without a gap, then 6 s of
       recorded silence. Every chunk boundary of every voice shows in it. Each step is 16 s plus an eighth of a host
       block long, so the host's block grid (which starts anew with every step) moves by an eighth of a block from
       step to step: the "late chunks" of session D hang on where a boundary falls in a host block.
  t04  120 isolated impulses of 0.5, left and right in turn, one every 4 s + 1/30 s: a fine time grid (33 ms; 67 ms
       per side) across the 4 s frame in which the three clocks repeat at 120 BPM. Each impulse gives the mirror
       point of its chunk in every voice, so chunk lengths and their phase are read off without a model.
  t05  session D's probe once more: is the sound still Abyss?

Budget: everything behind the first Abyss probe is under 90 s of wall time at 15 times real time (--list prints it).
Before every case the wall time since the creation of the instance is checked against the host's stop at 19 minutes.
A case that fails on the driver's side (a read-back, a file) is noted under "failed" in info.json and the session goes
on with the next one as long as the host is running.

Output: session_<label>/ next to this script: one .npy per recording (float32, (frames, 2), 48 kHz, raw: the 48
samples of reported latency are inside), the stimuli, and info.json, rewritten after every recording. Until Abyss is
confirmed the flush and the pre-roll of every probe are recorded too (the same frames, only written down), and the two
probes in front of the first Abyss probe are kept with them: the change of mode lies in those files.

First reading afterwards: analysis2/tempo_quicklook.py <label>.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import hashlib
import json
import re
import sys
import time
from pathlib import Path

import numpy as np

CAMPAIGN = Path("/Users/nespesha/Workspace/AmanitaOcean/Analyzer/Campaigns/RevOceanCharacterization")
sys.path.insert(0, str(CAMPAIGN))
import revocean as ro                 # noqa: E402
import revocean_session as rs         # noqa: E402

HERE = Path(__file__).resolve().parent
SR = 48000
STOP_MINUTES = 19.0           # the host ends the session this long after the creation of the instance
MARGIN_SECONDS = 45.0         # nothing starts that would end later than this in front of the host's stop
RATE = 15.0                   # processed seconds per wall second the budget assumes (sessions D and E: 13 to 37)
BUDGET_WALL_SECONDS = 90.0    # everything behind the first Abyss probe, at RATE
ABYSS_DB = -40.0              # 400..600 Hz share of the left channel: Tide -83 to -88 dB, Abyss -4 to -7 dB
CONFIRMATIONS = 2             # Abyss probes in a row before the programme starts
HINT_AFTER = 12               # probes without Abyss before the job names carry HINT
BLOCKS = (64, 128, 256, 512, 1024, 2048)
STOPPED = False   # set by --stopped: the play head reports a stopped transport
# The window shows two lines of the instruction, about 160 characters, and under it the name of the running job.
INSTRUCTION_LIMIT = 150
INSTRUCTION = "Press Demo, then Start without touching the mode, then switch the mode to Abyss with the arrows and leave it."
# If the label already reads Abyss and the sound is not (a click in front of the first processed block, as in session
# E), only switching away and back helps: the hint says both. Letters, digits and _ only (it names a folder).
HINT = "_NOT_ABYSS_YET_switch_the_mode_to_Abyss_or_away_and_back_to_it"
INSTRUCTION_UNATTENDED = "Automatic dry run of the tempo script in the mode Tide. Please do not touch this window; it closes by itself."
PRETEND_FROM = 2              # dry run: probes from this index on count as Abyss

TONE_STEPS = 8                # steps of the tone; the host's block grid moves by an eighth of a block per step
TONE_STEP_SECONDS = 16.0      # 24 chunks of 2/3 s, 16 of 1 s, 12 of 4/3 s per step at 120 BPM
TONE_TAIL_SECONDS = 6.0
SCAN_IMPULSES = 120
SCAN_SPACING_FRAMES = 4 * SR + SR // 30      # 4 s + 33.3 ms
SCAN_SECONDS = 486.0


def at(seconds: float) -> int:
    return int(round(seconds * SR))


def S(decay: float, macro: float = 0.0, size: float = 100.0) -> dict:
    return {"decay": ro.normalised("decay", decay), "macro": macro, "size": ro.normalised("size", size)}


# ---------------------------------------------------------------- stimuli (nothing here depends on the tempo)

def probe_stimulus() -> np.ndarray:
    """Session D's probe (probe3.py): 1 kHz left and 3 kHz right from 0.5 to 8 s, impulses at 10 s (left) and 13 s (right)."""
    t = np.arange(at(7.5)) / SR
    fade = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    probe = np.zeros((at(20), 2), np.float32)
    probe[at(0.5):at(8.0), 0] = 0.2 * np.sin(2 * np.pi * 1000 * t) * fade
    probe[at(0.5):at(8.0), 1] = 0.2 * np.sin(2 * np.pi * 3000 * t) * fade
    probe[at(10.0), 0] = 0.5
    probe[at(13.0), 1] = 0.5
    return probe


def train_stimulus() -> np.ndarray:
    """c01 and c02 of sessions D and E: 20 impulses of 0.5 on the left, 1 s apart from 0.25 s on; 24 s."""
    train = np.zeros((at(24), 2), np.float32)
    for k in range(20):
        train[at(0.25 + k), 0] = 0.5
    return train


def tone_stimulus(block: int) -> tuple:
    """(whole tone, list of (start frame, frames) per step). 1 kHz left, 3 kHz right, amplitude 0.2, 20 ms fades at
    the two ends only; the steps are slices of one array, so they join without a click."""
    step = at(TONE_STEP_SECONDS) + block // 8
    frames = TONE_STEPS * step
    t = np.arange(frames) / SR
    window = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    tone = np.zeros((frames, 2), np.float32)
    tone[:, 0] = 0.2 * np.sin(2 * np.pi * 1000 * t) * window
    tone[:, 1] = 0.2 * np.sin(2 * np.pi * 3000 * t) * window
    return tone, [(index * step, step) for index in range(TONE_STEPS)]


def scan_stimulus() -> tuple:
    """(stimulus, table of impulses). Impulse n at 1 s + n * (4 s + 1/30 s), left for even n, right for odd n."""
    x = np.zeros((at(SCAN_SECONDS), 2), np.float32)
    table = []
    for n in range(SCAN_IMPULSES):
        frame = at(1.0) + n * SCAN_SPACING_FRAMES
        x[frame, n % 2] = 0.5
        table.append({"n": n, "frame": int(frame), "seconds": frame / SR, "channel": n % 2, "amplitude": 0.5})
    return x, table


_F = np.fft.rfftfreq(at(6), 1 / SR)


def _share(P: np.ndarray, lo: float, hi: float) -> float:
    return float(10 * np.log10(P[(_F >= lo) & (_F < hi)].sum() / P.sum() + 1e-30))


def describe(y: np.ndarray) -> dict:
    """The descriptors of probe3.py and probe5.py, unchanged, so that the log reads like sessions D and E."""
    y = y.astype(np.float64)
    PL = np.abs(np.fft.rfft(y[at(1.5):at(7.5), 0] * np.hanning(at(6)))) ** 2
    PR = np.abs(np.fft.rfft(y[at(1.5):at(7.5), 1] * np.hanning(at(6)))) ** 2
    h = y[at(10.0):at(13.0), 0]

    def e(a: float, b: float) -> float:
        return float(10 * np.log10((h[at(a):at(b)] ** 2).sum() + 1e-30))

    return {"L1k_within3Hz": round(_share(PL, 997, 1003), 1), "L_400to600": round(_share(PL, 400, 600), 1),
            "L_below900": round(_share(PL, 20, 900), 1), "L_above1100": round(_share(PL, 1100, 20000), 1),
            "R_1400to1600": round(_share(PR, 1400, 1600), 1), "R_below2700": round(_share(PR, 20, 2700), 1),
            "imp_0to0p3": round(e(0, 0.3), 1), "imp_0p3to1": round(e(0.3, 1.0), 1), "imp_1to3": round(e(1.0, 3.0), 1),
            "level": round(float(10 * np.log10((y ** 2).mean() + 1e-30)), 2)}


# ---------------------------------------------------------------- jobs

@dataclasses.dataclass
class Case:
    name: str
    steps: list                      # rs.Step, in order
    names: list                      # one recording name per recorded step
    stimuli: dict                    # stem -> array, saved as <stem>.stimulus.npy
    note: str
    events: dict | None = None       # written into info.json under "events"


def recorded_all(steps: list) -> list:
    """The same steps, frame for frame, with the silences recorded too (recording does not touch the processing)."""
    return [dataclasses.replace(step, record=True) if step.kind == "silence" else step for step in steps]


def probe_steps(record_flush: bool) -> list:
    steps = rs.case(probe_stimulus(), S(2.0, 1.0), SR)
    return recorded_all(steps) if record_flush else steps


def programme(block: int) -> list:
    """The cases behind the confirmed switch, in order of value per second."""
    train = train_stimulus()
    tone, slices = tone_stimulus(block)
    scan, impulses = scan_stimulus()
    tone_steps = rs.flush(S(0.5, 1.0), SR) + [rs.play(tone[start:start + frames]) for start, frames in slices] \
        + [rs.silence(at(TONE_TAIL_SECONDS), record=True)]
    tone_names = [f"t03_tone_macro100_decay0p5_{index:02d}" for index in range(TONE_STEPS)] + ["t03_tone_macro100_decay0p5_tail"]
    return [
        Case("t01_macro0_decay0p5_train", rs.case(train, S(0.5, 0.0), SR), ["t01_macro0_decay0p5_train"],
             {"t01_macro0_decay0p5_train": train},
             "Macro 0: 20 left impulses 1 s apart (c01 of sessions D and E). The base alone: oscillator origin, and whether tempo or offset touch it"),
        Case("t02_macro100_decay0p5_train", rs.case(train, S(0.5, 1.0), SR), ["t02_macro100_decay0p5_train"],
             {"t02_macro100_decay0p5_train": train},
             "Macro 100 %: the same train (c02 of sessions D and E)"),
        Case("t03_tone_macro100_decay0p5", tone_steps, tone_names, {"t03_tone_macro100_decay0p5": tone},
             f"1 kHz left / 3 kHz right for {len(tone) / SR:.2f} s in {TONE_STEPS} gapless steps of 16 s + {block // 8} frames "
             f"(the host block grid moves an eighth of a block per step), then {TONE_TAIL_SECONDS:.0f} s of silence",
             {"steps": [{"index": index, "startFrameInTone": int(start), "frames": int(frames),
                         "gridShiftFrames": int((index * (block // 8)) % block)} for index, (start, frames) in enumerate(slices)],
              "leftHz": 1000.0, "rightHz": 3000.0, "amplitude": 0.2, "fadeSeconds": 0.02}),
        Case("t04_scan_macro100_decay0p5", rs.case(scan, S(0.5, 1.0), SR), ["t04_scan_macro100_decay0p5"],
             {"t04_scan_macro100_decay0p5": scan},
             f"{SCAN_IMPULSES} impulses of 0.5, left and right in turn, every 4 s + 1/30 s: a 33 ms grid across the 4 s frame",
             {"spacingFrames": SCAN_SPACING_FRAMES, "impulses": impulses}),
        Case("t05_end_probe", probe_steps(False), ["t05_end_probe"], {"probe": probe_stimulus()},
             "session D's probe once more: is the sound still Abyss?"),
    ]


def audio_frames(steps: list) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus) for step in steps if step.kind != "set")


def recorded_frames(steps: list) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus)
               for step in steps if step.kind != "set" and step.record)


def stimulus_peak(steps: list) -> float:
    return max((float(np.abs(step.stimulus).max()) for step in steps if step.kind == "wav"), default=0.0)


# ---------------------------------------------------------------- --list

def list_programme(bpm: float | None, offset: float, block: int, out=sys.stdout) -> bool:
    """Prints every case; True when the budget holds and the stimuli are in order."""
    ok = True
    probe = probe_steps(True)
    probe_seconds = audio_frames(probe) / SR
    print(f"{'case':34} {'audio s':>8} {'recorded':>9} {'peak':>6} {'behind the switch, s':>21} {'wall s at %gx' % RATE:>14}", file=out)
    print(f"{'p000, p001, ... (until Abyss)':34} {probe_seconds:8.2f} {recorded_frames(probe) / SR:9.2f} {stimulus_peak(probe):6.3f} "
          f"{'-':>21} {'-':>14}   session D's probe, Decay 2 s, Macro 100 %; flush and pre-roll recorded too", file=out)
    total = CONFIRMATIONS * probe_seconds
    print(f"{f'the {CONFIRMATIONS} Abyss probes that confirm':34} {CONFIRMATIONS * probe_seconds:8.2f} "
          f"{CONFIRMATIONS * recorded_frames(probe) / SR:9.2f} {stimulus_peak(probe):6.3f} {total:21.2f} {total / RATE:14.1f}", file=out)
    recorded = 0.0
    for case in programme(block):
        seconds, peak = audio_frames(case.steps) / SR, stimulus_peak(case.steps)
        if peak > rs.MAX_INPUT:
            ok = False
            print(f"PEAK TOO HIGH in {case.name}: {peak}", file=out)
        if len(case.names) != sum(1 for step in case.steps if step.kind != "set" and step.record):
            ok = False
            print(f"NAMES AND RECORDED STEPS DIFFER in {case.name}", file=out)
        total += seconds
        recorded += recorded_frames(case.steps) / SR
        print(f"{case.name:34} {seconds:8.2f} {recorded_frames(case.steps) / SR:9.2f} {peak:6.3f} {total:21.2f} {total / RATE:14.1f}   {case.note}", file=out)
    budget = BUDGET_WALL_SECONDS * RATE
    fits = total <= budget
    ok &= fits
    print(file=out)
    print(f"behind the switch: {total:.2f} s of audio = {total / RATE:.1f} s of wall time at {RATE:g}x "
          f"({'within' if fits else 'OVER'} the budget of {BUDGET_WALL_SECONDS:.0f} s = {budget:.0f} s of audio); "
          f"{recorded:.0f} s recorded behind the probes, about {recorded * SR * 8 * 2 / 1e9:.2f} GB with the stimuli", file=out)
    tone, slices = tone_stimulus(block)
    shifts = sorted({(index * (block // 8)) % block for index in range(TONE_STEPS)})
    print(f"tone: {TONE_STEPS} steps of {slices[0][1]} frames; host block grid of step i starts {block // 8} * i frames "
          f"later than a grid of {block} that runs through: shifts {shifts}", file=out)
    for name, built in (("probe.stimulus.npy", probe_stimulus()), ("c01_macro0_decay0p5_train.stimulus.npy", train_stimulus())):
        saved = HERE / "session_D" / name
        if saved.exists():
            same = bool(np.array_equal(np.load(saved), built))
            ok &= same
            print(f"session_D/{name} is this script's stimulus bit for bit: {same}", file=out)
    length = len(INSTRUCTION)
    ok &= length <= INSTRUCTION_LIMIT
    print(f"instruction in the window ({length} characters, the window shows about {INSTRUCTION_LIMIT}): {INSTRUCTION}", file=out)
    print(f"job names while the sound is not Abyss, from probe {HINT_AFTER} on: p{HINT_AFTER:03d}{HINT}", file=out)
    if bpm is not None:
        print(f"at {bpm:g} BPM, transport offset {offset:g} s, host block {block}:", file=out)
        for voice, seconds in (("same pitch", 2.0 / 3.0), ("octave up", 1.0), ("octave down", 4.0 / 3.0)):
            print(f"  {voice:11} chunk: {seconds:.6f} s if it is seconds, {seconds * 120.0 / bpm:.6f} s if it is a note value "
                  f"({seconds / 2.0:.4f} of a 4/4 bar)", file=out)
        if bpm < 88.0:
            print("  NOTE: below 88 BPM a layer in note values answers an impulse for longer than the 4.03 s between two "
                  "impulses of t04; the copies still carry their mirror points, but they overlap", file=out)
    return ok


# ---------------------------------------------------------------- the session

def sha256(array: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(array, dtype=np.float32).tobytes()).hexdigest()


def write_json(path: Path, data) -> None:
    partial = path.with_name(path.name + ".partial")
    partial.write_text(json.dumps(data, indent=1, default=str))
    partial.replace(path)


def entry(name: str, rec, stimulus_file: str | None) -> dict:
    y = rec.output
    return {"name": name, "firstFrame": int(rec.first_frame), "warmupSeconds": rec.warmup_seconds, "latency": int(rec.latency),
            "settings": rec.settings, "frames": int(len(y)), "peak": float(np.abs(y).max()) if len(y) else 0.0,
            "sha256": sha256(y), "stimulus": stimulus_file,
            "secondsSinceCreation": rec.step.get("secondsSinceCreation"), "stepWallSeconds": rec.step.get("wallSeconds")}


def run_session(label: str, bpm: float, offset: float, block: int, attended: bool = True) -> int:
    out = HERE / f"session_{label}"
    instruction = INSTRUCTION if attended else INSTRUCTION_UNATTENDED
    if out.exists() and any(out.iterdir()):
        print(f"{out} already holds a session; choose another label", file=sys.stderr)
        return 2
    if len(instruction) > INSTRUCTION_LIMIT:
        print("the instruction is too long for the window", file=sys.stderr)
        return 2
    cases = programme(block)             # built before the window opens: a mistake in a stimulus costs nobody a sitting
    probe = probe_stimulus()
    out.mkdir(parents=True, exist_ok=True)

    session = rs.Session.open(instruction=instruction, sample_rate=SR, block_size=block, plugin_format="AudioUnit",
                              bpm=bpm, transport_offset_seconds=offset, start_after=None if attended else 3.0,
                              transport_playing=False if STOPPED else None)
    print("window open:", session.folder, flush=True)
    print("instruction in the window:", instruction, flush=True)
    info = {"label": label, "script": "probe7_tempo.py", "variant": "attended" if attended else "unattended dry run (Tide; Abyss is pretended)",
            "bpm": bpm, "transportOffsetSeconds": offset, "blockSize": block, "transportPlaying": not STOPPED,
            "sampleRate": SR, "instruction": instruction, "sessionFolder": str(session.folder),
            "chunkSecondsIfSeconds": [2.0 / 3.0, 1.0, 4.0 / 3.0],
            "chunkSecondsIfNoteValues": [2.0 / 3.0 * 120.0 / bpm, 120.0 / bpm, 4.0 / 3.0 * 120.0 / bpm],
            "firstAbyssProbe": None, "confirmedAtProbe": None, "probes": [], "cases": [], "events": {}, "skipped": [],
            "failed": [], "endProbeAbyss": None, "outcome": "running",
            "planned": [case.name for case in cases]}

    def save_info() -> None:
        write_json(out / "info.json", info)

    pictures = {"possible": True}

    def picture(name: str) -> None:
        """The only record of the mode's label. Never worth a session: screencapture can be refused or hang (the
        driver gives it 30 s), so a failure is printed and no second picture is tried."""
        if not pictures["possible"]:
            return
        try:
            if not session.screenshot(session.folder / name):
                pictures["possible"] = False
                print(f"no picture {name} (the terminal may not record the screen)", flush=True)
        except Exception as problem:
            pictures["possible"] = False
            print(f"no picture {name}: {type(problem).__name__}: {problem}", flush=True)

    def keep(name: str, rec, stimulus_file: str | None, where: str = "cases") -> dict:
        np.save(out / f"{name}.npy", rec.output.astype(np.float32, copy=False))
        line = entry(name, rec, stimulus_file)
        info[where].append(line)
        return line

    def keep_probe(index: int, recordings: list) -> None:
        """A probe with its recorded flush and pre-roll, when it has them."""
        names = [f"p{index:03d}_flush", f"p{index:03d}_preroll", f"p{index:03d}"][-len(recordings):]
        for name, rec in zip(names, recordings):
            if not (out / f"{name}.npy").exists():
                np.save(out / f"{name}.npy", rec.output.astype(np.float32, copy=False))
                info.setdefault("probeFiles", []).append({"name": name, "firstFrame": int(rec.first_frame), "frames": int(len(rec.output)),
                                                          "sha256": sha256(rec.output)})

    save_info()
    created = None
    try:
        record = session.record()
        created = datetime.datetime.fromisoformat(record["instanceCreatedAt"])
        info["instanceCreatedAt"] = created.isoformat()
        # The host must have taken the tempo and the offset: a host built before they existed would run at 120 BPM
        # without a word, and the sitting would be lost.
        told = (float(record.get("bpm", 120.0)), float(record.get("transportOffsetSeconds", 0.0)))
        info["hostReports"] = {"bpm": told[0], "transportOffsetSeconds": told[1], "blockSize": record.get("blockSize")}
        if abs(told[0] - bpm) > 1e-9 or abs(told[1] - offset) > 1e-9 or int(record.get("blockSize", block)) != block:
            raise RuntimeError(f"the session host reports {told[0]} BPM, offset {told[1]} s, block {record.get('blockSize')}: "
                               f"not what was asked for ({bpm}, {offset}, {block}); is build-session up to date?")

        def seconds_left() -> float:      # until the host stops by itself
            return STOP_MINUTES * 60.0 - (datetime.datetime.now(created.tzinfo) - created).total_seconds()

        # as long as a probe could still start (the loop below has the same limit); never less than half a minute
        session.wait_until_started(max(30.0, seconds_left() - MARGIN_SECONDS - 15.0))
        started = time.monotonic()
        print("started", time.strftime("%H:%M:%S"), "- seconds left:", round(seconds_left()), flush=True)
        np.save(out / "probe.stimulus.npy", probe)

        # ---- identical probes until Abyss has been heard CONFIRMATIONS times in a row
        k, row, first, recent, complete = 0, 0, None, [], True
        while row < CONFIRMATIONS:
            if seconds_left() < MARGIN_SECONDS + 15.0:
                complete = False
                print("OUT OF TIME in the probes", flush=True)
                break
            hint = HINT if attended and row == 0 and k >= HINT_AFTER else ""      # the window shows the name of the running job
            recordings = session.run(probe_steps(True), label=f"p{k:03d}{hint}", timeout=180.0).recordings
            rec = recordings[-1]
            d = describe(rec.output)
            d.update(index=k, wallSeconds=round(time.monotonic() - started, 1), warmupSeconds=rec.warmup_seconds,
                     firstFrame=int(rec.first_frame), sha256=sha256(rec.output), abyss=bool(d["L_400to600"] > ABYSS_DB))
            if not attended and k >= PRETEND_FROM:
                d.update(abyss=True, pretended=True)          # dry run: the sound is Tide; go on as if it were Abyss
            info["probes"].append(d)
            print(json.dumps(d), flush=True)
            if d["abyss"]:
                row += 1
                if first is None:
                    first = k
                    info["firstAbyssProbe"] = k
                    for index, earlier in recent:             # the change of mode lies in these
                        keep_probe(index, earlier)
                    print(f"ABYSS from probe {k}", flush=True)
                keep_probe(k, recordings)
            else:
                if row:
                    print(f"WARNING: probe {k} does not sound like Abyss any more; waiting again", flush=True)
                row = 0
                if k == 0 or k % 10 == 0:
                    keep_probe(k, recordings[-1:])
                if attended and first is None and k == HINT_AFTER - 1:
                    print("Abyss not heard yet: switch the mode to Abyss with the arrows, or away and back if the label already reads Abyss "
                          "(from now on the job names in the window say so)", flush=True)
            recent = (recent + [(k, recordings)])[-2:]
            save_info()
            k += 1
        info["probesRun"] = k
        if complete:
            info["confirmedAtProbe"] = k - 1
        save_info()
        picture("window-after-probes.png")

        # ---- the programme
        for case in cases:
            if not complete:
                break
            seconds = audio_frames(case.steps) / SR
            left = seconds_left()
            if left < MARGIN_SECONDS + 1.5 * seconds / RATE:
                print(f"SKIPPED {case.name}: {seconds:.0f} s of audio, {left:.0f} s left", flush=True)
                info["skipped"].append({"name": case.name, "secondsLeft": round(left, 1)})
                save_info()
                continue
            begun = time.monotonic()
            try:
                recordings = session.run(case.steps, label=case.name, timeout=300.0 + seconds).recordings
                if len(recordings) != len(case.names):
                    raise RuntimeError(f"{case.name}: {len(recordings)} recordings for {len(case.names)} names")
                files = {}
                for stem, array in case.stimuli.items():
                    files[stem] = f"{stem}.stimulus.npy"
                    if not (out / files[stem]).exists():
                        np.save(out / files[stem], array)
                stimulus_file = next(iter(files.values()))
                for name, rec in zip(case.names, recordings):
                    keep(name, rec, stimulus_file)
                if case.events is not None:
                    info["events"][case.name] = case.events
            except Exception as problem:
                # One case must not cost the rest of the instance (as in probe4.py). The driver refuses a job for
                # reasons that leave the instance as it is (a read-back, a file it cannot read, a job the host marks
                # as failed); the cases behind it show their own firstFrame, so nothing has to be assumed about how
                # much of the refused one was processed. A host that has stopped, or that does not answer in time,
                # ends the session.
                if isinstance(problem, TimeoutError) or not session.alive() or session.record().get("status") != "running":
                    raise
                failure = {"name": case.name, "error": f"{type(problem).__name__}: {problem}",
                           "secondsLeft": round(seconds_left(), 1), "hostResults": str(session.folder / "results")}
                info["failed"].append(failure)
                save_info()
                print(f"FAILED {case.name}: {failure['error']}; what the host recorded of it stays under "
                      f"{failure['hostResults']}; the session goes on", flush=True)
                continue
            if case.name == "t05_end_probe":
                d = describe(recordings[-1].output)
                info["endProbeAbyss"] = bool(d["L_400to600"] > ABYSS_DB)
                info["endProbe"] = d
                print("end probe:", json.dumps(d), "- still Abyss" if info["endProbeAbyss"]
                      else ("- NOT ABYSS ANY MORE: what was recorded is in doubt" if attended else "- Tide, as a dry run is"), flush=True)
            save_info()
            print(f"{case.name} ok: {seconds:.0f} s of audio in {time.monotonic() - begun:.1f} s, "
                  f"peak {max(float(np.abs(rec.output).max()) for rec in recordings):.4f}, seconds left {round(seconds_left())}", flush=True)
        done = {line["name"] for line in info["cases"]}
        missing = [name for case in cases for name in case.names if name not in done]
        info["notRun"] = missing
        info["outcome"] = "complete" if complete and not missing else ("Abyss never confirmed" if not complete else "cut short")
        info["wallSecondsFromStart"] = round(time.monotonic() - started, 1)
        picture("window-end.png")
    except BaseException as problem:      # the demo limit, a closed window, Ctrl-C: keep what there is
        info["outcome"] = f"stopped: {type(problem).__name__}: {problem}"
        print("STOPPED:", info["outcome"], flush=True)
        raise
    finally:
        save_info()                       # before the window is closed: the outcome is on disk whatever closing does
        try:
            info["record"] = session.close()
        except Exception as problem:
            info["record"] = {"error": f"closing the session failed: {type(problem).__name__}: {problem}"}
        save_info()
        print("session closed:", str(info["record"])[:200], flush=True)
        print(f"outcome: {info['outcome']}; first Abyss probe: {info['firstAbyssProbe']}; {len(info['cases'])} recordings in {out}"
              + (f"; FAILED: {[failure['name'] for failure in info['failed']]}" if info["failed"] else ""), flush=True)
    return 0 if info["outcome"] == "complete" else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Attended Abyss session at a given tempo and transport offset.")
    parser.add_argument("label", nargs="?", help="the session becomes session_<label>/ (A to E exist)")
    parser.add_argument("bpm", nargs="?", type=float, help="tempo the play head reports, 20 to 999 (every session so far: 120)")
    parser.add_argument("offset", nargs="?", type=float, default=0.0,
                        help="play head position in seconds when the instance has processed nothing (default 0)")
    parser.add_argument("--block", type=int, default=ro.BLOCK_SIZE, choices=BLOCKS, help="host block size in frames (default 512)")
    parser.add_argument("--list", action="store_true", help="print the cases and the budget, open nothing")
    parser.add_argument("--stopped", action="store_true",
                        help="the play head says stopped and stays at the offset (what a DAW reports while it is not playing)")
    parser.add_argument("--unattended", action="store_true", help="dry run in Tide that needs nobody (the host starts by itself)")
    arguments = parser.parse_args(argv)
    global STOPPED
    STOPPED = bool(arguments.stopped)
    label, bpm, offset = arguments.label, arguments.bpm, arguments.offset
    if arguments.list and label is not None and re.fullmatch(r"[0-9.]+", label):
        # "--list 90" and "--list 90 0.25": no label was given, the numbers are the tempo and the offset
        label, bpm, offset = None, float(label), (0.0 if bpm is None else bpm)
    if bpm is not None and not 20.0 <= bpm <= 999.0:
        parser.error("the session host takes a tempo from 20 to 999 BPM")
    if not 0.0 <= offset <= 86400.0:
        parser.error("the session host takes a transport offset from 0 to 86400 s")
    if arguments.list:
        return 0 if list_programme(bpm, offset, arguments.block) else 1
    if not label or not re.fullmatch(r"[A-Za-z0-9_-]+", label) or bpm is None:
        parser.error("usage: probe7_tempo.py <label> <bpm> [transport offset in seconds]   (label: letters, digits, _ or -)")
    return run_session(label, bpm, offset, arguments.block, attended=not arguments.unattended)


if __name__ == "__main__":
    sys.exit(main())
