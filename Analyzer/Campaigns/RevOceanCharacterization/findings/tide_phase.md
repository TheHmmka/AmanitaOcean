# Tide: the generator of the voice phase

Packet `tide_phase`. Generator and comparison: `tide_phase.py`. Regenerating script:
`measure_tide_phase.py` (about 2 minutes once the captures are cached; a second run
writes the same file). Numbers and trajectories: `tide_structural_data/tide_phase.npz`
(1.0 MB; its `summary` entry is JSON). Figures marked "exploration" come from scratch scripts in
`Analyzer/Results/RevOceanCharacterization/work/tide_phase/` and are not reproduced by
the script.

**Operating point.** Macro 100 %, Mix 100 %, 48 kHz, block 512, Decay 0.5 s, Size 100 %,
no warm-up, unless stated. The no-warm-up choice is deliberate: the first seconds of an
instance are part of the question. Every capture reads back the requested Macro and Mix.
Smaller sets at Macro 75, 50 and 25 %, at a 44.1 kHz and a 96 kHz host, with
Size 60 % / Decay 2 s / block 128, and after 1, 2 and 30 s of warm-up.

Tags: MEASURED, FITTED (residual given), INFERRED, OPEN.

## 1. Result

```
phase_o(t) = level_o(t) + rate(Macro) · t          cycles, voice A of output o, t from the first processed sample
voice B    = phase_o(t) + 1/2                      at all times

level_o(t): one generator per output channel
    grid       f_L = 0.136 Hz (cell 7.353 s),  f_R = 0.160 Hz (cell 6.25 s),  origin at the first processed sample
    knot k     at (k + u_k) / f_o,  k = 1, 2, ...,  u_k random in [0, 1): exactly one knot in every cell
    values     the level holds its start value until knot 1; between knot k and knot k + 1 it goes from its
               value at knot k to a new random target along a raised cosine, (1 - cos(pi x)) / 2
    targets    uniform in [0.184, 0.591] (left), [-0.007, 0.433] (right)
rate(m)   = 0.0508 + 0.00565 · (e^m - 1) / (e - 1)  cycles/s;   0.05645 at Macro 100 %
```

This is value noise on a jittered grid: a sample-and-hold random modulator with random
timing and a cosine glide, one per output, added to a fixed-rate ramp. It replaces the
"bounded wander" of wave 1.

| | |
|---|---|
| The same in every instance | rate law, both grid frequencies, grid origin, level bounds, raised-cosine shape, the hold before knot 1, voice B = voice A + 1/2 |
| Random per instance | the two start levels (tied to each other, section 6), every jitter u_k, every target |
| Independent of | host rate (44.1, 48, 96 kHz), block size, Size, Decay, warm-up, render speed; it runs without input |
| Depends on Macro | only the rate |

The generator `tide_phase.TidePhase` built from these numbers cannot be told apart from
the 40 measured instances by 54 statistics (section 8). Not captured: the fine structure
of the joint law of the two start levels, and a 20 % deficit of late knots (u > 0.8).

## 2. Method

**Estimator.** All instances of a set receive the same Gaussian noise (rms 0.1, peak 0.5).
In a short-time transform (4096 samples, hop 2048, bins 300 Hz to 18 kHz) every bin of
every instance is `Y_i = G(phase_i) E + G(phase_i + 1/2) L`: E and L, what the network
feeds to voice A and voice B, are common to all instances; G is the voice response of
`tide_verification.md` (gain sqrt(2) sin^2, cut-off curve, Q table, fixed numerator).
Solving each frame for E, L and all phases removes the randomness of the noise, so the
phases are relations between captures and not spectral estimates. One global ambiguity
(all phases shifted by 1/2 with E and L swapped) is fixed by the start centres of
`tide_verification.md` (0.40 left, 0.22 right).

**Calibration of the phase axis.** During the first seconds every instance runs at one
fixed rate (below). The raw phase is corrected so that it is linear in time there, with
half-cycle symmetry, and the cycle is closed (`calibrate`). The correction is between
-0.002 and +0.005 cycle. FITTED.

