# Tide phase generator: independent verification of the packet `tide_phase`

Packet `tide_phase_verification`. Checked: `findings/tide_phase.md`, `tide_phase.py`,
`measure_tide_phase.py`, `tide_structural_data/tide_phase.npz`. Everything below was re-derived
with my own code on fresh captures; the author's analysis functions are not used (only the
delivered generator, to test its predictions). Scripts and intermediate arrays:
`Analyzer/Results/RevOceanCharacterization/work/tide_phase_verification/` (`tpv.py` estimator,
`knots.py` knot descriptions, `sets.py` captures, one script per check).

Tags: MEASURED, FITTED, INFERRED, OPEN. Verdicts: CONFIRMED, REFUTED, PARTLY, UNVERIFIED.

## 0. What was captured

228 captures, 0.97 GB of cache, all with noise of my own (seed 4242, rms 0.1, peak 0.5), every
instance a realisation the author did not use. Macro and Mix read back as requested in every
capture (enforced by `revocean.capture`); all but `macro60` are at **Macro 100 %, Mix 100 %**.

| Set | Instances x length | Settings that differ from the author's main set |
|---|---|---|
| `main` | 10 x 105 s | **warm-up 10 s**, Decay 1.0 s, Size 150 %, block 256 |
| `hold` | 12 x 16 s | warm-up 0, Decay 1.0 s, Size 150 %, block 256 |
| `starts` | 120 x 1.5 s | warm-up 0, Decay 1.0 s, Size 150 %, block 256 |
| `starts_b` | 64 x 1.2 s | warm-up 0, the author's settings (Decay 0.5 s, Size 100 %, block 512) |
| `macro60` | 8 x 40 s | warm-up 0, **Macro 60 %** (not measured by the author) |
| `rate441` | 8 x 40 s | warm-up 0, 44.1 kHz host |
| `rate96` | 6 x 30 s | warm-up 0, **96 kHz host** (the author measured 14 s there, no grid) |

## 1. Verdicts

