# Tide model: implementation specification

For a C++ port of the campaign's model of Arturia Rev OCEAN 1.0.0.5848 in Tide
mode. Processing order, every constant, every place where single precision
matters, and what is not known. The findings documents are the evidence and
the history; this file and its companions are enough to build the engine.

| companion | role |
| --- | --- |
| `reference_render.py` | the executable form of this specification: `render(...)` is the whole chain |
| `tide_structural_data/engine_constants.json` | every constant and table below, with derived tables; written by `emit_engine_constants.py` |
| `tide_structural_data/engine_golden.npz`, `engine_golden.json` | golden vectors for unit tests; written by `emit_engine_constants.py golden` |
| `Source/dsp/FathomEngine.h` | the interface the engine implements (section 18) |

Status of an element: **M** measured and exact at the depth of the nulls of
section 16; **F** fitted, with what the fit leaves; **I** inferred; **U** unknown
in the reference and left to the product. `fl()` is rounding to single precision,
a suffix `f` a single-precision constant. Everything not marked single precision
is double precision in the model.

## 0. The chain

```
x ── pre-delay ── converter ── equaliser ── cos(90° m) + sin(90° m) comb ── 44 samples ── 2 x 16 lines, Hadamard loop
  ── per output: lines 1-8 -> voice at phi, lines 9-16 -> voice at phi + 1/2 ── converter ── wet
out = clip( dryGain * x[N - latency] + wetGain * width( level stage(x) * wet ) )
```

Everything between the two converters runs at 44.1 kHz at every host rate (M:
the same core between two captures at -131 dB at 19 rates; comb and voices
inside it, -101 to -107 dB at 88.2 and 96 kHz). Two groups of 16 lines, one per
output channel; nothing passes between the groups after the line inputs.
`m` below is Macro as a fraction. Every other control of the reference is at
its neutral value and is not part of the model.

## 1. Time bases and the timing contract

- `n`: host frames, counted from the first frame the instance processes
  (engine: from `prepare()` or `reset()`). `k`: internal samples at 44.1 kHz,
  counted from the same instant. Every clock of the model counts one of the two
  (M: warm-ups of 3 to 60 s null alike).
- `fs / 44100 = L / M` in lowest terms. On a lattice of `1 / L` internal sample
  host frame `n` sits at `M n` and internal sample `k` at `L k`.
- Reported latency: `Λ = 4 floor(11 fs / 44100 + 0.5)` host frames (M, 95 rates).
- **Reference, raw time base** (what `revocean.capture` and `render` return):
  frame `N` of the output answers frame `N` of the input; the dry signal in it
  is `x[N - Λ]`, the wet signal is the up-converter's read of the core at
  lattice time `M N - b` (section 3).
- **Engine** (`FathomEngine.h`): no latency is reported. For input frame `n` the
  engine returns the reference's wet at raw frame `N = n + Λ`, and the plug-in
  mixes it with the undelayed `x[n]`: its output is the reference's, advanced
  by `Λ`. The level stage (section 14) is keyed by `x[n]` and acts on the wet
  of the same engine frame.
- This is causal. The wet at `n + Λ` needs internal samples up to
  `floor((M (n + Λ) - b) / L) + 17`, and those need host frames no later than
  `n - 70`, `n - 34`, `n - 131`, `n - 56`, `n - 250` at 48, 88.2, 96, 176.4 and
  192 kHz (computed from the tables of section 3).
- At a 44.1 kHz host there are no converters: `u[k] = x'[k]` (`x'` the
  pre-delayed input), the reference's wet is `y[N - 44]`, the engine returns
  `y[n]`.

| host rate | L / M | Λ | a | b | b - Λ M | first arrival of a line of length 0, raw frames |
| --- | --- | --- | --- | --- | --- | --- |
| 44.1 kHz | 1 / 1 | 44 | 0 | 44 | 0 | 88 |
| 48 kHz | 160 / 147 | 48 | 4250 | 18592 | 11536 | 203.2789 |
| 88.2 kHz | 2 / 1 | 88 | 45 | 144 | 56 | 277 |
| 96 kHz | 320 / 147 | 96 | 7030 | 37184 | 23072 | 396.5578 |
| 176.4 kHz | 4 / 1 | 176 | 79 | 288 | 112 | 543 |
| 192 kHz | 640 / 147 | 192 | 12443 | 74368 | 46144 | 782.1156 |

