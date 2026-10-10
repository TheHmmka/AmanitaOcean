#!/usr/bin/env python3
"""Attended FOAM session with the reference's Audio Unit: does anything in Foam follow the host's tempo or play head,
how do the outer controls act in Foam, and how does Macro move?

    probe_foam2.py <label> [bpm] [offset seconds] [--stopped] [--block N]
    probe_foam2.py --list [bpm] [offset seconds] [--stopped] [--block N]
                                      prints every case with durations, peaks and the wall-time budget; opens nothing
    probe_foam2.py <label> [bpm] [offset seconds] --unattended
                                      dry run that needs nobody: the host starts by itself after 3 s, the sound stays
                                      Tide, and the script takes the third probe for Foam so that the whole programme
                                      runs once against the real host (about a minute; the recordings are Tide's)

Defaults: 90 BPM, the play head 0.25 s ahead of the frame count and playing, host block 512, 48 kHz, Audio Unit.
--stopped: the play head says stopped and stays at the offset (what a DAW reports while it is not playing).

What is known (session A, 120 BPM, play head = frames processed; analysis/FOAM_FIRST_LOOK.md): Foam is a fixed, static
diffusing filter in front of the reverb network, crossfaded with the dry input by Macro with cos/sin gains. No pitch
shift, no clocks found; its delays are whole samples of the internal 44.1 kHz rate. One instance has one tempo, so
every tempo is a session of its own. The mechanics are those of ../abyss/probe7_tempo.py (the tempo session for Abyss);
part K and the Macro steps come from ../abyss/probe4.py (parts K and S4).

What the owner does (the window shows INSTRUCTION): Demo, Start, then the mode. A mode chosen BEFORE Start does not
reach the sound (session E of Abyss), so the script runs session D's probe over and over from Start on and waits until
it hears Foam in two probes in a row. Foam by sound, with the probe's own descriptors:

    imp_1to3    energy of the left channel 1 to 3 s behind the left impulse:  Tide -49 to -53 dB, Foam -17.8 to -18.0 dB
    L_400to600  400..600 Hz share of the left channel under the 1 kHz tone:   Tide and Foam -83 to -87 dB, Abyss -4 to -7 dB

    Foam = imp_1to3 above -35 dB AND L_400to600 below -60 dB   (Abyss rings behind the impulse too: -22 to +12 dB)

Every probe is printed with the mode it sounds like (Tide, Foam, Abyss or unclear). While the script waits, the job
names in the window end in HINT from probe 12 on. Two in a row, because one Foam probe alone is also what a mode on
its way from Tide to Abyss leaves (session D of Abyss: probe 23 has Foam's numbers, its level included):
firstFoamProbe in info.json is the first probe of the run that held, and a run that broke is listed under "foamLost".

Then, every case behind a flush (8 s at Decay minimum) and 2 s of pre-roll unless stated:

  f01  Macro 0, Decay 0.5 s, 20 left impulses 1 s apart (c01 of the earlier sessions): the base alone. Gives the
       oscillator origin and shows whether tempo or play head touch the base.
  f02  the same train at Macro 100 %.
  f03  Macro 100 %, Decay 0.5 s: 24 isolated impulses of 0.5, left and right in turn, one every 4 s + 1/30 s (a time
       scan in steps of 33 ms; 67 ms per side), 100 s.
  f04  Macro 100 %, Decay 2 s: a steady tone, 1 kHz left and 3 kHz right, 64 s in four steps without a gap, then 6 s of
       recorded silence. Each step is 16 s plus an eighth of a host block long (64 frames at 512), so the host's block
       grid, which starts anew with every step, moves by an eighth of a block from step to step (t03 of probe7_tempo.py).
  k00..k16  the outer controls, one changed per case from the neutral baseline at Macro 100 %, Decay 2 s (K_CASES):
       16 s with a left impulse at 1.0 s, a 5 ms noise burst on both sides at 5.0 s and a right impulse at 9.0 s.
  m01  Macro in motion at Decay 2 s on the same continuous tone: nine steps of 6.25 s (M_STEPS), then a ramp 0 -> 100 %
       and back in steps of 50 ms; one flush in front, every step recorded, no gap; 6 s of recorded silence behind.
  n01  Macro 100 %, Decay 2 s: 10 s of white noise at 0.05 rms, then 6 s of silence.   n02  the same noise at Macro 0.
  end_probe  session D's probe once more: is the sound still Foam?

Budget: everything behind the first Foam probe stays under 150 s of wall time at 15 times real time (--list prints it;
sessions ran 13 to 45 times faster than real time). Before every case the wall time since the creation of the instance
is checked against the host's stop at 19 minutes; a case that does not fit is skipped, shorter ones behind it still
run. A case that fails on the driver's side (a read-back, a file) is noted under "failed" in info.json and the session
goes on with the next one as long as the host is running.

Output: session_<label>/ next to this script: one .npy per recording (float32, (frames, 2), 48 kHz, raw: the 48
samples of reported latency are inside), the stimuli, and info.json, rewritten after every recording. Until Foam is
confirmed the flush and the pre-roll of every probe are recorded too (the same frames, only written down), and the two
probes in front of the first Foam probe are kept with them: the change of mode lies in those files.

The module can be imported without side effects: programme(block) returns the cases with their steps and stimuli.
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
SCRIPT = "probe_foam2.py"
SR = 48000
DEFAULT_BPM = 90.0            # session A ran at the host's 120
DEFAULT_OFFSET = 0.25         # seconds the play head is ahead of the frame count
STOP_MINUTES = 19.0           # the host ends the session this long after the creation of the instance
MARGIN_SECONDS = 45.0         # nothing starts that would end later than this in front of the host's stop
RATE = 15.0                   # processed seconds per wall second the budget assumes (measured: 13 to 45)
BUDGET_WALL_SECONDS = 150.0   # everything behind the first Foam probe, at RATE
FOAM_IMP_DB = -35.0           # imp_1to3: Tide -49 to -53 dB, Foam -17.8 to -18.0 dB (session A); Abyss -22 to +12 dB
FOAM_SHARE_DB = -60.0         # L_400to600: Tide and Foam -83 to -87 dB
ABYSS_DB = -40.0              # L_400to600: Abyss -4 to -7 dB (the threshold of probe7_tempo.py)
TIDE_IMP_DB = -45.0           # only for the name printed with a probe: between this and FOAM_IMP_DB it reads "unclear"
CONFIRMATIONS = 2             # Foam probes in a row before the programme starts
HINT_AFTER = 12               # probes without Foam before the job names carry HINT
BLOCKS = (64, 128, 256, 512, 1024, 2048)
STOPPED = False               # set by --stopped: the play head reports a stopped transport
# The window shows two lines of the instruction, about 160 characters, and under it the name of the running job.
INSTRUCTION_LIMIT = 150
INSTRUCTION = "Press Demo, then Start without touching the mode, then switch the mode to Foam with the arrows and leave it."
# If the label already reads Foam and the sound is not (a click in front of the first processed block, as in session
# E of Abyss), only switching away and back helps: the hint says both. Letters, digits and _ only (it names a folder).
HINT = "_NOT_FOAM_YET_switch_the_mode_to_Foam_or_away_and_back_to_it"
INSTRUCTION_UNATTENDED = "Automatic dry run of the Foam script in the mode Tide. Please do not touch this window; it closes by itself."
PRETEND_FROM = 2              # dry run: probes from this index on count as Foam
END_PROBE = "end_probe"

TONE_STEPS = 4                # steps of the tone of f04; the host's block grid moves by an eighth of a block per step
TONE_STEP_SECONDS = 16.0
TONE_TAIL_SECONDS = 6.0
SCAN_IMPULSES = 24
SCAN_SPACING_FRAMES = 4 * SR + SR // 30      # 4 s + 33.3 ms
SCAN_SECONDS = 100.0                         # the last impulse at 93.77 s keeps 6.2 s of tail

K_SECONDS = 16.0
K_SEED = 2026101001
K_BASE = {"decay": 2.0, "macro": 1.0, "size": 100.0}      # Decay in seconds, Macro as a fraction, Size in percent
# One control per case, by display value (ms, Hz, %, dB; Macro as a fraction), set the way probe4.py part K sets them.
K_CASES = [
    ("k00_neutral", {}),
    ("k01_predelay50ms", {"predelay": 50.0}),
    ("k02_predelay200ms", {"predelay": 200.0}),
    ("k03_hpf1k", {"hpf": 1000.0}),
    ("k04_lpf2k", {"lpf": 2000.0}),
    ("k05_width0", {"width": 0.0}),
    ("k06_width150", {"width": 150.0}),
    ("k07_mix50", {"mix": 50.0}),
    ("k08_brightness_plus100", {"brightness": 100.0}),
    ("k09_brightness_minus100", {"brightness": -100.0}),
    ("k10_ducking50", {"ducking": 50.0}),
    ("k11_transients_full", {"transients": -10.0}),
    ("k12_size30", {"size": 30.0}),
    ("k13_size200", {"size": 200.0}),
    ("k14_decay20", {"decay": 20.0}),
    ("k15_macro50", {"macro": 0.5}),
    ("k16_neutral_again", {}),
]

M_STEPS = [0.0, 0.30, 0.70, 1.0, 0.55, 0.10, 1.0, 0.0, 0.50]     # Macro per step of 6.25 s
M_STEP_SECONDS = 6.25         # 300 000 frames: no whole number of host blocks (64 to 2048 frames) and none of seconds
M_RAMP_STEPS = 40             # moves of 2.5 % from 0 to 100 %, and as many back
M_RAMP_STEP_SECONDS = 0.05    # 2400 frames: whole cycles of 1 kHz and of 3 kHz
M_TAIL_SECONDS = 6.0          # recorded silence at the last level (Macro 0): what the ramp leaves behind

N_SEED = 2026101002
N_NOISE_SECONDS = 10.0
N_TAIL_SECONDS = 6.0


def at(seconds: float) -> int:
    return int(round(seconds * SR))


def S(decay: float, macro: float = 0.0, size: float = 100.0, **more: float) -> dict:
    """Settings by display value (Decay in seconds, Size in percent, Macro as a fraction, the rest in their own units):
    probe4.py's S."""
    settings = {"decay": ro.normalised("decay", decay), "macro": macro, "size": ro.normalised("size", size)}
    for key, value in more.items():
        settings[key] = ro.normalised(key, value)
    return settings


