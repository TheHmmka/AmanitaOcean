# Laws of the first pass: Size, Decay, sample rate

Packet `laws`. Script: `measure_laws.py`. Data: `tide_structural_data/laws.json`
(its `laws` block holds the constants below, the other blocks the evidence).
Reference: Arturia Rev OCEAN 1.0.0.5848, Tide mode, neutral baseline.

Tags: MEASURED (read off captures), FITTED (a model constant, with residual),
INFERRED (reasoned, not tested), OPEN.

## Conditions, and the Tide 100 % / Mix 100 % question

- **Mix is 100 % in every capture** (read-back `100` in each one).
- **Macro (the Tide knob) is 0 % for the laws below.** That is where the
  reference is repeatable, so the laws could be measured as exact relations
  between captures. They are therefore Macro 0 values, not Tide 100 % values.
- **The laws were then checked at Tide 100 %, Mix 100 %** (section 7). The
  first-pass pattern of lines 1 to 8 comes back unchanged at every Size and at
  44.1 kHz (each line is found again within 3 samples of where Macro 0 puts
  it), so the delay law, the modulation and the sample-rate behaviour carry
  over. The Decay law
  holds within 0.8 dB, which is as far as that check reaches. Tide 100 % adds
  things this packet does not model: an input delay that drifts, an echo
  comb, a slow swell, and a strong reduction of lines 9 and 10.

## The laws in short

