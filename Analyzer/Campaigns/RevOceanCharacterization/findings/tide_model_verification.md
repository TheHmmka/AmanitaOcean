# tide_model_verification: independent check of the complete Tide model

Packet `tide_model_verification`, wave 2, 7 October 2026. Checked: `tide_model.py`, `score_tide.py`,
`fit_tide_model.py`, `tide_structural_data/tide_model.json`, `findings/tide_model.md`. Reference:
Arturia Rev OCEAN 1.0.0.5848, Tide mode, neutral baseline, **Mix 100 % in every capture; Macro
100 % unless a line says otherwise**.

Tags: MEASURED, FITTED (residual given), INFERRED, OPEN. Verdicts: CONFIRMED, REFUTED, PARTLY,
UNVERIFIED. A null is `20 log10(rms(model - reference) / rms(reference))` with no gain, delay or
polarity fitted; above Macro 0 the phase of each output is fitted and nothing else.

## 0. Verdict

- **The model holds.** Voice assignment, loop, rate law, order of equaliser and comb, the generator's
  form, Macro 0 and both hosts above 48 kHz were re-derived with own code on 105 fresh captures and none
  failed. The data file regenerates entry for entry, the scorer reproduces its numbers with other
  seeds, no holdout is read.
- **One headline number is too good for a product to rely on.** "-89 to -107 dB at Macro 100 %" is the
  null on sparse material at instance times below 44 s. On steady pink noise the same render nulls
  at **-86.8 and -93.2 dB**, and on 50 s of mixed material 58 to 108 s after the instance start at
  **-79.9 dB**. The cause is measured: single tap changes of the comb read that the model places one
  sample off. Moving 1 of 67 (and 12 of 434) downward crossings of the comb delay by one sample brings
  the same captures to -98.4 dB (and -92.8 dB). This is weak spot 1 of `tide_stage_verification.md`;
  the author's summary lists it only as an inferred limit of impulse trains.
- **Three statements are corrected**: the start of an instance (section 9), the open question on
  start levels after a warm-up (answered on 64 realisations, section 8), and the missing warning
  that the Size rule of `network_model.py`, refuted by `network_verification.md`, is inherited
  (Size 80.1 %: -16 to -20 dB at Macro 100 %).
- At Macro 100 %, Mix 100 % the free-running model is, on 89 numbers a listener would care about
  (long-term spectrum, level and band-level cycle, decay per band, correlation of the outputs),
  another draw of the reference: largest difference 0.09 dB in the spectrum, 0.4 dB in a band's
  level range, 0.06 s in a decay time, 0.008 in a correlation; the same comparison rejects the
  model set to Macro 90 % at 23 standard errors.

## 1. What was done

Own code for every check, in `Analyzer/Results/RevOceanCharacterization/work/tide_model_verification/`:
`vstim.py` (stimuli, capture list), `vfit.py` (phase estimator), `vcomb.py` and `vcomb.c` (the comb
with its delay given per sample), one script per section (`v00` to `v15`). The delivered model is called
for its predictions only (`voice_inputs`, `voices`, `output`, `line_taps`, `render`); none of the
author's analysis functions (`PhaseFit`, `SplineCurve`, `EaseCurve`, `local_weight`, `Bench`, ...) is
used. Every reported null is computed from `tide_model.render(..., phase=...)`, or from the same
delivered functions with other voice inputs where a split, an order or a tap change is varied.

**Own phase estimator.** (a) A model-free track: an independent quadratic per window of 0.25 s and
output, not continuous across windows (168 numbers per output per 14 s, against 59 of the author's
finer spline), fitted window by window after a level grid. (b) The generator's form (a rate, a hold,
raised cosines between one knot per cell of 7.353 s / 6.25 s), fitted to the track and then to the
audio, knots held in their cells. Weights: 1 / rms of the reference per 20 ms frame.

**Captures: 105, 0.62 GB of cache, every input sample at or below 0.5** (programme 0.5, noise 0.45, level-cycle noise 0.25).
Programme = `datasets.network_programme` with seed 97531 (the author: 8301); realisations 5 and up.

| set | captures | what |
|---|---|---|
| A1 to A11 | 11 | whole responses: programme at 44.1, 48, 88.2, 96 kHz (Decay 0.5 to 30 s, Size 30 to 200 %, warm-ups 8 to 17.3 s); steady pink noise of 12 s at 44.1 and 48 kHz (Macro 100 and 35 %); an impulse train 0.35 to 0.6 s apart; 50 s of noise, impulses and tones after a 58 s warm-up |
| B | 14 | programme starting 2.5 s after the instance, Decay 2.6 s, Macro 100, 88, 62, 35, 15, 3.5, 1 %, two realisations each |
| C, start | 11 | pink noise from the instance start and from 1 s after it, Decay 2 s and 0.5 s, Macro 100, 50, 10, 0.1 % |
| D | 9 | 60 s listening stimulus (30 s pink noise, five bursts), Decay 3 s: realisations 5 to 12 at Macro 100 %, one capture at Macro 0 |
| E | 2 | Macro 0: programme at 96 kHz, pink noise at 44.1 kHz |
| G | 2 | Size 80.0 and 80.1 % at Macro 100 % |
| level | 56 | the author's level-cycle noise (48 kHz, Decay 0.5 s): realisations 8 to 15 of the 52 s stimulus, 48 realisations of its first 3 s |

