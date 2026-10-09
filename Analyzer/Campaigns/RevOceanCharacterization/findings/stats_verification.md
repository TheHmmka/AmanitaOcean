# Verification of packet `stats` (packet `stats_verification`)

An independent check of `findings/stats.md` and `tide_structural_data/targets.json`
with fresh captures and code that shares nothing with `descriptors.py`.

Tags: MEASURED (read off captures), FITTED (a constant, residual given),
INFERRED (reasoned, not tested), OPEN. Verdicts: CONFIRMED, PARTLY, REFUTED,
UNVERIFIED.

## Which state was checked (Tide 100 %, Mix 100 %)

- **Every capture of this check is at Mix 100 %.** All 38 read back "100", and
  in each of the 23 impulse renders the output is exactly 0.0 until the first
  reflection: there is no dry signal in the data. MEASURED.
- **Tide is the Macro knob** (the reference is in Tide mode, README item 1).
  21 of the 38 captures are at Macro 100 %, 6 at 50 %, 11 at Macro 0.
- The author's file says the same: Mix "100" in all 37 cases; 171, 324 and 480
  impulse responses at Macro 0, 50 and 100 % (recounted from the stimuli).
- What this means for the laws. The decay law, the loss table, the level law
  with G0 = 2.2 dB, the echo density line -13.5 + 2.171 Size and the output
  correlations were **established at Macro 0**. At Tide 100 % this check finds:
  the decay law holds unchanged (1.7 % rms), the level law does not carry over
  (another gain and another spectrum, section 6), the echo density line of
  Macro 100 % does not generalise (section 5), and the answers to the two
  inputs are not independent (section 4).

## Verdicts

Claims in the order of the author's report.

| # | Claim | Verdict | This check |
|---|---|---|---|
| 1 | All targets at Mix 100 %; Macro 0, 50, 100 % with 171, 324, 480 responses | CONFIRMED | read-backs and counts reproduced; no dry in the audio |
| 2 | 60/T30 = 60/Decay + loss(f) 100/Size | CONFIRMED from 250 Hz; PARTLY below | 8 points off the author's grid, Decay 0.75 to 32 s with Size 30 to 200 %: 1.56 % rms from 250 Hz, mid band within 0.5 %; 6.5 % from 63 to 200 Hz |
| 3 | Size exponent of the loss is 1 | CONFIRMED | free fit 0.985 (1.50 % from 5 kHz), exponent 1 gives 1.59 % |
| 4 | "Decay equals the decay time" is false | CONFIRMED | mid-band T30 / Decay from 0.98 to 0.36 at the fresh points; 13.04 s at Decay 32 s |
| 5 | loss(f): 2.2 dB/s at 2 kHz, 3.7 at 160-500 Hz, 13.5 at 8 kHz, 42.7 at 16 kHz | CONFIRMED from 315 Hz (within 4.3 %); corrected below | 63 Hz 3.80 (not 2.87), 125 Hz 3.95 (not 3.55), 250 Hz 3.95 (not 3.66); 16 kHz 43.9 with a steeper band filter |
| 6 | bandLevel = 2.2 + S(f) - 10 log10(Size/100) + 10 log10(T30/13.8); no make-up gain | PARTLY | G0 = 2.04 dB, sd 0.14 dB; 0.19 to 0.68 dB rms from 200 Hz to 6.3 kHz; S(f) fails below 200 Hz at sizes other than 100 % (1.8 to 4.3 dB rms); Macro 0 only |
| 7 | Opposite output -8.2 dB; input correlation 0.53, 0.26, 0.03; "independent reverberators" at Macro 100 % | numbers CONFIRMED; independence REFUTED | -8.3 dB; 0.53 to 0.57, 0.14 to 0.26, -0.01 to 0.02 by three other routes; at Macro 100 % the answers correlate 0.08 to 0.19 at a relative delay of 257 samples |
| 8 | Echo density reaches 0.9 at -13.5 + 2.171 Size ms, independent of Decay; Macro 100 %: 12.1 + 1.281 Size | PARTLY | Macro 0 line 7.6 ms rms off the grid (stated 2.2 ms), a proportional law does as well; Macro 100 % line 25 ms rms, +33 and +51 ms at Size 175 and 200 % |
| 9 | Outputs uncorrelated from 400 Hz, correlated below 200 Hz, less for long decays | CONFIRMED | band means within 0.07 from 400 Hz (0.15 at Decay 0.75 s); 63-125 Hz: 0.53 at Decay 3 s, 0.30 at 32 s |
| 10 | Macro changes the spectrum, not the decay time; T30 spread grows with decay length | CONFIRMED, three details corrected | +3.1 dB at 2 kHz, -6.2 dB at 8 kHz, -24.1 dB at 16 kHz; T30 within 3.4 % to 12.5 kHz; the +20 % at 16 kHz is +6 % here; "onset 16-25 ms later" is not a delay |
| 11 | The Tide is a slowly moving resonant low-pass outside the loop (INFERRED) | CONFIRMED, now MEASURED, and located; it is not the whole Tide | after the tank, one per output channel, the two moving independently; a downward sweep that restarts about every 8.6 s; two effects of the Macro cannot come from behind the tank |
| 12 | Macro 0: low bands 2 dB at 0.6 Hz; Macro 100 %: high bands 4.5 dB below 0.32 Hz; Macro 50 %: 2.4 dB "at slower rates" | totals CONFIRMED; "slower at 50 %" REFUTED | 1.95 / 0.78 / 0.29 dB and 2.67 / 1.86 / 4.39 dB; the rate is the same at 50 and 100 % (median 0.116 Hz); time variance UNVERIFIED |
| 13 | A realisation is another stretch of time; scoring separates Macro settings | PARTLY | true for decay, level and the output filter (between over expected 0.48 to 0.71 for T30, 0.67 to 1.49 for band level; sweep phase between instances 0.07); false for the first arrival, which is the same in every instance and drifts by 5.1 samples per second; modulation scores 9-24 UNVERIFIED |
| 14 | Impulse layouts of Decay 0.5, 8, 16 s cover a third of the 0.6 Hz cycle; low-band spreads several times too small | PARTLY; 8 and 16 s UNVERIFIED | resultant lengths reproduced; at Decay 0.5 s the target mean is 1.18 of its own spreads off the population below 1 kHz (0.24 above), but its spread is 0.69 of the population's, not several times too small |
| 15 | At Decay 0.5 s part of the spread is a function of the 0.6 Hz phase | CONFIRMED | share explained 1.00 at 63-125 Hz, median band 0.24, broadband level 0.05 (author: 0.13 to 0.39, 1.00, 0.03 to 0.05) |

