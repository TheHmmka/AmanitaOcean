# first_order_verification: an independent attempt to break the first-pass model

Packet `first_order_verification`, 6 October 2026. Subject: `first_order_model.py`,
`fit_first_order.py`, `tide_structural_data/first_order.json`, `findings/first_order.md`.
Everything below comes from 36 captures of this packet (seed family 86031, about 0.75 GB of
cache) and from code of this packet; nothing was fitted to make the model look better.

Tags: MEASURED (read off captures), FITTED (a constant, residual given), INFERRED (reasoned,
not tested), OPEN. Verdicts: CONFIRMED, PARTLY, REFUTED, UNVERIFIED. A null is
`20 log10(rms(model - reference) / rms(reference))` on raw 48 kHz samples [1200, 2500) with no
gain, delay or polarity fit, unless another window is named.

## 0. Operating point, and the owner's question (Tide 100 %, Mix 100 %?)

- **Mix: 100 % in every capture.** MEASURED: read-back `Mix = 100` in all 36 captures.
- **Tide (the Macro knob): the model and its -80 dB are for Macro 0 %, not for Tide 100 %.**
  MEASURED: read-back of my 33 Macro 0 captures: `Macro = 0.000`, `Decay = 0.500` (two at
  1.00 and 2.00), `Size = 100`, `Width = 100`, `Brightness = 0.000`, `HPF = 20.0`,
  `LPF = 20000`, `Predelay = 0.000`, `Ducking = 0.000`, `Transients = 0.000`. The findings
  and the model's docstring say the same; nothing in them is presented as a Tide 100 %
  result that is not one.
- **At Tide 100 %, Mix 100 %** (3 captures of this packet, read-back `Macro = 100`,
  `Mix = 100`; section 5) the network of the model is the one in force, behind the Tide
  input stage: driven at the exit of the Tide comb delay and passed through one two-pole
  filter per impulse it nulls the first arrivals at **-47 to -52 dB** (median of 87 impulses,
  all four paths, three instance/input cases), against 0.0 dB with the oscillators 1 s late
  and -6 to -10 dB when the comb delay is ignored. That is a sharper result than the
  findings' -21.5 / -22.0 dB, and it holds on instances the author never saw.
- So the answer is: Mix yes; the model itself is Tide 0 %; at Tide 100 % it is verified as
  the unchanged core of the sound to about -50 dB, not as a model of the Tide stage
  (comb, high-pass, voice filters), which `predict` does not contain.
- That the plug-in is in its Tide mode cannot be verified by anyone: the mode is not a host
  parameter (README item 1). UNVERIFIED.

## 1. Verdicts

