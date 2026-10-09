#!/usr/bin/env python3
"""Attended Abyss session with the reference's Audio Unit: the job sequence of session D, then the structural programme.

    probe4.py <label>                 attended: Demo, Start, then the mode (see INSTRUCTION); everything else is automatic
    probe4.py <label> --unattended    dry run in Tide that needs nobody (the host starts by itself after 3 s)
    probe4.py --list                  prints every case with its duration, peak and running totals; opens nothing

    --until PART      stop behind that part (A, B, S0, S1, Z1, S2, S3, Z2, K, S4, Z3, S5, S6, H, Z4, S7, S8, Z5, X)
    --probes N        number of probes in part A, an even number (dry runs only; 100 is session D's and the default)
    --before-start    the order that failed in session E (mode first, then Start); only to test that order again
    --out-root DIR    where session_<label>/ is made (default: the folder of this script)

Why the mode comes AFTER Start: in session E the owner chose Abyss before Start, the label showed Abyss and the sound
stayed Tide for 375 probes (campaign README, "Attended sessions host the Audio Unit"). A switch after the first processed
block does reach the sound. So the script runs probe p000 in Tide, then leaves the instance idle until 20 s after Start:
the owner switches in that pause. The host processes nothing between two jobs, so wherever in the pause the click
falls, the instance has processed exactly 1 440 000 frames. Two sessions made with this script therefore switch on the
same frame and can be compared bit for bit (info.json holds the SHA-256 of every recording). If the sound is still
Tide behind the pause, the probes that follow carry "STILL_TIDE_switch_mode_away_and_back_to_Abyss" in their job
names, which the window shows under the instruction, until Abyss is heard.

Part A (probes p000..p099) and part B (the batch and the ring-out) are frame for frame the jobs of probe3.py, the
script of session D: same stimuli, settings, order and flushes. From the first Abyss probe on, a recording here sits
at the processed time it had in session D. The parts behind them are designed in analysis/SESSION_E_DESIGN.md.
Part X, added in review, is session D's probe once more at the very end (on the probes' own 30 s grid, as probe
276, when nothing was skipped): part K moves eight controls that no session has moved in Abyss, and nobody has
seen whether the mode survives that. info.json lists under "modeChecks" what the sound said behind K (in S4), in S7
and in that last probe.

Output: <out-root>/session_<label>/ with one .npy per recording (float32, (frames, 2), 48 kHz, raw: the 48 samples of
reported latency are still inside), the stimuli, and info.json, rewritten after every recording, so that a session the
demo limit cuts short still leaves a usable record. Every probe is kept (session D kept one in four), and until Abyss
has been heard three times the flush and the pre-roll of a probe are recorded too (p000_flush, p000_preroll): the same
frames, only written down. Before every case the wall time since the creation of the instance is checked; a case that
does not fit in front of the host's 19 minute stop with 45 s to spare is skipped, shorter ones behind it still run.
A case behind the probes that fails on the driver's side (a read-back, a file) is noted under "failed" in info.json
and the session goes on with the next one as long as the host is running; what the host recorded of such a case
stays as WAV files in its results folder (sessionFolder in info.json).

The module can be imported without side effects: `probe4.build_programme()` returns the jobs, `job.make()` their
steps and stimuli (every stimulus is seeded), `probe4.list_programme()` prints them.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import functools
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
import datasets                       # noqa: E402

HERE = Path(__file__).resolve().parent
SR = 48000
PROBES = 100                  # session D
PREROLL_OF_A_PROBE = rs.PREROLL_SECONDS    # 2 s, as rs.case gives the probes of part A
STOP_MINUTES = 19.0           # the host ends the session this long after the creation of the instance
MARGIN_SECONDS = 45.0         # nothing starts that would end later than this in front of the host's stop
RATE_SLOW = 27.0              # processed seconds per wall second: session E's batch and long recordings (busy machine)
RATE_FAST = 36.0              # session D, everything included
JOB_SECONDS = 0.05            # wall time a job costs besides its audio (hashing, saving, the host's 50 ms poll)
SWITCH_WINDOW_END = 20.0      # the pause for the owner's switch ends this long after Start ...
SWITCH_WINDOW_MIN = 15.0      # ... and lasts at least this long. Session D: told "about 10 seconds after Start", the
                              # owner's switch reached the sound 16.4 s after Start, so a pause that ends at 12 s is too short
ABYSS_DB = -40.0              # 400..600 Hz share of the left channel under the 1 kHz tone: Tide -83 to -87 dB, Abyss -4 to -7 dB
RECORD_FLUSH_UNTIL = 40       # the flush and pre-roll of a probe are recorded until Abyss has been heard three times, at most this far
PART_ORDER = ["A", "B", "S0", "S1", "Z1", "S2", "S3", "Z2", "K", "S4", "Z3", "S5", "S6", "H", "Z4", "S7", "S8", "Z5", "X"]
# Recordings with a steady 1 kHz tone on the left at Macro 100 %, and where their 6 s window starts: on these the
# script hears whether the sound is still Abyss (the 400..600 Hz share again; sessions D and E: Abyss -4 to -7 dB,
# Tide -83 to -88 dB). The label in the window cannot say: in session E it showed Abyss while the sound was Tide.
MODE_CHECKS = {"s4_steps_11_macro100": 4.0, "s7_tone_pair_120s_macro100_decay0p5": 4.0, "x_end_probe": 1.5}

# The session window shows two lines of the instruction and cuts the rest: of session D's 166 characters the last
# word was missing (78 + 81 characters shown, see window-after-probes.png of that session). Stay well below that.
INSTRUCTION_LIMIT = 150
INSTRUCTION = ("Press Demo, then press Start WITHOUT touching the mode. Count to three, then switch the mode to Abyss "
               "with the arrows. Change nothing else.")
INSTRUCTION_BEFORE_START = ("Press Demo, select the mode Abyss with the arrows next to the mode name, change nothing "
                            "else, then press Start.")
INSTRUCTION_UNATTENDED = ("Automatic dry run of the capture script in the mode Tide. Please do not touch this window; "
                          "it closes by itself.")
# The window also shows the name of the running job. While the sound is still Tide behind the pause, the probes
# carry this in their names: the owner sees it without the terminal. Letters, digits and _ only (it names a folder).
STILL_TIDE = "_STILL_TIDE_switch_mode_away_and_back_to_Abyss"


def at(seconds: float) -> int:
    return int(round(seconds * SR))


def pad(x: np.ndarray, seconds: float) -> np.ndarray:
    return np.concatenate([x, np.zeros((at(seconds), 2), np.float32)])


def S(decay: float, macro: float = 0.0, size: float = 100.0, **more: float) -> dict:
    """Settings by display value (Decay in seconds, Size in percent, Macro as a fraction, the rest in their own units)."""
    settings = {"decay": ro.normalised("decay", decay), "macro": macro, "size": ro.normalised("size", size)}
    for key, value in more.items():
        settings[key] = ro.normalised(key, value)
    return settings


# ---------------------------------------------------------------- session D's jobs (probe3.py), unchanged

@functools.lru_cache(maxsize=1)
def d_sequence():
    """The probe, the fifteen cases and the ring-out burst of probe3.py, from its own random generator in its own order."""
    t = np.arange(at(7.5)) / SR
    fade = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    probe = np.zeros((at(20), 2), np.float32)
    probe[at(0.5):at(8.0), 0] = 0.2 * np.sin(2 * np.pi * 1000 * t) * fade
    probe[at(0.5):at(8.0), 1] = 0.2 * np.sin(2 * np.pi * 3000 * t) * fade
    probe[at(10.0), 0] = 0.5
    probe[at(13.0), 1] = 0.5
    rng = np.random.default_rng(20261008)
    train = np.zeros((at(24), 2), np.float32)
    for k in range(20):
        train[at(0.25 + k), 0] = 0.5
    imp3 = np.zeros((at(30), 2), np.float32)
    imp3[at(0.25), 0] = 0.5
    imp3[at(6.25), 1] = 0.5
    imp3[at(12.25), :] = 0.4
    prog = datasets.network_programme(SR, seed=4242)
    noise = np.clip(rng.standard_normal((at(20), 2)) * 0.05, -0.5, 0.5).astype(np.float32)
    t6 = np.arange(at(6)) / SR
    fade6 = np.minimum(1, np.minimum(t6, t6[::-1]) / 0.01)
    sine1k = np.zeros((at(6), 2), np.float32)
    sine1k[:, 0] = 0.25 * np.sin(2 * np.pi * 1000 * t6) * fade6
    sine220 = np.zeros((at(6), 2), np.float32)
    sine220[:, :] = (0.25 * np.sin(2 * np.pi * 220 * t6) * fade6)[:, None]
    burst = np.clip(rng.standard_normal((at(2), 2)) * 0.1, -0.5, 0.5).astype(np.float32)
    cases = [
        ("c01_macro0_decay0p5_train", train, S(0.5)),
        ("c02_macro100_decay0p5_train", train, S(0.5, 1.0)),
        ("c03_macro100_decay2_imp3", imp3, S(2.0, 1.0)),
        ("c04_macro100_decay2_prog", pad(prog, 16), S(2.0, 1.0)),
        ("c05_macro100_decay2_noise", pad(noise, 10), S(2.0, 1.0)),
        ("c06_macro100_decay2_sine1k", pad(sine1k, 12), S(2.0, 1.0)),
        ("c07_macro100_decay2_sine220", pad(sine220, 12), S(2.0, 1.0)),
        ("c08_macro50_decay2_imp3", imp3, S(2.0, 0.5)),
        ("c09_macro25_decay2_imp3", imp3, S(2.0, 0.25)),
        ("c10_macro75_decay2_imp3", imp3, S(2.0, 0.75)),
        ("c11_macro100_decay2_imp3_again", imp3, S(2.0, 1.0)),
        ("c12_macro100_decay2_size60_imp3", imp3, S(2.0, 1.0, 60.0)),
        ("c13_macro100_decay2_size150_imp3", imp3, S(2.0, 1.0, 150.0)),
        ("c14_macro100_decay8_imp3", imp3, S(8.0, 1.0)),
        ("c16_macro0_decay2_prog_late", prog, S(2.0)),
    ]
    return probe, cases, burst


_F = np.fft.rfftfreq(at(6), 1 / SR)


def _share(P: np.ndarray, lo: float, hi: float) -> float:
    return float(10 * np.log10(P[(_F >= lo) & (_F < hi)].sum() / P.sum() + 1e-30))


def describe(y: np.ndarray) -> dict:
    """The descriptors of probe3.py, unchanged, so that the log reads like session D's."""
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
class Content:
    steps: list                      # rs.Step, in order
    names: list                      # one (recording name, stimulus stem or None) per recorded step
    stimuli: dict                    # stem -> array, saved as <stem>.stimulus.npy (or .npz) once the job has run
    events: dict | None = None       # written as <job>.events.json: where every event of a map sits


