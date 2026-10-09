# Abyss, analysis3: independent verification and the holdout

9 October 2026. Independent scorer. Black box only: recorded audio, the campaign's code, the two packets' reports and
modules. No session was opened, no capture made, no binary or file of the reference inspected. Nothing was fitted and
nothing in either packet, the campaign code or the plug-in source was edited. My scripts, logs and numbers are in
`analysis3/verify/`. The model under test is packet 1's `tempo/model/abyss_model.py` (imported); stimulus, warm-up and
settings come from each session's `info.json`, the null is `revocean.null_db` over the whole recording.

`PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python`; commands run inside `analysis3/verify/`
with `PYTHONDONTWRITEBYTECODE=1`.

## 1. Verdicts

| # | Claim | Verdict | My numbers |
|---|---|---|---|
| 1 | The holdout h1..h3 was never loaded by either packet | **confirmed** by search | no `h1_`, `h2_`, `h3_` in any script or in `scores.json`; both loaders refuse the names; the one `glob` reads `p[0-9][0-9][0-9].npy` only. Not checkable: what was done outside the saved scripts |
| 2 | Packet 1's model predicts unseen programmes | **confirmed** | h1 -65.60, h2 -71.02, h3 -73.66 dB, nothing fitted (section 2) |
| 3 | Packet 1's score table | **confirmed** | 46 recordings re-scored with my own script, largest difference from `scores.json` 0.00 dB (section 3) |
| 4 | "The late chunks follow from reading the host position in quarter notes once per host block in single precision, nothing fitted per recording" | **confirmed**, with the limits of section 4 | D batch c02..c14: -56.29 to -77.79 dB with the origin, 120 BPM, block 512 and the job's block starts, no table. Stamp in double precision: -0.5 to -15.8 dB. Block grid moved by half a block: -0.7 to -12.8 dB. Out of sample for packet 1: S4 step 8, layer -66.6 dB (origin alone -4.5 dB) |
| 5 | With `clock` unset the module is analysis2's module bit for bit | **confirmed** | three renders (D c10 with the late rule, D c05 strict, F c12 Size 60 %) identical arrays, max difference 0 |
| 6 | Session D's context holds nothing per recording or per session beyond origin, tempo, block size and block starts | **confirmed** by code and by my own context | `{'oscillator_origin': 31574180, 'clock': 'host', 'bpm': 120.0, 'playhead_offset_seconds': 0.0, 'host_block': 512, 'steps': [S - 10 s, S - 2 s, S]}`. One remark: D's session record carries no tempo or offset field; 120 BPM is the default in the session host's present source and is taken on trust for that day |
| 7 | T90: tempo and play-head law | **confirmed** | t02 -60.25, t03 eight alignments -66.06 to -66.65 and tail -62.27, t04 -59.47 dB. With one block grid running through t03 instead of the true eight: -10.5 to -17.7 dB |
| 8 | Stopped transport: the layer counts from the oscillator origin at the host's tempo | **confirmed** up to 512 quarter notes behind the switch | S90 t02 -61.88 dB (the drift curve plays no part there); zero moved by +-1 sample -2.2 dB, +-44 samples -1.8 dB; chunks of 120 BPM -1.7 dB; "as if playing" -0.2 / -0.7 dB |
| 9 | Stopped transport behind 512 quarter notes: an unexplained fitted drift; the scan nulls at -31.1 dB | **confirmed as stated; the law there is not established** | t04 -31.10 dB with the curve, -11.21 dB without; t05 -65.58 with, -19.20 without. With the curve 99.9 % of the scan's error sits in twelve of its 486 seconds, four of them at 370 to 374 s (516 to 522 quarter notes) (section 5) |
| 10 | Packet 2 (a): P is tapped behind the pre-delay | **confirmed** | k16 (15 982 frames): behind -59.24 dB, in front +0.50 dB, no pre-delay +3.36 dB, one frame more or less +0.05 dB. k01: -55.71 / +0.17 / +2.81 |
| 11 | Packet 2 (b): the input filter sits in front of the tap | **confirmed** | k02: in front -61.21 dB, on the base's input only -11.04, behind P's return -8.05, none -8.21. The filter law on the base alone, 0.8 s behind the first event: -123.48 dB |
| 12 | Packet 2 (c): Mix 50 % is the Tide law, dry 1 / wet 1 | **confirmed** | k06: -69.94 dB; 0.5 / 0.5 -6.02; 0.707 / 0.707 -10.67; wet only -0.24; dry one frame off +2.50. Free gains 1.000000 / 1.000072. The wet part alone (recording minus dry) nulls at -57.28 dB: the -69.9 is flattered by the exact dry |
| 13 | Packet 2: Macro in motion is a one-pole of about 10 ms on each voice gain, not on the Macro value | **confirmed** | first 0.25 s behind the step, layer null, gain law against Macro law: step 5 (0.25 to 0.35) -52.74 / -35.14; step 9 (0.55 to 0.65) -61.24 / -40.00; step 13 (0.62 to 0.30) -54.04 / -30.31 dB (section 6) |
| 14 | Packet 2's correction does not matter at rest | **confirmed** | the three holdout renders with the gains smoothed as arrays are identical arrays to the module's (max difference 0); the holdout numbers do not change |

