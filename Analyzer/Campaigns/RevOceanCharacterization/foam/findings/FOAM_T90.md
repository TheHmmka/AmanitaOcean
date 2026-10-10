# Foam: session T90 (another instance, 90 BPM, play head 0.25 s ahead) as a blind test

10 October 2026. Black box only: the recorded audio of `work/foam/session_T90` and `session_dry90` (script
`work/foam/probe_foam2.py`), the campaign's code, the structure model of session A (`foam_structure.py`,
`structure_final.json`). No session was opened, nothing outside this folder was written.

Marks: **M** measured (a null or a level decides it), **F** fitted (a number that minimised a residual), **G** guessed.
Null = `revocean.null_db`, whole recording, both channels, nothing fitted per recording unless said.
`PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python`; commands run inside this folder.

## 0. What was re-fitted

One number: the oscillator origin of this instance, **9 315 548** (44 x 211 717; 211.2369 s), scanned on f01 over
36 082 multiples of 44 from 186 s to 222 s: -110.1 dB at the origin, -48.4 / -48.1 dB one sample off, -15.3 dB 44
samples off, next best -46.3 dB (`t01_origin.py T90 186 222`, `t01_origin.py T90 check 9315548`). **M**

Everything else is session A's: the 28 delays, the 7 gains, the mixes, the crossfade law, the staircase of the gains.

## 1. Tempo and play head: the table

`t02_score.py T90` (log `t02_score_T90.log`), `t07_motion.py`.

| Recording | Processed time | Null | Base alone |
|---|---|---|---|
| p007, p008 (the first two Foam probes) | 220, 250 s | -117.5, -117.4 dB | -1.8 |
| f01 train, Macro 0 | 280 s | -103.2 | -103.2 |
| f02 train, Macro 100 %, Decay 0.5 s | 314 s | -104.0 | +2.9 |
| f03 time scan, 24 impulses L / R, every 4 s + 1/30 s | 348 s | -103.4 | +3.0 |
| f04 tone 64 s in 4 gapless steps (host block grid moved by 64 frames per step) and 6 s of tail | 458 s | -116.9 (steps -117.5, -117.6, -115.7, -117.2; tail -117.7; first 0.2 s of steps 1..3: -119.1, -116.9, -118.1) | -3.3 |
| k00, k16 neutral | 538, 954 s | -103.5, -97.0 | +3.0 |
| k12 / k13 Size 30 / 200 % | 850, 876 s | -107.0 / -100.0 | +3.0 |
| k14 Decay 20 s | 902 s | -100.0 | +3.0 |
| k15 Macro 50 % | 928 s | -100.9 | -2.3 |
| n01 white noise 10 s, Macro 100 % | 1056 s | -103.2 | +3.0 |
| n02 the same noise, Macro 0 | 1082 s | -103.1 | -103.1 |
| end probe | 1108 s | -117.8 | -1.7 |
| m01 Macro in motion, 90 plays and the tail (section 3) | 980 s | -115.6 | |
| k01..k07, k10 with their control modelled (section 2) | 564 to 798 s | -101.7 to -112.8 | |

**Nothing in Foam follows the tempo or the play head.** At 90 BPM, with the play head 12 000 frames ahead of the
frame count, a second instance nulls at the depth of the base itself (Macro 0: -103 dB) with the delays in samples
and no clock. A delay that followed the tempo would be 4/3 as long here. The host's block grid does not enter
either (f04). **M**

## 2. The outer controls in Foam (part K)

Every case: Macro 100 %, Decay 2 s; left impulse at 1 s, 5 ms noise bursts on both sides at 5 s, right impulse at
9 s. `t03_controls.py` (`t03_controls.json`, log), `t04_bright_trans.py`, `t05_transients_solve.py`.

