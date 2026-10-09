# Tide: independent verification of the packet `tide`

Packet `tide_verification`. Checked: `findings/tide.md`, `tide_structural_data/tide.json`,
`measure_tide.py`. Everything below was re-derived with my own code and fresh captures
(other impulse times, both inputs, other warm-ups, other Macro values, other instances).
Scripts and intermediate arrays: `Analyzer/Results/RevOceanCharacterization/work/tide_verification/`
(`tv.py` helpers, `designs.py` stimuli, one `analyse_*.py` per section; none of the author's
analysis functions are used). 83 captures, about 1.06 GB of cache.

Tags: MEASURED, FITTED, INFERRED, OPEN as in the campaign. Verdicts: CONFIRMED, REFUTED,
PARTLY, UNVERIFIED.

## 0. Operating point

The checks are centred on **Macro reading back `100` and Mix reading back `100`**, the owner's
listening point. 57 of my captures are at that point; every one of them reads back
`Macro 100`, `Mix 100` (read-back enforced to 1e-6 by `revocean.capture`). MEASURED.

- 100 % wet: at Macro 100 % the output is exactly 0.0 from the start of the stimulus to the
  first delayed arrival of the first impulse; at the undelayed first-arrival positions the
  level is -82 dB re Macro 0 (median, max -61 dB, the tail of the previous response).
- Other Macro values (60, 35, 15, 8, 6, 5.5, 4, 3, 0.5 %) were captured only to test the
  Macro law at points the author did not measure; Macro 0 and 0.2 % only as references.
- Not verifiable by me: that the mode is Tide. The mode is not a host parameter (README
  item 1); every capture is a fresh instance in its default state. UNVERIFIED.

## 1. Verdicts

| # | Claim of the packet | Verdict | Short reason |
|---|---|---|---|
| 1 | Main point is Macro 100 %, Mix 100 %, all captures 100 % wet | CONFIRMED | section 0 |
| 2 | Comb delay D_L(t) = 469.34 + 256.9 tri(t/200 s), deterministic, independent of Macro | CONFIRMED, mid corrected | 830 new points at 14 to 168 s: rms 0.020 samples; true delay is 0.10 samples longer |
| 3 | Feedback comb, -8 dB inverted, loop delay = first delay, one-pole high-pass 35 Hz in the delayed path only | PARTLY | comb confirmed by cancelling its echo; corner is 30 Hz, the one-pole form is not established |
| 4 | Comb per input, right input a quarter period ahead; filters per output | CONFIRMED | all four paths; right input followed through its minimum |
| 5 | Voice filter is a bilinear two-pole low-pass with a gain; one state for the first eight arrivals | PARTLY | poles yes; the numerator is not (1 + z^-1)^2 (section 4). Shared state confirmed |
| 6 | g = 1.40 sin^2(pi phi), cutoff curve 19.28 kHz to 460 Hz (bend 2.94), Q table | PARTLY | laws hold on fresh instances for cutoff below 12 kHz; gain maximum is sqrt(2); Q above 12 kHz is not a plateau |
| 7 | Two voices half a cycle apart, same law, gains sum to 1.40; arrivals 9 to 14 in voice B | CONFIRMED | cutoff_B/prediction 0.9996; sum 1.403 after correcting the gain reading |
| 8 | Filters once after the network, network unchanged | CONFIRMED | band decay within 1 dB; first arrivals null to -45 dB against Macro 0 references |
| 9 | Phase: rate 0.0553 cycles/s, **fixed** start phases L 0.38 / R 0.22, wander independent per output, only the wander differs between instances | REFUTED in part | start phase is random within about +-0.2 cycle; rate at Macro 100 % is 0.0566; L and R start values correlate |
| 10 | Wander generator unknown: bounded modulation or diffusion (OPEN) | PARTLY resolved | bounded (+-0.2 cycle about a fixed line), absent during the first 8 s, runs without input; generator still OPEN |
| 11 | Macro crossfades undelayed and comb path with cos/sin(m pi/2) | CONFIRMED | 53.9, 31.3, 13.2 deg at 60, 35, 15 % (claim 54, 31.5, 13.5) |
| 12 | f_low(m), Q0(m) formulas; gain maximum 1.40 at every Macro; cycle rate unchanged | PARTLY | formulas hold at 60 and 35 %; fail below about 10 %; gain maximum sqrt(2); **rate does depend on Macro** |
| 13 | Depth d = min(1, 17.6 m) | CONFIRMED | 17.58 to 17.60 at 0.5, 3, 4, 5.5 %; full at 6 and 8 % |
| 14 | Level of the impulse-train response re Macro 0 | CONFIRMED | -1.1 to -1.4 dB at 100 % on three new instances |
| 15 | Late arrivals go to a voice by the line they leave through | UNVERIFIED | not testable from outside; see section 6 |

