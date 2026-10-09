# Packet first_order_a: the first pass, resolved from one arrival outwards

Reference: Arturia Rev OCEAN 1.0.0.5848, Tide mode, neutral baseline, 48 kHz.
Every number below comes from audio rendered through `revocean.capture`.

Tags: MEASURED (read off captures), FITTED (model constant, residual given),
INFERRED (reasoned, not tested), OPEN.

## 0. Settings of this packet (answer to the owner's question)

- **Mix is 100 % in every capture** (MEASURED: the plug-in reads back `Mix = 100`).
- **Macro, the knob that is "Tide" in this mode, is 0 % in this packet**, not
  100 % (read-back `Macro = 0.000`). That is the packet's assignment: Macro 0
  is deterministic and linear, which is what allows an exact model.
- Size 100 %, Decay 0.5 s, Width 100 %, everything else neutral (`revocean.BASELINE`).
- One side check was made at **Macro 100 %, Mix 100 %** (section 8): the model
  of this packet does not describe that state. The first sound comes about
  540 samples later, two instances differ at -1.5 dB, and the Macro 0 first
  pass, shifted by any lag, correlates only 0.25 to 0.52 with the Macro 100 %
  response. Nothing below may be read as a statement about Tide 100 %.

## 1. Result

A structural model predicts both outputs for an impulse on either input at any
time, raw samples [1200, 2500):

| data | overall null |
|---|---|
| grid times no fit has seen (49 times, 4 paths) | -62.2 dB |
| own captures off the grid and off the 160-sample conversion period (2 x 59 times) | -63.7 dB, -63.8 dB |
| **locked holdout, `score_first_order.py`** | **-63.05 dB** (paths -63.4 / -63.9 / -63.1 / -62.5, worst response -60.2 dB) |

The model (`first_order_a.py`), with `r = 44100/48000 = 147/160`:

```
host impulse at sample t
 -> ideal conversion to a 44.1 kHz lattice (lattice time m0 = t r)
 -> pre kernel u (89 taps on the lattice)
 -> two independent networks (left, right) of delay lines; line n of a network
    is read with linear interpolation at
        m - 187 - P_n - A sin(phi_n(m)),        A = 0.88 ms = 38.808 lattice samples
    P_n prime:  left  1031 1097 1187 1289 1423 1583 1783 2027 2333 (2699 3163)
                right 1039 1109 1193 1301 1429 1597 1787 2039 2333 (2707 3163)
    phi_n: float32 accumulator, phi += float32(2 pi 0.6 / 44100) once per lattice
    sample, minus float32(2 pi) when phi >= 2 pi
 -> second pass: the output of line a read again by line b of the same network
    at m - P_b - A sin(phi_b(m)), no further delay
 -> ideal conversion to 48 kHz (band limit 22.05 kHz)
 -> post kernel q (48 kHz FIR, lags -48 ... 1000)
out_o = q * convert( sum_n g[o,n,i] first_n + sum_(a,b) s[o,a,b,i] second_ab )
```

The answer to README item 7 (the crux): the delay network runs at **44.1 kHz
whatever the host rate**, and its lines are read with **linear interpolation on
that 44.1 kHz lattice**. The high-frequency content of an arrival follows the
fractional part of the delay counted in 44.1 kHz samples, not in 48 kHz samples.

## 2. One arrival resolved

Arrival studied: the L->L line centred near raw 1752 (line L5, prime 1423), on
the 980 grid responses, where it is at least 85 samples from its neighbours.

1. MEASURED. The gain of the arrival at 8, 14, 17 and 20 kHz relative to its
   low-frequency gain moves as a one-parameter family: 8 kHz 0.865 to 1.015,
   14 kHz 0.578 to 1.037, 17 kHz 0.396 to 1.08, 20 kHz 0.045 to 0.255. It is
   unrelated to the fractional part of the position in 48 kHz samples.
2. The dips are deeper than linear interpolation at 48 kHz allows (0.609 at
   14 kHz, 0.442 at 17 kHz), and match `|cos(pi f / F)|` for F near 44 kHz.
   FITTED: the gains at 14 and 17 kHz follow
   `sqrt(1 - 2 d (1 - d)(1 - cos(2 pi f / F)))` with `d = frac(p F/48000 + c)`;
   rms misfit over 567 arrivals: **0.008 at F = 44100**, 0.060 at 44000, 0.065
   at 44200, 0.12 at 43900 and 44300, 0.30 or more at 40, 42, 43, 45, 46, 48,
   50 kHz.
