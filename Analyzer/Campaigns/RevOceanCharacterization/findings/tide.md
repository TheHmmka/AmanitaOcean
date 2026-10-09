# Tide: what the Macro control does

Packet `tide`. Script: `measure_tide.py`. Numbers: `tide_structural_data/tide.json`.
Every figure below is in that file and the script regenerates it from captures in
about one minute once they are cached. The few figures marked "exploration" come
from the scratch scripts in `Analyzer/Results/RevOceanCharacterization/work/tide/`
and are not reproduced by the script.

**Operating point.** All captures are 100 % wet (`Mix` reads back `100`) and the
analysis is centred on `Macro` reading back `100`, the owner's listening point.
Lower Macro values were captured only to find the Macro law. Other controls:
campaign baseline, Decay 0.5 s, 48 kHz, 10 s warm-up, fresh instance per capture.

Tags: MEASURED (read off captures), FITTED (model constant, residual given),
INFERRED (reasoned, not tested), OPEN.

## 1. Result in one picture

```
x_L ─┬─ cos(mπ/2) ───────────────────────────┐
     └─ comb delay D_L(t), feedback −8 dB ─ HP 35 Hz ─ sin(mπ/2) ─(+)─► Macro 0 network ─┐
x_R ─┬─ cos(mπ/2) ───────────────────────────┐                      (unchanged)          │
     └─ comb delay D_R(t), feedback −8 dB ─ HP 35 Hz ─ sin(mπ/2) ─(+)─►                   │
                                                                                          │
   per output channel:  early arrivals ─► voice A: gain g(φ)    · low-pass fc(φ),    Q ───┤
                        later arrivals ─► voice B: gain g(φ+½)  · low-pass fc(φ+½),  Q ───┴─► out
```

m is Macro, 0 to 1. Three things move:

1. a **comb delay** in front of the network, one per input channel, swept by a
   200 s triangle between 4.4 and 15.1 ms. It is fully deterministic;
2. two **voices** per output channel, each a gain window times a resonant
   two-pole low-pass whose cutoff slides from 19 kHz down to 460 Hz and jumps
   back while its gain is zero. The voices are half a cycle apart and their
   gains add up to 1.40. One cycle lasts about 18 s;
3. the **phase** of that cycle. Its mean rate and start value are fixed; a slow
   random wander on top of it is the only thing that differs between two
   instances.

The network itself (delays, the 0.6 Hz tap modulation and its phases, the
recirculation) is the Macro 0 network, unchanged.

## 2. Method

The Macro 0 network is deterministic and linear, so any stimulus can be rendered
through it. If the layer in front of the network is a delay, an impulse at time t
reaches the network at t + D, and the Macro 0 response to an impulse **at t + D**
is the right reference: the taps of the network move by up to 0.0033 samples per
sample (0.6 Hz, ±42 samples), so a reference rendered at t is wrong by up to
±2.2 samples per arrival at D = 656.

`measure_tide.py` therefore renders, at Macro 0, impulses at the first comb exit,
at the second, and at the third to sixth with the comb's weights, and fits each
Macro > 0 response as

    y = LP(fc, Q) * [ g_dry·u_dry + HP * (g_1·u_1(n−δ_1) + g_2·u_2(n−δ_2) + g_3·u_3(n−δ_3)) ]

on the window that holds voice A only (from the first arrival to raw sample
2640, plus D at Macro 100 %). Per impulse the unknowns are fc, Q, the gains and
one fractional delay per reference. No arrival positions, tap gains or tap
shapes are modelled: they all come from the Macro 0 captures.

Captures: left impulses 0.5 s apart over 98 s (the grid stimulus of
`datasets.py`) at Macro 100 % (four realisations), 90, 75, 50, 25 and 10 %;
alternating left and right impulses at 100 %; 11 s trains at 0.2, 1, 2 and 5 %;
one 110 s train after a 60 s warm-up. About 0.9 GB of cache.

Fit quality (median null over 392 impulse × channel fits per capture, voice open):

| Macro | 100 % (4×) | 90 % | 75 % | 50 % | 25 % | 10 % |
|---|---|---|---|---|---|---|
| null, dB | −32.0 … −33.0 | −33.8 | −33.7 | −31.9 | −27.4 | −20.7 |

On the cleanest window (first arrivals up to their first comb echo, 34
impulses) the bilinear low-pass reaches −39.3 dB; an all-pole two-pole reaches
−29.1 dB and an unwarped analogue prototype −32.1 dB. Above about 12 kHz the
description degrades (see limits).

