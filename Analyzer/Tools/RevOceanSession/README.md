# Rev OCEAN Session

A small host for the black-box campaign of `Analyzer/Campaigns/RevOceanCharacterization`. It loads
Arturia Rev OCEAN 1.0.0.5848, creates one instance, shows that instance's own editor in a window, and
after its owner has pressed Start runs capture jobs inside that same instance.

It exists because the Macro Mode of the reference (Abyss, Foam, Tide) is not a host parameter. The
campaign's Analyzer creates a fresh instance for every capture and never shows an editor, so it only
ever measures the mode a fresh instance starts in, Tide. The demo keeps no state when it is closed, so
a mode chosen by hand lasts exactly as long as the instance it was chosen in. Here that instance stays
open and takes every capture.

## Boundary

The tool hosts the plug-in the way a DAW does and nothing more.

- It loads the plug-in by its path, sets host parameters, gives it audio and reads the audio it returns.
  A bundle that ends in `.component` is loaded as an Audio Unit, anything else as a VST3. Attended
  sessions use the Audio Unit: in the VST3 host a mode chosen in the editor does not reach the sound
  (campaign README, "Attended sessions host the Audio Unit").
- It never reads or changes the plug-in's files apart from loading them. The one file it hashes is the
  plug-in's binary, whose SHA-256 the campaign pins (`revocean.PLUGIN_BINARY_SHA256` for the VST3,
  `revocean_session.HOSTED` for the Audio Unit); a different binary is refused.
- It never stores or restores the plug-in's state, and it does nothing about the demo: the plug-in runs
  in its normal demo mode and the tool stops before the demo's limit (see "Time").
- It never sends mouse or keyboard events to the plug-in. Whatever is chosen in the editor is chosen by
  the person at the screen.

## Build

JUCE 8.0.14 from the checkout next to this repository (`../Juce`; otherwise pass
`-DREVOCEAN_SESSION_JUCE_SOURCE=<path>`), arm64, Release, into `build-session` (ignored by Git):

```sh
cd /Users/nespesha/Workspace/AmanitaOcean
cmake -S Analyzer/Tools/RevOceanSession -B build-session -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-session
```

The application is
`build-session/RevOceanSession_artefacts/Release/Rev OCEAN Session.app/Contents/MacOS/Rev OCEAN Session`.

## Run

