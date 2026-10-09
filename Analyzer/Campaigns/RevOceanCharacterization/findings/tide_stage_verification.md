# tide_stage: independent verification

Packet `tide_stage_verification`. Checked: `findings/tide_stage.md`, `tide_stage.py`, `fit_tide_stage.py`,
`tide_structural_data/tide_stage.json`. Scripts and arrays of this check:
`Analyzer/Results/RevOceanCharacterization/work/tide_stage_verification/` (`tsv.py` and `tsv_kernels.c`: my own
comb, voice, laws and fit, written from the formulas of the findings document; `designs.py`: stimuli;
`v00` to `v25`: one script per section). The author's analysis functions are not used; the delivered
module is called only where a section says so. 42 captures, 0.99 GB of cache, every input sample at or
below 0.5.

Tags: MEASURED, FITTED (residual given), INFERRED, OPEN. Verdicts: CONFIRMED, REFUTED, PARTLY, UNVERIFIED.
A null is `20 log10(rms(model - reference) / rms(reference))` per impulse and output with nothing fitted
but the phase of the voice and its rate, unless stated.

## 0. Short answer

The packet holds. Every closed form it states was re-derived with independent code on fresh captures
(other impulse times, warm-ups of 10, 17.3, 21.9, 25, 30 and 60 s, eight Macro values the author did not
measure, other Size and Decay, four new instances at Macro 100 %) and none failed. Free refits return
the published constants, mostly more tightly than the author states. Three things a product should know
are not in the author's summary:

1. **The comb, not the voice, sets the -94 dB floor**, and the floor is not uniform. A transient that
   leaves the comb within about 150 samples before a downward whole-number crossing of the delay is
   reproduced at about -70 dB even when the tap change is on the right sample, and at -37 dB when it is
   not (3 of 53 unselected crossings). Energy-weighted, the Tide stage stands at -75 to -86 dB at
   Macro 100 %, not at the -93.5 dB of the median.
2. **Voice B is now measured** (the author left it INFERRED): the same forms at exactly half a cycle,
   over the 136 ms after the voice A window, at -104 dB median.
3. **"At every host rate" is shown for 44.1 and 48 kHz only.** At 96 kHz I confirm the 1 ms update
   of the voice; the comb at 88.2 and 96 kHz is untested.

## 1. Verdicts

