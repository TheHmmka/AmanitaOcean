# Outer controls of the reference (packet io)

Mix, Return, Master Volume, Width, Pre-delay and the level handling that sits
around the Tide network. Script: `measure_io.py`. Numbers: `tide_structural_data/io.json`.

Tags: MEASURED (read off captures), FITTED (a constant, with its residual),
INFERRED (reasoned, not tested), OPEN. A null is
`20 log10(rms(prediction - capture) / rms(capture))` with nothing fitted.

## Which state was measured (Tide 100 %, Mix 100 %?)

Both, in two steps, and the difference matters.

- **Exact laws: Macro (the Tide knob) 0 %.** Only there is the reference
  repeatable, so only there can a law predict one capture from another sample
  for sample. In every law the wet term is the **Mix 100 %** output of the same
  stimulus; Width, Return and Pre-delay were nulled at Mix 100 % and again at
  lower Mix.
- **Re-check at the listening point: Macro 100 %, Mix 100 %** (section 7).
  What can be exact there is exact (dry coefficient, mono output at Width 0 %,
  the first wet sample against Pre-delay). What cannot be exact, because two
  instances differ at +0.8 dB of null, was checked on levels of 18 s of noise:
  the wet level follows the laws within the realisation spread of 0.43 dB, the
  side/mid balance within 0.03 dB.

Nothing found at Macro 100 % contradicts a law found at Macro 0. A sample-exact
null of the wet at Macro 100 % is not possible with this reference.

## The model

`x` is the input, `fs` the sample rate, `Λ` the reported latency (44, 48, 88,
96 samples at 44.1, 48, 88.2, 96 kHz), `N(.)` the Tide network including its
latency. All controls below are static; the order of the linear wet stages
(ducking gain, Width, Return) cannot be observed and is written in one of the
equivalent orders.

```
D       = max(0, floor(P fs / 1000) - 1)              P = displayed Pre-delay, ms
u[n]    = x[n - D]                                    input of the network
w       = N(u)                                        100 % wet, Width 100 %, Return 0 dB
g[n+Λ]  = ducking gain from x[n]                      section 6.2; 1 while |x| < 0.5629
w1      = g * w
M', S'  = gm (w1L + w1R)/2,  gs (w1L - w1R)/2         gm = sqrt(2/(1+s)), gs = s gm, s = 2 width_normalised
w2      = 10^(Return dB / 20) * [M' + S', M' - S']
y0[n]   = a x[n - Λ] + b w2[n]                        a = min(1, 2(1-m)), b = min(1, 2m), m = Mix/100
y       = clip(10^(Master dB / 20) * y0)              section 6.1; identity while |.| <= 2.5119
```

`outer_shell()` in `measure_io.py` is this model. One capture with every outer
control off neutral (Pre-delay 20.84 ms, Width 0.8 normalised, Return -4.5 dB,
Master +2.5 dB, Mix 30 %) is predicted from a neutral 100 % wet capture at
**-140.9 dB** (MEASURED).

## 1. Mix

MEASURED, 27 values from 0 to 100 % including 12.5, 33.33, 66.67 and 87.3 %.

- `out = a(m) dry[n - Λ] + b(m) wet`, `a = min(1, 2(1 - m))`, `b = min(1, 2m)`.
  The dry stays at unity up to 50 %, the wet reaches unity at 50 % and stays
  there: at 50 % the output is dry + wet, both at full level. It is neither a
  linear crossfade nor equal power.
- Fitted coefficients equal the law to 8 decimals (for example 87.3 %:
  a = 0.25399995, b = 1.00000000). Law null, worst of the 25 partial mixes:
  **-151.8 dB**, which is the rounding of 32-bit samples.
- Mix does not touch the wet: the same Mix 100 % capture serves every value.
- The dry path is delayed by exactly the reported latency and nothing else. At
  Mix 0 the output is **bit-identical** to the input delayed by Λ.
- A single-precision evaluation of the law is bit-identical to the capture at
  every Mix up to 50 % and at 75 and 100 %; at the other values above 50 % it
  differs in the last bit (the rounding order of `2(1 - m)` inside the
  reference is not identified, OPEN and of no audible consequence).

## 2. Return and Master Volume

MEASURED on 36 (Return) and 43 (Master) captures at Mix 100, 80, 50 and 30 %.

- **Return** is a gain on the wet only: `10^(displayed dB / 20)` from -24 to
  +24 dB. The dry coefficient is unchanged at every Mix. Fitted wet gain equals
  the displayed dB within 5e-6 dB; law null -138 dB or better.
- **Master Volume** is a gain on dry and wet together: `10^(displayed dB / 20)`
  over the declared law (-70 to +6 dB, Exp(-3)). Fitted gain equals the law
  within 1e-5 dB. Law nulls are -122.6 dB or better, fitted nulls -148.9 dB;
  the gap is the reference's single-precision evaluation of its own display
  law (INFERRED).