| # | Claim of the packet | Verdict | Short reason |
|---|---|---|---|
| 1 | phase = level + rate·t; the level is cosine-eased value noise on a jittered grid | CONFIRMED | new instances, own estimator: one-knot-per-cell description 0.00095 cycle rms; mean transition within 0.0006 of a raised cosine, smoothstep off by 0.0039 (9 standard errors) |
| 2 | exactly one knot per cell; cells 7.353 s (left) and 6.25 s (right); origin at the first processed sample | CONFIRMED | grid-free knots: 96 % / 97 % of windows hold one knot (49 to 88 % elsewhere); admissible cells 7.3424 to 7.3568 s and 6.2469 to 6.2503 s; with a 10 s warm-up the origin is the instance start within 0.1 s. 7.35 s is as admissible as 1/0.136 Hz |
| 3 | jitter uniform in [0, 1); targets uniform in [0.184, 0.591] and [-0.007, 0.433]; all independent; spans differ | CONFIRMED | 441 knots: KS p 0.70; 276 targets: 0.186 to 0.593 and -0.008 to 0.426, KS p 0.94 / 0.37; correlations within 0.14 +- 0.09 |
| 4 | knots late in the cell (u > 0.8) are 20 % rarer | UNVERIFIED, not reproduced | share 0.211 +- 0.019 in my 441 knots (claim 0.160 +- 0.013); the uniform law of the generator is right |
| 5 | the level holds its start value until knot 1 (cell 1 of the grid) | CONFIRMED | 68 curves in four sets: constant to 0.0005 to 0.002 cycle rms (median per set; largest 0.006) before the first knot; earliest 0.01-cycle move at 7.66 s; first grid-free knot inside cell 1 in 63 of 68 |
| 6 | start levels uniform over the target ranges; the two outputs tied at the start and only there | PARTLY | margins and later independence confirmed (196 instances). The strength of the tie is **not a constant**: correlation 0.68 +- 0.04 and same half 0.92 +- 0.02 in my batches against 0.535 and 0.83 |
| 7 | law of the start tie and source of the randomness are open | CONFIRMED as open | the tie depends on the batch, not on the settings (section 6) |
| 8 | voice B = voice A + 1/2 at all times | CONFIRMED | -0.0001 +- 0.0041 cycle over 3265 readings; lag -0.2 +- 1.3 ms |
| 9 | rate 0.05645 +- 0.00005 cycles/s at Macro 100 % | PARTLY | 0.0564 is right; the stated error is not. My estimates lie between 0.0561 and 0.0568 by method; **0.0564 +- 0.0002** |
| 10 | rate(m) = 0.0508 + 0.00565 (e^m - 1)/(e - 1); only the rate depends on Macro | CONFIRMED at one new point | Macro 60 %: 0.05354 +- 0.00005, law 0.05350; grid and bounds unchanged. 75, 50, 25 % not re-measured; below 25 % UNVERIFIED |
| 11 | no dependence on host rate, block size, Size, Decay, warm-up, render speed; runs through silence | CONFIRMED, extended | Size 150 % + Decay 1 s + block 256 + warm-up 10 s reproduce grid, bounds and rate; **grid at 96 kHz measured**: within 3 % of nominal; 44.1 kHz anomaly of the author not reproduced. Controls still not separated one by one; render speed not varied by me |
| 12 | time counts from the first processed sample, warm-up included | CONFIRMED | grid origin 0 +- 0.1 s of instance time after a 10 s warm-up (origin at the stimulus start costs 40 to 100 times more); ramp origin within 0.04 s modulo half a cycle |
| 13 | instances share no sequence | CONFIRMED | repeated level triples, mine against the author's and mine against mine: 3 found, 2.4 expected |
| 14 | the generator cannot be told apart from measured trajectories | CONFIRMED | my 51 statistics: rms z 1.10 on my 10 instances, 0.84 on the author's 40, none beyond 3 sd. The start-pair law is the exception (claim 6) |
| 15 | the voice biquad is not designed at the host rate | CONFIRMED | 96 kHz host: -35.3 dB (44.1 kHz design) against -27.9 dB (96 kHz design). "Inside the 44.1 kHz core" stays INFERRED, with better support at Macro 60 % |
| - | the estimator is accurate to a few thousandths of a cycle | PARTLY | my estimator on the author's 200 start captures agrees with the author's start levels to 0.001 (largest 0.0023); both use the same voice law; I did not repeat the impulse-probe comparison |
| - | regenerating script reproduces the data file | CONFIRMED | byte-identical file (sha256 `876c25bda1804493...`), 135 s, no new capture |

New, not in the packet: **during the first 0.6 s of an instance the voice law is that of a lower
Macro** (section 5). No statement of the packet contradicts README.md or the verified wave-1
findings. Neither delivered file touches a holdout loader; no holdout applies.

## 2. Method

**Estimator** (`tpv.py`). Same physical model as the author's, the one the reference dictates
(per frame and bin `Y_i = G(phi_i) E + G(phi_i + 1/2) L`), written independently and solved
differently: 4096-sample frames, bins 300 Hz to 16 kHz, closed-form E and L, then a grid search
per instance on a 4000-point phase table with a parabola (no gradient steps); start from band
levels modulo half a cycle with the flips found by enumeration. Residual of the joint model:
-33.4 dB (`main`), -32.5 dB (`hold`); frame noise 0.0002 cycle. MEASURED.

**Axis.** Local slope of the raw phase against raw phase during the start hold of the `hold` set
(period 1/2, 20 bins); correction -0.002 to +0.004 cycle. After it the hold is straight to
0.0004 cycle (median per curve, largest 0.002). The half-cycle convention is fixed by the centres
of the level ranges of `tide_verification.md`. Weak spot of my estimator: a kink where the cut-off
passes 12 kHz (phase 0.15 and 0.65, the knee of the Q table) gives errors of 0.003 to 0.006 cycle
when the phase lingers there; a free-knot fit with a low penalty then invents knots.

**Cross-check of the two estimators.** My tracker on the author's 200 cached start captures
(nothing rendered): my start level minus the author's is +0.0010 / +0.0007 cycle (left / right),
sd 0.0004 / 0.0005, largest 0.0023. MEASURED. Differences between my results and the author's
are therefore differences between captures.