| # | Claim of the packet | Verdict | My numbers |
|---|---|---|---|
| 1 | Comb and voice run inside at 44.1 kHz at every host rate | PARTLY | 44.1 and 48 kHz CONFIRMED (delivered model on fresh 48 kHz captures: -78.0, -80.4, -81.1 dB at Macro 100, 40, 7 %). Voice update every 44.00 internal samples at 48 and 96 kHz hosts (tone lines). 88.2 kHz and the comb at 96 kHz: tested neither by the author nor by me |
| 2 | Comb: all-pass read, feedback -0.4, bilinear high-pass 30 Hz and low-pass 20 kHz inside the loop | CONFIRMED | relation with my own comb -96.3 dB; free refit: feedback -0.400000 +- 0.000003, high-pass 30.000 +- 0.001 Hz, low-pass 20000.0 +- 0.3 Hz |
| 3 | Delay law 431.2075 + 236.0360 tri((c - 85.7)/200 s + channel/4), from the start of processing | CONFIRMED | free offsets: mid -5e-6, swing +7e-6 samples, origin 85.7 to 85.8 samples; residual 2.7e-5 samples rms (221 cases, both inputs, both flanks, 31 to 129 s) |
| 4 | The delay is a single-precision number; arithmetic not identified | CONFIRMED | unselected crossings: published law right in 50 of 53, double precision in 45 of 53; all 5 crossings where the two differ follow single precision. See weak spot 1 |
| 5 | Mix cos / sin (90 deg x Macro) | CONFIRMED, tighter | delayed / undelayed path relative to tan(90 deg m): medians within 2e-6, quartiles within 5e-6 at Macro 0.5, 2, 4, 5.5, 7, 20, 40, 85 % |
| 6 | Comb and voice independent of Size and Decay | CONFIRMED, extended | -98.6 dB at Size 80 % (Macro 100 %), -99.2 dB at Size 130 % with Decay 1.3 s (Macro 65 %), -97.9 dB at Decay 4 s (Macro 100 %) |
| 7 | The voice is the fixed 20 kHz, Q 1 low-pass, moved; bilinear transfer function at 44.1 kHz | CONFIRMED | with that low-pass divided out of the Macro 0 capture the relation reaches -96 to -120 dB from Macro 100 to 0.5 % |
| 8 | Parameters updated once per 44 internal samples and held; blocks at multiples of 44 of the internal count; host block plays no part | CONFIRMED | hold -96.8 dB, per sample -76.4, blocks +-1 sample -85.1 / -85.2, +-2 -82, 22 -70.9; same with warm-ups whose sample count is 8, 14, 32 and 34 mod 44 and with a host block of 100 samples |
| 9 | Trapezoidal state-variable recursion, gain behind the filter | CONFIRMED as the best of the forms tried | direct form I -86.0, transposed direct form II -86.5, gain in front -84.7, against -96.8 dB; voice alone (two instances, no comb model) -110 dB |
| 10 | Cut-off law 20000 - 19990 E(0.88 m) E(phi) | CONFIRMED | cut-off scale within 4e-6 at eight new Macro values; top 20000.00 +- 0.02 Hz, bend 3.0000 +- 0.0001, amount 0.88000 +- 0.00001, end 10.0 +- 0.2 Hz |
| 11 | Gain (1 - d) + d sqrt(2) sin^2(pi phi), d = min(1, 17.6 m) | CONFIRMED, tighter | median gain offset within 4e-6 at eight new Macro values; depth slope 17.6000 +- 0.0002 (author: +- 0.003); top sqrt(2) +- 3e-5 |
| 12 | Q law (knots, plateau and 3 kHz value against Macro, slope below 1 kHz), Q 9.5 blocks late | CONFIRMED | Q scale within 1e-5 at new Macro values; plateau 7.0000 - 5.6400 E(m), 3 kHz knot 2.0900 and 1 kHz knot 7.0100 at Macro 100 % (each +- 0.0005); slope below 1 kHz 3.645e-3 per Hz (34 new cases, 663 to 989 Hz); smoothing 10.0 +- 0.2 blocks, form open |
| 13 | Nulls: model -78.0 to -81.5 dB; stage alone -93.5 dB at Macro 100 % | CONFIRMED | model on fresh captures -78.0 to -81.8 dB; stage alone -96.3 and -96.1 dB (two new instances, 258 cases) |
| 14 | Phase alone is limited by the irregular rate and misses -60 dB above Macro 25 % | CONFIRMED | phase only: -54.5, -59.3, -62.9, -66.0, -69.4 dB at Macro 100, 85, 40, 20, 7 %; the fitted rate is the true derivative of the phase (section 6) |
| 15 | Voice B follows the same forms half a cycle away (INFERRED, not measured) | CONFIRMED by a new measurement | section 7: -103.6 dB over 6000 samples after the voice A window; shift from half a cycle 0.000000 +- 0.000001 cycle |