`a`, `b` in lattice steps (section 3). The engine's up-converter reads at
`M n - (b - Λ M)`.

## 2. Parameters

The engine works with physical values. Ranges are the reference's.

| parameter | range | enters |
| --- | --- | --- |
| `decaySeconds` T | 0.5 to 60 s | attenuation (9), tap weights (10) |
| `sizeScale` s | 0.3 to 2, single precision | line lengths (8), attenuation (9) |
| `preDelaySeconds` | 0 to 2 s | section 4 |
| `macro` m | 0 to 1 | comb mix (6), voices (12), rate of the phase (13) |
| `widthScale` | 0 to 2 | section 14 |
| `mix` | 0 to 1 | section 14 |

Behaviour while any of them moves is **U**: every capture holds its settings
from the first frame. The reference smooths at least Macro (a glide of about
0.2 s is seen at the start of an instance).

The reference's own knob laws matter only to a scorer that sets its knobs
(`reference_render.physical`). `x` is the normalised host value, single precision:

- Size: `r = fl(fl(expf(fl(0.71337f x)) - 1) * fl(1 / fl(expf(0.71337f) - 1)))`,
  `s = fma(1.7f, r, 0.3f)` (one rounding). **M**, 69 of 69 captures; the display
  law in percent gives another whole length for one or two lines at one knob
  position in 200 (80.1, 162.1, 180.1 %: -10 to -34 dB instead of -113 dB).
  Note `s = 1.0000001` at the host's value for 100 %.
- Decay: `0.5 + 59.5 (expf(4.4f x) - 1) / (expf(4.4f) - 1)` in single
  precision. **I**; the reciprocal and fused form of Size differs by one step at
  most and cannot be told apart.
- Pre-delay: `P = 2000 (expf(6 x) - 1) / (expf(6) - 1)` ms in single precision. **M** with section 4.
- Width: `widthScale = 2 x` (display: `200 (1 - 4^-x)` %). Mix, Macro: `x`.

## 3. Converters

None at 44.1 kHz. Otherwise two polyphase filters that read one table. **M**:
kernel, gains, delays and block rule at 66 rates from 44.101 to 384 kHz at
-126 to -132 dB between captures; clock signs at the five rates below with
all nine pairs tried.

**Table** (69632 entries, single precision; double precision scores the same):

```
table[i] = fl( 0.9 sinc(0.9 i / 4096) * I0(6 sqrt(1 - (i / 69631)^2)) / I0(6) ),   i = 0 .. 69631,   sinc(x) = sin(pi x) / (pi x)
```

17 zero crossings per wing, 4096 entries per crossing, read without
interpolation. The window ends at entry 69631 with the value `1 / I0(6)`.

**Positions.**

```
input converter  (host -> internal, source step M, target step L):   P = L k - a,   a = (X_in + 1) M - L,   X_in = floor(18 L / M + 10)
output converter (internal -> host, source step L, target step M):   P = M N - b,   b = 28 L + D M,        D = Λ if M = 1, else 2 Λ
base = floor(P / source step),   j = P mod source step               (the branch)
```

**Branch `j`** of a converter with clock sign `σ` (-1 low, 0 exact, +1 high):

```
if σ < 0 and j = 0:   left = 1 - 2^-33,  shift = -1
else:                 left = j / source + σ 2^-33,  shift = 0          (double precision)
right = 1 - left
up   (output converter):  before = int(4096 left)  + 4096 i  while < 69632;   after = int(4096 right) + 4096 i  while < 69631;   gain 1
down (input converter):   step = 4096 M / L;  p = left step;   before: take int(p), p += step, while int(p) < 69632
                                             p = right step;  after:  take int(p), p += step, while int(p) < 69631;            gain M / L
output sample = gain * ( sum_i table[before[i]] in[base + shift - i]  +  sum_i table[after[i]] in[base + shift + 1 + i] )
```

- `2^-33` stands for the rounding drift of the converter's clock; only its sign
  acts. It decides the entry where a distance falls exactly on one: every tap at
  88.2 and 176.4 kHz, one phase in five at 48, 96 and 192 kHz.
- The down-converter accumulates its table position in double precision
  (`p += step`); do not replace it by `(left + i) step`.
- The wing after the output sample never uses the last entry (limit 69631).
- Input gain `M / L` (0.91875 at 48 kHz), output gain 1. A constant comes back
  1.0004 to 1.0010 times as large through both; that is part of the law.

