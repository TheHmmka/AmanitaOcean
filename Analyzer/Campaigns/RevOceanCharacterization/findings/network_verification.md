# network_verification: independent check of the campaign's network model

Packet `network_verification`, wave 2, 7 October 2026. Checked: `network_model.py`
with `network_model.c`, `fit_network.py`, `tide_structural_data/network.json`,
`findings/network.md`. Reference: Arturia Rev OCEAN 1.0.0.5848, Tide mode,
neutral baseline, **Macro 0 %, Mix 100 % in every capture**; nothing here is a
measurement at Tide 100 %.

Tags: MEASURED, FITTED (residual given), INFERRED, OPEN. Verdicts: CONFIRMED,
REFUTED, PARTLY, UNVERIFIED. A null is
`20 log10(rms(model - reference) / rms(reference))`, nothing fitted.

## 0. Verdict

- The model holds. On 38 fresh captures of this packet (own seed, own
  stimuli, Decay 0.5 to 59 s, Size 30 to 200 %, warm-ups 3.3 to 59 s, host
  rates 44.1, 48, 88.2, 96 kHz) the delivered `render` nulls at **-108.1 to
  -121.3 dB** on 14 s material and at **-100.0 to -102.9 dB** on 60 to 100 s
  material. A second implementation, written from section 4 of
  `findings/network.md` alone, agrees with the delivered one at -270 dB, so the
  document is a complete description. Holdout score and data file reproduce
  exactly; no holdout leak was found.
- **One formula is wrong: how Size becomes whole line lengths** (section 3).
  The document's rule gives a wrong length for one or two lines at 0.49 % of
  all normalised Size values and at the display values 80.1, 162.1 and
  180.1 %; there the delivered model nulls at -6 to -24 dB instead of
  -100 dB. The reference's arithmetic was identified and holds on 69 of 69
  captures (28 of them held out): Size is a factor computed with a
  reciprocal and one fused multiply-add, and the length is rounded in single
  precision. Whole-percent Sizes are not affected.
