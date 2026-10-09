# Verification of packet io (packet io_verification)

Independent re-derivation of the claims in `findings/io.md` (Mix, Return, Master
Volume, Width, Pre-delay, output clipper, Ducking at 0 %, the listening point).
Own code, fresh stimuli, settings the author did not measure, another network
state (Decay 1 and 2 s, Size 70 and 130 % instead of 0.5 s and 100 %), other
realisations at Macro 100 %. None of the author's analysis functions is used.

Scripts and their JSON results: `Analyzer/Results/RevOceanCharacterization/work/io_verification/`
(`v1_linear.py` to `v10_summary.py`, `vlib.py`). 342 distinct captures, 145 of
them at Macro above 0, 611 MB of cache.

Tags: MEASURED, FITTED, INFERRED, OPEN as in the campaign. A null is
`20 log10(rms(prediction - capture) / rms(capture))`, nothing fitted.

## Tide 100 %, Mix 100 %: what was looked at

Every outer law was checked twice.

- **Macro (Tide) 0 %**, sample for sample, because only there one capture can
  predict another. The wet term of every prediction is a Mix 100 % capture.
- **Macro (Tide) 100 % with Mix 100 % as the reference**, with 12 reference
  realisations and 6 per moved control. What can be exact there is exact (dry
  coefficient, mono at Width 0 %, first wet sample against Pre-delay and Mix).
  The wet gains can only be read as levels: each law is met within 0.45 dB by
  the mean of 6 realisations (standard errors 0.14 to 0.41 dB).

Three things are **different at Tide 100 %** and the io report either does not
say so or says it too weakly (details in "Corrections"):

1. The wet does **not** pass DC at Macro 100 %. The io claim holds at Macro 0 only.
2. The wet level at Macro 100 % cycles by about 5.5 dB with a quasi-period of
   16 to 19 s that is tied to the age of the plug-in instance, not to the
   stimulus. It is not a single arch.
3. The early part of the response fades out continuously as Macro rises and is
   absent only at exactly 100 %; "the first wet sample is 522 samples later at
   100 % only" is a property of the first non-zero sample, not of where the
   energy is.

Not checked by the author and added here: the Ducking 0 % reduction also acts
at Macro 100 % (-8.86 +- 0.19 dB measured for a +12 dBFS input, model -8.57 dB).

## Verdicts