**Precision.** MEASURED, main set:

- the joint model explains the spectra to -31.7 dB (median relative residual);
- frame-to-frame noise of a phase 0.00024 cycle; deviation from a straight line between
  0.8 and 6.2 s: 0.0007 cycle rms (slow errors of the voice law);
- against impulse probes (exploration, `b03_validate_final.py`): the same tracker run on
  the 32 impulse-only captures of `tide_verification` (designs E0 and E1) and compared with
  that packet's phases read from the cut-off of single responses, 766 readings: mean
  difference -0.0010 cycle, sd 0.0029, 96 % within 0.005; start values per instance agree
  within 0.005. The two methods share no code and no stimulus.

**Sets.** 402 captures, 2.6 GB of cache (the packet asked for 40 instances of 100 s, which
alone is 1.5 GB; the 1 GB guideline could not be kept).

| Set | Instances x length | Purpose |
|---|---|---|
| main | 40 x 100 s | everything at Macro 100 % |
| starts | 200 x 1.2 s; 24 x 1.2 s after 0, 1, 2 s of warm-up | start levels |
| Macro 75, 50, 25 % | 8 x 40 s each | Macro law |
| 44.1 kHz host | 8 x 40 s | host rate |
| Size 60 %, Decay 2 s, block 128 | 8 x 40 s | three controls at once |
| warm-up 30 s | 8 x 25 s | time origin, running without input |
| 96 kHz host; block 32; background priority | 8 x 14 s each | host rate, block size, render speed |

## 3. The process runs on audio time

MEASURED (exploration, `p08_slow.py`, `p09_compare.py`). The Analyzer renders 42 times
faster than real time, so a generator driven by the wall clock would look 42 times too
slow. Rendering the same 14 s at background priority takes 6.2 to 7.7 s instead of 1.5 s
(4.3 times slower); the curves keep their time scale: in both sets all 16 levels are
constant to 0.01 cycle until 8 s and the first ones move between 8 and 10 s.

Eight instances started within the same second differ (nulls between -12 and +2 dB).
The start levels of 200 instances show no relation to launch order or launch time (circular
correlation at lags 1 to 6 in launch order at the noise level; no linear relation to the
finish time for periods of 0.5 to 400 s). MEASURED. The randomness is therefore drawn
per instance and is not a function of a coarse clock; what seeds it is OPEN.

## 4. The level: cosine eases between knots on a jittered grid

All numbers from the main set (80 curves of 100 s) unless stated. Level = phase - rate·t.

**Shape.** The rate of the level is a train of bumps that return to zero between them:
the level goes from one value to the next along an S-curve and the next S-curve starts
at once. MEASURED, exploration: 726 segments with a step above 0.05 cycle and longer than
1.5 s, normalised to unit step and unit duration, differ from (1 - cos(pi x))/2 by at
most 0.0004 on average at 20 points along the segment (smoothstep 3x^2 - 2x^3 would
differ by 0.010); on 239 isolated transitions the raised cosine fits to 0.00036 cycle
rms, smoothstep 0.00044, a fifth-order ease 0.00048, two parabolas 0.0010, a straight
line 0.0025. A one-pole or slew-limited approach is excluded: the bumps are symmetric.

**One knot per cell.** MEASURED. With knots found without any grid (penalised
segmentation, exploration) 97 % of the windows [k·6.25 s, (k+1)·6.25 s) of the right
output and 93 % of the windows of 7.35 s of the left output hold exactly one knot; for
other window lengths the share falls to 50 to 70 %. Fitting every curve with exactly one
knot per cell (`fit_grid`: dynamic programme, then least squares on jitters and levels):

- residual 0.0008 cycle rms (median of 80 curves; 78 below 0.002), the size of the
  estimator's own slow error. Two curves end at 0.003 and 0.005: the optimiser left a
  knot at a cell boundary; the worse one was inspected and has one knot per cell;
- the cost of the description is flat for a cell of 7.32 to 7.36 s (left) and 6.24 to
  6.27 s (right) and rises steeply outside: +120 % at 7.40 s, 7 times at 7.20 s; 3 times
  at 6.20 s, 56 times at 6.10 s (exploration, 20 curves);