@dataclasses.dataclass
class Job:
    part: str
    name: str
    make: object                     # () -> Content; stimuli are built when asked for
    note: str = ""


def audio_frames(steps) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus) for step in steps if step.kind != "set")


def recorded_frames(steps) -> int:
    return sum(step.frames if step.kind == "silence" else len(step.stimulus)
               for step in steps if step.kind != "set" and step.record)


def stimulus_peak(steps) -> float:
    return max((float(np.abs(step.stimulus).max()) for step in steps if step.kind == "wav"), default=0.0)


def wall_estimate(seconds: float, rate: float = RATE_SLOW) -> float:
    return seconds / rate + JOB_SECONDS


def case4(stimulus: np.ndarray, settings: dict) -> list:
    """A case whose flush and pre-roll take 12 s (8 s at Decay 0.5 s, 4 s at the case's Decay). With stimuli that
    are whole multiples of 4 s long, every recording behind part B starts on a multiple of 4 s of processed time,
    the common period of the layer's three chunk clocks (2/3 s, 1 s, 4/3 s)."""
    if len(stimulus) % at(4.0):
        raise ValueError("stimuli behind part B are whole multiples of 4 s long")
    return rs.case(stimulus, settings, SR, flush_seconds=8.0, preroll_seconds=4.0)


def recorded_all(steps) -> list:
    """The same steps, frame for frame, with the silences recorded too (recording does not touch the processing)."""
    return [dataclasses.replace(step, record=True) if step.kind == "silence" else step for step in steps]


def probe_content(index: int, record_flush: bool = False) -> Content:
    probe, _, _ = d_sequence()
    steps = rs.case(probe, S(2.0, 1.0), SR)
    name = f"p{index:03d}"
    if record_flush:
        return Content(recorded_all(steps), [(f"{name}_flush", None), (f"{name}_preroll", None), (name, "probe")], {"probe": probe})
    return Content(steps, [(name, "probe")], {"probe": probe})


def simple_case(name: str, stimulus_of, settings: dict, stem: str | None = None, long_flush: bool = True) -> Job:
    def make() -> Content:
        stimulus = stimulus_of()
        steps = case4(stimulus, settings) if long_flush else rs.case(stimulus, settings, SR)
        return Content(steps, [(name, stem or name)], {stem or name: stimulus})
    return Job("", name, make)


# ---------------------------------------------------------------- stimuli of the structural programme (all seeded)