**Knots** (`knots.py`). (a) Grid-free: a dynamic programme over free knot times with a penalty
per knot, then least squares on times and values. On 12 curves from the delivered generator with
measurement-like errors it finds 187 of 188 knots with 1 spurious (penalty 1e-4), time error
0.017 s median. (b) One knot per cell of a trial grid (period, origin), no penalty, used for the
scans; (c) a free least-squares refinement started from (b), which may move knots out of their cells.

## 3. Structure, grid and time origin (claims 1, 2, 12)

All on `main`: 20 curves of 105 s, instance time 10.04 to 115.2 s.

**Shape.** MEASURED. Whole-curve rms of the refined description: raised cosine 0.00095 cycle
(median; largest 0.0016), smoothstep 0.00123, two parabolas 0.0021, quintic 0.0026, straight line
0.0056. Mean of 187 transitions (step above 0.08 cycle, longer than 2 s), each normalised with
knots fitted for the shape under test: raised cosine off by 0.0006 rms (largest 0.0011, standard
error of a point 0.0004), smoothstep 0.0039 (largest 0.0057), parabolas 0.012, quintic 0.015.
Sets at Macro 60 %, 44.1 and 96 kHz rank the shapes the same way.

**One knot per cell, without a grid in the fit.** MEASURED, penalty 1e-3: 142 knots on the left
(143 expected for one per cell), 167 on the right (168). Share of windows that hold exactly one knot:

| window | left | right |
|---|---|---|
| nominal length, origin at the first processed sample | 0.962 | 0.969 |
| length 0.7 to 0.98 and 1.02 to 1.3 of nominal | 0.53 to 0.70 | 0.58 to 0.77 |
| nominal length, origin moved by 0.1 to 0.9 of a cell | 0.49 to 0.81 | 0.51 to 0.88 |

**Cost of the one-knot-per-cell description**, relative to the nominal grid (left / right):

| period / nominal | 0.98 | 0.99 | 0.995 | 0.998 | 1.002 | 1.005 | 1.01 | 1.02 |
|---|---|---|---|---|---|---|---|---|
| cost | 15 / 16 | 2.9 / 3.8 | 1.4 / 1.7 | 1.00 / 1.09 | 1.11 / 1.06 | 1.8 / 1.6 | 5.6 / 4.8 | 31 / 23 |

| origin, s of instance time | -1 | -0.5 | -0.2 | -0.1 | +0.1 | +0.2 | +0.5 | +1 | +-3 |
|---|---|---|---|---|---|---|---|---|---|
| cost | 7.4 / 6.5 | 2.2 / 2.4 | 1.10 / 1.10 | 1.00 / 1.02 | 1.05 / 1.04 | 1.31 / 1.17 | 3.3 / 2.5 | 13 / 11 | 92 to 99 |

The fine scan is flat within 3 % for 7.338 to 7.360 s and 6.244 to 6.256 s, and for origins of
-0.15 to +0.10 s. The author's origin window (-0.2 to +0.1 s) was measured without warm-up; mine
is measured **10 s after the first processed sample**: an origin at the first stimulus sample is
40 to 100 times worse. The ramp counts from the same instant: with t from the first processed
sample my levels span 0.185 to 0.593 (left) and -0.009 to 0.429 (right), the claimed ranges within
0.004; an origin error of 0.1 s would shift them by 0.006. MEASURED. The generator therefore ran
through 10 s of silence.

**Cell from the refined knots.** FITTED. After the free refinement 311 of 316 knots are still alone
in their cells (the 5 others are 0.08 to 3.4 s outside, at record edges or at the estimator kink).
Knots inside their cells allow **7.3424 to 7.3568 s** (left) and **6.2469 to 6.2503 s** (right);
with the author's bounds the intersections are 7.3475 to 7.3568 s and 6.2469 to 6.2503 s. 6.25 s
(0.160 Hz) is pinned to 0.03 %. For the left output 1/0.136 Hz = 7.3529 s and the round 7.35 s
both lie inside and cost the same (ratio 1.000): which of the two is meant is OPEN and inaudible.