| # | Claim of the findings / judge | Verdict | Numbers of this packet |
|---|---|---|---|
| 1 | Mix 100 %, Macro 0 %; at Macro 100 % the same network sits behind the Tide delay | CONFIRMED, sharpened | read-backs above; -47 to -52 dB at Macro 100 % (section 5) |
| 2 | The model does not read the holdout; constants regenerate bit for bit; `predict` is structural | CONFIRMED | regenerated file byte-identical (35 s, no capture rendered); section 6 |
| 2b | Packets a, b, c reproduce -63.05 / -68.71 / -70.03 dB | UNVERIFIED | not re-run here, to keep holdout use to one scoring |
| 3 | Holdout -73.92 dB; fresh clean train -80.07 dB; the holdout is limited by the tail of the previous impulse | CONFIRMED | holdout re-scored once: -73.92 (paths -73.80 / -74.76 / -74.83 / -73.71, worst -66.88). Fresh: **-80.09 dB**, 316 responses. Tail removed exactly: -74.31 -> -80.11 dB |
| 4 | Start phases 11.25 k rad, unwrapped, single-precision accumulator "wrap test, use, add" | CONFIRMED | free refit: +6.0e-7 rad (4.5e-7 to 7.3e-7) on 16 lines; wrap after the addition -74.7 dB; start modulo 2 pi -60.3 dB; ideal phase -25.0 dB at best |
| 5 | 16 lines per group at the listed primes | CONFIRMED (15 by a search free in phase; the 16th more firmly than claimed) | section 4.2 |
| 5b | No 17th line above 0.007 | PARTLY | nothing unexplained above 0.0061 for centres 6400 to 12000; below 6400 a weak line could hide (clutter up to 0.03) |
| 6 | Converters: Kaiser sinc 0.9 / 17 / 6, table of 4096 entries without interpolation, gain 1 | CONFIRMED | exact kernel -68.1 dB; 2048 / 8192 entries -67.2 / -72.6 dB; beta 5.9 / 6.1 -62.2 / -62.3; cut-off 0.899 / 0.901 -52.7 / -52.8; 16 / 18 crossings -45.3 / -46.0; least-squares gain 1.0000002 |
| 7 | Lattice delays 4250/160 and 18592/147; arrival at 203.2789 + 1.08844 L (88 + L at 44.1 kHz); one wing takes the lower entry | CONFIRMED | either delay moved by one lattice step, both directions: -41.3 dB; 44.1 kHz network -80.19 dB; no wing rule -74.5, other wing -73.2 dB |
| 8 | Time origin = first processed sample, warm-up included; block size plays no part | CONFIRMED | warm-ups of 163337 and 2270413 samples: -80.41 / -80.10 dB; one host sample off -49.2 to -49.8 dB; 10 s origin +2.7 / +2.9 dB; block 512 and 100 bit-identical |
| 9 | Linear interpolation at the length of the sample being read; single-precision length; depth 0.88 ms; nominal 0.6 Hz | CONFIRMED | length taken at the write: +2.5 dB; double-precision length -79.22 against -80.09 dB; depth off by +3.4e-6 samples; rate off by 3e-11 Hz (largest 5e-10) |
| 10 | Gain = coefficient x Decay attenuation x cross feed from the width law | CONFIRMED at Decay 0.5 s; PARTLY as a law | own gains within 4.1e-6, cross feed within 1.8e-6 on lines 1 to 8. At Decay 1 s / 2 s the formula alone gives -33.5 / -23.8 dB: the coefficient depends on Decay (section 4.4) |
| 11 | Six second passes per group, feedback magnitude 0.247772, weights 1, 1.377953, 1.517010, signs, 40-tap loop kernel, no exchange between groups | CONFIRMED with two qualifications | all six gains within 2.3e-5 at 44.1 kHz; cross-group passes below 3.6e-5 of the feedback. Only 1->1 (both groups) and 1->2 (left) matter in the 48 kHz window; "1/4 is rejected" holds only for the chosen normalisation (section 4.5) |
| 12 | Fixed filter = 20 kHz Q 1 low-pass times a low-frequency correction, no 20 Hz high-pass | CONFIRMED | ratio to the low-pass reproduced from the taps (+0.30, +0.76, 0, -0.10, +0.24, +0.30 dB at 0, 200, 1000, 3000, 10000, 20000 Hz); a free 32-tap cascade on fresh data gains 0.0005 dB |
| 13 | Packet c's "-71 dB floor" was the previous impulse's tail | UNVERIFIED directly | the mechanism is confirmed (claim 3); c's constants were not run here |
| 14 | Residual -80 dB, proportional to the local signal, mostly 300 Hz to 6 kHz; its larger part is proportional to the speed of the line length | CONFIRMED, extended to 48 kHz | published kernel, nothing refitted: **-80.09 -> -85.42 dB** at 48 kHz, -80.12 -> -85.20 dB at 44.1 kHz |
| 14b | Above 21 kHz the model is at -50 dB | REFUTED as a number | -31 dB (Blackman-Harris window), -35 dB (Hann segments); 0.3 to 0.5 % of the residual either way |

New, not in the findings (sections 3 and 4.4):

- **The reference is not linear above an input peak of 0.56 to 0.58 of full scale.** The
  model is linear, so its -80 dB holds only below that level: a unit impulse of amplitude
  1.0 scores -68.1 dB, a 50 ms tone at full scale -28 dB (reference against itself).
- **The model joined with the Decay weights of `laws_verification` holds at Decay 1 s and
  2 s: -80.04 and -79.76 dB**, nothing fitted.

## 2. Fresh captures: where the model is weak

Clean trains: 158 impulse times of my own (seed 86031), 0.5 to 0.75 s apart with 157
different residues modulo 160 (the period of the 48 kHz / 44.1 kHz lattice), rendered as
two halves so that impulses are at least 1.03 s apart, on both inputs: 316 responses.
`first_order_model.predict` is used as published.

| split | null |
|---|---|
| all 316 responses | **-80.09 dB** (best -82.33, median -80.15, worst -76.25) |
| L->L, L->R, R->L, R->R | -80.11, -80.08, -80.09, -80.06 dB |
| impulse time, ten bins of 10 s | -79.71 (40 to 50 s) to -80.28 dB (80 to 90 s) |
| impulse 5 samples after the warm-up | -79.13 (left input), -79.94 dB (right input) |
| input sample index modulo 5 = 0 (exact table entries) / 1 / 2 / 3 / 4 | -80.19 / -80.17 / -80.10 / -79.96 / -80.02 dB |
| output sample index modulo 5 = 1 (exact table entries) / 0 / 2 / 3 / 4 | **-79.56** / -80.26 / -80.37 / -80.19 / -80.10 dB |
| position of the impulse between internal samples, eight bins | -79.79 to -80.34 dB |
| 157 residues modulo 160, two responses each | -81.20 best, -80.18 median, -77.51 worst (residue 38) |
| least-squares gain of the model | 1.0000002 |

