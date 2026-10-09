# session_tool_verification: independent check of the session capture tool

8 October 2026. Checked: `Analyzer/Tools/RevOceanSession` (host, about 1,300 lines with its CMakeLists.txt) and `revocean_session.py`
(driver). Reference: Arturia Rev OCEAN 1.0.0.5848, **Tide, Macro 0, unattended** (`--start-after`), 48 kHz,
block 512, the demo's Authorization box left open. Nothing here was done with a person at the window and
nothing is a measurement of Abyss or Foam.

The host was rebuilt from its sources into `build-session-verify` (JUCE 8.0.14, arm64 Release, 27 steps,
0 warnings). Jobs, stimuli, settings and analysis are this packet's own; scripts and numbers are in
`Analyzer/Results/RevOceanCharacterization/work/session_verification` (ignored by Git), which also holds
its own capture cache and session folders. A null is `20 log10(rms(a - b) / rms(b))`, nothing fitted.

## 0. Verdict

**The tool is fit for a session in which the owner selects the mode by hand.** As a host it does what its
README says: it processes nothing before Start, counts frames exactly, applies parameters at the frame it
records, repeats bit for bit, and ends cleanly however it is stopped. One defect can cost a hand-made
session (F1), one check of the driver gives a false alarm in a corner (F2), and the flush has less margin
than documented when the next case has Pre-delay (F3). None of them blocks the use; section 7 says what
the owner has to be told.

What nobody has verified, engineer or verifier: pressing Demo, pressing Start by a real click, selecting
a mode, whether a mode survives the jobs, and whether anything proven in Tide (repeatability, the flush,
clocks that count frames) holds in Abyss.

## 1. Findings

**F1. A step with an absurd frame count is accepted, and a running job cannot be cancelled** (medium).
`Tools/RevOceanSession/Source/Job.cpp:105-108` accepts any positive whole number of frames;
`Session.cpp:464-470` then runs it until the session stops. Scenario, job 0114 of this packet's first
session: `{"kind": "silence", "frames": 99999999999999999999999}`. JUCE's JSON reader wraps the literal
to 200,376,420,520,689,663 and the host took it. The step ran 660.2 s and 1,563,018,240 frames (9.05 hours
of audio); the 27 jobs queued behind it (26 of the batch and the next case) never ran; the window stayed on screen 11 min 07 s and ended
only because the driver's `close()` sent SIGTERM. Alone it would have run to the 19-minute margin.
`revocean_session.silence` (`revocean_session.py:84-87`) lets the same through: any positive integer.
In an attended session one wrong number therefore ends the session, and the mode chosen by hand with it.
Evidence: `oversized_silence.json`, `sessions/e1-attempt1-oversized-silence`.

**F2. The driver's "demo time is over" check misfires on dry, steady input and misses a recording that
straddles the limit** (low). `revocean_session.py:325-327` refuses a recording that equals its stimulus.
(a) At Mix 0 % the output is the input delayed by the 48 samples of latency, so a constant input (or any
input that repeats every 48 frames, a 1 kHz sine at 48 kHz) comes back as itself: the control session's
jobs `0007-g` and `0008-h` (Mix 0 %, constant 0.25) were both refused with that message within 5 s of the
creation of the instance. (b) In the demo run of section 5 the recording in which the limit fell was the
input for 1,850,368 of its 1,920,000 frames and is not equal to its stimulus as a whole; the driver would
return it as a normal capture. (b) needs `stopAfterMinutes` of 20 or more, or F5. A check on the host's
own times (`secondsSinceCreation + wallSeconds` of the step against 1200 s) would have neither fault.

**F3. "6 s is the shortest flush that left no trace" does not hold when the next case has Pre-delay**
(low). `revocean_session.py:59`, campaign README line 408. The flush sets the next case's controls; with
Pre-delay 2000 ms the pre-delay line still holds the last 2 s of the previous input and plays it into the
network during the flush. Measured behind 16 s of noise at 0.5 into Decay 59.5 s, Size 200 % (section 3):
the output stays at -19 dB for 2 s and reaches the floor after **7.70 s** of the 8 s flush (5.81 s without
Pre-delay). The default of 8 s holds with 0.30 s to spare; a flush of 6 s, as `test_revocean_session.py:196`
uses, would leave about -509 dBFS there. Harmless for any null of this campaign, wrong as a statement.