## 2. Verdict per claim

Rows 1 to 8 and 10 to 16 are the fifteen claims of the author's report in their order; row 9 is the
hold of his findings table.

| # | Claim of the packet | Verdict | This packet's numbers |
|---|---|---|---|
| 1 | Output taps of lines 1 to 8 pass the voice at the output's phase, lines 9 to 16 the voice half a cycle away; no line shared | CONFIRMED | six fresh captures (Macro 100, 88, 35 %; Decay 0.5 to 30 s; Size 30 to 200 %; 44.1 and 48 kHz): delivered split -101.4 to -110.5 dB per output; split after line 7 -6.4 to -14.7 dB, after line 9 -5.4 to -14.2 dB, lines 8 and 9 exchanged -3.2 to -9.1 dB, odd/even -1.8 to -7.8 dB, one voice -1.6 to -8.0 dB, each with 168 free phase numbers per output. 64 free weights per output: 1 or 0 within 3.6e-6 to 4.9e-5 in all 12 outputs (section 5) |
| 2 | The cross-fed part goes with the line; the phase belongs to the output | CONFIRMED | cross-fed part at the phase of its input's channel: -0.3 to -17.6 dB on the six captures; its 32 free weights equal those of the line |
| 3 | Loop, tap weights and gains of Macro 0 hold above Macro 0 | CONFIRMED, extended | stretch 6 to 14 s of six fresh programme captures: -92.3 to -100.2 dB, now from Decay 0.5 s / Size 30 % (-98.5 dB) to **Decay 30 s / Size 200 %** (-94.9 dB); free weights 1 |
| 4 | The input equaliser sits in front of the comb | CONFIRMED | 8 of 8 fresh captures, both outputs in every one: 1.2 to 6.1 dB better overall with a model-free phase (-104.6 to -110.6 against -99.5 to -109.4 dB), Macro 15 to 100 %, 44.1 and 48 kHz |
| 5 | At Macro 0 `render` equals `network_model.render` | CONFIRMED | bit-identical on three stimuli (96 kHz programme, 14 s and 60 s of noise); general path at Macro 0: -304.8 to -309.9 dB, at Macro 1e-9: -160 dB (continuous); against fresh Macro 0 captures -109.6, -110.9, -116.0 dB |
| 6 | Phase-fitted null -88.8 to -106.8 dB at Macro 100 %, -91.6 to -105.7 dB at 50 %, -96.1 to -100.8 dB at 25 % | **PARTLY** | reproduced for sparse material (fresh programme: -96.9 to -104.2 dB at Macro 100 %, -106.2 dB at 62 %; train -89.6 dB). **Steady noise: -86.8 and -93.2 dB (Macro 100 %), -90.0 dB (35 %); 58 to 108 s after the start: -79.9 dB.** Cause measured: comb tap changes one sample off (section 4) |
| 7 | Comb and voices run inside the 44.1 kHz core at 88.2 and 96 kHz hosts | CONFIRMED | with the Macro values exchanged: 88.2 kHz at Macro 100 %: -96.9 dB (model-free -101.1); 96 kHz at Macro 62 %: -106.2 dB |
| 8 | The model holds from Macro 2 to 100 %, the blend below 5.68 % included | CONFIRMED, extended to 1 % | 14 captures at Macro 100, 88, 62, 35, 15, 3.5, 1 %: generator form -100.6 to -114.6 dB (-114.5 and -114.6 dB at 1 %) |
| 9 | During the hold the phase is a straight line | CONFIRMED | 22 of 28 outputs: 8.7e-8 to 1.7e-6 cycle rms from 3.25 s to 0.5 s before the first knot. The six others are at Macro 3.5 and 1 %, where the track does not determine the phase (the generator form still nulls them) |
| 10 | rate(m) = 0.0431885 + 0.0070490 e^(0.63 m) cycles/s (0.0502375 to 0.0564238) | CONFIRMED | law minus this packet's readings: +2.8e-7, +1.4e-7, +2.2e-7, +2.9e-7, -0.9e-7 at Macro 100, 88, 62, 35, 15 % (four readings each, standard errors 0.5e-7 to 1.4e-7), +2.0e-7 and +4.0e-7 at 3.5 and 1 % (one reading each). Own free fit: A 0.0431867, B 0.0070506, k 0.62989, ends 0.0502373 and 0.0564235. k = 0.630 +- 0.001 (section 6) |
| 11 | `tide_phase`'s law is wrong at low Macro; 0.05645 and 0.0565 excluded | CONFIRMED | old law minus readings: +5.5e-4 (1 %), +5.2e-4 (3.5 %), +4.0e-4 (15 %), +2.0e-4 (35 %), +1.9e-5 (62 %), -2.1e-5 (88 %), +2.6e-5 (100 %) |
| 12 | Hold, then raised cosines between one knot per cell, at about 1e-5 cycle | CONFIRMED | 22 outputs of 11 fresh captures: generator form against the model-free track 1e-7 to 4.7e-6 cycle rms in 21 (the 22nd is one bad track window); 62 knots inside the records, all alone in their cell also when the knots are set free, all values inside the published bounds +-0.004; holds 58 to 108 s after the start (15 knots in cells 8 to 16). The form costs up to 10 dB on single outputs against the model-free phase (-92.5 against -103.0 dB) |
| 13 | A fresh instance needs up to 3 s to settle and behaves as if Macro came from about 50 % | **PARTLY** | two things, not one (section 9). (a) The first 0.75 s of an instance are unrelated to the model at every Macro value (-1.5 to -13 dB, also at 50 %; -21 to -24 dB from 0.75 to 1 s); input in that stretch leaves an error that rings on at the Decay rate (Decay 2 s: below -100 dB only at 3.5 s). (b) From 1 s on a glide with a time constant of about 0.2 s remains, smallest at Macro 50 % (-72 dB in the first 0.25 s against -51 dB at 100 %), over at **2.1 s**. "As if from 50 %" is supported for (b) only |
| 14 | With its own generator the model is another draw of the reference at Macro 100 %; within one error of the means at 50 % | CONFIRMED | the author's score with seeds 100 to 107 and 200 to 207: impulse descriptors 0.13 to 0.42 and 0.15 to 0.47 spreads, fluctuation 1.51 and 1.36, modulation rates 1.01 and 0.91; Macro 50 %: largest rms in errors of the means 1.10. Own comparison on fresh realisations: section 7 |
| 15 | The level cycle under noise is reproduced; given the phase it is exact | CONFIRMED | eight new realisations against seeds 40 to 79: floor, top, swing, mean power, period within 1.4 standard errors (-36.42 / -36.57, -31.52 / -31.60, 4.90 / 4.98, -33.47 / -33.56 dB, 17.5 / 17.5 s). "Exact given the phase" follows from the nulls on noise (-86.8 to -98.4 dB), far below the 0.05 dB the author could show |
| 16 | Law of the start levels after a warm-up: OPEN (8 of 8 on the level floor, p = 0.08) | **answered** | 64 realisations: level in the first second of noise -37.19 / -36.76 / -34.90 dB (5, 50, 95 % points) against -37.20 / -36.77 / -34.66 dB of 400 model seeds, KS p 0.91; 6 of 64 (6 of the 56 new ones) more than 1 dB above the author's highest, against 14.8 % of the seeds (binomial p 0.15). The eight were a fluctuation |
| - | regenerating script reproduces the data file | CONFIRMED | 1095 s with six workers; every entry equal, scores included. Not byte-identical: the top-level keys come in another order |
| - | no locked holdout read | CONFIRMED | section 10 |