Normally through the campaign's driver, which writes the session folder, starts the tool and hands it jobs:

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY revocean_session.py            # opens a session that waits for its owner; Ctrl-C closes it
```

```python
import revocean, revocean_session as rs
session = rs.Session.attach()                    # the open session, from another Python
capture = session.run(rs.case(stimulus, {"decay": revocean.normalised("decay", 2.0)}, session.sample_rate)).recordings[0]
session.close()                                  # ends the session and its window
```

Directly:

```sh
"Rev OCEAN Session" --session <folder> [--start-after <seconds>]
```

`<folder>` holds `session.json`. Without `--start-after` the tool waits for the Start button; with it,
it starts by itself that many seconds after its window opened (for unattended checks, which measure the
mode of a fresh instance, Tide). Exit code 0 when the session ran and stopped, 1 when it could not be opened.

## What the owner does in the window

The window shows a line of instruction, a Start button, the time left, and under them the plug-in's editor.

1. The plug-in shows its own Authorization box, because it is not activated. Press **Demo** in it.
2. Select the mode with the arrows next to the mode name (`<  Tide  >`). Change nothing else: every
   knob is a host parameter and is set by the jobs anyway.
3. Press **Start**. From here on leave the editor alone; the window may be covered or put aside.
4. The window closes by itself when the driver ends the session or when the time is up. Closing it by
   hand ends the session.

Nothing is processed before Start, so the instance's sample clock is at zero when the first job begins.
That is why Demo and the mode come before Start.

The first time a person sits at the window it is worth leaving the mode at Tide: pressing Demo is the
one thing the automatic checks could not do, and a first case in Tide must still be the Analyzer's capture.

```python
import numpy as np, revocean, revocean_session as rs
session = rs.Session.attach()
session.wait_until_started(900.0)
stimulus = revocean.impulses(3.0, [(4800, None, 0.5)])
settings = {"decay": revocean.normalised("decay", 0.5)}
capture = session.run(rs.first_case(stimulus, settings, session.sample_rate)).recordings[0]
print(np.array_equal(capture.output, revocean.capture(stimulus, settings).output[:len(stimulus)]))   # True when Demo changed nothing
session.close()
```

The driver's `python revocean_session.py` keeps a picture of the window as it was at Start
(`window-at-start.png` in the session folder, taken by the system's `screencapture` of this window alone).
It is the only record of the mode: the host interface cannot read it. The picture stays in the
ignored results folder; it shows the account name in the Authorization box if that is still open.

## Time

Measured on 8 October 2026 in Tide, unattended (numbers in the campaign's README, "Session captures"):

- The demo's limit is wall-clock time from the creation of the instance: 20 minutes, whether or not
  anything is processed. An instance that processed its first frame 2:00 after its creation was still
  itself at 19:55.5 and was cut off at 20:00.4, not at 22:00.
- Processed audio does not count. Offline the reference runs about 50 times faster than real time;
  50 minutes of audio went through one instance in 65 s.
- At the limit the plug-in hands its input back untouched, bit for bit and without its latency (seen at
  Mix 100 and 50 %, Master 0 and -6 dB). Its parameters still answer and it still reports its latency;
  the host is not blocked and no window opens. The Authorization box in the editor then reads "The time
  limit of the demo mode has been reached" and has no Demo button any more.

So the window counts the 20 minutes down from the creation of the instance, and the tool ends a
session 19 minutes after it by default (`stopAfterMinutes`), one minute in front of the limit. The
time a person takes before Start is part of those 19 minutes; what is left of them is some 50 times
as much audio. The driver refuses a recording that equals its stimulus bit for bit, which is what an
instance returns once its time is over.

Not measured: whether pressing Demo in the Authorization box moves that clock. No automatic run may
press it, so every check ran with the box open.

While a session lasts the tool asks macOS not to nap its process (with the window hidden or covered,
App Nap slowed processing to a tenth) and not to put an idle machine to sleep.

## How it processes

One instance, created at the session's sample rate and block size and prepared in the Analyzer's order:
stereo main buses with every other input bus off, offline mode (`setNonRealtime(true)`),
`prepareToPlay`, `reset`. Each step then gives the instance its frames in blocks of the session's block
size with a shorter last block, a play head at 120 BPM in 4/4 that is playing at the number of frames
processed so far, and no MIDI. Parameters are set on the message thread between two blocks, all values
of a step before the next block, and are read back there with their display texts. Audio runs on a
worker thread; the editor stays usable.

The tool counts every frame it gives the instance, from the first. Every clock of the reference found
so far counts the same frames, so the count in front of a recording is the warm-up the campaign's
models need. What captures made this way are worth is measured in the campaign's README, section
"Session captures".

## Files

Everything of a session lives in its folder.

`session.json`, written before the tool starts:

```json
{
  "schema": "revocean.session/1",
  "plugin": {"path": "/Library/Audio/Plug-Ins/VST3/Rev OCEAN.vst3", "name": "Rev OCEAN", "manufacturer": "Arturia",
             "version": "1.0.0.5848", "binarySha256": "eb43f0bd..."},
  "sampleRate": 48000,
  "blockSize": 512,
  "title": "Rev OCEAN session - waiting for you",
  "instruction": "Press Demo in the plug-in's Authorization box, select the mode Abyss ..., then press Start.",
  "stopAfterMinutes": 19,
  "idleSeconds": 0
}
```

`stopAfterMinutes` (optional) is the safety margin, counted from the creation of the instance.
`idleSeconds` (optional) ends the session when no job arrives for that long; 0 waits until the margin.

`queue/<name>.json`, a job. The tool takes the job whose name sorts first, so the driver numbers them
(`0001-...`). A job file must appear whole (write it under another name and rename it). A job is read
and checked completely, stimuli included, before any of it runs:

```json
{
  "schema": "revocean.session.job/1",
  "steps": [
    {"kind": "set", "parameters": [{"id": 0, "value": 0.0}, {"id": 1, "value": 1.0}, "... all fifteen ..."]},
    {"kind": "silence", "frames": 480000, "record": false},
    {"kind": "wav", "file": "0001-job.002.wav", "record": true}
  ],
  "quit": false
}
```

- `set`: host parameters by the format's parameter ID with normalised values (the Audio Unit uses the
  VST3's numbers 1 to 14 and has no ID 0). The campaign sets all of them in
  every step (IDs 0 to 14 and their baseline in `revocean.py`); a parameter the host never set reads
  back as 0 and cannot be trusted. The plug-in declares 2189 parameters (program changes, preset
  navigation, MPE and MIDI controller helpers besides the fifteen); a job that names an ID it does not
  declare is rejected.
- `silence`: that many frames of silence.
- `wav`: a stereo 32-bit float WAV at the session's rate, named relative to the job file.
- `record`: write the output of the step as `results/<name>/step-<index>.wav` (stereo, 32-bit float).
- `quit`: end the session when the job is done.

`results/<name>/record.json`, written last, when the job's recordings are complete:

```json
{
  "schema": "revocean.session.job-record/1",
  "job": "0001-job",
  "status": "ok",
  "error": "",
  "plugin": {"path": "...", "name": "Rev OCEAN", "manufacturer": "Arturia", "version": "1.0.0.5848", "format": "VST3", "binarySha256": "..."},
  "sampleRate": 48000, "blockSize": 512, "reportedLatencySamples": 48,
  "instanceCreatedAt": "2026-10-08T14:00:07.561+03:00",
  "steps": [
    {"index": 0, "kind": "set", "firstFrame": 0, "frames": 0, "startedAt": "...", "secondsSinceCreation": 4.32, "wallSeconds": 0.06,
     "parameters": [{"id": 3, "requested": 0.0, "readback": 0.0, "text": "0.500"}, "..."]},
    {"index": 1, "kind": "silence", "firstFrame": 0, "frames": 480000, "outputPeak": [0.0, 0.0], "outputRms": [0.0, 0.0], "...": "..."},
    {"index": 2, "kind": "wav", "firstFrame": 480000, "frames": 156560, "input": "0001-job.002.wav", "recorded": "step-002.wav", "...": "..."}
  ],
  "framesProcessed": 636560,
  "finishedAt": "..."
}
```

`firstFrame` is the number of frames the instance had processed before the step. `status` is `ok`,
`rejected` (the job was not valid; nothing of it ran and the session goes on) or `failed` (it stopped
on the way: the output was not finite, a file could not be written, or the session ended).

`session-record.json`, rewritten whenever the state changes: the same identity block, `status`
(`waiting_for_start`, `running`, `stopping`, `stopped`, `failed`), `stopReason`, `error`, the jobs done,
the frames processed, the process ID and the window number.

## Stopping

A session ends when a job says `quit`, when its window is closed (`window_closed`), when no job arrived
for `idleSeconds` (`idle`), at the safety margin (`demo_margin`), or on SIGTERM or SIGINT (`terminated`);
a job that is running then is cut short and marked `failed`. `stopReason` in the session record says which. No wait is open-ended: the
wait for Start and for jobs ends at the margin, a parameter step waits at most 10 s for the message
thread, and if the plug-in does not return from processing within 20 s of a stop the tool records that
as an error and ends its process.

## Limits

- macOS on Apple silicon only; one instance per process (several processes may run side by side).
- The tool cannot see or set the mode. The only evidence of it is the picture of the window.
- It cannot press the plug-in's Demo button; unattended runs leave the Authorization box open, which
  did not change the audio of any check.