def k_settings(changes: dict) -> dict:
    """probe4.py's k_settings on this session's base (Macro 100 %, Decay 2 s, Size 100 %)."""
    base = dict(K_BASE)
    more = {key: value for key, value in changes.items() if key not in base}
    base.update({key: value for key, value in changes.items() if key in base})
    return S(base["decay"], base["macro"], base["size"], **more)


def k_note(changes: dict) -> str:
    """What a K case changes, in the words of the controls: "Predelay 50 ms"."""
    if not changes:
        return "neutral: Macro 100 %, Decay 2 s, Size 100 %, every other control at the campaign's baseline"
    words = [f"Macro {value * 100:g} %" if key == "macro" else f"{ro.CONTROL[key].name} {value:g} {ro.CONTROL[key].unit}"
             for key, value in changes.items()]
    return ", ".join(words) + "; everything else as in k00"


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
    """c01 and c02 of the earlier sessions: 20 impulses of 0.5 on the left, 1 s apart from 0.25 s on; 24 s."""
    train = np.zeros((at(24), 2), np.float32)
    for k in range(20):
        train[at(0.25 + k), 0] = 0.5
    return train


def two_tone(frames: int) -> np.ndarray:
    """1 kHz left, 3 kHz right, amplitude 0.2, 20 ms fades at the two ends only. Steps are slices of one such array,
    so they join without a click."""
    t = np.arange(frames) / SR
    window = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    tone = np.zeros((frames, 2), np.float32)
    tone[:, 0] = 0.2 * np.sin(2 * np.pi * 1000 * t) * window
    tone[:, 1] = 0.2 * np.sin(2 * np.pi * 3000 * t) * window
    return tone