Nothing in the packet contradicts README.md once the three README edits the author lists are made.
Two verified wave-2 findings are missing from the packet's own list of limits: the comb tap changes
(`tide_stage_verification.md`, weak spot 1) and the Size rule (`network_verification.md`, section 3).

## 3. Whole responses on fresh captures (claims 3, 6, 7, 8)

`v14_form.py`, `v02_rate.py`. Delivered render, phase by this packet's estimator. "Model-free": a
quadratic per 0.25 s. "Generator form": 7 to 13 numbers per output for 14 s, 21 and 23 for 50 s,
knots held in their cells. Null in dB, overall and left / right.

| capture (host, Decay, Size, warm-up, Macro) | model-free | generator form | left / right |
|---|---|---|---|
| A1 programme, 44.1 kHz, 4 s, 100 %, 10 s, 100 % | -106.0 | -97.7 | -106.5 / -92.5 |
| A2 programme, 48 kHz, 1.3 s, 91 %, 17.3 s, 100 % | -104.8 | -104.2 | -104.6 / -103.6 |
| A3 programme, 88.2 kHz, 2.4 s, 140 %, 10 s, 100 % | -101.1 | -96.9 | -99.8 / -94.8 |
| A4 programme, 96 kHz, 0.9 s, 45 %, 8 s, 62 % | -102.6 | -106.2 | -109.8 / -104.1 |
| A9 programme, 44.1 kHz, 30 s, 200 %, 10 s, 100 % | -102.3 | -97.6 | -105.1 / -96.1 |
| A10 programme, 44.1 kHz, 0.5 s, 30 %, 10 s, 100 % | -104.6 | -102.9 | -105.7 / -102.0 |
| A7 impulse train, 44.1 kHz, 0.7 s, 100 %, 10 s, 100 % | -80.3 (the track fails in three windows) | -89.6 | -92.3 / -87.4 |
| **A5 pink noise, 44.1 kHz, 4 s, 100 %, 10 s, 100 %** | -86.5 | **-86.8** | -92.1 / -84.6 |
| **A6 pink noise, 48 kHz, 2 s, 120 %, 23.7 s, 100 %** | -91.6 | **-93.2** | -92.3 / -94.5 |
| **A11 pink noise, 44.1 kHz, 1.5 s, 100 %, 10 s, 35 %** | -90.0 | **-90.0** | -98.3 / -86.5 |
| **A8 noise, impulses, tones; 44.1 kHz, 3 s, 100 %, 58 s, 100 %** (50 s) | -80.1 | **-79.9** | -83.3 / -78.1 |
| B, 14 programme captures from 2.5 s, 44.1 kHz, 2.6 s, Macro 100 to 1 % | -73.3 to -112.6 | -100.6 to -114.6 | |