**Clock signs** (input, output). **M**: 48 kHz (low, high); 88.2 kHz (exact,
exact); 96 kHz (low, high); 176.4 kHz (exact, exact); 192 kHz (high, low);
also 56 kHz (low, low). With a wrong sign the relation between two captures
nulls at -65 to -80 dB instead of -128 dB; nothing of it is audible.

| host rate | X_in | input branches, taps, offsets | output branches, taps, offsets |
| --- | --- | --- | --- |
| 48 kHz | 29 | 147, 37 to 38, -18 .. 19 | 160, 33 to 34, -16 .. 17 |
| 88.2 kHz | 46 | 1, 67, -33 .. 33 | 2, 33 to 34, -16 .. 17 |
| 96 kHz | 49 | 147, 74 to 75, -37 .. 37 | 320, 33 to 34, -16 .. 17 |
| 176.4 kHz | 82 | 1, 135, -67 .. 67 | 4, 33 to 34, -16 .. 17 |
| 192 kHz | 88 | 147, 148 to 149, -74 .. 75 | 640, 34, -17 .. 17 |

`engine_constants.json` (`converters.rates`) has these numbers and, per branch
table, the sum, the absolute sum and a SHA-256 of the coefficients in branch
order (`converters._branches`, float64 little-endian).

**Any other rate from 44.1 kHz up: I.** The table, `Λ`, `X_in`, `a`, `b`, the
one-or-two-block rule and the gains are closed rules and hold at every rate
measured. The clock signs are not a closed rule: `converters.clock_signs`
simulates a block converter (`converters.Clock`, blocks of `Λ` host frames) and
is right for 121 of 122 clocks measured, wrong at 56 kHz. A product may port
that simulation or take both signs as exact. The engine ports it
(`FathomRateLattice::at`, the same signs as `converters.clock_signs` at 432 rates
from 22.05 to 384 kHz): captures at 64 and 384 kHz null at -117.5 and
-118.8 dB with the simulated signs and at -77 dB with both taken as exact
(`score_engine.py clocks`). **Below 44.1 kHz: U** (the host
tool refuses such rates; `converters.py` lets the converters exchange roles,
untested). Where the clock error is no longer small against the spacing of
table positions (non-standard rates with a fine lattice, from seconds after
the start) the sign rule falls short by 9 to 16 dB of a -126 dB null; at the
rates above it holds for a day.

## 4. Pre-delay

`D = max(0, floor(P fs / 1000) - 1)` whole host frames, `P` in milliseconds,
in front of the input converter; no interpolation; nothing below two frames.
The dry signal and the key of the level stage are not delayed. **M**, bit for
bit on 110 settings at four rates when `P`, the product and the quotient are
single precision (`reference_render.predelay_samples`); in double precision 7 of
the 110, all on a whole number of frames within 3.3e-4, are one frame off.

## 5. Input equaliser

Per input channel, on the converter's output, in front of the comb (**M**: the
null is better or equal on 25 of 25 captures, by up to 6.1 dB). Two peaking
sections of the audio-EQ cookbook at 44.1 kHz (`A = 10^(dB / 40)`,
`alpha = sin(w0) / (2 Q)`):

| section | frequency | gain | Q | b0, b1, b2 | a1, a2 |
| --- | --- | --- | --- | --- | --- |
| 1 | 200 Hz | +0.5 dB | 0.4 | 1.001981820, -1.932322573, 0.931125518 | -1.932322573, 0.933107337 |
| 2 | 1750 Hz | -0.5 dB | 0.4 | 0.986520911, -1.471136926, 0.531559171 | -1.471136926, 0.518080082 |

**F**: free fits return the round values within 1.2e-5. Only the transfer
function is measured; the model runs the sections in transposed direct form II.

## 6. Tide input stage: the comb

One comb per input channel `c` (0 left, 1 right) on the equalised stream `e`.
It does not depend on Macro, Size or Decay and runs from the first sample.

