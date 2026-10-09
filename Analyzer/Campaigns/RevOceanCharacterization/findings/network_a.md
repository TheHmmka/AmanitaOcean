# network_a: the whole response of the network at Macro 0

Packet `network_a`, 6 and 7 October 2026. Model: `network_a.py` with `network_a.c`.
Constants and evidence: `tide_structural_data/network_a.json`, regenerated
byte for byte by `fit_network_a.py` (80 to 90 s once the captures are cached).

Tags: MEASURED (read off captures), FITTED (a model constant, residual given),
INFERRED (reasoned, not tested), OPEN. A null is
`20 log10(rms(model - reference) / rms(reference))` with nothing fitted.

## 0. Operating point, and the owner's question (Tide 100 %, Mix 100 %?)

- **Mix is 100 % in every capture. Macro (the Tide knob) is 0 % in every
  capture of this packet.** The model below is the Macro 0 network: the only
  state in which the reference repeats and can be nulled sample for sample.
- At Tide 100 % the same network sits behind the Tide comb and in front of the
  Tide voices (README, packets `first_order` and `tide`). This packet did not
  re-test that; nothing here is a Tide 100 % measurement.

## 1. Result

The reference at Macro 0 is reproduced through every pass of the
recirculation, for any stimulus below the ducking threshold, at 44.1 and
48 kHz, any warm-up, Decay 0.5 to 45 s and Size 30 to 200 %.

Locked holdout, `score_network.py network_a.py` (final constants):

| case (rate, Decay, Size, warm-up) | overall | 0.25-1.4 s | 1.4-4 s | 4-6 s | 6-14 s |
| --- | --- | --- | --- | --- | --- |
| 48 kHz, 0.5 s, 100 %, 10 s | -94.41 | -93.15 | -94.42 | -90.08 | -86.96 |
| 48 kHz, 2.0 s, 100 %, 10 s | -94.14 | -91.33 | -94.15 | -90.63 | -87.92 |
| 48 kHz, 7.3 s, 100 %, 10 s | -93.62 | -90.01 | -93.78 | -91.27 | -88.37 |
| 48 kHz, 1.4 s, 62 %, 10 s | -94.41 | -91.45 | -94.41 | -91.89 | -87.34 |
| 48 kHz, 3.1 s, 157 %, 10 s | -94.03 | -87.30 | -94.05 | -90.60 | -88.12 |
| 44.1 kHz, 2.0 s, 100 %, 10 s | -101.65 | -92.54 | -101.66 | -92.79 | -88.58 |
| 48 kHz, 1.0 s, 88 %, 12.5 s | -94.49 | -85.88 | -94.50 | -85.97 | -82.58 |

**worstOverallDb -93.62, meanOverallDb -95.25.** The scorer was run three
times:

1. with provisional constants (both filters as four free pole/zero pairs):
   worst -93.72, mean -95.34;
2. with the final constants (shelf gains at round decibel values, section 4):
   worst -93.62, mean -95.25;
3. once more after the data file had been regenerated with more evidence in
   it (same constants): identical to the second run. The data file was
   regenerated a last time after that to correct one ablation row; its
   constants are again identical.

The round values were adopted on this packet's own data before the second run;
nothing was chosen on the holdout and no holdout audio was read outside the
scorer.

Own captures, nothing fitted on them except where stated (`evidence` in the
data file):

| data | null |
| --- | --- |
| two 100 s impulse trains, 44.1 kHz, Decay 0.5 s (16 impulses of each are fitted on) | -92.44 and -90.68 dB; single responses -100 best, -96.6 median, -80 worst |
| the same stimulus class, 18 Decay values from 0.5 to 45 s (24 s each) | -93.77 to -89.41 dB; last third (16 to 24 s) -83.0 to -84.0 dB |
| 8 Sizes from 30 to 200 % at Decay 1 s | -94.5 to -90.3 dB; Size 200 %: -84.1 dB |
| scorer-type programme, seed 777, 6 settings at 48 kHz | -94.41 to -93.51 dB; stretches -85.8 to -94.4 dB |
| the same at 44.1 kHz (two settings, one with a 3.3 s warm-up) | -101.46 and -99.02 dB |
| two impulses at Decay 20 s followed for 100 s, 44.1 / 48 kHz | -88.2 / -84.9 dB overall; -66.5 / -66.3 dB in the last 10 s (section 5) |