- Sparse material confirms the author's range. MEASURED.
- **Steady material and late instance times do not.** MEASURED. In A5 the residual is one event: the
  windows from 13.25 s read -69.9 dB (right) and -78.6 dB (left) and then fall with the reverberation
  (4.3 dB per 0.25 s at Decay 4 s). A8 has such events at 60.5, 62.0, 64.3, 66.8, 68.8, 72.8, 75.0,
  76.0 and 77.0 s and one at 98.75 s that stands 50 dB below the local signal for 0.75 s.
- The model-free track is this packet's own weak point at low Macro: at 3.5 and 1 % it loses the
  phase in single outputs (-73 to -84 dB) where the generator form reaches -100.6 to -114.6 dB.

## 4. The comb's tap changes limit the null (new; claim 6, the author's first open question)

`v06_events.py`, `v08_tapchanges.py`, `vcomb.py`. The comb read is a first-order all-pass whose
coefficient is near 1 just above a whole-number delay; where the delay falls through a whole number
the taps change. The chain behind the comb is linear for a given phase, so the effect of moving one tap
change by one sample is the response to the difference of two comb outputs. Every crossing in a
capture was tried one sample earlier and later; a move was kept when it removed more than half of
the weighted residual in the 0.6 s after it.

| capture | downward crossings (left, right input) | moves kept | null as delivered | with the moves, same phase |
|---|---|---|---|---|
| A5 pink noise, Macro 100 % | 0, 67 | 1 | -86.82 dB | **-98.35 dB** (-98.7 / -98.0) |
| A11 pink noise, Macro 35 %, other Decay and realisation | 0, 67 | 1, the same crossing | -89.95 dB | **-102.01 dB** |
| A8, 58 to 108 s | 236, 198 | 12 | -79.91 dB | **-92.82 dB** (-93.0 / -92.7) |
| A6 pink noise at 48 kHz; A1, A2, A3, A9 programme | 0, 66 or 67 | 0 | -93.2; -96.9 to -104.2 | unchanged |
| A7 and the author's four impulse trains | 0, 66 or 67 | 0 | -88.8 to -96.1 | unchanged |

- **MEASURED: the residual of steady material is single tap changes on the wrong sample.** One move
  removes 99.6 % of the residual of the 0.6 s behind it in A5 and 11.5 dB of the null of the whole
  14 s. All 13 moves are at downward crossings.
- The reference's tap-change samples, as far as these captures show them (seconds since the first
  processed sample, whole number crossed, where the reference changes taps relative to
  `tide_stage.comb_delay`):

| input | time, s | whole number | reference is | input | time, s | whole number | reference is |
|---|---|---|---|---|---|---|---|
| right | 13.18712 | 605 | 1 sample later | right | 68.89898 | 342 | 1 earlier |
| left | 62.12796 | 610 | 1 later | right | 72.92379 | 323 | 1 earlier |
| left | 63.18712 | 605 | 1 later | right | 75.04211 | 313 | 1 earlier |
| left | 64.24628 | 600 | 1 later | right | 76.10127 | 308 | 1 earlier |
| right | 60.63751 | 381 | 1 later | right | 77.16043 | 303 | 1 earlier |
| right | 62.54401 | 372 | 1 earlier | right | 98.76730 | 201 | 1 earlier |
| right | 66.78066 | 352 | 1 earlier | | | | |

- The left crossing at 63.18712 s is the right crossing at 13.18712 s exactly 2 205 000 samples
  (50.000 s, a quarter period) later, with the same whole number and the same move: the two combs
  run one delay sequence a quarter period apart, to the sample. MEASURED.
- At whole numbers of 381 and more the reference is later than the model, at 372 and less earlier.
  The delivered law has the delay at 372.2 samples where its triangle passes -0.25, a power of two,
  where a single-precision number doubles its step. INFERRED: the direction of the error follows the
  rounding of the triangle value; a hint for whoever identifies the arithmetic, which stays OPEN.
  Exact stream indices are in `v08_*.json`.
- How often: 11 of 188 downward crossings in the 20 s of noise of A8, 1 of 67 in A5, 0 of 66 in A6;
  `tide_stage_verification.md` found 3 of 53. A crossing without signal in the comb cannot be judged,
  so these are lower bounds.
- **Impulse trains are not limited by this.** No move helps any of the five trains (-88.8 to
  -96.1 dB). MEASURED. The author's inference (the delay's value near a crossing, not the sample of
  the tap change) is not contradicted; it stays UNVERIFIED here.
- After the moves A8 stands at -92.8 dB and A5 at -98.4 dB. What is left was not separated.

## 5. Which signal each voice filters (claims 1, 2, 3)

`v03_assignment.py`. Per-line output taps from `tide_model.line_taps` with single-line groups, each
input apart. Every split gets its own model-free phase. Null per output, left / right, dB:

| capture | lines 1-8 / 9-16 | 1-7 / 8-16 | 1-9 / 10-16 | 8 and 9 exchanged | odd / even | one voice | cross-fed part at its input's phase |
|---|---|---|---|---|---|---|---|
| A1, Macro 100 %, Decay 4 s | **-108.1 / -103.0** | -8.1 / -9.1 | -5.4 / -5.8 | -4.0 / -4.2 | -1.9 / -2.4 | -3.2 / -3.4 | -13.8 / -5.1 |
| A9, Decay 30 s, Size 200 % | **-104.8 / -101.4** | -8.9 / -9.1 | -6.8 / -6.9 | -4.8 / -5.0 | -2.4 / -2.5 | -2.8 / -3.3 | -7.3 / -6.0 |
| A10, Decay 0.5 s, Size 30 % | **-105.9 / -104.1** | -6.8 / -6.4 | -7.6 / -6.7 | -3.4 / -5.2 | -2.5 / -3.0 | -3.9 / -1.9 | -10.0 / -9.1 |
| A2, 48 kHz, Size 91 % | **-105.7 / -103.6** | -9.0 / -11.8 | -7.5 / -11.0 | -5.7 / -9.1 | -3.6 / -6.0 | -5.4 / -8.0 | -10.9 / -8.0 |
| B88, Macro 88 % | **-109.2 / -103.6** | -7.1 / -10.1 | -6.6 / -8.8 | -3.2 / -7.6 | -1.8 / -4.5 | -1.6 / -4.9 | -17.6 / -0.3 |
| B35, Macro 35 % | **-110.5 / -107.0** | -14.7 / -10.0 | -14.2 / -7.8 | -8.0 / -6.9 | -7.8 / -4.5 | -8.0 / -4.5 | -10.7 / -17.0 |

- Exchanging the voices gives the same null to the digit, with the phase half a cycle away. MEASURED.
- Free weights (64 per output, at the phase of the delivered split): largest deviation from 1 or 0
  is 3.6e-6 to 4.9e-5 in all 12 outputs (standard errors 3e-7 to 1.3e-6). MEASURED. The regression
  needs a phase without a flaw: with the model-free track as the phase the left output of A2 first
  read 3.4e-3, from a single window in which the track had lost the phase; with the generator form
  it reads 4.9e-6.

## 6. Rate law and hold (claims 9, 10, 11)

`v02_rate.py`, `v02_rate_law.py`. Slope of a straight line through the track from 3.25 s to 0.5 s
before the first knot. Cycles/s:

| Macro | 100 % | 88 % | 62 % | 35 % | 15 % | 3.5 % | 1 % |
|---|---|---|---|---|---|---|---|
| readings | 4 | 4 | 4 | 4 | 4 | 1 | 1 |
| mean | 0.05642352 | 0.05545995 | 0.05360578 | 0.05197621 | 0.05093621 | 0.05039446 | 0.05028165 |
| range of the readings | 6.5e-7 | 2.1e-7 | 5.6e-7 | 4.6e-7 | 2.8e-7 | | |
| delivered law minus mean | +2.8e-7 | +1.4e-7 | +2.2e-7 | +2.9e-7 | -0.9e-7 | +2.0e-7 | +4.0e-7 |

- The law holds at five Macro values the author did not measure, within 4.0e-7. FITTED residual of
  the delivered law on these seven means: 2.5e-7 rms. The readings above Macro 15 % lie 1.4e-7 to
  2.9e-7 below the law (1.6 to 3.0 standard errors each): the end value at Macro 100 % is
  0.0564235 here against 0.0564238. Use **0.0564237 +- 0.0000003**; the difference is 5e-6 of the rate.
- Shape. FITTED on the author's seven and these seven means together: free k = 0.63010; rms
  residual 1.9e-7 at k = 0.63, 7.4e-7 at 0.6275, 6.9e-7 at 0.6325, 1.4e-6 at 0.625 and 0.635. So
  k = 0.630 +- 0.001, and 0.88 x 0.71337 = 0.62777 is rejected (6.7e-7 rms), as the author says.
- Considered and excluded (INFERRED, arithmetic only): a single-precision accumulator of the ramp.
  Per 44-sample block it would change the rate by up to 4.7e-4 of its value where the ramp passes
  0.5 cycle (8.9 s at Macro 100 %); the holds are straight to 1e-6 cycle across that point.
- No round form for 0.0502375 and 0.0564237 found here either. OPEN.
- The ramp counts from the first processed sample: after a 58 s warm-up (A8) every knot value lies
  inside the published bounds; an origin at the stimulus start would shift them by 0.27 cycle. The
  rates fitted without a hold are 0.0564224 and 0.0564158 there (law 0.0564238): the long-run rate
  equals the hold rate within 1e-5. MEASURED.

## 7. Listening with numbers at Macro 100 %, Mix 100 % (claims 14, 15)

`v07_listen.py`, own descriptors. 60 s at 44.1 kHz, Decay 3 s, Size 100 %, warm-up 10 s: 30 s of
steady pink noise, independent on the two inputs, then five bursts of 0.5 s. Reference: eight fresh
realisations. Model: 24 seeds (500 to 523). z = difference of the means in standard errors.