3. A window around the arrival, aligned to integer 48 kHz samples, has singular
   values 1, 0.34, 0.12, 0.06, 0.016: it is not a two-tap mixture on the host
   grid. On the 44.1 kHz lattice it is one two-tap read (section 5).
4. Rejected for this arrival: two fractional reads in series, satellites with
   their own motion, a time-varying filter. One interpolated read on the
   internal lattice plus fixed kernels reaches the nulls of section 5.

## 3. Lines

- FITTED, then exact. With a free sinusoidal law per line the centres came out
  at integer - 0.017 lattice samples for all 14 lines fitted; with the exact
  LFO of section 4 the centres are integers within 2e-4 samples:
  left 1218, 1284, 1374, 1476, 1610, 1770, 1970 (2214, 2520), right 1226, 1296,
  1380, 1488, 1616, 1784, 1974 (2226, 2520).
- INFERRED, strongly supported. Subtracting one common latency of **187**
  lattice samples (4.240 ms, 203.54 host samples) makes all 18 values prime.
  187 is the only offset in 100..299 that does so; no other offset makes even
  13 of the 18 prime. The latency is confirmed by the second passes, which sit
  at `187 + P_a + P_b` with no free position parameter (section 6).
- MEASURED. Each output carries only the lines of its own network. The left
  and right networks do not exchange signal: every cross-network second pass
  that was fitted has |gain| below 4e-4 (within a network 0.10 to 0.26).
- MEASURED (projection on the model arrival, scratch script `s52`). Lines 10
  and 11 exist: left 2699 and 3163, right 2707 and 3163. The prime explains
  0.70 to 0.84 of the local energy, the neighbouring integers 0.08 or less.
  The ninth and the eleventh line have the same length in both networks.
- OPEN. Whether a network has more than 11 lines. Two hints at 16: the
  second-pass matrix entries all have magnitude close to 1/4 (section 6), and
  the input law of section 6 reaches its floor at line 16.

## 4. Modulation: a float32 phase accumulator

- MEASURED. Per-arrival delays (free delay per arrival, global kernel, 980
  responses x 14 lines) deviate from the best sinusoid by a fixed curve of the
  LFO phase, the same for all 14 lines within 1e-3 samples: -0.025 at phase 0,
  +0.026 at 2.1 rad, -0.032 at 3.8 rad, +0.030 at 5.0 rad, with kinks near 2 rad
  and 4 rad. With one kernel after the lines, a sinusoid of 0.599878 Hz with a
  free centre and phase per line and a free common depth cannot do better than
  **-30.1 dB**; free delays reach -48.7 dB.
- The kinks sit at powers of two of the phase in radians. Hypothesis: the phase
  is a single-precision number in radians, advanced by
  `float32(2 pi 0.6 / 44100) = 8.548552e-05` per 44.1 kHz sample and reduced by
  `float32(2 pi)` when it reaches it. Rounding makes the phase advance by 179/179.27
  of the increment between 4 and 2 pi and by 359/358.54 between 2 and 4.
- MEASURED against that hypothesis: the emulated accumulator has a mean period
  of 73514.95 samples, 0.5998780 Hz; the free fit gave 0.5998781 Hz. An
  accumulator in cycles (0..1) would give 0.5998056 Hz and is rejected.
  The nominal rate is therefore **0.6 Hz**; the 203 ppm deficit is rounding.
- FITTED with the emulated accumulator: per-arrival delay residual 0.0014 to
  0.0033 samples rms (robust 0.0002), depth 38.80795 +- 0.00014 samples for 14
  lines. Round value **0.88 ms = 38.808 samples**: holdout-independent
  validation -62.35 dB with 38.808, -62.31 dB with 38.80795, -40.8 dB with
  38.80, -37.3 dB with 38.82.
- One accumulator per line (the deviation curve is a function of each line's
  own phase). FITTED start phases: lines ordered L1, R1, L2, R2, ... start at
  `-q * 1.3163744 rad` (q = 0 for L1), 13 measured steps between 1.316366 and
  1.316378. No round value found: 5/24 cycle gives +0.8 dB, 0.21 cycle -1.9 dB,
  1 - sqrt(5/8) cycle -15 dB, against -62.35 dB.