By band (one Blackman-Harris window over the scored window; share of the residual energy):

| band | residual re reference in the band | share |
|---|---|---|
| 0 to 300 Hz | -76.8 dB | 5.1 % |
| 300 Hz to 1 kHz | **-75.7 dB** | 14.3 % |
| 1 to 3 kHz | -77.0 dB | 26.7 % |
| 3 to 6 kHz | -79.7 dB | 21.1 % |
| 6 to 10 kHz | -82.2 dB | 14.4 % |
| 10 to 14 kHz | -83.3 dB | 8.8 % |
| 14 to 19 kHz | -83.1 dB | 8.3 % |
| 19 to 20 kHz | -80.5 dB | 0.6 % |
| 20 to 21 kHz | -70.0 dB | 0.2 % |
| 21 to 24 kHz | **-31.2 dB** | 0.5 % |

By time inside the window (stretches of 100 samples; second number: energy density of the
reference in the stretch against the mean of the window): -79.2 to -81.9 dB in every
stretch that holds an arrival, -78.5 dB in the last one (2400 to 2500, where the second
passes arrive); **-57.9 dB (raw 2000 to 2100) and -57.4 dB (2200 to 2300) where the
reference itself is 35 dB down**; -85.9 dB in 1200 to 1300.

MEASURED weak spots, in order of weight:

1. The band 300 Hz to 3 kHz (41 % of the residual at -75.7 to -77.0 dB). It is the term
   proportional to the speed of the line length (claim 14): with the 24-tap kernel published
   in `first_order.json` (fitted by the author at a 44.1 kHz host) added to the model, my
   48 kHz trains go from -80.09 to **-85.42 dB**, and the residual energy of a response
   correlates 0.76 with the energy that term predicts for it. The same kernel on my 44.1 kHz
   train: -80.12 to -85.20 dB. This is a reproducible element of the reference that the
   model leaves out; it is worth 5.3 dB.
2. The quiet stretches between arrival groups: there the model explains 57 dB of what is
   there, not 80. Irrelevant for the score (93 dB below the window mean), relevant if a
   later model is scored on the gaps.
3. Above 21 kHz: -31 dB. The findings give -50.5 dB; I do not reproduce that number with
   either window (REFUTED as a number). The stop band of the converters is not modelled
   well; it holds 0.5 % of the residual.
4. Output samples that sit on exact table entries stay 0.6 dB worse than the others with
   the wing rule in place (-79.56 against -80.10 to -80.37 dB), as the findings say.

No class of impulse time, lattice position or path is worse than -76 dB, and nothing drifts
over the 100 s.

### The holdout score, tested by exact subtraction

The same 158 times rendered complete (left input) put every response on the tail of the
previous one, as in the holdout. The reference is linear at this level (complete minus the
two halves: -130.1 dB), so the other half's capture *is* the tail and can be subtracted.

| gap to the previous impulse | responses | model | tail of the previous impulse | model, tail subtracted |
|---|---|---|---|---|
| 0.50 to 0.55 s | 27 | -68.9 dB | -69.2 dB | -80.2 dB |
| 0.55 to 0.60 s | 38 | -74.7 dB | -76.3 dB | -79.9 dB |
| 0.60 to 0.65 s | 28 | -78.3 dB | -82.5 dB | -80.3 dB |
| 0.65 to 0.70 s | 34 | -79.7 dB | -89.3 dB | -80.2 dB |
| 0.70 to 0.75 s | 29 | -79.9 dB | -95.2 dB | -80.0 dB |
| all 157 | | **-74.31 dB** | -75.6 dB | **-80.11 dB** |

MEASURED. The findings' explanation of the -73.92 dB is right, and no first-pass model can
score below about -74 dB on that holdout.

## 3. Stimuli that are not impulses, and the level limit

176 events at least 1 s apart in two captures (seeds 86032, 86033): noise bursts of 16, 48
and 96 samples on the left, the right and both inputs, pairs of impulses 0 to 55 samples
apart on the same or on opposite inputs, 96-sample tone bursts at 0.5 to 18 kHz, and single
impulses of amplitude 1.0, 0.9, 0.01 and -0.03. Each is predicted twice: by superposition
of `predict` over every non-zero input sample, and by feeding the event into my own
simulation of the model (`vmodel.py`). The two agree at -304 dB.

