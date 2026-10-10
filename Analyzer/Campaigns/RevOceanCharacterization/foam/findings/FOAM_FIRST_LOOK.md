# Foam: first structural look (session A)

> **Later the same day:** the kernel's topology is solved and Macro in motion is exact. `FOAM_STATE.md` is the current
> state (28 all-passes in 7 stages of 4, Hadamard mixes, all constants; nulls -100 to -130 dB). It supersedes the
> structural hypothesis of section 3, the motion row of section 4, the table of section 5 and open question 1 below.
> What stands unchanged: where Foam sits, the crossfade law, the absence of clocks, the proposed session.

10 October 2026. Black box only: the recorded audio of `work/foam/session_A` (Audio Unit, 48 kHz, blocks of 512,
120 BPM, play head = frames processed), the campaign's code and notes. No session was opened, nothing outside this
folder was written. Marks: **M** measured, **F** fitted, **G** guessed.

`PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python`; every command runs inside this folder.

## 1. Answers in short

1. **Where and what.** Foam is a fixed linear filter in front of the network, per channel, crossfaded with the dry
   input. In the words of `SPEC.md`: the network input `u[m]` (output of the input converter, 44.1 kHz) is replaced by

       u'[m] = cos(pi/2 Macro) u[m] + sin(pi/2 Macro) (k * u)[m]

   and nothing else changes. `k` is one kernel on the internal sample grid, the same for left and right, each
   channel filtered on its own. The base network is untouched (Macro 0 in Foam is the base at -104 to -121 dB). **M**
2. **The transform.** `k` is static, deterministic, the same for every impulse, independent of Decay, Size, level and
   processed time. It is a sparse-to-dense train of copies at whole internal samples with exactly repeating gains:
   a network of Schroeder all-passes with ±1 mixing, not noise, not grains, nothing moving. Unit energy, 3.8 s
   measured, energy peak at 0.9 to 1.0 s, tail -29 dB/s. The first 138.7 ms are explained to -48 dB by ten
   all-passes whose delays lie on one geometric series (30 ms x 1.1007^k); the rest of the topology is open. **M / F**
3. **Macro law.** Equal-power sine/cosine crossfade, exact at all eleven positions (-89 to -106 dB in steady state).
   The wet gain acts behind the kernel; the kernel runs at every Macro, also at 0. In motion: about 10 ms. **M / F**
4. **Clocks.** None found. One kernel and one oscillator origin null every recording from 610 s to 5050 s of
   processed time at the same depth. The only clock is the base's: its 32 oscillators restarted at the mode switch
   (origin 23 964 028, 543.402 s), as in Abyss. What made Foam impulses look different from one another is the
   base's own line modulation, which does the same at Macro 0. **M**
5. **Next session.** Tempo and stopped transport (expected: no effect), another host rate, the outer controls,
   Macro in motion. No denser maps, no noise maps: the kernel is static and already measured. Section 7.

A model that needs nothing fitted per recording is `foam_model.py` (measured kernel): **-77.8 to -88.9 dB** on all
fifteen Macro > 0 recordings of the batch and extras, -82.2 to -82.3 dB on seventeen probes (section 5).

## 2. Method and what each step showed