def tone_stimulus(block: int) -> tuple:
    """(whole tone of f04, list of (start frame, frames) per step)."""
    step = at(TONE_STEP_SECONDS) + block // 8
    return two_tone(TONE_STEPS * step), [(index * step, step) for index in range(TONE_STEPS)]


def scan_stimulus() -> tuple:
    """(stimulus, table of impulses). Impulse n at 1 s + n * (4 s + 1/30 s), left for even n, right for odd n."""
    x = np.zeros((at(SCAN_SECONDS), 2), np.float32)
    table = []
    for n in range(SCAN_IMPULSES):
        frame = at(1.0) + n * SCAN_SPACING_FRAMES
        x[frame, n % 2] = 0.5
        table.append({"n": n, "frame": int(frame), "seconds": frame / SR, "channel": n % 2, "amplitude": 0.5})
    return x, table


def noise_burst(rng: np.random.Generator, frames: int = 240) -> np.ndarray:
    """5 ms of white Gaussian noise at an rms of 0.1, never above 0.5. probe4.py's burst is the same draw at a
    standard deviation of 0.1; here every burst is scaled to the rms itself, so both sides carry the same energy."""
    burst = rng.standard_normal(frames)
    burst *= 0.1 / np.sqrt(np.mean(burst ** 2))
    return np.clip(burst, -0.5, 0.5).astype(np.float32)


def shell_stimulus() -> np.ndarray:
    """16 s for the K cases: a left impulse at 1.0 s, noise bursts on both sides at 5.0 s (drawn left first, each side
    its own noise), a right impulse at 9.0 s."""
    rng = np.random.default_rng(K_SEED)
    x = np.zeros((at(K_SECONDS), 2), np.float32)
    x[at(1.0), 0] = 0.5
    for channel in (0, 1):
        burst = noise_burst(rng)
        x[at(5.0):at(5.0) + len(burst), channel] = burst
    x[at(9.0), 1] = 0.5
    return x


def noise_stimulus() -> np.ndarray:
    """n01 and n02: 10 s of white Gaussian noise, each side its own (as c05 of session A) and each scaled to an rms of
    0.05, then 6 s of silence."""
    rng = np.random.default_rng(N_SEED)
    noise = rng.standard_normal((at(N_NOISE_SECONDS), 2))
    noise *= 0.05 / np.sqrt(np.mean(noise ** 2, axis=0))
    x = np.zeros((at(N_NOISE_SECONDS + N_TAIL_SECONDS), 2), np.float32)
    x[:at(N_NOISE_SECONDS)] = np.clip(noise, -0.5, 0.5)
    return x


def macro_plays() -> list:
    """(Macro, frames, kind) for every play of m01: the steps, then the level 0 once more for 50 ms, forty moves of
    2.5 % up to 100 % and forty back down to 0, each held for 50 ms."""
    step, short = at(M_STEP_SECONDS), at(M_RAMP_STEP_SECONDS)
    plays = [(macro, step, "step") for macro in M_STEPS]
    plays.append((0.0, short, "ramp start"))
    plays += [(k / M_RAMP_STEPS, short, "ramp up") for k in range(1, M_RAMP_STEPS + 1)]
    plays += [((M_RAMP_STEPS - k) / M_RAMP_STEPS, short, "ramp down") for k in range(1, M_RAMP_STEPS + 1)]
    return plays