| events (input peak at most 0.56) | count | null |
|---|---|---|
| pairs, same input / opposite inputs | 26 / 24 | -80.18 / -80.00 dB |
| impulses of amplitude 0.01 / -0.03 | 6 / 6 | -80.52 / -80.11 dB |
| noise bursts | 23 | -79.76 dB |
| tone bursts 18 kHz / 12 kHz / 6 kHz | 8 / 1 / 3 | -81.9 / -83.3 / -80.9 dB |
| tone bursts 2 kHz / 500 Hz | 4 / 8 | -77.0 / -75.7 dB |
| all 109 | | -78.19 dB |

MEASURED. Superposition holds; narrow-band events score what the band table of section 2
says for their band, which is why the 500 Hz and 2 kHz bursts are the worst.

| events (input peak above 0.56) | count | null | after one gain per event | that gain |
|---|---|---|---|---|
| noise bursts, peak 0.57 to 0.74 | 40 | -78.55 dB | -80.12 dB | 0.99984 to 1.00001 |
| noise bursts, peak 0.75 to 0.90 | 15 | -71.89 dB | -80.00 dB | 0.99966 to 0.99989 |
| impulses of amplitude 0.9 | 6 | -71.29 dB | -79.81 dB | 0.99974 to 0.99975 |
| impulses of amplitude 1.0 | 6 | **-68.09 dB** | -80.15 dB | 0.99962 |

So the model does break here, and the cause is the reference, not the network.

### The reference is not linear above about -5 dBFS (new)

Model-free test (`v5_level.py`): one stimulus of 14 events (peak 1.0) rendered at 15 scales;
the response at scale s divided by s is compared with the response at scale 0.05 divided by
0.05. MEASURED:

- Scales 0.25, 0.5, 0.52, 0.54 and 0.56: identical, -125 to -132 dB for every event (0.25
  and 0.5 agree to the last digit, as exact scaling by 2 must).
- From 0.58 up the wet signal is turned down, the more the longer the input stays loud:

| scale (input peak) | 0.58 | 0.6 | 0.7 | 0.8 | 0.9 | 1.0 | 1.41 | 2.0 |
|---|---|---|---|---|---|---|---|---|
| single impulse: 1 - gain | 1.3e-6 | 4.6e-6 | 5.2e-5 | 1.3e-4 | 2.4e-4 | 3.6e-4 | 9.2e-4 | 1.7e-3 |
| single impulse: null re linear | -117 dB | -107 | -86 | -77 | -72 | -69 | -61 | -55 |
| 1 kHz tone, 20 ms: 1 - gain | 4.7e-6 | 4.1e-5 | 1.4e-3 | 5.3e-3 | 1.2e-2 | 2.0e-2 | 6.9e-2 | 0.15 |
| 100 Hz tone, 50 ms: 1 - gain | 3.3e-5 | 2.1e-4 | 3.9e-3 | 1.2e-2 | 2.5e-2 | 4.0e-2 | 0.12 | 0.24 |
| 100 Hz tone, 50 ms: null re linear | -90 dB | -74 | -48 | -38 | -32 | **-28** | -19 | -13 |
| noise, 100 ms, both inputs: 1 - gain | 7.8e-7 | 3.8e-6 | 8.6e-5 | 4.2e-4 | 1.4e-3 | 3.4e-3 | 3.0e-2 | 0.10 |

- It is a gain on the wet output that recovers slowly: a probe impulse of 0.05 placed 2, 10,
  50, 200, 500 and 1000 ms after an impulse of 1.0 is turned down by 3.57e-4, 3.49e-4,
  3.05e-4, 1.85e-4, 6.8e-5 and 1.3e-5 (isolated as "both" minus "loud alone", against
  "probe alone"; after that one gain the probe nulls at -90 to -119 dB). FITTED recovery
  time constant **0.300 s** (0.300, 0.3005 and 0.299 s on the three later intervals).
- The detector looks at the larger of the two inputs, not at their sum: impulses on L, on R,
  on L and R together and on L and -R at scale 1.0 give 0.999641, 0.999637, 0.999635 and
  0.999646. Polarity does not matter (-L: 0.999638).
- INFERRED: the threshold is -5 dBFS (0.5623; measured between 0.56 and 0.58), and this is
  the plug-in's ducking or transient stage, which is not fully out of the path at
  Ducking 0 % and Transients 0 dB. Which control it belongs to, and its law, are OPEN and
  belong to packet `io`. The Analyzer host as the source is unlikely (a threshold, a 0.3 s
  recovery and a per-channel detector) but was not excluded by measurement.