The network runs at **44.1 kHz at every host rate**; at 48, 88.2 and 96 kHz the
signal is resampled in and out. All lengths are whole 44.1 kHz samples ("core
samples"). With `s = Size / 100` and `T = Decay` in seconds, the first arrival
through line `i` sits at raw sample (after the impulse, latency included)

    p_i(t) = a(fs) + (fs / 44100) * [ N_i + 38.808 * sin(theta_i) ]
    N_i    = floor(P_i * s + 0.5)                      whole core samples
    theta_i  evaluated when the arrival is read (not when it is written)

and has the gain

    g_i = c_i * w_i(T) * 10^( -3 * P_i * s / (44100 * T) )
    w_i(T) = 1 + k_i * (min(T, 6 s) - 0.5 s)           lines 1 to 9

| constant | value | tag |
|---|---|---|
| `P_i` (lengths at Size 100 %) | primes, table below | MEASURED |
| `a(fs)`, fixed part | 88.06 / 203.34 / 277.12 / 396.69 samples at 44.1 / 48 / 88.2 / 96 kHz | FITTED |
| modulation depth | 0.88 ms = 38.808 core samples, all lines, all Sizes, all rates | FITTED |
| modulation rate | nominal 0.6 Hz, effective 0.599878 Hz (float32 phase accumulator) | FITTED |
| `T60` in the attenuation | the displayed Decay, ratio 1.00002 | FITTED |
| attenuation length | the unrounded `P_i * s`, not `N_i` | MEASURED |
| knee of the weights | 6.000 s | FITTED |

Lines (48 kHz phases; `c` is the sum of the arrival over +-1/6 ms at Decay
0.5 s with the attenuation removed; "own share" is the part of the arrival
that comes from the same-side input):

| line | P (L) | P (R) | ms (L) | phase L | phase R | c | own share | k (1/s) | w at 6 s and above |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1031 | 1039 | 23.379 | 6.2668 | 4.9486 | 0.0713 | 0.636 | -0.091700 | 0.49565 |
| 2 | 1097 | 1109 | 24.875 | 3.6310 | 2.3167 | 0.0840 | 0.636 | -0.04511 | 0.75191 |
| 3 | 1187 | 1193 | 26.916 | 1.0009 | 5.9675 | 0.0825 | 0.636 | -0.03381 | 0.81405 |
| 4 | 1289 | 1301 | 29.229 | 4.6494 | 3.3330 | 0.0786 | 0.636 | -0.03016 | 0.83410 |
| 5 | 1423 | 1429 | 32.268 | 2.0186 | 0.7024 | 0.0753 | 0.636 | -0.02889 | 0.84112 |
| 6 | 1583 | 1597 | 35.896 | 5.6685 | 4.3505 | 0.0735 | 0.636 | -0.02764 | 0.84800 |
| 7 | 1783 | 1787 | 40.431 | 3.0347 | 1.7199 | 0.1169 | 1.000 | -0.02437 | 0.86596 |
| 8 | 2027 | 2039 | 45.964 | 0.4037 | 5.3689 | 0.1199 | 0.9596 | -0.01644 | 0.90959 |
| 9 | 2333 | 2333 | 52.902 | 4.0512 | 2.7373 | 0.1373 | 0.9192 | 0 | 1.00000 |
| 10 | 2699 | 2707 | 61.202 | 1.4214 | 0.1045 | - | 0.8785 | not linear | 1.0278 |

Further lines continue the series: `P` = 3163 (both sides; at 48 kHz tracked
at 10 Sizes on the left and 3 on the right with a law residual of 0.09 to
0.25 samples, whole lengths as the rounding rule predicts), 3719 and 4409
(seen in clutter, residual 0.15 to 0.75 samples). MEASURED at that lower
precision; the Decay weight of line 11 was measured once.

## 1. Method

Stimulus: unit impulses of amplitude 0.5, 0.51 s apart, alternating between the
left and the right input, 40 s per capture (76 impulses; 0.306 cycle of the
modulation per impulse). Decay 0.5 s unless Decay is the subject.

An arrival is cut out in a window of +-1/6 ms. Its position is the slope
through the origin of the phase of the window between 250 Hz and 4 kHz; its
gain is the sum of the window. On a line that nothing crosses this estimator
has 0.004 samples rms of noise at 48 kHz. An arrival is used only where no
other tracked line is within 1/3 ms; a neighbour at that distance biases the
position by 0.03 samples or less (exploration: a second arrival overlaid at
known distances; not part of the script).

Lines are found without start values: a Hough search over (D, A, phi) on
coarsely detected peaks, one line at a time, then three rounds of measuring
and fitting `p = D + A sin(2 pi f (t + p)/fs + phi)`. `t` counts from the end
of the 10 s warm-up. Lines keep the same phase at every Size, which is how a
line is recognised from one capture to the next.

What the estimator cannot do: it does not resolve two arrivals closer than
about 0.2 ms, so a line is not measured while it crosses another. Lines 1 to 3
therefore have fewer arrivals (50 to 59 of 76) and their fitted depth and
phase scatter more (depth +-0.03 samples, phase +-0.003 rad).

## 2. Size (Macro 0, Decay 0.5 s)

18 values from 30 to 200 % at 48 kHz (30, 34.7, 41.3, 50, 58.9, 66.6, 75,
87.3, 100, 109.1, 120, 133.3, 141.4, 150, 163.7, 175, 188.2, 200) and 7 at
44.1 kHz. 20 lines, 330 (line, Size) fits at 48 kHz and 131 at 44.1 kHz.

- **Affine, and quantised.** MEASURED. Every centre delay lies on the lattice
  `a + u N` with whole `N` and `u = fs/44100`: distance to the lattice
  0.0076 samples rms over all 330 fits at 48 kHz (0.0045 rms, 0.017 max over
  the 283 fits with a law residual under 0.03), 0.0094 rms at 44.1 kHz. A free
  fit of the unit gives 1.088437 at 48 kHz (160/147 = 1.0884354) and 1.0000008
  at 44.1 kHz.
- **`N = floor(P s + 0.5)`.** MEASURED. No mismatch in 330 + 131 fits. The
  alternatives fail: `floor(P s)` 0.54 samples rms, `ceil` 0.54, no rounding
  0.32, against 0.008 for rounding to nearest. A delay proportional to Size
  (no fixed part) misses by 91 samples rms.
- Ties are not decided: at Size 50 % and 150 % the float32 parameter lands at
  50.0000003 % and 150.0000056 %, so `P s` sits a few millionths above the
  half in all near-tie cases (38 at each rate). All were rounded up. OPEN
  whether an exact tie rounds up.
- **The Size scale is the displayed value.** MEASURED by the null below: the
  display law the plug-in declares gives `s` to about 1e-6.
- **Fixed part** `a` = 203.342 samples at 48 kHz (4.2363 ms) and 88.058 samples
  at 44.1 kHz (1.9968 ms). FITTED, residual as above. With the exact
  modulation waveform of section 3 these become 203.361 and 88.076. At
  44.1 kHz that is 88 = 2 x 44 samples plus 0.06 to 0.08; 44 samples are the
  reported latency. INFERRED: the fraction is the low-frequency delay of the
  filtering each arrival carries, not part of the delay line. OPEN whether
  `a` is exactly 88.
- `a` is tied to the estimator of section 1 (low-frequency delay of a short
  window). A model that gives each arrival its own kernel carries part of it
  in the kernel: the first-pass packets use 88 core samples at 44.1 kHz and
  187 core samples (203.54 host samples) at 48 kHz, where this estimator
  reads 88.06 and 203.34. The differences between lines, Sizes and rates
  reported here do not depend on that choice.
- **The earliest arrival behaves like the others**: same lattice, same
  rounding, same depth and rate. MEASURED (line 1, 18 Sizes).
- **Depth, rate and phase do not change with Size.** MEASURED on 150 fits with
  60 or more arrivals: depth 42.2488 +- 0.0066 samples at 48 kHz, rate
  0.5998779 +- 0.0000005 Hz, phase within 0.00044 rad of its Size 100 value.
  The depth is constant in milliseconds; it does not scale with Size. A
  constant phase over a 6.6-fold change of delay is what "the delay is
  evaluated when the arrival is read" predicts; evaluated at the write, the
  phase of line 1 would move by 0.15 rad between Size 30 and 200 %, that of
  longer lines by more.
- **Gains change with Size only through the attenuation.** MEASURED. The raw
  gain of a line changes by a factor of 1.7 to 2.6 over the Size range; with
  `10^(-3 P s / (44100 * 0.5))` removed it is constant within 0.2 to 0.6 %
  (the noise of the window sum). The cross-feed ratio does not change either
  (0.572 to 0.573 on lines 1 to 6 at every Size).

### Exact form (null test, 44.1 kHz)

At 44.1 kHz the law can be stated as a relation between two captures with
nothing fitted. Put one impulse `N(s)` samples before a fixed instant, so that
at every Size line 1 is read at the same instant with the same modulation.
Then the first arrival at Size `s` must equal the first arrival at 100 %,
moved by `N(s) - 1031` whole samples and scaled by
`10^(-3 * 1031 * (s - 1) / (44100 * 0.5))`. MEASURED, 23-sample window:

| Size % | N | null, gain by the law | null, gain with whole N |
|---|---|---|---|
| 66.6 | 687 | -133.7 dB | -79.1 dB |
| 75 | 773 | -137.8 dB | -82.1 dB |
| 87.3 | 900 | -137.3 dB | -94.1 dB |
| 133.3 | 1374 | -132.8 dB | -79.9 dB |
| 163.7 | 1688 | -136.6 dB | -82.0 dB |
| 200 | 2062 | -129.6 dB | -129.5 dB |
| 30 / 41.3 / 50 | 309 / 426 / 516 | -87.4 dB | -83.4 / -80.2 / -74.4 dB |

One sample off, the null is +0.2 dB. So between 66.6 and 200 % the whole law
(whole-sample delay, modulation taken at the read, unrounded length in the
attenuation, nothing else depending on Size) holds to float32 precision.

What does not null: at 30, 41.3 and 50 % the residual stays at -87.4 dB, the
same at all three. It is a single extra sample of 1.6e-6 at the start of the
arrival followed by its tail (exploration, seen at 50 %), and the fitted gain
is 1.0000323 times the law.
INFERRED: the interpolation fraction differs by about 3e-5 sample in float32
arithmetic for short delays. OPEN.

## 3. Modulation

- **Depth 0.88 ms.** FITTED. A plain sinusoid gives 0.88018 ms (42.2488
  samples at 48 kHz); with the accumulator waveform below the two reference
  lines give 0.880007 and 0.880015 ms, and fixing the depth at exactly 0.88 ms
  leaves the residual unchanged (0.00403 against 0.00403 samples). The depth is
  the same in milliseconds at all four sample rates (0.880047 to 0.880056 ms
  from 76 arrivals per line).
- **Rate: nominal 0.6 Hz, effective 0.599878 Hz.** FITTED, 0.5998779 +-
  0.0000005 Hz at every Size and rate. A sinusoid at exactly 0.6 Hz misses by
  0.65 samples rms over 98 s.
- **The waveform is the sine of a float32 phase accumulator.** A float32 phase
  that grows by `float32(2 pi 0.6 / 44100)` per core sample and wraps by
  subtracting 2 pi runs at a mean rate of 0.5998782 Hz, because the step is
  rounded differently in each binade of the phase. The same rounding bends
  the waveform: harmonics 2 and 3 of its sine are (-0.000429, -0.000549) and
  (-0.000074, +0.000135) of the depth (cosine, sine, relative to the
  fundamental). Measured on two lines of the shared grid, 980 arrivals each:
  (-0.000421, -0.000544), (-0.000070, +0.000121) and (-0.000430, -0.000547),
  (-0.000069, +0.000117). Law residual over 98 s:

  | model | residual, samples rms |
  |---|---|
  | sinusoid, four free parameters | 0.0214, 0.0216 |
  | sinusoid plus free harmonics 2 and 3 | 0.0044, 0.0044 |
  | accumulator, no extra parameter | 0.0040, 0.0040 |
  | accumulator, rate fixed at the simulated value | 0.0041, 0.0040 |
  | accumulator, depth fixed at 0.88 ms | 0.0040, 0.0040 |

  MEASURED against the simulation. Both lines show the bend at the same place
  of their own phase, so each line has its own accumulator. INFERRED.
- A plain sinusoid fit is therefore slightly biased: `D` 0.018 samples low and
  `A` 0.007 samples high at 48 kHz. The Size and rate tables use the plain
  fit; the bias is the same for every line and cancels in differences.
- **Phases.** MEASURED at 48 kHz, 20 lines: `phi_n = 6.2655 + 4.9668 n`
  (mod 2 pi), `n` = 0 for L1, 1 for R1, 2 for L2 and so on, residual
  0.0010 rad rms and 0.0020 at worst. The step is 0.79049 cycle (0.79048 when
  only the lines with 60 or more arrivals count), uncertain by about 0.00002.
  83/105 = 0.790476 lies within that; INFERRED as a round value, not tested.
  The phases are 0.0062 rad higher at 44.1 kHz and 0.0038 rad higher at
  88.2 kHz than at 48 and 96 kHz (spread over lines under 0.0001 rad).
- OPEN: where the oscillators' time origin lies. The warm-up was not varied.

## 4. Decay (Macro 0)

Method: one impulse on both inputs per capture, so the first pass is clean at
any Decay; 13 Decay values from 0.5 to 45 s at six impulse times, 11 more
between 2.5 and 7.5 s at two times. The gain ratio between two Decay settings
is the least-squares gain between the same arrival's windows, and the
residual of the scaled window says whether anything but the gain changed.

- **Decay changes only gains.** MEASURED. On lines L1 and R1, which nothing
  precedes, the scaled window nulls at -124.7 and -127.2 dB at every Decay.
  On the other lines the null is -64 to -98 dB (the window holds a little of
  the earlier lines' tails, whose gain changes differently) and the position
  moves by 0.0012 samples at most. Delays and modulation do not depend on
  Decay.
- **Attenuation.** MEASURED. From 6 s upward the ratio of every line follows
  `10^(-3 L / (44100 T))` with `L` = 1031.01, 1097.01, 1187.03, 1289.07,
  1423.07, 1583.03, 1783.02, 2027.01, 2333.01, 2699.00 on the left and the
  same quality on the right. Against `P`: slope 0.99998, offset 0.06 samples,
  largest deviation 0.09 samples. So `T60` is the displayed Decay and the
  length is the line's own loop length, with no fixed part (the 88 samples of
  `a` are not attenuated).
- **The length in the attenuation is `P s`, not the rounded `N`.** MEASURED at
  Size 50 %, where they differ by half a sample: 515.50 (L1), 519.50 (R1),
  554.50 (R2), 596.50 (R3), 650.50 (R4) on the arrivals that null below
  -65 dB; at 200 %: 2062.00, 2194.01, 2374.02 and so on. The null test of
  section 2 says the same at -130 dB.
- **It is applied before the output tap.** MEASURED: the first arrival already
  carries it. Second-pass arrivals carry the sum of both lengths: 2194.3 for
  the path through line 2 twice (2 x 1097), 2374.5 for line 3 twice
  (2 x 1187), 2609.9 for lines 3 and 5 (1187 + 1423).
- **The attenuation uses the nominal length, not the modulated delay.**
  MEASURED: the ratio between Decay 30 s and 0.5 s is the same at all impulse
  times within 0.0025 dB (0.0000 dB on L1); the instantaneous delay would
  change it by +-0.1 dB.
- **Tap weights depend on Decay.** MEASURED. After the attenuation is removed
  a factor `w_i(T)` remains. For lines 1 to 8 it is a straight line in `T`
  from 0.5 s to 6 s and constant above: residual of the straight line 6e-8 to
  7e-6, the two pieces meet at 5.9999 to 6.0001 s, largest deviation from
  `1 + k (min(T, 6) - 0.5)` 2e-6 to 2.5e-5. Line 9 does not change (within
  0.00006 of 1). Line 10 rises along a curve that is not a straight line:
  1.0027, 1.0063, 1.0104, 1.0153, 1.0208, 1.0243 at 0.7, 1, 1.4, 2, 3, 4 s and
  1.0278 from 6 s on; line 11 does the same up to 1.049. FITTED slopes in the
  table. The same weights were found at Size 50 and 200 % and at 44.1 kHz
  (differences under 1.2e-4 on arrivals that null below -59 dB).
- The weight enters a path once: the second pass through line 2 twice shows
  -2.478 dB against -2.477 dB for the first pass, and line 3 twice -1.788
  against -1.787 dB. The path through lines 3 and 5 shows -1.63 dB, between
  the two lines' own -1.79 and -1.50 dB. Whether the weight sits at the input
  or the output of a line is OPEN (the two orders of such a path arrive
  together; the feedback matrix would be needed to tell).
- **Cross-feed does not depend on Decay.** MEASURED at 0.5, 4 and 30 s: the
  left input supplies 0.63600 of a left-output arrival of lines 1 to 6 and
  0.36400 of a right-output one; 1.0000 of L7, 0.9596 of L8, 0.9192 of L9,
  0.8785 of L10; 0.0404, 0.0809, 0.1214 of R8, R9, R10. The cross share of
  lines 7 to 10 is 0, 1, 2, 3 times 0.04044. These numbers are a by-product;
  the first-pass packets cover the structure.
- OPEN: the form of `w` for line 10 and beyond, and what the weights are for.

## 5. Sample rate (Macro 0, Size 100 %, Decay 0.5 s)

- **The network runs at 44.1 kHz.** MEASURED, three ways. (1) Line lengths are
  whole samples at 44.1 kHz at every host rate; `D - (fs/44100) P` is the same
  for all lines within 0.010 to 0.021 samples at each rate. (2) At 48, 88.2
  and 96 kHz the response is empty above 21 kHz with one and the same band
  edge (-8.0 dB in 18 to 20 kHz, -24.2 in 20 to 20.5, -38.8 in 20.5 to 21,
  -60 in 21 to 21.5, -75 to -85 dB beyond, up to 48 kHz), while at 44.1 kHz it
  reaches Nyquist (-4.4 dB in 18 to 20 kHz, -35.6 dB in 21.5 to 22.05 kHz).
  (3) At 44.1 kHz an arrival starts abruptly as two samples; at the other
  rates it has pre-ringing.
- **Fixed in seconds or hertz at every host rate**: line lengths, depth
  (0.88 ms), rate (0.599878 Hz), the Decay law and the tap weights. In host
  samples none of them is constant; in core samples all lengths are whole.
- **The shift of README item 8 is the fixed part.** MEASURED:

  | rate | reported latency | `a` in samples | `a` in ms | later than at 44.1 kHz |
  |---|---|---|---|---|
  | 44.1 kHz | 44 | 88.059 | 1.9968 | 0 |
  | 48 kHz | 48 | 203.343 | 4.2363 | 2.2395 ms |
  | 88.2 kHz | 88 | 277.118 | 3.1419 | 1.1451 ms |
  | 96 kHz | 96 | 396.687 | 4.1322 | 2.1354 ms |

  It is the same for every line and does not depend on the impulse time:
  with impulses on 76 different residues modulo 160 samples at 48 kHz the
  law residual is 0.0045 samples against 0.0047 with all impulses on
  multiples of 160, and the centres agree within 0.0013 samples. INFERRED:
  the extra delay is the resampling in and out; it is not reported to the
  host (the plug-in reports 1 ms at every rate).
- The modulation phase differs between rates by an amount that corresponds to
  1.64 ms (44.1 against 48 or 96 kHz) and 1.00 ms (88.2 against 48 kHz) of
  oscillator time. INFERRED: it measures how much of the extra delay lies
  after the delay lines. OPEN.
- Consequence for the campaign: at 44.1 kHz the reference shows the network
  without the resampler, and delays are whole samples. Exact nulls are within
  reach there, as section 2 shows.

## 6. What a second pass shows

Second-pass arrivals sit at `a + u (N_i + N_j)` with the same `a`: measured
`N` 2193.91, 2373.96, 2609.96, 2613.95, 2879.97 on the left and 2621.95 on the
right, for 2 x 1097, 2 x 1187, 1187 + 1423, 1031 + 1583, 1097 + 1783 and
1193 + 1429. MEASURED (law residual 0.05 to 0.11 samples). So the feedback
path adds no delay, and left lines feed left lines. Their modulation depth is
that of two sinusoids added (84.5 samples for a line taken twice).

## 7. Tide 100 %, Mix 100 %

At Macro 100 % the reference is not repeatable, so each Macro 100 % response
was compared with the Macro 0 response to the same impulse: the lag and the
gain at which the Macro 0 first pass is found again.

- **The first-pass pattern is the Macro 0 pattern, delayed.** MEASURED. The
  shifted Macro 0 pattern correlates at 0.37 to 0.45 with the Macro 100 %
  response (each arrival is spread into a pulse and a train of echoes of
  alternating sign), single lines 1 to 8 at 0.65 to 0.82 (median). Clear
  single lines are found at the lag of the whole pattern: median difference
  0 samples, 46 to 70 % within one sample (the search covered +-3 samples;
  the modulation moves lines by +-42 samples against each other).
- **The lag belongs to the input, not to the output.** MEASURED on all four
  paths: 11.00 ms (10.95 to 11.02) one second after the warm-up and rising by
  0.107 ms/s for the left input, 13.99 ms (13.95 to 14.01) and falling by
  0.107 ms/s for the right input, whichever output is read. Fit residual
  0.03 to 0.05 ms. This is the swept delay in front of the network that the
  `tide` packet describes; here it only serves to line the two responses up.
- **Size law unchanged.** MEASURED at 30, 50, 100, 150 and 200 %: the lag is
  the same (L to L 11.017, 11.004, 10.993, 11.003, 11.016 ms). The delay the
  tide adds does not scale with Size, and all lines move together, so the
  lengths, the rounding and the modulation are those of Macro 0.
- **Sample rate unchanged.** MEASURED at 44.1 kHz, two realisations: 10.976
  and 10.976 ms (left), 13.985 and 13.985 ms (right to left); the difference
  from 48 kHz is one to two samples, the resolution of the lag.
- **Decay law consistent within 0.8 dB.** MEASURED on five left-output lines,
  one impulse 7 s after the warm-up, Decay 0.5, 2, 6 and 30 s, three
  realisations each: the gain of a line relative to the other lines of the
  same response changes by 0.09 to 0.78 dB over Decay. Without the Macro 0
  law the ratio of lines 3 and 8, for one, would change by 3.2 dB between
  0.5 and 30 s (2.2 dB of attenuation, 1.0 dB of weight). This is a weaker
  test than the Macro 0 one: the tide's own filter differs from one instance
  to the next and limits it.
- Not carried over, OPEN, for the `tide` packet. Lines 9 and 10 (lengths 2333,
  2699, 2707) come back 3 to 10 times weaker than lines 1 to 8 at Macro 100 %:
  their gain against the gain of the whole pattern is 0.0 to 0.65 where
  lines 1 to 8 show 1.3 to 1.9, with a median correlation of 0.37 to 0.55
  against 0.65 to 0.82 (Size 50 to 200 %, both sides), and no other lag
  recovers them. The cross-fed path changes as well: with a left-only impulse
  the right output's lines differ in level by more than 20 dB from one
  instance to the next while the left output's do not, and line 7, which
  has no cross-feed at Macro 0, has one at Macro 100 %.

## 8. What failed, limits

- The Decay check at Macro 100 % was first run with an impulse on both
  inputs. It gave nonsense on the right output (ratios scattered over 25 dB):
  the two inputs carry different delays there, so each cross-fed line arrives
  twice. It was redone with a left-only impulse.
- First attempt at the Size null picked the wrong arrival (at the chosen
  instant line 2 preceded line 1 by 9 samples at Size 100 %) and did not null
  at all. The instant was moved by half a modulation period.
- Lines 10 and later are measured through second-pass clutter at Size 100 %;
  their weights rest on one or two arrivals per line (null -79 dB), their
  lengths on residuals of 0.03 to 0.25 samples.
- Tap gains `c` are sums over a short window, good to about 0.5 %. They are
  not the total gain of an arrival including its tail.
- Everything here is the first pass (and a few second-pass arrivals). The
  feedback matrix and the late response were not examined.
- The Hough search assumes the modulation rate (0.599878 Hz); the fits then
  free it. A line with a very different rate would not have been found.

## 9. Reproduce

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    cd Analyzer/Campaigns/RevOceanCharacterization
    $PY measure_laws.py            # all parts, about 3 minutes from the cache
    $PY measure_laws.py decay      # one part; the others are kept from laws.json

Captures of this packet: 36 impulse trains of 40 s (574 MB) and about 250
single-impulse captures (about 180 MB), of which the script uses 180; the
rest were superseded during the work.

Not reproduced by the script (exploration, scratch scripts under
`Analyzer/Results/RevOceanCharacterization/work/laws/`): the neighbour-bias
test of the estimator, the lines of length 3719 and 4409 at Size 100 %, the
shape of the residual of the Size null at 50 %, and the observation that an
arrival at 44.1 kHz starts abruptly.