_F = np.fft.rfftfreq(at(6), 1 / SR)


def _share(P: np.ndarray, lo: float, hi: float) -> float:
    return float(10 * np.log10(P[(_F >= lo) & (_F < hi)].sum() / P.sum() + 1e-30))


def describe(y: np.ndarray) -> dict:
    """The descriptors of probe3.py, probe5.py and probe7_tempo.py, unchanged, so that the log reads like theirs."""
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


def sounds_like(d: dict) -> str:
    """The mode a probe sounds like, from its descriptors: "Tide", "Foam", "Abyss" or "unclear". Foam is exactly
    imp_1to3 above FOAM_IMP_DB and L_400to600 below FOAM_SHARE_DB."""
    share, late = d["L_400to600"], d["imp_1to3"]
    if share > ABYSS_DB:
        return "Abyss"
    if share < FOAM_SHARE_DB and late > FOAM_IMP_DB:
        return "Foam"
    if share < FOAM_SHARE_DB and late < TIDE_IMP_DB:
        return "Tide"
    return "unclear"


# ---------------------------------------------------------------- jobs

@dataclasses.dataclass
class Case:
    name: str
    steps: list                      # rs.Step, in order
    names: list                      # one (recording name, stimulus stem or None) per recorded step
    stimuli: dict                    # stem -> array, saved as <stem>.stimulus.npy
    note: str
    events: dict | None = None       # written into info.json under "events"


def recorded_all(steps: list) -> list:
    """The same steps, frame for frame, with the silences recorded too (recording does not touch the processing)."""
    return [dataclasses.replace(step, record=True) if step.kind == "silence" else step for step in steps]


def probe_steps(record_flush: bool) -> list:
    steps = rs.case(probe_stimulus(), S(2.0, 1.0), SR)
    return recorded_all(steps) if record_flush else steps


def macro_motion_case() -> Case:
    """m01: one continuous tone through every level of macro_plays(); the controls are set between two plays, as
    probe4.py part S4 does."""
    plays = macro_plays()
    tone = two_tone(sum(frames for _, frames, _ in plays))
    steps = rs.flush(S(2.0, plays[0][0]), SR)
    names, table, start = [], [], 0
    for index, (macro, frames, kind) in enumerate(plays):
        if index:
            steps.append(rs.set_parameters(S(2.0, macro)))
        steps.append(rs.play(tone[start:start + frames]))
        name = f"m01_{index:02d}_macro{round(macro * 1000):04d}"
        names.append((name, "m01_tone"))
        table.append({"index": index, "name": name, "kind": kind, "macro": macro, "startFrameInTone": int(start),
                      "frames": int(frames)})
        start += frames
    steps.append(rs.silence(at(M_TAIL_SECONDS), record=True))
    names.append((f"m01_{len(plays):02d}_tail", None))
    ramp = 2 * M_RAMP_STEPS + 1
    return Case("m01_macro_motion_decay2", steps, names, {"m01_tone": tone},
                f"Decay 2 s, one continuous 1 kHz / 3 kHz tone of {len(tone) / SR:.2f} s: Macro "
                + ", ".join(f"{round(macro * 100)}" for macro in M_STEPS) + f" % for {M_STEP_SECONDS:g} s each, then {ramp} plays of "
                f"{M_RAMP_STEP_SECONDS * 1000:g} ms (0, up to 100 % and back to 0 in moves of {100 / M_RAMP_STEPS:g} %), "
                f"then {M_TAIL_SECONDS:g} s of silence",
                {"plays": table, "leftHz": 1000.0, "rightHz": 3000.0, "amplitude": 0.2, "fadeSeconds": 0.02,
                 "macroName": "macroNNNN in a recording's name is Macro in tenths of a percent",
                 "tail": {"name": names[-1][0], "frames": at(M_TAIL_SECONDS), "macro": plays[-1][0]}})