- Master at normalised 0 (displayed -70.0) is a **mute**: the output is exactly
  zero. At normalised 1e-6 the gain is already on the law (-69.99976 dB).
- Master acts before the output clipper (section 6.1).

## 3. Width

MEASURED, 14 values from 0 to 150 %, L-only, R-only and stereo input.

- Width is a mid/side operation on the **wet output**. With `x` the normalised
  parameter (`Width % = 200 (1 - 4^-x)`, so `s = 2x = -log2(1 - Width%/200)`):

  ```
  mid gain  gm = sqrt(2 / (1 + s))        side gain  gs = s sqrt(2 / (1 + s))
  ```

  | Width % | s | gm | gs |
  |---|---|---|---|
  | 0 | 0 | 1.41421 (+3.01 dB) | 0 |
  | 50 | 0.41504 | 1.18886 | 0.49342 |
  | 100 | 1 | 1 | 1 |
  | 150 | 2 | 0.81650 (-1.76 dB) | 1.63299 (+4.26 dB) |

- Evidence. A free 2x2 matrix from the Width 100 % output to the Width w output
  nulls at -146.7 dB or better and is symmetric; its mid and side gains equal
  the law to 7 decimals at all 13 values. Law null: **-141.6 dB** worst, also
  with L-only input.
- It is not on the input: the best mix of the Width 100 % responses to L-only
  and R-only input predicts the Width w response to L-only input at -3 dB
  (0 %) to -43 dB (99 and 101 %). It is not inside the network either, since
  an output-only relation is exact.
- The dry is untouched: at Mix 50 and 80 % the law nulls at -145 dB or better.
- At 0 % the wet is mono (L and R bit-identical), 3.01 dB above the mid of the
  100 % output. Above 100 % the side keeps growing linearly in the normalised
  parameter while the mid falls; nothing saturates up to 150 %.

## 4. Pre-delay

MEASURED on 80 settings by bit comparison.

- Pre-delay is a delay of the **input of the network** by a whole number of
  samples,

  ```
  D = max(0, floor(P fs / 1000) - 1)        P = displayed value in ms
  ```

  relative to Pre-delay 0. The capture with Pre-delay P is **bit-identical** to
  the capture at Pre-delay 0 whose stimulus was moved later by D samples: all
  26 settings of the main set (0.5 to 96000 samples, the full 2000 ms; 24 of
  them a tenth to half a sample away from an integer), and all 24 at 44.1, 88.2
  and 96 kHz.
- So the measured delay is the displayed value minus one sample, rounded down:
  displayed 100 ms at 48 kHz is 4799 samples. Everything below 2 samples
  (0.042 ms at 48 kHz) gives no delay.
- There is no interpolation. A least-squares kernel over the ten neighbouring
  input shifts is a single unit tap (the nine others below 1e-9) for 100.5,
  240.25 and 1000.75 samples.
- The delay is before the modulated network, not after it: moving the
  Pre-delay 0 output by D instead nulls at +0.6 to +2.8 dB for 10 to 300 ms
  (-15.8 dB for 1 ms, where the network has barely moved).
- The dry is not delayed (Mix 50 % with 20.84 ms: -147.4 dB).
- The displayed law is the delay law. The integer boundary was probed at 480,
  4800 and 14400 samples: of 30 settings the formula misses 5, all within
  0.00033 samples (7 ns) of an integer; the 21 farther away all hold. The
  reference evaluates its display law in single precision (INFERRED).
- Whether the "- 1" is a property of the delay line (a minimum of one sample
  that is also present at Pre-delay 0) cannot be told from outside; only the
  delay relative to Pre-delay 0 is observable (OPEN, without consequence for
  the model as long as the network is measured at Pre-delay 0).

## 5. Sample rates

MEASURED at 44.1, 48, 88.2 and 96 kHz.

- Reported latency 44, 48, 88, 96 samples (1 ms rounded down). At each rate the
  Mix 0 output is bit-identical to the input delayed by exactly that latency.
- The Mix law nulls at -151.8 dB or better at 25, 50 and 75 % at each rate.
- Pre-delay follows the same formula with the rate's `fs` (24 of 24).
- The ducking time constants are in milliseconds at 44.1 and 96 kHz too
  (model null -148.8 and -148.4 dB).

## 6. Level handling outside the network

Two stages are active at the neutral state. Every law above holds only below
their thresholds. All campaign stimuli so far (impulses of 0.5, noise of 0.25)
are below both.

### 6.1 Output clipper

MEASURED at Mix 0 with a ramp from -10 to 10, noise of peak 6 and sines.