- Three statements are narrower or more optimistic than the evidence:
  steady broadband input nulls at -100 dB, not at -112 to -119 dB; the
  per-pass lag is the same at Size 60, 100 and 200 % (the "1.3e-7 at Size
  140 %" is not a Size effect); the free-fit kernel values absorb the lag
  only as a set of six (the low shelf at 1220.9958 Hz alone makes it worse).
- One open alternative is closed: the lag is **not** a longer line length.
  First-pass reads of the reference sit -2.8e-8 +- 0.2e-8 samples from the
  model's, and reads that are one single-precision step off go both ways
  equally (12 longer, 11 shorter in 32 991 reads).

## 1. What was done

Own code for every check (scratch folder, section 11): stimuli and nulls
(`nv_common.py`), an independent network core (`nv_core.c`, `nv_net.py`:
direct-form-I sections, explicit 44-sample delays, a matrix, gains and taps
per group), least-squares re-derivations, own delay estimator. The delivered
model and `converters.py` were called only to test predictions; of the
author's script only the loaders of its cached captures and the list of its
free-fit constants were used.

Captures of this packet (0.6 GB of cache, all Macro 0, every input sample at or
below 0.5):

| set | captures | what |
| --- | --- | --- |
| programme, seed 271828 | 28 | `datasets.network_programme` at Decay 0.5, 0.57, 1.23, 2.0, 2.6, 2.9, 3.4, 4.0, 4.5, 5.9, 6.0, 6.1, 9, 30, 59 s; Size 30, 46, 77, 83, 91, 95, 100, 109, 117, 129, 171, 200 %; warm-ups 3.3, 7.3, 10, 12.9, 23.3, 31.7, 59 s; 44.1, 48, 88.2, 96 kHz; once at 1/100 of the level |
| own stimulus | 3 | impulses at host samples 0 and 1, 80 random impulses, two sine sweeps 20 Hz to 20 kHz, a DC step, one-sided noise, a side-only impulse |
| steady noise | 3 | independent white noise on both inputs for 60 or 90 s, then 10 s of silence |
| 100 s tails | 4 | three impulses and a noise burst; Decay 12, 20, 30 s; Size 60, 100, 200 %; one with a 59 s warm-up; one at 48 kHz |
| impulse trains | 3 | 189 single impulses 0.5 s apart, Decay 0.5 s, Size 60, 100, 200 % |
| Size points | 70 | a 3 s stimulus at chosen normalised Size values (section 3) |

Reused from the cache, for cross-checks only: the author's five captures at
Size 50 % and the two 44.1 kHz tails.

## 2. Verdict per claim of the author's report

| # | claim | verdict | this packet's numbers |
| --- | --- | --- | --- |
| 1 | holdout: worst -114.07, mean -116.79 dB, worst stretch -92.62 dB | CONFIRMED | scorer re-run: identical in every case and stretch |
| 2 | 16 fresh captures at -111.81 to -118.47 dB | CONFIRMED, range wider | 31 fresh 14 s captures: -108.11 to -121.25 dB, mean -115.30; steady noise -100.1 to -101.9 dB (section 4) |
| 3 | audit: both authors' scores reproduce; no holdout read; structural models | CONFIRMED | `network_a` -93.62 / -95.25, `network_b` -114.07 / -116.74 re-run; search of the three fit scripts and three scratch folders finds no holdout read (section 8) |
| 4 | `network_a` and `network_b` are one network; shelves factor exactly | CONFIRMED (the factorisation), rest UNVERIFIED | own calculation: poles 953.410, 1587.887, 4888.248, 19054.954 Hz with steps -0.14000, -0.14000, +0.06000, +0.06000 dB; 105.058, 380.694, 838.329, 3605.465 Hz with 0.83484 dB |
| 5 | each element of `network_a` is worse | UNVERIFIED | regenerated byte-identically by the author's script; not re-derived (nothing a product depends on) |
| 6 | `network_a`'s tail drift is its fitted poles | UNVERIFIED | as 5; the round kernel's own tail figure is confirmed (-83.0 dB at 90 to 100 s on an independent capture) |
| 7 | null falls to -83 dB after 100 s through a linear lag of 1.3e-7 to 1.8e-7 samples per pass; independent of level | PARTLY | lag reproduced on four independent tails; per pass 1.9e-7 to 2.3e-7 at Size 60, 100, 200 % (slope of the last 20 s), so not a function of Size; not linear before 30 s; level independence confirmed (section 5) |
| 8 | lag absorbed by kernel values 1.5e-8 to 1.5e-5 off round, or by 1.5e-7 to 2.0e-7 samples on every length; OPEN | PARTLY | kernel free fit confirmed on tails it never saw; the length alternative is REFUTED; "1220.996 Hz" is not a better constant on its own (section 5) |
| 9 | all constants are round values, free fit within 2.3e-5 | CONFIRMED | own fits: 14 filter constants within 1.2e-5, 2 x 32 input gains within 6.4e-6, 2 x 16 taps within 2.7e-6 (typically 2e-7), 2 x 256 matrix entries within 2.4e-6 (section 6) |
| 10 | `B_1` = 0.24397766; closed form 0.24397783 not supported | CONFIRMED, precision overstated | 38 captures: 0.24397756 to 0.24397771, mean 0.24397764; the closed form is worse on all 38 (by 0.02 to 2.04 dB) |
| 11 | Size display law evaluated in single precision; lengths `floor(P s + 0.5)` | **REFUTED as stated**, corrected | single precision yes; the form is different (section 3) |
| 12 | single-precision line length with the reduced sine | CONFIRMED | correctly rounded sine or `sinf`: 14 to 15 dB worse; depth one step lower or higher: 14 to 15 dB worse (section 7) |
| 13 | agreed elements are each necessary | CONFIRMED | own core, four fresh captures (section 7) |
| 14 | `line_outputs` / `network_output` reproduce `render` | CONFIRMED, and per line | each of the 2 x 16 unweighted line outputs agrees with the own core at -260.6 to -268.1 dB |
| 15 | nothing measured above Macro 0 | stands | not testable here either |

## 3. Size to line length: the document's rule is wrong off whole percents (MEASURED)

`findings/network.md` 3.3 and `network_model.plugin_value` / `parameters`:
Size (percent) is `30 + 170 (e^(shape x) - 1)/(e^shape - 1)` in single
precision, and `N = floor(P * Size / 100 + 0.5)` in double precision. The
author tested this at one tie (Size 50 %, five captures) and concluded that
away from a tie the choice moves nulls by 0.05 dB at most. That overlooks the
ties of single lines: `P s` of some line is within a rounding error of a half
at many Size values.

**Test.** A 3 s stimulus, Decay 2 s, 44.1 kHz, at normalised Size values chosen
so that candidate arithmetics give different whole lengths for at least one
line. A capture nulls at about -100 dB under the arithmetic that gives the
reference's lengths and between -24 and +2 dB under any other.

1. Size 150 %, the tie the author did not measure, and steps around it:
   the double-precision law fails one step below (+2.3 dB), the single-
   precision law holds (-101.6 dB). Six more points between 37 and 195 %
   where the two laws differ: single -100.4 to -106.3 dB, double -8.9 to
   -19.1 dB. **The law is evaluated in single precision: CONFIRMED, 7 of 7.**
2. Six points where the document's double-precision rounding and a single-
   precision rounding differ: the document's rule fails five (-8.4 to
   -11.2 dB).