```
tri(x):  x -= floor(x);  4x if x < 1/4;  2 - 4x if x < 3/4;  else 4x - 4
D[k]   = fl( 431.2075f + fl( 236.0360f * fl( tri((k - 85.7) / 8820000 + c / 4) ) ) )        samples; tri in double precision
i = floor(D),  d = D - i
read   = line[k - i - 1] + (1 - d) / (1 + d) * (line[k - i] - read)                          first-order all-pass, read = its previous output
hp     = (read - hpIn) / (1 + kh) + (1 - kh) / (1 + kh) * hp;   hpIn = read                  kh = tan(pi 30 / 44100)
lp     = kl / (1 + kl) * (hp + lpIn) + (1 - kl) / (1 + kl) * lp;  lpIn = hp                  kl = tan(pi 20000 / 44100)
line[k] = e[k] - 0.4 lp                                                                      both filters inside the loop
t[k]    = cos(pi m / 2) e[k] + sin(pi m / 2) lp
```

- Delay 195.17 to 667.24 samples (4.43 to 15.13 ms) on a 200 s triangle; the
  right input a quarter period ahead.
- **M**: all-pass read with the fraction in [0, 1); filters inside the loop;
  cos / sin mix (within 5e-6 from Macro 0.5 to 85 %). **F**: feedback -0.400000
  +- 0.000003; corners 30.000 +- 0.001 Hz and 20000.0 +- 0.3 Hz; delay law 2.7e-5
  samples rms, origin 85.7 to 85.8, no round form.
- The delay is a single-precision number (**M**); the arithmetic above is the
  best form found and is not the reference's (**U**). It puts the sample at
  which the delay falls through a whole number, and the read changes taps, one
  sample off in a few percent of the downward crossings (3 of 53, 12 of 434,
  1 of 67). Such an event leaves an error 50 to 70 dB below the local signal
  that rings on with the reverberation; it is what keeps steady material at -80
  to -93 dB at Macro 100 %. Thirteen observed tap changes are listed in
  `findings/tide_model_verification.md`, section 4.

## 7. Line inputs

`x_g[k] = t_g[k - 44]`: the lines are written 44 internal samples behind the
stage above, at every host rate (**M**). Line `n` (1 to 16) of group `g`
receives `own[n] x_g[k] + cross[n] x_other[k]`, with

```
w[n]     = 0.272 (n = 1 .. 6);   1 - (n - 7) 0.728 / 9 (n = 7 .. 16)
S_c(x)   = sign(x) (e^(c |x|) - 1) / (e^c - 1)
B[n]     = B_1 ( (1 + 0.236) / 2 - (1 - 0.236) / 2 * S_3.36((n - 8.5) / 7.5) ),     B_1 = 0.24397766367314302
own[n]   = B[n] (1 + w[n]) / 2,     cross[n] = B[n] (1 - w[n]) / 2
```

**F**: `B_1` is the one fitted number of the network (0.24397764 +- 1e-7 over
38 captures); the laws hold within 6.4e-6 of free gains. The values per line
are in `engine_constants.json` (`lines`).

## 8. Lines

Primes `P[g][n]`, 44.1 kHz samples:

```
left   1031 1097 1187 1289 1423 1583 1783 2027 2333 2699 3163 3719 4409 5261 6299 7589
right  1039 1109 1193 1301 1429 1597 1787 2039 2333 2707 3163 3719 4409 5261 6299 7589
```

Whole length, single precision (**M**): `N[g][n] = floorf(fl(P[g][n] * s) + 0.5f)`.
The longest read is 15178 + 39 + 1 samples back.

Oscillator of each line, a single-precision accumulator stepped once per
internal sample from the first sample on (**M**: 14 to 50 dB lost with any other
form):

```
theta[g][n] = fl(11.25f * fl(2 (n - 1) + g))                     start value, radians (0 .. 348.75); not reduced beforehand
inc   = fl(2 pi 0.6 / 44100) = 8.5485517e-5f                     twoPi = 6.2831855f
depth = fl(fl(0.88f * 0.001f) * 44100f) = 38.808002f

per internal sample, in this order:
  t = theta;  if (t >= twoPi) t = fl(t - twoPi);  theta = fl(t + inc)      one subtraction per sample at most
  q = nearest whole number to (double) t / (pi / 2)
  sine = fl( sin( (double) t - q * ((double) fl(pi / 2) - pi / 2) ) )      each quadrant lags the true sine by 4.37e-8 rad
  len  = fl( fl(N) + fl(depth * sine) )                                    three roundings; no fused multiply-add
  i = floor(len),  f = len - i
  r = (1 - f) line[k - i] + f line[k - i - 1]                              linear interpolation; line[k] is written in this sample
```