## 3. The comb delay

MEASURED at Macro 100 %, from the fractional delays of 102 well-conditioned
impulses, averaged over four realisations:

- rising flank: D = 469.341 + 5.13831·t samples (t = seconds since processing
  started), rms 0.007 samples;
- falling flank: slope −5.13828 samples/s, rms 0.007 samples;
- apex at t = 50.0014 s, D = 726.264 samples;
- spread between the four realisations: 0.004 samples. **The delay is the same
  in every instance.**

FITTED model, used for all references:

    D_L(t) = 469.34 + 256.90 · tri(t / 200 s)      samples at 48 kHz
    D_R(t) = D_L(t + 50 s)                         (right input a quarter period ahead)

tri is a unit triangle that starts at 0 going up. Model error against the
measured delays: mean +0.019, rms 0.021 samples (the measured swing is 256.92).
In milliseconds: 9.778 ± 5.352, that is 4.426 to 15.130 ms; period 200.005 s
measured, 200 s assumed; origin within 2 ms of the start of processing.

- Range below the apex: a second capture after a 60 s warm-up follows the
  triangle through its minimum and back up (394 first-arrival onsets between
  60 and 169 s: period 200.08 s, minimum 215 samples with the +1 to +3 sample
  bias of onset picking; model minimum 212.4). MEASURED, coarse (rms 2 samples).
- The element is a **comb**, not a pure delay: every arrival is followed one
  delay later by an inverted copy. Gain of the second exit relative to the
  first: −0.3986 (quartiles 0.3980 to 0.3990, 1137 fits); third-exit group
  +0.1580 = 0.3975². FITTED. The round value −8 dB = 0.39811 is inside the
  quartiles. The loop delay equals the first delay: the second exit, fitted on
  its own, lies 0.10 samples after two model delays (quartiles 0.08 to 0.12).
- The delayed path has a **high-pass**: without it the null at Macro 100 % is
  −23.6 dB, with a one-pole at 35 Hz −35.6 dB (28 Hz: −34.3, 42 Hz: −34.4).
  FITTED 35 ± 5 Hz. At Macro 50 % the delayed path needs it as well (−27.1 dB
  without, −34.8 dB at 28 Hz, −34.6 dB at 35 Hz) and the undelayed path must
  not have it (high-pass on both paths: −23 dB, exploration).
- **Per input channel.** With alternating left and right impulses the right
  impulses fit only with the quarter-period lead (null −33.1 dB for both
  inputs, comb gain 0.3987 and 0.3984, residual delay within the ±0.5 samples
  of reference rounding). With the left law the right impulses do not fit at
  all (0 dB). MEASURED over 58 s; the lead was not followed over a full period.
- **In front of the network.** References rendered at the exit times align all
  arrivals to a fraction of a sample; references rendered at the impulse time
  leave ±2 samples of misalignment between arrivals, the size the tap
  modulation predicts. MEASURED.
- The delay does **not** scale with Macro. Macro crossfades the undelayed and
  the comb path (section 7).

The README's "about 526 samples at 100 %" is this triangle read 11 s after the
start of processing.

## 4. The voice filter

Type: two-pole resonant low-pass, bilinear form (RBJ cookbook low-pass), with a
gain. FITTED, nulls above. Its state is one-dimensional: gain, cutoff and Q are
functions of one phase φ in [0, 1).

**Gain** (Macro 100 %): maximum 1.403 (99.5th percentile of 1137 fits), reached
at fc = 4014 Hz. With

    g(φ) = 1.40 · sin²(π φ)

the cutoff becomes a single smooth curve of φ (next item) and the second voice
lands exactly half a cycle away (section 5), which is how the window was
identified. FITTED; a sin window instead of sin² is excluded by section 5.

**Cutoff** (Macro 100 %), 1110 fits with 0.03 < g < 1.38:

    fc(φ) = f_low + (f_high − f_low) · (e^(−k φ) − e^(−k)) / (1 − e^(−k))
    f_low = 460.3 Hz, f_high = 19277 Hz, k = 2.939      rms error 0.25 %

Round values tested: (500 Hz, 20 kHz, k = 3) gives 2.9 % rms and is rejected;
(460 Hz, 19.27 kHz, 2.94) gives 0.35 %. The cutoff always falls; at φ = 1 the
gain is zero and the cutoff returns to f_high.

**Resonance.** Q is a function of the cutoff in Hz, piecewise linear:

| fc | 500 Hz | 1000 Hz | 3000 Hz | ≥ 5000 Hz |
|---|---|---|---|---|
| Q at Macro 100 % | 5.2 | 7.0 | 2.1 | 1.40 |

Free knots land at 999, 2996 and 4928 Hz with Q 7.00, 2.10, 1.40 (rms 0.016
over 1111 fits); the round table above scores 0.018. FITTED.

Not resolved: above about 13 kHz the fitted Q falls (about 1.0 at 17 kHz at
Macro 100 %). The gain is below 0.2 there and the filter model is weakest, so
this may be an artefact. OPEN.

## 5. Two voices, and what each one filters

**Voice A** carries everything that arrives before the ninth first-pass arrival:
the eight first arrivals of an output (Macro 0 centres 1326 to 2409 samples on
the left) and the weak arrivals between them and raw sample 2640. One filter
fits that whole window at −32 dB. Fitted on the first arrival alone, the same
filter reproduces the other seven with per-arrival gains of 1.000 ± 0.006
(exploration, realisation 0). MEASURED.

**Voice B** carries the later strong arrivals. When voice A is closed
(g_A < 0.012, one impulse per realisation, `partition` in the data file), the
left output still contains, at +0 to +4 dB relative to the filtered Macro 0
reference (g = 1.40 is +2.9 dB):

- the strong arrivals near raw 2720–2790, 3100–3180, 3600–3690, 4210–4290,
  4960–5040 and 5890–5970 (the continuation of the first-pass series, arrivals
  9 to 14);
- most weak arrivals after raw 3800.

Missing (−11 to −81 dB): arrivals 1 to 8 and most weak arrivals between raw
2480 and 3600 (a few weak ones next to arrival 9 stay). MEASURED on four
impulses.

**Relation between the voices.** Fitting voice B on arrivals 9 to 11 after
removing voice A's modelled part:

| | left out, left in (4 realisations) | right out, right in |
|---|---|---|
| impulses used | 263 | 18 |
| fc_B measured / fc(φ_A + ½), median | 1.0002 (quartiles 0.985 to 1.013) | 1.0003 (0.983 to 1.013) |
| g_A + g_B, median | 1.32 (1.27 to 1.37) | 1.36 (1.29 to 1.38) |
| g_B measured / 1.40 sin²(π(φ_A + ½)) | 0.91 | 0.96 |

So φ_B = φ_A + ½ with the same cutoff curve. MEASURED for the cutoff. The gain
of voice B reads 4 to 9 % low because the fit window (null only −6 dB) also
holds voice B arrivals that the three-arrival reference does not contain; the
sum g_A + g_B = 1.40 is therefore INFERRED from the window law, not measured to
better than 6 %. With a sin window the predicted fc_B would be off by up to
28 %.

**Per output channel.** In the alternating capture the cutoff read from a
right-input impulse lies on the curve through the neighbouring left-input
impulses of the same output: rms 0.46 % (left output), 1.18 % (right output).
The two outputs follow different phases. MEASURED. The comb belongs to the
input, the voices to the output.

## 6. The phase in time, and what differs between instances

From the cutoff, φ(t) was unwrapped for 16 curves (8 at Macro 100 %, 8 at 25 to
90 %), each 98 s:

- mean rate 0.0553 cycles/s, period 18.09 s (FITTED jointly; a fixed 0.0550 or
  1/18 fits equally well, 0.0500 does not);
- phase at the start of processing: left 0.380, right 0.218 (left − right
  0.162). FITTED; the scatter of single curves around these is the wander
  below. Two of four realisations have the left voice A closed at exactly
  t = 1.0 s. **The start phase is not random.**
- **wander** (unwrapped phase minus the straight line): overall sd 0.160
  cycles, range −0.57 to +0.32. Its spread across curves grows from 0.096
  cycles at 11 s to 0.20 cycles at 101 s after the start. MEASURED;
- instantaneous rate (0.5 s differences, Macro 100 %): sd 0.030 cycles/s,
  percentiles 1/5/50/95/99 = −0.032/0.012/0.054/0.101/0.149; 2.6 % of the
  values are negative, i.e. the sweep occasionally runs backwards (cutoff and
  gain retrace the same law). Autocorrelation of the rate: 0.87 at 0.5 s, 0.35
  at 2 s, zero at 3.5 s, −0.28 at 6.5 s, back to zero near 15 s. MEASURED;