A render of 14 s takes 0.2 to 0.4 s.

## 2. The model

Internal rate 44.1 kHz at every host rate. `t` counts internal samples from
the first processed sample, warm-up included.

```
host in -> [converter] -> Q -> 44 samples -> lines of both groups -> low-pass -> [converter or 44 samples] -> host out

per internal sample t, per group g (left, right):
  L_k[t]   = N_k + fl(fl(0.00088 * sine(theta_k[t])) * 44100)             single precision, k = 1..16
  read_k   = (1 - f) buffer_k[t - i] + f buffer_k[t - i - 1]              i = floor(L_k[t]), f = L_k[t] - i
  tap_k    = 10^(-3 P_k s / (44100 T)) * read_k
  out_g[t] = sum_k c_k(T) tap_k                                           -> low-pass 20 kHz, Q 1
  fed_k    = (H * tap_k)[t]                                               loop filter
  write_j  = beta_j (1 + w_j) x_g[t] + beta_j (1 - w_j) x_other[t] + (1/4) sum_k sigma_jk fed_k
```

`x` is the input after the filter `Q` and 44 samples; `s` = Size/100, `T` =
Decay in seconds, `N_k = floor(P_k s + 0.5)`; `theta_k` is the accumulator of
`first_order.md` section 4.3 (accumulator index `2 (k - 1) + g`).

### 2.1 The feedback matrix

- MEASURED. `sigma_jk = -1` where the source index `k - 1` has a bit that the
  destination index `j - 1` lacks an odd number of times, else `+1`
  (`popcount((k-1) & ~(j-1))` odd). That is the Sylvester Hadamard matrix of
  order 16 in natural order **with its outputs written to the lines in reverse
  order**: `sigma_jk = H[16 - j][k - 1]`. Equivalently the fourth Kronecker
  power of `[[1, -1], [1, 1]]` (rows = destination).
- MEASURED. Both groups have the same matrix and the same constants
  throughout; nothing passes from one group to the other.
- Evidence. (a) The measurement that found it: all 256 second passes of a
  group regressed by linear least squares on their known waveforms (first pass
  of line `i`, read again by line `j`), no feedback in the model, later passes
  left in as clutter: 256 of 256 fitted signs follow the rule in each group
  (`firstLook`). (b) With the finished model: the gain of every one of the 256
  second passes, fitted individually against the model's, is 1 within
  3.0e-5 (left, rms 1.0e-5) and 2.8e-5 (right, rms 0.6e-5); no sign differs
  (`matrixEntries`). (c) One flipped sign (entry 16 -> 16): -15.2 dB instead of
  -87.0 dB; the Hadamard matrix without the reversal: +2.2 dB (`ablations`).
- The six entries of `first_order.md` (1->1 +, 1->2 +, 2->1 -, 2->2 +, 1->3 +,
  3->1 -) are those of this rule.

### 2.2 The loop filter and "the factor against 1/4"

FITTED. Every entry is `+-1/4` times one filter `H`, the same for every line:

| element | gain | pole frequency (fitted) |
| --- | --- | --- |
| flat | -0.06 dB | |
| first-order high shelf (1 at 0 Hz) | -0.06 dB at Nyquist | 19060 Hz |
| first-order low shelf (1 at Nyquist) | +0.06 dB at 0 Hz | 4888.0 Hz |
| first-order low shelf | -0.14 dB at 0 Hz | 1587.9 Hz |
| first-order low shelf | -0.14 dB at 0 Hz | 953.3 Hz |

Shelves are bilinear: pole `(1 - t)/(1 + t)`, `t = tan(pi f / 44100)`, zero from
the gain. Response per pass re 1/4: -0.280 dB at 0 Hz, -0.168 dB at 1 kHz,
-0.047 dB at 5 kHz, -0.101 dB at 20 kHz, -0.120 dB at Nyquist. Its first
coefficient is 0.9911004, so the first tap of a second pass is 0.2477751: that
is the 0.247772 of `first_order.md`. **The matrix is 1/4; the rest is the
loop filter.** The 40-tap "loop kernel" of the first-pass model is the start
of this filter's impulse response.