Nothing in the packet contradicts README.md or the verified wave-1 findings. Where it differs
(feedback 0.4, low-pass in the loop, cut-off from 20 kHz, origin 85.7 internal samples instead of "the
reported latency") the packet is right and my refits agree; the three README lines the author lists
should be changed by the lead.

## 2. Method

**Relation between captures, linear in the Macro 0 side.** At a 44.1 kHz host the internal rate is the
host rate. The reference is linear at Macro 0, so three Macro 0 captures of one impulse train carry
every Macro value:

```
X0 = Macro 0 capture of the train x                    (undelayed path)
C0 = Macro 0 capture of comb(x)                        (my comb, published constants)
Dk = Macro 0 capture of d comb(x) / d constant k       (feedback, high-pass corner, low-pass corner, delay)
T  = capture of x at Macro m
T  ?= voice_phi[ unrest( cos(90 deg m) X0 + sin(90 deg m) (C0 + sum_k delta_k Dk) ) ]
```

`unrest` divides out the resting 20 kHz, Q 1 low-pass. With `delta_k` = 0 this is the author's relation
null; with `delta_k` free it is a refit of the comb constants that does not involve the network model
or any of the author's code. One set of Macro 0 captures serves every Macro value, so the path mix is
tested directly as the ratio of the two Macro 0 parts.

**Design P** (main set): 71 impulses of 0.5, random input channel, 1.25 to 1.5 s apart (the author:
0.8 to 1.0 s), 100 s, warm-up 30 s, plug-in time 31 to 128 s; left delay rising to 50 s and falling
after, right delay falling to 100 s and rising after. Macro 100 % on four new instances; Macro 85, 40,
20, 7, 5.5, 4, 2 and 0.5 % on the first 50 s. Window: voice A only, 1302 samples from the first line
to just before the ninth, behind 600 samples of run-in; a case is "open" above -30 dB re the voice at
rest, as in the packet.

**Own implementation.** `tsv.py` holds a comb and a voice written from section 1 of the findings.
Against the delivered module on random input they differ by 3e-16 (comb) and 2e-14 (voice): the
document describes the module completely. MEASURED.

## 3. Reproduction, holdout, levels

- `fit_tide_stage.main()` run with its output redirected to my scratch folder: 131 s, no new render,
  result byte-identical to `tide_stage.json` (sha256 `0c74119f...6d9bee`). CONFIRMED.
- No holdout is read: neither file imports `datasets` or mentions a holdout loader, and no script in
  the author's scratch folder does. The packet reports no holdout score, so there was none to re-run.
- Stimulus levels in the regenerating script are 0.5 (impulses) and at most 0.45 (tones). CONFIRMED.
- Small inconsistency: the docstring of `fit_tide_stage.py` says 0.85 GB and three minutes, the
  findings 1.07 GB and 2 min 12 s.

## 4. Relation at Macro 100 %

| | cases | median | quartiles | worst tenth | energy-weighted | phase only |
|---|---|---|---|---|---|---|
| instance 3 | 128 | -96.32 dB | -100.36 / -90.84 | -86.62 | -75.1 | -54.47 |
| instance 4 | 130 | -96.14 dB | -100.08 / -90.70 | -86.53 | -75.8 | -54.62 |

MEASURED. The author's -93.5 dB is confirmed. My median is 2.7 dB lower; the longer gaps (a weaker tail
of the previous impulse) may be the reason, which I did not separate. No dependence on the path (-95.4 to -98.6 dB
for the four input-output pairs), on the flank of the triangle, or on the phase (-92.7 to -97.9 dB in
five phase bands). Two impulses 1.29 s before and 0.19 s after the left delay's maximum null at -89 to
-100 dB.

**What limits it.** The author lists this as unknown. It is the comb: (a) the open cases above -84 dB are
all transients that leave the comb shortly before a downward whole-number crossing of the delay
(section 5); (b) a third number per case (phase curvature) gains 2 dB only (-98.7 dB); (c) the
two-instance relation of section 7, which takes voice A's input from a second Macro 100 % capture and
needs no comb model, reaches **-110.4 dB** on the same window. So the voice is known to about -110 dB
and the comb emulation to about -96 dB. MEASURED.

**Dense material** (`v18`): 71 noise bursts of 1300 samples instead of impulses, two instances, 253
open cases: median -92.5 dB, energy-weighted **-85.6 dB**; bursts that straddle a crossing -78.6 dB
energy-weighted (40 cases), the others -92.9 dB. MEASURED. The worst burst is at -73.1 dB; a tap change
on the wrong sample inside a burst would show near -47 dB (INFERRED from the recursion), so none occurred
in this set.

## 5. The comb

**Free refit** (`v05`; 221 cases with a base null below -88 dB, two instances; median and quartiles of
the per-case offsets, each fitted next to phase and rate):

| constant | published | offset, median | quartiles |
|---|---|---|---|
| feedback | -0.4 | +2e-7 | -2.5e-6 / +2.3e-6 |
| high-pass corner | 30 Hz | +0.00015 Hz | -0.0004 / +0.0009 Hz |
| low-pass corner | 20 kHz | +0.004 Hz | -0.21 / +0.24 Hz |
| delay | law | -3e-7 samples | -1.0e-5 / +0.8e-5 samples |