No statement of the packet contradicts README.md. Its author's script reproduces
`tide.json` byte for byte (sha256 `2cde2796...`, 37 s, no new capture).

## 2. Method

Two things differ from the author's method and make the checks sharper.

**Integer exits.** Impulse times were chosen so that the claimed first comb exit
`e1 = n + D(t(e1))` falls on an integer sample (within 2e-4). The Macro 0 reference is then
an impulse at exactly `e1`; a fitted fractional delay is directly "measured minus claimed".

**Echo cancellation.** If the comb is `v(n) = u(n - D)`, `u = x - G v`, an extra impulse of
amplitude `+G` at `e1` cancels what the comb writes back, and the response is the first
exit alone. Two thirds of the impulses carry such an impulse with `G = 10^(-8/20)`; the
others are plain. This tests the comb structure on the plug-in itself and gives responses
without comb echoes for the voice analysis.

Designs (all Decay 0.5 s, baseline otherwise, impulses 0.54 to 0.70 s apart, alternating
left and right input):

| Design | Warm-up | Plug-in time | Captures at Macro > 0 |
|---|---|---|---|
| A | 10 s | 10.6 to 108 s | 100 % x 3 instances, 60, 35, 15 % |
| B | 60 s | 60.8 to 168 s | 100 % x 2 instances |
| E10 | 10 s | 10.4 to 13.9 s | 100 % x 10 instances |
| E1 / E0 | 1 s / 0 s | 1.1 to 11.3 s / 0.13 to 10.4 s | 100 % x 16 instances each |
| G | 1 s | probes at 1.1 to 5.2 s and 29 to 35 s, silence between | 100 % x 8 instances |
| D | 10 s | 10.8 to 34 s | 0.5, 3, 4, 5.5, 6, 8 % |
| F | 10 s | 10.6 to 39 s, exits at chosen fractions of a sample | 100 % x 1 |

Macro 0 cannot be captured with a warm-up of 1 s (two renders differ, `capture` rejects
it). For E0, E1 and G the first-exit reference was rendered at Macro 0.2 %, which equals
Macro 0 within a gain of 0.965 to 1.014; two such references agree at -31 to -44 dB.

## 3. Comb delay and comb

**Delay law at new times.** MEASURED, 830 fits (null below -28 dB, gain above 0.4), designs
A and B, five instances, both inputs, plug-in time 14 to 168 s (the author: 11 to 109 s
precisely, left input only on the long captures):

- measured minus claimed first delay: mean +0.014, rms 0.020, largest 0.052 samples;
- the corners are sharp: within 1.5 s of the left apex (50 s), the left minimum (150 s) and
  the right minimum (100 s) the error stays below 0.05 samples;
- by path: L in to L out +0.020, L to R +0.018, R to L +0.011, R to R +0.013 (medians);
- free refit: mid 469.352, swing 256.910, period 200.0010 +- 0.0004 s, origin +1.0 ms
  (+1.45 ms with the period held at 200 s), rms 0.010 samples; left minimum 212.439, left
  maximum 726.262, right minimum 212.443 samples;
- the delay is the one in force at the exit: evaluating it at the entry time gives rms
  0.042 (origin 0) or 0.019 (origin free, -8 ms), against 0.012 and 0.010. CONFIRMED;