| group | numbers | largest difference | rms z | beyond 2 / 3 | model at Macro 90 %: largest difference, rms z |
|---|---|---|---|---|---|
| spectrum, 28 third octaves, 31 Hz to 16 kHz | 28 | 0.09 dB | 0.33 | 0 / 0 | 1.53 dB, 8.3 |
| level per 0.25 s: 5, 50, 95 % points, swing, mean power, left minus right | 7 | 0.03 dB | 0.25 | 0 / 0 | 0.22 dB, 2.6 |
| level per octave band, 125 Hz to 8 kHz: 5 and 95 % points, swing | 21 | 0.38 dB | 0.59 | 0 / 0 | 2.31 dB, 6.8 |
| EDT, T20, T30 per octave band | 21 | 0.063 s | 1.20 | 2 / 0 | 0.050 s, 1.2 |
| correlation of the outputs: broadband, per band, spread over 0.5 s frames | 9 | 0.008 | 1.19 | 1 / 0 | 0.009, 1.2 |
| tilt 4 kHz minus 250 Hz: 5, 50, 95 % points | 3 | 0.30 dB | 0.50 | 0 / 0 | 1.98 dB, 5.0 |

- 89 numbers, three beyond 2 standard errors (four expected by chance), none beyond 3; the largest
  is the EDT at 500 Hz, 2.669 s against 2.641 s (+1 %, z 2.7). MEASURED.
- What the cycle is under these conditions: the level of the 8 kHz band swings by 9.8 dB in the
  reference and 9.4 dB in the model (4 kHz: 7.5 / 7.2 dB; 2 kHz: 6.1 / 5.9 dB); the broadband level
  of pink noise swings by 1.7 dB, as at Macro 0, because the low end is hardly touched. T30 per band:
  2.56, 2.63, 2.72, 2.51, 2.01 s at 500 Hz to 8 kHz in the reference, within 0.04 s in the model.
- The comparison can tell: the model at Macro 90 % is off by 1.5 dB in the spectrum (20 of 28 numbers
  beyond 3 standard errors) and 2.3 dB in the band levels. Decay times and correlations do not
  separate Macro 90 from 100 %; they test the network, not the Tide amount.
- At Macro 0 model and reference agree on every one of the 89 numbers within 5e-6.
- Weak: with four against four, the reference's own halves differ by up to 3.8 standard errors in the
  spectrum. The level cycle lasts 17.7 s and 28 s of noise hold 1.6 cycles, so single renders are not
  Gaussian draws; the z values above are a guide, not a test.

**The author's level-cycle noise on new realisations** (`v10_levels.py`, 48 kHz, Decay 0.5 s; own
descriptors; period from the autocorrelation of the level per second):

| | reference, realisations 8 to 15 | model, seeds 40 to 79 | difference, standard errors |
|---|---|---|---|
| floor (5 % point), dB | -36.42 +- 0.40 | -36.57 +- 0.33 | -1.0 |
| top (95 % point), dB | -31.52 +- 0.15 | -31.60 +- 0.12 | -1.4 |
| swing, dB | 4.90 +- 0.39 | 4.98 +- 0.33 | +0.5 |
| mean power, dB | -33.47 +- 0.25 | -33.56 +- 0.25 | -1.0 |
| period, s | 17.49 +- 0.79 | 17.55 +- 0.84 | +0.2 |

Mean level curves of 40 seeds and all 16 realisations differ by 0.35 dB rms. MEASURED.

## 8. Start levels after a warm-up (claim 16)

`v10_levels.py`. 64 reference realisations (16 long, 48 of the first 3 s of the same stimulus)
against 400 model seeds and against the generator's own law at 11.6 s.

- Level in the first second of noise: section 2, row 16. The reference reaches -34.3 dB; ten
  realisations lie above -35.9 dB.
- Level of the phase 11.6 s after the start (59 tracks that null below -70 dB), as a place in the
  generator's range: 0.04 to 1.02 (left) and 0.04 to 0.99 (right); KS p against 10 000 generated
  instances 0.17 and 0.37; share in the outer fifths 0.39 and 0.17 against 0.35 and 0.27; correlation
  of the two outputs 0.50 against 0.38 in the generator. MEASURED.
- So start levels near the ends of the ranges do occur after a warm-up, at the generator's rate
  within what 64 instances can show (a deficit of a factor of two on the right output is not
  excluded). The tie of the two outputs is stronger than the generator's, as both phase
  verifications found. One left level reads 0.599, 0.008 above the published bound of 0.591.

## 9. The start of an instance (claim 13)

`v12_start.py`, `v15_start_short.py`. Pink noise; per 0.25 s window the null of the delivered model
with a free phase per window (left output; the right one reads alike).

| input starts | Decay | Macro | window centred at 0.125, 0.375, 0.625, 0.875 s | 1.125 | 1.375 | 1.625 | 2.125 | 2.625 | 3.125 | 3.625 s |
|---|---|---|---|---|---|---|---|---|---|---|
| with the instance | 0.5 s | 100 % | -7.5, -11.3, -12.6, -24.0 | -55.2 | -86.7 | -102.4 | -102.6 | -104.6 | -100.7 | |
| with the instance | 0.5 s | 50 % | -10.1, -11.1, -9.9, -23.8 | -55.3 | -81.5 | -96.1 | -104.4 | -106.5 | -105.7 | |
| with the instance | 2 s | 100 % | -1.5, -5.9, -10.0, -16.0 | -24.5 | -34.0 | -42.7 | -60.3 | -76.5 | -92.5 | -102.5 |
| with the instance | 2 s | 50 % | -5.0, -6.1, -6.5, -13.2 | -23.5 | -34.1 | -42.3 | -62.1 | -78.4 | -94.7 | -104.1 |
| with the instance | 2 s | 0.1 % | -8.7, -7.7, -7.9, -15.1 | -25.3 | -35.9 | -43.9 | -62.5 | -78.7 | -95.1 | -108.6 |
| 1 s after it | 0.5 s | 100 % | | -50.9 | -60.7 | -77.6 | -101.4 | -103.7 | -103.0 | -104.2 |
| 1 s after it | 0.5 s | 50 % | | -72.2 | -83.4 | -95.7 | -104.8 | -107.8 | -105.2 | -105.6 |
| 1 s after it | 0.5 s | 10 % | | -57.1 | -69.0 | -82.5 | -107.9 | -112.7 | -109.0 | -110.8 |
| 1 s after it | 0.5 s | 0.1 % | | -64.1 | -76.7 | -89.8 | -111.8 | -117.9 | -112.4 | -112.9 |