The effective rate is 0.59988 Hz because the accumulator adds a rounded
increment. A read reaches at least 270 samples back, so the order of lines and
groups inside a sample does not matter. The last place of the reference's sine
is **U**: 7e-4 of the reads differ by one single-precision step of the length,
either way, which is the floor of the nulls.

## 9. Attenuation: Decay and Size

`a[n] = att[g][n] * r`, `att[g][n] = 10^(-3 P[g][n] s / (44100 T))`: 60 dB per
`T` seconds of the unrounded length. The attenuated read is both the output tap
of the line and its contribution to the feedback. **M** (law, unrounded length,
place in front of the tap); the expression the reference evaluates is **U**
(per-pass gains agree to 1.2e-8). Freeze (the reference's Decay knob at its
top) is **U**.

## 10. Output taps

```
p      = (min(T, 6) - 0.5) / 5.5
t1     = 0.46 + (0.228 - 0.46) p
t16    = 0.336 + (0.428 - 0.336) (e^(-2.6 p) - 1) / (e^(-2.6) - 1)
tap[n] = (t1 + 1) / 2 + (1 - t1) / 2 * S_4((n - 5) / 4)              n = 1 .. 9
       = (1 + t16) / 2 + (t16 - 1) / 2 * S_2((n - 12.5) / 3.5)       n = 9 .. 16        (tap[9] = 1)
```

**F**: round values, free weights within 2.7e-6, knee at exactly 6 s. The
weights are the same above Macro 0 (**M**: free weights 1 within 4.9e-5).
Lines 1 to 8 of a group form the input of voice A of that output, lines 9 to
16 that of voice B, each line times `tap[n]`; the cross-fed part of a line goes
with the line (**M**: one line moved to the other voice costs 60 to 100 dB).

## 11. Feedback

```
v[j]       = (1 / 4) sum_n H[16 - j][n - 1] a[n],     H[r][c] = (-1)^popcount(r & c)      j, n = 1 .. 16
line[j][k] = own[j] x_g[k] + cross[j] x_other[k] + K_j(v[j])                              written in the sample it is computed in
```

A Sylvester Hadamard matrix of order 16 with its rows reversed, times 1/4, the
same for both groups (**M**: 2 x 256 entries within 2.4e-6, all signs). `K` is
two cookbook shelving sections in series with their own state per line written:

| section | kind | frequency | gain | Q | b0, b1, b2 | a1, a2 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | low shelf | 1221 Hz | -0.28 dB | 0.484 | 0.997352225, -1.669002645, 0.696815433 | -1.668590425, 0.694579878 |
| 2 | high shelf | 12840 Hz | -0.12 dB | 0.26 | 0.993729815, 0.180104426, -0.298201996 | 0.176519299, -0.300887055 |

**F**: round values within 1.5e-5 of free fits. The output tap is taken in
front of `K`: a first pass does not see it. Against this loop the reference
lags by about 2e-7 samples per pass (**U**; -83 dB after 100 s at Decay 20 s,
below -100 dB in the first 14 s). The loop does not change with Macro (**M**).
Full-precision coefficients are in `engine_constants.json`.

## 12. Voices

Two per output `g`: A on the taps of lines 1 to 8 at phase `phi_g`, B on those
of lines 9 to 16 at `phi_g + 1/2` (**M**: 0.000000 +- 0.000001 cycle). A voice is
a two-pole low-pass in trapezoidal state-variable form with a gain behind it.
Its three parameters are set where the internal sample count is a multiple of
44 and held for 44 samples (**M**; one sample off costs 11 dB), from the phase
at that sample, `t = k / 44100` s:

```
a = 0.88 m,   d = min(1, a / 0.05),   E(x) = (1 - e^(-3x)) / (1 - e^(-3))
fc   = 20000 - 19990 E(a) E(phi - floor(phi))                         Hz; lowest value 463.86 Hz at Macro 100 %
gain = (1 - d) + d sqrt(2) sin^2(pi phi)
K(fc): straight lines through (0 Hz, 3.365) (1 kHz, 7.01) (3 kHz, 10 - 7.91 E(m)) (5 kHz, 7 - 5.64 E(m)) (15 kHz, the same) (20 kHz, 1)
Qt   = 1 + d (K(fc) - 1)
Q   += (1 - e^(-1/10)) (Qt - Q)                                       once per block

per sample:  g = tan(pi fc / 44100),  kq = 1 / Q,  a1 = 1 / (1 + g (g + kq)),  a2 = g a1,  a3 = g a2
             v3 = in - s2;  v1 = a1 s1 + a2 v3;  v2 = s2 + a2 s1 + a3 v3;  s1 = 2 v1 - s1;  s2 = 2 v2 - s2;  out = gain * v2
y_g[k] = voice A + voice B
```