- Round values: the five gains were first free (four free pole/zero pairs and
  a gain); read as shelves they came out at -0.0600 dB (flat), -0.0600,
  +0.0600, -0.1399 and -0.1401 dB. Fixed at -0.06, -0.06, +0.06, -0.14, -0.14
  the null does not change (-90.63 / -86.01 dB for the two groups, free and
  fixed alike; exploration `e28`).
- The pole frequencies are fitted, four numbers. They are not round in any
  convention I tried (corner at the pole, at the zero, at their geometric
  mean: 953 / 938 / 946 Hz, 1588 / 1563 / 1575 Hz, 4888 / 4919 / 4904 Hz,
  19060 / 19080 / 19070 Hz). OPEN.
- The filter does not depend on the line: sixteen free gains on the feedback
  of single lines come out at 0.9999992 to 1.0000001 (`perLineLoopGain`), and a
  free strength of the filter's tail per line at 0.9992 to 1.0013 (exploration
  `e05`, on a free FIR). So the Decay attenuation `10^(-3 P s / (44100 T))`
  is exact to 1e-6 for all sixteen lines, and the loop filter is not a
  Decay-time filter scaled with the line length.
- It does not depend on Decay or Size: the model nulls at -89 to -94 dB from
  Decay 0.5 to 45 s and Size 30 to 157 % with these constants.
- Consequence: the loop loses 0.05 to 0.28 dB per pass on top of the Decay
  attenuation. At long Decay settings the real decay time is shorter than the
  displayed one (Decay 20 s: the wide-band level falls by 52.3 dB per 10 s,
  a T60 of 11.5 s; `lateTail`).

### 2.3 Order of operations

- MEASURED. **The output is taken behind the attenuation and in front of the
  loop filter.** With the loop filter in front of the output tap (and the
  input filter divided by it, so that the first pass keeps its transfer
  function) the null is -82.7 dB against -87.0 dB, and -88.6 against -92.4 dB
  in the first-pass window (`ablations`).
- Between the read of one line and the write of another there is nothing but
  the loop filter and the matrix, in the same sample (no extra delay). Their
  order cannot be told apart (both are time-invariant); nor can "filter the
  input and the feedback together at the write" from this arrangement.
- The attenuation is written in front of the output tap because then the
  output weights do not depend on Size and follow one law at every Decay
  (section 2.5). A black box cannot distinguish that from weights that carry
  the attenuation. INFERRED.
- INFERRED: the input joins behind the matrix. In that basis the input gains
  are one S-curve times a width ramp (section 2.4); carried through the matrix
  they have no such form.

### 2.4 Input filter, input gains, and the "speed term"

- MEASURED. **The low-frequency part of the first-pass model's "fixed filter"
  is in front of the lines; only the 20 kHz low-pass is behind them.** Moving
  it there, with nothing fitted, takes the first passes from -80.25 to -88.4
  ... -89.2 dB (exploration `e10`); in the finished model the first-pass
  window stands at -92.4 dB against -80.25 dB with the filter behind the lines
  (`ablations`).
- This is the mechanism of the term proportional to the speed of the line
  length (`first_order.md` section 7). A filter in front of a line whose length
  changes by `v` samples per sample is read compressed in time; lumped behind
  the line it leaves an error `-v * (dR * (n q[n]))`, `q` the filter's tail and
  `dR` the difference of the filter behind. Measured on isolated first arrivals
  of line 1 and of line 7 (190 samples, 134 arrivals): the residual per unit
  speed is 0.0155 on the second sample, -0.018 on the fourth, then a slow
  positive tail of 0.0027 that crosses zero after 96 samples, the same on both
  lines; removing it takes those arrivals from -79.4 to -87.4 dB (exploration
  `e07`, `e09`). The first-pass packet excluded a filter in front because its
  kernel's first tap was not zero; that tap (0.0025 to 0.0027 here) is of the
  size of the phase lag of the oscillator's sine (section 2.7: 0.001 to 0.002
  sample of oscillator time) and is not part of this term. INFERRED.