3. The whole lengths of all 41 captures made up to then were read off (the
   lengths of whichever candidate nulled) and compared offline with 376
   combinations of a law form and a rounding form (percent or factor, single
   or double precision, lengths from samples, seconds or milliseconds): none
   matches all 41, the best 39. Of 11 further law forms with a reciprocal
   and/or a fused multiply-add, exactly one matches all 41:

```
r = fl( fl(expf(fl(shape * x)) - 1) * fl(1 / fl(expf(shape) - 1)) )      reciprocal, not a division
s = fma(1.7f, r, 0.3f)                                                    Size as a factor 0.3 .. 2; ONE rounding of 0.3 + 1.7 r
N = floorf( fl(P * s) + 0.5f )                                            single precision
x: the normalised host value (single precision); shape = 0.71337f
```

4. **Held-out test of that rule (W)**: 28 new normalised values, none used to
   select it: 10 random values where W differs from the document's rule, 18
   where it differs from its nearest rivals (no fused operation; division in
   place of the reciprocal; neither).

| rule | captures where its lengths differ from W's | of those nulled | captures nulled, of 31 |
| --- | --- | --- | --- |
| **W** | | | **31** (-99.7 to -106.6 dB) |
| W without the fused multiply-add | 11 | 0 | 20 |
| W with a division | 7 | 0 | 24 |
| neither | 16 | 0 | 15 |
| the document's rule | 16 (13 of them new) | 0 (-8.6 to -23.5 dB) | 15 |

   With the 41 captures of the selection set W gives the reference's lengths
   on 69 of 69 captures, the author's five at Size 50 % included.

**How often the delivered model is wrong** (computed from the two rules):

| Size values | document's rule gives a wrong length |
| --- | --- |
| 171 whole-percent display values | 0 |
| 1701 tenth-percent display values | 3: 80.1, 162.1, 180.1 % |
| 17 001 hundredth-percent display values | 67 (0.39 %) |
| 20 000 random normalised values | 98 (0.49 %) |

MEASURED with the delivered `render` itself: Size 80.0 / **80.1** / 80.2 %:
-102.9 / **-13.95** / -103.3 dB; 162.0 / **162.1** / 162.2 %: -104.1 /
**-11.60** / -103.4 dB; 180.0 / **180.1** / 180.2 %: -101.0 / **-18.54** /
-101.9 dB.

- What it means: when the model's length is wrong, one line (or the two lines
  of a shared prime) is one sample longer or shorter. That is inaudible, and a
  product has its own Size parameter. It matters for validation: a null test
  of a product against the reference at an arbitrary knob position fails
  completely at one position in 200 unless rule W is used.
- `network_model.host_value` should return the factor `s` of rule W and
  `parameters` should round in single precision. INFERRED, not testable at
  this depth: Decay is computed the same way (`fma(59.5f, r, 0.5f)`); the
  difference from the document's value is at most one single-precision step.
- Limits: the reciprocal is decided by 7 held-out captures, the fused
  operation by 11. `floorf(fl(P s) + 0.5f)` and `floorf(fma(P, s, 0.5f))`
  give the same lengths on every capture and could not be told apart. W was
  found by a search, so its form rests on the held-out test, not on the 41.
  NumPy's single-precision `exp` and the C library's `expf` agree on 20 000
  of 20 000 arguments here; which the plug-in uses is not known.