The author's script reproduces his data file: `measure_targets.py`, run with
its two output paths redirected to scratch, wrote a `targets.json` that is
byte for byte the published one (sha256 `9a2f0e92...3ec0`, 53 s). MEASURED.

## Method

- **Captures.** 38 captures through `revocean.capture`, 2717 s, 1.04 GB of
  cache; none shares a stimulus with the author.
  - Impulses at eight points off his grid: (Decay s, Size %) = (0.75, 125),
    (1.5, 45), (3, 100), (4, 175), (6, 80), (12, 30), (12, 200), (32, 100) at
    Macro 0; six of them at Macro 100 % (2 or 3 realisations), (3, 100) at
    Macro 50 %. Impulses start 0.7 s in, have amplitude 0.5 and cycle through
    left, right, both in phase ("M") and both in antiphase ("S"). Their times
    are spread over the 0.6 Hz cycle (resultant length 0.04 to 0.23).
  - Periodic noise (period 8192 samples, another seed), 108 s, at Decay 3 s:
    four realisations at Macro 100 %, two at 50 %, one at Macro 0, one at
    Macro 100 %, Size 45 %.
  - A comb of mid-only, empty, side-only, empty noise bands, and 14 steady
    sines, each at Macro 0, 50, 100 %.
- **Code.** `own.py`: zero-phase band filters in the frequency domain
  (eighth-order Butterworth magnitude), backward integration with a tail from
  a line through the last third of the envelope, a second decay estimator that
  does not integrate, forward cumulative energy for the onset.
- **Calibration** on four of the author's captures (his stimuli rebuilt from
  the JSON): echo density time identical to 0.1 ms, onset within 0.13 ms,
  broadband T30 within 0.0001 s, band T30 within 0.8 % up to 6.3 kHz. From
  8 kHz up this code reads 0.1 to 3.5 % shorter (16 kHz, Decay 16 s: -3.5 %):
  INFERRED, the author's third-order band filter lets in more of the slower
  decay of the band below. On the fresh points the non-integrating estimator
  agrees with the integrated one within 1.4 % from 630 Hz to 10 kHz for Decay
  of 1.5 s and more, and within 2.8 % at Decay 0.75 s. MEASURED.

## 1. Decay law (claims 2 to 5)

T30 at Macro 0 against `60 / (60/Decay + loss(f) 100/Size)` with the author's
loss table. MEASURED; the law's constants are his.

| Decay, Size | responses | mid-band T30, s | law, s | T30 / Decay | rms error from 250 Hz | all 25 bands |
|---|---|---|---|---|---|---|
| 0.75 s, 125 % | 24 | 0.732 | 0.730 | 0.98 | 2.41 % | 4.89 % |
| 1.5 s, 45 % | 20 | 1.304 | 1.299 | 0.87 | 1.23 % | 2.15 % |
| 3 s, 100 % | 16 | 2.639 | 2.633 | 0.88 | 0.94 % | 1.26 % |
| 4 s, 175 % | 14 | 3.620 | 3.615 | 0.90 | 0.89 % | 1.30 % |
| 6 s, 80 % | 14 | 4.465 | 4.454 | 0.74 | 0.90 % | 3.09 % |
| 12 s, 30 % | 14 | 4.265 | 4.247 | 0.36 | 2.18 % | 4.91 % |
| 12 s, 200 % | 11 | 9.408 | 9.389 | 0.78 | 1.00 % | 1.89 % |
| 32 s, 100 % | 8 | 13.038 | 12.991 | 0.41 | 1.96 % | 5.26 % |

- Over the eight points: 1.56 % rms from 250 Hz, 3.48 % over all bands, 6.54 %
  from 63 to 200 Hz. The author's in-sample figures from 250 Hz were 1.05 %
  (Decay 2 to 16 s), 1.82 % (Decay 0.5 and 1 s) and 0.90 % (sizes).
- The law holds where it was never fitted: Decay and Size moved together, and
  Decay 32 s, twice his longest (14.69 s at 2 kHz; the law gives 14.80 s). The
  term 60/Decay is right within 0.5 % (mid band at Decay 0.75 s, where it is
  97 % of the rate).
- Size exponent, fitted over the eight points in the bands from 5 kHz: 0.985,
  error 1.50 %; with 1 the error is 1.59 %. The round value stands. FITTED.
- **Low bands.** `loss × Size / 100` at 63 Hz reads 3.75, 3.78, 3.84, 4.38 dB/s
  at the four points where the loss dominates; the author's 2.87 dB/s rests on
  his Decay 2 s, Size 100 % case, which he himself flagged as five standard
  errors off.