- At Macro 0: `fc` = 20 kHz, Q = 1, gain 1, whatever the phase. Both voices are
  then the fixed low-pass of the network (cookbook low-pass, 20 kHz, Q 1), and
  the model is the Macro 0 network (-305 to -310 dB against it).
- When `phi` passes a whole number the cut-off returns to 20 kHz within one
  block and Q follows through its smoothing; the gain is zero there from Macro
  5.68 % up.
- **F**, each constant symmetric about its value on fresh captures: 20000.00 +-
  0.02 Hz, end 10.0 +- 0.2 Hz, bend 3.0000 +- 0.0001, 0.88000 +- 0.00001, depth
  slope 17.6000 +- 0.0002 (`0.88 / 0.05`), gain top sqrt(2) +- 3e-5, plateau
  7.0000 - 5.6400 E(m), 3 kHz knot 2.0900 and 1 kHz knot 7.0100 at Macro 100 %.
  The slope below 1 kHz (3.645e-3 per Hz, written as the knot at 0 Hz) is
  measured down to 464 Hz. The lag of Q is 9.5 blocks; its form (one-pole on Q,
  on 1/Q, or a delay) is **U**, any of them scores within 0.6 dB. The
  state-variable form is the best of four tried (10 to 12 dB), not shown to be
  the only one. Voice alone: about -110 dB.
- **U**: the state of the Q smoothing and of the filter at the start. The model
  starts Q at its target and the filter at rest.

## 13. Phase generator

One phase per output, in cycles; voice B is half a cycle later at all times.

```
phi_o(t) = level_o(t) + rate(m) t,        t in seconds since the first processed frame
rate(m)  = 0.0502375 + (0.0564238 - 0.0502375) (e^(0.63 m) - 1) / (e^0.63 - 1)        cycles/s
level_o:   knot j at (j + u_j) / f_o seconds, j = 1, 2, ...;  f = 0.136 Hz (left), 0.160 Hz (right);  u_j uniform in [0, 1)
           the level holds its start value up to knot 1; from knot j to knot j + 1 it moves to a new target along (1 - cos(pi x)) / 2
           start value and targets uniform in [0.184, 0.591] (left) and [-0.007, 0.433] (right)
```

- **Exact** (same in every instance): the form, the grid and its origin, the
  rate law. The rate is **F** within 4e-7 of fourteen readings (0.0564237 +-
  3e-7 at Macro 100 %); the hold is a straight line to 1e-6 cycle; raised
  cosines between one knot per cell describe measured phases to a few 1e-6
  cycle. The cell of the left output may be 7.35 s instead of 1 / 0.136 s.
- **Statistical** (random per instance): the two start values, every `u_j`,
  every target. Bounds +- 0.004 cycle. The two start values are positively
  correlated (0.5 to 0.7, depending on how instances were launched); the
  campaign's generator puts both in the same half of their ranges with
  probability 0.83. What seeds the reference is **U**. The engine draws from its
  own generator (`setVoiceSeed`); only statistics can agree, and they do: 89
  listening descriptors within the reference's own spread (0.09 dB in the
  spectrum, 0.4 dB in a band's level range).