## 2. The holdout

`$PY v1_holdout.py` (`v1_holdout.log`, `v1_holdout.json`), `$PY v5_extras.py`, `$PY v4_controls.py rest`.

Three programmes of 32 s, session F, processed time 7296 to 7416 s. Context: oscillator origin 1 323 432, 120 BPM,
play head on the frame count, host blocks of 512, block runs started at S - 12 s, S - 4 s and S (the job: 8 s flush,
4 s pre-roll, recording; read from `info.json` and `probe4.py`). Every other control at the baseline (checked). The
samples' SHA-256 equal those in `info.json`.

| Recording | Decay | Macro | Base alone (Macro 0) | analysis2's law, origin alone | **Packet 1, host clock** | left | right | layer null |
|---|---|---|---|---|---|---|---|---|
| h1_holdout_macro100_decay2_prog5151 | 2 s | 100 % | -3.17 | -17.67 | **-65.60** | -65.81 | -65.33 | -62.43 |
| h2_holdout_macro60_decay4_prog6262 | 4 s | 60 % | -4.38 | -16.37 | **-71.02** | -71.45 | -70.55 | -66.64 |
| h3_holdout_macro30_decay1_prog7373 | 1 s | 30 % | -5.25 | -16.91 | **-73.66** | -73.14 | -74.29 | -68.40 |

- One least-squares gain on the whole model: 1.000042, 1.000004, 0.999979; it would buy 0.00 to 0.04 dB.
- The holdout does exercise the clock arithmetic: in each recording 8 of 48 same-pitch chunks and 8 of 24
  octave-down chunks sit 5.93 samples from where analysis2's law puts them (the octave-up reader: none of 32).
  Without the host clock the recordings null at -16 to -18 dB.
- Not sensitive to the one fitted constant of the clock law here: `host_mirror_a` from 41.9 to 42.6 gives the same
  three numbers to 0.01 dB. Packet 1's own step layout (S - 10 s, S - 2 s, S) instead of the job's true one, and
  Macro rounded to four digits, give the same numbers too.
- Packet 2's correction (one pole on each voice gain) changes nothing: at a constant Macro the arrays are identical.
- The error is not even in time: in h1 two seconds (3 and 5 s into the recording) hold most of it, 10 to 12 dB above
  the recording's average error.

## 3. Re-score of packet 1's table

`$PY v2_rescore.py D F T90 S90` (`v2_rescore_*.log`, `v2_rescore.json`), `$PY v3_stopped.py short`, `$PY v2b_bitcheck.py`.
Difference from `tempo/scores.json`: 0.00 dB on every row.