## 4. Fresh nulls, and where the null is lost (MEASURED)

Delivered `render`, nothing fitted (`nv02_fresh.py`).

| captures | overall, dB |
| --- | --- |
| Decay 0.5 s, Size 30 % (44.1 / 48 kHz) | -120.64 / -121.25 |
| Decay 0.5 s, Size 200 % (44.1 / 48 / 96 kHz) | -114.83 / -115.94 / -118.48 |
| Decay 30 s, Size 30 % (44.1 / 48 / 88.2 kHz) | -113.82 / -113.88 / -113.82 |
| Decay 30 s, Size 200 % (44.1 / 48 kHz, warm-up 7.3 s) | **-108.11** / -109.28 |
| Decay 59 s, Size 100 % | -110.47 |
| Decay 4 s, Size 100 %, the campaign's baseline (44.1 / 48 kHz) | -113.94 / -114.23 |
| Decay 0.57 to 9 s, Size 46 to 171 % (8 captures) | -113.32 to -120.97 |
| warm-up 3.3 / 23.3 / 59 s (44.1 kHz), 31.7 / 59 s (48 kHz) | -115.71 / -114.04 / -115.31, -115.32 / -115.75 |
| own stimulus (44.1 kHz; 48 kHz, Size 141 %; Decay 12 s, Size 67 %, warm-up 17.9 s) | -118.32 / -118.53 / -119.58 |
| programme at 1/100 of the level against full level | -115.01 / -115.01 |
| steady noise: 60 s at Decay 4 s; 90 s at Decay 20 s, 48 kHz; 90 s at Decay 30 s, Size 52 % | **-100.58 / -100.09 / -101.94** |
| 100 s tails (section 5) | -100.00 to -102.91 |

All 31 captures of 14 s: -108.11 to -121.25 dB, mean -115.30 dB.

**By time.** Stretches of the 14 s captures, best to worst: 0.25 to 1.4 s
-123.5 to -99.0 dB; 1.4 to 4 s -121.3 to -110.3; 4 to 6 s -128.5 to -101.1;
6 to 10 s -109.6 to -98.1; 10 to 14 s -102.5 to -96.0. Quarter-second blocks
above -300 dBFS (1466 blocks): median -102.1 dB, 90 % below -97.8 dB, worst
-94.3 dB. The overall figure of a programme capture is set by its loudest
second, which holds first and second passes; any stretch that holds only
recirculated sound reads -96 to -105 dB.

**Steady input.** While white noise is running the quarter-second null is flat:
median -101.3 / -100.8 / -102.7 dB, worst block -93.0 / -93.8 / -95.8 dB. This
is the figure for sustained broadband material. The author's "sustained
material averages them (-112 to -119 dB)" holds for the programme, whose
energy is tones and bursts in their first passes, not for a steady input. The
worst stretch falls at 20 to 30 s in all three noise captures (two Sizes, two
host rates). INFERRED: the one-step reads are tied to the oscillators' time,
which fits the author's account of the floor.

**By frequency.** Residual over reference per octave under steady noise:
-100.5 to -103.9 dB from 31.5 Hz to 8 kHz, -97.6 to -99.5 dB in the 16 kHz
octave. With the own stimulus (sweeps) the residual rises steadily from
-123 dB at 31.5 Hz to -99 dB at 16 kHz. No band stands out.

**By level.** The reference is linear between the two programme levels at
-131.0 dB and the model's null is the same to 0.01 dB; the tails are followed
to -600 dBFS (the reference's output stops falling near -720 dBFS, as the
author reports).

## 5. The late response (MEASURED)

Own estimator: per stretch, least-squares gain and delay of the reference
against the model with an eighth-order differentiator; phase per band from the
cross-spectrum (`nv07_tails.py`).