- **Systematic residue.** At (12 s, 30 %) and (32 s, 100 %) T30 from 3.15 to
  8 kHz is 1.5 to 2.8 % longer than the law, at 0.1 to 0.3 % standard error.
  INFERRED: where the loss dominates, the spread of rates inside one
  third-octave band is largest and the slower part of the band wins late in the
  decay. The law may be exact per frequency; this was not tested.

**Loss table refitted on the eight fresh points** (dB/s at Size 100 %, weighted
so that every T30 counts alike). FITTED:

| Hz | 63 | 80 | 100 | 125 | 160 | 200 | 250 | 315 | 400 | 500 | 630 | 800 | 1000 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| this check | 3.80 | 3.83 | 4.07 | 3.95 | 4.00 | 3.82 | 3.95 | 3.84 | 3.72 | 3.51 | 3.30 | 3.02 | 2.71 |
| author | 2.87 | 3.91 | 3.94 | 3.55 | 3.98 | 3.74 | 3.66 | 3.72 | 3.75 | 3.67 | 3.34 | 3.07 | 2.74 |

| Hz | 1250 | 1600 | 2000 | 2500 | 3150 | 4000 | 5000 | 6300 | 8000 | 10000 | 12500 | 16000 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| this check | 2.42 | 2.23 | 2.21 | 2.43 | 3.02 | 4.12 | 5.96 | 8.92 | 13.44 | 20.29 | 30.26 | 43.89 |
| author | 2.44 | 2.19 | 2.18 | 2.45 | 3.11 | 4.25 | 6.10 | 9.05 | 13.51 | 20.24 | 29.80 | 42.70 |

Rms T30 error from 250 Hz / all bands / 63 to 200 Hz:

| table | on the fresh points | on the author's ten Macro 0 cases |
|---|---|---|
| author | 1.56 / 3.48 / 6.54 % | 1.20 / 3.09 / 5.93 % (his fit) |
| this check | 1.06 / 2.23 / 4.14 % (its fit) | 1.18 / 2.78 / 5.26 % |

The refitted table is no worse on his data and better on the fresh data, by
its low bands. Below 250 Hz no table gets under 4 %: a third-octave band holds
few arrivals there, as the author says.

## 2. Level law (claim 6)

Macro 0, one driven input, both outputs. "Start level" is the mean over 500 Hz
to 2 kHz of bandLevel - 10 log10(T30/13.8) + 10 log10(Size/100); the law says
2.2 dB everywhere. MEASURED.

| Decay, Size | level, dB | start level, dB | residual 200 Hz-6.3 kHz, dB rms | 63-160 Hz, dB rms |
|---|---|---|---|---|
| 0.75 s, 125 % | -14.59 | 1.63 | 0.53 | 2.55 |
| 1.5 s, 45 % | -8.06 | 2.12 | 0.47 | 1.77 |
| 3 s, 100 % | -8.62 | 2.29 | 0.21 | 0.38 |
| 4 s, 175 % | -9.64 | 2.12 | 0.19 | 2.19 |
| 6 s, 80 % | -6.55 | 1.90 | 0.29 | 4.33 |
| 12 s, 30 % | -3.57 | 1.89 | 0.68 | 2.47 |
| 12 s, 200 % | -7.06 | 1.97 | 0.24 | 2.10 |
| 32 s, 100 % | -3.78 | 2.00 | 0.24 | 0.51 |

- G0 over the seven points with Decay of 1.5 s or more: **2.04 dB, sd 0.14 dB**
  (author 2.13, sd 0.16, quoted as 2.2). FITTED. The start level is 0.2 to
  0.3 dB lower from Decay 6 s up than at 1.5 to 4 s, in his table as in this
  one. No make-up gain against Decay: confirmed over Decay 1.5 to 32 s.
- The proportionality to 1/Size holds from Size 30 to 200 % combined with
  other Decay values (the start levels above already contain the Size term).
- **S(f) is not one function below 200 Hz.** At Size 100 % his S(f) fits
  (0.38 and 0.51 dB rms from 63 to 160 Hz); at the other sizes single bands
  miss by up to 7.1 dB (63 Hz at Size 80 %) and -6.0 dB (80 Hz). The low
  bands depend on Size in a way the law does not have. MEASURED.
- The law is a Macro 0 law. At Macro 100 % the start level is 3.45 dB (sd
  0.28 dB, five points) and the spectrum is that of section 6.

## 3. Outputs (claim 9)

Macro 0, MEASURED. Correlation of the two outputs, mean over the responses:

| Decay, Size | one input, 63-125 Hz | both inputs | antiphase inputs | largest band mean from 400 Hz |
|---|---|---|---|---|
| 0.75 s, 125 % | 0.57 | 0.79 | -0.84 | 0.146 |
| 1.5 s, 45 % | 0.58 | 0.73 | -0.70 | 0.033 |
| 3 s, 100 % | 0.53 | 0.73 | -0.71 | 0.068 |
| 4 s, 175 % | 0.45 | 0.62 | -0.74 | 0.055 |
| 6 s, 80 % | 0.45 | 0.62 | -0.60 | 0.031 |
| 12 s, 30 % | 0.22 | 0.55 | -0.42 | 0.055 |
| 12 s, 200 % | 0.36 | 0.48 | -0.57 | 0.053 |
| 32 s, 100 % | 0.30 | 0.41 | -0.41 | 0.061 |