- the knots that lie strictly inside their cells allow cells of 7.3475 to 7.3610 s
  (left) and 6.2440 to 6.2519 s (right). **1/0.136 Hz = 7.3529 s and 1/0.160 Hz = 6.25 s
  lie inside**; these round values are used. FITTED. (8 x 44100 samples at 48 kHz, 7.35 s,
  also lies inside, but the cell does not change at a 44.1 kHz host, section 7.);
- grid origin: the cost is flat for origins from -0.2 to +0.1 s around the first
  processed sample and rises outside (exploration, `a44_anchor.py`). MEASURED;
- successive segment durations correlate with -0.51 (left) and -0.46 (right); a jittered
  grid gives -0.5, independent durations 0. Durations run from 0.45 to 14.1 s. MEASURED.

**Jitter.** u_k of 1014 knots: uniform on [0, 1) within the precision of a
Kolmogorov-Smirnov test per channel (p = 0.22, 0.15), successive jitters uncorrelated
(-0.05, -0.06). Pooled, p = 0.026: the share of knots with u > 0.8 is 0.160 +- 0.013
instead of 0.200 (0.167 in the 203 knots of the Macro sets). The fit does not cause it:
on 240 generated curves with measurement-like errors it returns 0.208 for a true 0.208
(exploration, `a49_synthetic.py`). MEASURED deficit, cause OPEN; the generator uses the
uniform law and the comparison of section 8 does not see the difference.

**Targets.** Levels reached at knots 2 and later: 390 (left) between 0.1845 and 0.5900,
468 (right) between -0.0057 and 0.4323; uniform (KS p = 0.20, 0.85); successive levels
uncorrelated (-0.03, +0.05); level and jitter uncorrelated (0.00, 0.04; all pairs of
neighbours within 0.09). Bounds corrected for the finite sample: left 0.1835 to 0.5910,
right -0.0066 to 0.4332, so the two spans differ: 0.407 and 0.440 cycle. MEASURED. The
absolute position depends on the voice law used by the tracker: designing its biquad at
48 kHz instead of 44.1 kHz moves the upper bounds by +0.003. Bounds are good to +-0.004.

**No shared sequence.** Exploration (`a27_repeat.py`). Level triples that repeat between
two instances within 0.006 cycle: 2 (left) and 1 (right), with 1.2 and 1.8 expected by
chance; jitter triples within 0.01: 2 and 1, expected 0.6 and 0.9. MEASURED. The instances do not replay one
pseudo-random sequence from different positions, and they are not one curve shifted or
scaled: only the grid, the bounds and the shape are common.

## 5. Voice B

MEASURED. Fitting the two voices with independent phases
(`Tracker.frame_two_phases`, every fourth frame of the main set): phase_B - phase_A - 1/2
has mean -0.0003 cycle and sd 0.006 over all readings; -0.0004 +- 0.0055 over the 17930
readings where both gains are between 0.29 and 1.12. The mean is the same in the first
8 s and later (exploration: +0.0010, -0.0003, +0.0005, -0.0001 in four time ranges). Its
regression on the rate of the level gives a lag of voice B behind voice A of
0.4 +- 1.3 ms. Voice B is voice A plus half a cycle at all times, also while the level
moves at 0.3 cycle/s; there is one phase per output, not two.

The reading has a phase-dependent bias of up to +-0.006 cycle (antisymmetric under a
half-cycle shift), an error of the voice law near the gain zero, not a property of the
reference.

## 6. The first seconds and the start levels

**Hold.** MEASURED. Every one of the 80 main curves, and every curve of the other sets, is
constant from the start until its first knot. First knots: 7.82 to 14.71 s on the left
(cell 1 is 7.35 to 14.71 s), 6.43 to 12.33 s on the right (6.25 to 12.5 s). During the
hold the phase is a straight line (0.0007 cycle rms, per-curve rate 0.05648 +- 0.0003
cycles/s). Because a raised cosine starts flat, a move of 0.01 cycle is seen later:
5/50/95 % of the curves at 8.7/11.3/16.4 s (generator: 8.5/11.8/16.3 s). This is the
"no wander during the first 8 s" of `tide_verification.md`: not a start-up mode, the
first cell of the grid with the start level as both ends of the first segment.