- A null needs the phase to about 1e-6 cycle (1e-5 cycle leaves -85 dB).
- **U**: the rate when Macro moves (the campaign's generator integrates it).

## 14. Outer shell

On the wet signal `w` (Width 100 %) and the raw input `x`, in this order (the
order of the linear wet stages cannot be observed):

```
level stage   key  = 20 log10 max(|xL[n]|, |xR[n]|)                           raw input, not pre-delayed
              G    = 0 (key <= -W/2);  -(5/7) (key + W/2)^2 / (2 W) (|key| < W/2);  -(5/7) key (key >= W/2),   W = 9.98306 dB
              g    = fl( G + fl( c * fl(g - G) ) ),   c = exp(-1 / (0.005 fs)) while G < g, exp(-1 / (0.300 fs)) otherwise      single precision
              w'[n + Λ] = 10^(g / 20) w[n + Λ]                                 both channels alike; engine: the same frame
width         mid = (w'L + w'R) / 2,  side = (w'L - w'R) / 2,  s = widthScale
              gm = sqrt(2 / (1 + s)),  gs = s gm;   w'' = (gm mid + gs side,  gm mid - gs side)
mix           y = min(1, 2 (1 - mix)) x[N - Λ] + min(1, 2 mix) w''
clipper       T = 10^(8/20),  C = 10^(12/20);   |y| <= T: y;   |y| >= 2C - T: sign(y) C;   else sign(y) (|y| - (|y| - T)^2 / (4 (C - T)))
```

- **M**: every law. The level stage is the reference's Ducking at 0 %, a
  compressor with its threshold at 0 dBFS, ratio 3.5 : 1; it leaves the wet
  alone while every input sample is at or below 0.5629. Slope exactly 5/7;
  `W` is **F** (9.98306 +- 0.00005, no round form); the smoother must be single
  precision (in double precision the nulls stop at -81 to -120 dB). The dry is
  neither ducked nor delayed beyond `Λ`. The clipper is last and acts on dry
  and wet together.
- The reference's Return and Master are plain gains (`10^(dB / 20)` on the wet,
  and on the sum in front of the clipper) and stay at 0 dB here.

## 15. One internal sample, in order

```
u[k]            input converter of the pre-delayed host input (44.1 kHz host: the input itself)
e[k]            equaliser, per channel
t[k]            comb and Macro mix, per channel (comb delay at count k)
x_g[k]          = t_g[k - 44]
per group g:    for each line: oscillator step, length, read, a[n]
                voice inputs: sum of tap[n] a[n] over lines 1-8 and over lines 9-16
                v = (1/4) H a;  line[j][k] = own[j] x_g[k] + cross[j] x_other[k] + K_j(v[j])
if k mod 44 = 0: phase, cut-off, gain, Q target, Q of the four voices
y_g[k]          = voice A + voice B
then every host frame whose up-converter taps are complete: wet = output converter of y
```

## 16. What is exact, and how far

Nulls are `20 log10(rms(model - reference) / rms(reference))`, nothing fitted
but, above Macro 0, the phase. Captures of `proof.py` are fresh (programme seed
31415, own loud stimulus); nothing was fitted on them.