Decay 3 s and 32 s at Size 100 % continue his series (0.63 at 2 s, 0.50 at 4 s,
0.35 at 16 s). New: the low-band correlation also depends on Size (0.22 at
Size 30 %), and with antiphase inputs the outputs are anticorrelated below
200 Hz by about the same amount.

## 4. Cross-feed and the two inputs (claim 7)

**Numbers confirmed, by two methods the author did not use.** MEASURED.

| | Macro 0 | Macro 50 % | Macro 100 % |
|---|---|---|---|
| author (level of "both" against one input) | 0.53 | 0.26 | 0.03 |
| in-phase against antiphase impulses, (E_M - E_S) / (E_M + E_S) | 0.531, sd 0.012 over 8 points | 0.137 (one point, 8 + 8 responses) | -0.013, sd 0.072 over 6 points |
| mid-only against side-only noise bands, same time, 200 Hz-12 kHz | 0.550 | 0.264 | 0.021 |
| exact, 980 pairs of the shared grid (Decay 0.5 s) | 0.570, sd 0.013 over impulse times | | |

- The correlation is flat in frequency at each Macro value (comb: 0.56, 0.58,
  0.54 at Macro 0 for 200-750 Hz, 0.75-3 kHz, 3-12 kHz; 0.00, 0.05, 0.02 at
  Macro 100 %).
- Opposite output against same side: -8.30 dB (-8.20 to -8.37) for Decay 1.5 s
  and more at Macro 0, -7.54 dB at Decay 0.75 s, -7.55 dB on the grid at 0.5 s;
  -8.19 dB at Macro 100 % (-7.6 to -8.8 per point).
- Driving both inputs adds 4.968 dB on the grid (author 4.95 dB).

**"Independent reverberators" does not hold.** At Macro 100 % the answers to
the two inputs are correlated at a **relative delay of 257 samples (5.35 ms)**.

- With R_M and R_S the mean autocorrelations of the responses to in-phase and
  antiphase impulses, D(k) = (R_M(k) - R_S(k)) / (R_M(0) + R_S(0)). D(0) is the
  correlation above. At Macro 0, D stays below 0.033 for every lag from 100 to
  700 samples and its largest value sits at another lag in each point. At Macro
  100 % the largest value is at 257 samples in five of six points: 0.040 to
  0.113, against 0.009 to 0.012 of scatter (4 to 13 times). The sixth point
  (Decay 0.75 s) reads 0.035 at 257, 2.3 times its scatter. The five points
  have Size 45 to 200 %: a lag that belonged to the reflection pattern would
  move with Size, this one does not.
- The same shows as a ripple of period 48000 / 257 = 186.8 Hz in
  (P_M - P_S) / (P_M + P_S): best delay 256.0 to 258.25 samples in all six
  points, amplitude **0.076 to 0.192** (mean 0.115), against 0.011 to 0.019 at
  other delays. At Macro 0 no delay stands out (0.015 to 0.032).
- The amplitude is the correlation coefficient at that delay: about 0.1 to 0.2,
  against 0.53 at zero delay for Macro 0. Macro 50 % has both: 0.14 to 0.26 at
  zero delay and 0.07 at 257 samples.
- Consequence. A mono source at Tide 100 % gets a comb of 186.8 Hz spacing,
  about ±0.5 dB for a coefficient of 0.115 (INFERRED from the coefficient); a
  model with two independent reverberators has none.
- The comb of this check cannot see the effect by an accident of design: its
  mid and side bands are 187.5 Hz wide or repeat every 187.5 Hz, one ripple
  period. Its zero-delay result is valid.
- **Agreement with packet `tide`**, whose findings appeared while this check
  ran and were not used for it. `findings/tide.md` fits a delay in front of the
  network per input, `D_L(T) = 469.34 + 256.90 tri(T / 200 s)` samples and
  `D_R(T) = D_L(T + 50 s)`. Two triangles a quarter period apart differ by
  exactly their swing, 256.9 samples, for half of every cycle and cross zero
  in between: the 257 samples found here are that swing. It also explains the
  size of the coefficient: the plateau lies at stimulus times 40 to 90 s, the
  46 s render (Decay 0.75 s) barely reaches it and shows no clean peak, and
  the coefficient grows with the length of the render (0.086 at 50 s, 0.118
  and 0.125 at 62 s, 0.192 at 97 s; 0.093 at 73 s does not follow). INFERRED
  from the two packets together: at any moment the two answers are correlated
  at the momentary difference of the two delays, which moves on a 200 s cycle.

## 5. Echo density and onset (claim 8)

MEASURED with the same definition as the author (20 ms Hann window, 10 ms
running mean, first time at 0.9).

| Macro 0: Decay, Size | reaches 0.9, ms | standard error | author's line | difference |
|---|---|---|---|---|
| 12 s, 30 % | 57.9 | 1.0 | 51.6 | +6.3 |
| 1.5 s, 45 % | 86.3 | 1.6 | 84.2 | +2.1 |
| 6 s, 80 % | 165.6 | 3.1 | 160.2 | +5.4 |
| 3 s, 100 % | 207.1 | 2.9 | 203.6 | +3.5 |
| 32 s, 100 % | 210.8 | 6.2 | 203.6 | +7.2 |
| 0.75 s, 125 % | 248.8 | 3.5 | 257.9 | -9.1 |
| 4 s, 175 % | 381.1 | 5.5 | 366.4 | +14.7 |
| 12 s, 200 % | 416.1 | 4.0 | 420.7 | -4.6 |