def noise_burst(rng: np.random.Generator, scale: float = 1.0, frames: int = 240) -> np.ndarray:
    """5 ms of Gaussian noise, 0.1 rms at scale 1, never above 0.5."""
    return (np.clip(rng.standard_normal(frames) * 0.1, -0.5, 0.5) * scale).astype(np.float32)


def burst_map(seed: int, offset: float = 0.0, events: int = 292, spacing: float = 2.0137, seconds: float = 596.0):
    """Unique noise bursts every 2.0137 s: each event comes 13.7 ms later in every cycle that divides 2 s, and
    alternate events sit in alternate halves of the 4 s frame. Kinds by event number modulo 4: left alone, right
    alone, both at once (independent noise), and a fourth kind that alternates between both sides at a tenth of the
    level (scaling) and a pair of bursts 150 ms apart on one side (superposition in time). Each kind covers the
    whole 4 s frame in steps of 54.8 ms; kinds 0 and 2 together give the left input every 27.4 ms, 1 and 2 the right."""
    rng = np.random.default_rng(seed)
    x = np.zeros((at(seconds), 2), np.float32)
    table = []

    def put(frame: int, channel: int, scale: float) -> dict:
        b = noise_burst(rng, scale)
        x[frame:frame + len(b), channel] += b
        return {"frame": int(frame), "channel": channel, "scale": scale, "frames": int(len(b))}

    for n in range(events):
        frame = at(1.0 + offset + spacing * n)
        kind = n % 4
        if kind == 0:
            label, bursts = "left", [put(frame, 0, 1.0)]
        elif kind == 1:
            label, bursts = "right", [put(frame, 1, 1.0)]
        elif kind == 2:
            label, bursts = "both", [put(frame, 0, 1.0), put(frame, 1, 1.0)]
        else:
            sub = (n // 4) % 4
            if sub in (0, 2):
                label, bursts = "both_tenth", [put(frame, 0, 0.1), put(frame, 1, 0.1)]
            else:
                channel = 0 if sub == 1 else 1
                label = "pair_left" if channel == 0 else "pair_right"
                bursts = [put(frame, channel, 1.0), put(frame + at(0.150), channel, 1.0)]
        table.append({"n": n, "kind": label, "frame": int(frame), "seconds": frame / SR, "bursts": bursts})
    return x, {"seed": seed, "spacingSeconds": spacing, "offsetSeconds": offset, "burstFrames": 240, "burstRms": 0.1,
               "note": "frame = host frame inside the recording; a burst is rng.standard_normal(240) * 0.1 clipped to "
                       "0.5, times its scale, drawn in the order of this table",
               "events": table}


def impulse_map(channel: int, events: int = 146, spacing: float = 4.0274, seconds: float = 592.0, offset: float = 0.0):
    """Isolated impulses every 4.0274 s on one side: 27.4 ms later in the 4 s frame each time, so 146 of them cover
    the whole frame. Amplitude 0.5 for even and 0.05 for odd impulses: neighbours must give the same kernel."""
    x = np.zeros((at(seconds), 2), np.float32)
    table = []
    for n in range(events):
        frame = at(1.0 + offset + spacing * n)
        amplitude = 0.5 if n % 2 == 0 else 0.05
        x[frame, channel] = amplitude
        table.append({"n": n, "frame": int(frame), "seconds": frame / SR, "channel": channel, "amplitude": amplitude})
    return x, {"spacingSeconds": spacing, "offsetSeconds": offset, "events": table}


TONE_SLOT, TONE_ON = 8.0, 6.0
MUSICAL = [110, 1109, 147, 1480, 196, 1976, 262, 2637, 349, 3520, 466, 4699, 622, 6272, 831, 8372]


def tone_slots() -> list:
    """(left Hz, right Hz) per 8 s slot. Around 1 kHz and around 3 kHz in steps of 1 Hz, the two sides always in
    different ranges and each side changing range from slot to slot, so the tail of one tone (the octave-up voice
    recirculates for seconds) never lies next to the lines of the following one. Then sixteen tones over six octaves."""
    slots = []
    for i in range(21):
        slots.append((990.0 + i, 3010.0 - i))
        slots.append((2990.0 + i, 1010.0 - i))
    for j in range(16):
        slots.append((float(MUSICAL[j]), float(MUSICAL[15 - j])))
    return slots


def stepped_tones():
    slots = tone_slots()
    x = np.zeros((at(TONE_SLOT * len(slots)), 2), np.float32)
    t = np.arange(at(TONE_ON)) / SR
    fade = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    table = []
    for index, (left, right) in enumerate(slots):
        start = at(TONE_SLOT * index)
        x[start:start + len(t), 0] = 0.2 * np.sin(2 * np.pi * left * t) * fade
        x[start:start + len(t), 1] = 0.2 * np.sin(2 * np.pi * right * t) * fade
        table.append({"slot": index, "frame": int(start), "seconds": start / SR, "onSeconds": TONE_ON,
                      "leftHz": left, "rightHz": right, "amplitude": 0.2})
    return x, {"slotSeconds": TONE_SLOT, "fadeSeconds": 0.02, "events": table}


@functools.lru_cache(maxsize=1)
def shell_stimulus() -> np.ndarray:
    """16 s for the cases of part K: a left impulse at 0.25 s, a right one at 4.25 s, noise bursts on both sides at 8.25 s."""
    rng = np.random.default_rng(2026100803)
    x = np.zeros((at(16), 2), np.float32)
    x[at(0.25), 0] = 0.5
    x[at(4.25), 1] = 0.5
    for channel in (0, 1):
        b = noise_burst(rng)
        x[at(8.25):at(8.25) + len(b), channel] = b
    return x


@functools.lru_cache(maxsize=2)
def anchor_stimulus(alternate: bool) -> np.ndarray:
    """12 s: eight impulses of 0.5 one second apart from 0.25 s on; on the left, or left and right in turn."""
    x = np.zeros((at(12), 2), np.float32)
    for k in range(8):
        x[at(0.25 + k), (k % 2) if alternate else 0] = 0.5
    return x


def two_tone(seconds: float, left: float = 1000.0, right: float = 3000.0, fade: float = 0.0) -> np.ndarray:
    """Whole cycles on both sides when `seconds` times the frequencies are whole numbers: steps then join without a click."""
    t = np.arange(at(seconds)) / SR
    window = np.minimum(1, np.minimum(t, t[::-1]) / fade) if fade > 0 else 1.0
    x = np.zeros((len(t), 2), np.float32)
    x[:, 0] = 0.2 * np.sin(2 * np.pi * left * t) * window
    x[:, 1] = 0.2 * np.sin(2 * np.pi * right * t) * window
    return x


def sweep(channel: int, rising: bool, seconds: float = 120.0, tail: float = 12.0, low: float = 50.0, high: float = 20000.0) -> np.ndarray:
    t = np.arange(at(seconds)) / SR
    ratio = np.log(high / low)
    s = 0.2 * np.sin(2 * np.pi * low * seconds / ratio * (np.exp(t / seconds * ratio) - 1.0))
    s *= np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
    x = np.zeros((at(seconds + tail), 2), np.float32)
    x[:len(t), channel] = s if rising else s[::-1]
    return x


MACRO_STEPS = [0.0, 0.07, 0.13, 0.17, 0.25, 0.35, 0.43, 0.48, 0.55, 0.65, 0.85, 1.0, 0.62, 0.30, 0.0, 1.0]
SIZE_STEPS = [60.0, 150.0, 30.0, 200.0, 100.0]
DECAY_STEPS = [2.0, 8.0, 20.0, 0.5]
LONG_STEP, SHORT_STEP, STEP_TAIL = 20.25, 8.25, 5.75


def steps_content() -> Content:
    """One continuous two-tone (1 kHz left, 3 kHz right) through steps of Macro (20.25 s each, so that a step falls
    0.25 s further into every chunk each time), then of Size and of Decay (8.25 s each). Counted in seconds from the
    first frame, every step that moves a voice's gain lies off that voice's chunk edge, with one exception: step 4
    (Macro 0.17 to 0.25) falls on an edge of the octave-up voice's 1 s chunk. Steps 8 and 12 fall on such an edge
    too, and step 8 on an edge of the 2/3 s chunk, but there the gains of those voices do not move
    (analysis2/script_review/check_scripts.py p4 lists the phases)."""
    long_tone, short_tone = two_tone(LONG_STEP), two_tone(SHORT_STEP)
    steps = rs.flush(S(0.5, MACRO_STEPS[0]), SR, flush_seconds=8.0, preroll_seconds=4.0) + [rs.play(long_tone)]
    names = [(f"s4_steps_00_macro{round(MACRO_STEPS[0] * 100):03d}", "s4_steps_long")]
    for macro in MACRO_STEPS[1:]:
        steps += [rs.set_parameters(S(0.5, macro)), rs.play(long_tone)]
        names.append((f"s4_steps_{len(names):02d}_macro{round(macro * 100):03d}", "s4_steps_long"))
    for size in SIZE_STEPS:
        steps += [rs.set_parameters(S(0.5, 1.0, size)), rs.play(short_tone)]
        names.append((f"s4_steps_{len(names):02d}_size{round(size):03d}", "s4_steps_short"))
    for decay in DECAY_STEPS:
        steps += [rs.set_parameters(S(decay, 1.0)), rs.play(short_tone)]
        names.append((f"s4_steps_{len(names):02d}_decay{str(decay).replace('.', 'p')}", "s4_steps_short"))
    steps.append(rs.silence(at(STEP_TAIL), record=True))
    names.append((f"s4_steps_{len(names):02d}_tail", None))
    return Content(steps, names, {"s4_steps_long": long_tone, "s4_steps_short": short_tone})


K_CASES = [
    ("k00_neutral", {}),
    ("k01_predelay100ms", {"predelay": 100.0}),
    ("k02_hpf1k", {"hpf": 1000.0}),
    ("k03_lpf2k", {"lpf": 2000.0}),
    ("k04_width0", {"width": 0.0}),
    ("k05_width150", {"width": 150.0}),
    ("k06_mix50", {"mix": 50.0}),
    ("k07_brightness_plus100", {"brightness": 100.0}),
    ("k08_brightness_minus100", {"brightness": -100.0}),
    ("k09_ducking50", {"ducking": 50.0}),
    ("k10_transients_full", {"transients": -10.0}),
    ("k11_size30", {"size": 30.0}),
    ("k12_size200", {"size": 200.0}),
    ("k13_decay20", {"decay": 20.0}),
    ("k14_macro80", {"macro": 0.8}),
    ("k15_macro30", {"macro": 0.3}),
    ("k16_predelay333ms", {"predelay": 333.0}),
    ("k17_neutral_again", {}),
]


def k_settings(changes: dict) -> dict:
    base = {"decay": 0.5, "macro": 1.0, "size": 100.0}
    more = {key: value for key, value in changes.items() if key not in base}
    base.update({key: value for key, value in changes.items() if key in base})
    return S(base["decay"], base["macro"], base["size"], **more)


def map_job(part: str, name: str, build, settings: dict, note: str = "") -> Job:
    def make() -> Content:
        stimulus, events = build()
        return Content(case4(stimulus, settings), [(name, name)], {name: stimulus}, events)
    return Job(part, name, make, note)


def anchors(part: str) -> list:
    tag = part.lower()
    jobs = [simple_case(f"{tag}_anchor_macro0_train", lambda: anchor_stimulus(False), S(0.5, 0.0), stem="z_anchor_left"),
            simple_case(f"{tag}_anchor_macro100_train", lambda: anchor_stimulus(True), S(0.5, 1.0), stem="z_anchor_left_right")]
    for job in jobs:
        job.part = part
    jobs[0].note = "Macro 0: the base alone; gives the oscillator count of the session and shows that it has not moved"
    jobs[1].note = "Macro 100 %: the same eight chunk positions at another absolute time"
    return jobs


def build_programme(probes: int = PROBES) -> list:
    """Every job of the session in order. Part A and B are probe3.py's; the rest is ordered by value."""
    probe, cases, burst = d_sequence()
    jobs = [Job("A", f"p{k:03d}", functools.partial(probe_content, k),
                "session D's probe: 1 kHz left / 3 kHz right 0.5-8 s, impulses at 10 s (left) and 13 s (right); Decay 2 s, Macro 100 %")
            for k in range(probes)]

    for name, stimulus, settings in cases:
        job = simple_case(name, lambda stimulus=stimulus: stimulus, settings, long_flush=False)
        job.part, job.note = "B", "session D's batch"
        jobs.append(job)

    def ringout() -> Content:
        s = S(2.0, 1.0)
        steps = rs.flush(s, SR) + [rs.play(burst), rs.set_parameters(dict(s, decay=0.0)), rs.silence(at(40), record=True)]
        return Content(steps, [("c15_ringout_0", "c15_ringout"), ("c15_ringout_1", None)], {"c15_ringout": burst})
    jobs.append(Job("B", "c15_ringout", ringout, "session D's ring-out: 2 s of noise at Macro 100 %, then Decay at its minimum and 40 s of silence"))

    def silence_content() -> Content:
        s = S(2.0, 1.0)
        steps = recorded_all(rs.flush(s, SR, flush_seconds=8.0, preroll_seconds=4.0)) + [rs.silence(at(28), record=True)]
        return Content(steps, [("s0_silence_flush", None), ("s0_silence_preroll", None), ("s0_silence_macro100_decay2", None)], {})
    jobs.append(Job("S0", "s0_silence", silence_content,
                    "silence in, Macro 100 %: the layer's self-noise and what its 1 s loop still holds 40 to 80 s behind the ring-out's burst"))

    jobs.append(map_job("S1", "s1_burst_map_macro100_decay0p5", lambda: burst_map(2026100801), S(0.5, 1.0),
                        "292 unique 5 ms noise bursts 2.0137 s apart: left, right, both, a tenth of the level, pairs 150 ms apart"))
    jobs += anchors("Z1")
    jobs.append(map_job("S2", "s2_stepped_tones_macro100_decay0p5", stepped_tones, S(0.5, 1.0),
                        "58 tones of 6 s: 990..1010 Hz and 2990..3010 Hz in steps of 1 Hz on either side, then 110 Hz to 8372 Hz"))
    jobs.append(map_job("S3", "s3_impulse_map_right_macro100_decay0p5", lambda: impulse_map(1), S(0.5, 1.0),
                        "146 right impulses 4.0274 s apart, 0.5 and 0.05 in turn: the whole 4 s frame in steps of 27.4 ms"))
    jobs += anchors("Z2")

    for name, changes in K_CASES:
        job = simple_case(name, shell_stimulus, k_settings(changes), stem="k_shell")
        job.part = "K"
        job.note = ", ".join(f"{key} {value:g}" for key, value in changes.items()) or "Macro 100 %, Decay 0.5 s, everything else neutral"
        jobs.append(job)

    jobs.append(Job("S4", "s4_steps", steps_content,
                    "continuous 1 kHz / 3 kHz: 16 steps of Macro (20.25 s), 5 of Size and 4 of Decay (8.25 s), 5.75 s of silence"))
    jobs += anchors("Z3")
    jobs.append(map_job("S5", "s5_impulse_map_left_macro100_decay0p5", lambda: impulse_map(0), S(0.5, 1.0),
                        "146 left impulses 4.0274 s apart, 0.5 and 0.05 in turn"))

    for name, channel, rising in (("s6_sweep_left_up_macro100_decay0p5", 0, True), ("s6_sweep_right_down_macro100_decay0p5", 1, False)):
        job = simple_case(name, functools.partial(sweep, channel, rising), S(0.5, 1.0))
        job.part = "S6"
        job.note = "logarithmic sweep 50 Hz to 20 kHz in 120 s " + ("upwards on the left" if rising else "downwards on the right") + ", 12 s of silence"
        jobs.append(job)

    for name, seed, settings in (("h1_holdout_macro100_decay2_prog5151", 5151, S(2.0, 1.0)),
                                 ("h2_holdout_macro60_decay4_prog6262", 6262, S(4.0, 0.6)),
                                 ("h3_holdout_macro30_decay1_prog7373", 7373, S(1.0, 0.3))):
        job = simple_case(name, lambda seed=seed: pad(datasets.network_programme(SR, seed=seed), 18), settings)
        job.part = "H"
        job.note = "holdout: a musical programme no analyst has seen; for scoring a finished model only"
        jobs.append(job)
    jobs += anchors("Z4")

    job = simple_case("s7_tone_pair_120s_macro100_decay0p5", lambda: pad(two_tone(120.0, 1000.0, 1370.0, fade=0.02), 8), S(0.5, 1.0))
    job.part, job.note = "S7", "bonus: 1000 Hz left and 1370 Hz right for 120 s: every line of the three clocks at 0.01 Hz"
    jobs.append(job)
    jobs.append(map_job("S8", "s8_burst_map_macro80_decay0p5", lambda: burst_map(2026100808, offset=0.0137), S(0.5, 0.8),
                        "bonus: the burst map again, 13.7 ms later in the frame, at Macro 80 % (the octave-down voice at half its gain)"))
    jobs += anchors("Z5")

    def end_probe() -> Content:
        steps = rs.flush(S(2.0, 1.0), SR, flush_seconds=28.0, preroll_seconds=PREROLL_OF_A_PROBE) + [rs.play(probe)]
        return Content(steps, [("x_end_probe", "probe")], {"probe": probe})
    jobs.append(Job("X", "x_end_probe", end_probe,
                    "session D's probe once more, its settings and pre-roll, behind a flush of 28 s: does it still sound like Abyss?"))

    unknown = sorted({job.part for job in jobs} - set(PART_ORDER))
    if unknown:
        raise RuntimeError(f"parts without a place in PART_ORDER: {unknown}")
    return jobs


def stimulus(name: str) -> np.ndarray:
    """The stimulus of a recording by its stem or job name, for analysis: built again from its seed."""
    for job in build_programme():
        if job.part == "A" and name not in ("probe", job.name):
            continue
        content = job.make()
        if name in content.stimuli:
            return content.stimuli[name]
        if name == job.name and len(content.stimuli) == 1:
            return next(iter(content.stimuli.values()))
    raise KeyError(name)


# ---------------------------------------------------------------- --list

def verify_against_session_d() -> list:
    """Compares parts A and B with what session D saved: stimuli bit for bit, settings and frame counts."""
    folder = HERE / "session_D"
    if not (folder / "info.json").exists():
        return ["session_D is not here: parts A and B could not be compared with it"]
    probe, cases, burst = d_sequence()
    recorded = json.loads((folder / "info.json").read_text())
    by_name = {case["name"]: case for case in recorded["cases"]}
    lines, ok = [], True
    same = np.array_equal(np.load(folder / "probe.stimulus.npy"), probe)
    ok &= same
    lines.append(f"  probe stimulus identical to session D's: {same}")
    first = 10.0
    for name, stim, settings in cases:
        saved = np.load(folder / f"{name}.stimulus.npy")
        resolved = rs.set_parameters(settings).settings
        same = (np.array_equal(saved, stim) and by_name[name]["frames"] == len(stim)
                and all(np.float32(by_name[name]["settings"][key]) == np.float32(value) for key, value in resolved.items()))
        ok &= same
        if not same:
            lines.append(f"  {name}: DIFFERS from session D")
    same = np.array_equal(np.load(folder / "c15_ringout.stimulus.npy"), burst)
    ok &= same
    lines.append(f"  ring-out burst identical to session D's: {same}")
    frame = PROBES * at(30.0)
    for name, stim, _ in cases:
        frame += at(first)
        if by_name[name]["firstFrame"] != frame:
            ok = False
            lines.append(f"  {name}: first frame {frame} here, {by_name[name]['firstFrame']} in session D")
        frame += len(stim)
    lines.insert(0, "parts A and B against session_D (stimuli, settings, frame counts): " + ("IDENTICAL" if ok else "NOT IDENTICAL"))
    return lines


def list_programme(probes: int = PROBES, out=sys.stdout) -> dict:
    jobs = build_programme(probes)
    total = recorded = wall = 0.0
    stimulus_bytes = 0
    seen = set()
    rows = []
    print(f"{'part':4} {'case':44} {'audio s':>8} {'recorded':>8} {'peak':>6} {'total audio s':>13} {'wall s at 27x':>13}", file=out)

    def account(content: Content):
        nonlocal stimulus_bytes
        for stem, array in content.stimuli.items():
            if stem not in seen:
                seen.add(stem)
                sparse = array.nbytes > 64e6 and np.count_nonzero(array) < 0.05 * array.size
                stimulus_bytes += array.nbytes // 100 if sparse else array.nbytes

    content = jobs[0].make()
    account(content)
    seconds, rec, peak = audio_frames(content.steps) / SR, recorded_frames(content.steps) / SR, stimulus_peak(content.steps)
    total += seconds; recorded += rec; wall += wall_estimate(seconds)
    print(f"{'A':4} {'p000 (Tide)':44} {seconds:8.2f} {rec:8.2f} {peak:6.3f} {total:13.2f} {wall:13.1f}", file=out)
    wall = max(wall + SWITCH_WINDOW_MIN, SWITCH_WINDOW_END)
    print(f"{'A':4} {'-- pause: the owner switches to Abyss --':44} {0.0:8.2f} {0.0:8.2f} {'':6} {total:13.2f} {wall:13.1f}", file=out)
    rest = probes - 1
    total += seconds * rest; recorded += rec * rest; wall += wall_estimate(seconds) * rest
    print(f"{'A':4} {f'p001..p{probes - 1:03d} ({rest} probes of {seconds:.0f} s)':44} {seconds * rest:8.2f} {rec * rest:8.2f} {peak:6.3f} {total:13.2f} {wall:13.1f}", file=out)
    rows.append(f"A  p000..p{probes - 1:03d}: {probes} probes of session D ({seconds * probes:.0f} s); pause for the switch behind p000")
    marks = {}
    for count, job in enumerate(jobs[probes:], probes + 1):
        content = job.make()
        account(content)
        seconds, rec, peak = audio_frames(content.steps) / SR, recorded_frames(content.steps) / SR, stimulus_peak(content.steps)
        if peak > rs.MAX_INPUT:
            raise RuntimeError(f"{job.name}: peak {peak} is above {rs.MAX_INPUT}")
        total += seconds; recorded += rec; wall += wall_estimate(seconds)
        marks[job.part] = (total, wall, SWITCH_WINDOW_END + (total - 30.0) / RATE_FAST + JOB_SECONDS * (count - 1))
        print(f"{job.part:4} {job.name:44} {seconds:8.2f} {rec:8.2f} {peak:6.3f} {total:13.2f} {wall:13.1f}", file=out)
        rows.append(f"{job.part:2} {job.name}: {seconds:.2f} s ({rec:.2f} s recorded), peak {peak:.3f}; {job.note}")
    fast = SWITCH_WINDOW_END + (total - 30.0) / RATE_FAST + JOB_SECONDS * (len(jobs) - 1)
    print(file=out)
    print(f"total: {total:.2f} s of audio in {len(jobs)} jobs, {recorded:.2f} s recorded "
          f"({recorded * SR * 8 / 1e9:.2f} GB) plus {stimulus_bytes / 1e9:.2f} GB of stimuli", file=out)
    print(f"estimated wall time from Start: {wall:.0f} s at {RATE_SLOW:.0f}x (session E), {fast:.0f} s at {RATE_FAST:.0f}x (session D); "
          f"both include the pause for the switch", file=out)
    for part in PART_ORDER[1:]:
        if part in marks:
            print(f"  behind part {part:2}: {marks[part][0]:8.0f} s of audio, {marks[part][1]:5.0f} s of wall time at {RATE_SLOW:.0f}x, "
                  f"{marks[part][2]:5.0f} s at {RATE_FAST:.0f}x", file=out)
    for line in verify_against_session_d():
        print(line, file=out)
    for variant, text in (("attended", INSTRUCTION), ("--before-start", INSTRUCTION_BEFORE_START), ("--unattended", INSTRUCTION_UNATTENDED)):
        fits = "fits" if len(text) <= INSTRUCTION_LIMIT else "TOO LONG"
        print(f"instruction in the window, {variant} ({len(text)} characters, {fits}: the window shows about {INSTRUCTION_LIMIT}): {text}", file=out)
    return {"audioSeconds": total, "recordedSeconds": recorded, "wallSlow": wall, "wallFast": fast, "jobs": len(jobs), "rows": rows}


# ---------------------------------------------------------------- the session

def sha256(array: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(array, dtype=np.float32).tobytes()).hexdigest()


def save_recording(path: Path, array: np.ndarray) -> None:
    np.save(path, array.astype(np.float32, copy=False))


def save_stimulus(folder: Path, stem: str, array: np.ndarray) -> str:
    """Long sparse stimuli (the maps) are kept compressed (.npz, key "stimulus"); everything else as .npy."""
    sparse = array.nbytes > 64e6 and np.count_nonzero(array) < 0.05 * array.size
    path = folder / (f"{stem}.stimulus.npz" if sparse else f"{stem}.stimulus.npy")
    if not path.exists():
        if sparse:
            np.savez_compressed(path, stimulus=array)
        else:
            np.save(path, array)
    return path.name


def write_json(path: Path, data) -> None:
    partial = path.with_name(path.name + ".partial")
    partial.write_text(json.dumps(data, indent=1, default=str))
    partial.replace(path)


def recording_entry(name: str, part: str, rec, stimulus_file: str | None) -> dict:
    y = rec.output
    rms = float(np.sqrt(np.mean(y.astype(np.float64) ** 2))) if len(y) else 0.0
    return {"name": name, "part": part, "firstFrame": int(rec.first_frame), "warmupSeconds": rec.warmup_seconds,
            "latency": int(rec.latency), "settings": rec.settings, "frames": int(len(y)),
            "peak": float(np.abs(y).max()) if len(y) else 0.0, "rms": rms, "rmsDb": float(20 * np.log10(rms + 1e-300)),
            "sha256": sha256(y), "stimulus": stimulus_file,
            "secondsSinceCreation": rec.step.get("secondsSinceCreation"), "stepWallSeconds": rec.step.get("wallSeconds")}


def run_session(arguments) -> int:
    label, attended = arguments.label, not arguments.unattended
    out = Path(arguments.out_root) / f"session_{label}"
    if out.exists() and any(out.iterdir()):
        print(f"{out} already holds a session; choose another label (sessions A to E exist)", file=sys.stderr)
        return 2
    jobs = build_programme(arguments.probes)
    last_part = PART_ORDER.index(arguments.until) if arguments.until else len(PART_ORDER) - 1
    switch_after_start = attended and not arguments.before_start
    instruction = INSTRUCTION_UNATTENDED if not attended else (INSTRUCTION_BEFORE_START if arguments.before_start else INSTRUCTION)
    if len(instruction) > INSTRUCTION_LIMIT:
        print(f"the instruction has {len(instruction)} characters; the window shows about {INSTRUCTION_LIMIT}", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)

    session = rs.Session.open(instruction=instruction, sample_rate=SR, plugin_format="AudioUnit",
                              start_after=None if attended else 3.0)
    print("window open:", session.folder, flush=True)
    print("instruction in the window:", instruction, flush=True)
    created = datetime.datetime.fromisoformat(session.record()["instanceCreatedAt"])

    def seconds_left() -> float:   # until the host stops by itself
        return STOP_MINUTES * 60.0 - (datetime.datetime.now(created.tzinfo) - created).total_seconds()

    info = {"label": label, "script": "probe4.py", "variant": "attended" if attended else "unattended (Tide)",
            "order": "mode before Start" if arguments.before_start else ("mode after Start, in the pause behind p000" if attended else "no mode change"),
            "instruction": instruction, "sessionFolder": str(session.folder), "sampleRate": SR,
            "instanceCreatedAt": created.isoformat(), "firstAbyssProbe": None, "switchWindow": None,
            "probes": [], "cases": [], "skipped": [], "failed": [], "modeChecks": [], "partsDone": [], "outcome": "running",
            "planned": [{"part": job.part, "name": job.name} for job in jobs if PART_ORDER.index(job.part) <= last_part]}
    ran = set()
    pictures = {"possible": True}

    def save_info() -> None:
        write_json(out / "info.json", info)

    def picture(name: str) -> None:
        """A picture of the session's window. It is never worth a session: screencapture can be refused or hang
        (the driver gives it 30 s), so a failure is printed and no second picture is tried."""
        if not pictures["possible"]:
            return
        try:
            if not session.screenshot(session.folder / name):
                print(f"no picture {name} (the terminal may not record the screen)", flush=True)
        except Exception as problem:
            pictures["possible"] = False
            print(f"no picture {name}: {type(problem).__name__}: {problem}", flush=True)

    def keep(content: Content, recordings, part: str, audio: bool = True) -> None:
        """Every recording gets its line in info.json (with its SHA-256); `audio` says whether the samples are kept too."""
        if len(recordings) != len(content.names):
            raise RuntimeError(f"{len(recordings)} recordings for {len(content.names)} recorded steps")
        files = {stem: save_stimulus(out, stem, array) for stem, array in content.stimuli.items()}
        for rec, (name, stem) in zip(recordings, content.names):
            if audio:
                save_recording(out / f"{name}.npy", rec.output)
            info["cases"].append(dict(recording_entry(name, part, rec, files.get(stem)), saved=audio))
        save_info()

    save_info()
    try:
        session.wait_until_started(max(30.0, seconds_left() - 90.0))
        started = time.monotonic()
        info["startedSecondsAfterCreation"] = round(STOP_MINUTES * 60.0 - seconds_left(), 1)
        print("started at", time.strftime("%H:%M:%S"), "- seconds left:", round(seconds_left()), flush=True)

        # ---- part A: the probes of session D, with the pause for the owner's switch behind p000
        target, first, heard, k, complete = arguments.probes, None, 0, 0, True
        while k < target:
            if seconds_left() < MARGIN_SECONDS + 5.0:
                complete = False
                print("OUT OF TIME in the probes", flush=True)
                break
            record_flush = heard < 3 and k < RECORD_FLUSH_UNTIL
            waiting = k >= arguments.probes and first is None      # the owner has not switched in 100 probes: keep one in ten
            content = probe_content(k, record_flush)
            # p001 is the first probe that can say whether the switch in the pause was heard; from the probe behind
            # it on, a sound that is still Tide is written into the job name, which the window shows to the owner.
            still_tide = attended and first is None and k > (1 if switch_after_start else 0)
            recordings = session.run(content.steps, label=f"p{k:03d}" + (STILL_TIDE if still_tide else ""), timeout=120.0).recordings
            rec = recordings[-1]
            d = describe(rec.output)
            d.update(index=k, wallSeconds=round(time.monotonic() - started, 1), warmupSeconds=rec.warmup_seconds,
                     firstFrame=int(rec.first_frame), sha256=sha256(rec.output), abyss=bool(d["L_400to600"] > ABYSS_DB))
            info["probes"].append(d)
            keep(content, recordings, "A", audio=not waiting or k % 10 == 0 or d["abyss"])
            print(json.dumps(d), flush=True)
            if d["abyss"]:
                heard += 1
                if first is None:
                    first = k
                    info["firstAbyssProbe"] = k
                    if k >= target - 4:               # Abyss came late: go on to the next multiple of ten probes
                        target = -(-(k + 5) // 10) * 10
                    print(f"ABYSS from probe {k}; the batch starts behind probe {target - 1}", flush=True)
            elif first is not None:
                print(f"WARNING: probe {k} does not sound like Abyss any more", flush=True)
            if k == 0 and switch_after_start:
                opens = time.monotonic() - started
                closes = max(SWITCH_WINDOW_END, opens + SWITCH_WINDOW_MIN)
                info["switchWindow"] = {"behindProbe": 0, "framesProcessed": int(rec.first_frame + len(rec.output)),
                                        "opensSecondsAfterStart": round(opens, 2), "closesSecondsAfterStart": round(closes, 2)}
                print(f"PAUSE for the switch to Abyss: the instance is idle until {closes:.1f} s after Start", flush=True)
                time.sleep(max(0.0, closes - (time.monotonic() - started)))
                picture("window-after-switch.png")
                print("pause over, probes go on", flush=True)
            if k == (1 if switch_after_start else 0) and attended and first is None:
                print(f"MODE NOT HEARD in p{k:03d}: the sound is still Tide. Switch the mode now (away and back to Abyss); "
                      "the probes go on, their job names in the window say so, and the session stays usable", flush=True)
            k += 1
            if k == target and attended and first is None:
                if seconds_left() > 240.0:
                    target += 10
                    print(f"Abyss not heard in {k} probes; ten more (switch the mode away and back to Abyss)", flush=True)
                else:
                    complete = False
                    print("ABYSS NEVER HEARD; nothing more is recorded", flush=True)
        if complete:
            ran.update(job.name for job in jobs[:arguments.probes])
        info["probesRun"] = k
        save_info()
        picture("window-after-probes.png")
        print("probes done, seconds left:", round(seconds_left()), flush=True)

        # ---- part B (session D's batch) and the structural programme
        part = "A"
        for job in jobs[arguments.probes:]:
            if not complete or PART_ORDER.index(job.part) > last_part:
                break
            if job.part != part:
                part = job.part
                print(f"part {part}, seconds left: {round(seconds_left())}", flush=True)
            left = seconds_left()
            if left < MARGIN_SECONDS + wall_estimate(24.0):        # not even the shortest case fits any more
                print("OUT OF TIME before", job.name, flush=True)
                info["skipped"].append({"name": job.name, "part": job.part, "secondsLeft": round(left, 1), "reason": "out of time"})
                complete = False
                break
            content = job.make()
            seconds = audio_frames(content.steps) / SR
            estimate = wall_estimate(seconds)
            if left < estimate + MARGIN_SECONDS:
                print(f"SKIPPED {job.name}: needs about {estimate:.0f} s, {left:.0f} s left", flush=True)
                info["skipped"].append({"name": job.name, "part": job.part, "secondsLeft": round(left, 1), "reason": "does not fit"})
                save_info()
                continue
            begun = time.monotonic()
            try:
                result = session.run(content.steps, label=job.name, timeout=120.0 + seconds / 4.0)
                keep(content, result.recordings, job.part)
                if content.events is not None:
                    write_json(out / f"{job.name}.events.json", content.events)
            except Exception as problem:
                # One case must not cost the rest of the instance. The driver refuses a job for reasons that leave
                # the instance as it is (a read-back that differs on a control no Audio Unit session has moved yet,
                # a file it cannot read); the host has then run the job, so the cases behind it keep their processed
                # time (a job the host itself rejected has processed nothing: the entries behind it show their own
                # firstFrame). A host that has stopped, or that does not answer in time, ends the session.
                if isinstance(problem, TimeoutError) or not session.alive() or session.record().get("status") != "running":
                    raise
                failure = {"name": job.name, "part": job.part, "error": f"{type(problem).__name__}: {problem}",
                           "secondsLeft": round(seconds_left(), 1), "hostResults": str(session.folder / "results")}
                info["failed"].append(failure)
                save_info()
                print(f"FAILED {job.name}: {failure['error']}; kept by the host under {failure['hostResults']}; "
                      "the session goes on", flush=True)
                continue
            ran.add(job.name)
            try:      # does it still sound like Abyss? (never worth a session either)
                for rec, (name, _) in zip(result.recordings, content.names):
                    if name in MODE_CHECKS:
                        window = rec.output[at(MODE_CHECKS[name]):at(MODE_CHECKS[name] + 6.0), 0].astype(np.float64)
                        share = round(_share(np.abs(np.fft.rfft(window * np.hanning(at(6)))) ** 2, 400, 600), 1)
                        check = {"recording": name, "warmupSeconds": rec.warmup_seconds, "L_400to600": share,
                                 "abyss": bool(share > ABYSS_DB)}
                        info["modeChecks"].append(check)
                        save_info()
                        if check["abyss"]:
                            print(f"MODE CHECK {name}: still Abyss ({share} dB)", flush=True)
                        elif first is not None:
                            print(f"MODE CHECK {name}: THE SOUND IS NOT ABYSS ANY MORE ({share} dB); what was recorded "
                                  "behind the last good check is in doubt. Ask for the mode to be switched away and back", flush=True)
                        else:
                            print(f"MODE CHECK {name}: not Abyss ({share} dB), as in the probes", flush=True)
            except Exception as problem:
                print(f"no mode check behind {job.name}: {type(problem).__name__}: {problem}", flush=True)
            last = info["cases"][-1]
            print(f"{job.name} ok: {seconds:.0f} s of audio in {time.monotonic() - begun:.1f} s, peak {last['peak']:.4f}, "
                  f"rms {last['rmsDb']:.1f} dB", flush=True)
        wanted = [job for job in jobs if PART_ORDER.index(job.part) <= last_part]
        info["notRun"] = [job.name for job in wanted if job.name not in ran]
        info["outcome"] = "complete" if not info["notRun"] else "cut short"
        picture("window-end.png")
    except BaseException as problem:      # the demo limit, a closed window, Ctrl-C: keep what there is
        info["outcome"] = f"stopped: {type(problem).__name__}: {problem}"
        print("STOPPED:", info["outcome"], flush=True)
        raise
    finally:
        info["partsDone"] = [part for part in PART_ORDER
                             if any(job.part == part for job in jobs) and all(job.name in ran for job in jobs if job.part == part)]
        info["secondsLeftAtEnd"] = round(seconds_left(), 1)
        save_info()                       # before the window is closed: the outcome is on disk whatever closing does
        try:
            info["record"] = session.close()
        except Exception as problem:
            info["record"] = {"error": f"closing the session failed: {type(problem).__name__}: {problem}"}
        save_info()
        print("session closed:", str(info["record"])[:300], flush=True)
        print(f"outcome: {info['outcome']}; first Abyss probe: {info['firstAbyssProbe']}; "
              f"{len(info['cases'])} recordings in {out}", flush=True)
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Session D's jobs and the structural programme in one attended Abyss session.")
    parser.add_argument("label", nargs="?", help="the session becomes session_<label>/ (A to E exist)")
    parser.add_argument("--unattended", action="store_true", help="dry run in Tide: the host starts by itself after 3 s")
    parser.add_argument("--list", action="store_true", help="print every case and the totals, open nothing")
    parser.add_argument("--until", choices=PART_ORDER, help="stop behind this part")
    parser.add_argument("--probes", type=int, default=PROBES, help="probes in part A (dry runs only)")
    parser.add_argument("--before-start", action="store_true", help="mode first, then Start: the order that failed in session E")
    parser.add_argument("--out-root", default=str(HERE), help="where session_<label>/ is made")
    arguments = parser.parse_args(argv)
    if arguments.probes < 2 or arguments.probes % 2:
        parser.error("--probes takes an even number of at least 2 (the recordings behind part B then start on multiples of 4 s)")
    if arguments.probes != PROBES and not (arguments.unattended or arguments.list):
        parser.error("an attended session runs session D's 100 probes; --probes is for dry runs")
    if arguments.list:
        list_programme(arguments.probes)
        return 0
    if not arguments.label or not re.fullmatch(r"[A-Za-z0-9_-]+", arguments.label):
        parser.error("a label of letters, digits, _ or - is needed")
    if arguments.unattended and arguments.before_start:
        parser.error("--before-start is for attended sessions")
    return run_session(arguments)


if __name__ == "__main__":
    sys.exit(main())