- **The first 0.75 s are not the model at any Macro value**, 50 % included: -1.5 to -13 dB, and
  -13 to -24 dB from 0.75 to 1 s. MEASURED. This is not the Tide layer: Macro 0.1 % shows it, and
  at Macro 0 two renders of a capture that starts with the instance differ, so `revocean.capture`
  rejects it.
- **What enters in that stretch rings on at the Decay rate.** With Decay 2 s the null improves by
  8.5 dB per 0.25 s (the reverberation falls by 7.5 dB) and passes -100 dB only at 3.5 s, alike at
  Macro 0.1, 50 and 100 %. MEASURED. "The first 3 s" is therefore not a constant: it grows with Decay
  when the input is present from the start.
- **From 1 s on a smaller, Macro-dependent settling remains**: -51 dB at Macro 100 %, -57 dB at 10 %,
  -64 dB at 0.1 % and -72 dB at 50 % in the first quarter second, falling by 10 to 13 dB per 0.25 s
  (amplitude time constant about 0.2 s) and at the floor from **2.1 s**. The fitted phase lies below
  the hold line at Macro 100 % (-5.2e-4 cycle at 1.1 s) and 50 % (-6.7e-5 and -1.6e-5), above it at
  10 % (+7.9e-5) and 0.1 %. MEASURED. This supports the author's reading that something tied to Macro
  starts near 50 % and is smoothed; the sign turns a little below 50 %. (The window from 1.75 to
  2.0 s reads -67 to -78 dB at Macro 100 and 50 % in both outputs, the right one worse: the signature
  of a comb tap change, not followed up.)
- In the captures that start 2.5 s after the instance the track is within 2.5e-6 cycle of the hold
  line at 2.9 s (20 clean outputs). MEASURED: by 2.9 s nothing is left.
- For validation: with a silent warm-up of 2.2 s or more none of this is seen. The README's 10 s is safe.

## 10. Regeneration, scores, holdout

- `fit_tide_model.py` run with its output redirected to the scratch folder (`v00_regenerate.py`):
  1095 s with six workers, no new capture, every entry of `tide_model.json` equal (assignment,
  cases, phase curves, rate, ablations, level start, level given phase, all three scores).
  CONFIRMED. The file is not byte-identical because the sections were first written in another order.
- `score_tide.py tide_model.py --null --case 0 --case 7` from the command line: -106.85 and -88.78 dB,
  as reported.
- Statistics score with other seeds: section 2, row 14. The spread ratio the author quotes (0.92 to
  1.07) excludes the noise descriptors; `timeVariance` reads 0.05 to 0.07 because its spread is
  floored at 0.02 in `descriptors.py`, not because the model is steadier.
- Holdout. No file of the packet and no script in its scratch folder calls a holdout loader or the
  default programme seed. Of 3660 cache entries nine hold a holdout stimulus, all at Macro 0 and all
  rendered on 6 October when the holdouts were set up; none exists above Macro 0. The author's one Macro 0 programme
  capture uses seed 8301. The rate law is fitted on the hold captures, which no score reads. CONFIRMED.
- By construction the phase curves stored for score 1 are fitted to the captures the score is taken
  on. That is what the score means (everything but the phase), not a leak; a reader should know it.

## 11. Sharper or corrected values

| item | packet | here |
|---|---|---|
| whole-response null at Macro 100 % | -88.8 to -106.8 dB | sparse material -89.6 to -104.2 dB; **steady noise -86.8 to -93.2 dB; 58 to 108 s after the start -79.9 dB**; with the comb's tap changes moved -92.8 to -98.4 dB |
| what limits the null | inferred: the comb's delay arithmetic, for trains | measured for steady material: tap changes one sample off, 13 listed; trains: not this |
| loop unchanged above Macro 0 | Decay up to 11 s, Size 62 to 181 % | Decay 0.5 to 30 s, Size 30 to 200 % |
| equaliser before comb | 1.5 to 4.1 dB | 1.2 to 6.1 dB, 8 of 8 |
| model range in Macro | 2 to 100 % | 1 to 100 %, and bit-exact at 0 |
| rate at Macro 100 % | 0.0564238 | 0.0564237 +- 0.0000003 (two independent sets) |
| shape of the rate law | 0.63, free 0.63032 | 0.630 +- 0.001, free 0.63010 on fourteen means |
| settling of a fresh instance | up to 3 s, as if Macro came from 50 % | gross mismatch for 0.75 to 1 s at every Macro value, ringing on at the Decay rate; Macro-dependent glide over at 2.1 s |
| start levels after a warm-up | OPEN, p = 0.08 | no deviation in 64 realisations (KS p 0.91) |
| free tap weights | within 1.3e-5 to 2.4e-4 | within 3.6e-6 to 4.9e-5 (12 outputs) |
| level bounds | +-0.004 | one reading 0.008 above the left upper bound |