- The line `-13.5 + 2.171 Size` is 7.6 ms rms off these points; its stated
  residual of 2.2 ms is the in-sample residual of five points and two
  constants. Refitted here: `-8.4 + 2.153 Size`, residual 6.8 ms; the
  proportional law `2.091 Size`, residual 7.8 ms. The fresh data do not
  resolve the intercept; either form is good to about 8 ms. FITTED.
- Independence of Decay holds within that: 207.1 and 210.8 ms at Decay 3 and
  32 s, Size 100 %.
- **Macro 100 %.** His line `12.1 + 1.281 Size` gives 140.2, 69.7, 114.6,
  172.2, 236.3, 268.3 ms; measured 139.7, 68.7, 107.2, 181.7, 269.1, 319.5 ms
  (standard errors 5.6, 1.5, 6.4, 5.3, 10.4, 13.0). It is right up to Size
  125 % and **33 and 51 ms short at Size 175 % (Decay 4 s) and 200 % (Decay
  12 s)**, 3.2 and 3.9 standard errors; 25 ms rms in all. His own Size 200 %
  at Decay 2 s read 268 ms, so Decay, Size, the time since the start (his
  render lasts 37 s, this one 97 s), or several of them enter; OPEN. The ratio to
  Macro 0 (0.65 to 0.80 here, 0.64 to 0.77 in his data) is confirmed.
- **The onset law is a descriptor law only.** The -0.1 dB level time jumps from
  one early arrival to the next as Decay changes the share of the first
  arrival: its spread is 0.9 ms at (0.75 s, 125 %) and 11 ms at (12 s, 200 %).
  The line `3.89 + 0.2333 Size` is for Decay 2 s, as he says.