**F4. The read-back the records carry is the host's own cache** (information). `Session.cpp:414-418`
reads `getValue()` straight after `setValue()`; JUCE's VST3 parameter returns the value it was just given
(`juce_VST3PluginFormatImpl.h:2016-2027`). `readback == requested` (`revocean_session.py:315-317`) can
therefore not fail and proves nothing about the plug-in; the display text does come from the plug-in. The
Analyzer's read-back is the same. What proves the settings are the audio comparisons.

**F5. The tool's clock stops while the machine sleeps** (low, not exercised). `Session.cpp:96,123,247-250`
and `Main.cpp:147` measure the safety margin with `Time::getMillisecondCounterHiRes()`, which is
`mach_absolute_time` (`juce_SystemStats_mac.mm:342`) and does not advance in sleep. `Native.mm:18-24`
prevents idle sleep only. If the lid is closed during a session and the demo counts wall-clock time, the
margin and the countdown in the window come late; F2 (b) then applies.

**F6. Smaller observations** (information).
- A `wav` step may name a file outside the session folder (`Job.cpp:119`, `getSiblingFile`): job
  `0128` with `../../../not-there.wav` was looked up in `work/session_verification/`. Reading only.
- `framesProcessed` in `session-record.json` is the count at the last change of state, not a live one
  (`Session.cpp:538-558`); during the 11 minutes of F1 it said 732,560.
- The plug-in answers a parameter step late: Master 0 to -6 dB set at frame 723,259 first shows 53 frames
  into the step and settles after 1,673; Mix 0 to 50 % shows after 85. That is the plug-in (the host
  hands the change over with the first block of the step, section 2); it is why a case needs its pre-roll.
- The -126 to -135 dB difference behind a change of Size was seen once here: -132.8 dB, from 9,769 frames
  after the first arrival, largest difference 1.1e-8 (Size 37 to 137 %; not behind 73 to 37 or 137 to 88 %).
- The binary in `build-session` was linked at 15:03:44, after the engineer's last kept session
  (15:03:15). It has the size of this packet's build (10,903,008 bytes); one session with it passed
  (first case bit-identical, a later case identical from the first arrival; `owner_binary_check.json`).

## 2. The host against its README

| claim | result | evidence |
| --- | --- | --- |
| exit 1 without instance or window when it cannot open | holds | 17 bad starts (arguments, a missing or malformed `session.json`, a wrong bundle, hash, name or version): exit 1 each, nothing left running; `fail_paths.json` |
| nothing is processed before Start | holds | `Session::process` is reached only from the worker that `start()` creates (`Session.cpp:182-191, 265`), and JUCE calls the plug-in's `process` only from `processBlock` (`juce_VST3PluginFormatImpl.h:2487`). A job put in the queue before Start was still there 2.5 s later, with no result folder; its first step then has `firstFrame` 0, and its 252,560 frames equal the Analyzer's fresh instance |
| the job whose name sorts first runs first | holds | eight jobs written last to first before Start ran 0001 to 0008 |
| frames are counted exactly over jobs and partial blocks | holds | 482,025 frames of silence in pieces of 1, 511, 513, 1000 and 480000 over three jobs, then the stimulus as 12345 frames, 1 frame and the rest over two jobs: every `firstFrame` and the total (731,451) as counted here, and the joined recording **bit-identical** (192,000 frames) to one fresh Analyzer instance with 482,025 frames of warm-up in whole blocks. One frame of warm-up less or more: -55.4 dB |
| parameters act at the frame of the record | holds | two sessions with the same jobs except two late parameter steps: 193,234 recorded frames in front of the step bit-identical, first different frame 53 into the step that follows it (of a 512-frame block), see F6 |
| a malformed job is rejected, nothing of it runs, the session goes on | holds, except F1 | 39 malformed jobs (broken JSON, wrong types, unknown and negative IDs, values outside 0..1, thirteen bad stimuli, a valid step in front of a bad one) and one under a name already used: all rejected in 0.12 s, `framesProcessed` unchanged at 732,560, three later cases then ran normally |
| a queue that runs empty | holds | `idleSeconds` 5: exit 0 after 5.07 s, `idle`, 2,597,363 frames as counted here. `idleSeconds` 0: still waiting after 4 s, then stopped by the margin |
| stops: quit, window, idle, margin, signals; a running job is cut and marked failed | holds | table below |