| Case | Control | Nulls of the candidates | Routing found | As in Abyss? |
|---|---|---|---|---|
| k01 | Pre-delay 50 ms (2398 frames) | A -103.3; C -72.3; N +3.0; A one frame less / more -0.0 / -0.0 | A: whole host frames in front of the converter, so F is fed from the delayed input | yes |
| k02 | Pre-delay 200 ms (9599 frames) | A -103.1; C -75.4; N +3.1 | A | yes |
| k03 | HPF 1 kHz | A -102.2; internal rate -51.2; one pole -23.7; corner 1 Hz off -71.7; N -8.9 | A: first-order bilinear filter at the host rate in front of the converter and of F | yes |
| k04 | LPF 2 kHz | A -107.9; internal rate -39.4; one pole -12.3; corner 2 Hz off -63.4; N +5.5 | A | yes |
| k05 | Width 0 % | A -103.4; B -2.4; E -1.9; F -0.1 | A: Tide's law on the wet behind the network | yes |
| k06 | Width 150 % | A -103.2; B -8.9; F -7.0 | A | yes |
| k07 | Mix 50 % | A -112.8; B -6.0; C -10.7; D -0.5; dry one frame off +2.6 | A: dry 1, wet 1, dry delayed by 48 frames | yes |
| k10 | Ducking 50 % | A -101.7 (threshold -13.45 dBFS); B -38.4; N -60.5 | A: the level stage on the wet at output time, keyed by the raw input | yes; the law is complete here |
| k08, k09 | Brightness +100, -100 % | no null (no law); bands, 2.4 | in the network; F itself is not touched | consistent; front or behind F cannot be seen |
| k11 | Transients at full | no model; the input behind the stage solved: -98.8 dB (2.5) | **in front of F**, on the network input | **no**: P's return is in front of the stage, F is behind it |
| k12, k13 | Size 30, 200 % | model -107.0, -100.0 | F untouched | yes |
| k14 | Decay 20 s | model -100.0 | F untouched | yes |
| k15 | Macro 50 % | model -100.9 | the cos / sin law | (Foam's own law) |

Candidates: **A** as in the column "routing found"; **N** the control absent. Pre-delay **C**: F taps the undelayed
input and its return is delayed at the internal rate. Width **B** none, **E** mono at the input, **F** the law on F's
return in front of the network. Mix **B** linear, **C** equal power, **D** wet only. Ducking **B** the reduction on
the input in front of F.

What a time-invariant F cannot show: at Macro 100 % the dry gain is 0 and F commutes with every linear,
time-invariant stage, so "in front of the tap" and "behind the return" are the same sound. What is decided is where
the stage sits relative to the converter (and with it to F, which runs behind the converter) and to the network.

### 2.1 Pre-delay — M

`D = max(0, floor(P fs / 1000) - 1)` whole host frames (`reference_render.predelay_samples`): 2398 for the knob's
"50 ms" (49.99999 ms), 9599 for 200 ms. One frame off: 0 dB. The same delay as a fractional delay at the internal
rate behind F (C) stops at -72 to -75 dB: the converter is not shift-invariant below a frame, so the delay is at the
host rate in front of it. F works on the converter's output and therefore on the delayed input (G becomes M).

### 2.2 Input filter — M (law as in Abyss, exact at this depth)

    k = tan(pi fc / fs);  a1 = (k - 1) / (k + 1)
    low-pass:  y[n] = k / (1 + k) (x[n] + x[n-1]) - a1 y[n-1]        high-pass:  y[n] = 1 / (1 + k) (x[n] - x[n-1]) - a1 y[n-1]

at the host rate, corner as displayed: -102.2 dB (HPF 1 kHz) and -107.9 dB (LPF 2 kHz), the base model's own depth.
The same filter on `u` at the internal rate: -51.2 / -39.4 dB. A corner 0.1 % off: -71.7 / -63.4 dB.

### 2.3 Width, Mix, Ducking — M

Tide's shell unchanged on the wet. Mix 50 %: free gains dry 1.0000000, wet 1.0000000. Ducking 50 %: Tide's level
stage with its threshold at **-13.45 dBFS** (+-0.01; Abyss's fit was -13.4; 0 dBFS at Ducking 0 %): -101.7 dB whole,
-107.9 dB in the 0.3 s behind the impulse, -97.6 dB in the 0.3 s behind the bursts (reduction 0.021 and 0.174 dB).
A moved threshold is the whole law at this depth for these stimuli. The reduction on the input in front of F
(B: -38.4 dB) would turn the whole wash down; the recording has it on the output, where it has recovered long
before the wash arrives. One number fitted (**F**), routing **M**.

### 2.4 Brightness — by band levels; F untouched M, law unknown

Recording against the neutral Foam model of the same case, octave bands 0.5 / 1 / 2 / 4 / 8 / 16 kHz, mean of the
three events, in windows behind the event; beside it what the base's own response to Brightness predicts
(`abyss/findings/controls.md`, 2.4: two windows of network age, extended as a change of decay rate, mixed over the
ages that F's wash puts into each window; energies add).

| +100 % | measured | predicted from the base |
|---|---|---|
| 0.025..0.105 s | -0.13 / -0.00 / +0.42 / +1.35 / +2.51 / +3.99 | -0.42 / -0.18 / +0.37 / +1.29 / +2.50 / +3.91 |
| 0.4..1.0 s | -0.70 / -0.57 / +0.13 / +1.24 / +2.48 / +3.96 | -1.27 / -0.72 / +0.08 / +1.19 / +2.45 / +3.89 |
| 1.0..1.6 s | -1.97 / -1.39 / -0.30 / +1.08 / +2.40 / +3.93 | -2.35 / -1.38 / -0.33 / +1.04 / +2.39 / +3.87 |
| 2.4..3.5 s | -5.48 / -4.74 / -2.56 / +0.29 / +2.24 / +3.89 | -6.21 / -4.78 / -2.63 / +0.27 / +2.16 / +3.83 |

| -100 % | measured | predicted from the base |
|---|---|---|
| 0.025..0.105 s | +0.00 / -0.01 / -0.21 / -0.32 / -0.41 / -0.27 | -0.00 / -0.02 / -0.35 / -0.71 / -0.98 / -0.96 |
| 0.4..1.0 s | -0.05 / -0.17 / -0.60 / -1.16 / -1.34 / -0.88 | -0.07 / -0.20 / -0.86 / -1.62 / -2.13 / -1.79 |
| 1.0..1.6 s | -0.08 / -0.37 / -1.43 / -2.57 / -2.88 / -1.72 | -0.15 / -0.44 / -1.52 / -2.73 / -3.27 / -2.52 |
| 2.4..3.5 s | -0.22 / -1.70 / -4.54 / -6.51 / -4.91 / -2.35 | -0.59 / -2.03 / -4.86 / -6.49 / -5.74 / -3.50 |

+100 %: the fixed high boost (+3.9 dB at 16 kHz in every window) and the faster decay of the lows, within 0.1 dB
above 2 kHz and 0.3 to 0.7 dB below. -100 %: the same course, within 0.1 to 1.2 dB (the two-window description of
the base is coarse where the loss depends on age). The wash keeps its shape in time in every band; nothing shows a
change inside F (a damping in F's own loops would grow with F's 1 to 3 s, not with the network's 0.4 s). **M** at
this resolution. Whether the fixed part sits in front of or behind F cannot be seen.

### 2.5 Transients — in front of F, M; and what the stage is at full

The campaign has no law for Transients. Against the neutral Foam model the recording is uncorrelated (+3.1 dB on the
left impulse's wash), a gain that follows the wash explains nothing (on the wet output -0.0 dB, on the network
input behind F -0.1 dB; `t06_transients_gain.py`), and the levels read:

| event | 0..0.1 s | 0.1..0.4 | 0.4..1 | 1..1.6 | 1.6..2.4 | 2.4..3.5 | whole wash |
|---|---|---|---|---|---|---|---|
| left impulse | -3.73 | -1.38 | -0.79 | +0.02 | +0.34 | +0.59 | -0.19 dB |
| bursts | -11.28 | -8.55 | -8.06 | -7.36 | -6.73 | -6.77 | -7.52 dB |
| right impulse | -4.19 | -1.22 | -0.83 | +0.01 | +0.24 | +0.59 | -0.22 dB |

The bursts' wash is 7.3 dB under the impulses' at every moment of its 3.5 s: what takes 7 dB from a 5 ms burst and
nothing from a single sample has heard the event itself, not its wash.

**Solved** (`t05_transients_solve.py <event> 150 1.5 k11_transients_full 4 1e-7`): the recording is
`net(F(x))` for an input x that differs from `u` only in the 150 ms behind the event. x is solved through the exact
model on the first 1.5 s of the wash and scored on the rest:

| event | fit (0..1.5 s) | 1.5..3.9 s, not seen by the fit | energy of x against the plain event |
|---|---|---|---|
| left impulse | -104.0 dB | **-95.6 dB** | -0.29 dB |
| right impulse | (wash of the bursts underneath) | -81.0 dB | -0.29 dB |
| bursts, both sides (110 ms window) | -67.6 dB whole | -65.4 dB | -7.59 / -7.35 dB |
| control: the same solve on k00 (neutral) | -103.9 dB | -102.3 dB | 0.00 dB, x = u |

So the stage sits on the network input in front of F: **M**. And x shows what the stage does at full:

- **To an impulse**: a fixed dense kernel in place of the pulse. Nothing for 7 ms (-66 dB at the pulse's place), most
  of the energy 10 to 30 ms behind it (largest copy 0.34 at 850 internal samples), then -8 dB per 10 ms; energy
  -0.29 dB (the -0.28 dB Abyss read on the direct sound); magnitude between 0.007 and 1.99, rms 0.91 to 1.20 in
  seven bands: the same kind of object as F (parallel all-pass chains summed), shorter. The same on left and right
  (-40.8 dB between the two solved inputs, with the bursts' wash under the second).
- **To a 5 ms burst**: that same kernel applied to the burst, times **0.437** (-7.19 dB), on both sides:
  correlation +0.999, one gain per side leaves -26.0 / -28.4 dB. So the stage is a gain that hears the event,
  followed by a linear diffusing kernel.
- This is why the wet onset is "rebuilt" in every mode and why no gain or short filter of the neutral wet fits.

**Against Abyss:** there the stage was found behind P's return (each copy judged on its own shape) with "network
input or wet output" open. Foam closes that: network input. The two findings together give the order

    u -> (+ P, Abyss) -> Transients -> (F and its crossfade, Foam) -> equaliser -> network

Not done: the kernel's structure (the solved inputs are in `t05_k11_left.npz` / `_right.npz`; it should fall to the
method that decoded F), the law of the gain, positions of the knob between 0 and full.

## 3. Macro in motion (m01)

One continuous 1 kHz / 3 kHz tone: steps of 6.25 s at 0, 30, 70, 100, 55, 10, 100, 0, 50 %, a staircase 0 -> 100 ->
0 % in 81 plays of 50 ms (moves of 2.5 %), 6 s of silence. `t07_motion.py`, `t08_motion_scan.py`.

**The law of session A holds unchanged**: the two gains cos / sin move once per block of 44 internal samples by
`(target - gain) (1 - exp(-44/441))`, blocks start at m = 43 (mod 44). **M**, now for downward steps, jumps of 90 and
100 %, and changes that arrive before the last one has settled (the state carries on).

| change | first 0.25 s behind it | whole play |
|---|---|---|
| 0 -> 30 % | -117.3 dB | -116.3 |
| 30 -> 70 % | -117.7 | -117.3 |
| 70 -> 100 % | -115.7 | -116.6 |
| 100 -> 55 % | -117.6 | -115.5 |
| 55 -> 10 % | -117.0 | -115.9 |
| 10 -> 100 % | -116.2 | -116.6 |
| 100 -> 0 % | -115.7 | -112.1 |
| 0 -> 50 % | -115.6 | -115.8 |
| staircase, 81 plays of 50 ms | -110 to -120 per play | -115.7 as a whole |
| 6 s of silence behind it | -116.3 (first 0.25 s) | -115.6 |
| whole run, 66.3 s | | **-115.6** |

**One thing had to be corrected: which block is the first to use a new value.** Session A's rule ("the first block
at or behind floor(b) - 112", b the internal time of the host frame h of the change) leaves four of the eight
steps at -43 to -50 dB. Scanned to the sample, the first block lies 46 to 82 samples in front of floor(b) here
against 71 to 111 in session A; one sample off: -61 to -79 dB.

    first block = the first block of 44 that starts later than b - L
    session A   (h = 0 mod 48):   L in (111.56, 115.56]        ten changes
    session T90 (h = 16 mod 48):  L in (82.76, 90.76]          eight steps scanned to the sample; the 81 moves of the staircase null with L = 84.5

Reading (**F**, two alignments only): the reference converts in chunks of 48 host frames on a grid of its own, and
a block of 44 takes the value of the host call in which its chunk is completed:
`L = L0 + (147/160) ((h - c) mod 48)`. The two sessions allow c = 1..16 with L0 moving along (c = 16: L0 in
(82.76, 86.16]); `foam_model.first_block` uses c = 16, L0 = 84.5 and reproduces all 99 changes of both sessions.
This is the reference's handling of host blocks, not a property of the effect.

## 4. The change of mode

`t09_switch.py`. This time the switch is on record: the origin lies **1.235 s into the flush behind probe 6**
(host frame 10 139 295 at the input), in silence, 8.2 s behind the probe's last impulse.

- **The network is emptied.** Tide's tail is on record down to 7e-21 and falling 124 dB/s; it is cut: the last
  non-zero output sample is 172 frames behind the origin, then exact zeros for 6.76 s of flush and 2 s of pre-roll.
  The same flush without a change of mode (session dry90) rings on to 1e-34. **M**
- **The oscillators restart there**: the origin scanned on f01, 69 s later, is the frame of the cut. **M**
- **F starts empty.** If its lines had run in Tide and been kept, they would hold probe 6's impulse at 9e-13 at the
  switch and the flush would show 1.5e-13 falling to 1e-16; it shows 0.0 exactly (a float32 recording holds 1e-45).
  Whether the lines do not run in Tide or are cleared at the switch cannot be told apart, and makes no difference:
  nothing played before the switch comes out behind it. **M**
- Not seen: the two gains at the switch (silence), a switch under signal, a switch from Abyss.

## 5. Files

| File | What |
|---|---|
| `gcommon.py` | sessions A, T90, dry90; the model in linear parts (converter, F, network) |
| `t01_origin.py`, `origin_T90.json` | the origin |
| `t02_score.py` (`t02_score_T90.log`) | section 1 |
| `t03_controls.py` (`.json`, `.log`) | pre-delay, filters, Width, Mix, Ducking |
| `t04_bright_trans.py` (`.json`, `.log`) | Brightness by bands, Transients by levels |
| `t05_transients_solve.py`, `t06_transients_gain.py`, `t05_k11_*.npz` | Transients: the input behind the stage |
| `t07_motion.py`, `t08_motion_scan.py` (`t07_motion.log`) | Macro in motion |
| `t09_switch.py` (`.log`) | the change of mode |