- left and right rates are uncorrelated in three of four realisations (−0.04,
  0.11, 0.10; one −0.33). The same holds between realisations. MEASURED;
- the mean rate per Macro value is 0.0510, 0.0533, 0.0556, 0.0563, 0.0565
  cycles/s at 25, 50, 75, 90, 100 %. Two curves per value and a wander of 0.16
  cycles give about ±0.002, so a weak dependence is possible. OPEN.

Between two instances at the same Macro value, **only the wander differs**: the
delay (0.004 samples), the filter laws, the start phases and the network are the
same. That is the non-repeatability of README item 9.

What generates the wander is not established. The data are compatible with a
bounded smooth random modulation of the phase (negative lobe of the rate
autocorrelation) plus a slow diffusion (growing spread); 16 curves cannot
separate the two. OPEN, statistical.

## 7. Macro law

MEASURED and FITTED on one capture per value (98 s).

**Mix of the two input paths.** Undelayed and comb copies of the early arrivals
share one filter; their gains have a constant ratio:

| Macro | 25 % | 50 % | 75 % | 90 % |
|---|---|---|---|---|
| atan(g_comb / g_dry), degrees | 22.29 | 44.89 | 67.44 | 80.97 |
| 90° · m | 22.5 | 45 | 67.5 | 81 |

Quartile width 0.04° to 0.09°. So g_dry = g·cos(mπ/2), g_comb = g·sin(mπ/2): an
equal-power crossfade into the comb. The comb gain stays 0.394 to 0.398. At
Macro 100 % nothing is left at the undelayed positions: −66 dB re Macro 0
(median; that is the tail of the preceding response). At 90 to 99.9 % the
delayed copies sit at the same D(t) as at 100 % (onsets, exploration).

**Depth of the gain window.** The minimum gain of the first arrivals over 11 s:

| Macro | 0.2 % | 1 % | 2 % | 5 % | ≥ 10 % |
|---|---|---|---|---|---|
| 1 − g_min | 0.0352 | 0.1760 | 0.3520 | 0.8797 | 0.999 |
| (1 − g_min) / m | 17.59 | 17.60 | 17.60 | 17.59 | |

    g = (1 − d) + d · 1.40 · sin²(π φ),     d = min(1, 17.6 · m)

Full depth from Macro 5.7 % up. MEASURED for the minimum; the form of g between
the extremes is INFERRED (maxima seen: 1.015, 1.073, 1.134, 1.365; the form
gives 1.014, 1.070, 1.141, 1.352, not all reached in 11 s). The null of the
whole capture against Macro 0 is −33, −18, −13, −3 dB at 0.2, 1, 2, 5 %.

**Gain maximum.** 1.403, 1.403, 1.400 at 100, 90, 75 %. The fits read 1.383 and
1.343 at 50 and 25 %, but a model-free estimate (undelayed first arrivals, 0.3
to 1 kHz, 99th percentile) gives 1.44, 1.43, 1.42, 1.42, 1.41 at 90, 75, 50, 25,
10 %, which includes the low-pass's own rise of 1 to 5 % below its peak. Read
together: 1.40 ± 0.02 at every Macro value; the low fitted values are a model
artefact at high cutoff. INFERRED.

**Cutoff range.** The curve of section 4 keeps its shape (k = 2.93 to 3.16,
f_high 19.2 to 20.2 kHz, rms 0.19 to 0.36 %); only its lower end moves:

| Macro | 100 % | 90 % | 75 % | 50 % | 25 % |
|---|---|---|---|---|---|
| f_low, Hz | 460 | 913 | 1869 | 4582 | 9850 |
| cutoff at the gain maximum, Hz | 4014 | 4379 | 5018 | 7344 | 11620 |

    f_low(m) = f_high − (f_high − 460) · (1 − e^(−2.55 m)) / (1 − e^(−2.55))      errors +1.0, +0.5, −0.4, −2.0 %

**Resonance.** The table of section 4 holds at every Macro value when its upper
part scales with a plateau Q0(m): Q = 5.2 at 500 Hz, 7.0 at 1 kHz, 1.5·Q0 at
3 kHz, Q0 from 5 kHz up. Rms error 0.018, 0.030, 0.033, 0.023, 0.012 at 100, 90,
75, 50, 25 %.

| Macro | 100 % | 90 % | 75 % | 50 % | 25 % |
|---|---|---|---|---|---|
| Q0 | 1.40 | 1.52 | 1.74 | 2.44 | 4.01 |

    Q0(m) = 1.411 + 6.015 · (e^(−3.14 m) − e^(−3.14)) / (1 − e^(−3.14))        residual ≤ 0.009