- Per sample and per channel, odd-symmetric, memoryless:

  ```
  clip(v) = v                                   |v| <= T
          = sign(v) (|v| - (|v| - T)^2 / (4 (C - T)))   T < |v| < 2C - T
          = sign(v) C                           otherwise
  T = 10^(8/20) = 2.5119 (+8 dBFS)      C = 10^(12/20) = 3.9811 (+12 dBFS)
  ```

- FITTED freely on the ramp: T = 2.511888, C = 3.981072, that is +8.0000 and
  +12.0000 dBFS. With the round values the nulls are -143.5 dB (ramp, both
  channels), -142.0 dB (noise) and -142.4 dB (sines); a linear path nulls at
  -3.9 to -12.2 dB. Below 2.512 every ramp value passes bit-exact.
- It is the last stage: after Mix and after Master. With Return +24 dB and
  Master +6 dB on an input of peak 0.5, the prediction with the clipper last
  nulls at -137.0 dB; with the clipper before Master -25.2 dB, on the wet only
  -30.3 dB.
- Consequence: the dry path at Mix 0 is bit-exact only up to +8 dBFS.

### 6.2 Ducking at 0 % is a compressor with its threshold at 0 dBFS

MEASURED. A loud input lowers the wet although Ducking reads 0.000. The same
DC staircase at Ducking 0, 25, 50 and 100 % shows the same stage with its
threshold moving down (about 0, -5, -13 and -50 dBFS if the slope stays 5/7,
INFERRED from four levels each), so this is the Ducking compressor, and 0 % is
its mildest setting rather than off.

Law at Ducking 0 %, proven by nulls on 24 loud events (sines of 50 Hz, 1 kHz
and 10 kHz, DC, square, single impulses, L only, R only, L = -R, two DC
staircases from -8 to +36 dBFS) placed on a quiet noise carrier:

```
L[n]     = 20 log10 max(|xL[n]|, |xR[n]|)             raw input, DC included
G(L)     = 0                             L <= -W/2
         = -(5/7) (L + W/2)^2 / (2W)     |L| < W/2
         = -(5/7) L                      L >= W/2     ratio 3.5 : 1, threshold 0 dBFS
g_dB[n]  = G + k (g_dB[n-1] - G),  k = exp(-1 / (tau fs))
           tau = 5 ms while the reduction grows, 300 ms while it recovers
wet[n+Λ] is multiplied by 10^(g_dB[n] / 20), both channels alike
```

- FITTED: slope 0.7142786 (5/7 = 0.7142857), threshold 0.000006 dBFS, knee
  W = 9.983 dB (largest error of the static curve 0.00005 dB over 32 levels).
  With the round W = 10 dB the largest error is 0.0015 dB and the event nulls
  are about -80 dB instead of -122 to -134 dB inside the knee. What 9.983 is,
  is OPEN.
- The first step of the gain after a DC event of 2.0 is 0.004158 of the target
  (5.000 ms at 48 kHz); the recovery has 299.95 ms. Both are exponentials in
  dB, not in linear gain.
- Model nulls: **-113.9 dB** worst (1 kHz sine of peak 8), -119 to -150 dB for
  the others; without the model the same captures null at +4.8 to -54.7 dB.
  The smoother has to be run in single precision in the form above. In double
  precision the worst null is -98 dB, and the DC event of 2.0 nulls at -108 dB
  at 48 kHz and -81 dB at 96 kHz instead of -149 and -148 dB (scratch runs).
- The key is the raw input, not the pre-delayed one and not the wet: with 2 s
  of pre-delay the gain moves at the event, exactly Λ samples after it, with
  no look-ahead (the three samples before are untouched).
- The gain is applied after the network and is common to L and R: the wet of
  a carrier that is already inside the network drops at once, by the same
  factor on both channels. L-only, R-only and L = -R events give the same
  trajectory as L = R.
- An event of peak 0.5 leaves the wet bit-identical. The wet is reduced as
  soon as an input sample exceeds 10^(-W/40) = **0.5629 (-4.99 dBFS)**. Static
  reduction: 0.14 dB at -3 dBFS, 0.89 dB at 0 dBFS, 2.28 dB at +3 dBFS,
  4.29 dB at +6 dBFS.
- The dry is not ducked (noise of peak 2 at Mix 0 is bit-identical to the input).

### 6.3 Bypass and DC

- MEASURED: with On/Off at 1 ("Bypassed") the output is the input delayed by
  the latency, bit-identical, whatever Mix says.
- MEASURED: the wet passes DC. A DC input of (0.5, -0.25) settles to a wet of
  (0.286927, -0.040452) and holds it within 1.5e-5 over 0.7 s. No DC blocker
  and no high-pass is active at HPF 20 Hz.

## 7. The listening point: Macro 100 %, Mix 100 %

Reference state Macro 100 %, Mix 100 %, Decay 0.5 s; 8 realisations of 18 s of
stereo noise, 22 further noise captures with one control moved and 30 short
ones for Pre-delay. Two realisations null against each other at +0.8 dB.