## 12. Weak spots a product should know

1. **A null test against the reference on programme material will read -80 to -93 dB at Macro
   100 %, not -100 dB**, with single events 50 to 70 dB below the local signal that ring on with the
   reverberation. They come from the comb's tap changes (section 4) and are far below audibility. A
   test hook that accepts a list of moved tap changes, or the 13 of section 4, separates them from real
   errors. On the right input the comb's delay falls from 0 to 100 s after the start, on the left from
   50 to 150 s; the captures behind the author's nulls all end before 44 s, so they never saw the
   left input's.
2. **Size.** `tide_model.line_taps` uses `network_model.parameters`, whose rule
   `floor(P s + 0.5)` is refuted by `network_verification.md`. At Size 80.1 % and Macro 100 % the
   model nulls at -16.0 dB (generator form) and -20.3 dB (model-free), at Size 80.0 % at -96.8 and
   -101.1 dB. MEASURED. Whole-percent Sizes are not affected. The corrected rule is in that document.
3. **The first 2.2 s of an instance** are not the model, and input in the first second leaves a tail
   that lasts as long as the reverberation (section 9). Not to be copied; not to be compared.
4. **The generator form is good to a few 1e-6 cycle, not exact.** It costs up to 10 dB of null on a
   single output against a model-free phase. Nothing to hear; a candidate should be scored against
   the form's own null, as `score_tide.py` does.
5. **Fits of the generator form have local minima** where a small step follows a large one; this
   packet's first fit of one capture stopped at -85 dB where the right knots give -105 dB. A scorer
   that refits without a stored curve can fail a correct candidate. `score_tide.py` then also starts
   from `tide_phase.rate`, the refuted law.
6. `tide_phase.TidePhase.phase` and `TidePhaseStream` still carry the old rate law (the author's
   note); only `tide_model.generator` has the new one.
7. Inherited and unchanged: the network's per-pass lag (-83 dB after 100 s at Macro 0), the output
   floor near 1e-36, everything outside the neutral baseline, input peaks above 0.5.

## 13. Not established, and limits of this verification

- The arithmetic of the comb delay. The 13 tap changes are observations, not a rule.
- What keeps impulse trains at -89 to -96 dB.
- What is left at -92.8 dB (A8) and -98.4 dB (A5) after the moves; whether more tap changes are
  off than the search keeps (it needs signal in the comb and half of the local residual).
- What the first second of an instance is, and the law of the glide behind it. Not needed.
- A Macro that moves while playing; nobody has measured it.
- A round form for the two end values of the rate law.
- Voice law, block timing, Q smoothing and the half cycle were taken from `tide_stage.py` as verified
  by `tide_stage_verification.md`; the author's ablations for them were regenerated, not re-derived.
- The level cycle was compared on 17.5 s periods with 28 s (own stimulus) and 48 s (the author's) of
  noise; "given the phase the level is exact" rests on the nulls, the author's 0.05 dB figure was
  regenerated only.
- Own estimator: the model-free track loses the phase in single windows (six of the eight outputs
  at Macro 3.5 and 1 %, one window in A2, three in A7); the generator form was used there. Statistics of section 7 use eight
  reference realisations.
- Budget: 105 captures, 0.62 GB.

## 14. Reproduce

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
W=../../Results/RevOceanCharacterization/work/tide_model_verification      # results are written there
$PY $W/vstim.py                      # captures A, B, C, D, E, G (cached)
$PY $W/v00_regenerate.py             # the author's script, output compared entry by entry (18 minutes)
$PY $W/v14_form.py A1 A2 A3 A4 A5 A6 A7 A8 A9 A10 A11      # section 3
$PY $W/v01_nulls.py; $PY $W/v08_tapchanges.py A5 A11 A8 A6 A7 author:6 author:7 author:11 author:14   # section 4
$PY $W/v03_assignment.py A1 A9 A10 B35_5 B88_6 A2         # section 5
$PY $W/v02_rate.py; $PY $W/v02_rate_law.py                # section 6
$PY $W/v07_listen.py; $PY $W/v10_levels.py; $PY $W/v11_statistics_seeds.py     # sections 7 and 8
$PY $W/v12_start.py; $PY $W/v15_start_short.py            # section 9
$PY $W/v04_order.py A1 A10 B100_6 B88_5 B62_5 B35_5 B15_5 A2; $PY $W/v05_macro0.py; $PY $W/v09_size.py
```

`vcomb.c` is compiled once: `cc -O2 -ffp-contract=off -shared -fPIC -o $W/vcomb.dylib $W/vcomb.c`.
`v08_tapchanges.py` reads the phase written by `v01_nulls.py`. The free weights of A2 at the phase of
the generator form (`v03_A2_generator_form_weights.json`) come from `v03_assignment.free_weights`
called with the curve of `v14_A2.npz`. Results of a first pass with a weaker fit of the generator form
are kept in `first_pass/`.