- **First arrival** (first sample above 1 % of the response's peak, median),
  which does not depend on Decay (25.98 and 25.95 ms at Decay 3 and 32 s).
  FITTED:
  - Macro 0: `2.41 ms + 0.2368 ms × Size[%]`, residual 0.13 ms.
  - Macro 100 %, median over the impulse times of each point:
    `14.08 ms + 0.2364 ms × Size[%]`, residual 0.56 ms. The same slope,
    11.7 ms later on average (10.7 to 12.5 ms per point).
  - Macro 50 %: 0.15 ms later, that is, not later.
- **The shift at Macro 100 % is not a constant.** Over the 112 left-only and
  right-only responses it runs from 275 to 731 samples (5.7 to 15.2 ms). It
  falls by 5.18 samples per second for the right input and rises by 5.04 for
  the left input during the first 38 s. It is the same in every instance: two
  realisations differ by 1.0 sample (median) at the same impulse. MEASURED.
  It follows the delay law of `findings/tide.md` (above) with a mean difference
  of +7.2 samples and 12.2 samples of scatter when T counts from the start of
  processing, that is, stimulus time plus the 10 s warm-up; read at stimulus
  time the scatter is 53.3 samples. Three responses were misread by the
  detector and left out. README item 10 (526 samples) and `io.md` (522) are
  single readings of this drift.
- So "onset 16-25 ms later" (14.0 to 23.1 ms here) must not be read as a fixed
  pre-delay. The first arrival moves by 5.7 to 15.2 ms depending on the time
  since the start and on the input, and only at Macro 100 %; the rest is early
  energy that is weaker. His onset targets at Macro 100 % mix this drift with
  the descriptor's own dependence on Decay.

## 6. What Macro changes (claim 10)

Same stimuli at Macro 0 and Macro 100 %, six points, MEASURED. Mean band level
difference in dB (sd over the points), with the author's mean of ten cases:

| Hz | 63 | 80 | 125 | 250 | 500 | 1000 | 1600 | 2000 | 4000 | 8000 | 12500 | 16000 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| this check | -1.85 (0.65) | -3.32 (0.66) | -0.88 (0.46) | -1.05 (0.55) | -0.77 (0.21) | 0.93 (0.37) | 3.15 (0.92) | 3.13 (0.84) | -0.39 (0.34) | -6.18 (0.56) | -15.22 (0.76) | -24.05 (0.74) |
| author | -1.29 | -4.08 | -0.05 | -1.05 | -0.43 | 0.90 | 2.98 | 3.14 | -0.16 | -6.57 | -15.50 | -24.22 |

- Spectrum confirmed. Macro 50 % at (3 s, 100 %): +4.08 dB at 6.3 kHz,
  -15.0 dB at 16 kHz, level +1.05 dB (author +4.3, -16.4, +1.04).
- **Low bands.** The loss is broad: -0.8 to -3.3 dB in every band from 63 to
  500 Hz, 1.6 dB on average from 63 to 400 Hz. "80 Hz is 4.1 dB lower while
  125 Hz is unchanged" is too narrow a reading: 125 Hz is 0.88 dB lower here
  (standard error 0.19 dB).
- **The level against Macro 0 depends on Decay**, which his summary (one value,
  Decay 2 s) hides:

  | Decay | 0.5 s | 0.75 s | 1 s | 1.5 s | 2 s | 3 s | 4 s | 6 s | 8 s | 12 s | 16 s |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | this check, dB | | -2.20 | | -0.65 | | -0.43 | -0.66 | -0.08 | | -0.15 | |
  | his `targets.json`, dB | -1.91 | | -1.21 | | -0.77 | | -0.40 | | -0.26 | | +0.67 |

  Most of this is the same spectrum change acting on another spectrum: the
  share of the Macro 0 energy that lies from 5 kHz up is 0.67 at Decay 0.75 s
  and 0.49 at Decay 6 s, and that is the part the Tide removes. Applying the
  mean band differences of the five points with Decay of 1.5 s or more to each
  point's own Macro 0 band energies gives the measured level minus 0.63 to
  0.92 dB at those five points (a constant, because means of dB were used) and
  minus 0.05 dB at Decay 0.75 s. MEASURED.
- **What is left at short decay**: at Decay 0.75 s the bands from 315 Hz to
  3.15 kHz are 1 dB lower against Macro 0 than at the longer decays (+0.05 dB
  on average against +0.75 to +1.18 dB; +1.5 dB at 2 kHz against +3.1 to
  +3.9 dB). One point; INFERRED: the early part of the response is weaker at
  Macro 100 % and a short decay has more of its energy there.
- **Decay time unchanged.** Mean T30 ratio 0.978 to 1.034 in every band up to
  12.5 kHz. His Macro 0 loss table predicts the Macro 100 % T30 from 250 Hz to
  10 kHz within 1.74 % rms (0.7 to 2.6 % per point; he found 2.62 %), and the
  Macro 50 % T30 within 0.57 %. Broadband T30 is 4 to 6 % longer at Macro 100 %.
- **16 kHz.** The ratio is 1.057 here, not 1.20, and the excess rate at Macro
  100 % is 38 to 42 dB/s against 44 dB/s at Macro 0 (five points with Decay of
  1.5 s or more). The band is 24 dB down under a steep spectrum, so the band
  filter decides the reading. INFERRED: the "Macro 100" row of his loss table
  at 12.5 and 16 kHz (27.0 and 30.6 dB/s) describes his filter, not the tank.
- T30 spread at 2 kHz, Macro 100 %: 4.6, 8.2, 12.8, 12.8, 16.5, 18.0 % for
  Decay 0.75, 1.5, 3, 4, 6, 12 s (0.5 to 1.8 % at Macro 0). His trend holds.
- Macro 0 against the Macro 100 % ensemble, in spreads of the latter: band
  level 1.5 to 2.05 rms, T30 0.16 to 0.57 (he: 2.1 and 0.73).

## 7. The Tide filter (claims 11 and 12)

### It sits after the tank, one per output channel. MEASURED

- **Absolute-time test.** For each response at Macro 100 % the tilt (2.5-5 kHz
  against 630 Hz-1.25 kHz) relative to the Macro 0 response to the same impulse
  was read early (0.1 to 0.6 s) and late (the last half second before the next
  impulse), per output:

  | Decay, Size | pairs | late of k with early of k+1 (0.6 s apart) | early with late of the same response (3 to 8 s apart) |
  |---|---|---|---|
  | 12 s, 200 % | 40 | +0.76 | +0.17 |
  | 6 s, 80 % | 52 | +0.58 | -0.36 |
  | 4 s, 175 % | 52 | +0.74 | -0.26 |
  | 3 s, 100 % | 90 | +0.58 | -0.19 |

  The tilt (sd 3.2 to 3.8 dB) belongs to the time at which the sound leaves,
  not to the impulse that caused it. A filter in front of the tank would give
  the opposite pattern.
- Inside one decay of 8 s (Decay 12 s, first response of realisation 11; a
  one-off reading, not in `verification.json`) the resonance travels from 2 kHz
  to 1 kHz in the first 3 s and then starts again near 5 kHz; at the same
  moments the other output shows another position.
- The two outputs move independently: correlation of the slow level movement
  from 2.5 to 16 kHz between left and right -0.06 (Macro 100 %), 0.11 (50 %);
  of the peak frequency -0.23 to +0.27 per realisation.
- This also settles the author's reading of the T30 spread: it grows with decay
  length because the filter moves under the decay.

### What moves, and how. MEASURED (periodic noise, Decay 3 s)

- Against the Macro 0 spectrum the output shows one resonance followed by a
  roll-off. Its frequency (sixth-octave resolution, half-second means):

  | | 5 % | 25 % | median | 75 % | 95 % | height, median (5 to 95 %) |
  |---|---|---|---|---|---|---|
  | Macro 100 % | 1.0 kHz | 1.6 kHz | 2.5 kHz | 3.5 kHz | 4.5 kHz | 6.7 dB (2.9 to 9.7) |
  | Macro 50 % | 5.0 kHz | 5.6 kHz | 7.1 kHz | 7.9 kHz | 10 kHz | 6.4 dB (2.8 to 8.0) |

- **It is a downward sweep that starts again**: 66 % of the half-second steps
  go down, 4 % up (Macro 100 %); from about 5 kHz to about 1 kHz, then back to
  the top, with the old position fading while the new one comes in. In
  one-second spectra (a one-off reading) the roll-off above the resonance
  stops at a floor between -14 and -2 dB that rises as the sweep nears its
  end. OPEN: the filter's order and Q, and the shape of the cross-fade.
- **Rate.** Half of the level movement from 2.5 to 16 kHz lies between 0.077
  and 0.154 Hz, median 0.116 Hz (a period of 8.6 s) at Macro 100 %; between
  0.096 and 0.135 Hz, median 0.116 Hz at Macro 50 %. The sweep is not periodic:
  successive restarts in one channel were 9.7, 7.7 and 12.8 s apart, and a
  sweep can stall for seconds (one-off readings of the peak track).
  `findings/tide.md` describes two voices half a cycle apart on a cycle of
  about 18 s (0.0553 cycles per second), that is, a restart every 9.0 s; the
  median rate found here corresponds to 8.6 s.
- **The rate does not change with Macro; the range does.** His "at slower rates"
  for Macro 50 % came from 17.7 s segments, whose resolution is about 0.11 Hz.
- Bands are not moved together. Adjacent bands correlate 0.82, 3.15 kHz against
  10 kHz -0.97; a band moves by 4.3 dB while the mean of the nine bands from
  2.5 to 16 kHz moves by 1.4 dB. His `fluctuation` target takes each band
  alone and does not test this.
- Size does not change it (Size 45 %: same percentiles, fluctuation 4.29 dB).
- **No frequency spreading.** The Macro leaves the reference's frequency spread
  untouched: 90 % of the energy of a steady sine stays within 7.2 / 7.15 /
  7.05 Hz at 1 kHz and 48.6 / 47.95 / 48.85 Hz at 8 kHz for Macro 0 / 50 / 100 %
  (0.5 to 0.7 % of the frequency at every Macro value), and the empty
  bands of the comb fill to -13.5 dB (0.75-3 kHz) and -10.8 dB (3-12 kHz) at
  all three. The Tide is a slow gain-and-filter process, not a pitch or delay
  modulation. The frequency spread is a property of the tank on which a
  candidate can be checked above 1 kHz, where his `timeVariance` is at its
  ceiling.

### What the moving low-pass does not explain

Claim 11 reads as if the moving filter were the Tide. Two effects of the Macro
cannot come from any element behind the tank. INFERRED from this check alone:
a second element in front of or inside the tank. `findings/tide.md` names it,
a comb delay per input that the Macro fades in.

- The answers to the two inputs lose their zero-delay correlation (0.53 to
  0.02) and gain one at 257 samples (section 4). Whatever sits behind the tank
  treats both answers alike, and the correlation is flat in frequency, so a
  filter or a delay there cannot change it.
- README item 9: the Macro 100 % output is not a slowly varying filter of the
  Macro 0 output (short-time coherence below 0.2). With only a slow filter
  behind the tank it would be one. `stats.md` does not address this.

Both are already present at Macro 50 % (input correlation 0.26, README's
coherence of 0.3 to 0.5).

Four more effects are not part of a moving low-pass, wherever they arise:

- the first arrival 5.7 to 15.2 ms later, at Macro 100 % only (section 5);
- 1.6 dB less from 63 to 400 Hz, far below the sweep's range (section 6);
- 1 dB less from 315 Hz to 3.15 kHz at Decay 0.75 s (section 6);
- the echo density building up in 0.65 to 0.80 of the Macro 0 time, which the
  author already notes a ringing filter could also produce.

### Fluctuation totals (claim 12). MEASURED

Standard deviation of the band level in windows of one noise period, inside
17.7 s segments as he defines it; 63-250 Hz / 315 Hz-2 kHz / 2.5-16 kHz:

| | this check (Decay 3 s) | author (Decay 2 s) |
|---|---|---|
| Macro 0 | 1.95 / 0.78 / 0.29 dB | 1.94 / 0.77 / 0.28 dB |
| Macro 50 % | 2.70 / 1.43 / 2.36 dB | 2.59 / 1.52 / 2.43 dB |
| Macro 100 % | 2.67 / 1.86 / 4.39 dB | 2.65 / 1.93 / 4.48 dB |
| all bands together, Macro 0 / 50 / 100 % | 0.09 / 1.16 / 0.64 dB | 0.09 / 1.31 / 0.74 dB |

By rate, 2.5 to 16 kHz, rms dB per octave over the whole 104 s:

| octave from, Hz | 0.01 | 0.02 | 0.04 | 0.08 | 0.16 | 0.32 | 0.64 |
|---|---|---|---|---|---|---|---|
| Macro 100 %, this check | 0.65 | 1.11 | 1.92 | 3.16 | 2.05 | 0.80 | 0.30 |
| Macro 100 %, author (17.7 s) | | | 1.83 | 2.67 | 2.81 | 0.88 | 0.27 |
| Macro 50 %, this check | 0.33 | 0.49 | 0.99 | 2.16 | 0.97 | 0.32 | 0.17 |
| Macro 50 %, author | | | 1.51 | 1.53 | 1.08 | 0.31 | 0.18 |

- "Below 0.32 Hz" holds: 96 % of the variance at Macro 100 %. The split between
  his octaves is set by his segment length: 0.08-0.16 Hz is the strongest
  octave at both Macro values, and 8 % of the variance lies below 0.04 Hz,
  which 17.7 s cannot see. His `modulationRates` targets are still fair for a
  candidate analysed the same way.
- Macro 0: 1.77 dB of the low-band movement is in the 0.32-0.64 Hz octave.
- Time variance was not recomputed. UNVERIFIED.

## 8. Realisations and start-up (claim 13)

- Spread of the per-realisation means over what the scatter of the responses
  predicts, Macro 100 %: 0.48 to 0.71 for T30, 0.67 to 1.49 (mean 0.93) for the
  band level of one input; Macro 50 %: 0.63 and 0.81. He found 0.44 to 1.08.
  MEASURED.
- **Not everything is redrawn.** The delay in front of the network is the same
  in every instance and depends on the time since the start (section 5). For
  whatever depends on it (first arrival, onset times, the relation of the two
  inputs) a realisation is a repeat, not another stretch of time, and a
  target taken over the first 28 to 80 s of a render samples part of a 200 s
  cycle. A candidate rendered on the same stimuli is scored fairly only if its
  delay follows the same clock. MEASURED for the first arrival; INFERRED for
  the targets.
- The output filter's sweep is drawn anew per instance: the peak frequency
  correlates 0.07 between realisations (standard error 0.04 over 12 pairs that
  are not independent), the slow level movement 0.10 from 2.5 to 16 kHz. Four
  instances 0.8 s after the start had the resonance at 2.5, 5.0, 1.26 and
  5.0 kHz (one-off reading of the peak track). `findings/tide.md` gives the
  sweep a fixed start phase and mean rate with a wander of 0.16 cycles per
  instance. INFERRED: with a restart every half cycle that puts two instances
  0.45 restart periods apart (sd), which is compatible with the small
  correlation found here; the two packets do not contradict each other, but
  this check cannot confirm the fixed start phase.
- What is common to all instances is the low end: the slow level movement from
  63 to 250 Hz correlates 0.76 between realisations (0.30 from 315 Hz to
  2 kHz). The 0.6 Hz modulation is the same in every instance, as he says.
- **No start-up effect in level.** The first response of a render (impulse 0.7 s after
  the start) against the later ones of the same input: -0.09 dB, standard
  error 0.29 dB over 13 renders at Macro 100 %; +0.02 dB, 0.05 dB at Macro 0.
  Steady noise at Macro 100 %: the first 20 s are 0.04 dB below the rest.
- `io.md` reports a 4 dB arch of the wet level over 18 s at Macro 100 %, Decay
  0.5 s, common to eight instances. Nothing of the kind appears at Decay 3 s:
  in 2 s windows the level scatters by 0.38 dB inside a realisation and the
  four realisations correlate 0.16. Whether the arch is tied to Decay 0.5 s
  was not tested. OPEN.
- INFERRED: his floor of 0.3 to 0.5 spreads for another draw is the sampling
  expectation sqrt(1/n_candidate + 1/n_target), not a property of the
  reference. Here realisation 11 against 12 and 13 scores 0.22 on T30 with
  0.31 expected (MEASURED, one case). A candidate that renders fewer responses
  scores higher for that reason alone.
  The scores of 9 to 24 on the modulation descriptors were not recomputed
  (UNVERIFIED); the level differences behind them are confirmed (0.29 against
  4.39 dB).

## 9. Impulse phases and the population (claims 14 and 15)

- Resultant lengths of his impulse phases, recomputed from the stimuli in
  `targets.json`: 0.84, 0.84, 0.90 per input at Decay 0.5 s; 0.89 at 8 s; 0.76
  at 16 s; 0.04 to 0.28 at 1, 2, 4 s. As stated. MEASURED.
- Shared grid, 980 responses, Decay 0.5 s, band level of both outputs: spread
  0.26 dB (8 kHz) to 2.56 dB (80 Hz); share explained by three harmonics of the
  0.6 Hz phase 1.00 from 63 to 125 Hz, 0.92 at 315 Hz, 0.49 at 500 Hz, below
  0.2 from 1.6 kHz, median band 0.24. As stated.
- His Decay 0.5 s target (left input, 8 responses) against that population: off
  by 1.18 of its own spreads rms below 1 kHz (2.3 at 63 Hz) and 0.24 from 1 kHz;
  its spread is 0.69 of the population's below 1 kHz and 0.95 above (medians
  over the bands). The limit is real. At Decay 0.5 s it is a mean that sits on
  one part of the cycle rather than a spread that is several times too small.