With all four free the median null moves from -97.5 to -99.7 dB. FITTED: the published values are at
the optimum; -8 dB (-0.398107) and -0.3986 lie 0.0019 and 0.0014 away, about 400 and 300 times the
quartile range of 4.8e-6.

**Delay law.** The per-case delay offsets by input and flank: left rising -6.6e-6 (17 cases), left
falling +5.2e-6 (78), right falling -0.7e-6 (86), right rising -1.4e-5 samples (40). As a law: mid
-5e-6, swing +7e-6 samples, origin +0.10 samples (85.8), residual 2.7e-5 samples rms. The tap-change
count below peaks at 85.77 to 85.78 on my 53 crossings (52 of 53) and at 85.69 to 85.72 on the author's
and mine together (91 of 94; his 41 known ones agree with 85.7 by construction). FITTED: mid 431.2075,
swing 236.0360, origin 85.7 to 85.8 samples; the period was not refitted. CONFIRMED. I found no round
form either. The origin is not the reported latency of 44 samples, which is what wave 1 inferred at
48 kHz.

**Tap changes** (`v12`; warm-up 60 s, plug-in time 61 to 109 s, 53 impulses on both inputs, each
leaving the comb 100 samples before a downward crossing; the crossings are the ones nearest to a 0.9 s
grid, not selected). Three Macro 0 partners were rendered: the published single-precision law, the
tap change one sample later, and one sample earlier. MEASURED:

- the reference agrees with the published law in **50 of 53** (left input 30 of 32, right 20 of 21),
  with "one sample later" in 2 (left, whole numbers 522 and 479) and "one sample earlier" in 1 (right,
  299). The misses go both ways, so the reference's value is not a fixed rounding of the published
  expression;
- the matching partner nulls at **-69.9 dB** (quartiles -71.4 / -68.9), not at the -96 dB of the
  stage; a partner one sample off nulls at -36.6 dB;
- candidates in pure arithmetic against these 53 tap-change samples (`v23`, origin 85.7): published
  50, exact law rounded once to single precision 51, double precision 45, single-precision phase 47,
  milliseconds 38, seconds 41. None reaches 53 at any origin between 85.6 and 85.9 (best 52). In the
  5 crossings where single and double precision put the change on different samples the reference
  follows single precision. The author's "single precision, arithmetic OPEN" stands.

The -70 dB follows from the read itself: just above a whole number the all-pass coefficient is near 1,
the read rings at the Nyquist frequency with a memory of 1/(2d) samples, and one single-precision step
of the delay (3e-5 to 6e-5 samples) changes that ringing by about 1 %. In design P the same shows by
distance: exits 82 and 134 samples before a crossing -70 and -75 dB; one exit 190 samples before a
crossing -55 dB in both instances and both outputs, which is what one sample of the remaining ringing
amounts to at that distance, so the model has that tap change on the wrong sample; second and third
exits 210 to 260 samples before a crossing -81 to -84 dB. INFERRED from the recursion, consistent with
every open case above -84 dB in design P.

**Other forms**, my comb inside the delivered first-pass model on a fresh capture (`v25`, 44.1 kHz,
Macro 85 %, 44 cases; as published -78.7 dB): linear read -31.9 dB, no low-pass -31.2 dB, low-pass
19.8 kHz -51.4 dB, high-pass 31 Hz -55.8 dB, filters behind the loop -46.5 dB on the worst tenth (the
median does not see the second exit in this train). CONFIRMED.

## 6. The voice

**One element changed at a time** (`v06`, Macro 100 %, 129 cases of two new instances; as published
-96.83 dB, quartiles -100.1 / -90.9). Every detuning is symmetric about the published value:

| change | null, dB | change | null, dB |
|---|---|---|---|
| no hold (per sample) | -76.4 | cut-off end 9 / 11 Hz | -80.5 / -80.4 |
| blocks +1 / -1 sample | -85.1 / -85.2 | cut-off end 0 / 20 Hz | -60.8 / -60.8 |
| blocks +2 / -2 | -82.1 / -81.8 | cut-off top 19990 / 20010 Hz | -62.7 / -62.9 |
| blocks 22 later | -70.9 | bend 2.999 / 3.001 | -69.6 / -69.8 |
| direct form I | -86.0 | amount 0.879 / 0.881 | -67.5 / -67.5 |
| transposed direct form II | -86.5 | gain top 1.4140 / 1.4144 | -77.4 / -78.2 |
| gain in front | -84.7 | gain top 1.40 | -40.6 |
| Q smoothing 9 / 11 blocks | -82.8 / -84.4 | Q plateau 1.35 / 1.37 | -49.4 / -49.4 |
| Q smoothing 9.5 / 10.5 | -87.6 / -88.4 | Q at 3 kHz 2.08 / 2.10 | -88.4 / -88.7 (upper quartile -63.2) |
| no Q smoothing | -70.4 | Q at 1 kHz 7.00 / 7.02 | -94.3 (upper quartile -64.5 / -64.3) |

**Laws at Macro values the author did not measure** (`v07`, one new instance each, 63 to 72 open
cases; one further number fitted next to phase and rate, median over the cases):

| Macro | null, phase and rate | phase only | gain offset | cut-off scale | Q scale | delayed / undelayed path |
|---|---|---|---|---|---|---|
| 85 % | -96.0 dB | -59.3 | -1.4e-6 | -1.2e-6 | -4.2e-6 | -1.6e-6 |
| 40 % | -100.8 | -62.9 | -2.5e-7 | -2e-9 | -2.6e-7 | -1.1e-6 |
| 20 % | -100.7 | -66.0 | -1.4e-7 | +1.3e-7 | -1.3e-7 | -5.7e-7 |
| 7 % | -108.0 | -69.4 | +7.7e-7 | -1.1e-7 | -3.7e-7 | -7.6e-7 |
| 5.5 % (d = 0.968) | -109.1 | -65.6 | +6.6e-7 | +8e-8 | -4.9e-7 | -2.6e-7 |
| 4 % (d = 0.704) | -111.0 | -70.9 | -3.1e-6 | +1.6e-7 | -6e-8 | -6.8e-7 |
| 2 % (d = 0.352) | -117.0 | -82.9 | -4.1e-7 | +7e-8 | +4.2e-7 | -8.6e-7 |
| 0.5 % (d = 0.088) | -120.3 | -90.6 | +3.8e-7 | -3e-8 | +7e-8 | -3.1e-7 |

Quartiles of the offsets are within 5e-6 (path ratio; gain, except -2.5e-5 at Macro 4 %), 4e-6
(cut-off) and 1e-5 (Q); by cut-off band the Q scale stays within 1e-5 from 1 kHz to 20 kHz. MEASURED. So `E(0.88 m)`, the plateau and
3 kHz laws against Macro, the straight line to Q = 1 between 15 and 20 kHz, the blend by `d` below
Macro 5.68 % and the mix law all hold between the author's points, and at 0.5 %, below his range.

**Sharpness below full depth** (`v21`, Macro 4, 2 and 20 %): depth slope 17.5998 / 17.6002 gives
-107.6 / -108.0 dB against -111.0 dB (17.599 / 17.601: -96.9 / -97.2); amount 0.87998 / 0.88002 gives
-95.8 / -94.2 dB; cut-off top 19999.9 / 20000.1 Hz -88.0 / -88.6 dB; bend 2.9998 / 3.0002 -86.0 /
-85.5 dB; plateau at Macro 0 6.999 / 7.001 -78.7 / -78.6 dB and its fall 5.639 / 5.641 -84.6 / -84.6 dB
against -100.7 dB at Macro 20 %. FITTED: 17.6000 +- 0.0002, 0.88000 +- 0.00001, 20000.00 +- 0.02 Hz,
3.0000 +- 0.0001, 7.0000 +- 0.0002, 5.6400 +- 0.0003. The round values are exact at this precision.