| stretch, s | 5-10 | 10-20 | 20-30 | 40-50 | 60-70 | 80-90 | 90-100 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Decay 20 s, Size 60 %**: null, dB | -100.5 | -97.1 | -91.7 | -85.9 | -82.5 | **-79.8** | (below -600 dBFS) |
| delay, samples | 1.1e-5 | 3.4e-5 | 8.4e-5 | 1.81e-4 | 2.73e-4 | 3.77e-4 | |
| gain and delay removed / author's free fit, dB | -101.2 / -101.4 | -100.1 / -100.6 | -98.4 / -99.0 | -97.0 / -97.6 | -95.8 / -96.2 | -94.9 / -95.2 | |
| **Decay 20 s, Size 100 %, warm-up 59 s**: null | -99.4 | -97.9 | -94.3 | -89.4 | -86.3 | -84.0 | **-83.0** |
| delay | 6.1e-6 | 2.0e-5 | 5.0e-5 | 1.11e-4 | 1.70e-4 | 2.28e-4 | 2.56e-4 |
| removed / author's free fit | -99.5 / -99.6 | -98.9 / -99.2 | -97.4 / -97.9 | -96.2 / -97.0 | -95.4 / -96.4 | -94.7 / -95.9 | -94.4 / -95.6 |
| **Decay 30 s, Size 200 %, 48 kHz**: null | -97.6 | -96.8 | -95.0 | -92.7 | -90.8 | -88.8 | **-87.9** |
| delay | 3.3e-6 | 1.1e-5 | 2.7e-5 | 5.8e-5 | 8.8e-5 | 1.21e-4 | 1.39e-4 |
| removed / author's free fit | -97.7 / -97.7 | -97.1 / -97.2 | -95.8 / -95.9 | -95.2 / -95.3 | -94.7 / -94.8 | -94.1 / -94.0 | -94.0 / -93.9 |

(Decay 12 s, Size 100 %: -85.4 dB at 70 to 80 s, free fit -96.2 dB.)

1. **The lag is reproduced** on captures with other Sizes, another Decay,
   another warm-up and another stimulus. The warm-up of 59 s (oscillators
   followed to 159 s of processing) gives the author's profile to 0.1 dB
   (-83.0 against -82.9 dB).
2. **Per pass it does not depend on Size.** Slope of the delay over the last
   20 s, divided by passes per second (25.1, 15.0, 7.5): 2.08e-7, 1.91e-7
   (1.93e-7 at Decay 12 s) and 2.25e-7 samples per pass at Size 60, 100 and
   200 %. The same estimator on the author's two captures gives 2.00e-7
   (Size 100 %) and 1.58e-7 (Size 140 %, Decay 40 s, programme). So the
   author's "1.3e-7 at Size 140 %" is the spread of the delay figure with
   what survives in the tail, not a law in Size. Phase lag per pass, late
   stretches, all six tails: 3.0e-8 to 4.9e-8 rad (1 to 1.5 kHz), 4.4e-8 to
   5.3e-8 (1.5 to 2 kHz), 5.7e-8 to 6.6e-8 (2 to 3 kHz); the author's 2.9e-8,
   4.7e-8, 5.8e-8 at 1.25, 1.75, 2.5 kHz agree.
3. **"Linearly" holds only late.** Delay divided by elapsed passes rises from
   0.7e-7 at 5 to 10 s over 1.4e-7 at 20 to 30 s to 1.7e-7 to 2.0e-7 after
   60 s, at every Size.
4. **Gain.** Drift of the least-squares gain at the last stretch: +7e-8
   (Size 100 %), -9.0e-6 (Size 60 %, 2090 passes), -7.2e-6 (Size 200 %,
   700 passes): 4e-9 to 1.0e-8 per pass, inside the author's 1.2e-8, with the
   opposite sign of the author's +1.2e-5.
5. **The author's free-fit constants generalise** (their evidence was weak:
   the "48 kHz tail not fitted on" is the fitted capture at another host
   rate). On these four tails, none of which any fit of the author saw, they
   take the last stretch from -79.8 / -83.0 / -85.4 / -87.9 dB to -95.2 /
   -95.6 / -96.2 / -93.9 dB.
6. **Their values are not corrected constants.** Applied in parts to a tail no
   fit used (Decay 20 s, Size 100 %, warm-up 59 s; `nv19_kernel_parts.py`;
   last ten seconds, round values -83.01 dB):