| Group | Recordings | Mine (host clock) | Packet 1 |
|---|---|---|---|
| D batch, Macro above 0 | c02..c14 (13) | -62.28, -57.76, -65.32, -60.32, -68.04, -67.21, -65.74, -77.79, -68.22, -66.11, -60.15, -64.23, -56.29 | the same; "old" and "rule" columns the same too |
| F part A | p003, p020, p050, p085 | -67.33, -67.38, -67.30, -67.65 (origin alone: -14.79, -8.37, -67.30, -8.85) | the same |
| F parts B, Z2, S1, S6, S7 | c05, c10, z2 Macro 100 %, s1 burst map, s6 right down, s7 tone pair | -60.33, -69.72, -58.21, -60.73, -64.90, -66.55 | the same |
| T90 | t02; t03 00..07 and tail; t04 | -60.25; -66.65, -66.28, -66.18, -66.17, -66.12, -66.27, -66.35, -66.06, -62.27; -59.47 | the same |
| S90 | t02; t03 00..07 and tail; t04; t05 | -61.88; -66.52, -66.15, -66.11, -66.08, -66.12, -65.97, -66.18, -66.07, -62.68; -31.10; -65.58 | the same |

## 4. The late chunks as one arithmetic

What I checked beyond the numbers of section 3:

- **No table in the path.** With `clock == "host"` the module passes no late-chunk list to the layer and
  `hostclock.py` holds no list, no session name and no time range: two formulas and three global constants
  (`a = 42.2`, the catch window of 43 samples, the rounding rule of the offset per voice).
- **The context is built from job records.** My script builds it from `info.json` alone and gets packet 1's numbers.
- **Single precision is needed.** The same clock with the stamp in double precision (patched at run time, nothing
  edited): D c02..c07 fall to -5.21, -1.47, -0.52, -4.41, -5.63, -15.80 dB.
- **The block layout is needed.** D c02..c07 with the grid moved by 256 frames: -7.54, -8.82, -0.65, -6.61, -3.88,
  -12.75 dB. T90 t03 with one grid running through instead of a new one per step: the first step unchanged at
  -66.65 dB, the other eight -10.49 to -17.67 dB.
- **Out of sample.** Packet 1 never loaded part S4. Its step 8 (grid half a block off the 4 s frame) could not be
  nulled by packet 2. With packet 1's clock and the true layout the steady part (1 to 20.25 s) nulls at -71.15 dB,
  layer -66.57 dB; with the origin alone -9.05 / -4.47 dB; with a wrong layout -6.25 / -1.67 dB
  (`$PY v4_controls.py S4steady`). Step 7, which has no displaced chunk, is -72.26 dB either way.
- **The two packets agree without having exchanged the number.** Packet 2 measured, and could not explain, chunks of
  the anchor z2 displaced by 8.93 to 9.0 samples where the boundary is 8/3 s modulo 4 s (same pitch and octave down).
  Packet 1's schedule for that recording, computed here, displaces exactly those chunks by 8.934 samples and no
  octave-up chunk. It is the case in which a stamp one step low pushes the offset over the catch window (43.93 minus
  the whole part of the offset; 5.93 in the holdout).

Limits of the verdict:

- **Session D is not a blind test of this arithmetic.** Its late chunks were known as a table before packet 1
  started, and the arithmetic was chosen among six variants so that it reproduces them and 537 measured mirror
  points. What D shows is that one rule with no per-recording input replaces the table. The blind tests are the
  holdout and S4 step 8, both in session F between 5900 and 7420 s.
- Everything is one host rate (48 kHz), one block size (512) and two tempi. Block alignments tested: whole 4 s
  frames, eighths of a block (T90 t03), half a block (S4 step 8).

## 5. Stopped transport