| Step | Script | Result | Mark |
|---|---|---|---|
| Oscillator origin on c01 (Macro 0), every multiple of 44 from 505 to 555 s | `a00_origin.py 505 555`, `a00_origin.py check 23964028` | origin 23 964 028 = 543.401995 s = host frame 26 083 295.8; first impulse -96.8 dB, one sample off -48.6 / -48.7 dB, 44 off -15.8 dB, next best of 50 114 origins -46.5 dB; whole c01 -104.1 dB | M |
| Macro 0 in Foam against the base | `a01_macro0.py` | c01 -104.1 dB, c16 (programme, 3534 s) -115.4 dB, x05 step 0 (tone) -121.0 dB; best gain 1.0000000 | M |
| Envelope of isolated impulses (x02, 149 impulses, Decay 0.5 s) | `a03_env.py`, `a05_avgenv.py` | same total energy as Macro 0 (-22.27 against -22.13 dB L); rise to a peak near 1.0 s, then -29 dB/s; identical in five bands from 50 Hz to 20 kHz; L - R constant 7.7 dB | M |
| Impulses against each other | `a04_repeat.py` | null +3.0 dB, peak cross-correlation 0.05 to 0.14: uncorrelated. The same holds at Macro 0 (`a02_look.py`: +2.7 to +3.2 dB): it is the base's modulation, not Foam | M |
| **Front test**, band < 1 kHz: one free input signal on one network input, fitted to both outputs through the exact base model | `a06_solve.py x02_impulse_map_left_macro100_decay0p5 1.0 0 0.5 100 1.3` | residual **-81.8 dB** over 1.3 s (floor of the method on a synthetic front case: -81.4 dB). The same fit on synthetic "behind" cases (a kernel convolved with the base's output): -7.0 dB (one common kernel), -6.2 dB (a kernel per output) | M |
| Same, every internal sample unknown, raw host samples as data, first 0.2 s | `a09_wide.py <case> <t> <ch> <decay> 100 0.2 1 <tag>` | x02 first impulse -107.8 dB, x03 first impulse (right input) -115.2 dB, c03 (Decay 2 s) -113.8 dB, Macro 25 / 50 / 75: -114.5 / -105.8 / -118.2 dB; Macro 0 pulse (calibration) -98.7 dB | M |
| Arrivals in the solved input (matched filter with the Macro 0 pulse; CLEAN) | `a10_taps.py <tag> <threshold> <ms>`, `a12_kernel.py <tag>` | discrete copies at whole internal samples, floor 5e-9; the same list and gains for x02 impulse 0, impulse 100 (402 s later) and x03 (right) | M |
| Whole kernel: causal blocks of 0.2 s (0.13 s kept), 3.77 s | `a11_extend.py x03_impulse_map_right_macro100_decay0p5 1.0 1 0.5 100 3.7 long_x03_0` (8 minutes), `a13_longkernel.py long_x03_0` | block residuals -106 to -113 dB up to 1.7 s, -58.6 dB in the last block (the kernel is 85 dB down there) | M |
| Model with that kernel against everything | `a15_validate.py`, `a17_time.py`, `a16_macro.py` (logs `validate.log`, `time.log`, `macro.log`) | section 5 | M |

Why the front test decides: the base is time-varying (line lengths move by up to 0.88 ms at 0.6 Hz), and the Foam
response is 1 to 3 s long. A filter in front makes the network see every copy at the oscillator phase of its own
arrival; a filter behind it, or a change inside it, does not. A single input signal fits both output channels at
the method's floor only in the first case. A left impulse needs a signal on the left network input only, a right
impulse on the right only: the channels are filtered separately and the base's own cross-feed does the rest.

Not decided: whether the kernel sits in front of or behind the input equaliser (a fixed filter commutes with it),
and where the pre-delay is (never moved).

## 3. The kernel

Measured file: `kernel_long_x03_0.npy` (44.1 kHz, index = delay in internal samples, 166 192 samples; band-limited
to what the input converter passes, 19.8 kHz, so its samples are not the bare tap gains; the bare gains of the
first 200 ms are in `kernel_w_x03_0.npy`).

| Property | Value | Mark |
|---|---|---|
| Direct copy (delay 0) | 0.003806 of the dry pulse, positive | M |
| Energy | sum k^2 = 1.02 (0.09 dB) full band; the solved input has 1.00065 of the dry pulse's energy | M |
| Magnitude response | rms 1.000, but not all-pass: 0.006 to 1.98 between 30 Hz and 19 kHz (std 5.1 dB), no tilt (rms 0.94 to 1.01 in five bands) | M |
| Energy against time | 1.6 % in the first 0.5 s, 49 % by 1.0 s, 96 % by 1.5 s; centroid 1.02 s, median 1.006 s, strongest 100 ms at 0.9 to 1.0 s | M |
| Tail | -28.95 dB/s from 2.0 s on (-60 dB in 2.07 s) | M |
| Left / right | one kernel: taken from one right impulse, it nulls left, right and both-channel material at -78 dB | M |
| Decay, Size | no dependence: Decay 0.5 / 2 / 8 s, Size 60 / 100 / 150 % at -77.8 to -78.0 dB | M |
| Level | linear from noise at 0.05 rms to impulses of 0.5 and a 0.25 sine at -78 dB or better; above 0.5 not recorded | M |
| Silence in | exact silence out (x01 peak 8e-37) | M |

**What is decoded** (bare gains relative to the direct copy, `kernel_w_x03_0.npy`; each value repeats to four digits):

| Delays (internal samples) | Gain / direct | Reading |
|---|---|---|
| 1323, 1457, 1604, 1765 | +0.5770 each | four parallel all-passes, g = 0.3728 |
| twice those | -0.2152 each | their second pass (-g) |
| 2851, 3137, 3457, 3803 | +0.2941 each | four all-passes, g = 0.572, each in series with one of the first four |
| 4174, 4594, 5061, 5568 (a_i + b_i, same i) | +0.6787 each | product of the two; a_i + a_j and a_i + b_j (i != j) are exactly 0 |
| 2591 (and 5182: -0.7829) | +1.6800 | one all-pass in series with the sum, g = 0.4660 |
| 5581 | +1.6357 | a second one, g = 0.4740 |
| 2591 + a_i | +0.9693 | product, as predicted |

    AP(D, g):  y[n] = g x[n] + x[n - D] - g y[n - D]
    k (first 6116 samples) = c * AP(2591, 0.4660) AP(5581, 0.4740) * 0.5 * sum over i of AP(a_i, 0.3728) AP(b_i, 0.572)

`a14_structure.py long_x03_0 0.6 0.03` with `structure.json`: this explains 0 to 100 ms at **-48.3 dB**; the first
arrival it does not explain is at 6117 samples (138.7 ms). The factor c is 0.0401: the product of the direct gains
of sections not yet found (**F**; the gains g are from one decimal fit each, **F**; the delays are **M**).

The twelve delays lie on one geometric series, `1323.46 x 1.10071^k`: k = 0..3 (30.00, 33.04, 36.37, 40.02 ms),
k = 7 (2591, predicted 2590.7), k = 8..11, k = 15 (5581, predicted 5582.3). The members k = 4, 5, 6 (1943, 2137,
2353) appear in the first unexplained arrivals: copies of ±0.2850 at 4794 + a_i, 4988 + a_i and 5204 + a_i with
the sign patterns (+ - - +), (+ + - -), (+ - + -), which are rows 2 to 4 of a Hadamard matrix of order 4, and
4794, 4988, 5204 = 2851 + (1943, 2137, 2353). **M** for the numbers, **G** for what follows:

**Structural hypothesis to start a model from.** Four channels, sixteen all-passes D_k = 1323.46 x 1.10071^k
rounded to whole samples, in pairs (k, k + 8). Stage 1: channel i carries AP(D_i) AP(D_i+8), i = 0..3. A Hadamard
mix of order 4 follows; its sum row passes AP(D_7) AP(D_15) and reaches the output directly; its three difference
rows pass the pairs (4, 12), (5, 13), (6, 14) and reach the output only later (first at 2851 + D_4..6 samples).
The gains are not one number (0.3728, 0.572, 0.466, 0.474 so far). Something recirculates: four all-passes in a
row cannot put the energy peak at 1 s nor give a tail of -29 dB/s (the longest one alone falls 51 dB/s), and the
missing direct-gain product 0.0401 says the direct copy passes more sections than the ten found.

## 4. Macro

| | Value | Mark |
|---|---|---|
| Dry gain at Macro 25 / 50 / 75 / 100 % (direct copy of the solved input) | 0.9253 / 0.7098 / 0.3862 / 0.0038 = cos(pi m / 2) + 0.003806 sin(pi m / 2): 0.92535 / 0.70980 / 0.38620 | M |
| Wet gain (copy at 2591) | 0.0045 / 0.0059 / 0.0064 at 50 / 75 / 100 % = 0.0064 sin(pi m / 2) | M |
| Steady state at 0, 10, ..., 100 % (x05, from 4 s into each step) | -120.8, -105.9, -101.0, -98.2, -96.3, -94.8, -93.6, -92.4, -91.3, -90.2, -88.9 dB | M |
| Tone level against Macro (x05, 1 kHz, L) | +0.00 +1.15 +1.98 +2.54 +2.87 +2.99 +2.89 +2.57 +2.02 +1.21 +0.08 dB: dry and wet are nearly in phase at this one frequency; it is not a level law | M |
| Wet gain behind the kernel, not in front | first 0.2 s of a step: -36 to -52 dB behind, -22 to -36 dB in front | M |
| The kernel runs at Macro 0 | the tone played at Macro 0 comes out of the wet path at once when Macro rises (step 0 to 10 %: -70 dB whole step) | M |
| Motion | Abyss's smoothing (Macro taken 44 samples early, one pole of 10 ms): -48 to -64 dB in the first 0.2 s of a step; no smoothing -36 to -52; 30 ms -35 to -52; 100 ms -28 to -46 | F, on steps of 10 % upward on a block edge only |

## 5. How close the model is

`foam_model.render(stimulus, first_frame, decay, size, macro)`: measured kernel, origin from `origin.json`, nothing
fitted per recording. Null = `revocean.null_db` over the whole recording, both channels.

| Recordings | Null | Base alone |
|---|---|---|
| c02 (train, Decay 0.5 s), c03, c11 (impulses L, R, both), c12 / c13 (Size 60 / 150 %), c14 (Decay 8 s) | -77.8 to -78.0 dB | +2.9 to +3.0 dB |
| c09 / c08 / c10 (Macro 25 / 50 / 75 %) | -86.2 / -80.8 / -78.6 dB | -8.2 / -2.3 / +0.9 dB |
| c04 programme, c05 noise, x06 sweep | -78.7, -77.9, -78.4 dB | +2.5 to +3.0 dB |
| c06 1 kHz, c07 220 Hz, x04 1 kHz 120 s | -88.1, -85.5, -88.9 dB | -4.3, +4.0, -11.4 dB |
| probes 20, 25, ..., 95, 99 (610 to 2980 s) | -82.2 to -82.3 dB | |
| x02 impulses 0..29, 5..34, 70..99, 140..147; x03 0..29, 44..72 (3681 to 4584 s) | -77.8 to -77.9 dB | |

The limit is the kernel, not the structure: it ends at 3.77 s (what lies behind is about -74 dB of its energy) and
its last second comes from blocks solved at -58 to -80 dB. Late parts of recordings show it (third quarter of the
impulse cases: -35 dB, on a signal 60 dB down). Not checked: c15 (Decay changed during the ring-out).

## 6. Open questions

1. **The rest of the topology** (needs no session): the paths of the three difference rows, what recirculates, the
   gains of the six all-passes not yet seen directly, the output mix. Route: extend `structure.json`, peel with
   `a14_structure.py` (it lists the first arrivals a structure does not explain), check with `foam_model.render(...,
   kernel=a14_structure.kernel(n, S))`. The measured kernel is the target; -78 dB is what a correct structure must
   reach or beat (its tail would then be complete).
2. **Tempo and transport.** Every delay found is a whole number of 44.1 kHz samples and the first is 30.000 ms; none
   is a note value at 120 BPM, and nothing follows the play head. Expected: no dependence. Not tested.
3. **Host rate.** Expected: the same kernel at the internal rate. Only 48 kHz was recorded.
4. **Outer controls in Foam**: pre-delay in front of or behind the kernel, Width, the filters, Transients, Ducking.
5. **Macro in motion**: only steps of 10 % upward on a block edge.
6. **The switch itself**: whether the kernel's buffers run in Tide and Abyss (they run in Foam at Macro 0), and what
   they hold at the switch. Probe 17 (the switch) was not saved.
7. Input above 0.5; more than 5050 s of processed time; the first 543 s of an instance in Foam.

## 7. Proposed next session (most valuable first)

All at Decay 0.5 s, Size 100 %, 48 kHz, block 512 unless stated; each case behind the usual flush. "Pair" = the
Macro 0 train of c01 (for the origin) followed by one left impulse of 0.5 at 0.25 s in 6 s at Macro 100 %; it is
read with `a00_origin.py` and `a09_wide.py ... 0.2 1` in a minute, and the answer is the tap list of section 3.

1. **T90**: the pair at 90 BPM with the play head 0.25 s ahead, and again with the transport stopped
   (`work/abyss/probe7_tempo.py` has both host settings). Settles question 2. Two minutes of the owner's time.
2. **Outer controls**, Macro 100 %, the same impulse plus the 2 s noise burst of c15: pre-delay 50 ms and 200 ms
   (does the direct copy move with the wash, and does the pre-delay sit in front of the kernel), Width 0 and 100 %,
   HPF and LPF at mid positions, Brightness at both ends, Transients and Ducking at 100 % (burst only).
3. **Macro in motion** on the 1 kHz tone and on noise at 0.05 rms: steps 0 to 100 % and 100 to 0 % at 0, 100 and 300
   frames behind a block edge; a linear ramp 0 to 100 % over 2 s and back.
4. **Host rate**: the pair at 44.1 kHz and at 96 kHz (two more instances; the mode has to be switched by hand in each).
5. **The switch**: probes of 20 s as in this session, but all saved, with one left impulse every 2 s, switched
   Tide to Foam by hand once; then the same from Abyss. Shows what the kernel's buffers hold at the switch.
6. **Level**: the impulse at 1.0 and a 1 kHz tone at 0.9, Macro 100 % and 50 %.

Not needed: denser impulse maps, longer tones, other Decay or Size positions.

## 8. Files

| File | What |
|---|---|
| `fcommon.py` | session A, the base with an origin, origin scan |
| `foam_model.py` | the model of section 1 with the measured kernel |
| `a00` to `a05` | origin, Macro 0, envelopes, repeatability |
| `a06_solve.py`, `a07_kernel.py`, `a08_early.py` | front test below 1 kHz and its surrogates; first looks at the solved input |
| `a09_wide.py`, `a10_taps.py`, `a12_kernel.py` | wide-band solve of 0.2 s, arrivals, bare tap gains and their formal logarithm |
| `a11_extend.py`, `a13_longkernel.py` | the whole kernel |
| `a14_structure.py`, `structure.json` | the all-pass structure found so far and what it leaves unexplained |
| `a15_validate.py`, `a16_macro.py`, `a17_time.py` | nulls, Macro law, processed time |
| `origin.json`, `kernel_long_x03_0.npy`, `kernel_w_*.npy`, `solve_*.npz`, `*.log` | results |