| free values used (everything else round) | own free fit | author's free fit |
| --- | --- | --- |
| low shelf frequency alone (1220.9958 Hz) | -82.32 | -82.35 |
| low shelf, three values | -81.73 | -81.51 |
| high shelf, three values | -87.55 | -87.46 |
| all six kernel values | -95.15 | -95.59 |
| equaliser, or low-pass | -82.98, -83.01 | -82.98, -83.01 |

   An own free fit of the 14 filter constants on other captures (own core;
   `nv13_filters.py`) lands on the same low shelf, 1220.99577 Hz and
   -0.2800005 dB (author 1220.9958 and -0.2800004; `network_b` 1220.9960 and
   -0.2800006), and on another high shelf (12839.88 Hz against 12839.81 and
   12840.10). The agreement of three fits on the low shelf is real as a fit
   result, but that value alone moves the tail the wrong way; only the six
   values together carry the lag. The model should keep the round values, as
   it does.
7. **The lag is not in the line length** (`nv15_events.py`, self-test
   `nv16_events_selftest.py`). On three trains of single impulses (Decay
   0.5 s; Size 60, 100, 200 %) the two reads that carry each first-pass
   arrival were solved for a length error, in single-precision steps of that
   read's length:

| Size | first-pass reads | one step longer | one step shorter | unclear | other reads: mean offset of the reference, samples |
| --- | --- | --- | --- | --- | --- |
| 100 % | 11 083 | 4 | 2 | 0 | -2.9e-8 +- 0.2e-8 |
| 200 % | 10 263 | 2 | 4 | 0 | -2.8e-8 +- 0.3e-8 |
| 60 % | 11 645 | 6 | 5 | 2 (near -0.5) | -2.7e-8 +- 0.2e-8 |

   - One-step reads: 23 in 32 991 (7.0e-4; `network_b` found 5 in 5568), on
     every step size from 6.1e-5 to 4.9e-4 samples, spread over the whole
     oscillator cycle, **12 longer and 11 shorter**. A mean length longer by
     1.9e-7 samples would need a surplus of about 40 longer reads in this
     set. The one-step reads are the incoherent floor and not the lag.
   - All other first-pass reads: the reference is 2.8e-8 samples *early*, not
     1.9e-7 late. The estimator returns 1.897e-7 from a synthetic reference
     made with 1.9e-7 samples added to every length.
   - A first pass contains the equaliser, the read, the attenuation, the tap
     and the output filter, and not the matrix and the loop kernel. REFUTED:
     "1.5e-7 to 2.0e-7 samples added to every line length" as the mechanism
     (it fits the tail, -94.3 to -95.4 dB here, only as a stand-in).
     INFERRED: the lag is added in the feedback path; what in it is OPEN. The
     -2.8e-8 samples of a first pass (about half a single-precision step
     at 1) is unexplained too.

## 6. Laws re-derived on fresh captures (own core, own least squares)

- **Tap weights** (`nv04_taps_gains.py`). 2 x 16 weights solved freely at 16
  fresh settings, Decay 0.5, 0.57, 1.23, 2.5, 2.9, 4.0, 4.5, 5.9, 6.0, 6.1, 9,
  12, 30, 59 s: largest difference from the law 1.4e-7 to 8.9e-7; once
  2.7e-6 (line 16 at Decay 0.5 s, Size 200 %, where that line hardly sounds).
  Line 1: 0.4570474 against 0.4570473 at 0.57 s, 0.2322181 / 0.2322181 at
  5.9 s, 0.2280000 at 6.0, 6.1, 9, 30 and 59 s. Tap of line 9: 1 within 4e-7.
  CONFIRMED, knee at 6 s included.
- **Input gains.** 2 x 32 gains (own and cross per line) solved freely on five
  captures: `B_n` within 4.1e-7 to 6.4e-6 relative of the S-curve, width
  weight within 4.9e-7 to 2.7e-6 of the law. CONFIRMED.
- **`B_1`** (`nv05_scale.py`). Least-squares scale of the model on each of 38
  captures: 0.24397756 (Decay 30 s) to 0.24397771, mean 0.24397764, standard
  deviation 3.4e-8. The value is right to about +-1e-7; the author's "spread
  1.0e-7 relative" is the spread of one kind of capture, the spread over
  settings is 6e-7 relative. The closed form 0.24397783 is worse on every
  capture. (`network.json` holds 0.24397766367314302, the document
  0.24397766: -156 dB apart.)