- FITTED. The input filter `Q`: four first-order low shelves (unit gain at
  Nyquist) with gains -g, +g, +g, -g at 0 Hz and pole frequencies 105.04,
  380.66, 838.55 and 3604.0 Hz. Unit gain at 0 Hz and at Nyquist, +0.46 dB at
  200 Hz, -0.46 dB at 2 kHz. With `g` free: 0.8359 dB; the model uses 0.835 dB.
  Tests of `g`: 0.835 dB -87.64 dB (first-pass window -94.22), 5/6 dB -87.58
  (-94.26), 0.83 dB -86.60 (-90.41), 0.84 dB -86.62 (-90.22). So
  `g = 0.835 +- 0.003 dB`; 5/6 dB is not excluded. The four gains were first
  free and came out at -0.8346, +0.8343, +0.8354, -0.8348 dB.
- Round pole frequencies fail. The geometric means of pole and zero are
  100.1, 399.4, 879.7 and 3441.8 Hz; corners fixed at 100, 400, 880 and
  3520 Hz without frequency warping give -85.1 dB in the first-pass window
  against -98.2 dB free (left group), with warping -65 dB, and 100, 400, 880,
  3450 Hz with warping -83.9 dB (exploration `e30`). The four frequencies stay
  fitted. What the filter is for
  is OPEN (two plateaus of +-0.835 dB between 100 and 380 Hz and between 840
  and 3600 Hz).
- FITTED. Input gains `beta_k (1 + w_k)` for the own input and
  `beta_k (1 - w_k)` for the other one, the same in both groups (table in
  section 2.6).
  - `w` = 0.272 on lines 1 to 6 and `1 - (k - 7) 0.728 / 9` on lines 7 to 16.
    MEASURED on all sixteen lines: largest difference between the fitted
    `(own - other)/(own + other)` and this law 2.3e-7. This closes the
    first-pass packet's UNVERIFIED cross feed of lines 9 to 16.
  - `beta_k + beta_(17-k)` = 0.150776 for all eight pairs (spread 1e-6).
    MEASURED. One S-curve from line 1 to line 16 (form below, shape 3.36,
    ends 0.1219873 and 0.0287889) fits the sixteen values within 1.1e-5
    relative. FITTED; the model keeps the sixteen fitted numbers.

### 2.5 Output weights

FITTED, a closed form with round constants. With

```
S(x, a) = 1/2 + sign(2x - 1) (e^(a |2x - 1|) - 1) / (2 (e^a - 1))        an S-curve from 0 to 1
u       = (min(T, 6 s) - 0.5 s) / 5.5 s
A1(T)   = 0.46 - 0.232 u                                                  line 1:  0.46 -> 0.228
A16(T)  = 0.336 + 0.092 (e^(-2.6 u) - 1) / (e^(-2.6) - 1)                 line 16: 0.336 -> 0.428
c_k(T)  = A1 + (1 - A1) S((k - 1)/8, 4)          k = 1 .. 9
c_k(T)  = 1 + (A16 - 1) S((k - 9)/7, 2)          k = 9 .. 16
```

Line 9 is the unit. Three anchors (lines 1, 9, 16), two S-curves between them,
and only the two outer anchors move with Decay, up to exactly 6 s.

- Sixteen free weights at Decay 0.5 s differ from this law by 1.6e-6 at most
  (`fitted.largestWeightDeviation`).
- Sixteen free weights at each of 18 Decay values (0.5 to 45 s) differ from it
  by 3.6e-6 to 1.5e-5 (`decaySeries`). This gives the Decay dependence of lines
  11 to 16, which was missing, and it is the same law as for the other lines.
- It contains the verified values of `laws_verification.md`: line 1 falls to
  0.228/0.46 = 0.495652, line 2 to 0.751828, line 10 rises to 1.027804 along
  the exponential map with shape -2.6. The "straight line per line" of that
  packet is the straight anchor of line 1 seen through the S-curve.
- INFERRED: `S` and the map of `A16` are the vendor's Exp(shape) map, the one
  its parameters declare to the host.
- The unit is a convention: only products of input gain and output weight are
  measured. With line 9 = 1, the low-pass at unit gain at 0 Hz and `Q` at unit
  gain at 0 Hz and Nyquist, `beta` is as in the table.

### 2.6 Table of lines