| # | Claim of the io report | Verdict | My evidence |
|---|---|---|---|
| 1 | Mix: `a = min(1, 2(1-m))`, `b = min(1, 2m)`, Mix does not change the wet | CONFIRMED | 14 new Mix values, law null -151.0 dB worst; Macro 100 %: dry gain exact at 6 values, first wet sample unmoved in 21 captures, wet level within 0.45 dB |
| 2 | Dry = input delayed by the latency; bypass the same | PARTLY | Dry: bit-identical at four rates, also at Macro 100 %. Bypass: goes through the output clipper, so it is the delayed input only up to +8 dBFS |
| 3 | Return and Master are `10^(dB/20)`; Master normalised 0 mutes | CONFIRMED | 10 + 12 captures at new values, law nulls -137.2 and -130.3 dB worst, fitted gains within 3e-6 dB of the display |
| 4 | Width is mid/side on the wet output, `gm = sqrt(2/(1+s))`, `gs = s gm`, `s = 2x` | CONFIRMED | 8 new widths, law null -144.7 dB worst; Macro 100 %: mono in 6 of 6, side/mid within 0.06 dB |
| 5 | Rejected: Width on the input, Pre-delay after the network, fractional delay | CONFIRMED | input matrix -10.8 and -11.9 dB against -145 dB; output delayed instead +2.7 dB; 20 of 20 non-integer settings are bit-identical to one whole-sample shift |
| 6 | Pre-delay `D = max(0, floor(P fs/1000) - 1)`, before the network, dry undelayed | CONFIRMED | 20 of 20 non-integer settings at four rates; dry undelayed -152.0 dB; Macro 100 %: two new settings by the first wet sample |
| 7 | At integer boundaries the double-precision formula can miss by one sample | CONFIRMED, sharpened | 2 of 10 whole-millisecond settings miss. A single-precision form reproduces 110 of 110 cases (below) |
| 8 | Output clipper: T = +8 dBFS, C = +12 dBFS, quadratic knee, last stage | CONFIRMED | nulls -143.4 to -147.9 dB on a fresh stimulus; free fit +8.000005 and +12.0000000 dBFS; five other positions -26.5 to -39.2 dB against -140.4 dB |
| 9 | Ducking at 0 % is a compressor: threshold 0 dBFS, 3.5 : 1, knee 9.983 dB, 5 / 300 ms, in dB, keyed by max(abs L, abs R) of the raw input | CONFIRMED, one correction | another design, 7 new events: -123.7 to -139.7 dB. The free-fit slope 0.7142786 and threshold 6e-6 dBFS are artefacts; the slope is exactly 5/7 (below) |
| 10 | Wet untouched for inputs up to 0.5629; 0.14 / 0.89 / 2.28 / 4.29 dB at -3 / 0 / +3 / +6 dBFS; dry never ducked | CONFIRMED | wet bit-identical up to 0.5629, changed at 0.5632; 0.14189 / 0.89134 / 2.28472 / 4.28566 dB |
| 11 | The complete shell predicts a capture with every outer control off neutral | CONFIRMED, extended | -132.5 dB at another state; -132.9 dB with ducking and clipper both active; -132.4 and -132.6 dB at 44.1 and 96 kHz |
| 12 | The laws hold at the listening point as far as the reference allows | CONFIRMED | see section 8 |
| 13 | At Macro 100 % the wet gains are confirmed only to about +-0.4 dB | CONFIRMED | per-capture spread 0.55 dB; 11 settings: mean deviation +0.12 dB, spread 0.25 dB, largest 0.45 dB |
| 14 | At Macro 100 % the wet level is not steady; first wet sample 1787 against 1265 | PARTLY | numbers reproduced, interpretation corrected (below) |
| 15 | The wet passes DC | PARTLY | true at Macro 0 (same DC gain as the author's); at Macro 100 % the wet has no DC at all (below) |

The author's script reproduces the data file: `measure_io.main()` with its
output redirected to the scratch folder gives a JSON equal to the published
`io.json` in every value (502 distinct captures, 979 MB, no new render; the
report says 496 and 971 MB). 64 of those captures are realisations at Macro
above 0 (61 at 100 %), so the Macro 100 % numbers of `io.json` reproduce from
the cache only; a cold run would draw new realisations.

## Corrections and better values

### C1. DC through the wet depends on Macro (claim 15)

MEASURED, DC input (0.4, -0.2), mean of the wet 1.5 to 2.9 s after the onset,
two realisations per Macro value:

| Macro % | L wet / L wet at Macro 0 | R wet / R wet at Macro 0 | fluctuation re the Macro 0 DC |
|---|---|---|---|
| 0 | 1 (0.229542, steady) | 1 (-0.032362) | -88 dB |
| 10 | 0.48, 0.54 | 1.04, 1.04 | -39, -34 dB |
| 25 | 0.45, 0.46 | 1.07, 1.09 | -44, -42 dB |
| 50 | 0.41, 0.39 | 0.79, 0.84 | -35, -49 dB |
| 75 | 0.19, 0.20 | 0.27, 0.29 | -52, -42 dB |
| 90 | 0.10, 0.11 | 0.13, 0.18 | -46, -46 dB |
| 99 | 0.012, 0.008 | 0.019, 0.019 | -69, -82 dB |
| 100 | below 1e-8 | below 4e-8 | -96, -101 dB |

- At Macro 0 the DC gain equals the author's (their 0.286927 and -0.040452 for
  an input of 0.5 and -0.25 are 1.25 times mine).
- At Macro 100 % the steady wet is silent: -101 to -103 dB below the input
  over 6.5 s in two more realisations. Because nothing fluctuates at the
  output, the DC does not reach the randomly modulated part (INFERRED).