| Law | Check at Macro 100 % | Result |
|---|---|---|
| Mix, dry gain | a 1000-sample burst the wet has not answered yet (the first wet sample comes 1787 samples after its start) | MEASURED exact: 1, 1, 0.5 at 25, 50, 75 %; null -148.9 dB; Mix 0 is bit-identical to the delayed input |
| Mix, wet gain | wet level over 18 s against the reference mean | MEASURED: -6.07 and -6.42 dB at 25 % (law -6.02), -0.39 to +0.21 dB at 50 and 75 % (law 0) |
| Return | wet level | MEASURED: -11.97, -12.16 dB at -12; +11.98, +12.17 dB at +12 |
| Master | dry burst exactly, wet level | MEASURED: dry 0.50118726 at -6 dB and Mix 50 % (null -135.7 dB); wet -5.78, -6.31 dB |
| Width 0 % | L and R compared bit for bit | MEASURED exact: mono in both realisations |
| Width, side/mid | level of side minus level of mid | MEASURED: -7.65, -7.62, -7.64, -7.65 dB at 50 % (law -7.638); +6.013, +6.018 dB at 150 % (law +6.021) |
| Width, mid gain | wet mid level | MEASURED: +3.25, +2.60 dB at 0 % (law +3.01); +1.20, +0.45, +2.22, +0.61 dB at 50 % (law +1.50); -1.90, -1.23 dB at 150 % (law -1.76) |
| Pre-delay | first non-zero wet sample, which is the same in every realisation | MEASURED exact: for 100.5 to 14400.5 samples it equals the first wet sample at Pre-delay 0 with the input moved by D; D - 1 and D + 1 miss by one sample |

Over the 21 captures that contain wet, measured level minus law is
-0.12 dB on average with 0.43 dB spread (largest 1.05 dB); the reference
realisations themselves spread by 0.54 dB. The side/mid balance is far more
stable (0.017 dB between realisations) and meets the Width law within 0.03 dB.

Two observations at Macro 100 % that belong to the Macro packets:

- MEASURED: the level of the wet under steady noise is not steady. In 2 s
  windows it runs from -36.4 dB at 1 to 3 s (std 0.3 dB over 8 realisations)
  through -32.3 dB at 11 to 15 s back to -35.3 dB at 17 to 19 s; the arch is
  common to all 8 realisations, with 0.5 to 1.4 dB of spread around it. At
  Macro 0 the same noise gives -32.4 dB, flat within 0.1 dB (-32.7 dB in the
  last window, which holds the end of the noise). A level read at Tide 100 %
  depends on when it is read. Cause and period OPEN.
- MEASURED: the first wet sample is deterministic at Macro 100 % (1787 samples
  after the input, against 1265 at Macro 0, 25, 50 and 75 % for this
  stimulus), although the wet differs between instances from that sample on.

## What did not work or is limited

- At Macro 100 % the wet-level laws (b(m), Return, Master, gm) are confirmed
  only to about ±0.4 dB per capture. The data cannot tell b = 2m from a law
  that differs by a few tenths of a dB at Macro 100 %; it does exclude a linear
  or equal-power crossfade (0 dB instead of -6.0 or -3.0 dB at 50 %).
- The first wet sample cannot be used as a delay meter on its own: at Macro 0
  it moves by 4812 samples for a pre-delay of 4799, because the delayed input
  meets the modulated network at another phase. Only the comparison with a
  shifted stimulus is valid.
- The first model of the ducking smoother (double precision, increment form)
  stopped at -98 dB and at -81 dB at 96 kHz; the single-precision form fixed
  it. The knee width refuses the round value 10 dB.
- The order of ducking gain, Width and Return on the wet is unobservable.
- Decay was 0.5 s and Size 100 % throughout. The laws are static gains, delays
  and matrices outside the network and were not re-measured at other Decay or
  Size (INFERRED to be independent of them).
- Width, Return and Master were not re-measured at the other sample rates.

## Open questions

1. Is the wet reduction at Ducking 0 % wanted in the new Character? At the
   listening point a programme peaking at 0 dBFS has its wet lowered by up to
   0.9 dB, with 5 ms attack and 300 ms release. The model is given above if it
   is wanted; otherwise level matching against the reference must use input
   peaks below -5 dBFS.
2. Laws of the Ducking control above 0 % (threshold against percent, whether
   ratio, knee and time constants change) are outside this packet.
3. Origin of the knee width 9.983 dB.
4. The slow level cycle and the 522 samples of extra delay at Macro 100 %.
5. Predelay Synced was left at its default and not studied.

## Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY measure_io.py
```

The script uses 496 captures (971 MB of cache); exploration used about 130 MB
more. Scratch scripts are in `Analyzer/Results/RevOceanCharacterization/work/io/`.