**Start levels.** MEASURED on 200 instances of 1.2 s: left 0.190 to 0.593, right -0.007
to 0.431, each uniform over the range of the later targets (KS p = 0.64, 0.18). They are
values of the same random variable as the targets, not a separate start phase.

**The two outputs are tied at the start and only there.** Over 312 start pairs (200 +
72 + 40): correlation 0.535 (single sets 0.49 to 0.67); both levels lie in the same half
of their ranges in 83.3 % of the instances (50 % if independent). MEASURED. After the
start the two outputs are independent: knots of one output fall near knots of the other
no more often than between different instances (22 % within 0.5 s in both cases),
level correlation after 16 s is 0.03 +- 0.04, jitters and targets of the two outputs are
unrelated (exploration). MEASURED.

The generator uses "same half with probability 0.83, otherwise independent". This gives
the correlation (0.50) and the uniform margins but not the fine structure: the measured
difference left - right differs from the law (KS p = 0.008), pairs in opposite halves
are denser near the centre of the plane than the law says (24 of 53 in the two central
sixteenths, 13 expected), and nearest neighbours in the plane are closer than in a
surrogate with the same coarse occupancy (z = -4.5, exploration). The law behind the
tie is OPEN. INFERRED: the two generators draw their first numbers from sources that are
related at start-up and unrelated afterwards.

**Not reproduced.** In the 16 instances of design E1 of `tide_verification` (warm-up 1 s)
15 left start values lie in the upper 43 % of the range. My sets with 0, 1 and 2 s of
warm-up (24 instances each) are spread evenly (left mean 0.368, 0.437, 0.392; sd 0.11 to
0.12). Warm-up does not shift the start levels; why that one batch is clustered is OPEN.

## 7. Rate, Macro and the other controls

**Rate at Macro 100 %.** FITTED: 0.05645 cycles/s (period 17.71 s), from the rate that
minimises the cost of the grid description over 100 s: 0.05642 (left), 0.05648 (right).
The hold alone gives 0.05658 (cycle closure, +-0.0002). Uncertainty +-0.00005. The round
value 0.0565 fits as well (cost +0.6 %); 0.0566, the wave 1 value and 0.04 sqrt(2), costs
+8 % and is two standard errors away (exploration).

**Macro.** One set of 8 x 40 s per value. MEASURED:

| Macro | 25 % | 50 % | 75 % | 100 % |
|---|---|---|---|---|
| rate, left / right, cycles/s | 0.05208 / 0.05118 | 0.05296 / 0.05278 | 0.05458 / 0.05447 | 0.05642 / 0.05648 |
| rate, mean | 0.05163 | 0.05287 | 0.05452 | 0.05645 |
| law of section 1 | 0.05173 | 0.05293 | 0.05447 | 0.05645 |
| cell / nominal, admissible, left | 0.94 to 1.09 | 0.97 to 1.05 | 0.93 to 1.01 | 0.9993 to 1.0011 |
| cell / nominal, admissible, right | 0.95 to 1.05 | 0.97 to 1.01 | 0.98 to 1.02 | 0.9990 to 1.0003 |
| levels left, min to max (24 per set; 390 at 100 %) | 0.174 to 0.578 | 0.222 to 0.576 | 0.184 to 0.545 | 0.184 to 0.590 |
| levels right, min to max (32 per set; 468 at 100 %) | -0.023 to 0.422 | -0.001 to 0.415 | -0.008 to 0.369 | -0.006 to 0.432 |
| joint-fit residual, dB | -29.4 | -34.3 | -35.9 | -31.7 |

