# Verification of the packet `laws` (Size, Decay, sample rate)

Packet `laws_verification`. Checked: `findings/laws.md`, `measure_laws.py`,
`tide_structural_data/laws.json`. Reference: Arturia Rev OCEAN 1.0.0.5848, Tide
mode, neutral baseline. Everything below comes from my own captures (229,
about 360 MB, none of them the author's stimuli) and my own code; the author's
analysis functions were not used. The holdout was not touched. Scripts and
result files are in
`Analyzer/Results/RevOceanCharacterization/work/laws_verification/` (section 9).

Tags: MEASURED (read off captures), FITTED (a constant, with residual),
INFERRED (reasoned, not tested), OPEN.

## 0. Tide 100 % and Mix 100 %: which values these are

- **Mix is 100 % in every capture**, the author's and mine (read-back `100`).
- **The laws of the packet are Macro (Tide) 0 % values.** The author checked
  them at Tide 100 % only coarsely (delays to 3 samples, gains to 0.8 dB).
- **I tested them at Tide 100 %, Mix 100 % directly** (section 6, fresh
  realisations 5 and 6, Size 62 / 100 / 157 %, Decay 0.5 / 3.3 / 16 s, both
  inputs, 48 and 44.1 kHz). Result, MEASURED: the first pass at Tide 100 % is
  the Macro 0 first pass **at the same Size and Decay**, delayed by the tide's
  input delay and passed through one filter per response. That relation nulls
  at -38 to -45.5 dB on lines 1 to 6, the relative gains of those lines are
  the Macro 0 ones within 0.6 %, and the same test with the Decay law left out
  stops at -13 to -21 dB. So the line lengths, the rounding, the modulation,
  the attenuation and the tap weights of this packet **are** the values at
  Tide 100 %; the tide adds a layer around them (the `tide` packet) and does
  not change them.
- Not covered at Tide 100 %: lines 9 and later (they pass the tide's second
  voice), the late response, 88.2 and 96 kHz.

## 1. Verdicts

| # | claim of the author | verdict | section |
|---|---|---|---|
| 1 | Mix 100 % everywhere; laws are Macro 0 values, re-checked at Tide 100 % | CONFIRMED | 0 |
| 2 | the network runs at 44.1 kHz at every host rate | CONFIRMED | 2, 5 |
| 3 | delay `D = a + (fs/44100) floor(P s + 0.5)` | CONFIRMED at 19 new Sizes; ties now resolved to +-0.002 sample | 3 |
| 4 | exact Size null on line L1, with a failure at 30, 41.3, 50 % | CONFIRMED on line R1 at 30 new Sizes; the failure is PARTLY as described: it is tied to the whole delay falling below 512 samples, not to those Sizes | 3 |
| 5 | lengths are the listed primes; further lines 3163, 3719, 4409 | CONFIRMED; a 14th line of 5261 samples found | 2, 5 |
| 6 | fixed part `a(fs)`, the same for every line and impulse time | CONFIRMED; at 44.1 kHz it is exactly 88 samples (the author's open point) | 2, 5 |
| 7 | depth 0.88 ms at every Size, rate 0.599878 Hz, delay taken at the read | CONFIRMED | 2, 3 |
| 8 | float32 phase accumulator, one per line | CONFIRMED; the proposed phase step 83/105 cycle is REFUTED | 2 |
| 9 | Decay changes only gains | CONFIRMED | 4 |
| 10 | attenuation `10^(-3 P s / (44100 T))`, unrounded length, displayed Decay | CONFIRMED | 4 |
| 11 | tap weights `1 + k_i (min(T, 6 s) - 0.5 s)` with the listed `k_i` | PARTLY: form and knee confirmed, constants of lines 2 to 7 corrected; line 10 has a closed form; the weight belongs to the output tap | 4 |
| 12 | gains depend on Size only through the attenuation; cross-feed constant | CONFIRMED (apart from the 3.6e-5 step of claim 4) | 3, 4 |
| 13 | Tide 100 %: Macro 0 first pass delayed by an input lag that ignores Size and rate | CONFIRMED and sharpened from +-3 samples to +-0.06 sample | 6 |
| 14 | Tide 100 %: Decay law holds within 0.8 dB | CONFIRMED and sharpened to +-0.6 % (0.05 dB) on lines 1 to 6 | 6 |
| 15 | Tide 100 %: lines 9 and 10 are 3 to 10 times weaker; line 7 gets a cross-feed | line 7: REFUTED; lines 9 and 10: PARTLY (a selection effect) | 6 |

The author's script reproduces the data file: run into a scratch copy
(`repro_author.py`, 2 min 16 s from the cache), the result is byte-identical to
`tide_structural_data/laws.json` (529367 bytes).

Nothing in the packet contradicts `README.md`. The preliminary shifts of README
item 8 (2.235, 1.09 and 0.104 ms against 48 kHz) agree with 2.2395, 1.0944 and
0.1042 ms from this verification.

## 2. An exact model at 44.1 kHz (the basis of most checks)

At a host rate of 44.1 kHz the response is exactly zero before the first
arrival and every arrival is a two-tap read. MEASURED (`v_exact44.py`):

    y[m] = sum_i g_i (k * z_i)[m]
    z_i[m] = (1 - f) x[m - n] + f x[m - n - 1],     n + f = 88 + N_i + 38.808 sin(theta_i(m - 44))
    N_i = floor(P_i s + 0.5)

`m` is the raw output sample, `theta_i` the float32 accumulator of the line
(start phase `-q * 1.3163744 rad`, q = 0 for L1, 1 for R1, 2 for L2, ..., the
value of packet `first_order_a`), stepped once per sample since the start of
processing, and `k` one kernel **after** the read (alternating ringing for
about 40 samples, then a slow negative tail).

- Line 1 alone, first 14 samples, 39 and 41 arrivals over 60 s, everything
  freed: length 1031.00000 and 1039.00000, depth 38.80800 and 38.80801, read
  lead 44.04 and 43.99 steps; null -76.7 and -77.9 dB. With the kernel before
  the read the same fit stops at -42 dB (exploration).
- Lines 1 to 7 of each side jointly on the whole stretch before the second
  pass (36 impulses, 20 s): -65.7 / -65.6 dB with all constants fixed, -71.3 dB
  with length, depth and phase freed per line. Freed lengths: 1031.000000,
  1097.000007, 1187.000010, 1289.000009, 1422.999999, 1583.000001,
  1783.000001 (left) and 1038.999999, 1108.999999, 1193.000002, 1301.000015,
  1429.000009, 1597.000009, 1787.000001 (right). Freed depths 38.80798 to
  38.80801 (all twenty lines).
- Lines 7 to 10 on `own - cross / 0.5723286` (the response to an impulse on
  the own input minus the scaled response to the same impulse on the other
  input; every path that enters through lines 1 to 6 cancels there): lengths
  1783.000001, 2026.999998, 2333.000012, 2699.000009 and 1787.000000,
  2038.999996, 2333.000004, 2706.999994; -73.1 dB freed.

So the twenty lengths are whole to 1.5e-5 sample, the fixed part is
**88.00000 samples** for every line, and the depth is 38.808 samples = 0.88 ms.
The 88.06 to 88.08 the author reads is the same 88 seen through the
low-frequency delay of the kernel: with the author's estimator I read 88.077,
with a wider window 88.154 (section 5).

Alternatives in the same joint model (`v_misc.py`): a sinusoid at the
accumulator's mean rate -27.8 dB, a sinusoid at exactly 0.6 Hz -14.3 dB, the
phase step 83/105 cycle that the author proposed -29.1 dB, against -65.7 dB.

Limit of this model: it is not float-exact. It stops near -65 dB with fixed
constants; freed phases move by 0 to 0.84 accumulator steps (up to 7e-5 rad)
in a pattern that is not a straight line in q. OPEN, and a matter for the
first-pass packets.

## 3. Size (Macro 0, Decay 0.5 s)

**Rounding and depth** (`v_size.py`). 19 Sizes the author did not use (33,
37.7, 45.5, 54.3, 62, 70, 77.6411, 77.6450, 81, 93.1, 107, 116.5, 127, 137.9,
146, 157, 171, 183, 194.6 %), 15 impulses each, lines 1 to 10 of both sides,
412 (line, Size) fits with the kernel of Size 100 %:

- fitted whole length minus `floor(P s + 0.5)`: no mismatch, 1.2e-4 samples rms,
  1.5e-3 at worst. MEASURED;
- ties: `P s` with fractional part 0.4990 rounds down, 0.5020 (twice) and
  0.5049 round up. So the rule is round to nearest within +-0.002 sample. What
  happens at an exact tie stays OPEN (the Size scale is known to about 1e-6,
  0.003 sample of the longest line, so an exact tie cannot be produced);
- depth 38.80800 +- 0.00013 samples at every Size (38.8066 to 38.8092): it does
  not scale with Size. MEASURED;
- null with every constant fixed, phases included: -64.5 to -66.2 dB (lines 1
  to 6 or 7) and -54.3 to -57.0 dB (lines 7 to 10) at every Size, the same as at
  100 %. A delay evaluated at the write instead of the read would shift the
  phase by 0.06 rad at 33 % and destroy that null.

**Exact null** (`v_size_null.py`): line R1 (the author used L1), read instant
57329 (the author used 52170), impulse placed `floor(1039 s + 0.5)` samples
earlier, 24-sample window, nothing fitted:

| Sizes | null, gain by the law | gain with the whole length | one sample off |
|---|---|---|---|
| 30 values from 51 to 194.6 % | -117.1 to -127.6 dB | -76.6 to -110.3 dB | +2.1 dB |
| 30, 33, 37.7, 41.3, 45.5, 50 % | -88.7 to -88.8 dB | | |

The fitted gain equals the law within 3.1e-7 from 51 % up. CONFIRMED, including
the unrounded `P s` in the gain.

**The step below 51 %** (`v_size_step.py`). MEASURED:

- it is not a property of the Sizes 30, 41.3 and 50 %: it is present at every
  Size up to 50.8 % (N = 528) and gone at 51.0 % (N = 530). At the instant
  used the modulation is -16.17 samples, so the step lies where the whole part
  of the delay without the fixed 88, `N + floor(38.808 sin theta)`, passes 512
  (present at 511, absent at 513);
- the residual is the kernel starting at the **second** tap, 4.14e-5 of the
  peak: the weight of the second tap is larger by 3.6e-5 of the line's gain,
  the first tap is unchanged; fitted gain 1.0000358 times the law (the author:
  1.0000323 on L1). With the gain fitted the null is still only -102.8 dB;
- the same number (3.57e-5 to 3.60e-5) at 16 Sizes from 30 to 50.8 %, so it is
  a step, not a drift.

The author's reading "the interpolation fraction differs for short delays" is
close, but the trigger is the 512-sample boundary of the whole delay, which
the modulation carries line 1 across at Sizes of about 46 to 53 %.
Mechanism OPEN. It is a -89 dB effect.

**Gains and cross-feed.** The cross / own ratio of lines 1 to 6, sample for
sample, is 0.5723261 to 0.5723290 at Size 33, 62, 100, 157 and 194.6 % and
0.5723270 to 0.5723271 at 21 Decay values (null -119 dB). MEASURED.

## 4. Decay (Macro 0)

Captures: one impulse per capture on the left or on the right input at the
same instant; 21 Decay values the author did not use (0.5, 0.55, 0.62, 0.85,
1.2, 1.7, 2.3, 3.3, 4.2, 5.2, 5.7, 5.95, 6.05, 6.3, 7, 8, 11, 16, 25, 38, 55 s)
at Size 100 %, 12 of them at Size 62 and 157 %, two impulse times (0.23 and
0.77 s; the author's were 0.10 to 1.45 s in steps of 0.27 s).

**The law as a whole** (`v_decay.py`). If Decay changes nothing but the gain
`G_i(T)` of each line, the responses at all Decay values are
`sum_i G_i(T) a_i[m]` with unknown but common `a_i`. Solving the `a_i` per
sample leaves a residual that tests the law without any model of an arrival:

| `G_i(T)` | lines 1 to 7, eight cases | lines 7 to 9 |
|---|---|---|
| the author's law and constants | -95.6 to -125.5 dB | -109.0 to -111.2 dB |
| no tap weight | -21.4 to -26.3 dB | -28.5 to -30.4 dB |
| knee at 5.9 s / 6.1 s | -51.8 to -59.0 dB | |
| the constants of the table below | **-125.8 to -131.1 dB** | |
| those constants, whole length `N` in the attenuation (Size 62 and 157 %) | -100.8 to -103.7 dB, against -126.0 to -128.4 dB with `P s` | |

This test cannot separate the slopes of two overlapping lines of similar
length (their `G` nearly span the same space), so the constants come from the
second method.

**Per line** (`v_decay_onset.py`). At 44.1 kHz an arrival starts abruptly.
Where the previous arrival is at least 70 samples back, earlier lines leave
only a slow tail under it; that tail is extrapolated from the 24 samples
before the onset and removed, and the gain ratio is taken on the first 6
samples. 72 arrivals with nulls of -110 to -134 dB:

| line | slope k (1/s) | weight from 6 s up | arrivals | author's slope | author's weight |
|---|---|---|---|---|---|
| 1 | -0.0916996 | 0.4956522 | 5 | -0.091700 | 0.49565 |
| 2 | -0.0451223 | 0.7518273 | 3 | -0.045107 | 0.75191 |
| 3 | -0.0338270 | 0.8139515 | 6 | -0.033809 | 0.81405 |
| 4 | -0.0301757 | 0.8340338 | 4 | -0.030164 | 0.83410 |
| 5 | -0.0288917 | 0.8410959 | 7 | -0.028887 | 0.84112 |
| 6 | -0.0276377 | 0.8479926 | 7 | -0.027636 | 0.84800 |
| 7 | -0.0243731 | 0.8659480 | 16 | -0.024371 | 0.86596 |
| 8 | -0.0164387 | 0.9095871 | 8 | -0.016438 | 0.90959 |
| 9 | 0.0000000 | 1.0000000 | 8 | 0 | 1.00000 |
| 10 | not linear | 1.0278038 | 8 | | 1.0278 |

FITTED. Spread between arrivals (both sides, Size 62, 100 and 157 %, two
impulse times): 1e-7 in the slope, 7e-7 in the weight. The weight is a straight
line in `T` to 3e-7, and the two pieces meet at 6.0000 s in every case. So the
form of claim 11 holds; the constants of lines 2, 3 and 4 were off by 8e-5,
1.0e-4 and 7e-5 in the weight (more than the 2.5e-5 the author gives as the
largest deviation of a measured weight from the law), those of lines 5 to 7
by 0.7e-5 to 2.4e-5. INFERRED cause: the author's windows on these lines hold
the ringing of the earlier lines (their own nulls there are -64 to -98 dB).
The independent test above moves from -95.6 to -127.6 dB in its worst case
with the new constants.

**Line 10** follows the vendor's exponential map (the form of its declared
parameter laws):

    w_10(T) = 1 + 0.0278038 * (e^(-2.6 x) - 1) / (e^(-2.6) - 1),      x = (min(T, 6 s) - 0.5 s) / 5.5 s

FITTED on 104 values (8 arrivals): free shape -2.60001, residual 1.8e-7 rms,
4.6e-7 at worst; shape exactly -2.6 gives the same residual, -2.5 gives 1.1e-4,
a quadratic 5.7e-4. This closes the author's open point for line 10. Lines 1 to
8 are the same map with shape 0. Line 11 was not measured here (UNVERIFIED:
"up to 1.049").

**Attenuation length.** From the ratio between 6.05 (6.3) s and 55 s the
length is `P s` within 0.012 sample on all 72 arrivals; where `P s` and the
whole `N` differ by 0.17 to 0.47 sample the fitted length follows `P s` (29
cases at Size 62 and 157 %). `T60` is the displayed Decay. CONFIRMED.

**Where the weight sits** (`v_second_pass.py`). The author left open whether
the weight belongs to the input or the output of a line. In
`own - cross / 0.5723286` the first arrivals after line 10 are the second
passes that enter through line 7 and leave through line 1, 2 or 3. Their gain
against Decay, with the attenuation of `(P_7 + P_b) s` removed, on 11 arrivals
(nulls -104 to -130 dB, one -82 dB):

| path | weight from 6 s up | line 7 | exit line | product |
|---|---|---|---|---|
| 7 then 1 | 0.495652, 0.495652, 0.495653 (0.495631 at -82 dB) | 0.865948 | 0.495652 | 0.4292 |
| 7 then 2 | 0.751829, 0.751828 | 0.865948 | 0.751827 | 0.6510 |
| 7 then 3 | 0.813952 (four times), 0.813951 | 0.865948 | 0.813952 | 0.7048 |

MEASURED: a path carries the weight of the line it **leaves** through, once.
The weight is a gain of the output tap, not of the input and not of the loop.
The same arrivals confirm that the attenuation uses the sum of both unrounded
lengths (plateau flat within 1e-6 from 6.05 to 55 s) and that a second pass
starts within one sample of `88 + d_b(m) + d_7(m - d_b(m))`.

## 5. Sample rate and further lines (Macro 0, Size 100 %, Decay 0.5 s)

`v_rate.py`: impulses at the same times in seconds at the four rates, lines L1
and R1, the author's definition of the position (phase slope 250 Hz to 4 kHz,
window +-1/6 ms) implemented again, exact accumulator, fixed part `a` and read
lead `c` fitted per rate (20 arrivals per line, residual 0.003 to 0.007
samples rms).

| rate | `a`, lines L1 / R1 | author (accumulator fit) | later than 44.1 kHz | author | read lead `c` (44.1 kHz samples) |
|---|---|---|---|---|---|
| 44.1 kHz | 88.0766 / 88.0775 | 88.0759 | 0 | 0 | 44.2 / 44.5 |
| 48 kHz | 203.3619 / 203.3628 | 203.3614 | 2.2395 ms | 2.2395 | 116.3 / 116.4 |
| 88.2 kHz | 277.1533 / 277.1546 | 277.1517 | 1.1451 ms | 1.1451 | 72.1 / 72.4 |
| 96 kHz | 396.7251 / 396.7268 | 396.7236 | 2.1353 ms | 2.1354 | 116.2 / 116.6 |

CONFIRMED. Three remarks:

- `a` depends on the estimator, as the author says: a window of +-1/4 ms and
  500 Hz to 2.5 kHz reads 88.154, 203.452, 277.318 and 396.904. The differences
  between rates do not move (2.2396, 1.1452, 2.1355 ms). In the two-tap model
  the value at 44.1 kHz is exactly 88.
- The headline values of the packet (88.06, 203.34, 277.12, 396.69) are the
  plain-sinusoid fits, 0.018 sample below the accumulator fits.
- Impulses on 19 and 20 different residues modulo 160 at 48 kHz
  (`v_rate_offgrid.py`): `a` = 203.3634 and 203.3620, residual 0.004 samples.

The read lead reproduces the author's phase offsets: 116.3 - 44.3 = 72 steps is
1.63 ms (author: 1.64 ms), 116.3 - 72.2 = 44 steps is 1.00 ms.

Band edge: -7.8, -24.4, -38.7, -60 dB in 18-20, 20-20.5, 20.5-21, 21-21.5 kHz
at 48, 88.2 and 96 kHz and -74 to -80 dB beyond; at 44.1 kHz -4.3 dB in
18-20 kHz and -35.5 dB in 21.5-22.05 kHz. As reported.

**Further lines** (`v_misc.py`). A two-tap matched sum over 36 impulses of
`own - cross / 0.5723286`, for every whole length from 2900 to 9000 and the
phase of the 11th to 16th line, peaks at **3163, 3719, 4409 and 5261** on both
sides (peak 100 to 340 times the median for the first three, 41 times for
5261; the neighbouring lengths reach 25 to 47 % of the peak, as two taps must).
The first three confirm the author; **5261 (a prime) is a 14th line**,
MEASURED at this lower level. For a 15th and 16th line the two sides do not
agree on a length: not found. OPEN.

## 6. Tide 100 %, Mix 100 %

**Method** (`v_tide_capture.py`, `v_tide.py`). The `tide` packet puts a comb
delay in front of the Macro 0 network and one filter ("voice A") behind lines 1
to 8 of an output. Then, for one impulse,

    y_tide = h * r        on the first pass of lines 1 to 8,

with `r` the Macro 0 response, **at the same Size and Decay**, to what the comb
makes of the impulse, and `h` one short filter per response. `r` is a capture,
so it holds the whole Macro 0 law; nothing about the lines is fitted. The comb
constants are the `tide` packet's. On the stretch before the comb's second
exit arrives (lines 1 to 5 at Size 100 %, 1 to 6 at 62 %, 1 to 3 or 4 at
157 %) only the first delay matters.

Captures: one impulse 9 s after the warm-up; Macro reads back `100`, Mix `100`;
realisations 5 and 6 (the author used 0 to 2); (Size, Decay) = (100, 0.5),
(62, 0.5), (157, 0.5), (100, 3.3), (100, 16), (62, 16), (157, 3.3); right input
at (100, 0.5) and (100, 16); 44.1 kHz at (100, 0.5). 40 responses.

**Results.** MEASURED:

- Comb-free stretch, `h` a free FIR of 49 taps: -41.3 to -45.5 dB in 36 of 40
  responses. In the other four the voice is resonant (cutoff 2.3 to 3.4 kHz,
  Q 2 to 3.8) and 49 taps are too short (-14.5 to -37.6 dB); there `h` as a
  gain, a two-pole low-pass and a delay (4 numbers) gives -37.9 to -40.6 dB.
  Better of the two per response: **-37.9 to -45.5 dB, median -42.9 dB**.
  Without a filter the same stretch is at +1 dB.
- Relative gains of the lines: each line's own arrival against the 4-number
  model, 196 values: **0.996 to 1.006**, rms deviation 0.14 %; spread within a
  response 0.2 % (median), 1.0 % at worst. The same at Decay 3.3 and 16 s as at
  0.5 s.
- Control: with `r` rendered at Decay 0.5 s for a response at 3.3 or 16 s the
  null is -13.3 to -20.8 dB and the line gains spread by 17 to 57 %. The test
  does see the Decay law, weights included (at 16 s line 1 carries 0.4957,
  line 5 carries 0.8411).
- The 4-number model fitted on the first two lines only predicts the rest of
  the stretch at -21.0 to -39.7 dB, within 2.1 dB (median 0.3 dB) of its null
  when fitted on the whole stretch (-21.6 to -40.6 dB; this model is weak when
  the cutoff is above 9 kHz, as the `tide` packet notes).
- Delay of the response against the `tide` packet's law, from the same fit:
  -0.056 to -0.001 sample (left input, 28 responses), +0.011 to +0.032 (right
  input), -0.014 to -0.006 at 44.1 kHz with the law taken in milliseconds.
  With the cutoff under 9 kHz, left input: medians -0.015, -0.013 and -0.019
  sample at Size 62, 100 and 157 %. **The tide's delay does not depend on Size
  or Decay and is the same in milliseconds at 44.1 kHz, within 0.06 sample.**
  (The `tide` packet lists both as open.)
- Whole stretch up to line 9, comb echoes included, FIR of 131 taps: -22.1 to
  -36.6 dB (median -31.9 dB). Line gains there: lines 1 to 6 0.982 to 1.009;
  lines 7 and 8 0.977 to 1.044 for the left input and 0.910 to 1.056 for the
  right input, where an echo of line 3 lands on line 7. Control: 36 to 83 %.

So claims 13 and 14 hold, and far more tightly than stated. Corrected value:
the author's lag (11.00 ms rising, 13.99 ms falling, one second after the
warm-up) contains the voice filter's delay and has a resolution of 3 samples;
the `tide` packet's law gives 10.955 and 13.953 ms there and is the one that
this test confirms.