Consequences. (1) README item 2 ("linear at -130 dB") is true only up to an input peak of
0.56; every campaign dataset uses amplitude 0.5, 1 dB under the threshold, so the existing
results stand. (2) The first-pass model is valid for input peaks up to -5 dBFS; at full
scale the missing gain stage is worth -68 dB on an impulse and -28 to -50 dB on sustained
material. A Character that follows the reference at Mix 100 % needs this stage, or a
statement that it is left out.

## 4. The claims, one by one

`vmodel.py` is my own implementation, written from the formulas of section 4 of the findings
and sharing no code with `first_order_model.py`. For unit impulses it reproduces `predict`
at -307 dB, so the findings' formulas are the model. It lets one element be replaced at a
time and gives the derivative of the response with respect to every constant.

### 4.1 Free refit around the model (lines 1 to 8, both groups, 48 kHz, 316 fresh responses)

The residual is regressed on the derivative of the model with respect to own gain, cross
feed, centre delay, modulation depth, start phase and modulation rate of every line, the
gain and cross feed of every second pass, and three second passes the model does not have:
132 free numbers. They buy 0.34 dB (-80.09 -> -80.43 dB). FITTED deviations from the
model's constants over the 16 lines (largest half-difference between the two halves of the
data in brackets):

| constant | deviation | largest |
|---|---|---|
| own gain, relative | mean +1e-7 | 4.1e-6 (3.7e-6) |
| cross feed (ratio) | mean +3e-7 | 1.8e-6 (2.2e-6) |
| centre delay | mean -4e-8 samples | 1.6e-6 samples (3.0e-6) |
| modulation depth | mean +3.4e-6 samples of 38.808 | 6.1e-6 samples (2.9e-6) |
| start phase | mean **+6.0e-7 rad**, 4.5e-7 to 7.3e-7 on all 16 | (2.2e-7) |
| modulation rate | mean +3e-11 Hz | 4.8e-10 Hz (3.9e-10) |

Delay, depth, rate, phase and gain of every line that reaches the window are therefore
confirmed far beyond what the -80 dB residual would suggest. The only systematic entry is
the phase: all sixteen oscillators sit 6e-7 rad (0.007 internal samples of oscillator time,
2e-5 samples of delay) ahead of the rule. It is real (half-differences 1e-8 to 2e-7) and
without consequence; its cause is OPEN.

### 4.2 Number of lines (44.1 kHz host, own train of 88 impulses, both inputs)

A search that assumes neither the primes nor the phase rule: the own-input responses are
projected on the two interpolation samples of a line of every whole centre from 900 to
12000 samples and every start phase on a grid of 384 values (0.0164 rad), with depth
38.808 samples and the accumulator's mean rate (0.5998782 Hz). MEASURED:

- The 15 strongest separate peaks of each group are lines 1 to 15 of the model, at the
  claimed primes, with start phases within 0.010 rad of 11.25 k rad.
- Line 16 (7589): for centres from 6400 to 12000 it is the strongest peak of the left group
  (0.0118; rms of that region 0.0010) and the second strongest of the right group (0.0106),
  and in both groups the best of the 384 phases is the rule's (4.483 against 4.491 rad,
  3.174 against 3.175 rad).
- The other peaks there are second passes whose two modulations happen to add up to one of
  the same depth, at the phase that predicts: 9372 = 1783 + 7589 (left, 0.0094) and
  9376 = 1787 + 7589 (right, 0.0113), line 7 read again by line 16; 11560 = 5261 + 6299 in
  both groups (-0.0085, -0.0079), line 14 read again by line 15. INFERRED identification
  (centre exact, phase within the grid, both groups). The first of them needs a line of
  7589 samples in each group, so it supports the 16th line independently.
- Nothing else exceeds 0.0061 between 6400 and 12000. Below 6400 the side lobes of the
  strong lines reach 0.03 and a weak extra line could not be seen; at the phases the rule
  would give accumulators 32 to 39 the largest values are 0.009 to 0.015, all next to
  strong lines. The author's bound of 0.007 there is not improved. A 17th line is not
  indicated and not excluded below these levels.
- Projected coefficients of lines 9 to 16 agree with the model's within 5 % after the
  calibration on lines 1 to 8 (factor 1.08). Their cross feed (width law on lines 9 to 16)
  was not measured here: UNVERIFIED.

### 4.3 Oscillators, read rule, time origin (48 kHz, fresh clean trains)