| line | P left | P right | beta | w | own input | other input | weight, Decay 0.5 s | weight, 6 s and above |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1031 | 1039 | 0.1219875 | 0.272000 | 0.1551682 | 0.0888069 | 0.460000 | 0.228000 |
| 2 | 1097 | 1109 | 0.1045554 | 0.272000 | 0.1329945 | 0.0761163 | 0.633857 | 0.476551 |
| 3 | 1187 | 1193 | 0.0934175 | 0.272000 | 0.1188271 | 0.0680079 | 0.697815 | 0.567988 |
| 4 | 1289 | 1301 | 0.0863017 | 0.272000 | 0.1097757 | 0.0628276 | 0.721344 | 0.601625 |
| 5 | 1423 | 1429 | 0.0817554 | 0.272000 | 0.1039929 | 0.0595179 | 0.730000 | 0.614000 |
| 6 | 1583 | 1597 | 0.0788500 | 0.272000 | 0.1002972 | 0.0574028 | 0.738656 | 0.626375 |
| 7 | 1783 | 1787 | 0.0769950 | 1.000000 | 0.1539899 | 0.0000000 | 0.762185 | 0.660012 |
| 8 | 2027 | 2039 | 0.0758092 | 0.919111 | 0.1454862 | 0.0061321 | 0.826143 | 0.751449 |
| 9 | 2333 | 2333 | 0.0749671 | 0.838222 | 0.1378063 | 0.0121280 | 1.000000 | 1.000000 |
| 10 | 2699 | 2707 | 0.0737815 | 0.757333 | 0.1296587 | 0.0179043 | 0.832867 | 0.856024 |
| 11 | 3163 | 3163 | 0.0719257 | 0.676444 | 0.1205794 | 0.0232719 | 0.738485 | 0.774719 |
| 12 | 3719 | 3719 | 0.0690215 | 0.595556 | 0.1101276 | 0.0279154 | 0.685185 | 0.728804 |
| 13 | 4409 | 4409 | 0.0644740 | 0.514667 | 0.0976567 | 0.0312914 | 0.650815 | 0.699196 |
| 14 | 5261 | 5261 | 0.0573581 | 0.433778 | 0.0822388 | 0.0324774 | 0.597515 | 0.653281 |
| 15 | 6299 | 6299 | 0.0462213 | 0.352889 | 0.0625323 | 0.0299103 | 0.503133 | 0.571976 |
| 16 | 7589 | 7589 | 0.0287890 | 0.272000 | 0.0366196 | 0.0209584 | 0.336000 | 0.428000 |

The first-pass coefficient of `first_order.md` is `own input x weight x 0.845782`
(first taps of `Q` and of the low-pass): 0.060370 for line 1, as there; lines 9
to 16 now read 0.116554, 0.091335, 0.075314, 0.063821, 0.053755, 0.041561,
0.026610, 0.010407 (lines 1 to 8 agree with that packet within 1e-6).

### 2.7 Line length arithmetic

- INFERRED from rounding statistics, FITTED as a rule. The first-pass packets
  left the single-precision expression of the length open. On 4168 isolated
  interpolation taps of first passes the length the reference used was
  recovered to a whole unit of single precision (scatter 0.09 unit). With the
  length as "prime + 38.808 sin(theta), rounded once" 122 taps (2.9 %) are one
  unit off, always where the exact value lies within 0.06 unit of a rounding
  tie, and the sign of the miss follows the oscillator phase: the reference's
  sine is that of a phase 0.9e-7 rad smaller in the middle half of the cycle
  and 1.7e-7 rad smaller in the last quarter. That is what folding the phase
  into [-pi/2, pi/2] with the single-precision values of pi and 2 pi gives
  (`pi - theta` and `theta - 2 pi` are exact subtractions). With that, and
  with the products rounded separately (`fl(0.00088 sine)`, then times 44100,
  then plus the whole length), 50 taps (1.2 %) remain one unit off
  (exploration `e24`, `e25`). Whole-response null of the two groups before and
  after: -84.8 / -84.0 dB and -90.5 / -85.8 dB.
- OPEN: the sine itself. The remaining misses are within 0.04 unit of a tie,
  about two units of the sine's own single precision; an approximating
  polynomial evaluated in single precision would do that. The order of the two
  products is weakly supported (44 against 51 poor responses of 352).

## 3. The six questions of the packet

1. **Matrix**: section 2.1. Reversed-output Sylvester Hadamard, 1/4, the same
   in both groups; the factor against 1/4 is the loop filter's first
   coefficient 0.99110.