def programme(block: int) -> list:
    """The cases behind the confirmed switch, in the order they run."""
    train = train_stimulus()
    scan, impulses = scan_stimulus()
    tone, slices = tone_stimulus(block)
    shell = shell_stimulus()
    noise = noise_stimulus()
    probe = probe_stimulus()
    stem = "f04_tone_macro100_decay2"
    tone_steps = rs.flush(S(2.0, 1.0), SR) + [rs.play(tone[start:start + frames]) for start, frames in slices] \
        + [rs.silence(at(TONE_TAIL_SECONDS), record=True)]
    tone_names = [(f"{stem}_{index:02d}", stem) for index in range(TONE_STEPS)] + [(f"{stem}_tail", None)]
    cases = [
        Case("f01_macro0_decay0p5_train", rs.case(train, S(0.5, 0.0), SR), [("f01_macro0_decay0p5_train", "f_train")],
             {"f_train": train},
             "Macro 0, Decay 0.5 s: 20 left impulses 1 s apart (c01). The base alone: oscillator origin, and whether tempo or play head touch it"),
        Case("f02_macro100_decay0p5_train", rs.case(train, S(0.5, 1.0), SR), [("f02_macro100_decay0p5_train", "f_train")],
             {"f_train": train}, "Macro 100 %: the same train (c02)"),
        Case("f03_scan_macro100_decay0p5", rs.case(scan, S(0.5, 1.0), SR), [("f03_scan_macro100_decay0p5", "f03_scan")],
             {"f03_scan": scan},
             f"Macro 100 %, Decay 0.5 s: {SCAN_IMPULSES} impulses of 0.5, left and right in turn, every 4 s + 1/30 s",
             {"spacingFrames": SCAN_SPACING_FRAMES, "impulses": impulses}),
        Case(stem, tone_steps, tone_names, {stem: tone},
             f"Macro 100 %, Decay 2 s: 1 kHz left / 3 kHz right for {len(tone) / SR:.3f} s in {TONE_STEPS} gapless steps of "
             f"16 s + {block // 8} frames (the host block grid moves an eighth of a block per step), then {TONE_TAIL_SECONDS:.0f} s of silence",
             {"steps": [{"index": index, "startFrameInTone": int(start), "frames": int(frames),
                         "gridShiftFrames": int((index * (block // 8)) % block)} for index, (start, frames) in enumerate(slices)],
              "leftHz": 1000.0, "rightHz": 3000.0, "amplitude": 0.2, "fadeSeconds": 0.02}),
    ]
    for name, changes in K_CASES:
        cases.append(Case(name, rs.case(shell, k_settings(changes), SR), [(name, "k_shell")], {"k_shell": shell}, k_note(changes),
                          {"changed": dict(changes), "base": dict(K_BASE), "leftImpulseFrame": at(1.0), "burstFrame": at(5.0),
                           "burstFrames": 240, "rightImpulseFrame": at(9.0), "seed": K_SEED}))
    cases.append(macro_motion_case())
    cases += [
        Case("n01_noise_macro100_decay2", rs.case(noise, S(2.0, 1.0), SR), [("n01_noise_macro100_decay2", "n_noise")],
             {"n_noise": noise},
             f"Macro 100 %, Decay 2 s: {N_NOISE_SECONDS:g} s of white noise at 0.05 rms (each side its own), then {N_TAIL_SECONDS:g} s of silence",
             {"seed": N_SEED, "noiseFrames": at(N_NOISE_SECONDS), "rms": 0.05}),
        Case("n02_noise_macro0_decay2", rs.case(noise, S(2.0, 0.0), SR), [("n02_noise_macro0_decay2", "n_noise")],
             {"n_noise": noise}, "Macro 0: the same noise"),
        Case(END_PROBE, probe_steps(False), [(END_PROBE, "probe")], {"probe": probe},
             "session D's probe once more: is the sound still Foam?"),
    ]
    return cases


def audio_frames(steps: list) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus) for step in steps if step.kind != "set")


def recorded_frames(steps: list) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus)
               for step in steps if step.kind != "set" and step.record)


def stimulus_peak(steps: list) -> float:
    return max((float(np.abs(step.stimulus).max()) for step in steps if step.kind == "wav"), default=0.0)


# ---------------------------------------------------------------- --list

def list_programme(bpm: float, offset: float, block: int, out=sys.stdout) -> bool:
    """Prints every case; True when the budget holds and the stimuli are in order."""
    ok = True
    probe = probe_steps(True)
    probe_seconds = audio_frames(probe) / SR
    print(f"{'case':30} {'audio s':>8} {'recorded':>9} {'peak':>6} {'total audio s':>14} {'wall s at %gx' % RATE:>14}", file=out)
    print(f"{'p000, p001, ... (until Foam)':30} {probe_seconds:8.2f} {recorded_frames(probe) / SR:9.2f} {stimulus_peak(probe):6.3f} "
          f"{'-':>14} {'-':>14}   session D's probe, Decay 2 s, Macro 100 %; flush and pre-roll recorded too", file=out)
    total = CONFIRMATIONS * probe_seconds
    print(f"{f'the {CONFIRMATIONS} Foam probes that confirm':30} {CONFIRMATIONS * probe_seconds:8.2f} "
          f"{CONFIRMATIONS * recorded_frames(probe) / SR:9.2f} {stimulus_peak(probe):6.3f} {total:14.2f} {total / RATE:14.1f}"
          f"   the totals count from the first Foam probe on", file=out)
    cases = programme(block)
    recorded = behind = 0.0
    taken, stimulus_bytes, stems = set(), 0, {}
    for case in cases:
        seconds, peak = audio_frames(case.steps) / SR, stimulus_peak(case.steps)
        if peak > rs.MAX_INPUT:
            ok = False
            print(f"PEAK TOO HIGH in {case.name}: {peak}", file=out)
        if len(case.names) != sum(1 for step in case.steps if step.kind != "set" and step.record):
            ok = False
            print(f"NAMES AND RECORDED STEPS DIFFER in {case.name}", file=out)
        for name, stem in case.names:
            if name in taken or re.fullmatch(r"p\d{3}(_flush|_preroll)?", name) or (stem is not None and stem not in case.stimuli):
                ok = False
                print(f"RECORDING NAME {name} of {case.name} IS TAKEN OR ITS STIMULUS IS MISSING", file=out)
            taken.add(name)
        for stem, array in case.stimuli.items():
            if stem in stems and stems[stem] is not array and not np.array_equal(stems[stem], array):
                ok = False
                print(f"TWO STIMULI UNDER THE NAME {stem}", file=out)
            if stem not in stems:
                stems[stem] = array
                stimulus_bytes += array.nbytes
        total += seconds
        behind += seconds
        recorded += recorded_frames(case.steps) / SR
        print(f"{case.name:30} {seconds:8.2f} {recorded_frames(case.steps) / SR:9.2f} {peak:6.3f} {total:14.2f} {total / RATE:14.1f}   {case.note}", file=out)
    budget = BUDGET_WALL_SECONDS * RATE
    fits = total <= budget
    ok &= fits
    print(file=out)
    print(f"behind the detection: {behind:.2f} s of audio in {len(cases)} cases = {behind / RATE:.1f} s of wall time at {RATE:g}x; "
          f"from the first Foam probe on {total:.2f} s = {total / RATE:.1f} s "
          f"({'within' if fits else 'OVER'} the budget of {BUDGET_WALL_SECONDS:.0f} s = {budget:.0f} s of audio)", file=out)
    print(f"{recorded:.0f} s recorded behind the probes in {len(taken)} files, about {(recorded * SR * 8 + stimulus_bytes) / 1e9:.2f} GB "
          f"with the stimuli; every case fits {MARGIN_SECONDS:.0f} s in front of the host's stop at {STOP_MINUTES:g} minutes or is skipped", file=out)
    tone, slices = tone_stimulus(block)
    shifts = sorted({(index * (block // 8)) % block for index in range(TONE_STEPS)})
    print(f"f04: {TONE_STEPS} steps of {slices[0][1]} frames; the host block grid of step i starts {block // 8} * i frames "
          f"later than a grid of {block} that runs through: shifts {shifts}", file=out)
    plays = macro_plays()
    starts = np.cumsum([0] + [frames for _, frames, _ in plays])[:len(M_STEPS)]
    print(f"m01: {len(M_STEPS)} steps of {at(M_STEP_SECONDS)} frames at Macro " + ", ".join(f"{round(m * 100)}" for m in M_STEPS)
          + f" %; step i starts {[int(s % block) for s in starts]} frames into a grid of {block} and "
          f"{[round(float(s % SR) / SR, 2) for s in starts]} s into a second, both counted from the first frame of the tone", file=out)
    levels = [macro for macro, _, kind in plays if kind != "step"]
    print(f"m01: ramp of {len(levels)} plays of {at(M_RAMP_STEP_SECONDS)} frames: Macro {levels[0] * 100:g}, {levels[1] * 100:g}, "
          f"{levels[2] * 100:g}, ... {max(levels) * 100:g}, ... {levels[-2] * 100:g}, {levels[-1] * 100:g} %; "
          f"{len(plays)} plays and {M_TAIL_SECONDS:g} s of silence in one job", file=out)
    for name, changes in K_CASES:
        unknown = [key for key in changes if key not in ro.CONTROL]
        if unknown:
            ok = False
            print(f"UNKNOWN CONTROL in {name}: {unknown}", file=out)
    for name, built in (("probe.stimulus.npy", probe_stimulus()), ("c01_macro0_decay0p5_train.stimulus.npy", train_stimulus())):
        saved = HERE / "session_A" / name
        if saved.exists():
            same = bool(np.array_equal(np.load(saved), built))
            ok &= same
            print(f"session_A/{name} is this script's stimulus bit for bit: {same}", file=out)
    length = len(INSTRUCTION)
    ok &= length <= INSTRUCTION_LIMIT and len(INSTRUCTION_UNATTENDED) <= INSTRUCTION_LIMIT
    print(f"instruction in the window ({length} characters, the window shows about {INSTRUCTION_LIMIT}): {INSTRUCTION}", file=out)
    print(f"instruction of the dry run ({len(INSTRUCTION_UNATTENDED)} characters): {INSTRUCTION_UNATTENDED}", file=out)
    print(f"Foam by sound: imp_1to3 above {FOAM_IMP_DB:g} dB and L_400to600 below {FOAM_SHARE_DB:g} dB, in {CONFIRMATIONS} probes in a row; "
          f"job names while the sound is not Foam, from probe {HINT_AFTER} on: p{HINT_AFTER:03d}{HINT}", file=out)
    ok &= re.fullmatch(r"\w+", HINT, re.ASCII) is not None
    factor = 120.0 / bpm
    print(f"at {bpm:g} BPM (a quarter note is {60.0 / bpm:.6f} s), the play head {offset:g} s = {at(offset)} frames ahead of the frame count "
          f"and {'STOPPED (it stays there)' if STOPPED else 'playing'}, host block {block}:", file=out)
    if abs(factor - 1.0) > 1e-9:
        print(f"  session A ran at 120 BPM. If something in Foam follows the tempo it is {factor:.4f} times as long here: the first "
              f"all-pass delay (1323 internal samples = 30.000 ms) {30.0 * factor:.3f} ms, the kernel's energy peak (about 1.0 s) "
              f"about {factor:.2f} s", file=out)
    elif offset == 0.0 and not STOPPED:
        print("  the host settings of session A (120 BPM, the play head at the frame count and playing): a repeat of that "
              "session's conditions, nothing can move with tempo or play head", file=out)
    else:
        print("  the tempo of session A: nothing can move with it; only the play head's offset or state differ from that session", file=out)
    return bool(ok)


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
    if out.exists():
        print(f"{out} exists already; choose another label", file=sys.stderr)
        return 2
    if len(instruction) > INSTRUCTION_LIMIT:
        print("the instruction is too long for the window", file=sys.stderr)
        return 2
    cases = programme(block)             # built before the window opens: a mistake in a stimulus costs nobody a sitting
    probe = probe_stimulus()
    out.mkdir(parents=True)

    try:
        session = rs.Session.open(instruction=instruction, sample_rate=SR, block_size=block, plugin_format="AudioUnit",
                                  bpm=bpm, transport_offset_seconds=offset, start_after=None if attended else 3.0,
                                  transport_playing=False if STOPPED else None)
    except BaseException:
        try:
            out.rmdir()                  # no window, nothing written: the label stays free
        except OSError:
            pass
        raise
    print("window open:", session.folder, flush=True)
    print("instruction in the window:", instruction, flush=True)
    info = {"label": label, "script": SCRIPT, "variant": "attended" if attended else "unattended dry run (Tide; Foam is pretended)",
            "bpm": bpm, "transportOffsetSeconds": offset, "blockSize": block, "transportPlaying": not STOPPED,
            "sampleRate": SR, "instruction": instruction, "sessionFolder": str(session.folder),
            "quarterNoteSeconds": 60.0 / bpm, "transportOffsetFrames": at(offset),
            "foamIs": {"imp_1to3_above": FOAM_IMP_DB, "L_400to600_below": FOAM_SHARE_DB, "inARow": CONFIRMATIONS},
            "firstFoamProbe": None, "confirmedAtProbe": None, "probes": [], "cases": [], "events": {}, "skipped": [],
            "failed": [], "endProbeFoam": None, "outcome": "running",
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
        # The host must have taken the tempo, the offset and the transport's state: a host built before they existed
        # would run at 120 BPM with a playing transport without a word, and the sitting would be lost.
        told = (float(record.get("bpm", 120.0)), float(record.get("transportOffsetSeconds", 0.0)))
        playing = record.get("transportPlaying")          # None from a host that does not report it: it is playing
        info["hostReports"] = {"bpm": told[0], "transportOffsetSeconds": told[1], "blockSize": record.get("blockSize"),
                               "transportPlaying": playing}
        if abs(told[0] - bpm) > 1e-9 or abs(told[1] - offset) > 1e-9 or int(record.get("blockSize", block)) != block:
            raise RuntimeError(f"the session host reports {told[0]} BPM, offset {told[1]} s, block {record.get('blockSize')}: "
                               f"not what was asked for ({bpm}, {offset}, {block}); is build-session up to date?")
        if (playing is not False) if STOPPED else (playing is False):
            raise RuntimeError(f"the session host reports transportPlaying = {playing!r}, asked for was "
                               f"{'a stopped' if STOPPED else 'a playing'} transport; is build-session up to date?")

        def seconds_left() -> float:      # until the host stops by itself
            return STOP_MINUTES * 60.0 - (datetime.datetime.now(created.tzinfo) - created).total_seconds()

        # as long as a probe could still start (the loop below has the same limit); never less than half a minute
        session.wait_until_started(max(30.0, seconds_left() - MARGIN_SECONDS - 15.0))
        started = time.monotonic()
        print("started", time.strftime("%H:%M:%S"), "- seconds left:", round(seconds_left()), flush=True)
        np.save(out / "probe.stimulus.npy", probe)

        # ---- identical probes until Foam has been heard CONFIRMATIONS times in a row
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
                     firstFrame=int(rec.first_frame), sha256=sha256(rec.output), sounds=sounds_like(d))
            d["foam"] = d["sounds"] == "Foam"
            if not attended and k >= PRETEND_FROM:
                d.update(foam=True, pretended=True)           # dry run: the sound is Tide; go on as if it were Foam
            info["probes"].append(d)
            print(json.dumps(d), flush=True)
            print(f"p{k:03d} sounds like {d['sounds']} (imp_1to3 {d['imp_1to3']} dB, L_400to600 {d['L_400to600']} dB)"
                  + ("; taken for Foam, as a dry run does" if d.get("pretended") else ""), flush=True)
            if d["foam"]:
                row += 1
                if first is None:
                    first = k
                    info["firstFoamProbe"] = k
                    for index, earlier in recent:             # the change of mode lies in these
                        keep_probe(index, earlier)
                    print(f"FOAM from probe {k}", flush=True)
                keep_probe(k, recordings)
            else:
                if row:
                    # One Foam probe alone is what a mode on its way elsewhere leaves (session D of Abyss: probe 23
                    # has Foam's numbers between Tide and Abyss). The first Foam probe is the one of the run that holds.
                    print(f"WARNING: probe {k} does not sound like Foam any more; waiting again", flush=True)
                    info.setdefault("foamLost", []).append({"firstFoamProbe": first, "lostAtProbe": k, "sounds": d["sounds"]})
                    first = info["firstFoamProbe"] = None
                    keep_probe(k, recordings)
                row = 0
                if k == 0 or k % 10 == 0:
                    keep_probe(k, recordings[-1:])
                if attended and d["sounds"] == "Abyss":
                    print("the sound is Abyss, not Foam: switch the mode to Foam", flush=True)
                if attended and first is None and k == HINT_AFTER - 1:
                    print("Foam not heard yet: switch the mode to Foam with the arrows, or away and back if the label already reads Foam "
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
                for (name, stem), rec in zip(case.names, recordings):
                    keep(name, rec, files.get(stem))
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
            if case.name == END_PROBE:
                d = describe(recordings[-1].output)
                d["sounds"] = sounds_like(d)
                info["endProbeFoam"] = d["sounds"] == "Foam"
                info["endProbe"] = d
                print("end probe:", json.dumps(d), "- still Foam" if info["endProbeFoam"]
                      else (f"- NOT FOAM ANY MORE (it sounds like {d['sounds']}): what was recorded is in doubt" if attended
                            else f"- {d['sounds']}, as a dry run is"), flush=True)
            save_info()
            print(f"{case.name} ok: {seconds:.0f} s of audio in {time.monotonic() - begun:.1f} s, "
                  f"peak {max(float(np.abs(rec.output).max()) for rec in recordings):.4f}, seconds left {round(seconds_left())}", flush=True)
        done = {line["name"] for line in info["cases"]}
        missing = [name for case in cases for name, _ in case.names if name not in done]
        info["notRun"] = missing
        info["outcome"] = "complete" if complete and not missing else ("Foam never confirmed" if not complete else "cut short")
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
        print(f"outcome: {info['outcome']}; first Foam probe: {info['firstFoamProbe']}; {len(info['cases'])} recordings in {out}"
              + (f"; SKIPPED: {[skipped['name'] for skipped in info['skipped']]}" if info["skipped"] else "")
              + (f"; FAILED: {[failure['name'] for failure in info['failed']]}" if info["failed"] else ""), flush=True)
    return 0 if info["outcome"] == "complete" else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Attended Foam session: tempo and play head, the outer controls, Macro in motion.")
    parser.add_argument("label", nargs="?", help="the session becomes session_<label>/ next to this script (A exists)")
    parser.add_argument("bpm", nargs="?", type=float, help=f"tempo the play head reports, 20 to 999 (default {DEFAULT_BPM:g}; session A: 120)")
    parser.add_argument("offset", nargs="?", type=float,
                        help=f"play head position in seconds when the instance has processed nothing (default {DEFAULT_OFFSET:g})")
    parser.add_argument("--block", type=int, default=ro.BLOCK_SIZE, choices=BLOCKS, help="host block size in frames (default 512)")
    parser.add_argument("--list", action="store_true", help="print the cases and the budget, open nothing")
    parser.add_argument("--stopped", action="store_true",
                        help="the play head says stopped and stays at the offset (what a DAW reports while it is not playing)")
    parser.add_argument("--unattended", action="store_true", help="dry run in Tide that needs nobody (the host starts by itself)")
    arguments = parser.parse_args(argv)
    global STOPPED
    STOPPED = bool(arguments.stopped)
    label, bpm, offset = arguments.label, arguments.bpm, arguments.offset
    if arguments.list and label is not None and offset is None and re.fullmatch(r"[0-9]+(\.[0-9]*)?", label):
        # "--list 120" and "--list 120 0": no label was given, the numbers are the tempo and the offset
        label, bpm, offset = None, float(label), bpm
    bpm = DEFAULT_BPM if bpm is None else bpm
    offset = DEFAULT_OFFSET if offset is None else offset
    if not 20.0 <= bpm <= 999.0:
        parser.error("the session host takes a tempo from 20 to 999 BPM")
    if not 0.0 <= offset <= 86400.0:
        parser.error("the session host takes a transport offset from 0 to 86400 s")
    if arguments.list:
        return 0 if list_programme(bpm, offset, arguments.block) else 1
    if not label or not re.fullmatch(r"[A-Za-z0-9_-]+", label):
        parser.error("usage: probe_foam2.py <label> [bpm] [transport offset in seconds]   (label: letters, digits, _ or -)")
    return run_session(label, bpm, offset, arguments.block, attended=not arguments.unattended)


if __name__ == "__main__":
    sys.exit(main())