| how it was stopped | exit | seconds to exit | record | the job that was running |
| --- | --- | --- | --- | --- |
| SIGTERM during a recorded step | 0 | 0.18 | stopped, `terminated` | failed; 3,540,480 frames in the record and in the WAV, count adds up |
| SIGKILL during a recorded step | -9 | 0.01 | stale `running` | no record; driver raises, `attach` refuses |
| application asked to quit during a step | 0 | 0.07 | stopped, `window_closed` | failed; 3,761,152 frames in record and WAV |
| asked to quit before Start | 0 | 0.04 | stopped, `window_closed`, 0 frames | |
| SIGINT before Start | 0 | 0.19 | stopped, `terminated`, 0 frames | |
| SIGHUP before Start | -1 | 0.02 | stale `waiting_for_start` | |
| margin (0.1 min) before Start | 0 | at 6.18 s | stopped, `demo_margin`, 0 frames | |
| margin (0.2 min) during a step | 0 | at 12.05 s | stopped, `demo_margin` | failed; 18,080,768 frames in record and WAV |

No process was left in any case. The quit request was `NSRunningApplication.terminate`, which reaches
`systemRequestedQuit` and from there the same `closeButtonPressed` (`Main.cpp:135-139, 202-208`) a click
on the close button calls; the click itself was not made. The two last-resort exits (message thread
silent for 10 s, plug-in not returning for 20 s) were read, not exercised.

A picture of the tool's own window before Start showed the title "Rev OCEAN session - automatic check, do
not touch", the instruction, the Start button, "Demo time left: 19:59 of 20:00 ... stops by itself in
18:59", the plug-in's editor with the mode shown as `< Tide >` and its Authorization box with the Demo
button. The box shows the account name; the picture was deleted.

## 3. Equivalence

**First case of an instance against `revocean.capture`: bit-identical**, 252,560 frames (impulses, two
windowed tones and a noise burst; Decay 1.3 s, Size 73 %, Pre-delay 35 ms, Width 120 %, Mix 55 %,
Brightness +20 %, HPF 80 Hz, LPF 9 kHz, Master -3 dB; 10 s warm-up), in two sessions of this packet's
build and one of the engineer's. The 482,025-frame case of section 2 is a second one.

**Later cases behind the driver's flush (8 s) against `network_model.render`**, at the warm-up the host
counted. None of the Decay, Size or pre-roll values is one the engineer used.

| session | Decay s | Size % | pre-roll frames | audio in front | model null dB | against a fresh instance |
| --- | --- | --- | --- | --- | --- | --- |
| E1 | 0.7 | 37 | 12000 | 23.5 s | -117.5 | identical from the first arrival (189,737 frames) |
| E1 | 5.5 | 137 | 81603 | 37.2 s | -107.5 (noise burst) | -132.8 dB (F6); the model scores the fresh capture -107.5 too |
| E1 | 15 | 88 | 43200 | 50.1 s | -110.6 | identical from the first arrival (189,355 frames) |
| E2 | 2.7 | 165 | 148800 | 1.42 min | -114.9 | beyond the Analyzer's 60 s |
| E2 | 9 | 55 | 624 | 4.62 min | -111.0 (noise burst) | |
| E2 | 30 | 190 | 240000 | 10.90 min | -109.5 | |
| E2 | 1.9 | 100 | 96000 | 10 s, 21.14 min, 26.37 min | -107.5, -109.8, -115.7 | the first: identical from the first arrival |