`$PY v3_stopped.py short`, `$PY v3_stopped.py scan` (`v3_stopped_*.log`).

| Recording | Processed time | Packet 1's model | Drift curve off | Other |
|---|---|---|---|---|
| t02 train | 134 to 158 s | -61.88 | -61.88 | base alone -4.80; zero at origin -1 / +1 sample: -2.21 / -2.20; -44 / +44: -1.80 / -1.83; chunks of 120 BPM: -1.68; as if playing: -0.21 and -0.70 |
| t03, eight steps and tail | 168 to 302 s | -65.97 to -66.52, tail -62.68 | (not involved) | the host block grid plays no part |
| t04 scan | 312 to 798 s | **-31.10** (left -28.87, right -36.13) | **-11.21** | see below |
| t05 end probe | 808 to 828 s | -65.58 | -19.20 | |

The scan in windows of 30 s, with the curve: -59.6, **-19.2, -38.7**, then -56.0 to -62.8 dB in all fourteen windows
from 402 s on. The four seconds from 370 to 374 s (516 to 522 quarter notes of the free clock) are the only ones above
-40 dB against the recording's mean power. Without the curve the scan is fine up to 372 s and fails in every window
after it (-4.7 to -30.3 dB).

What a product can rely on:

- With the transport stopped the chunk lengths still follow the host's tempo (the same note values).
- The clock then runs free from the mode switch: zero at the oscillator origin, to the sample. It ignores the frozen
  host position and the host's block grid.
- Up to 512 quarter notes behind the switch (5.7 minutes at 90 BPM) this needs nothing else and nulls like a
  running transport.

What is not understood:

- Behind 512 quarter notes the boundaries drift by a few hundredths of a sample. That is inaudible, but the
  octave-up reader rounds its offset upward, and the sign of a thousandth of a sample then moves a whole chunk by
  one sample. The curve that repairs it was fitted on this session's own scan; the only recording that tests it
  without having been used for the fit is the end probe (-65.58 dB). It fails in the first quarter notes behind 512.
- One stopped session exists. Nothing about a transport that starts, stops, jumps or loops, and nothing about a
  second mode switch, was recorded.

## 6. Packet 2: spot checks

`$PY v4_controls.py K`, `$PY v4_controls.py S4` (`v4_K.log`, `v4_S4.log`). My own render (`v_render.py`) builds the layer
from the parts of packet 1's module, with the host clock and the job's true block layout, which packet 2 did not use.
At a constant Macro it equals the module's render exactly (checked on k00 and k17: -59.38 and -57.79 dB, packet 2's
-59.4 and -57.8).

Macro in motion, layer null in the first 0.25 s behind the step (the steady part 1 to 4 s behind it is -65.6, -66.3
and -68.6 dB under every law):

| Law | step 5, 0.25 to 0.35 (unison reaches its knee) | step 9, 0.55 to 0.65 (octave down starts) | step 13, 0.62 to 0.30 (down) |
|---|---|---|---|
| one pole 10 ms on Macro, then the ramps (the scored module) | -35.14 | -40.00 | -30.31 |
| **ramps, then one pole 10 ms on each gain** | **-52.74** | **-61.24** | **-54.04** |
| the same with 5 / 8 / 12 / 20 ms | -35.25 / -42.89 / -47.04 / -33.73 | -44.19 / -52.09 / -54.69 / -42.18 | -33.40 / -41.54 / -44.14 / -31.33 |
| the same, 10 ms, acting 28 samples early instead of 44 | -55.52 | -62.61 | -55.37 |
| no smoothing | -27.41 | -36.35 | -25.67 |

Packet 2's numbers for the first two rows (-35.1 / -52.7, -40.0 / -61.2, -30.3 / -54.0) are reproduced. 10 ms is
pinned to about +-1 ms; the lead is between 28 and 44 samples and 28 is the better of the two on all three steps.

## 7. What the model can and cannot claim