| element replaced | null |
|---|---|
| the model | -80.09 dB |
| wrap test after the addition instead of before the use | -74.72 dB |
| start phase reduced modulo 2 pi before the first step | -60.34 dB |
| double-precision phase 11.25 k + 2 pi 0.6 m / 44100 | +0.77 dB |
| the same with the accumulator's mean rate 0.5998782 Hz | -25.03 dB |
| all oscillators one internal sample late / early | -48.53 / -48.66 dB |
| phase step 11.2499 / 11.2501 rad | -28.46 / -28.46 dB |
| nominal rate 0.599878 / 0.6001 Hz in the accumulator | +2.56 / -13.41 dB |
| depth 0.8803 / 0.8799 ms | -36.58 / -46.11 dB |
| line length kept in double precision | -79.22 dB |
| length in force when the sample is written, not when it is read | +2.48 dB |

MEASURED. The single-precision accumulator is not a refinement: no ideal sinusoid comes
closer than -25 dB. The mean rate of the accumulator of line L1 between 10 and 110 s is
0.5998782 Hz, the README's 0.59988.

Time origin. Two warm-ups the model never met, chosen off the 160-sample period and off
multiples of 5 (163337 samples = 3.4029 s, 26 impulses; 2270413 samples = 47.3003 s, 61
impulses, inputs alternating): -80.41 and -80.10 dB with the model's clocks started at the
first processed sample, -49.78 / -49.63 and -49.38 / -49.23 dB one host sample later /
earlier, +2.66 and +2.94 dB with the 10 s origin. The second capture reaches 115.4 s after
the start, beyond anything the model was built on; its five responses later than 110 s
score -80.05 dB. Host block 512 against 100: bit-identical output. Note that the module
function `predict` is fixed to the 10 s warm-up; another warm-up needs
`Model(constants, warmup_frames=...)`.

### 4.4 Gains and the Decay attenuation

- Own gains and cross feed of lines 1 to 8: section 4.1 (4.1e-6 and 1.8e-6). Width 0.27 on
  lines 1 to 6: -54.74 dB; width of line 8 set to 0.92 instead of 0.9191: -74.00 dB; lines
  1 to 7 only: -8.9 dB.
- The split of a gain into "coefficient" and `10^(-3 prime / (44100 x 0.5 s))` cannot be
  tested at one Decay. Two left-input trains at other settings (read-back `1.00` and
  `2.00`, gaps of two decay times, 47 and 24 impulses):

| model | Decay 1 s | Decay 2 s |
|---|---|---|
| constants of Decay 0.5 s unchanged | -14.13 dB | -11.49 dB |
| `decay_seconds` set to the Decay, nothing else | **-33.52 dB** (gain 0.983 / 0.978) | **-23.82 dB** (0.949 / 0.937) |
| plus the output-tap weights `1 + k_l (T - 0.5 s)` of `laws_verification.md`, on first passes only | -63.00 dB | -53.55 dB |
| the same weights also on the exit line of a second pass | **-80.04 dB** | **-79.76 dB** |

  MEASURED, nothing fitted. So (a) the claim's formula is the gain at Decay 0.5 s only: the
  "coefficient" is a function of Decay, as packet `laws` found; (b) with the eight slopes
  of `laws_verification` the first-pass model carries over to other Decay settings at its
  own floor, second passes included, which also confirms that the weight belongs to the
  line a path leaves through and that the feedback magnitude and the loop kernel do not
  depend on Decay (1 s, 2 s). The form `10^(-3 prime / (44100 T))` is CONFIRMED by this.

### 4.5 Second passes