**Low end of the Q law** (`v16`, four instances at Macro 100 %): 34 cases with the cut-off between 663
and 989 Hz: -93.7 dB as published, -83.2 / -80.7 dB with the slope at 3.640e-3 / 3.650e-3 per Hz;
1 kHz knot at 7.005 / 7.015: -65.8 / -66.5 dB there and -68.2 / -67.8 dB on the 149 cases between 1 and
3 kHz. At Macro 85 % the cases on the 1 to 3 kHz slope show a Q scale of -1.0e-5, so the knot value
holds at a Macro value where the author had not read it. CONFIRMED down to 663 Hz; no open case of
mine went lower.

**Form of the Q smoothing** (`v20`, 127 cases): one-pole on Q with 1 - e^(-1/10) -96.5 dB, with 0.095
-96.7, with 2/21 -96.3, with 0.1 -86.6; one-pole on 1/Q -96.3; one-pole on the cut-off -96.4; pure
delay of 9.5 blocks -96.1, of 9 or 10 blocks -86.4 / -88.2 dB. The lag of 9.5 blocks is established,
the form is not, as the author says. A product may take any of them.

**Blocks** (`v19`): host block of 100 samples at 44.1 kHz -96.2 dB, blocks +-1 sample -85.6 / -85.5 dB.
At a 48 kHz host, delivered model with its voice blocks moved against the internal count: 0 -77.9 dB,
+-1 -76.9 / -77.2, +-4 -74.6 / -74.7, 11 -72.2, 22 -71.1, 33 -72.9 dB. CONFIRMED.

**Tone lines** (`v10`, 1500 Hz at 48 and 96 kHz hosts, 2500 Hz at 44.1 kHz, Macro 100 %): spacing
1002.14 to 1002.27 Hz at all three host rates (44.000 to 44.006 samples at 44.1 kHz; 44100/44 =
1002.27 Hz), first line -90.5, -80.9 and -77.4 dB re the tone; nothing at host rate / 44 at 48 kHz
(-100.6 dB, the floor) and no family at Macro 0. MEASURED: the 1 ms update is internal at a 96 kHz
host as well.

**The fitted rate is physical** (`v17`): the phase step between consecutive impulses (1.25 to 1.5 s)
agrees with the trapezoid of the two fitted rates to 0.002 to 0.006 cycle rms (four curves, 45 to 54
pairs each); against the nominal rate the same steps scatter by 0.036 to 0.047 cycle. So the second
number per case is the derivative of the phase and does not absorb model error. MEASURED.

## 7. Voice B and the late field (new)

The author could not isolate voice B. A relation between three captures does it (`v15`):

```
s      = unrest(C0)                    input of voice A plus input of voice B (Macro 0, comb-driven)
y1, y2 = two Macro 100 % instances
model    y_i = A_i[a] + B_i[s - a]     A_i: the voice at phase phi_i(t); B_i: the same voice at phi_i(t) + 1/2
```

Instance 1 is one whose voice A is open and voice B nearly closed (phase 0.38 to 0.474); `a` follows
from `y1` by a fixed-point iteration on the exact inverse of the state-variable recursion; the
prediction for instance 2 is compared with `y2` on the voice A window (1302 samples) and on the 6000
samples (136 ms) after it, which hold the first passes of lines 9 to 16 and later passes. On synthetic
signals rounded to single precision the method's own floor is -109 to -116 dB. 148 pairs from four
instances (28 impulses, both inputs and outputs); phase per instance `phi + rate t + curve t^2`.

| | voice A window | late 6000 samples | first 2000 | next 4000 |
|---|---|---|---|---|
| two voices, 6 numbers (with curvature) | -110.4 dB | **-103.6 dB** (quartiles -108.1 / -96.9, worst -87.1) | -106.6 | -100.5 |
| two voices, 4 numbers (straight phases) | -91.0 | -82.1 | -85.4 | -76.8 |
| two voices, nothing fitted on the late part | -102.0 | -74.2 | -82.2 | -67.4 |
| one voice for everything, 6 numbers | -68.9 | -20.6 | -21.7 | -20.1 |

- A free shift of voice B from half a cycle fits to 0.000000 cycle (quartiles -1e-6 / 0, extremes
  +-1.4e-5). MEASURED.
