#!/usr/bin/env python3
"""First reading of a tempo session (../probe7_tempo.py): are the chunks of Abyss seconds or note values, and do the
chunk clocks count processed frames or follow the play head? Model-free, from the impulse scan t04 alone.

    tempo_quicklook.py <label>                         reads ../session_<label>/info.json and its t04 recording
    tempo_quicklook.py --recording R.npy --stimulus S.npy --warmup SECONDS [--bpm B] [--offset O]
                                                       any recording of isolated impulses at Macro 100 %, Decay 0.5 s
                                                       (checked on session E: x02 and x03, see the end of this text)

How it reads. At Decay 0.5 s the network answers an impulse within about 0.2 s. The same-pitch voice of Abyss plays
the input backwards in chunks: an impulse at time t comes back once more, as a click, at 2 M - t, where M is the
mirror point at the end of its chunk (0 to 1.5 ms in front of the chunk boundary). So the envelope of the output
above 8 kHz (where the octave-down copy is weak) repeats its first 0.2 s after a delay d, and M = t + d / 2. For
every impulse the script takes the strongest repeats and

  1. asks four readings where the mirror point should be: chunk length in seconds or as a note value (2/3 s at
     120 BPM = a third of a bar), counted from the first processed frame or from the play head (processed time plus
     the session's transport offset);
  2. lists, without any reading, how far apart the mirror points of neighbouring impulses lie (strongest repeat of
     each): in one voice that is a whole number of chunks, so the chunk lengths show as a few sharp values.

Impulses whose copy comes back within 0.15 s are not read (the copy lies under the direct answer and is faded), so
about four in five can match at best. A wrong reading matches a few per cent by chance.

The reading leans on one property of the base: its answer begins 10 ms behind the impulse (Size 100 %; TEMPLATE starts
there). The correlation is not normalised by the level of the stretch it is laid on, so an answer that had already
begun in front of the template would read every delay short by up to those 10 ms (seen in review with a made-up
answer, script_review/quicklook_synthetic.py). Do not use it unchanged at another Size or with a pre-delay.

Checked on session E (120 BPM, offset 0), where the answer is known (2/3 s, counted from the first frame); the
recordings were then read as if the session had been at 90 BPM with the play head 0.25 s ahead, to see the wrong
readings fail (tempo_quicklook.check.log):
    x02 (left, 149 impulses):  seconds from the first frame 130, seconds on the play head 0, note value on the
                               play head 0, note value from the first frame 30
    x03 (right, 74 impulses):  58, 0, 0, 21
The 30 and 21 are no fault: every fourth boundary of a 2/3 s grid is a boundary of an 8/9 s grid too, when both
start at the same instant. A reading therefore holds when it matches most impulses and at least twice as many as
the next one. Mirror points lie 1.1 ms in front of the boundary (median), as the mirror law says.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import butter, find_peaks, sosfiltfilt

HERE = Path(__file__).resolve().parent
ABYSS = HERE.parent
SR = 48000
CHUNK_SECONDS = float(np.float32(2.0 / 3.0))     # the same-pitch voice at 120 BPM (measured: fl32(2/3) s)
TEMPLATE = (0.010, 0.200)       # the direct answer, seconds behind the impulse
LAGS = (0.150, 3.750)           # where a repeat is looked for (the next impulse comes 4.01 to 4.03 s later)
SMOOTH = 0.002                  # envelope smoothing, seconds
PEAKS = 4                       # strongest repeats kept per impulse
TOLERANCE = (-0.0035, 0.0015)   # mirror point minus chunk boundary, seconds (law: -1.5 ms to 0, plus the reading's 1 ms)


def envelope(y: np.ndarray) -> np.ndarray:
    sos = butter(4, 8000.0, "highpass", fs=SR, output="sos")
    h = sosfiltfilt(sos, y.astype(np.float64))
    width = int(round(SMOOTH * SR))
    return np.sqrt(np.convolve(h * h, np.ones(width) / width, mode="same"))


def repeats(recording: np.ndarray, frame: int, channel: int) -> list:
    """[(delay in seconds, gain)] of the strongest repeats of the direct answer's envelope behind one impulse."""
    a, b = int(TEMPLATE[0] * SR), int(TEMPLATE[1] * SR)
    lo, hi = int(LAGS[0] * SR), int(LAGS[1] * SR)
    end = min(frame + hi + b + 1, len(recording))
    if end - frame < b + lo + SR // 10:
        return []
    env = envelope(np.asarray(recording[frame:end, channel]))
    template = env[a:b]
    energy = float(template @ template)
    if energy <= 0.0:
        return []
    score = np.correlate(env[a + lo:], template, mode="valid") / energy      # score[i] belongs to lag lo + i
    peaks, _ = find_peaks(score, distance=SR // 50)                          # local maxima at least 20 ms apart
    strongest = sorted(peaks, key=lambda i: -score[i])[:PEAKS]
    return [((lo + int(i)) / SR, float(score[i])) for i in strongest]


def boundary_miss(mirror: float, length: float) -> float:
    """Mirror point minus the nearest chunk boundary k * length, seconds."""
    return mirror - length * round(mirror / length)


def read(recording, impulses: list, warmup: float, bpm: float, offset: float, out=sys.stdout) -> dict:
    rows = []
    for frame, channel in impulses:
        found = repeats(recording, frame, channel)
        if found:
            rows.append((warmup + frame / SR, found))
    if len(rows) < 3:
        print(f"only {len(rows)} impulses of {len(impulses)} could be read: the recording is too short for a verdict", file=out)
        return {"impulses": len(rows), "readings": []}
    print(f"{len(rows)} impulses read of {len(impulses)}; processed time {rows[0][0]:.1f} to {rows[-1][0]:.1f} s", file=out)
    note = CHUNK_SECONDS * 120.0 / bpm
    readings = [("seconds, counted from the first processed frame", CHUNK_SECONDS, 0.0),
                ("seconds, following the play head", CHUNK_SECONDS, offset),
                ("note value, following the play head", note, offset),
                ("note value, counted from the first processed frame", note, 0.0)]
    result = {"impulses": len(rows), "readings": []}
    print(f"same-pitch chunk: {CHUNK_SECONDS:.6f} s if seconds, {note:.6f} s if a note value at {bpm:g} BPM; transport offset {offset:g} s", file=out)
    for name, length, shift in readings:
        hits, misses = 0, []
        for t, found in rows:
            best = min((boundary_miss(t + shift + delay / 2.0, length) for delay, _ in found), key=abs)
            if TOLERANCE[0] <= best <= TOLERANCE[1]:
                hits += 1
                misses.append(best)
        median = float(np.median(misses)) * 1e3 if misses else float("nan")
        result["readings"].append({"reading": name, "chunkSeconds": length, "matched": hits, "medianMissMs": median})
        print(f"  {name:52} chunk {length:.6f} s: {hits:3d} of {len(rows)} impulses have a repeat on a boundary"
              + (f" (median {median:+.2f} ms)" if misses else ""), file=out)
    same = abs(note - CHUNK_SECONDS) < 1e-9
    if same and offset == 0.0:
        print("  (120 BPM and no offset: the four readings are one and the same)", file=out)
    # Without any reading: the mirror points of two neighbouring impulses, in the same voice, lie a whole number of
    # chunks apart. Only the strongest repeat of each impulse is used (the others are mostly side lobes of it, 23 ms
    # apart: the network's own pattern), so most differences are between two same-pitch copies.
    strongest = [t + max(found, key=lambda p: p[1])[0] / 2.0 for t, found in rows]
    differences = np.diff(np.array(strongest))
    step = 0.001                                                               # 1 ms bins; a pile is 5 bins wide
    counts = np.bincount(np.round((differences - differences.min()) / step).astype(int))
    piled = np.convolve(counts, np.ones(5, dtype=int), mode="same")
    clusters = []
    while len(clusters) < 8:
        i = int(np.argmax(piled))
        if piled[i] < 3:
            break
        inside = np.abs(differences - (differences.min() + i * step)) <= 2.5 * step
        clusters.append((float(np.median(differences[inside])), int(inside.sum())))
        piled[max(0, i - 6): i + 7] = 0
    voices = (("same pitch", 2.0 / 3.0), ("octave up", 1.0), ("octave down", 4.0 / 3.0))
    print(f"distance between the mirror points of neighbouring impulses (a whole number of chunks; {len(rows) - 1} neighbour pairs):", file=out)
    result["mirrorDistances"] = []
    for value, count in clusters:
        labels = []
        for kind, scale in (("seconds", 1.0), ("note value", 120.0 / bpm)):
            for voice, seconds in voices:
                multiple = round(value / (seconds * scale))
                if multiple > 0 and abs(value - multiple * seconds * scale) < 0.003:
                    labels.append(f"{multiple} x {seconds * scale:.4f} s ({voice}, {kind})")
        if abs(120.0 / bpm - 1.0) < 1e-9:
            labels = [label.replace(", seconds)", ")") for label in labels if "note value" not in label]
        result["mirrorDistances"].append({"seconds": value, "pairs": count, "fits": labels})
        print(f"  {value:8.4f} s  in {count:3d} pairs   " + ("; ".join(labels) if labels else "fits no chunk of either reading"), file=out)
    top = max(result["readings"], key=lambda r: r["matched"])
    second = sorted(r["matched"] for r in result["readings"])[-2]
    if top["matched"] >= 0.5 * len(rows) and (same and offset == 0.0 or top["matched"] >= 2 * max(second, 1)
                                             or sum(r["matched"] == top["matched"] for r in result["readings"]) > 1):
        tied = [r["reading"] for r in result["readings"] if r["matched"] >= 0.9 * top["matched"]]
        print("VERDICT: " + " / ".join(tied) + (" (these cannot be told apart at this tempo and offset)" if len(tied) > 1 else ""), file=out)
    else:
        print("VERDICT: none of the four readings holds; see the free period above and analyse t02, t03 and t04 with the model", file=out)
    return result


def impulses_of(stimulus: np.ndarray) -> list:
    frames, channels = np.nonzero(np.asarray(stimulus))
    return [(int(f), int(c)) for f, c in zip(frames, channels)]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="First reading of a tempo session.")
    parser.add_argument("label", nargs="?")
    parser.add_argument("--recording")
    parser.add_argument("--stimulus")
    parser.add_argument("--warmup", type=float)
    parser.add_argument("--bpm", type=float)
    parser.add_argument("--offset", type=float)
    parser.add_argument("--root", default=str(ABYSS), help="folder that holds session_<label>/ (default: next to probe7_tempo.py)")
    arguments = parser.parse_args(argv)
    info = None
    if arguments.label:
        folder = Path(arguments.root) / f"session_{arguments.label}"
        info = json.loads((folder / "info.json").read_text())
        line = next((case for case in info["cases"] if case["name"] == "t04_scan_macro100_decay0p5"), None)
        if line is None:      # skipped for want of time, or failed: info.json says which
            print(f"session {arguments.label} has no recording of the scan t04 (outcome {info.get('outcome')!r}, skipped "
                  f"{[entry['name'] for entry in info.get('skipped', [])]}, failed {[entry['name'] for entry in info.get('failed', [])]}); "
                  "there is nothing for this quick look to read")
            return 1
        recording = np.load(folder / "t04_scan_macro100_decay0p5.npy", mmap_mode="r")
        impulses = [(e["frame"], e["channel"]) for e in info["events"]["t04_scan_macro100_decay0p5"]["impulses"]]
        warmup = line["warmupSeconds"]
        bpm = info["bpm"] if arguments.bpm is None else arguments.bpm
        offset = info["transportOffsetSeconds"] if arguments.offset is None else arguments.offset
        print(f"session {arguments.label}: {info['bpm']:g} BPM, transport offset {info['transportOffsetSeconds']:g} s, "
              f"host block {info.get('blockSize')}; outcome {info.get('outcome')!r}; end probe Abyss: {info.get('endProbeAbyss')}")
    elif arguments.recording and arguments.stimulus and arguments.warmup is not None:
        recording = np.load(arguments.recording, mmap_mode="r")
        impulses = impulses_of(np.load(arguments.stimulus, mmap_mode="r"))
        warmup = arguments.warmup
        bpm = 120.0 if arguments.bpm is None else arguments.bpm
        offset = 0.0 if arguments.offset is None else arguments.offset
        print(f"{arguments.recording}: warm-up {warmup:g} s, read as {bpm:g} BPM with transport offset {offset:g} s")
    else:
        parser.error("give a session label, or --recording, --stimulus and --warmup")
    if info is not None and str(info.get("variant", "")).startswith("unattended"):
        print("NOTE: this was the unattended dry run: the sound is Tide, which has no copies; expect no reading to hold")
    read(recording, impulses, warmup, bpm, offset)
    return 0


if __name__ == "__main__":
    sys.exit(main())