**Line 7** (`v_tide_lines.py`). Left impulse, right output. A cross-fed R7
would arrive exactly where the Macro 0 response to the same comb signal on the
right input has its own R7; that response, through the fitted `h`, is the
template. Cross share of line 7 at Tide 100 %: **+0.0003 and +0.0013** (an
own-fed R7 would stand 13.7 and 14.2 dB above the level of the whole stretch;
the residual in its window is -21.9 and -27.0 dB). REFUTED. What sits there is
the comb's inverted echo of earlier lines; the author's own numbers for this
"cross-feed" are negative (-13.7 and -4.4), as an inverted echo would be.

**Lines 9 and 10.** The author's "3 to 10 times weaker" was measured only on
impulses where lines 1 to 8 correlate with Macro 0, that is where voice A is
open. In the three responses here where voice A is nearly closed (gain 0.114,
0.132, 0.173) the stretch around line 9 stands at 0.88, 1.05 and 0.97 of its
Macro 0 level: 5 to 7 times **stronger** than lines 1 to 5 at that moment.
PARTLY: the observation is a selection effect, consistent with the second
voice of the `tide` packet. This measure is coarse (the stretch also holds
comb echoes of earlier lines); the laws of lines 9 and later at Tide 100 % are
not tested here.

## 7. Corrections and additions for a model