2. **Order of operations and loop kernel**: sections 2.2 and 2.3. Read,
   attenuate, tap the output, loop filter, matrix, add the input, write. The
   kernel is a flat -0.06 dB and four first-order shelves, not 40 free taps,
   and not a DC blocker: the reference passes 0 Hz (reference over model
   0.99989 to 0.99996 in every band from 0.5 Hz to 20 kHz over a 100 s
   capture, with an early state of the model; exploration `e08`).
3. **Weights and coefficients of all 16 lines**: sections 2.4 to 2.6.
4. **Does anything else in the loop change with time?** Not above the floor.
   MEASURED: with constant gains, filters and matrix the null is flat along
   the response (-92.4, -87.2, -90.7, -87.0, -86.4, -86.3, -86.3, -86.2 dB in
   the stretches from 1000 to 40000 samples after the impulse, impulses not
   used in the fit), and it does not drift over 100 s (-86.3 to -96.2 dB per
   ten seconds, no trend). What remains is single events of one unit of single
   precision in a line length (section 2.7), not a slow change.
5. **The speed term**: section 2.4. The input filter is in front of the lines.
6. **Size and Decay on later passes**: nothing new beyond the first-pass laws.
   Size sets the whole lengths `floor(P s + 0.5)` and, with Decay, the
   attenuation `10^(-3 P s / (44100 T))`; Decay moves the output weights;
   matrix, loop filter, input filter, input gains and modulation do not move.
   MEASURED by the nulls of section 1 (18 Decays, 8 Sizes, programme material).

## 4. Round values and ablations

Null on impulses kept out of the fit (`ablations`; model -87.01 dB, first-pass
window -92.43 dB):