- at the first impulses after the start (E1, t = 1.10 s): +0.011 samples; E0, t = 0.83 s:
  +0.04. The origin is the start of processing, measured directly, not by extrapolation.
  Only the impulse at t = 0.13 s deviates (-0.24 samples, null -24 dB): a start-up effect
  shorter than 0.8 s. MEASURED;
- the origin of +1.0 +- 0.2 ms equals the reported latency (48 samples). INFERRED: the
  latency sits in front of the comb.

**Absolute value.** The numbers above use the author's filter form. With the corrected
form of section 4 the first exit sits +0.104 samples after the claim and the second exit
+0.19 to +0.20 samples after two claimed delays, in all five captures. With the author's
form the same fits give +0.02 and +0.09, and the undelayed path at Macro below 100 %
reads -0.10 (author: -0.10; mine: -0.098, -0.107, -0.136 at 60, 35, 15 %). All three
numbers are one fact: **the comb delay is 0.10 samples longer than claimed**, and the
loop delay equals the first delay to 0.01 samples. FITTED: mid 469.44 samples (9.780 ms).

**Comb structure.** MEASURED on the plain impulses: second exit / first exit -0.3986
(quartiles -0.3989 to -0.3979, design A) and -0.3984 (design B); third-exit group +0.1581
and +0.1584 (0.3981^2 = 0.1585). On the impulses with cancellation the response fitted
with the first-exit reference **alone** reaches -31.6 dB, the same as with three
references (-32.0 dB); without cancellation the first-exit reference alone gives -8.9 dB.
The residual second exit after cancelling with exactly -8 dB is +0.004 of the first exit
(quartiles -0.005 to +0.010; -0.001 to +0.005 by input and design). So the feedback is
0.398 +- 0.005 by cancellation and 0.3985 +- 0.0005 by the ratio; -8 dB (0.39811) is
inside both. CONFIRMED.

**High-pass in the delayed path.** On the first 480 samples of single-exit responses
(144 windows, three instances, corrected filter form) the median null against the corner of
a one-pole high-pass is

| corner, Hz | 0 | 20 | 24 | 26 | 28 | 30 | 32 | 35 | 38 | 42 | 50 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| null, dB | -20.2 | -32.4 | -36.6 | -38.8 | -40.7 | **-41.7** | -40.7 | -38.1 | -35.7 | -33.2 | -29.9 |

FITTED: 30 +- 2 Hz; 35 Hz is 3.6 dB worse. On the whole voice A window the optimum is flat
between 28 and 35 Hz (-31.8 dB) and the band below 200 Hz of the later part of that window
is modelled to about -11 dB only. A one-pole at one corner is therefore not the exact
form. OPEN. That the undelayed path has no high-pass is CONFIRMED: it nulls to -47 dB
without one (section 4).

**How the line is read.** Design F puts the claimed exit at an integer plus 0, 0.25, 0.5
or 0.75 (43 impulses, one instance, first 480 samples). Median null, all groups / null
above 6 kHz: ideal fractional delay -38.9 / -28.4 dB; linear interpolation -37.5 / -25.9
(-33 dB at half-sample fractions); four-point Lagrange -40.1 / -28.8; four-point Hermite
-41.2 / -28.7; **first-order all-pass -43.6 / -32.1 dB**. Linear interpolation is
excluded; the all-pass read beats the ideal delay by 2 to 6 dB in all four groups and
every other read in three of them. FITTED, suggestive (small sample). With it the delay is
0.09 samples longer than claimed.

## 4. The voice filter is not a bilinear low-pass relative to Macro 0

The cleanest test the reference offers: at Macro below 100 % the undelayed copy of the
first arrivals, up to the sample where the first comb copy lands. Its Macro 0 reference is
the response to the same stimulus at the same time, so no delay needs fitting and a free
numerator is identifiable. Design A, median null per window:

| Macro | windows | bilinear, no delay | bilinear, free delay | free numerator, no delay | one fixed numerator, no delay |
|---|---|---|---|---|---|
| 60 % | 226 | -26.6 dB | -36.9 dB (delay -0.100) | -51.3 dB | -47.6 dB |
| 35 % | 215 | -22.7 dB | -32.5 dB (delay -0.105) | -48.5 dB | -45.2 dB |
| 15 % | 218 | -15.5 dB | -23.3 dB (delay -0.139) | -41.9 dB | -40.0 dB |

The free numerator, normalised to sum 1, is (0.317, 0.439, 0.243) at 60 %, (0.316,
0.442, 0.242) at 35 % and (0.310, 0.452, 0.238) at 15 %, with quartiles of +-0.003; the
bilinear low-pass would have (0.25, 0.5, 0.25). One numerator fitted jointly to all three
captures (FITTED):

    H(z) = g · [N(z) / N(1)] · [D(1) / D(z)]
    N(z) = 1 + 1.4410 z^-1 + 0.7639 z^-2         (zeros at radius 0.874, 19.4 kHz)
    D(z) = 1 + a1 z^-1 + a2 z^-2                 the poles of a bilinear low-pass (fc, Q), as claimed

N(z) is the denominator of a bilinear low-pass with cutoff 19304 Hz and Q 2.154. That
cutoff is the upper end of the claimed sweep (19277 Hz), and at Macro 0.5 % the fitted
poles (19.12 to 19.30 kHz, Q 2.06 to 2.15) all but coincide with these zeros. INFERRED:
N is the filter's own denominator at rest, i.e. the filter is flat at Macro 0, or
equivalently the Macro 0 response already contains the low-pass parked at 19.3 kHz and
Tide moves it. The two readings give the same transfer function; the Macro 0 spectrum
cannot separate them because a steep low-pass near 20 kHz (-18 dB at 20 kHz, -78 dB at
22 kHz on the first arrival) masks the region.

Consequences, all MEASURED with the fixed numerator:

- **Gain maximum.** 99th percentile of the gain 1.408, 1.403, 1.408 at 60, 35, 15 %; the
  bilinear form reads 1.396, 1.369, 1.314 (the author: 1.383 and 1.343 at 50 and 25 %,
  "model artefact"). At Macro 100 % the 99.5th percentile rises from 1.405 to 1.410
  (largest value 1.4125).
- **Q at high cutoff.** The bilinear form under-reads Q by 0.8 %, 2.4 % and 12.5 % at 60,
  35, 15 %. Corrected plateau values: 2.10, 3.28, 5.56 (claimed formula 2.094, 3.234,
  5.064).
- **Cutoff.** Unchanged within 0.5 % below 5 kHz, +0.7 % at 5 to 8 kHz, +1.3 % at 8 to
  12 kHz.
- **The "phase error of 0.1 sample"** listed in the author's limits is this numerator
  (its delay at low frequency is 0.926 samples, not 1).
- At Macro 100 % the null of the whole voice A window does not improve (-31.5 against
  -31.2 dB): the cutoff is mostly below 5 kHz there. On the first 480 samples of single-exit
  responses the corrected model reaches -47.7 dB for cutoffs below 2 kHz, -44.7 dB at 2 to
  4 kHz, -36.7 dB at 4 to 7 kHz and -32.3 dB above (310 windows, five instances).

The fixed numerator is not exact either: the free numerator is 2 to 4 dB better and
drifts slowly with the cutoff (first coefficient 0.320 at 3 to 6 kHz, 0.308 at 15 to
20 kHz). OPEN.

## 5. Laws of voice A at Macro 100 % on fresh instances

Five new instances (designs A and B), 1114 fits below -27 dB, author's filter form so that
the numbers are comparable, **no refit**:

- gain: 99.5th percentile 1.405, cutoff at the maximum 4011 Hz (claimed 1.403, 4014 Hz);
- claimed cutoff law driven by the gain: rms 0.41 %, median +0.12 % (author on his data:
  0.25 %). The error is systematic: the measured cutoff lies 0.3 to 0.7 % above the law
  before the gain maximum and 0.2 to 0.45 % below it after. Free refit: 461.4 Hz, 19372 Hz, bend 2.951,
  gain top 1.4025, rms 0.25 %. Round values 500 Hz / 20 kHz / 3: 2.9 %, rejected, as
  claimed;