Only the rate depends on Macro. FITTED: `0.0508 + 0.00565 (e^m - 1)/(e - 1)`, residuals
-0.0001, -0.00006, +0.00005, 0; a parabola `0.05105 + 0.0022 m + 0.0032 m^2` fits as
well. The value at Macro 0, 0.0508 to 0.0511, is an extrapolation; `tide_verification`
counted 0.0508 and 0.0509 at 15 % (law 0.0513). Below 25 % nothing was measured here
(the voice law the tracker needs is not valid there); at 25 % the two outputs disagree
by 0.0009 and the fit is three times worse than above. Grid, bounds and hold do not
change with Macro within the windows of the table. What a Macro change during playback
does is OPEN; `TidePhaseStream` integrates the rate (INFERRED).

**Host rate.** MEASURED. 44.1 kHz, 8 x 40 s: rate 0.05650; admissible cell 0.96 to 1.04
(left) and 0.96 to 1.03 (right) of nominal; a cell scaled by 48/44.1 costs 2.7 and 13
times more; levels 0.186 to 0.558 and 0.011 to 0.434. 96 kHz, 8 x 14 s: rate during the
hold 0.0567 +- 0.0006. Rates and grid are fixed in hertz. At 96 kHz the grid itself was
not measured.

**Size, Decay, block size.** MEASURED together: Size 60 %, Decay 2 s, block 128: rate
0.05639, admissible cell 0.98 to 1.05 and 0.96 to 1.01, levels 0.188 to 0.574 and 0.000
to 0.422. Block 32 (8 x 14 s): rate during the hold 0.0562 +- 0.0006. No dependence.

**Warm-up and silence.** MEASURED. After 30 s of silent warm-up the levels, computed with
t counted from the start of the instance, lie between 0.189 and 0.580 (left) and -0.009
and 0.409 (right): the same bounds. With t counted from the stimulus they would be
shifted by 0.19 cycle. Ramp and noise count from the first processed sample and
run without input. Noise and impulse trains give the same picture: the hold and the
start levels of the impulse-only captures of section 2 agree with the noise sets, and
the rate autocorrelation that `tide.md` read from impulse trains (0.87, 0.35, 0, -0.28
at 0.5, 2, 3.5, 6.5 s) is close to the one of the noise sets (0.82, 0.33, 0.07 and
-0.11 at 3 and 4 s, -0.26 at 6 s). A dependence on the input was not tested beyond that. With knots found without the grid, 18 of 24 right cells anchored at
the instance start hold exactly one knot, 13 of 24 if anchored at the stimulus start
(exploration; coarse, the records are 25 s).

**Side result for the voice filter.** MEASURED. The joint fit is a test of the voice
response. At a 96 kHz host the residual is -33.2 dB with the biquad designed at 44.1 kHz,
-31.8 dB at 48 kHz and -23.1 dB at 96 kHz; at a 48 kHz host -31.7, -30.9 and -22.6 dB.
The voice filter is not designed at the host rate. INFERRED: it runs inside the 44.1 kHz
core (44.1 against 48 kHz is a 0.8 to 1.5 dB difference, not decisive).

## 8. Generator and comparison

`tide_phase.TidePhase(seed, macro)` gives the phase of both outputs at any time, in
seconds, so at any sample rate; `TidePhaseStream` does the same block by block.
`python tide_phase.py` compares the measured sets with ensembles of the same size and
length from the generator (60 ensembles; z = (measured - mean) / sd over ensembles; for
the distributions of increments a two-sample Kolmogorov-Smirnov distance ranked among
generated-against-generated distances). Levels after 16 s, when every instance has left
its hold. Main set, 40 instances of 100 s:

| Descriptor | measured | generated | sd | z or p |
|---|---|---|---|---|
| increment sd, lag 0.5 / 2 / 5 / 10 / 20 s, cycles | 0.0168 / 0.0597 / 0.1165 / 0.1474 / 0.1511 | 0.0166 / 0.0586 / 0.1150 / 0.1467 / 0.1503 | 0.0004 to 0.0036 | +0.5, +1.0, +0.8, +0.2, +0.2 |
| KS distance of increments, same lags | 0.010 / 0.008 / 0.011 / 0.017 / 0.022 | 0.017 / 0.016 / 0.015 / 0.014 / 0.018 | | p 0.97, 1.00, 0.84, 0.29, 0.26 |
| rate of the level, percentiles 1 / 5 / 50 / 95 / 99, cycles/s | -0.098 / -0.053 / 0.000 / 0.050 / 0.092 | -0.090 / -0.050 / 0.000 / 0.050 / 0.091 | 0.005, 0.001, 0.0003, 0.001, 0.004 | -1.7, -2.2, -0.2, 0.0, +0.2 |
| share of time with rate within 0.002 cycles/s of zero | 0.119 | 0.124 | 0.007 | -0.7 |
| rate autocorrelation, lag 0.5 / 2 / 4 / 6 / 12 s | 0.82 / 0.33 / -0.11 / -0.26 / -0.04 | 0.82 / 0.34 / -0.10 / -0.26 / -0.05 | 0.02 to 0.04 | 0.0, -0.4, -0.9, +0.2, +0.5 |
| level spectrum, octaves from 0.02 to 1.28 Hz, dB re total | -2.9 / -4.6 / -9.0 / -19.4 / -28.1 / -37.2 | -2.9 / -4.5 / -9.3 / -19.9 / -28.5 / -38.2 | 0.2 to 2.2 | +0.1, -0.5, +0.6, +1.0, +0.3, +0.4 |
| level sd, left / right | 0.1035 / 0.1115 | 0.1017 / 0.1096 | 0.0025, 0.0022 | +0.7, +0.9 |
| level percentiles 0.5 and 99.5, left; right | 0.194, 0.584; 0.001, 0.425 | 0.193, 0.582; 0.002, 0.424 | 0.002 to 0.003 | +0.7, +0.6; -0.5, +0.7 |
| left-right level correlation | 0.034 | 0.006 | 0.041 | +0.7 |
| left-right start correlation (40 pairs) | 0.67 | 0.49 | 0.11 | +1.6 |
| first 0.01 cycle move, 5 / 50 / 95 %, s | 8.7 / 11.3 / 16.4 | 8.5 / 11.8 / 16.3 | 0.3, 0.3, 1.1 | +0.6, -1.4, +0.1 |

All 54 descriptors: one beyond 2 sd (the 5 % point of the rate, -2.2), none beyond 3 sd,
rms z 0.82; smallest KS p 0.26. On these statistics the generator and the reference cannot be told apart at Macro 100 %.

Small sets (8 x 40 s, 44 descriptors each): rms z 1.03 (Macro 75 %), 1.05 (50 %), 1.09
(25 %), 1.01 (Size/Decay/block), 1.45 (44.1 kHz). At 25 % the one value beyond 3 sd is
the earliest 0.01-cycle move, 6.5 s, which the estimator's larger error produces. In the
44.1 kHz set the right level sits low (mean 0.142 against 0.216 +- 0.024) and large
upward moves are rare (95 % rate 0.028 against 0.051 +- 0.008); the set holds four knots
per curve after the hold, its rate, cell and bounds agree with the main set, and I take
this as a fluctuation. It is not established.

Start pairs (200): correlation 0.493 against 0.502, same half 0.845 against 0.831,
margins uniform; the difference left - right differs (KS p = 0.008), see section 6.

## 9. What changes in the earlier descriptions

- `tide_verification.md` section 7 and the README: the "random offset confined to about
  +-0.20 cycle" has bounds that differ per output (span 0.407 and 0.440); "constant for
  the first 8 s" is the hold before the first knot of a fixed grid; "stretches at the
  nominal rate alternate with episodes" are the flat ends of consecutive raised cosines.
- Rate at Macro 100 %: 0.05645 +- 0.00005 instead of 0.0566 +- 0.0004; the linear law
  `0.0500 + 0.0064 m` is replaced by the curved one (the linear one is 0.0003 high at
  50 and 75 %).
- The level cycle of `io_verification.md` (16 to 19 s) is 1/rate = 17.7 s shifted by the
  level, up to +-3.6 s (left) and +-3.9 s (right).