| change | null |
| --- | --- |
| loop filter in front of the output tap | -82.71 dB |
| no loop shelves (flat -0.06 dB only) | -21.80 dB |
| flat gain 0 dB instead of -0.06 dB | -27.32 dB |
| Hadamard matrix without the reversed outputs | +2.17 dB |
| sign of one matrix entry (16 -> 16) flipped | -15.18 dB |
| output weights all 1 | -6.17 dB |
| input filter behind the lines (the first-pass model's place) | -70.93 dB (first passes -80.25, last stretch -67.38) |
| input shelves 5/6 dB / 0.83 dB / 0.84 dB (corners refitted) | -86.96 / -86.09 / -86.10 dB |

Round constants in the model: matrix 1/4 and its sign rule; loop gains -0.06,
-0.06, +0.06, -0.14, -0.14 dB; input shelf gain 0.835 dB; output weight
anchors 0.46, 0.228, 0.336, 0.428, shapes 4, 2, -2.6, knee 6 s; width 0.272 and
its ramp; low-pass 20 kHz, Q 1; everything taken over from wave 1 (primes,
0.88 ms, 0.6 Hz, 11.25 rad, 44 + 44 samples, converters). Fitted and not
round: four pole frequencies of the loop filter, four of the input filter,
sixteen input gains (or the three numbers of their S-curve at 1.1e-5).

## 5. How the null falls off, and what limits it

- First passes: -92 to -98 dB (fit impulses, by group), median single response
  -96.6 dB, best -100 dB. Isolated first passes of lines 7 to 10 (own minus
  scaled other input, which cancels lines 1 to 6 and 16): -108 to -111 dB
  where no rounding event falls (exploration, earlier state of the model).
- Whole impulse response at Decay 0.5 s: -86 to -87 dB in every stretch out
  to 0.9 s (about 15 passes, 108 dB below the start). Limited by the rounding
  events of the line lengths (section 2.7): a response without one is at
  -97 to -100 dB, one with an event in an early line at -80 dB. Sustained
  material averages them: the noise burst and tones of the programme null at
  -99 to -102 dB at 44.1 kHz.
- At 48 kHz sustained material stops at -94 dB. INFERRED: the converters of
  the first-pass model (table, lattice delays, wing rule), which no test of
  wave 1 could see below -80 dB. Not pursued.
- Size 200 %: -84.1 dB (Sizes 30 to 157 %: -90.3 to -94.5 dB). INFERRED: the
  lines are twice as long, the unit of single precision of their lengths is
  two to four times coarser and each rounding event costs more. Not examined
  further.
- Far tail (`lateTail`: Decay 20 s, 100 s): the null falls from -88.6 dB
  (0 to 2 s) over -83.1 dB (10 to 20 s) and -73.4 dB (40 to 50 s) to -66.5 dB
  (90 to 100 s), where the reference is 539 dB below full scale. MEASURED
  cause: a level drift. The least-squares gain of the model falls by 5.4e-6
  per second, the same at Decay 2, 4.5, 9, 25 and 45 s (-5.36e-6 to
  -5.54e-6 per second; exploration `t05`), so it belongs to the loop, not to
  the Decay law: the reference's loop gain is 3.5e-7 (0.000003 dB) per pass
  lower than the round constants give. With the flat gain at -0.060003 dB
  the same tail holds -79.2 dB in the last 10 s (48 kHz: -78.1 dB) and the
  drift is below 4e-7 per second at all five Decays. The model keeps the
  round -0.06 dB. INFERRED: single-precision coefficients in the reference.
  Rounding the model's loop polynomials to single precision makes it worse
  (-53.8 dB), so the reference does not run this filter as one fourth-order
  direct form.

## 6. What failed or was not established

- Free matrix by Gauss-Newton was not run; the 256 entries are tested one at
  a time against the model (section 2.1), which would not see a change that
  leaves every single second pass within 3e-5.
- A three-pole description of the input filter stops at -67.5 dB; a first
  free 720-tap fit of it failed twice (truncated division of the low-pass
  left a click 960 samples after every arrival, read for an hour as a fault
  of the second passes; then a penalty that forced its Nyquist gain to zero).
- Hypotheses for the second-pass residual that were tested and are wrong: a
  different read time or length for the fed-back signal (four regressors, no
  gain), single matrix entries off, a kernel per path.
- Gains -0.7, +0.8, +0.8, -0.8 dB for the input shelves: -56 dB. That guess
  came from reading the gains with the wrong normalisation.
- Not established: what the two filters are for, their eight pole
  frequencies as round values, the sine used for the modulation, the law
  behind the three numbers of the input-gain curve, the scale that makes the
  input gains round, the mechanism of the 3.5e-7 per pass.
- Not covered: host rates other than 44.1 and 48 kHz; input above the ducking
  threshold (0.5629); Width other than 100 % (`w` is its only place in the
  network, law OPEN); Macro above 0; the 3.6e-5 step of `laws_verification`
  below 512 samples of delay (not modelled, not seen at Size 30 %: -94.2 dB).
- For the lead (the README is not mine to edit): "a fixed output filter (20 kHz
  low-pass times a low-frequency correction)" is a low-pass behind the lines
  and a filter in front of them; "40-tap loop kernel" and "feedback magnitude
  0.247772" are the loop filter of section 2.2 with a matrix of 1/4; the tap
  weights are one law for all lines; the residual at -80 dB is explained.

## 7. Reproduce

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY fit_network_a.py                 # constants and evidence -> tide_structural_data/network_a.json
$PY score_network.py network_a.py    # locked holdout
```

Captures of this packet, about 0.41 GB: two 100 s trains at 44.1 kHz (seed
9101, left and right input, Decay 0.5 s), one 24 s stimulus at 18 Decay values
and at 8 Sizes, the programme with seed 777 at eight settings, two 100 s tails
at Decay 20 s.

Exploratory scripts are in
`Analyzer/Results/RevOceanCharacterization/work/network_a/` (`e01` to `e30`,
`t01` to `t05`). They are not deliverables and several run only against the
scratch copy of the core kept there. Numbers quoted from them and not
regenerated by `fit_network_a.py`: the speed kernel on lines 1 and 7 (`e07`,
`e09`), the low part of the fixed filter moved in front with nothing fitted
(`e10`), the per-line tail strength (`e05`), the isolated first passes of
lines 7 to 10 (`e13`), the rounding statistics and the variants of the length
arithmetic (`e24`, `e25`), the free pole/zero pairs and the round-gain tests
of the loop filter (`e20`, `e27`, `e28`), the rejected round corners (`e30`),
the low-frequency transfer (`e08`), the drift against Decay and the
single-precision coefficients (`t04`, `t05`).