(round values 1.4, 7.0, bend 3: residual up to 0.12 at 25 %). At 10 % the cutoff
stays between 15.2 and 20.2 kHz and Q reads 4.3 to 5.5 where the gain is high;
the formula gives 5.7. FITTED; below 25 % only this one consistency check.

**Rate.** The cycle does not get slower or faster with Macro beyond the ±0.003
cycles/s noted above.

## 8. Position in the structure

- **Comb: before the network, per input channel.** Section 3.
- **Voices: after the network's recirculation, per output channel, applied
  once.** Evidence:
  1. Band decay is unchanged. Energy 6000 and 12000 samples after the first
     arrivals, relative to the first 3000 samples, left output, octaves 100 Hz
     to 12.8 kHz: Macro 0 −16.9 to −20.0 dB and −32.5 to −37.1 dB; Macro 100 %
     (voice A open) −18.0 to −20.2 dB and −33.1 to −36.4 dB. A low-pass inside
     the loop at 0.5 to 5 kHz would add tens of dB per pass above its cutoff.
     MEASURED. (The right output reads 2 to 4 dB lower at Macro 100 % because
     the early part is then boosted by up to 2.9 dB.)
  2. The late response keeps its energy while voice A swings over 80 dB: raw
     samples 4840 to 12040, left output, relative to the Macro 0 reference:
     mean −0.1 dB, sd 0.7 dB, range −1.7 to +1.4 dB; +0.7 dB when voice A is
     closed, +0.4 dB when it is full. MEASURED. The late field therefore holds
     both voices in comparable amounts.
  3. Voice B's arrivals are present at full level while voice A is closed:
     they did not pass voice A. MEASURED.
  4. The late field of the right output for a left input carries the right
     output's filter, not the left one's (spectral ratios, exploration).
     MEASURED once.
- Which of the two voices a given late arrival passes is known only for the
  arrivals listed in `partition`. INFERRED rule: an arrival passes the voice of
  the line it leaves the network through, lines 1 to 8 (short) feeding voice A
  and the longer ones voice B. OPEN until the Macro 0 structure is known.
- **The network is otherwise unchanged**, including the 0.6 Hz tap modulation
  and its phases: one filter applied to Macro 0 references reproduces the early
  response at −32 dB with fractional delays that follow a straight line to
  0.007 samples. A different modulation depth, rate or phase would show as
  arrival misalignment of up to ±42 samples. MEASURED (through the null).

Rejected on the way (exploration): (a) one slowly varying filter applied to the Macro 0
response at the same impulse time: null −1 to −5 dB; (b) the same with the
reference moved to the delayed time but one filter for the whole response:
−11 dB on the first pass, −1 dB later; (c) the unfiltered comb-expanded
reference for the late field: −1 to −2 dB.

## 9. Level

Wet energy of the grid capture (left impulses, 98 s, Decay 0.5 s) relative to
Macro 0. MEASURED; the stimulus is impulsive and the result is dominated by the
early arrivals.

| Macro | 10 % | 25 % | 50 % | 75 % | 90 % | 100 % (4 realisations) |
|---|---|---|---|---|---|---|
| left, dB | +2.18 | +3.03 | +1.20 | −0.51 | −1.42 | −1.18, −1.27, −1.28, −1.37 |
| right, dB | +1.72 | +2.72 | +0.61 | −1.50 | −2.43 | −1.62, −0.93, −1.28, −2.00 |

At 100 %, left output, by octave (0–100, 100–200, …, 12.8–24 kHz):
−6.1, −0.6, −0.4, 0.0, +2.2, +3.8, +0.5, −6.3, −20.4 dB (mean of four). The
+3 dB at 25 % is the resonance (Q 4) sitting at 10 to 14 kHz, where the Macro 0
response has most of its energy.

## 10. Generative description

Sample rate 48 kHz; t = seconds since processing started; m = Macro in [0, 1].