**Jittered grid.** MEASURED. Successive durations correlate -0.56 (left) and -0.49 (right);
durations in units of the cell follow the triangular law of two independent uniform jitters
(256 inner segments, KS p 0.96, mean 1.012, sd 0.410 against 0.408).

## 4. Jitter, targets, bounds (claims 3, 4, 13)

MEASURED on the inner knots of `main` (124 left, 152 right); jitter also on all sets (441 knots).

- Jitter: uniform (KS p 0.47 left, 0.76 right, 0.70 pooled); successive jitters -0.01, -0.01.
- **Late knots.** Share with u > 0.8: 0.203 in `main`, **0.211 +- 0.019** in all my sets
  (deciles 50 48 38 38 37 53 49 35 56 37). The author's deficit (0.160 +- 0.013) is not there;
  the two samples differ by 2.2 sd and pooled give 0.177 +- 0.010. I take the deficit as a
  fluctuation or as a property of the author's bounded fit; it is not a property a model needs.
- Targets: left 0.1858 to 0.5934, right -0.0078 to 0.4263; uniform on the claimed ranges (KS p
  0.94, 0.37); mean 0.392 and 0.215 (claimed centres 0.3875, 0.213); successive targets -0.14 +-
  0.09 and +0.07 +- 0.08; target against jitter -0.12, +0.08. Level curves reach 0.1849 to 0.5931
  and -0.0086 to 0.4287. The claimed bounds hold within their stated +-0.004; my left upper bound
  is 0.002 higher (0.593), my right upper bound was not reached (152 targets leave 0.003 free).
  Spans 0.408 and 0.434 to 0.437: they differ, as claimed.
- Left and right: knots of one output within 0.5 s of a knot of the other in 18 % of the cases,
  19 % between different instances; level correlation 0.03 +- 0.08.
- No shared sequence: level triples within 0.006 cycle, my instances against the author's 40:
  3 found, 2.1 expected; among mine 0 found, 0.3 expected. Jitter triples within 0.02: 10 and 1
  found, 6.1 and 0.8 expected.

## 5. The hold, and the first 0.6 s (claim 5)

**Hold.** MEASURED on the four warm-up-0 sets (34 instances, 68 curves, three host rates, Macro
100 and 60 %). From 1 s to its first knot the level stays at its start value to 0.0005 to 0.002
cycle rms (median per set); the largest excursions (0.006) sit at the estimator kink. Grid-free first knots
(penalty 1e-3): 63 of 68 inside cell 1; of the others one is spurious (5.3 s, at the kink) and
four are 0.4 to 1.6 s late because the first step is small and the knot is missed (found inside
cell 1 with penalty 1e-4). First move of 0.01 cycle: earliest 7.66 s (right) and 9.9 s (left);
5 / 50 / 95 % at **8.4 / 11.4 / 16.3 s** (claim 8.7 / 11.3 / 16.4; generator 8.5 / 11.8 / 16.3).

**Start-up ramp, new.** MEASURED, coarse. In every warm-up-0 set, mine and the author's cached one,
a tracker that uses the Macro 100 % voice law reads a level that starts 0.05 to 0.11 cycle low and
settles within 0.8 s, while the joint residual is -15 to -21 dB instead of -33 dB. The level does not
move: the voice law does. Solving each early frame with voice tables of other Macro values (32
instances of `starts_b`, steps of 0.05):

| frame centre, s | 0.06 | 0.15 | 0.24 | 0.32 | 0.41 | 0.49 | 0.58 | 0.64 | 0.81 |
|---|---|---|---|---|---|---|---|---|---|
| best Macro of the voice law (left, right) | 0.65, 0.75 | 0.70 | 0.80 | 0.85 | 0.90 | 0.95 | 0.95 | 1.00 | 1.00 |
| residual with it, dB (left) | -22 | -24 | -25 | -28 | -30 | -32 | -29 | -30 | -36 |

