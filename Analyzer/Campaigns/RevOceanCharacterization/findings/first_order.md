# first_order: the campaign's first-pass model (audit and integration of packets a, b, c)

Packet `first_order`, 6 October 2026. Model: `first_order_model.py`. Constants,
scores and checks: `tide_structural_data/first_order.json`, regenerated bit for
bit by `fit_first_order.py` (36 s once the captures are cached).

Tags: MEASURED (read off captures), FITTED (a model constant, residual given),
INFERRED (reasoned, not tested), OPEN. A null is
`20 log10(rms(model - reference) / rms(reference))` with nothing fitted.

## 0. Operating point, and the owner's question (Tide 100 %, Mix 100 %?)

- **Mix: yes, 100 % in every capture** (plug-in read-back `Mix = 100`).
- **Tide (the Macro knob): no. The model, its constants and all scores below
  are for Macro 0 %** (read-back `Macro = 0.000`), Size 100 %, Decay 0.5 s,
  Width 100 %. That is what the packet and the locked holdout prescribe: only
  at Macro 0 is the reference repeatable, so only there can a model be nulled
  sample for sample.
- **What this means at Tide 100 %, Mix 100 %** (section 8, MEASURED on two
  instances, read-back `Macro = 100`, `Mix = 100`): the network of this model
  is still there, unchanged, behind the Tide input stage. Fed with an impulse
  at the first exit of the Tide comb delay (packet `tide`), the model explains
  the first arrivals at -21.5 and -22.0 dB (median of 120 impulses, one free
  65-tap filter per impulse for the Tide voice filter). Evaluated at the
  impulse time instead it reaches -4.3 and -3.3 dB, with its oscillators one
  second late -0.2 dB. So line lengths, modulation law, start phases and time
  origin carry over to Tide 100 %. The model is **not** a model of the Tide
  100 % sound: the comb delay, the voice filters and their slow cycle belong to
  packet `tide` and are not part of `predict`.

## 1. Result

| data (raw 48 kHz samples 1200..2500, no gain, delay or polarity fit) | null |
| --- | --- |
| **locked holdout**, `score_first_order.py first_order_model.py` | **-73.92 dB** (L->L -73.80, L->R -74.76, R->L -74.83, R->R -73.71; worst response -66.88) |
| fresh train of this packet, impulses 1.0 to 1.3 s apart (seed 5201, 86 times x 2 inputs) | **-80.07 dB** (paths -80.16, -79.83, -80.14, -80.02; responses -82.4 best, -80.1 median, -78.0 worst) |
| fresh train with the holdout's spacing, 0.5 to 0.75 s (seed 5202, 157 times x 2 inputs) | -74.56 dB (first response, no earlier impulse: -79.96 dB) |
| capture with a 4.321 s warm-up (seed 5301) | -80.15 dB (+2.96 dB with the 10 s origin) |
| 48 kHz trains of packet c (seeds 2001, 2002; nothing is fitted on them) | -80.03 dB |
| 44.1 kHz host, network alone, train not used in the fit (seed 5102) | -80.12 dB (fit train -80.27 dB) |

The holdout score is set by the reference, not by the model: the holdout's
impulses are 0.5 to 0.75 s apart and each response sits on the tail of the
previous one. MEASURED on the fresh train with the same spacing:

| gap to the previous impulse | responses | model null | energy density before the first arrival, raw [200, 1100), re the scored window |
| --- | --- | --- | --- |
| 0.50 to 0.55 s | 28 | -69.5 dB | -66.8 dB |
| 0.55 to 0.60 s | 32 | -74.8 dB | -73.3 dB |
| 0.60 to 0.65 s | 44 | -78.1 dB | -79.2 dB |
| 0.65 to 0.70 s | 23 | -79.5 dB | -85.4 dB |
| 0.70 to 0.75 s | 29 | -79.9 dB | -92.0 dB |

A first-pass model cannot remove that tail. On responses that are clean the
model stands at -80 dB at both host rates.

The scorer was run four times, always on the same constants: once when the
model was complete (-73.92 dB) and three times after evidence fields and
comments were added to the files (-73.92 dB each). Nothing was chosen on the holdout. No
holdout audio was read outside the scorer; its list of impulse times was
computed once to confirm that none of the 48 kHz trains used here shares a
time with it (0 of 157).

At 48 kHz the model has **no fitted constant of its own**: everything fitted
is measured with the host at 44.1 kHz, where the network is seen without rate
conversion; the converters are fixed by round values.

## 2. Audit of the three first-pass models