- Q table: rms 0.022 over 560 Hz to 12 kHz (1078 fits). Free knots 998, 2997, 4948 Hz
  with Q 5.16 (at 500 Hz), 7.00, 2.10, 1.39. CONFIRMED;
- the gain window is sin^2 and the cutoff curve is the true time law: during the first
  6.5 s after the start, where the phase does not wander (section 7), the phase read from
  the cutoff through the claimed curve is a straight line in time to 0.0002 to 0.0004
  cycles for each of 64 curves. The mean rate per phase octant in the long captures is
  flat within +-8 %. A triangular window with the best exponential curve gives 10.8 % rms,
  a sine window 3.5 %. CONFIRMED;
- above 12 kHz almost no fit passes -27 dB at Macro 100 %; section 8 shows that Q there
  is not a plateau.

One state for the voice A window: in 120-sample stretches of single-exit responses a
least-squares mix of "voice A model" and "voice B model" gives weights 1.00 / 0.00 for
every stretch with energy from raw 1240 to 2640 (L to L, 74 impulses with both voices
open). The accuracy falls along the window: -35 to -37 dB up to raw 1700, -27 to -32 dB
at 1700 to 1950, -28 to -29 dB on the strong arrivals after 2000, -19 to -23 dB on the
weak stretches; raw 1900 to 2640 as a whole -26.6 dB (256 impulses) with the filter of
the first arrivals. Letting cutoff and gain of that part differ changes its null by
0.2 dB, so the sweep during one response is not the cause. The error sits below 200 Hz
(that band is modelled to -11 dB there) and above 6 kHz, i.e. in the high-pass and the
delay read of section 3. CONFIRMED to about -30 dB.

## 6. Voice B, partition, position in the structure

**Cutoff of voice B.** Fitted on a 130-sample window around the ninth arrival (L to L) of
single-exit responses, voice A on the first 480 samples; phase of voice A from its cutoff.
132 impulses with voice B open: cutoff_B / claimed cutoff(phi_A + 1/2) median **0.9996**,
quartiles 0.983 to 1.021, rms 3.3 %. A shift of 1/4, 3/4 or 0 cycles gives 83 %, 99 %,
177 %. CONFIRMED.

**Gain of voice B.** Raw: g_A + g_B = 1.315 (quartiles 1.264 to 1.374), the author's
1.32. In my analysis the cause is known: the window null is -10.5 dB because the Macro 0
reference of that window also holds voice A arrivals that are absent from the target, and
a least-squares gain then reads low by exactly that residual share. Corrected with it, for
the 40 impulses with voice A nearly closed: g_A + g_B = **1.403** (quartiles 1.353 to
1.452), corrected / predicted 1.000. FITTED; the correction assumes the two contents are
uncorrelated. With voice A at its maximum (30 impulses) the fitted g_B is 0.05.

**Partition, L input to L output.** Stretch at raw 2680: weights A 0.06, B 0.92 (the
ninth arrival is voice B); 2920, 3280 and 3400: A 0.97 to 0.99, B at most 0.03 (weak
arrivals of voice A after the ninth arrival, as the author lists); 3040, 3160, 3640:
B 0.73 to 0.75. Beyond raw 3700 both voices are present in every stretch and a per-stretch
mix explains only -3 to -5 dB. CONFIRMED for the listed arrivals. For R to L and L to R
the stretches at 2680 and 3040 to 3160 lean to voice A (weights 0.79 to 0.90, mix null
only -6 to -9 dB): the strong voice B arrivals of the L to L path are weak in the cross
paths, so the raw positions of the partition hold for the same-side path only. MEASURED.