- **Matrix** (`nv06_matrix.py`). One Gauss-Newton step with all 256 entries of
  each group free, two fresh captures (condition 25 to 44): the left group
  moves by at most 2.4e-6 of 1/4, the right by 7.8e-7 (rms 3.6e-7 and
  2.4e-7), 256 of 256 signs kept in each group; the fit gains 0.07 dB at
  most. Rows in natural order +7.3 / +2.3 dB, the transposed matrix -10.6 /
  +0.7 dB, one sign flipped -24.6 / -11.4 dB, scale 0.249999 for 0.25:
  -101.6 / -91.4 dB (from -118.3 / -113.4). CONFIRMED; the judge had not
  re-measured the entries.
- **Filters** (`nv13_filters.py`). One Gauss-Newton step, 14 constants, eight
  captures (condition 1.7e6): all within 1.2e-5 relative of the round values
  (equaliser 200.0013 Hz, 0.5000002 dB, 0.3999977; 1749.983 Hz, -0.5000017,
  0.3999969; low-pass 20000.0019 Hz, Q 0.99999997). On captures the fit did
  not see, free against round values: -0.50 to +0.37 dB overall. CONFIRMED.

## 7. Oscillators, length expression, structure (own core, fresh captures)

One thing changed at a time. Rows up to the increment: four captures on which
the documented model reads -113.94, -118.30, -115.31 (warm-up 59 s) and
-115.96 dB (`nv14_oscillator.py`). Rows from the loop kernel on: four captures
at -113.94, -118.30, -113.73 (Size 171 %) and -113.83 dB (Decay 30 s, Size
30 %) (`nv17_elements.py`).

| change | null, dB |
| --- | --- |
| correctly rounded sine without the reduction / the C library's `sinf` | -98.97 to -104.25 / -98.98 to -104.26 |
| depth one single-precision step lower / higher | -98.73 to -103.41 / -98.61 to -103.51 |
| start phases group after group / equal in both groups | +3.6 to -9.3 / +0.8 to -13.1 |
| start phases reduced into one cycle before the first sample | -55.2 to -67.5 |
| every length 1e-5 / 1e-4 samples longer | -93.2 to -106.4 / -73.2 to -86.7 |
| increment one single-precision step higher; `fl(2 pi) * 0.6f / 44100f` | identical output |
| increment one single-precision step lower | 0.19 to 0.41 dB worse |
| no loop kernel / kernel reduced to its first tap | +4.3 to -23.0 / -3.5 to -26.1 |
| no cross feed | -11.0 to -16.7 |
| equaliser behind the lines | -63.0 to -84.7 |
| attenuation from the rounded length (Size 100 %; 171 %, 30 %) | unchanged; -94.3, -97.3 |
| tap weights of Decay 0.5 s | -15.3 to -29.4 |
| primes of the two groups exchanged | +3.1 to -10.6 |

- Time origin CONFIRMED: warm-ups of 3.3 to 59 s null alike, at both host
  rates, and an impulse at host sample 0 nulls (-102.4 dB in the first quarter
  second of the own stimulus).
- The increment is not pinned to its last place by a null: a single-precision
  accumulator adds the increment rounded to the accumulator's own step, so
  neighbouring increments give the same or nearly the same phases. This is
  also why the rate is 0.59988 Hz (README, reconnaissance 6) and not 0.6 Hz. A
  product that wants to null against the reference must keep the
  single-precision accumulator, wrap test and start values as documented.

## 8. Reproduction and audit

- `score_network.py` on the delivered model (through a wrapper that only
  redirects the build folder): worst -114.07, mean -116.79 dB, every case and
  stretch as reported.
- `fit_network.py`, run with its output redirected into this packet's scratch
  folder: the file is byte-identical to `tide_structural_data/network.json`
  (sha256 28e5555af1389e2d...).
- Holdout discipline: `holdout`, `20261007`, `NETWORK_HOLDOUT`,
  `score_network`, `network_programme(` searched in `fit_network.py`,
  `fit_network_a.py`, `fit_network_b.py` and the scratch folders `network`,
  `network_a`, `network_b`: only comments, the three scorer logs and other
  seeds (777, 7302, 60606). No leak found. Two soft points, neither a leak:
  `B_1` is fitted on `network_b`'s single impulses at the settings of the
  sixth holdout case (44.1 kHz, Decay 2 s, Size 100 %, warm-up 10 s) with
  another stimulus; and the three authors saw the scorer's output between
  revisions (three runs each on their own model, all disclosed). The 38
  fresh captures here show no sign of a model tuned to the holdout.