| | evidence |
| --- | --- |
| **Macro 0: exact.** | locked holdout (`score_network.py`): worst -114.07, mean -116.79 dB, for `network_model.py` and for `reference_render.py`. 15 fresh captures at 44.1, 48, 88.2, 96 kHz, Size 80.1, 162.1, 180.1, 32.95, 43.82 % among them: -113.5 to -119.0 dB, mean -116.3 dB (the old Size rule: -10 to -34 dB at those five). Half a second of impulses at 176.4 and 192 kHz (golden vectors): -109.3 and -108.0 dB. Recirculated sound alone -94 to -105 dB; steady broadband input about -100 dB; worst known corner Decay 30 s with Size 200 %, -108 dB |
| **Outer shell: exact.** | all outer controls moved at once, four rates, from a captured wet: -144.8 to -150.5 dB; with the level stage and the clipper working: -135.9 to -141.6 dB. Whole chain: -117.7 to -127.7 dB, loud -112.7 to -123.5 dB (without the level stage -4.5 to -17 dB) |
| **Macro above 0: exact up to the phase.** | 11 fresh captures, phase fitted as `score_tide.py` fits it (the description with the smallest weighted residual: the generator's form in ten, a spline with knots 0.25 s apart in one): Macro 100 % -98.9 to -108.0 dB (five, four host rates, one with pre-delay, Width 130 % and Mix 70 %), 50 % -103.5 to -105.1 dB, 25 % -101.0 to -107.1 dB. The campaign's fifteen stored-curve cases (`score_tide.py` on `reference_render.py`): worst -88.78, mean -98.50 dB at Macro 100 %; -91.62, -100.19 dB at 50 %; -96.10, -98.96 dB at 25 %. Steady noise -87 to -93 dB, late instance times -80 dB (section 6) |
| **The phase: statistical.** | section 13 |

## 17. Not in the model; left to the product

1. **The first seconds of an instance.** The reference is not repeatable in
   its first 1.5 s; the first 0.75 s are unrelated to the model at any Macro,
   and what enters then rings on at the Decay rate; a Macro-dependent glide is
   over 2.1 s after the start. Validate with 2.2 s of silence or more in front.
2. **Controls that move**, Macro included; Freeze; a reset or transport start.
3. **Everything outside the neutral baseline**: Brightness, the input filter,
   Transients, Ducking above 0 %.
4. **Host rates below 44.1 kHz and above 384 kHz**; clock signs at non-standard rates.
5. **Arithmetic not identified**: the comb delay (section 6), the last place of
   the oscillators' sine, the attenuation, the loop lag, the reference's output
   floor near 1e-36. None is audible.
6. Ocean's own Low Cut, High Damping, Focus, Harmony, Mono Safe and hold have
   no counterpart in the reference: at their neutral positions the chain above
   must be bit for bit what it is without them.

## 18. Mapping onto `FathomEngine.h`

| interface | this specification |
| --- | --- |
| `prepare(sampleRate)` | builds the branch tables of section 3, sizes the lines, sets `n = k = 0` |
| `reset()` | clears every signal state; `n = k = 0`; oscillators to their start values; a new phase instance from the seed |
| `setParameters` | sections 2, 4, 6, 8 to 10, 12, 13; `lowCutHz`, `highDampingHz`, `freeze` are Ocean's own |
| `processSample` | one host frame: the wet of section 1 (reference raw frame `n + Λ`), Width 100 %, in front of the level stage |
| `advanceIdle` | `n`, `k`, the 32 oscillators (one step per internal sample; they have no closed form), the comb's count, the block count and the phase advance, Q follows its target; signal states are cleared |
| `applyWidth`, `mixGains`, `mix`, `clip`, `LevelStage` | section 14; `mix` is the Mix sum of one channel in single precision; `LevelStage::process` takes the raw input frame and the wet of the same engine frame |

A null against the reference above Macro 0, and the Macro 100 % golden vectors,
need the phase of a capture. `setVoicePhaseForTesting` prescribes it: per
output the phase at the start of every block of 44 internal samples, which a
test computes from the rate and the knots (seconds, cycles) of section 13 that
`engine_golden.json` stores under `phase`.

## 19. Golden vectors

`engine_golden.json` describes each vector; arrays are in `engine_golden.npz`.

- Captures at Macro 0: impulses at 44.1, 48, 88.2, 96, 176.4 and 192 kHz and a
  short programme at 44.1 and 48 kHz, with Size 80.1, 162.1 and 180.1 % and
  pre-delay among the settings; two excerpts of 512 frames each, the reference
  and the model. Model against reference: -99 to -131 dB per excerpt.
- Captures at Macro 100 % at 44.1 and 48 kHz with their fitted phase: -84 to -99 dB.
- The outer laws and both converters on short inputs (model only).

A port should meet `model` far more closely than `model` meets `reference`. A
streaming engine written from this document and `engine_constants.json` alone,
with the timing contract of section 1 and no campaign module
(`Analyzer/Results/RevOceanCharacterization/work/specification/spec_engine.py`,
double precision apart from the places of section 20), meets `model` at -252
to -291 dB on all twenty excerpts and never reads a host frame that has not
arrived.

## 20. Every place where single precision matters

| place | rule | cost of double precision or another rounding |
| --- | --- | --- |
| line length (8) | `floorf(fl(P s) + 0.5f)` | one line a sample off at one Size in 200: -10 to -34 dB |
| oscillator accumulator (8) | single-precision state, increment, wrap constant and start values; start values not reduced | the whole null (a sinusoid at exactly 0.6 Hz: -14 dB); start values reduced beforehand: 50 dB |
| oscillator sine (8) | double-precision sine of the argument reduced with `fl(pi / 2)`, result rounded | 14 to 15 dB |
| modulated length (8) | `fl(fl(N) + fl(38.808002f * sine))`, no fused multiply-add | 5 to 16 dB |
| comb delay (6) | `fl(431.2075f + fl(236.0360f * fl(tri)))` | tap changes on another sample in 5 of 53 crossings |
| converter table (3) | single-precision entries; double-precision clock and table position | none measurable for the entries; the accumulated position is worth 46 to 51 dB at 66.15 and 132.3 kHz |
| level stage (14) | single-precision smoother | 20 to 60 dB on loud events |
| pre-delay (4) | single-precision milliseconds, product and quotient | one frame at 7 of 110 settings |
| Size knob (2) | scorer only | as the line length |

Everything else is double precision in the model. The reference's own signal
path is presumably single precision (**I**: its per-pass gains agree with the
model to 1.2e-8, its captures are single precision); what a single-precision
signal path costs a port against these nulls was not measured.