**After the network, applied once.** Band energy 6000 and 12000 samples later relative
to the first 3000 samples, octaves 100 Hz to 12.8 kHz, single-exit impulses with voice A
open: L out, Macro 0 -16.8 to -19.7 and -32.3 to -37.0 dB; Macro 100 % -17.4 to -19.9 and
-32.9 to -36.6 dB (R out within 1 dB as well). Late energy (raw 4840 to 12040) re the
Macro 0 reference over 565 impulses: mean -1.25 dB, sd 0.68 dB while voice A spans 52 dB;
-0.4 dB with voice A closed, -2.1 dB with both half open, -0.9 dB with voice A full. That
dip in the middle is what two complementary amplitude windows on two parts of comparable
energy give. CONFIRMED. (The author's mean of -0.1 dB includes the comb echoes, which my
single-exit impulses do not have.)

**Network unchanged.** First arrivals null against Macro 0 references to -45 to -48 dB
(cutoff below 4 kHz) and the undelayed path to -47 dB (Macro 60 %). CONFIRMED.

## 7. The phase: start value, rate, wander

**The start phase is random.** REFUTES "fixed start phases; the start phase is not
random". 32 instances with a warm-up of 0 or 1 s (designs E0, E1), phase from the cutoff:

- at t = 1.1 s two instances differ by a median null of -0.4 to -2.8 dB on the voice A
  window, and the energy of the right output differs by 12.6 dB between instances;
- phase extrapolated to t = 0: left 0.201 to 0.600 (mean 0.427, sd 0.119), right 0.023 to
  0.424 (mean 0.237, sd 0.124). Both ranges are 0.40 wide; a uniform distribution of that
  width has sd 0.115. MEASURED;
- the author's 0.38 and 0.22 are the centres of these ranges (0.40, 0.22), and his
  "wander of 0.096 cycles at 11 s" is mostly this start spread;
- left and right start values are correlated, +0.47 and +0.48 in the two designs (16
  instances each). MEASURED; not independent;
- the two designs differ in the left mean (0.346 +- 0.025 with no warm-up, 0.509 +- 0.018
  with 1 s). Not explained. OPEN.

**No wander during the first 8 s.** In all 64 curves the phase is a straight line from
0.5 to 6.5 s: scatter 0.0002 to 0.0004 cycles, rate 0.05697 +- 0.00014 and 0.05683 +-
0.00010 (left), 0.05655 +- 0.0004 and 0.05679 +- 0.0004 (right) cycles/s. No curve leaves
its line by more than 0.01 cycle before 8.3 s; 20 of 64 do so between 8.3 and 11 s, with
a smooth onset near 8.0 s of processing time (same in both designs, so it counts from the
start of processing, not from the first input). MEASURED.

**The wander is bounded.** For the ten long curves (five instances, up to 168 s) the
phase minus the fixed line `c + 0.0564 t` (c = 0.40 left, 0.22 right) stays between -0.22
and +0.21 cycles in every curve; pooled sd 0.100 (left), 0.106 (right); 1st to 99th
percentile -0.18 to +0.20 and -0.20 to +0.19; histogram flat between -0.2 and +0.2; no
growth from the first to the last third of a curve. A random walk built from rate
fluctuations of the measured size (sd 0.032 cycles/s, correlation time about 1.5 s) would
spread to about 0.5 cycle after 100 s. So the phase is
a fixed line plus a **random offset confined to about +-0.20 cycles**, random already at
the start, constant for the first 8 s, then moving smoothly: stretches at the nominal
rate (20 % of the time, up to 8.9 s long) alternate with episodes of several seconds at a
lower or higher rate. MEASURED. This settles the author's open alternative in favour of
bounded modulation; the generator itself remains OPEN.

**It runs without input.** Design G: after 24 s of silence the phase is off the line of
its first five seconds by -0.19 to +0.33 cycles (sd 0.157 over 10 curves; two independent
uniform values in +-0.2 would give 0.163). MEASURED.

**Rate.** At Macro 100 %: 0.0563 +- 0.0004 cycles/s by cycle counting (10 curves, sd
between curves 0.0011); 0.0569 during the first 6.5 s (calibration of the cutoff curve
+-1 %). FITTED: **0.0566 +- 0.0004 cycles/s, 17.7 s per cycle**. The claimed 0.0553 is
2 % low because it is a joint fit over all Macro values, and the rate depends on Macro:

| Macro | 15 % | 25 % | 35 % | 50 % | 60 % | 75 % | 90 % | 100 % |
|---|---|---|---|---|---|---|---|---|
| mean rate, cycles/s | 0.0508, 0.0509 | 0.0510 (author) | 0.0529, 0.0545 | 0.0533 (author) | 0.0546, 0.0531 | 0.0556 (author) | 0.0563 (author) | 0.0563 (mine), 0.0565 (author) |

One curve of 98 s has an uncertainty of 0.0011, not the 0.002 to 0.003 the author
assumed. 15 to 25 % against 100 % is a difference of 0.0055 +- 0.0008. REFUTES "cycle
rate unchanged". `0.0500 + 0.0064 m` fits all values within 0.0015. FITTED.

Left and right rates are uncorrelated within an instance (+0.07, +0.09, -0.02, -0.01,
+0.04). CONFIRMED.

## 8. Macro law at values the author did not measure

**Path mix.** atan(g_comb / g_dry): 53.88 and 53.92 deg at 60 % (single-exit and plain
impulses), 31.32 and 31.35 deg at 35 %, 13.23 and 13.24 deg at 15 %; quartile widths 0.06
to 0.26 deg. Against 90 deg x m the angle is low by 0.10, 0.17, 0.27 deg (the author's
0.03 to 0.21 deg show the same trend). CONFIRMED within 0.3 deg; the stated 0.1 deg is
too tight below 50 %. Comb ratio -0.3980, -0.3969, -0.388 at 60, 35, 15 %.