- **Time origin** (the README's open point). MEASURED: the accumulators start
  when processing starts and count 44.1 kHz samples. With warm-ups of 3, 7.5,
  9, 10, 10.25 and 20 s the same model nulls at -63.0 to -63.6 dB when the
  number of steps taken at lattice time 0 is `warm-up * 44100 - 116`; one step
  more or less gives -48.5 dB, and using the 10 s origin for another warm-up
  gives +3 dB. A warm-up of 1 s was rejected by the capture library (two
  renders differ). Host block boundaries play no role (the lead's block-size
  test, and the per-sample accumulator).
- FITTED. With that origin the phase of L1 at the start is 4.2e-6 rad above a
  whole turn. Forcing exactly 0 costs 0.4 dB (-61.95 against -62.35 dB).
  INFERRED: L1 starts at phase 0, and 116 = 187 - 71 would mean that 71 of the
  187 samples of latency lie before the line read. Not tested independently.
- The waveform is an exact sine of the accumulator phase at this precision.
  The delay is evaluated at the time of the read, once per lattice sample.

## 5. The fixed part: two kernels and why both are needed

- With the exact LFO and one kernel after the lines, the null on [1200, 2300)
  (first passes of lines 1 to 7) is **-50.7 dB**, and the error depends on the
  speed of the delay: -65 dB when |dL/dm| < 0.5e-3, -48.5 dB when it exceeds
  2.5e-3. It lies between 16 and 20.5 kHz (95 % of the residual energy); below
  16 kHz the residual is under -62 dB.
- A kernel proportional to the slope (first-order Doppler) gains 0.5 dB only.
  The same kernel placed entirely before the lines gives -46.5 dB.
- FITTED: a kernel before the lines (on the lattice) and one after them, by
  joint Gauss-Newton: **-64.9 dB** on the fitted rows (-64.5 dB on rows held
  back in the exploratory run of the same fit). Each ringing sample of the band edge is then read with the delay of
  its own instant, which a single kernel cannot express.
- The split is weakly determined: only the motion of the delays separates the
  kernels. The stored pre kernel is the smallest one (ridge 1e-6 of the
  reference energy on its distance from a unit impulse); without the ridge the
  fit drifts to other splits with the same null. Both are low-pass: pre
  -2.4 dB at 19 kHz, -7.6 dB at 20 kHz; post -0.9 dB at 19 kHz, -7.3 dB at
  20 kHz, below -62 dB from 21.5 kHz. Together: within +0.9 dB up to 18 kHz,
  -2.4 dB at 19 kHz, -14 dB at 20 kHz, -47 dB at 21 kHz. INFERRED: these are
  the two rate converters.
- This is the README's "36 samples of pre-ringing": the band edge of the two
  kernels. The post kernel shows no tail of a 20 Hz high-pass: beyond lag 480
  it stays within 1e-5 of its peak (-2e-6 at lag 480), where a first-order
  20 Hz filter would leave 9e-4. Its tail from lag 160 to 400 falls from
  -1.7e-4 to -9e-6 of the peak (a mild low-frequency shaping, +0.3 to +0.7 dB
  below 500 Hz relative to 1 kHz).
- Impulse times off the 160-sample period need no extra constant: the pre
  kernel shifted by a windowed sinc (band limit 22.05 kHz) nulls at -63.7 dB
  on own off-grid captures, the same as on the grid, with no dependence on
  `t mod 160`.

## 6. Gains

Normalised to line L1 from the left input. FITTED on [1200, 2800), nulls of
that fit -54.2 to -55.5 dB (the stretch beyond 2500 holds passes that are not
modelled).

First pass, left network (right network equal within 0.5 %, mirrored):

| line | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| from L | 1.0000 | 1.1568 | 1.1063 | 1.0233 | 0.9406 | 0.8731 | 1.2992 | 1.2325 | 1.2839 |
| from R | 0.5723 | 0.6621 | 0.6332 | 0.5857 | 0.5383 | 0.4997 | 0.0000 | 0.0519 | 0.1130 |

- MEASURED: cross / direct = **0.572329 +- 0.000006** for lines 1 to 6 of both
  networks (12 values), and for every second pass that starts in them (20
  values, 0.57213 to 0.57243).
- Round-value test: `(1 - w)/(1 + w)` with **w = 0.272** is 0.572327. The
  lines 7 to 11 give w = 1.000, 0.919, 0.838, 0.753, 0.675 (lines 10 and 11 by
  projection, 1 to 2 % uncertain): a descent of 0.0809 per line, which would
  reach 0.272 at line 16. INFERRED: each line takes `(1 + w)/2` of its own
  input and `(1 - w)/2` of the other, w = 0.272 for lines 1 to 6 and
  `1 - 0.0809 (n - 7)` from line 7 on. Not checked at another Width.
- The README's "arrivals much weaker in the cross path" are lines 7 and 8 (no
  or 4 % cross feed); what remains near raw 2370 in the cross path is the
  second pass L1 -> L1.

Second pass (left network, from L; a -> b means line a feeds line b):