Where the trial value fits (0.28 s with 0.80, 0.41 s with 0.90, 0.53 s with 0.95) the level read with
it equals the settled level within 0.0016 cycle (median; rms 0.001 to 0.003). So the level holds its
start value from at least 0.28 s, and before 0.8 s nothing read with the Macro 100 % law is valid.
INFERRED: Macro (or what is derived from it) is smoothed with a time constant of about 0.2 s and a
fresh instance starts from a lower value, presumably the preset's; the start value is not resolved.
This is the "start-up effect shorter than 0.8 s" of `tide_verification.md`. It does not change the
author's numbers (they use data after 0.35 to 0.8 s; the largest effect on a start level is 0.002),
but it is the only evidence so far on what a Macro change does: it is not applied at once.

## 6. Start levels and the tie between the outputs (claims 6, 7)

MEASURED, 196 instances (`starts` 120, `hold` 12, `starts_b` 64), levels read after 0.8 to 1.0 s.

- Margins: left 0.189 to 0.595, right -0.006 to 0.431; uniform on the claimed ranges (KS p 0.88,
  0.29). CONFIRMED.
- Tie: correlation **0.68 +- 0.04**, both in the same half of their ranges in **0.923 +- 0.019**
  of the instances (`starts`: 0.74, 0.917; `starts_b`: 0.61 +- 0.08, 0.938). The packet has 0.535
  and 0.833 over 312 pairs, and its 200-instance set reads 0.493 with my estimator as well. The two
  bodies of data differ: correlation by 0.15 +- 0.06, same-half share by 0.09 +- 0.03, and the share
  of pairs more than 0.4 of the span apart is 2.6 % (5 of 196) against 11.8 % (32 of 272).
- The difference is not a matter of settings: `starts_b` uses the author's settings and behaves
  like my other batch. It is a matter of when and how the batch was launched. INFERRED: the two
  start values come from sources that are close to each other at start-up by an amount that depends
  on launch conditions. This fits the one clustered batch of `tide_verification` that the author
  could not reproduce. What the source is remains OPEN.
- The start law of the generator ("same half 0.83") fails the left-minus-right distribution on my
  data too (KS p 0.012 over the 196 pairs): my pairs are closer together than the law (sd of the
  normalised difference 0.22 against 0.29; 2.6 % against 15 % more than 0.4 apart).

For a model: each start level uniform over its range, the two positively correlated (0.5 to 0.7).
The exact joint law is not a constant of the reference.

## 7. Voice B (claim 8)

MEASURED on `main`, every sixth frame, both phases solved independently (`Joint.solve_two`, a
two-dimensional grid search): phase_B - phase_A - 1/2 = -0.0001 cycle, sd 0.0044 (8160 readings);
-0.0001, sd 0.0041 over the 3265 readings with both gains between 0.29 and 1.12. The scatter is a
bias of the voice law, antisymmetric under a half-cycle shift (+0.004 at phase 0.15 to 0.35, -0.004
at 0.65 to 0.85), as the author describes. A plain regression on the rate of the level gives a
lag of -6 +- 2 ms; it comes from that bias. With the bias removed per phase bin the lag of voice B
behind voice A is **-0.2 +- 1.3 ms** (bootstrap over instances) and the difference while the level
moves faster than 0.05 cycles/s is +0.0001. One phase per output. CONFIRMED.

## 8. Rate and Macro (claims 9, 10)

Estimates of the rate at Macro 100 %, cycles/s:

| Method | Data | Rate |
|---|---|---|
| minimum of the one-knot-per-cell cost | `main` (no hold in the record) | 0.05628 +- 0.00013 (left 0.05607, right 0.05661) |
| same | `rate441`, `rate96` (records begin with the hold) | 0.05650 +- 0.00005, 0.05649 +- 0.00007 |
| same, my programme on the author's 80 curves | author's `main_phase` | 0.05643 +- 0.00002 (left 0.05646, right 0.05637) |
| plain slope of the phase, no model | `main`, 20 curves | 0.05614 +- 0.00025 |
| plain slope of the phase, no model | author's 80 curves | 0.05622 +- 0.00014 |
| start hold, half-cycle closure | `hold`, `rate441`, `rate96` | 0.05672, 0.05683, 0.05655 (bootstrap +-0.00004 to 0.00011) |