- Energy of each second pass in the 48 kHz scored window (re the window's reference):
  left group 1->1 -28.3 dB, 1->2 -30.5 dB, 2->1 -65.4 dB, 2->2 -77.8 dB; right group 1->1
  -28.8 dB, 1->2 -92.6 dB, the rest below -126 dB; **1->3 and 3->1 do not reach the window
  at all in either group** (the modulations of neighbouring lines are almost opposite, so
  their sum moves by only +-21 host samples). The holdout score therefore tests 1->1 and
  the left 1->2; the other constants are tested at 44.1 kHz only.
- 48 kHz, free refit: 1->1 off by +6e-6 (left) and -1e-5 (right, half-difference 2e-5);
  left 1->2 off by +3.8e-5 (1e-5).
- 44.1 kHz, own train of 88 impulses: network null -80.19 dB on raw [1000, 2060) (first
  passes only) and -80.12 dB on [1000, 2262), where all six second passes are present
  (about -21 to -34 dB each). Free gains of the twelve second passes: within 2.3e-5 of the
  model's for the own input and 2.0e-5 for the other input; first passes within 9.9e-6.
  CONFIRMED: one magnitude, two weights and the signs describe them.
- No exchange between the groups: a line of the other group read again by a line of this
  group (1->1, visible at -28 dB) fits at -1.8e-5 and +3.6e-5 of the feedback magnitude.
  CONFIRMED for the entries that reach the window.
- "Feedback magnitude 0.247772; 1/4 is rejected (-66.8 dB)": with the magnitude set to 1/4
  and the loop kernel scaled by 0.247772 / 0.25 the null is -80.0868 dB, identical to the
  model's. Only the product of magnitude and kernel is measured; the convention-free
  numbers are the first tap of that product, 0.247772, and its sum, 0.24209. Whether the
  matrix entry is 1/4 followed by a kernel starting at 0.9911 is OPEN, not rejected.
- Other ablations on fresh data: output weights 1 -43.5 dB, sign of 2->1 flipped -61.1 dB,
  no loop kernel -67.6 dB, no second passes -26.1 dB.

### 4.6 Converters and the fixed filter

Converter ablations are in the verdict table (claims 6 and 7); every value agrees with the
author's table within 0.2 dB although the train is different. Lattice delays are sharp in
both directions (-41.32 / -41.32 dB input, -41.32 / -41.32 dB output for +-1 step).

Fixed filter, computed from the 960 taps: gain 1.1421 at 1 kHz, sum of taps 1.1824, energy
beyond tap 64 -50.1 dB, taps 300 and 600 -2.0e-5 and -3e-7. Divided by a bilinear two-pole
low-pass at 20 kHz, Q 1, re 1 kHz: +0.30 dB at 0 Hz, +0.50 at 60 Hz, +0.76 at 200 Hz, +0.41
at 500 Hz, -0.10 at 3 kHz, +0.13 at 6 kHz, +0.24 at 10 kHz, +0.29 to +0.30 from 15 to
22 kHz; these are the findings' numbers. On fresh data a free 32-tap filter in cascade with
the model (lags -4 to 27) takes -80.0868 to -80.0873 dB: the filter is not where the
residual comes from. The filter and the loop kernel remain the model's two measured
kernels (1000 numbers); everything else is closed form.

## 5. Tide 100 %, Mix 100 %

Captures: 87 impulses 1.0 to 1.3 s apart at Macro 100 %; left input on two instances, right
input on one; read-back `Macro = 100`, `Mix = 100`. Tide stage from
`tide_verification.md`: comb delay `469.44 + 256.91 tri((t - 0.001 s)/200 s + 0.25 c)`
samples in force at the exit, a high-pass near 30 Hz in the delayed path, a two-pole
low-pass per output behind the network. For each impulse the reference from the first
arrival to the arrival of the comb's second exit (249 to 758 samples) is compared with the
model driven at the comb's first exit, through one filter per impulse:
two free poles, seven taps on the model's response to the impulse and three on its response
to the tail `(p - 1) p^j` of a one-pole 30 Hz high-pass (12 numbers; coefficients from the
equation error, the null is that of the simulated output).

| regressor | median over 87 impulses, six path/instance cases | energy-weighted |
|---|---|---|
| model at the comb exit, with the high-pass tail | **-47.4 to -52.3 dB** (quartiles -57 to -44, best -69) | -49.0 to -51.0 dB |
| the same, tail tied to the impulse (9 numbers) | -47.4 to -52.3 dB | -49.0 to -50.9 dB |
| the same without the tail (9 numbers) | -19.3 to -20.5 dB | -19.5 to -20.6 dB |
| free 33-tap FIR, no poles, no tail (the kind of test in the findings) | -15.1 to -20.8 dB | -11.4 to -13.7 dB |
| control: model at the impulse time, merely shifted | -5.8 to -10.1 dB | -5.3 to -7.6 dB |
| control: oscillators 1 s late | 0.0 dB | -0.1 to 0.0 dB |
| control: oscillators 0.1 s late | -0.1 to 0.0 dB | 0.0 to +0.5 dB |
| control: comb delay off by 24 samples | 0.0 to +0.1 dB | +0.2 to +0.5 dB |

MEASURED. Lines, modulation law, start phases, time origin and relative gains of the model
are in force at the listening point on three instances and all four paths, to about
-50 dB; the findings' -21.5 / -22.0 dB was limited by the missing high-pass tail and by the
free filter, not by the network. The worst impulses (-0.6 to -29 dB) are those where the
voice is closed (level down to 129 dB below the model's) and nothing is there to fit.
Limits: twelve free numbers per impulse on 249 to 758 samples; the controls show what they
can absorb (nothing when the timing is wrong). No fit of the model at the comb exit was
unstable; up to 12 of 87 control fits per case were and are left out of the control rows. This is not a model of Tide: comb echoes,
voice law and its random phase are packet `tide`.

## 6. Audit trail

- `fit_first_order.py` uses `datasets` only for `GRID_SETTINGS`, `GRID_SECONDS`,
  `IMPULSE_AMPLITUDE` and `grid_times` (the stimulus of the Tide check). It contains no
  reference to `holdout_first_order`, `holdout_times`, `HOLDOUT_SEED`, the cache folder or
  `np.load`. The same holds for `fit_first_order_{a,b,c}.py` and for the 23 exploratory
  scripts in `work/first_order/`. MEASURED by reading and by search.
- Regeneration: `fit_first_order.main()` run with its output redirected to my scratch folder
  wrote a file **byte-identical** to `tide_structural_data/first_order.json` (SHA-256
  dda19a86...6711b33) in 35 s without rendering a capture. MEASURED.
- Holdout: `score_first_order.py first_order_model.py` run once by me: -73.92 dB, paths
  -73.80 / -74.76 / -74.83 / -73.71, worst response -66.88. Identical to the report. No
  other use of the holdout by this packet; nothing here was chosen on it.
- Was anything chosen on the holdout by the author? The scratch folder shows a draft of the
  constants at 22:25:56 (no wing rule yet, coefficients of lines 9 to 16 zero, second-pass
  constants 1.4e-6 away), the first holdout score at 22:31:02 (-73.92 dB) and a regenerated
  file at 22:35:13 whose constants equal the final ones exactly. The wing rule was worked
  out between 22:26 and 22:28 on seed 2003 (`s17`, `s18`). The constants at the moment of
  the first scoring are not preserved, so "nothing changed after the first scoring" is
  INFERRED from the equal scores (five numbers to 0.01 dB), not shown. It is immaterial:
  the holdout is limited by the previous tail (section 2), and the model scores -80.09 dB
  on data rendered after it was frozen.
- "Fresh" trains of the author: seed 5201 and 5202 were used for the tables of the findings
  (`s23`) only after the constants were fixed; the wing rule and the table size were
  developed on packet c's seeds 2001 and 2003. Consistent with the findings.
- Structural: `first_order_model.py` imports only `json`, `pathlib` and `numpy`, renders
  nothing, and builds a response from line lengths, 32 accumulators, gains and three
  time-invariant kernels (converter table from a formula, 40-tap loop kernel, 960-tap
  fixed filter). There is no table indexed by impulse time. My independent implementation
  from the written formulas reproduces it at -307 dB. CONFIRMED.

## 7. What failed, limits, open

- I could not break the network on any class of impulse time, lattice position, path,
  warm-up or stimulus shape below an input peak of 0.56. The two things that do break the
  model lie outside it: the level-dependent gain of the reference (section 3) and the
  Decay dependence of the tap gains (section 4.4, already known to packet `laws`).
- Not covered by this packet: packets a, b, c; the cross feed of lines 9 to 16; the
  single-precision grid of the line length (only its effect on the null, 0.87 dB); host
  rates other than 48 and 44.1 kHz; Size; Width; passes after the earliest second ones.
- The 6e-7 rad lead of all oscillators and the 0.6 dB of the exact-entry output samples are
  measured and unexplained.
- The speed term is confirmed as an element of the reference at both host rates
  (-85.4 dB with it) but its origin stays OPEN, as in the findings.
- The Tide check uses the Tide stage of another packet; its -50 dB is a joint statement
  about that stage and this network.
- For the lead: README item 2 needs the level limit (linear up to an input peak of 0.56);
  README item 6 and the scorer's "first passes only" are already listed by the author.

## 8. Reproduce

Scripts and numbers are in `Analyzer/Results/RevOceanCharacterization/work/first_order_verification/`
(this packet owns no file in `tide_structural_data/`):

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Results/RevOceanCharacterization/work/first_order_verification
cc -O2 -shared -fPIC -ffp-contract=off -o vphase.dylib vphase.c
$PY vcap.py        # renders or re-reads the 18 captures of sections 2, 4 and 5
$PY v1_score.py    # section 2 and 3                      -> v1_score.json
$PY v2_claims.py   # sections 4.1, 4.3 to 4.6             -> v2_claims.json
$PY v3_core.py     # sections 4.2, 4.5 (44.1 kHz host)    -> v3_core.json, v3_surface.npy
$PY v4_tide.py     # section 5                            -> v4_tide.json
$PY v5_level.py    # section 3, level (18 more captures)  -> v5_level.json
```

`vmodel.py` is the independent implementation; `audit_*` and
`first_order_regenerated.json` are the audit of section 6. Captures: 5 dense/clean trains,
2 burst captures, 2 warm-up captures, 2 at a 44.1 kHz host, 3 at Macro 100 %, 2 block
sizes, 2 other Decay settings, 15 level scales and 3 probe captures.