E2 is one instance that processed 76,133,849 frames (26.4 minutes of audio, as counted here) in 33 s. The
model on fresh instances of its probe scores -107.5 to -114.7 dB (five warm-ups, 10 to 60 s), so the late
probes are the model's own figure. That the count is exact late in a session: the model rendered at the
host's count scores -109.5 dB on the case at 10.9 minutes and -115.7 dB at 26.4 minutes; one frame off,
-54.8 and -57.7 dB; 160 frames off, -11 and -14 dB; a block off, -3 and -6 dB. Before the first arrival a
later case carries the floor the engineer describes (8.6e-37 to 1.4e-36); the 20 quiet minutes of E2 stay
at or below 1.2e-36.

**Is the flush long enough?** The worst previous case constructed: 16 s of noise at 0.5 into Decay 59.5 s
(normalised 0.998), Size 200 %, cut off at full level (output peak 0.91, rms 0.18), then at once the steps
of `revocean_session.case` for a stimulus at 0.001, with the two silences recorded.

| next case | floor reached after | last second of the flush | pre-roll | quiet case |
| --- | --- | --- | --- | --- |
| Decay 0.7 s, Size 200 % | 5.81 s (122 dB/s) | 9.8e-37 | 9.8e-37 | identical to the fresh instance from the first arrival (187,866 frames); model -114.3 dB |
| Decay 0.5 s, Size 200 %, Pre-delay 2000 ms | **7.70 s** | 3.2e-32 | 1.4e-36 | identical to the fresh instance from the first arrival (91,852 frames) |
| Decay 0.7 s, Size 30 % | 5.81 s (128 dB/s) | 9.1e-37 | 9.1e-37 | model -117.6 dB (88 s into the instance, no fresh reference) |

Yes, in Tide at Macro 0: with 2.2 s to spare, and with 0.3 s when the next case has the longest Pre-delay
(F3). Not measured here: a previous case at Macro above 0 or with Ducking, and any other mode.

## 4. Boundary

Read: all of `Source/` and the driver. Searched for state, program, preset, licence, synthetic input,
child processes, network and file access.

- **State, files, licensing, demo timer:** the host calls no state or program function and nothing that
  concerns the demo; "demo" occurs only in the countdown text and in the stop reason. It reads one file of
  the plug-in itself, the VST3 binary, for the SHA-256 the campaign pins (`Session.cpp:116`); the driver
  hashes the processor library `revocean.py` pins (`revocean_session.py:185`). Loading goes through JUCE as
  in any JUCE host: it reads the bundle's `moduleinfo.json` if there is one, loads the module, and at
  instance creation passes the component's state to the controller in memory
  (`juce_VST3PluginFormatImpl.h:936-962, 3163-3170`). Nothing is stored or replayed.
- **Synthetic input:** none. No event, accessibility or scripting interface is used. The driver's
  `screenshot` runs the system's `screencapture -l` on the tool's own window.
- **Written outside the project:** nothing by the host. Every write is inside the session folder
  (`session-record.json`, `results/<job>/record.json`, `job.json`, `step-NNN.wav`; `Session.cpp:31-34,
  108, 316, 323, 367, 451`). After about 140 runs of both builds none of the usual per-application places
  of `audio.amanitaocean.revoceansession` exists (saved state, preferences, caches, application support,
  containers, logs). What the plug-in writes for itself was not looked at.
- The host asks macOS not to nap the process and not to idle-sleep (`Native.mm:18-24`); nothing persists.

## 5. The demo limit