Errors are bootstrap over curves and do not contain what the methods share. Three things follow.

- The author's value is reproduced on the author's trajectories by my programme. CONFIRMED as a
  number of that data set.
- Its stated uncertainty, +-0.00005, is the statistical error of one method. On new instances
  without a hold the same method gives 0.05628 +- 0.00013, and the model-free slope of all 100
  curves is 0.0562 +- 0.00012. FITTED: **0.0564 +- 0.0002 cycles/s, period 17.7 s**. The round
  values 0.0565 and 0.05625 both lie inside; 0.0566 (wave 1) is at the edge. PARTLY.
- Hold rates read 0.0001 to 0.0004 above the long-run rate in every data set (also the author's
  0.05658 and the 0.0566 to 0.0570 of `tide_verification`), and left reads above right (mine:
  0.05686 +- 0.00007 against 0.05651 +- 0.00008). A hold covers less than one cycle, so its rate
  depends on the axis calibration: an axis error of 0.001 cycle with a period of one cycle is
  enough. INFERRED: an artefact; a real difference of 0.0003 between the hold and later is not
  excluded.

**Macro 60 %**, a value the author did not measure (8 x 40 s): cost minimum 0.05355 (left),
0.05352 (right), together **0.05354 +- 0.00005**; hold closure 0.05340. The law gives 0.05350
(residual +0.00004), the author's parabola 0.05352, the linear law of wave 1 0.05384. Cells
admissible at 0.98 to 1.02 (left) and 0.97 to 1.02 (right) of nominal; targets 0.180 to 0.568 (19)
and -0.002 to 0.426 (30); first knots inside cell 1 in 16 of 16 curves. CONFIRMED at this point.
Not re-measured: 75, 50, 25 %. Below 25 % the law is an extrapolation (UNVERIFIED).
Observation: 0.0508 is 0.9 x 0.05645 to four digits, so the law may be
`rate(1) (0.9 + 0.1 (e^m - 1)/(e - 1))`; the data cannot tell. OPEN.

## 9. Host rate, Size, Decay, block size (claims 11, 15)

- **Size 150 %, Decay 1.0 s, block 256, warm-up 10 s** (`main`): cells, origin, bounds, shape,
  voice B and rate as above. With the author's Size 60 % / Decay 2 s / block 128 this covers both
  sides of the defaults. The controls are still changed together only. CONFIRMED.
- **44.1 kHz host** (new instances): rate 0.05650; cost flat for cells of 0.99 to 1.01 (left) and
  0.98 to 1.01 (right) of nominal, a cell scaled by 48/44.1 or 44.1/48 costs 6 to 30 times more;
  targets 0.200 to 0.593 (20) and -0.002 to 0.429 (28). The right level, low in the author's
  44.1 kHz set (mean 0.142, 3 sd), is normal here: mean of the targets 0.212, of the level after
  16 s 0.219. The author's deviation was a fluctuation. MEASURED.
- **96 kHz host** (6 x 30 s): rate 0.05649 +- 0.00007; right cell 0.98 to 1.02 of nominal (0.97
  costs 1.2, 1.06 costs 5.7); left cell 0.97 to 1.03, loosely (three cells per curve; 0.94 and
  1.06 cost 1.2); levels 0.247 to 0.533 and -0.012 to 0.424; first knots inside cell 1 in 11 of 12.
  The grid is fixed in seconds at 96 kHz as well. MEASURED, coarse.
- Render speed: not varied by me. A generator on the wall clock would not move during a render of
  a few seconds, and all sets rendered under whatever load there was give one grid. UNVERIFIED as
  a separate test.
- **Voice biquad** (median residual of my joint fit, dB, by the rate at which the biquad is designed
  and evaluated):