## 10. What failed and what is not established

- What draws the random numbers and what ties the two start levels (section 6). The
  simple law in the generator is a description; its fine structure is wrong at p = 0.008.
- The deficit of knots with u > 0.8 (section 4): real at about 3 sd, unexplained.
- Why the two level spans differ (0.407 and 0.440) and whether their bounds are round
  numbers; the absolute bounds carry +-0.004 from the voice law.
- Rate below Macro 25 %, and the reaction to a Macro change during playback.
- The grid at 96 kHz; Size, Decay and block size were changed together only.
- A first estimator from band levels of single instances reaches 0.004 cycle per frame
  and has errors common to all instances; it is kept only to start the tracker. An
  attempt to learn the axis correction as a Fourier series is degenerate (it converges
  to a sawtooth with rate 0) and was replaced by the local-rate calibration.
- Wave 1's alternatives are rejected by the data: a second slow oscillator (the level is
  not periodic and has plateaus), a random walk or diffusion (bounded, uniform, white
  targets), a slewed or one-pole sample-and-hold (symmetric cosine eases), a clock-driven
  process (section 3).

## 11. Claims

| # | Claim | Tag | Evidence |
|---|---|---|---|
| 1 | phase = level + rate·t; level is cosine-interpolated value noise on a jittered grid | MEASURED | grid fit 0.0008 cycle rms on 78 of 80 curves of 100 s; cosine within 0.0004 of the mean shape |
| 2 | exactly one knot per cell; cells 7.353 s (0.136 Hz) left, 6.25 s (0.160 Hz) right; origin at the first processed sample | FITTED | admissible cells 7.3475 to 7.3610 s and 6.2440 to 6.2519 s; origin within -0.2 to +0.1 s; duration correlation -0.51, -0.46 |
| 3 | jitter uniform in [0, 1), targets uniform in [0.184, 0.591] and [-0.007, 0.433], all independent | MEASURED | KS p 0.15 to 0.85 per channel; correlations within 0.09; bounds +-0.004 |
| 4 | 20 % fewer knots with u > 0.8 than uniform | MEASURED, cause OPEN | 0.160 +- 0.013 of 1014; fit unbiased on generated curves |
| 5 | level holds its start value until knot 1 (6.25 to 12.5 s right, 7.35 to 14.7 s left) | MEASURED | all curves; first knots 6.43 to 12.33 s and 7.82 to 14.71 s |
| 6 | start levels uniform over the target ranges; left and right tied only at the start | MEASURED | 200 instances; correlation 0.535 and same half 83 % over 312 pairs; later correlation 0.03 +- 0.04 |
| 7 | law of the start tie | OPEN | "same half 0.83" fits correlation, fails the difference at p = 0.008 |
| 8 | voice B = voice A + 1/2 at all times | MEASURED | difference -0.0004 +- 0.0055 cycle, lag 0.4 +- 1.3 ms |
| 9 | rate 0.05645 cycles/s at Macro 100 %; 0.05452, 0.05287, 0.05163 at 75, 50, 25 % | FITTED | grid-cost minimum, outputs agree within 0.00006 (0.0009 at 25 %) |
| 10 | rate(m) = 0.0508 + 0.00565 (e^m - 1)/(e - 1); grid and bounds do not depend on Macro | FITTED | residuals within 0.0001; cells within 1 to 9 % by Macro value |
| 11 | no dependence on host rate, block size, Size, Decay, warm-up, render speed; runs through silence | MEASURED | sections 3 and 7 |
| 12 | time counts from the first processed sample, warm-up included | MEASURED | level bounds after 30 s of warm-up |
| 13 | instances share no random sequence and are not one curve shifted or scaled | MEASURED | repeated triples at chance level |
| 14 | generator indistinguishable from 40 instances on 54 statistics | MEASURED | rms z 0.82, none beyond 3 sd, KS p from 0.26 |
| 15 | the voice biquad is not designed at the host rate | MEASURED | 96 kHz host: -33.2 dB with a 44.1 kHz design, -23.1 dB with a 96 kHz design |