The engineer's record of his 24-minute run is its log (`demo_clock.jsonl`, 99 probes) and a summary of
three recordings after the limit. It is consistent: the instance's first frame at 120.3 s, 50 normal
probes up to 1195.46 s, 48 probes from 1200.38 s whose peak and rms are those of his stimulus to every
digit (regenerated here: 0.5 and 0.002299229944903761), a null against the model of +0.15 dB as a
pass-through must give. It is not complete: no audio and no job record of that session were kept, the
pictures were deleted, and no script produces `demo_clock_after_limit.json`. Because the audio could not
be read, the run was made once more, with this packet's probe and with the limit inside a recording
(`demo_limit.py`, one window for 20 min 32 s, `demo_limit.jsonl`, the recording kept as
`demo_limit_recording_across.npz`).

- Nothing was processed for the first 90 s. Eight probes behind the driver's flush from 95 s to 1185.3 s:
  -104.2 to -121.0 dB against the model.
- Noise was then processed and recorded without a pause. Nine recordings up to 1200.2 s are normal (not
  one frame equal to the input). In the tenth the output is the plug-in's for 69,632 frames and **the
  input, bit for bit and undelayed, from global frame 21,957,632 on: 1200.31 s after the creation of the
  instance**, at a block boundary, without a fade. Six more recordings are the input throughout.
- Not at 1290 s: the limit is wall-clock time from the creation of the instance, not from its first
  frame. 7.6 minutes of audio had been processed by then; in E2 26.4 minutes of audio in 33 s changed
  nothing. Processed audio does not count.
- Two probes through the driver at 1207 s and 1219 s were refused ("its demo time is over"); they equal
  the stimulus, the latency is still reported as 48, parameters still answer, the host is not blocked.

The engineer's facts hold; his bracket of 19:55.5 to 20:00.4 narrows to 20:00.3. The default stop at
19 minutes is 60 s in front of it.

## 6. What went wrong here, and what was not done

- One window of this packet stayed open 11 min 07 s (F1) and one 20 min 32 s (section 5, deliberate).
  19 windows in all, the others 2 to 33 s; 17 starts without a window.
- One control session was lost to F2 and run again. One batch refused to start because the process check
  matched its own shell; no session was open then.
- The picture of the window shows the account name; it was viewed once and deleted, as were all WAV files.
- Not done: anything with a person; rates other than 48 kHz and blocks other than 512; Macro above 0;
  hiding the window (App Nap) and sleep; the engineer's five reference tests (his eight pure ones pass);
  his numbers for Ducking, Size settling and the fit of the Size effect. Whether the campaign README
  changed only by its new section could not be checked (`Analyzer/` is untracked).

## 7. For the owner

Do:

1. Have the capture script ready first. Then `python revocean_session.py`; in the window press **Demo**,
   select the mode with the arrows next to its name and nothing else, press **Start**. The 19 minutes run
   from the moment the window opens; what is left after Start is about 50 times as much audio.
2. Make the first attended session a Tide one (Demo, Start, no mode change) and run the README's check: a
   first case must still equal `revocean.capture` bit for bit. That is the only test of Demo and of Start.
3. In Abyss trust nothing from Tide until it is seen there: run one case twice in the session, record the
   flush once (the steps of `flush` with `record=True` on its silences) and look whether it reaches the floor, and take a picture at
   the end (`session.screenshot`) to see that the mode is still the one chosen.
4. Keep what is captured (`keep_audio=True` or save the arrays): the instance cannot be made again.
5. Add up the frames of a job before submitting it, and end with `session.close()`.

Do not:

1. Touch the editor after Start. Every knob is a host parameter; a touch changes a capture without a trace.
2. Close the window, press Cmd-Q, press Ctrl-C in the session's terminal or close that terminal before the
   captures are done: each ends the session, and the mode with it.
3. Let the Mac sleep or close its lid during a session (F5), or raise `stopAfterMinutes` to 20 or more (F2).
4. Shorten the flush below 8 s when the next case has Pre-delay (F3), or set `preroll_seconds` to 0
   behind a change of anything but Decay.
5. Read "demo time is over" at Mix 0 % as the truth (F2), or pass on `window-at-start.png`: it shows the
   account name while the Authorization box is open.