- Noise confirms a low cut. Wet spectrum of 50 s of white noise at Macro 100 %
  (3 realisations) relative to Macro 0: -26.7 dB at 1 to 3 Hz, -19.8 dB at 3 to
  6 Hz, -13.3 dB at 6 to 12 Hz, -7.5 dB at 12 to 25 Hz, -1.3 dB at 25 to 50 Hz,
  that is about 6 dB per octave below roughly 25 Hz. (The same comparison
  shows +2.2 and +4.0 dB at 0.8 to 3.2 kHz and -6.1 and -20.1 dB above 6.4 and
  12.8 kHz; that belongs to the Macro packets.)
- Corrected statement: "no DC blocker and no high-pass at HPF 20 Hz" is true
  at Macro 0. At the listening point the wet is high-passed and has no DC.

### C2. The level cycle at Macro 100 % (claim 14)

MEASURED on 7 captures of white noise of +-0.25 (level per second, dB):

- Noise from 1 to 51 s, warm-up 10 s, three realisations. Floor near -36.8 dB
  (mean of t = 1 to 3 s: -36.7, -36.7, -36.9), first maximum at t = 9 to 11 s (-31.9,
  -31.4, -31.3), minimum at t = 16 to 19 s (-36.9, -36.1, -36.9), second
  maximum at t = 26, 29, 27 to 29 s (-31.4, -31.3, -31.6), and so on to 51 s.
  Macro 0 with the same noise: -32.31 to -32.46 dB throughout.
- Same noise started at t = 9 s: the level is -34.2 and -33.5 dB at once,
  maximum at t = 12 to 13 s, minimum at t = 18 to 21 s (-36.9, -36.8). The
  pattern stays where it was in absolute time, it does not follow the onset.
- Warm-up 17 s instead of 10 s, noise from 1 s: maximum at t = 3 to 6 s (-31.5,
  -31.8), minimum at t = 11 to 13 s (-36.0, -36.7). The pattern moved 7 s
  earlier, so its clock starts with the instance, warm-up included.

Therefore: with T the time since the instance started, minima fall at T of
about 11 to 13 s and 26 to 31 s, maxima at about 19 to 23 s and 36 to 39 s; the
quasi-period is 16 to 19 s and the swing about 5.5 dB (-36.9 to -31.3 dB, that
is 4.5 dB below to 1.1 dB above the Macro 0 level). The realisations agree
during the first cycle and drift apart afterwards (third minimum at t = 32 to
36, 38, 37 s). The author's "arch common to all 8 realisations" is the first
cycle seen through an 18 s window; it is neither a single arch nor a start-up
effect of the stimulus.

Consequences. A level at Tide 100 % is comparable between captures only when
it is read in the same window of instance time, or over several cycles (60 s
or more). Within a 2.8 s window the 12 reference realisations still spread by
0.55 dB (floor with upward excursions of up to 1.9 dB), so short windows do
not help.

### C3. First wet sample and the early response against Macro (claim 14)

MEASURED, 1200-sample noise burst, first non-zero output sample counted from
the start of the burst, and the level of the 500 samples that follow the
Macro 0 start, relative to Macro 0 (two realisations):

| Macro % | first wet sample | early 500 samples re Macro 0 |
|---|---|---|
| 0 | 1279 | 0 dB |
| 25 | 1279, 1279 | -0.1, +3.4 dB |
| 50 | 1279, 1279 | -8.4, -3.1 dB |
| 75 | 1279, 1279 | -20.6, -23.0 dB |
| 90 | 1279, 1279 | -31.5, -53.3 dB |
| 97 | 1279, 1279 | -32.5, -85.4 dB |
| 99 | 1279, 1279 | -50.6, -57.0 dB |
| 99.9 | 1279, 1279 | -63.9, -70.9 dB |
| 100 | 1801, 1801 | nothing |

- The first wet sample is the same in every realisation (1801 in 12 of 12
  reference captures and in 21 captures at other Mix values), CONFIRMED.