| | first_order_a | first_order_b | first_order_c |
| --- | --- | --- | --- |
| holdout score reported | -63.05 dB | -68.71 dB | -70.03 dB |
| holdout score re-run here | -63.05 dB | -68.71 dB | -70.03 dB |
| fit script reads the holdout | no | no | no |
| impulse times of own captures | seeds 7741+ | seeds 7100+ | seeds 1001, 2001 to 2004 |
| `predict` | structural | structural | structural |

- MEASURED by reading the code: none of `fit_first_order_{a,b,c}.py` imports
  `datasets.holdout_first_order`, `holdout_times` or the holdout seed, and none
  opens a cache entry by path; each reads only `datasets.grid_responses` and
  its own captures through `revocean.capture`.
- All three `predict` functions build the response from lines, oscillators,
  gains and time-invariant kernels; none looks up a measured response by
  impulse time. The kernels are measured, not parametric: a (89 taps before
  the lines, 1049 after), b (33 before, 300 taps plus six exponential tails
  after), c (960 taps after the lines, parametric converters).
- The fit scripts were read, not re-run.

## 3. Where the three agree, where they differ, and how it was resolved

Established by all three (and confirmed here by the final nulls): the network
runs at 44.1 kHz whatever the host rate (README item 7); lines of prime length
in two independent groups, one per output; linear interpolation with the length
of the sample being read; 0.88 ms of modulation; a single-precision phase
accumulator per line at a nominal 0.6 Hz; oscillators that count from the start
of processing; cross feed 0.5723 on lines 1 to 6, none on line 7; gain
proportional to `10^(-3 prime / (44100 x 0.5 s))`; second passes inside the
scored window with a feedback magnitude near 0.248 and no exchange between the
groups.

Differences, each settled by a test of this packet:

| question | a | b | c | resolution (this packet) |
| --- | --- | --- | --- | --- |
| lines per group | at least 11 | 8 | 8 | **16** (section 4.2). 8 is what the scored window shows. MEASURED for 15, weaker for the 16th |
| start phases | progression, step -1.3163744 rad, no closed form | the same, -0.2095074 cycle | 16 fitted values, rule OPEN | **11.25 k rad**, not wrapped at the start (section 4.3). MEASURED: -80.1 dB against -66.9 dB (c's linear law) and -62.6 dB (a's and b's step) |
| what limits the network at 44.1 kHz | not seen | not seen | "-71 dB floor of the core", cause OPEN | the tail of the previous impulse in c's train (0.5 to 0.6 s apart). c's own constants score -79.5 dB on a train with 1 s gaps. MEASURED |
| converters | ideal conversion, fitted kernels around it | constructed zero-phase pre-kernel | two equal Kaiser sincs, fitted delays and gain 1.0003 | c's kernel, read from a **table without interpolation**, on **lattice delays**, gain 1 (section 4.1). MEASURED: -71.6 -> -80.0 dB |
| converter gain 1.0003 | - | - | OPEN | it is the table truncation; with it the least-squares gain is 0.9999998 |
| fixed delay | 187 internal samples | 187 | 203.27892 host samples (free fit) | `29882/147` = 203.27891 host samples, exact on the lattice |
| kernel in the feedback path | none needed | 12 taps | 40 taps | needed: -67.2 dB without it against -80.0 dB (40 taps kept) |
| fixed filter | pre and post kernel, split regularised | 20 kHz Q 1 low-pass plus free kernel | 960 taps behind the lines | c's form; b's low-pass confirmed on it (section 4.6) |
| cross feed | `(1-w)/(1+w)`, w = 0.272, then 1 - 0.0809 (n-7) | measured ratios | measured ratios | a's law, in the exact form `w = 1 - (n-7) 0.728/9` (section 4.4) |

What each packet gave the final model: c the 44.1 kHz host as a microscope, the
split into network and converters and the Kaiser constants; b the placement
argument (Doppler) and the 20 kHz low-pass; a the lines beyond the eighth, the
width law and the float32 accumulator (found by all three independently).

## 4. The model

Times: `n`, `N` host samples and `m` internal (44.1 kHz) samples, all counted
from the first processed sample, warm-up included. Host and internal samples
meet on a lattice of 1/160 internal sample (host sample = 147 steps, internal
sample = 160 steps).

```
u[m]      = (147/160) * K( 160 m - 147 n - 4250 )                unit impulse at host sample n, input converter
w[m + 44] = u[m]                                                 44 internal samples, then written into all lines
L_k[m]    = fl32( P_k + 38.808 * sin(theta_k[m]) )               length of line k when sample m is read
r_k[m]    = (1 - f) * w[m - i] + f * w[m - i - 1]                i = floor(L_k[m]),  f = L_k[m] - i
e_g[m]    = sum_l G[a,g,l] * r_l[m]  +  sum_(i,j) S[a,g,i,j] * (loop * read_j(r_i))[m]
y_g[m]    = (F * e_g)[m]                                         fixed filter, outside the loop
out_g[N]  = sum_m K( 147 N - 18592 - 160 m ) * y_g[m]            output converter;  raw index = N - n
```

`a` input channel, `g` output channel = group, `k = 2 (line - 1) + g`.

### 4.1 Time base and converters

- FITTED as round values, nulls in section 5. `K(q)` is the converter kernel at
  a distance of `q/160` internal samples (output time minus input time):
  `K(q) = table[ floor(4096 |q| / 160) ]`,
  `table[i] = fl32( 0.9 sinc(0.9 i/4096) * I0(6 sqrt(1 - (i/69631)^2)) / I0(6) )`, `i < 17 x 4096`.
  A Kaiser-windowed sinc, cut-off 0.9 of the internal Nyquist frequency, 17
  internal samples per wing, beta 6, 4096 entries per internal sample, **no
  interpolation between entries**. Both converters use it; the input converter
  scales by 147/160.
- MEASURED. When `4096 |q| / 160` is a whole number (q a multiple of 5, every
  fifth impulse time and every fifth output sample) the reference uses the
  entry below on one wing: inputs before the output sample (`q > 0`) in the
  input converter, inputs after it (`q < 0`) in the output converter. With the
  mathematically exact entry those impulses score -70.9 dB and those output
  samples -71.7 dB; with the rule both score -80 dB like the rest.
  INFERRED: the converter clocks are kept in floating point and sit a hair low
  (input) and a hair high (output) at those instants. A simple double-precision
  simulation of such a clock did not give a stable sign (section 9).
- MEASURED. Lattice delays: a host impulse at `n` is centred at internal time
  `0.91875 n + 26.5625` (4250 lattice steps); host output sample `N` reads
  internal time `0.91875 N - 116.2` (18592 steps; 126 + 70/147 host samples).
  Moving either by one lattice step gives -41.3 dB. Equivalent readings:
  internal sample -1 sits at host time -30, and host output sample 96 reads
  internal time -28.
- Consequence: a line of length `L` arrives at raw sample
  `29882/147 + (160/147) L = 203.2789 + 1.08844 L` at 48 kHz, and at `88 + L`
  at 44.1 kHz (44 samples before the lines, 44 after; MEASURED by packet c).
  Only sums are measured: how the 26.5625 + 44 internal samples before the
  write divide inside the plug-in is OPEN.
- INFERRED: the constants (roll-off 0.9, beta 6, 17 zero crossings per wing,
  4096 table entries per crossing, truncating lookup, offsets 30 and 28) are
  those of the classic public-domain Kaiser-window resampler design in its
  high-quality setting, as far as I recall its defaults. Nothing was checked
  against any source code.

### 4.2 Lines and the read rule

- MEASURED. Sixteen lines per group, prime lengths in internal samples:
  left 1031, 1097, 1187, 1289, 1423, 1583, 1783, 2027, 2333, 2699, 3163, 3719,
  4409, 5261, 6299, 7589; right 1039, 1109, 1193, 1301, 1429, 1597, 1787, 2039,
  2333, 2707, 3163, 3719, 4409, 5261, 6299, 7589.
  Evidence for lines 9 to 16 (`evidence.lineSearch`): for accumulator `k` the
  own-input response of 174 impulses (44.1 kHz, 1 s gaps) is projected on the
  two interpolation samples of every whole centre from 2100 to 12000. Lines 9
  to 13 stand 4.7 to 9.6 times above the next candidate, lines 14 and 15 2.3
  to 3.1 times, line 16 (7589, coefficient 0.0107 and 0.0116) 2.0 times in
  both groups independently. Accumulators 32 to 39 (lines 17 to 20, were they
  to exist) show nothing above 0.007. A seventeenth line weaker than that is
  not excluded by this test; the phase step 11.25 = 360/32 and the width law
  (section 4.4), which closes at line 16, argue for 16. INFERRED.
- FITTED. Depth 0.88 ms = 38.808 internal samples on every line
  (0.8803 ms: -36.7 dB).
- MEASURED. Read rule: linear interpolation at the length of the sample being
  read (`r_k[m]` above), and **the length is a single-precision number**. On
  isolated arrivals at 44.1 kHz the length used for the first interpolation
  sample lies on the 2^-13 grid of float32 between 1024 and 2048: distance to
  the grid 0.015 to 0.047 of a step rms on 8 lines (0.289 for no grid), and it
  is the ideal length rounded to nearest (difference within +-0.5 step, rms
  0.29). Null of the network: -78.97 dB with a double-precision length,
  -80.12 dB with the rounded one. Which float32 expression is used (product
  and sum rounded separately or once) is not resolved (-80.08 against
  -80.22 dB before the refit).

### 4.3 Oscillators and their time origin

```
theta_k[0]   = 11.25 * k   rad                      k = 0 (L1), 1 (R1), 2 (L2), ... 31 (R16); not wrapped
every internal sample:   if theta >= fl32(2 pi): theta -= fl32(2 pi);   use theta;   theta += fl32(2 pi 0.6 / 44100)
```

all in single precision. `theta_k[m]` in section 4 is the value used at
internal sample `m`.

- MEASURED. The sixteen start phases fitted by packet c equal this rule within
  9.5e-7 rad (rms 5.1e-7, one to two float32 steps); the linear law
  `k x 4.966812 rad` misses them by up to 2.1e-5 rad. Because the phase starts
  as large as 348.75 rad and comes down by one 2 pi per sample, its first
  additions are rounded on a coarse grid; that rounding is the deviation packet
  c could not explain and the reason a and b found "no closed form" for their
  step (11.25 - 2 pi = 4.9668147 rad, their mean step 4.966811).
- MEASURED, nulls of the 44.1 kHz network on the unseen train, constants
  unchanged: this rule -80.12 dB; add first, then wrap test -74.20 dB; start
  reduced modulo 2 pi before the first step -59.7 to -60.8 dB; c's linear law
  -66.92 dB; a's and b's step -62.60 dB; step 11.2499 rad -28.4 dB (48 kHz).
- INFERRED: 11.25 is 360/32, a spread of the 32 oscillators in degrees that is
  used as radians.
- FITTED. Rate: nominal 0.6 Hz. The measured 0.599878 Hz is the rounding of
  the accumulator (the same accumulator with a nominal 0.599878 Hz: +2.6 dB).
- MEASURED. **Time origin: the first processed sample**, warm-up included,
  with the delays of section 4.1. A capture with a 4.321 s warm-up (207408
  samples, not a multiple of the 160-sample conversion period) nulls at
  -80.15 dB when the model's clocks start 4.321 s before the stimulus and at
  +2.96 dB when they are left at 10 s. One internal step early or late:
  -48.0 and -47.9 dB. Line L1 starts at exactly 0. Host block boundaries play
  no part (README item 2).
- Consequence for the product: the modulation phase is defined by the sample
  count since reset, per line, and by nothing else.

### 4.4 First-pass gains

`G[a,g,l] = c_l * 10^(-3 P_(g,l) / (44100 * 0.5 s)) * (1 if a = g else (1 - w_l)/(1 + w_l))`

- FITTED on the waveforms (44.1 kHz): `c_1..8` = 0.060370, 0.071299, 0.070132,
  0.066975, 0.064208, 0.062661, 0.099269, 0.101657, one value per line index
  for both groups (largest difference between the groups 6e-6 relative). Unit:
  amplitude of the pair of interpolation samples in front of the fixed filter
  for a unit impulse; the fixed filter has first tap 1 and gain 1.1421 at
  1 kHz.
- FITTED by projection, lower precision: `c_9..16` = 0.1158, 0.0921, 0.0760,
  0.0640, 0.0534, 0.0416, 0.0276, 0.0103 (the two groups differ by up to 3 %
  on lines 9 to 15 and by 8 % on line 16). They lie outside the scored window.
- MEASURED. Width law: `w` = 0.272 on lines 1 to 6 and `1 - (l - 7) 0.728/9`
  on lines 7 to 16 (1, 0.9191, 0.8382, ... 0.272). Measured cross/own ratios:
  lines 1 to 6 0.5723271 on all twelve (spread 1e-7; law 0.5723270), line 7
  below 3e-7 (law 0), line 8 0.0421493 and 0.0421490 (law 0.0421491). Lines 9
  to 16 by projection: 0.082/0.091, 0.140/0.142, 0.197/0.195, 0.255/0.259,
  0.313/0.313, 0.391/0.394, 0.475/0.473, 0.617/0.587 against the law's 0.088,
  0.138, 0.193, 0.253, 0.320, 0.395, 0.478, 0.572. Round-value tests: w = 0.27
  on lines 1 to 6 -54.8 dB; ratio 1/sqrt(3) -48.6 dB. Only Width 100 % was
  measured; how `w` follows the Width control is OPEN (packet `io` has the
  outer Width law).

### 4.5 Second passes in the scored window

The scored window is not first-pass only (the scorer's docstring is wrong on
this point): six second passes per group arrive from raw sample 2350 on. A
second pass `i -> j` is line `i`'s output read again by line `j` of the same
group, with no extra sample in between.

`S[a,g,i,j] = sigma_ij * M * G[a,g,i] * 10^(-3 P_(g,j) / 22050) * o_j / o_i`

- FITTED: `M` = 0.247772 (eight estimates from 1->1, 2->2, 1<->2, 1<->3 of
  both groups: 0.247763 to 0.247781); output weights `o_1..3` = 1, 1.377953,
  1.517010; signs `sigma`: 1->1 +, 1->2 +, 2->1 -, 2->2 +, 1->3 +, 3->1 -.
  Twelve fitted pair gains are replaced by these three constants and the signs
  at no cost (-80.27 dB with free gains, -80.27 dB with the constants).
  Round-value test: `M = 1/4` -66.2 dB; output weights 1 -41.9 dB; sign of
  2->1 flipped -57.2 dB; no second passes -25.4 dB.
- FITTED: the fed-back signal passes a loop kernel the first pass does not
  see: 1, -0.00012, -0.00405, -0.00190, -0.00302, -0.00182, -0.00207, -0.00142,
  -0.00141, -0.00106, ... (40 taps), sum 0.9770, alternating sum 0.9952.
  Without it -67.2 dB.
- The cross feed of a second pass is that of its first line (measured ratios
  0.572325 to 0.572350), so the cross feed sits at the input of the lines.
- INFERRED: a 16 x 16 feedback matrix with entries of one magnitude close to
  1/4. Only 6 of 256 entries per group are measured. The matrix, the output
  weights of lines 4 to 16 and all later passes are outside a first-pass model.

### 4.6 Fixed filter

FITTED: 960 taps at 44.1 kHz behind the lines (`fixed_filter` in the data
file), first taps 1, 0.30947, -0.30432, 0.25161, -0.19385, 0.14969, -0.08545.

- MEASURED on those taps: it is packet b's low-pass, a bilinear two-pole
  low-pass (audio-EQ form) at 20 kHz with Q 1, times a low-frequency
  correction. Filter over low-pass, re 1 kHz: +0.30 dB at 0 Hz, +0.50 dB at
  60 Hz, +0.76 dB at 200 Hz, +0.41 dB at 500 Hz, 0 at 1 kHz, -0.10 dB at 3 kHz,
  +0.13 dB at 6 kHz, +0.24 dB at 10 kHz and +0.29 to +0.30 dB from 15 kHz to
  22 kHz, where its phase is below 0.01 rad.
- No tail of a 20 Hz high-pass (tap 300: -2e-5, tap 600: 0).
- INFERRED: the low-pass is the LPF control at its top value. OPEN: sections
  for the low-frequency part and for the loop kernel.

## 5. Table of lines

Depth 38.808 internal samples (0.88 ms) and nominal rate 0.6 Hz on every line.
Gains are `G` of section 4.4 (in front of the fixed filter; multiply by 1.1421
for the gain at 1 kHz). Lines 9 to 16 carry the projected coefficients.

| line | k | length, samples | centre, ms | start phase, rad | the same modulo 2 pi | c | w | own input -> own output | other input -> this output |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| L1 | 0 | 1031 | 23.379 | 0 | 0.0000 | 0.06037 | 0.2720 | 0.04371 | 0.02501 |
| L2 | 2 | 1097 | 24.875 | 22.5 | 3.6504 | 0.07130 | 0.2720 | 0.05056 | 0.02894 |
| L3 | 4 | 1187 | 26.916 | 45 | 1.0177 | 0.07013 | 0.2720 | 0.04835 | 0.02767 |
| L4 | 6 | 1289 | 29.229 | 67.5 | 4.6681 | 0.06697 | 0.2720 | 0.04472 | 0.02560 |
| L5 | 8 | 1423 | 32.268 | 90 | 2.0354 | 0.06421 | 0.2720 | 0.04111 | 0.02353 |
| L6 | 10 | 1583 | 35.896 | 112.5 | 5.6858 | 0.06266 | 0.2720 | 0.03816 | 0.02184 |
| L7 | 12 | 1783 | 40.431 | 135 | 3.0531 | 0.09927 | 1.0000 | 0.05678 | 0.00000 |
| L8 | 14 | 2027 | 45.964 | 157.5 | 0.4204 | 0.10166 | 0.9191 | 0.05387 | 0.00227 |
| L9 | 16 | 2333 | 52.902 | 180 | 4.0708 | 0.1158 | 0.8382 | 0.05574 | 0.00491 |
| L10 | 18 | 2699 | 61.202 | 202.5 | 1.4381 | 0.0921 | 0.7573 | 0.03952 | 0.00546 |
| L11 | 20 | 3163 | 71.723 | 225 | 5.0885 | 0.0760 | 0.6764 | 0.02822 | 0.00545 |
| L12 | 22 | 3719 | 84.331 | 247.5 | 2.4558 | 0.0640 | 0.5956 | 0.01995 | 0.00506 |
| L13 | 24 | 4409 | 99.977 | 270 | 6.1062 | 0.0534 | 0.5147 | 0.01342 | 0.00430 |
| L14 | 26 | 5261 | 119.297 | 292.5 | 3.4735 | 0.0416 | 0.4338 | 0.00799 | 0.00316 |
| L15 | 28 | 6299 | 142.834 | 315 | 0.8407 | 0.0276 | 0.3529 | 0.00383 | 0.00183 |
| L16 | 30 | 7589 | 172.086 | 337.5 | 4.4912 | 0.0103 | 0.2720 | 0.00096 | 0.00055 |
| R1 | 1 | 1039 | 23.560 | 11.25 | 4.9668 | 0.06037 | 0.2720 | 0.04360 | 0.02495 |
| R2 | 3 | 1109 | 25.147 | 33.75 | 2.3341 | 0.07130 | 0.2720 | 0.05037 | 0.02883 |
| R3 | 5 | 1193 | 27.052 | 56.25 | 5.9845 | 0.07013 | 0.2720 | 0.04826 | 0.02762 |
| R4 | 7 | 1301 | 29.501 | 78.75 | 3.3518 | 0.06697 | 0.2720 | 0.04456 | 0.02550 |
| R5 | 9 | 1429 | 32.404 | 101.25 | 0.7190 | 0.06421 | 0.2720 | 0.04104 | 0.02349 |
| R6 | 11 | 1597 | 36.213 | 123.75 | 4.3695 | 0.06266 | 0.2720 | 0.03799 | 0.02175 |
| R7 | 13 | 1787 | 40.522 | 146.25 | 1.7367 | 0.09927 | 1.0000 | 0.05671 | 0.00000 |
| R8 | 15 | 2039 | 46.236 | 168.75 | 5.3872 | 0.10166 | 0.9191 | 0.05367 | 0.00226 |
| R9 | 17 | 2333 | 52.902 | 191.25 | 2.7544 | 0.1158 | 0.8382 | 0.05574 | 0.00491 |
| R10 | 19 | 2707 | 61.383 | 213.75 | 0.1217 | 0.0921 | 0.7573 | 0.03942 | 0.00544 |
| R11 | 21 | 3163 | 71.723 | 236.25 | 3.7721 | 0.0760 | 0.6764 | 0.02822 | 0.00545 |
| R12 | 23 | 3719 | 84.331 | 258.75 | 1.1394 | 0.0640 | 0.5956 | 0.01995 | 0.00506 |
| R13 | 25 | 4409 | 99.977 | 281.25 | 4.7898 | 0.0534 | 0.5147 | 0.01342 | 0.00430 |
| R14 | 27 | 5261 | 119.297 | 303.75 | 2.1571 | 0.0416 | 0.4338 | 0.00799 | 0.00316 |
| R15 | 29 | 6299 | 142.834 | 326.25 | 5.8075 | 0.0276 | 0.3529 | 0.00383 | 0.00183 |
| R16 | 31 | 7589 | 172.086 | 348.75 | 3.1748 | 0.0103 | 0.2720 | 0.00096 | 0.00055 |

The phase modulo 2 pi is only a reading aid: the accumulator starts at the
unwrapped value. Left group = left output, right group = right output; the
"other input" column is R -> L for the left group and L -> R for the right.

## 6. Round values and ablations

One constant changed at a time, null on a 48 kHz train that no fit uses
(packet c's seed 2003; `ablations48kDb`). The model itself: -80.00 dB.

| change | null |
| --- | --- |
| table of 2048 / 8192 entries per sample | -67.1 / -72.5 dB |
| table of 262144 entries (as good as the exact kernel) | -68.1 dB |
| exact entries not lowered / lowered on the other wing | -74.7 / -73.3 dB |
| input delay + 1/160 internal sample / output delay + 1/147 host sample | -41.3 / -41.3 dB |
| Kaiser beta 5.9 / cut-off 0.899 / 16 zero crossings | -62.0 / -52.6 / -45.1 dB |
| depth 0.8803 ms / ideal rate 0.599878 Hz / phase step 11.2499 rad | -36.7 / +2.6 / -28.4 dB |
| width 0.27 on lines 1 to 6 / cross feed 1/sqrt(3) | -54.8 / -48.6 dB |
| feedback magnitude 1/4 / output weights 1 / sign of 2->1 flipped | -66.2 / -41.9 / -57.2 dB |
| no loop kernel / no second passes | -67.2 / -25.4 dB |

Least-squares gain of the model on the 48 kHz trains 2001 and 2002: 0.9999998.

Constants that are round values: internal rate 44100 Hz; 0.6 Hz; 0.88 ms;
11.25 rad; prime lengths; Decay attenuation with T = 0.5 s; w = 0.272 and the
linear descent from 1 over lines 7 to 16; Kaiser 0.9 / 17 / 6 / 4096; delays
4250/160 internal and 18592/147 host samples; 44 internal samples before the
lines; low-pass 20 kHz, Q 1 inside the fixed filter. Constants that stay
fitted: 16 line coefficients, feedback magnitude 0.247772, output weights
1.377953 and 1.517010, loop kernel (40 taps), fixed filter (960 taps).

## 7. What is still unexplained

Size: -80.1 dB on clean responses at both host rates. It is a property of the
network, not of the converters: at 48 kHz, impulses and output samples of every
kind score the same -80 dB once the table rule is in.

Where it sits at 48 kHz (fresh train 5201, raw 1200..2500):

| band | residual re reference in the band | share of the residual |
| --- | --- | --- |
| 0 to 300 Hz | -79.7 dB | 4.5 % |
| 300 Hz to 1 kHz | -75.4 dB | 15.2 % |
| 1 to 3 kHz | -77.0 dB | 26.8 % |
| 3 to 6 kHz | -79.7 dB | 21.4 % |
| 6 to 10 kHz | -82.1 dB | 14.6 % |
| 10 to 14 kHz | -83.3 dB | 8.8 % |
| 14 to 19 kHz | -83.5 to -82.9 dB | 7.9 % |
| 19 to 20 kHz | -81.0 dB | 0.5 % |
| 20 to 21 kHz | -71.3 dB | 0.1 % |
| 21 to 24 kHz | -50.5 dB | 0.3 % |

In time it follows the reference: about 80 dB below the local energy in every
100-sample stretch that holds an arrival (-79.2 to -80.5 dB re the stretch
from 1300 to 1800, -81.5 at 1800, -79.6 at 1900, -80.5 at 2100, -82.4 at 2300,
-78.9 at 2400). In the two stretches where the reference itself is 35 dB down
(2000 and 2200) the residual is 91 to 92 dB below the mean of the window.
Every response has it (-78.0 to -82.4 dB).

What it is made of (44.1 kHz host, where the network is seen directly):

1. MEASURED, the larger part: a term **proportional to the speed of the line
   length**. A 24-tap kernel applied to the first passes weighted by
   `d length / d sample` (fitted on one train, applied to the other;
   `evidence.slopeKernel`) takes the null from -80.24 to **-85.54 dB**; it
   removes 7 to 10 dB between 300 Hz and 6 kHz, 3 to 6 dB between 6 and
   14 kHz, and next to nothing below 300 Hz or above 17 kHz. Kernel: 0.0094, 0.0087, -0.0132, -0.0120, -0.0141, -0.0088,
   -0.0092, -0.0038, -0.0050, ... then about +0.003 on every second tap. Its
   first two taps are the "sum of the two interpolation weights is
   1 + 1.016 x slope" that packet c noted. It is not in the model because its
   origin is OPEN: a short filter in front of the lines would give a kernel
   whose first tap is zero, and this one's is not.
2. Not identified: the remaining -85.5 dB. It is not a slow change of the fixed
   filter (a free 16-tap correction per impulse and group removes 0.29 dB, the
   amount its degrees of freedom buy), and after the float32 length it is not
   position noise of the two interpolation samples.
3. At 48 kHz, above 20 kHz the model is worse than elsewhere (-71 dB in 20 to
   21 kHz, -50 dB above 21 kHz, together 0.4 % of the residual): the stop band
   of the converters, where the single-precision table and the exact wing rule
   matter most. Not pursued.

On the holdout the residual is the tail of the previous impulse (section 1).

## 8. The model at Tide 100 %, Mix 100 %

`tide100Check` in the data file, computed by `fit_first_order.py` on captures
of packet `tide` (left impulses 0.5 s apart, Macro 100 %, two instances;
read-back `Macro = 100`, `Mix = 100`). Packet `tide` found a comb delay in
front of the network, `469.34 + 256.90 tri(t / 200 s)` samples with `t` counted
from the start of processing, and a slowly moving two-pole low-pass per output
behind it. Each response of the first 60 s is regressed on the model's response
to an impulse at the comb's first exit, with one free 65-tap filter per impulse
(lags -8 to 56) standing in for the voice filter, on the window from the first
arrival to just before the comb's second exit (566 to 766 samples).

| regressor | instance 0, median | instance 1, median |
| --- | --- | --- |
| model at the comb exit | -21.5 dB (best -25.8) | -22.0 dB (best -25.5) |
| model at the impulse time, shifted by the delay | -4.3 dB | -3.3 dB |
| model at the comb exit, oscillators 1 s late | -0.2 dB | -0.2 dB |

- MEASURED: the lines, the modulation law, the start phases and the time
  origin of this model are in force at Tide 100 %, Mix 100 %, and the network
  sits behind the Tide delay (the free filter cannot repair a model evaluated
  at the wrong time: arrivals of different lines move differently).
- Limits: the -22 dB is what a free 65-tap filter achieves against a resonant
  low-pass whose cut-off goes down to 460 Hz and a 35 Hz high-pass (packet
  `tide`). Per impulse the null runs from -25.8 dB to -24 dB (best tenth) over
  the median to -5 dB (worst tenth); the worst are the impulses where the voice
  is closed (level 40 dB or more below the Macro 0 level) and nothing is there
  to fit. It is a test of the network's timing at Tide 100 %, not a null of
  Tide. Left input and left output only.

## 9. What failed or was not established

- Predicting from first principles which wing takes the lower table entry: a
  double-precision simulation of a resampler clock (`time += 1/factor`, whole
  part removed per block) gave signs that depend on the block size. The rule of
  section 4.1 is empirical, constant over the 100 s and the two warm-ups tested.
- With the kernel evaluated exactly (no table) the 48 kHz residual sat between
  14 and 20 kHz (-65 dB in 17 to 19 kHz, overall -71.6 dB). Refitting the
  Kaiser constants does not remove it (packet c: 0.07 dB). It was the table,
  not the window.
- The exact single-precision expression of the line length, and the reason the
  second interpolation sample is off the float32 grid (it carries the first
  tap of the slope kernel).
- The sixteenth line stands only 2.0 times above the clutter and a seventeenth
  below 0.007 cannot be excluded by the projection.
- Laws behind the 16 coefficients, the output weights (1, 1.378, 1.517) and the
  feedback magnitude 0.24777 (1/4 is rejected with this loop kernel).
- Not covered: other host rates (the lattice and both converters differ; the
  table is INFERRED to be the same), Size and Decay (packet `laws`), Width,
  everything after the earliest second passes, Macro above 0 beyond section 8.
- README items that this result overtakes (the README is not mine to edit):
  item 6, the oscillators count from the start of processing, not from the end
  of the warm-up; item 7, resolved; the scorer's statement that the window
  holds first passes only.

## 10. Reproduce

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY fit_first_order.py                         # constants, scores, ablations, checks -> tide_structural_data/first_order.json
$PY score_first_order.py first_order_model.py  # locked holdout
```

Captures rendered by this packet (about 0.3 GB): four 100 s trains at 44.1 kHz
with 1.0 to 1.25 s gaps (seeds 5101, 5102, both inputs), four 100 s trains at
48 kHz (seeds 5201, 5202, both inputs), two 13 s captures with a 4.321 s
warm-up. Reused from the cache: packet c's 48 kHz trains (seeds 2001 to 2003)
and its 44.1 kHz train (seed 1001), packet `tide`'s Macro 100 % captures.

Exploratory scripts are in
`Analyzer/Results/RevOceanCharacterization/work/first_order/` (`s01` to `s23`).
They are not deliverables; several were written against earlier drafts of the
model and no longer run. The numbers quoted from them and not regenerated by
`fit_first_order.py` are: the comparison with packet c's fitted phases (`s01`),
c's constants on clean 44.1 kHz data (`s05`), the float32 grid of the length
(`s12`), the per-impulse filter test (`s09`), the nulls of the start-phase
variants and of the double-precision length (`s22`), the tables of section 1
and section 7 (`s23`), and the split of the 48 kHz residual by class of
impulse time and output sample (`s17`, `s18`).