| item | packet `laws` | this verification |
|---|---|---|
| fixed part at 44.1 kHz | 88.06, "whether exactly 88 is open" | 88 exactly (two-tap model, 20 lines, 1.5e-5); the read is taken 44 samples before the raw output sample |
| tap weights, lines 2 to 7 | 0.75191, 0.81405, 0.83410, 0.84112, 0.84800, 0.86596 | 0.7518273, 0.8139515, 0.8340338, 0.8410959, 0.8479926, 0.8659480 |
| weight of line 10 | a curve, form not found | `1 + 0.0278038 (e^(-2.6 x) - 1)/(e^(-2.6) - 1)`, `x = (min(T, 6) - 0.5)/5.5` |
| place of the weight | input or output undecided | output tap (a second pass carries the weight of its exit line) |
| rounding near a tie | undecided | to nearest within +-0.002 sample |
| Size null below 51 % | fails at 30, 41.3, 50 %, cause open | 3.6e-5 on the second tap whenever the whole delay (without the fixed 88) is below 512 samples; mechanism open |
| phase step between lines | 0.79049 cycle, 83/105 suggested | 83/105 refuted (-29 dB against -66 dB); `-1.3163744 rad` per line holds within 7e-5 rad |
| further lines | 3163, 3719, 4409 | the same, and 5261 |
| Tide 100 % | delays +-3 samples, gains 0.8 dB | lines 1 to 6: null -38 to -45.5 dB, gains +-0.6 %; tide delay independent of Size, Decay and host rate within 0.06 sample |
| Tide 100 %, line 7 | gets a cross-feed | none (share 0.000 to 0.001) |