- It is not a constant of the reference: it depends on when the input arrives,
  1787 (author, burst at 0.1 s), 1801 (burst at 0.2 s), 1828 (impulse at
  0.37 s); at Macro 0 the same three give 1265, 1279, 1305. The difference
  between Macro 100 % and Macro 0 is 522 to 523 samples in all three.
- There is no jump between 75 and 100 %. The early part is already 20 dB down
  at 75 %, 30 to 50 dB at 90 % and 50 to 70 dB at 99 to 99.9 %; it vanishes
  only at exactly 100 %. "First non-zero sample" must not be used as the start
  of the response for Macro between 0 and 100.

### C4. Bypass passes through the clipper (claim 2)

MEASURED with an input of peak 30: the bypassed output is bit-identical to the
Mix 0 capture of the same stimulus, peaks at 3.981072 and equals
`clip(input delayed by the latency)` at -145.8 dB. It is the bit-identical
delayed input only while the input stays below +8 dBFS. The bypassed output
at Macro 100 % is bit-identical to the same Mix 0 capture.

### C5. Pre-delay in single precision (claims 6 and 7)

FITTED on 110 bit comparisons (my 40, the author's 70). With `x` the normalised
parameter as a 32-bit float and every operation in single precision,

```
P       = 2000 * (exp(6 x) - 1) / (exp(6) - 1)        ms
D       = max(0, floor(P * fs / 1000) - 1)
```

reproduces all 110 measured delays. The double-precision law of the io report
misses 7 of the 110 (their five boundary cases and my 5 ms and 150 ms). Five
other single-precision forms miss 3 to 7. The form was selected on these
cases; it relies on NumPy's `float32` exponential agreeing with the
reference's on them.

Practical reading: a whole number of milliseconds at 48 kHz gives `48 P - 1` or
`48 P - 2` samples depending on rounding (5, 20, 30, 200, 250, 1000 ms: minus
1; 50, 75, 150, 500 ms: minus 2).

Also MEASURED: Predelay Synced at normalised 0 ("1/32") and 1 ("1/2") gives
output bit-identical to the default 0.5 with 46.3 ms of pre-delay. In a
headless instance that parameter is inert (the io report left it unstudied).

### C6. The ducking slope is exactly 5/7; the knee is 9.98306 dB (claim 9)

- Both packets read a static curve whose free fit has slope 0.714278
  (5/7 = 0.7142857) and a largest error of 5e-5 dB. With the slope fixed at 5/7
  the readings are off by 3e-5 dB at 2 to 4 dB of reduction, 6e-5 at 4 to 8,
  1.1e-4 at 8 to 16 and 2.3e-4 dB at 16 to 32: doubling per binade.
- That is the smoother, not the curve. A single-precision one-pole
  `g = G + k (g - G)` with k = exp(-1/240) stops 120 units in the last place
  short of its target: simulated 1.43e-5, 2.86e-5, 5.72e-5, 1.144e-4, 1.144e-4,
  2.289e-4 dB at 2, 4, 6, 12, 18, 24 dBFS; my readings at 6, 12 and 24 dBFS are
  -4.285657, -8.571314 and -17.142628 dB against simulated -4.285657,
  -8.571314, -17.142628 (MEASURED against simulation).
- Decisive test: the full model on a DC staircase from -5.6 to +30 dBFS nulls at
  -131.2 dB with slope 5/7 (-138.8 dB over the +6 to +24 dBFS steps) and at
  -98.9 dB with the "fitted" slope 0.7142784. The fitted slope and the fitted
  threshold of 6e-6 dBFS in `io.md` should not be quoted as measurements of the
  curve; the model constants 5/7 and 0 dBFS are right.
- Knee width, FITTED by scanning the full model on three events that live
  inside the knee. Noise of peak 1: -116.8 / -125.6 / -127.8 / -117.7 dB at
  9.9829 / 9.9830 / 9.9831 / 9.9832 dB; the other two events have the same
  optimum (-130.7 and -132.6 dB). Optimum **9.98306 +- 0.00005 dB**. The author's 9.983 is
  inside it. Rejected: 10 dB (-76.1 dB), 10 * 8.6859 / 8.7 = 9.98379 dB
  (-103.5 dB). 10 + 10 log10(255/256) = 9.98300 dB cannot be separated from the
  optimum and has nothing else to recommend it. Origin still OPEN.

## Evidence by section

### 1. Mix (claim 1)

MEASURED at Macro 0, Decay 2 s, Size 70 %, own probe (impulses of 0.4, -0.45,
(0.3, 0.45), 150 ms of noise of +-0.3, a 180 Hz burst), Mix 3, 7.7, 17, 37.5,
42, 49.5, 50.5, 58, 62.5, 73, 83, 91.7, 97, 99.5 %.

- Fitted (a, b) equal the law within 8e-10. Law null -151.0 dB worst.
- A linear crossfade nulls at -6.0 to -40.6 dB, equal power at -10.7 to -48.3 dB.
- Mix 0 is bit-identical to the input delayed by 48 samples; the same at 44.1,
  88.2 and 96 kHz (44, 88, 96 samples) and at 96 kHz with Macro 100 %.

### 2. Return and Master (claim 3)

MEASURED, same state and probe.

- Return -21.3, -9.7, +2.2, +15.5 dB at Mix 100 and 65 %: fitted wet gain
  within 1.5e-6 dB of the display, dry coefficient 0.70000005 at every value,
  law null -137.2 dB worst.
- Return +23.9 dB: -47.2 and -51.0 dB without the clipper, -138.0 and -137.7 dB
  with it. At this state the wet reaches +8 dBFS; this is the clipper of
  section 5, not a deviation of Return.
- Master +4.4, -2.2, -9, -30, -55, -69 dB at Mix 100 and 40 %: fitted gain
  within 2.6e-6 dB of the declared law, law null -130.3 dB worst, fitted null
  -148.5 dB. The gap is the same one the author reports.
- Master at normalised 0 gives exact silence at Macro 100 %, at Mix 100 and 50 %.

### 3. Width (claims 4 and 5)

MEASURED, 5, 33, 66.6, 85, 115, 133, 145, 149.9 %.

- The free 2x2 matrix from the Width 100 % output is symmetric within 1.1e-9;
  its mid and side gains equal the law within 5.2e-8. Law null -144.7 dB worst,
  including R-only input (66.6 and 145 %) and Mix 40 % (33 and 133 %).
- Not on the input: for R-only input, the best mix of the Width 100 % responses
  to the same waveform on the R and on the L input predicts the Width 66.6 and
  145 % captures at -11.9 and -10.8 dB; the output law gives -145.4 and -145.0 dB.

### 4. Pre-delay (claims 5, 6, 7)

MEASURED by bit comparison with a Pre-delay 0 capture whose stimulus is moved.

- 48 kHz, 1.5, 2.5, 7.3, 33.7, 77.2, 333.4, 2222.6, 7000.3, 20000.7, 60000.2
  samples: identical to the shift D of the law and to neither neighbour, 10 of 10.
- 44.1 kHz (5.5, 60.3, 700.6, 5000.4), 88.2 kHz (60.3, 3000.4), 96 kHz (5.5,
  60.3, 700.6, 9000.4): 10 of 10.
- Whole milliseconds at 48 kHz: 8 of 10 with the double-precision law, 10 of 10
  with C5.
- Output delayed instead of the input: +2.7 dB (2221 samples). Dry undelayed at
  Mix 50 %: -152.0 dB; with the dry delayed as well +2.4 dB.

### 5. Output clipper (claim 8)

MEASURED at Mix 0 on a stimulus the author did not use: 5000 samples per
channel of random sign and log-uniform magnitude from 0.1 to 30, a 30 Hz sine
of 9 on L against a 1234.5 Hz sine of 2.2 on R, ramps of 3e-6 per sample
across both corners.

- Law nulls -147.1, -147.9, -143.4 dB; largest error 8.2e-7; a linear path
  +8.3 to -10.1 dB. Independent samples of random size null as well as slow
  ones, so the stage has no memory.
- FITTED freely: T = 2.5118878 (+8.000005 dBFS), C = 3.9810717 (+12.0000000 dBFS).
- R at 2.2 passes bit-exact while L clips: per channel.
- Position, with Return +22 dB, Master +5 dB, Mix 70 %, Width 130 % (unclipped
  peak 5.80, 3593 samples above T): clipper last -140.4 dB; before Master
  -26.5; on the wet before the Mix -26.5; on the wet after Master -39.2; before
  Width -26.5; none -26.5 dB.
- The same law at 44.1 and 96 kHz: -147.1 dB. Mix 0 at Macro 100 % with this
  stimulus is bit-identical to Macro 0.

### 6. Ducking at 0 % (claims 9 and 10)

Design different from the author's (no pre-delay, no carrier held back). The
stimulus x is a noise bed of +-0.03 plus loud events; it is captured as it is
and scaled by 1/64, where every sample is below the knee. Since the network is
linear and repeatable at Macro 0, `64 capture(x / 64)` is the unreduced wet of
x and the claim reads `capture(x) = clip(g[n] * 64 * capture(x / 64))`, with
`g` my own implementation of the law in `io.md`.

MEASURED, Decay 1 s, nulls over each event and its recovery:

| Event | without model | model | knee 10 dB | linear-gain smoothing | key sqrt(L^2+R^2) | attack 4 ms | release 250 ms |
|---|---|---|---|---|---|---|---|
| noise of peak 2, L and R independent | -5.3 | -128.5 | -101.2 | -41.4 | -18.0 | -45.0 | -41.6 |
| noise of peak 1 (inside the knee) | -21.3 | -125.6 | -76.1 | -66.8 | -22.4 | -53.9 | -54.0 |
| L Gaussian up to 4, R quiet | -17.4 | -127.7 | -79.8 | -45.4 | -76.2 | -39.1 | -43.1 |
| L 137 Hz of 0.8, R 3.1 kHz of 1.5 | -10.2 | -130.7 | -80.8 | -49.5 | -25.8 | -50.1 | -45.4 |
| decaying bursts of 6, L = -R | -0.7 | -123.7 | -100.3 | -21.5 | -15.2 | -30.1 | -29.3 |
| impulses 3 (L), 20 (R), 1.2 | -35.8 | -138.9 | -127.2 | -43.1 | -53.4 | -51.5 | -47.2 |
| DC of 2 on L only | -3.9 | -139.7 | -139.7 | -44.1 | -91.9 | -68.3 | -45.0 |

- Other keys: (abs L + abs R)/2 gives -6.5 to -54.9 dB, abs(L + R)/2 gives -0.7
  to -40.4 dB, abs L alone -11.5 dB on the fourth event.
- Timing: the gain one sample early or late gives -76.9 to -102.9 dB, so the
  alignment with the dry (x[n] acts on output sample n + latency) is exact.
- The smoother in double precision gives -102.3 to -120.2 dB; single precision
  is needed, as the author says.
- Attack coefficient read from the first 20 ms of the DC event: 0.99584197,
  that is 4.99997 ms. Recovery slope: 300.01 ms.
- 44.1 kHz: -137.6 dB (latency 44). 96 kHz: -136.4 dB (latency 96).
- The key is the raw input. With 50.3 ms of pre-delay: -138.9 dB, against
  -28.9 dB if the key were the pre-delayed input. With the input filter closed
  to 300 Hz .. 5 kHz (which changes the wet completely, -0.1 dB of null against
  the open filter): -133.3 dB with the unfiltered key, DC event included.
- Static curve on a clean DC staircase (57 levels from -5.6 to +5.6 dBFS in
  0.2 dB steps, then 2 dB steps to +24 dBFS): inside the knee the law with
  W = 9.983 holds within 2.9e-5 dB; above it see C6. Steps at +26 dBFS and more
  are excluded because their wet reaches the output clipper (the readings fall
  0.014 and 0.059 dB below the law at +28 and +30 dBFS; the model with the
  clipper explains them, staircase null -131.2 dB).
- Onset by bit comparison of `capture(x)` with `64 capture(x / 64)` on DC steps:
  identical at 0.5600, 0.5620, 0.5626 and 0.5629; different at 0.5632 (gain
  -8.0e-7 dB, law -8.1e-7 dB), 0.5636, 0.5645 (-2.24e-5, law -2.20e-5), 0.5660,
  0.5700 (-4.25e-4, law -4.25e-4). The law's onset is 0.56289.
- Dry not ducked: Mix 0 with the event stimulus equals `clip(delayed input)` at
  -148.4 dB (the impulse of 20 clips).

### 7. Complete shell (claim 11)

MEASURED with my own implementation of the model in `io.md`.

- H1, quiet probe, Decay 2 s, Size 70 %: Pre-delay 37.77 ms (D = 1811), Width
  62 %, Return +7.7 dB, Master -8.3 dB, Mix 81 %: **-132.5 dB**.
- H2, loud events, Decay 1 s, Size 130 %: Pre-delay 12.3 ms (D = 589), Width
  140 %, Return +20 dB, Master +5 dB, Mix 45 %. Ducking reaches 4.4 dB and
  47 025 samples lie above the clipper threshold: **-132.9 dB** (worst of five
  windows -131.6 dB). Without ducking -11.8 dB; without clipper +6.5 dB; dry
  ducked as well -16.3 dB; ducking keyed by the pre-delayed input -46.8 dB;
  clipper on the wet only -6.5 dB; ducking applied after the clipper -15.2 dB.
- H4, the controls of H1 with Pre-delay 9.37 ms at 44.1 kHz (D = 412) and
  96 kHz (D = 898): -132.4 and -132.6 dB. Width, Return and Master are
  therefore the same at these rates (the author measured them at 48 kHz only).
- My nulls stop near -132 dB where the author's reach -141 dB because Master
  is off 0 dB in all of them (the -130 dB limit of section 2).

### 8. The listening point, Macro 100 % (claims 12, 13)

Stimulus: a 1200-sample burst at 0.2 s, then noise of +-0.25 from 1 to 4 s;
Decay 0.5 s; 12 reference realisations (Mix 100 %), 6 per moved control. Two
reference realisations null against each other at -0.1 dB.

Exact parts, MEASURED:

- Dry gain in the burst window (the wet starts 1801 samples after the burst):
  1, 1, 1, 0.75, 0.254, 0.02 at Mix 10, 12.5, 37.5, 62.5, 87.3, 99 %, law null
  -146.9 dB or better in all 21 captures; with Master +3 dB at Mix 62.5 %
  1.0594 (law 1.059403), -132.9 dB. Mix 0 is bit-identical to the delayed input.
- The first wet sample is 1801 at every Mix (21 captures): Mix does not move
  the wet.
- Width 0 %: L and R bit-identical in 6 of 6 realisations.
- Pre-delay 333.4 and 7000.3 samples: the first wet sample (2134 and 8824, both
  realisations) equals that of Pre-delay 0 with the input moved by D = 332 and
  6999, and differs for D - 1 and D + 1. For 6998, 6999, 7000 the first sample
  is 8822, 8824, 8825: it does not move one for one, so it is valid only in
  this comparison, as the author used it. With Pre-delay at Mix 50 % the dry
  burst is bit-identical to the undelayed input.

Balance, MEASURED: side level minus mid level against the reference mean. Width
33 %: -11.743 to -11.637 dB over six realisations (law -11.696). Width 133 %:
+3.913 to +3.947 dB (law +3.961). Mean deviations +0.003 and -0.033 dB,
largest single one 0.059 dB; the reference balance itself spreads by 0.035 dB
in this 2.8 s window. The author's "within 0.03 dB" is right for 18 s windows
and slightly optimistic for short ones.

Wet level, MEASURED. Lowest 0.5 s level between 1.25 and 4.0 s, a statistic
chosen on the 12 reference captures alone because it spreads less (0.34 dB)
than the mean level (0.55 dB); mean of 6 realisations minus reference mean:

| Setting | measured dB | law dB | standard error dB |
|---|---|---|---|
| Mix 12.5 % | -12.04 | -12.04 | 0.18 |
| Mix 37.5 % | -2.56 | -2.50 | 0.15 |
| Mix 62.5 % | +0.45 | 0 | 0.31 |
| Return -9.7 dB | -9.60 | -9.70 | 0.23 |
| Return +15.5 dB | +15.30 | +15.50 | 0.14 |
| Master -9 dB | -8.82 | -9.00 | 0.31 |
| Master +4.4 dB | +4.38 | +4.40 | 0.18 |
| Width 0 %, mid | +3.27 | +3.01 | 0.28 |
| Width 33 %, mid | +2.43 | +2.01 | 0.29 |
| Width 133 %, mid | -0.70 | -1.10 | 0.41 |
| Master +3 dB at Mix 62.5 % (2 realisations) | +2.75 | +3.00 | 0.10 |

Mean deviation +0.12 dB, spread 0.25 dB, largest 0.45 dB. With the plain mean
level the same captures give +0.24 dB, 0.35 dB and 0.73 dB (single captures up
to 1.9 dB off). No law is contradicted; none is confirmed better than about
+-0.3 dB at Macro 100 %. This agrees with the author's limit.

Ducking at Macro 100 %, MEASURED (not in the io report). Noise carrier of
+-0.2 with 2 s of pre-delay, DC of +12 dBFS on both inputs for 0.4 s at 4.5, 8.0
and 11.5 s, four realisations with the events and four with the carrier only;
level change from the 0.33 s before each event:

| Window | measured dB | standard error | model dB |
|---|---|---|---|
| 0.10 to 0.39 s into the event | -8.86 | 0.19 | -8.57 |
| 0.10 to 0.30 s after its end | -4.53 | 0.24 | -4.40 |
| 0.9 to 1.3 s after its end | -0.34 | 0.50 | -0.24 |

In 5 ms steps from the event's arrival the level falls by 2.2, 6.7, 7.7, 8.2,
8.3 dB (model 3.4, 6.7, 7.9, 8.3, 8.5 dB). The reduction comes with the event
and not 2 s later, so the key is the raw input at Macro 100 % too.

## What I could not establish

- No sample-exact check of any wet gain at Macro 100 %. Level statistics stop
  at about +-0.3 dB with 6 realisations; ten times more captures would be
  needed for +-0.1 dB.
- The clipper's shape on the wet at Macro 100 %: with Return +24 dB and Master
  +6 dB my stimulus reached only 2.81 (10 samples above the threshold). At
  Macro 100 % the clipper is verified on the dry only (bit-identical to Macro 0).
- The Ducking static curve at Macro 100 % at more than one level; the knee
  cannot be resolved statistically there.
- Origin of the knee width 9.98306 dB.
- Whether the single-precision form of C5 holds beyond the 110 tested cases.
- Outer laws at Decay above 2 s or with Brightness and Transients off neutral
  (Decay 0.5, 1, 2 s, Size 70, 100, 130 % and one closed input filter are covered).
- Ducking above 0 % and Predelay Synced with the sync switch on were not
  studied (outside the io packet; the switch is not a host parameter).
- Period, depth law and cause of the level cycle and of the low cut at Macro
  100 % are described here only as far as they affect level readings.

## Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Results/RevOceanCharacterization/work/io_verification
$PY v1_linear.py         # Mix, Return, Master, Width at Macro 0
$PY v2_predelay.py       # Pre-delay bit comparisons, Predelay Synced
$PY v2b_predelay_rounding.py
$PY v3_clipper.py        # output clipper, bypass
$PY v4_ducking.py; $PY v4b_static.py; $PY v4d_slope.py; $PY v4e_stall.py
$PY v5_shell.py          # complete shell, loud and quiet, other rates
$PY v6_macro100.py; $PY v6b_floor.py; $PY v9_macro100_more.py
$PY v7_misc.py; $PY v8_extra.py; $PY e2_arch.py; $PY v10_summary.py
$PY reproduce_count.py   # the author's measure_io.main() against the published io.json (writes to this folder only)
```

Each script writes a JSON of the same name next to it. All captures are cached.