```
per input channel c (L = 0, R = 1):
    D_c(t)  = 469.34 + 256.90 · tri(t/200 + 0.25·c)                 samples, tri(0) = 0 rising
    v_c(n)  = u_c(n − D_c(n)),   u_c(n) = x_c(n) − 0.398 · v_c(n)   comb, read at the output
    in_c(n) = cos(mπ/2) · x_c(n) + sin(mπ/2) · HP35[v_c](n)         one-pole high-pass, 35 Hz

(in_L, in_R) → Macro 0 network → per output o: early part e_o, later part l_o     (section 5)

per output channel o:
    φ_o(t)  = φ0_o + 0.0553·t + wander_o(t)        φ0_L = 0.38, φ0_R = 0.22 (cycles)
    d       = min(1, 17.6·m)
    g(φ)    = (1 − d) + d · 1.40 · sin²(πφ)
    f_low   = 19277 − 18817 · (1 − e^(−2.55 m)) / (1 − e^(−2.55))
    fc(φ)   = f_low + (19277 − f_low) · (e^(−2.94 φ) − e^(−2.94)) / (1 − e^(−2.94)),  φ taken mod 1
    Q0      = 1.411 + 6.015 · (e^(−3.14 m) − e^(−3.14)) / (1 − e^(−3.14))
    Q(fc)   = linear through (500, 5.2), (1000, 7.0), (3000, 1.5·Q0), (5000, Q0); Q0 above
    out_o   = g(φ_o)   · LP[fc(φ_o),   Q] e_o  +  g(φ_o+½) · LP[fc(φ_o+½), Q] l_o
```

LP is the bilinear two-pole low-pass. The cutoff jumps from f_low to 19.3 kHz
while that voice's gain is zero.

| Constant | Value | Uncertainty | Tag |
|---|---|---|---|
| delay mid, swing | 469.34, 256.92 samples | 0.02 samples; ±0.1 sample common offset from the filter's phase model | MEASURED |
| delay period, origin | 200.0 s, start of processing | 0.01 s, 2 ms | MEASURED |
| right-input lead | 0.25 period | fits to ±0.5 sample over 58 s | MEASURED |
| comb feedback | −0.3986 (−8 dB) | ±0.0005 | FITTED |
| comb high-pass | 35 Hz, one pole | ±5 Hz; place relative to the feedback not resolved | FITTED |
| path mix | cos, sin of mπ/2 | 0.1° | MEASURED |
| gain window | 1.40 · sin² | 1.40 ± 0.01 at 100 %; ± 0.02 below | FITTED |
| depth slope | 17.6 per unit Macro | ±0.01 | MEASURED |
| cutoff curve at 100 % | 460.3 Hz, 19277 Hz, k 2.939 | 0.25 % rms | FITTED |
| f_low(m) bend | 2.55 | curve within 2 % of five values | FITTED |
| Q knots | 500, 1000, 3000, 5000 Hz | Q rms 0.02 to 0.03 | FITTED |
| Q0(m) | 1.411, 7.43, bend 3.14 | 0.01 on five values | FITTED |
| phase rate | 0.0553 cycles/s | ±0.001 | FITTED |
| start phases | 0.38, 0.22 | ±0.04 | FITTED |

**Statistical, not deterministic:** wander_o(t), independent per output channel
and per instance. Target statistics: phase deviation 0.10 cycles (sd across
instances) 10 s after the start growing to 0.20 at 100 s; rate sd 0.030
cycles/s with the autocorrelation of section 6. A band-limited random
modulation of the rate with those two properties is a sufficient surrogate
until the generator is identified.

## 11. Limits and open questions

- Everything was measured at Decay 0.5 s, Size 100 %, 48 kHz, Width 100 %, with
  Macro set before processing starts. Whether the delay scales with Size or
  with the sample rate, and what a Macro change during playback does to the
  oscillators, is OPEN.
- The split of the late field between the voices is known for the arrivals in
  `partition` only (section 8).
- The wander generator is OPEN (section 6).
- Above about 12 kHz the low-pass model is weaker: nulls −27 dB at Macro 25 %
  and −21 dB at 10 %, fitted gain maximum low by up to 4 %, Q roll-off above
  13 kHz unexplained. Below Macro 25 % only the gain depth and the path mix are
  established; cutoff and Q there rest on the formulas and one check at 10 %.
- The form of g between its extremes below Macro 5.7 % is INFERRED.
- The right-input lead and the delay minimum were not followed with the
  precise method over a full 200 s period.
- Voice B's gain is measured only to about 6 % (section 5), and for the right
  output only on 18 impulses.
- The time-varying implementation of the low-pass (state handling while the
  coefficients move) is not observable at these rates and was not studied.
- The fitted undelayed path sits 0.10 samples earlier than the Macro 0
  reference at every Macro value below 100 %: a small phase error of the
  low-pass model, which also bounds the absolute accuracy of the delay.