- The "not fitted on" sets of the judge's free fit are the fit set's settings
  at other host rates, so they are weaker than they read; the conclusions
  they carry (round values cost 0.2 dB; the free kernel removes the lag) hold
  on this packet's independent captures (sections 5 and 6).
- No contradiction with the README or the verified wave-1 findings beyond the
  README items the author already lists as superseded; `laws_verification`
  already has line 10's tap "not linear".

## 9. Weak spots a product should know

1. **Size off whole percents** (section 3): use rule W for any null test of a
   product against the reference; with the document's rule one knob position
   in 200 is one sample off on a line and nulls at -6 to -24 dB.
2. **Depth of the null by material**: -108 to -121 dB for programme, -100 to
   -103 dB for steady broadband input, -96 to -105 dB for any stretch that is
   only recirculated sound, -80 dB after 2000 passes (inaudible: below
   -500 dBFS). The worst corner found is Decay 30 s with Size 200 %
   (-108.1 dB).
3. **The null needs the single-precision details**: accumulators, the sine
   reduced with the single-precision pi/2, depth 38.808002, three separate
   roundings of the length, the wrap of the start phases during the first 55
   samples. An exact sine or a depth one step off costs 14 to 15 dB, start
   phases wrapped beforehand 50 dB (own tests); the roundings 5 to 16 dB (the
   author's test). None of them is audible.
4. **The floor** is set by reads whose length is one single-precision step
   off: 7.0e-4 of the reads, in either direction, at no preferred phase.
   INFERRED, with the author: the last place of the reference's sine, which
   is not identified.
5. **The lag** of about 2e-7 samples per pass sits in the feedback path and is
   not modelled; the free-fit kernel is a stand-in that must be used as a set
   of six or not at all.
6. **Scope**: Macro 0, Mix 100 %, Brightness 0 %, Width 100 %, input filter
   open, input peaks at or below 0.5, no Freeze. Whether the loop is the same
   above Macro 0 is not measured by anyone.
7. Housekeeping: `network_model.py` compiles its core into the scratch folder
   of packet `network` (`BUILD`); a product build should not depend on a
   scratch folder.

## 10. Not established, not checked

- OPEN: what in the feedback path produces the lag; why first-pass reads are
  2.8e-8 samples early; the last place of the reference's sine.
- Rule W: the plug-in's `expf`, and Decay's arithmetic, are inferred.
- Not re-derived with own code: the element swaps against `network_a` and its
  tail (claims 5 and 6); the converters (packet `converters` has its own
  verification; here only nulls at 48, 88.2 and 96 kHz).
- Not captured: host rates above 96 kHz, block sizes other than 512, a
  warm-up that is not a whole number of samples, input above 0.5.

## 11. Reproduce

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
W=../../Results/RevOceanCharacterization/work/network_verification
$PY $W/nv01_captures.py                 # the 38 named captures (1 minute)
$PY score_network.py $W/nv_candidate.py # locked holdout, delivered model
$PY $W/nv03_regenerate.py               # fit_network.py into scratch, byte comparison
$PY $W/nv02_fresh.py                    # nulls per capture, stretch, block and band
$PY $W/nv04_taps_gains.py; $PY $W/nv05_scale.py; $PY $W/nv06_matrix.py; $PY $W/nv13_filters.py
$PY $W/nv07_tails.py; $PY $W/nv19_kernel_parts.py; $PY $W/nv20_author_tails.py
$PY $W/nv08_size.py; $PY $W/nv09_size_rule.py; $PY $W/nv10_true_lengths.py
$PY $W/nv11_size_search.py; $PY $W/nv12_size_validate.py; $PY $W/nv21_display_sizes.py
$PY $W/nv14_oscillator.py; $PY $W/nv17_elements.py
$PY $W/nv15_events.py; $PY $W/nv16_events_selftest.py; $PY $W/nv22_factors.py
```

Each script writes a `.json` of its name beside itself. `nv_common.py` holds
stimuli, cases and nulls; `nv_core.c` and `nv_net.py` the own network.
`nv_try*.py` are exploration (the fused multiply-add search of section 3,
step 3, is `nv_try5.py`; its result is what `nv12_size_validate.py` tests on
new captures). `nv18_low_shelf.py` is the single-value test of section 5.6.