## 8. What failed, what is not established

- My joint model at 44.1 kHz stops at -65 dB (-71 dB with freed phases). I did
  not find what limits it.
- First versions of my windows were wrong twice: a line that was not in the
  model reached into the end of the window (nulls of -8 dB at small and large
  Sizes), and the second pass that enters through line 7 and leaves through
  line 2 can arrive before the one through line 1. Both are fixed in
  `model44.early_window` and `model44.late_window`.
- The whole-law Decay test does not fix the slopes of overlapping lines; my
  first attempt to read them from it gave values that scattered by 5e-6.
- A free per-Decay gain for line 10 inside that test converged to nonsense
  (the gain is only defined up to multiples of the other lines' laws). The
  onset method replaced it; the field `weightLine10` of `v_decay.json` is that
  failed attempt and must not be used.
- At Tide 100 %: one impulse time (9 s), two realisations, Decay up to 16 s,
  Sizes 62 to 157 %. Lines 7 and 8 are tested only with comb echoes on top
  (+-4 %, left input). Lines 9 and later, the late response and 88.2 / 96 kHz
  are not tested. The voice filter is taken as free; the comb constants are
  the `tide` packet's and were not re-measured.
- Not re-measured: the tap gains `c` of the table in `laws.md`, the cross
  shares of lines 7 to 10, the positions of second passes other than the three
  paths of section 4, the weight of line 11 and later.
- The mechanism of the 3.6e-5 step and the form of the start phases stay OPEN.

## 9. Reproduce

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
    W=/Users/nespesha/Workspace/AmanitaOcean/Analyzer/Results/RevOceanCharacterization/work/laws_verification
    cd Analyzer/Campaigns/RevOceanCharacterization
    cc -O2 -shared -o $W/accum.dylib $W/accum.c     # float32 accumulator
    $PY $W/v_exact44.py          # section 2; writes the kernels the next script needs
    $PY $W/v_size.py; $PY $W/v_size_null.py; $PY $W/v_size_step.py              # section 3
    $PY $W/v_decay_capture.py; $PY $W/v_decay.py; $PY $W/v_decay_onset.py        # section 4
    $PY $W/v_decay_corrected.py; $PY $W/v_second_pass.py
    $PY $W/v_rate.py; $PY $W/v_rate_offgrid.py; $PY $W/v_misc.py                 # sections 2, 3, 5
    $PY $W/v_tide_capture.py; $PY $W/v_tide.py; $PY $W/v_tide_lines.py           # section 6
    $PY $W/summary.py            # the numbers quoted here, from the v_*.json files
    $PY $W/repro_author.py; $PY $W/compare_repro.py                              # the author's script

Each `v_*.py` writes a `v_*.json` next to itself; with the captures cached the
whole set runs in about ten minutes. Captures: 229 (20 of them at Macro
100 %), about 360 MB.