- Decay 8 and 16 s were not rendered again; that part rests on his second draw.
  UNVERIFIED.

## Limits of this check

- Six Macro 100 % points with 22 to 48 responses each and one Macro 50 % point:
  band values at Macro 100 % are known to about 0.3 to 0.9 dB per point. The
  Macro 50 % input correlation from impulses (0.137) rests on 16 responses; the
  comb value (0.264) is the better one.
- The 257-sample relation was found, not predicted. This check did not follow
  the relative delay of the two inputs in time; the picture of a 200 s cycle
  rests on `findings/tide.md`, whose right-input law was measured over 58 s.
  The right-input shift measured here falls steadily to 78 s of stimulus time.
- The sweep was measured at Decay 3 s only, with noise. Its range and rate
  under programme material, any dependence on input level, and the law of the
  range against Macro between 50 and 100 % are OPEN. `io.md` puts the 11 ms of
  extra delay between Macro 75 and 100 %.
- T30 above 10 kHz depends on the band filter by 1 to 3.5 % at Macro 0 and more
  at Macro 100 %; neither this code nor the author's is the truth there.
- Only 48 kHz, neutral Brightness, Width 100 %, no pre-delay, impulses of 0.5.
- Not re-derived: EDT and T20 targets, the sixth-octave spectrum targets, the
  coherence, the time variance, the scores of `compare_to_targets` (the rerun
  reproduces them; they were not recomputed by other code).

## Reproduction

Scratch: `Analyzer/Results/RevOceanCharacterization/work/stats_verification/`.

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
W=../../Results/RevOceanCharacterization/work/stats_verification
cd Analyzer/Campaigns/RevOceanCharacterization
$PY $W/plan.py                # 38 captures (cached)
$PY $W/rows.py                # per-response tables, rows_<case>.npz
$PY $W/verify.py              # verification.json: the numbers of the tables and lists
$PY $W/reproduce_author.py    # the author's script, outputs redirected to scratch
$PY $W/own.py                 # self-test of the descriptor code on synthetic decays
```

| File | Content |
|---|---|
| `plan.py` | stimuli, settings and realisations of the fresh captures |
| `own.py` | the independent descriptor code |
| `rows.py`, `rows_*.npz` | per-response quantities of the 15 impulse cases |
| `verify.py`, `verification.json` | the checks and their results (70 kB) |
| `reproduce_author.py`, `reproduce_author.json`, `author_rerun/` | the rerun of `measure_targets.py` |
| `levels_*.npy` | band level series left over from exploration; `verify.py` does not read them |

This packet owns no file under `tide_structural_data/`; its numbers are in
`verification.json` in the scratch folder.