**Can.** A replica that nulls at about -60 dB is now supported by an out-of-sample test: three unseen programmes at
three Macro positions and three Decays null at -65.6, -71.0 and -73.7 dB with nothing fitted, and a fourth stretch
that neither the modeler nor the clock's author had nulled (S4 step 8) at -71.2 dB. The level depends on the material:
programmes and tones reach -65 to -74 dB, impulse trains and noise stay at -56 to -62 dB (D c14 -56.29, F z2 -58.21,
k17 -57.79), the floor analysis2 already named.

**Cannot.** "Any recording" is not shown. The blind evidence is one instance (session F), 120 BPM, 48 kHz, blocks of
512, 5900 to 7420 s of processed time, a running transport. 90 BPM and the shifted play head rest on one session
whose mirror points were used to find the law. A stopped transport is supported for the first 512 quarter notes
behind the mode switch only. Other host rates, other block sizes, a tempo that changes, transport events, and
positions beyond 32 768 quarter notes were never recorded. A moving Macro is at -52 to -61 dB only with packet 2's
correction, which is not in packet 1's module.

**Per session.** One integer, the oscillator origin; it is also the zero of the stopped clock. Besides it the model
needs what the host said, which is known and not fitted: tempo, play-head offset, playing or stopped, block size, and
the frame at which each run of host blocks began. The one thing still fitted on a single session is the drift curve
of the stopped clock.

## 8. Problems

1. **Session D is not blind for the clock arithmetic** (section 4). The report's "it yields the late chunks by
   itself" is true as arithmetic; as a prediction it rests on session F's holdout and S4 step 8.
2. **The stopped clock behind 512 quarter notes is a fitted curve from one session**, wrong for about four seconds
   behind 512, and extended beyond 1150 quarter notes by a guess (`hostclock.free_run_drift`).
3. **Packet 1's scorer takes Decay, Size and the block starts from recording names and a fixed layout**
   (`settings_of`, `[S - 10 s, S - 2 s, S]`), not from `info.json`. The jobs behind part B had 8 s + 4 s. It changes
   no number I checked, because every grid starts anew at S, but it is not what the report's key description says.
4. **Session D's record does not state tempo or play-head offset.** 120 BPM (the host's default) and offset 0 are assumed.
5. **The two packets are not merged.** The module scores a moving Macro 17 to 24 dB worse than packet 2's law; the
   correction lives only in packet 2's report (and in `verify/v_render.py`).
6. **The -69.9 dB of Mix 50 % is mostly the dry signal.** The wet part nulls at -57.3 dB, the model's usual depth.
7. Not re-checked here: packet 1's 537 mirror points and its six stamp variants, the origins of T90 and S90 (taken
   as given; the Macro 0 trains were not re-scored), packet 2's Width, Ducking, Brightness, Transients, Size and
   Decay sections and its level table.

## 9. Files

| File | Content |
|---|---|
| `verify/v_common.py` | my loader, settings from `info.json`, contexts, piecewise render, whole and per-channel nulls |
| `verify/v1_holdout.py`, `.log`, `.json` | the holdout |
| `verify/v2_rescore.py`, `v2_rescore_{D,F,T90,S90}.log`, `v2_rescore.json` | re-score and the control variants |
| `verify/v2b_bitcheck.py`, `.log` | bit identity with analysis2's module |
| `verify/v3_stopped.py`, `v3_stopped_{short,scan}.log`, `v3_stopped.json` | stopped transport |
| `verify/v_render.py`, `v4_controls.py`, `v4_{K,S4,S4steady,rest}.log`, `v4_controls.json` | packet 2's spot checks, S4 step 8, the correction at rest |
| `verify/v5_extras.py`, `.log`, `.json` | holdout integrity, displaced chunks, sensitivity to `a` |
| `verify/v6_z2.py`, `.log` | packet 1's schedule on the anchor z2 against packet 2's measured displacement |