| Host, Macro | 44.1 kHz | 48 kHz | 88.2 kHz | 96 kHz |
|---|---|---|---|---|
| 96 kHz, 100 % | -35.3 | -35.7 | -28.3 | -27.9 |
| 44.1 kHz, 100 % | -34.6 | -35.0 | -28.4 | -28.0 |
| 48 kHz, 100 % | -33.4 | -33.3 | -25.8 | -25.5 |
| 48 kHz, 60 % | -37.0 | -33.5 | -24.2 | -23.8 |

  Not designed at a 96 kHz host rate: CONFIRMED (7 dB). At Macro 100 % a 44.1 and a 48 kHz
  design cannot be separated: at a 44.1 kHz host the 48 kHz design even scores 0.4 dB better, so
  the author's 0.8 dB means nothing. At Macro 60 %, where the cut-off stays high, the 44.1 kHz
  design wins by 3.5 dB at a 48 kHz host. "Inside the 44.1 kHz core" stays INFERRED, now with
  this support.

## 10. The generator against measured trajectories (claim 14)

`compare_gen.py`: 51 statistics of my choice (quantiles of the level, time near the bounds; sd,
skewness, kurtosis and tails of the 1 s rate; reversals of the smoothed rate; autocorrelation at
3, 7, 15 s; changes over 0.25, 3, 8 s; third differences; three left-right correlations), measured
set against 120 generated ensembles of the same size on the same time axis, generated curves with
a measurement-like error added.

| Measured set | rms z | beyond 2 sd | beyond 3 sd | largest |
|---|---|---|---|---|
| my `main`, 10 x 105 s, my estimator | 1.10 | 3 | 0 | skewness of the right 1 s rate, -2.7 |
| the author's main set, 40 x 84 s, the author's estimator | 0.84 | 2 | 0 | reversals of the left rate, +2.4 |

The skewness is not in the author's set (+0.03). Reversals of the smoothed left rate are high in
both sets (+2.4 and +1.9 sd; 11.5 and 12.2 per 100 s against 10.4); the statistic counts wiggles
of the estimator on flat stretches, and I do not read a property of the reference into it.
CONFIRMED on these statistics. The author's own comparison re-run prints the reported figures
(54 descriptors, one beyond 2 sd, rms z 0.82, smallest KS p 0.26; small sets 1.03, 1.05, 1.09,
1.45, 1.01). Checks of the module: `TidePhaseStream` equals `TidePhase` to 4e-16 at a fixed
Macro; the result does not depend on the order of the queries; 800 generated curves of 300 s have
one knot per cell, a hold to the end of cell 0 and levels inside the bounds.

## 11. What a product should know

1. Use the structure as delivered. The numbers that carry it are confirmed on new instances at
   other settings: cells 7.353 s and 6.25 s from the first processed sample, raised cosine,
   uniform jitter, uniform targets in the stated bounds (+-0.004), voice B half a cycle away.
2. Rate at Macro 100 %: 0.0564 +- 0.0002 cycles/s. The fourth digit of 0.05645 is not established.
3. Ignore the late-knot deficit.
4. The start tie is not a number of the reference; any positive correlation of 0.5 to 0.7 between
   the two start levels is as right as the delivered 0.83 rule.
5. The first 0.6 s of a fresh instance run through lower Macro values. A model need not copy it,
   but captures with a short warm-up must not be compared before 0.8 s, and Macro is evidently
   smoothed (about 0.2 s): `TidePhaseStream`, which follows Macro at once, is untested there.
6. Nobody has measured: Macro moved during playback, Macro below 25 %, what a reset or a
   transport restart does to the grid and the ramp (every capture is a fresh instance), Freeze.

## 12. Limits of this verification

- My estimator and the author's share the voice law of `tide_verification.md`; absolute bounds
  and the half-cycle convention rest on it. I did not repeat the impulse-probe comparison.
- 75, 50 and 25 % Macro, the 30 s warm-up, block 32 and the background-priority renders were not
  repeated. Size, Decay and block size were again changed together.
- The start-up ramp is read in steps of 0.05 Macro on frames of 85 ms; its start value and exact
  law are not resolved.
- `main` has 10 instances: its statistics have errors two times those of the author's 40.
- Budget: 228 captures, 0.97 GB. The author used 2.6 GB against the 1 GB guideline, as reported.