- Where voice B dominates instance 2 (its gain 0.4 to 1.4, 35 pairs) the late null is -93.9 dB
  (quartiles -96.7 / -91.0) and a single voice gives -5.3 dB.
- Fitted curvatures: 0.010 cycles/s^2 rms, largest 0.057; the straight-phase rows are limited by the
  phase generator, not by the stage.

So the whole Tide stage at Macro 100 % (comb, voice A, voice B with the same cut-off, Q, gain, block
and smoothing laws at phi + 1/2) relates a Macro 0 capture to a Macro 100 % capture at about -100 dB
over 166 ms, given the phase, and the split of the network output between the two voices is the same
in every instance. MEASURED. Not shown: which lines feed which voice (the test only needs a fixed
split), and anything beyond 166 ms.

## 8. Other Size, Decay, warm-up (`v09`), fresh model scores (`v11`)

Relation with the constants of Size 100 %, Decay 0.5 s:

| setting | Macro | warm-up | open cases | median | quartiles | worst |
|---|---|---|---|---|---|---|
| Size 80 % | 100 % | 17.3 s | 36 | -98.6 dB | -103.9 / -91.8 | -78.2 |
| Size 130 %, Decay 1.3 s | 65 % | 21.9 s | 30 | -99.2 dB | -104.6 / -93.1 | -84.4 |
| Decay 4 s (impulses 7.6 s apart) | 100 % | 10 s | 22 | -97.9 dB | -100.4 / -90.3 | -81.6 |

Delivered `tide_stage.FirstPass`, my train, warm-up 25 s, my fit loop, new instances:

| host | Macro | open cases | phase and rate | worst tenth | phase only |
|---|---|---|---|---|---|
| 48 kHz | 100 % | 89 | -78.0 dB | -75.3 | -54.9 |
| 48 kHz | 40 % | 92 | -80.4 | -79.2 | -61.6 |
| 48 kHz | 7 % | 95 | -81.1 | -79.3 | -61.7 |
| 44.1 kHz | 85 % | 88 | -78.8 | -75.9 | -63.1 |
| 44.1 kHz | 20 % | 91 | -81.8 | -79.4 | -63.0 |

CONFIRMED.

## 9. Sharper or corrected values

| item | packet | here |
|---|---|---|
| stage alone at Macro 100 %, median | -93.5 dB | -96.3 dB with impulses 1.25 s apart; voice alone -110 dB |
| what limits the relation | unknown | the comb emulation near and between delay crossings (single-precision delay) |
| energy-weighted null of the stage at Macro 100 % | -83.4 dB | -75 dB (impulses, one wrong tap change in 71), -85.6 dB (noise bursts, none) |
| tap change on the right sample | 41 of 44 selected crossings | 50 of 53 unselected crossings; a right one still leaves -70 dB at 100 samples |
| mix law | exact to 2e-5, Macro 1 to 90 % | exact to 5e-6, Macro 0.5 to 85 % |
| depth slope | 17.600 +- 0.003 | 17.6000 +- 0.0002 |
| feedback | -0.4 (scan +- 0.0005 loses 0.5 dB) | -0.400000 +- 0.000003 |
| comb filters | 30 Hz, 20 kHz | 30.000 +- 0.001 Hz, 20000.0 +- 0.3 Hz |
| comb origin | 85.7 samples | 85.7 to 85.8 samples |
| host rates | "every host rate" | 44.1 and 48 kHz; at 96 kHz only the voice's update interval |
| voice B | INFERRED | MEASURED, -104 dB, half a cycle to 1e-5 |
| Q smoothing | time constant 10 blocks | 10.0 +- 0.2 blocks; coefficient 0.0950 to 0.0953 per block; 0.1 is 10 dB worse |

## 10. Weak spots a product should know