**Cutoff range and Q** (author's filter form, claimed formulas, no refit):

| Macro | claimed f_low | cutoff law rms | free f_low | Q plateau claimed / measured | Q plateau, corrected form |
|---|---|---|---|---|---|
| 60 % | 3286 Hz | 0.59 % | 3277 Hz | 2.094 / 2.092 | 2.10 |
| 35 % | 7227 Hz | 1.57 % | 7357 Hz | 3.234 / 3.228 | 3.28 |
| 15 % | 12790 Hz | 2.52 % | 13315 Hz | 5.064 / 5.075 | 5.56 |

CONFIRMED at 60 and 35 %. At 15 % the formulas are 4 % (f_low) and 9 % (Q, corrected
form) low.

**Depth of the gain window** (design D, undelayed first arrivals, 300 to 1000 Hz, 23 s):

| Macro | 0.5 % | 3 % | 4 % | 5.5 % | 6 % | 8 % |
|---|---|---|---|---|---|---|
| (1 - g_min) / m | 17.583 | 17.599 | 17.589 | 17.581 | g_min 0.001 | g_min 0.0001 |
| g_max measured | 1.0364 | 1.2169 | 1.2917 | 1.4010 | 1.4143 | 1.4136 |
| 1 + 0.40 d | 1.0352 | 1.2112 | 1.2816 | 1.3872 | 1.4000 | 1.4000 |
| 1 + (sqrt(2) - 1) d | 1.0365 | 1.2187 | 1.2916 | 1.4010 | 1.4142 | 1.4142 |

`d = min(1, 17.6 m)` is CONFIRMED at four new values. The maxima follow
`g = (1 - d) + d · sqrt(2) · sin^2(pi phi)` to 0.0002 where the maximum was reached; 1.40
is off by up to 0.014. MEASURED: **the gain maximum is sqrt(2)**, +3.01 dB.

**Below about 10 % the claimed filter law does not hold.** One moving biquad with the
fixed numerator, undelayed path:

| Macro | 0.5 % | 3 % | 4 % | 5.5 % | 6 % | 8 % |
|---|---|---|---|---|---|---|
| lowest cutoff, Hz | 19122 | 18221 | 17815 | 17137 | 16913 | 16016 |
| claimed f_low, Hz | 19018 | 17774 | 17298 | 16606 | 16381 | 15510 |
| Q at the lowest cutoff | 2.09 | 3.2 | 4.0 | 5.4 | 5.7 | 6.1 |
| claimed Q plateau | 7.33 | 6.86 | 6.68 | 6.43 | 6.35 | 6.03 |
| median null, dB | -43.2 | -30.9 | -27.0 | -28.0 | -27.3 | -31.4 |

The claimed f_low is 2.5 to 3.3 % low from 3 to 8 %. Q does not approach 7.4 as Macro
goes to 0: it falls back to 2.1, the value of the numerator. Within one sweep Q also
depends on the cutoff near the top (8 %: 4.1 at 18.3 kHz, 5.5 at 16.8 kHz, 6.1 at
16.1 kHz), so "Q0 from 5 kHz up" is not a plateau above about 16 kHz. This is the
"unexplained Q roll-off above 13 kHz" of the author. MEASURED. Between 3 and 8 % a single
biquad nulls only to -27 to -31 dB; a mix of the unmodulated signal and one filtered voice
improves it by 7 to 10 dB but with weights that make no physical sense (negative at 4 to
6 %). The structure in the fade-in region is OPEN.

## 9. Level

Plain impulses only, 0.52 s per impulse, relative to Macro 0, L in to L out: -1.08,
-1.39, -1.12 dB at Macro 100 % (three instances; author -1.18 to -1.37 dB); L in to R
out -1.15, -1.51, -1.57 dB; +0.40, +2.02, +2.51 dB at 60, 35, 15 % (between the author's
values at 50/75, 25/50 and 10/25 %). CONFIRMED.

## 10. Corrected description

Only the lines that change against section 10 of `tide.md`:

```
D_c(t)  = 469.44 + 256.91 · tri((t - 0.001)/200 + 0.25·c)        samples; delay in force at the exit
comb    : feedback -0.398 (-8 dB), loop delay = first delay (to 0.01 sample); read close to a first-order all-pass
in_c    = cos(m pi/2) · x_c + sin(m pi/2) · HP[v_c]               HP about 30 Hz, exact form open
voice   : H(z) = g · N(z)/N(1) · D(1)/D(z),  N = 1 + 1.4410 z^-1 + 0.7639 z^-2,  D = bilinear poles of (fc, Q)
g(phi)  = (1 - d) + d · sqrt(2) · sin^2(pi phi),   d = min(1, 17.6 m)
fc, Q   : as claimed for Macro >= 35 % and cutoff below 12 kHz; upper end of the sweep 19.3 kHz = the zeros of N;
          Q -> 2.15 at the upper end and at Macro -> 0; f_low(m), Q0(m) formulas not valid below about 15 %
phi_o(t)= c_o + r(m) · t + x_o(t)     c_L = 0.40, c_R = 0.22;  r(1) = 0.0566, r(m) ~ 0.0500 + 0.0064 m cycles/s
x_o(t)  : random, confined to about +-0.20 cycle; random at t = 0 (uniform), constant until t ~ 8 s,
          then smooth; left and right start values correlated (+0.47); runs without input
```

## 11. Limits and what I could not establish

- The generator of x_o(t) (shape of the transitions, their timing, why the left mean
  start value differs between warm-up 0 and 1 s). 32 short and 10 long curves.
- The exact high-pass of the comb path and the exact read of the delay line. Together
  they limit the Macro 100 % null of the voice A window to -31 dB; the first 10 ms reach
  -45 to -48 dB when the cutoff is below 4 kHz.
- The numerator N: fitted to +-0.03 in its first coefficient; a free numerator is 2 to
  4 dB better. Whether the Macro 0 response itself carries a resonance at 19.3 kHz is not
  decidable from outside.
- Filter structure and laws below Macro 10 %; cutoff and Q between 12 kHz and the top of
  the sweep at Macro 100 %.
- Voice B was measured on the ninth arrival of the L to L path only; its gain rests on
  a correction for reference content (scatter +-4 %). The right output's voice B was not
  measured separately.
- Claim 15 (which voice a late arrival passes) was not tested.
- All at Decay 0.5 s, Size 100 %, 48 kHz, Width 100 %, Macro set before processing
  starts, as in the packet. The all-pass read rests on 43 impulses of one instance.