| | a = 1 | a = 2 | a = 3 | a = 4 |
|---|---|---|---|---|
| b = 1 | +0.1793 | -0.1512 | -0.1314 | +0.1164 |
| b = 2 | +0.2423 | +0.2032 | -0.1762 | (-0.158) |
| b = 3 | +0.2597 | -0.2173 | (+0.189) | |
| b = 4 | +0.2591 | (+0.219) | | |

(values in brackets: fitted, outside the scoring window, not stored)

- Without second passes the scoring window nulls at -24 to -28 dB; with them
  at -62 dB. Their positions are fixed by the first-pass constants.
- FITTED relation. With `gamma_n = 10^(-3 P_n / (0.5 s * 44100))`:
  `s_aa / (g_a gamma_a)` = 0.24767, 0.24768 (left), 0.24746, 0.24763 (right);
  `sqrt(|s_ab s_ba| / (g_a g_b gamma_a gamma_b))` = 0.2484, 0.2486, 0.2469
  (left), 0.2484, 0.2485, 0.2468 (right). All matrix entries seen have the
  magnitude **0.2477 +- 0.0007** times the decay of a line for RT60 = 0.5 s.
  INFERRED: a feedback matrix with entries of +-1/4 (a 16-line network), with
  1 % of loss that is not explained. The measured sign block is consistent
  with orthogonal rows.
- No filter is needed in the loop at this precision (Brightness 0 %).
- OPEN: how a first-pass gain splits into input and output gain. The ratios
  `s_ab / s_ba` say the output gain grows and the input gain falls along lines
  1 to 4 (output 1, 1.376, 1.515, 1.572; input 1, 0.858, 0.767, 0.706 when the
  decay is counted inside the line).

## 7. Scores on the holdout

`score_first_order.py first_order_a.py` was run twice.

| run | change | overall | worst response |
|---|---|---|---|
| 1 | first complete model | -63.06 dB | -60.32 dB |
| 2 | ridge on the pre kernel, post kernel pinned to zero above 22.2 kHz, LFO constants refitted by the script | -63.05 dB | -60.21 dB |

The data file was regenerated once more by the final script and is identical
to the one of run 2 (largest difference 7e-18). Nothing was chosen on the
holdout; both changes of run 2 were made for the conditioning of the kernels.

## 8. Side check at Macro 100 %, Mix 100 %

Two realisations of a 34 s impulse train (56 impulses on L), scratch script `s50`.

- MEASURED: read-back `Macro = 100`, `Mix = 100`. The mean envelope first
  exceeds 2 % of its peak at raw 1831 and 1841 (Macro 0: 1291). Realisation 1
  against realisation 0 over [1200, 6000): -1.5 dB.
- MEASURED: the Macro 0 model of this packet, compared with each Macro 100 %
  response over all lags 0..1300, correlates at most 0.25 to 0.52 (median 0.35
  and 0.41), at lags of 529 to 1247 samples (median 625). On the Macro 0
  capture of the same stimulus it nulls at -63.1 dB.
- So the constants here are the Macro 0 foundation. Which of them survive at
  Tide 100 % (line lengths, LFO, kernels) is OPEN and needs its own packet.

## 9. What failed or was not established

- A sinusoidal delay law: -30 dB at best (section 4).
- One kernel, before or after the lines: -46.5 and -50.7 dB (section 5).
- A slope-proportional correction kernel: +0.5 dB only.
- Alternating least squares for the two kernels creeps (0.09 dB per round);
  joint Gauss-Newton is required.
- The exact converter filters are not identified: the split into pre and post
  kernel is regularised, not measured, and the model stops near -63 dB. The
  residual is spread over the band (-57 to -66 dB per band on own off-grid
  data), so no single missing element was isolated. -100 dB is not reached.
- Not studied: other sample rates, Size, Decay, Width, Pre-delay, Macro above 0.
- The meaning of 1.3163744 rad and of the 116 samples; lines beyond the
  eleventh; the split of gains into input and output.

## 10. Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY fit_first_order_a.py                        # about 3.5 min; writes tide_structural_data/first_order_a.npz and .json
$PY score_first_order.py first_order_a.py       # holdout
```

- `first_order_a.py`: the model and `predict(time, input_channel)`; no captures.
  `Model.lfo_origin` is `warm-up samples at 44.1 kHz - 116`.
- `tide_structural_data/first_order_a.npz`: kernels, gains, LFO constants (the
  file the model loads); `first_order_a.json`: the same constants and the
  validation nulls in readable form.
- Own captures: 4 off-grid renders of 100 s (152 MB), warm-up and Macro 100 %
  renders of 6 to 34 s. Exploratory scripts (not deliverables) are in
  `Analyzer/Results/RevOceanCharacterization/work/first_order_a/`.