1. **Comb read near a downward crossing of the delay.** On the falling flank of a channel (100 of every
   200 s) the delay passes a whole number every 9340 samples (0.21 s). Content that left the comb up
   to about 150 samples earlier is reproduced at about -70 dB, and in about 6 % of crossings (3 of 53;
   the author 3 of 44 on a selected set) the model changes taps one sample off and the same content is
   at -37 dB. A null test on programme material will therefore sit near -75 to -85 dB at Macro 100 %
   with an occasional error 37 dB below a transient that left the comb 100 samples before such a
   crossing, until the reference's single-precision arithmetic is found. This is far below audibility,
   but it is what an energy-weighted null will show. The 53 observed tap-change samples are in
   `v12_crossings.npy` in my scratch folder.
2. **The medians hide this.** The packet's headline -93.5 dB and the model's -78 to -81 dB are medians
   over impulses; its energy-weighted figures (-83.4 dB for the relation, -68 to -73 dB for the 44.1 kHz
   model captures) are in the data file and in section 5 of its findings, not in its summary.
3. **Host rates.** Nothing in either packet shows the comb at 88.2 or 96 kHz.
4. **Phase alone does not null.** -54 to -66 dB from Macro 100 to 20 % with the rate held at its
   nominal value. The stage is exact, the phase generator decides what a product reaches.
5. **Only voice A's first passes are modelled by the delivered module.** `FirstPass` covers 27 ms. The
   late field needs the network of the other packets; section 7 shows that the stage itself is not the
   obstacle.
6. `tide_stage.py` compiles its two recursions into the author's scratch folder on first use.

## 11. Not established

- The wrap of the phase below Macro 5.68 %: none of my 288 windows at Macro 0.5 to 5.5 % holds one (the
  nearest starts 48 ms after a wrap). UNVERIFIED beyond the author's handful.
- The Q law below 663 Hz, and the 1 kHz knot below Macro 85 %.
- That the state-variable recursion is the only one that fits; three others are 10 to 12 dB worse.
- The phase axis itself. The relation tests gain against cut-off against Q; that the gain window is
  sin^2 of a uniformly running phase rests on wave 1 (straight line during the first 6.5 s).
- Comb at 88.2 and 96 kHz hosts; a Macro change while playing; levels above 0.5; the first second after
  the start. Not tested, as in the packet.
- The exact arithmetic of the comb delay, and a round form for its mid, swing and origin. One idea is
  ruled out (INFERRED, arithmetic only): a single-precision one-pole smoother of the delay would explain
  a lag of 85.7 samples on both flanks, but its steady lag is set by where its increment rounds, which
  makes it 14 % shorter for delays above 512 samples (where the single-precision step doubles) than
  below; that is 1.3e-3 samples of delay, and the law holds to 2.7e-5 samples across both ranges.
- Which lines feed which voice in the late field.

## 12. Reproduce

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
W=../../Results/RevOceanCharacterization/work/tide_stage_verification      # result arrays are written there
PYTHONPATH=$W $PY $W/designs.py            # all captures of designs P, Q and the tones (cached)
PYTHONPATH=$W $PY $W/v03_relation100.py    # section 4;  v05 comb refit, v06 voice variants, v07 other Macro values,
                                           # v09 Size / Decay, v10 tones, v11 model scores, v12 tap changes,
                                           # v15_voice_b_full.py 6000 3 4 5 6 (section 7, 25 minutes), v16 to v25
PYTHONPATH=$W $PY $W/v00_regenerate.py     # the author's script, output compared byte for byte
```

Captures (44.1 kHz unless stated, Decay 0.5 s, Size 100 %, baseline otherwise): design P, warm-up 30 s:
six Macro 0 captures of 100 s (plain, comb, four derivatives), Macro 100 % x 4 instances of 100 s, Macro
85, 40, 20, 7, 5.5, 4, 2, 0.5 % and Macro 100 % with a host block of 100 samples, 50 s each; three
settings of section 8 (7 captures); tones at 48 kHz (Macro 100 and 0 %), 96 kHz and 44.1 kHz, 30 s; five
trains of 50 s for the delivered model (48 kHz x 3, 44.1 kHz x 2); the tap-change set (warm-up 60 s,
three Macro 0 partners and one Macro 100 % capture of 50 s); noise bursts (one Macro 0, two Macro 100 %
captures of 100 s). No holdout loader is called anywhere.
