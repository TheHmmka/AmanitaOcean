# Listening review with numbers: Ebb against Rev OCEAN at Macro 100 %, Mix 100 %

Packet `review_listening` (7 October 2026). Read-only for the source tree. Judged: the built, not
installed VST3 of `build-ebb` (Amanita Ocean 0.21.0, binary SHA-256
`e614cbff50624deeaac7a971a697a732a40d0684a157e0b2f12c6969f2d302c7`, Character Ebb) against Arturia
Rev OCEAN 1.0.0.5848 in Tide mode, the way the owner will compare them: Evolution = Macro = 100 %,
Mix 100 %, everything else neutral.

Nothing was listened to by ear and no host was opened. Every statement below is a measurement on
renders made through the Analyzer. Scripts, logs and stored results:
`Analyzer/Results/RevOceanCharacterization/work/review_listening/` (section 12).

## 1. Verdict

- **At the A/B state I found no difference a listener could hear that is not also there between
  two instances of the reference.** Over 16 cases (four Decay and Size settings, three host rates,
  six Macro steps, instance ages up to 165 s, up to 805 descriptors per case) the engine's voice
  seeds cannot be told from the reference's realisations: label-permutation p between 0.20 and
  1.00 in every case (section 3.1).
- **The nearest thing to a systematic difference is in the decay time**: pooled over nine cases
  the engine's T30 at 125 to 500 Hz is 0.50 % longer than the reference's (3.5 se); a test built
  for it reads +0.5 ± 0.5 %. That is a tenth of what can be heard (section 3.4).
- **At Evolution 0 the plug-in is the reference**: -110.67 dB on a 58 s programme of my own,
  nothing fitted (section 5).
- **One pass of the plug-in is as far from one pass of the reference as two passes of the
  reference are from each other**: above 2 kHz the level of a channel differs by 2.70 dB rms
  moment by moment (reference against itself: 2.77 dB), up to 7.7 dB at single moments in both
  comparisons (section 3.9). The swells never coincide in time. This is what an A/B will show
  first, and it is not a model error.
- **What a listener can hear** (section 9): Ocean's default positions of Low Cut, High Damping
  and Focus (they shorten and thin the tail far beyond the reference's own spread); the fixed
  voice seed (Ebb repeats itself, and two instances swell in unison); the first half second of a
  fresh instance.
- **Limits of this review**: the plug-in has one voice seed and the driver cannot set another, so
  seeds were varied in the engine's renderer, which returns the plug-in's samples bit for bit
  (section 2). Input peaks were kept at or below 0.5, so the level stage was never working. Moving
  controls, Freeze and instance ages beyond 165 s were not compared (section 11).

## 2. What was compared, and how

**Three sources of audio**, all at 48 kHz, block 512, unless stated:

- `ref`: the reference through `revocean.capture`, fresh realisations (213 captures, none from
  the campaign's cache or holdouts);
- `plug`: the built VST3 through `render_candidate.capture`, all 14 host parameters set, Ocean's
  own controls read back as `20`, `20000`, `0.0`, `0.0`, `Off`;
- `eng`: `build-ebb/AmanitaOceanEbbRender` with the plug-in's outer chain (level stage, Width,
  Mix, clipper) and voice seeds 1, 2, ...

**Why the renderer stands in for seeds.** `render_candidate.py` has `VOICE_SEEDS = (0,)`: the
plug-in exposes no seed. In every case I rendered the plug-in and the renderer from the plug-in's
own seed and counted differing samples: **0 of 109,596,000 samples in 16 renders** (all cases
with a warm-up). The one exception is the case without warm-up: 18,661 of 768,000 samples differ,
all inside the first 200 ms (section 6). So a seed in the renderer is what that seed would be in
the plug-in.

**Stimuli** (my own, deterministic, stereo, peak exactly 0.5; `rl_stimuli.py`):

- *programme*: an impulse on both channels, one on the left, one on the right, a 1 s pink noise
  burst (channel correlation 0.5), six plucked tones, a 6 s six-note sawtooth chord, four bars of
  a drum-like pattern; each followed by a gap of 1.2 x Decay for its tail (36 to 102.5 s);
- *steady*: 105 s of pink noise, left and right independent (and once white noise);
- *sweep programme* for the Macro steps: impulse, burst, 24 s of noise, chord (58 s);
- *start*: noise that is already running on the first frame of an instance, no warm-up;
- *probe*: an impulse every 0.5 s for 40 s, no warm-up, Decay 0.5 s.

**Cases** (Decay s / Size %; realisations of the reference / engine seeds; plug-in once each):

| Case | Setting | Macro | Warm-up | ref | eng |
|---|---|---|---|---|---|
| `prog_A`, `prog_B`, `prog_C`, `prog_D` | 5 / 100, 1.5 / 60, 2.5 / 150, 10 / 180.1 | 100 % | 10 s | 10, 6, 6, 6 | 12 each |
| `steady_A`, `steady_B`, `white_A` | 5 / 100, 1.5 / 60, 5 / 100 | 100 % | 10 s | 10, 6, 6 | 16, 12, 16 |
| `late_A` | 5 / 100 | 100 % | 60 s | 16 | 48 |
| `rate441_A`, `rate96_A` (44.1 and 96 kHz host) | 5 / 100 | 100 % | 10 s | 6, 6 | 12, 12 |
| `sweep_M0` ... `sweep_M100` | 5 / 100 | 0, 25, 50, 75, 90, 100 % | 10 s | 1, then 6 each | 12 each |
| `start_A` | 5 / 100 | 100 % | 0 | 10 | 12 |
| probe | 0.5 / 100 | 100 % | 0 | 24 | 240 |
| decay test | 5 / 100, 10 / 100, 10 / 180.1, 20 / 100 | 100 % | 10 to 31 s | 16 each | 48 each |

**Descriptors** are my own code (`rl_analysis.py`, checked on synthetic signals with known
answers in `t01_selftest.py`), not the campaign's `descriptors.py`: third-octave energy spectra
by FFT, Schroeder decay per octave band (EDT, T20, T30), onset and early energy of impulse
responses, a 4 ms early pattern, kurtosis as texture, channel correlation and side/mid broadband
and per octave, level curves and their swells, and the modulation spectrum of octave-band levels.

**Units.** For each descriptor the reference's realisations give a mean and a standard deviation
(*sd*: the reference's own spread between realisations). A difference is given

- in the descriptor's own unit (dB, %, ms, s),
- *in sd*: divided by the reference's spread,
- *in se*: divided by the standard error of the difference of the two ensemble means.

With 6 to 16 realisations the sd itself is known to about ±20 to 30 %. Descriptors that are the
same in every realisation of the reference (for example the time of the first arrival) have no
sd; for them only the unit is given. "Audible" is judged against common thresholds: 0.5 to 1 dB
of level, about 1 dB in a third-octave band, 5 % of a decay time, 0.05 to 0.08 of correlation,
1 ms of onset.

## 3. The owner's point: Decay 5 s, Size 100 %, Macro 100 %, Mix 100 %

### 3.1 Can the engine's ensemble be told from the reference's?

For each case: t = difference of the ensemble means in se, for every descriptor; then the rms of
t, the share beyond 2 and the largest. The null distribution comes from 2000 random
reassignments of the pooled renders to "reference" and "engine" (this keeps the correlation
between descriptors). `t11_permutation.py`, `permutation.txt`.

| Case | descriptors | ref / eng | rms t (null median) | share beyond 2 (null) | largest (null) | p of rms t | plug-in's seed among seeds: rms z |
|---|---|---|---|---|---|---|---|
| `prog_A` | 798 | 10 / 12 | 1.17 (1.04) | 7.6 % (5.5 %) | 3.89 (3.48) | 0.20 | 1.08 |
| `prog_B` | 805 | 6 / 12 | 1.12 (1.08) | 8.8 % (6.5 %) | 3.44 (3.81) | 0.40 | 0.95 |
| `prog_C` | 788 | 6 / 12 | 0.68 (1.08) | 0.4 % (6.6 %) | 2.31 (3.76) | 1.00 | 1.01 |
| `prog_D` | 783 | 6 / 12 | 1.18 (1.09) | 10.6 % (6.6 %) | 3.67 (3.93) | 0.28 | 1.20 |
| `steady_A` | 159 | 10 / 16 | 1.07 (1.02) | 5.0 % (5.0 %) | 2.99 (3.01) | 0.39 | 1.04 |
| `steady_B` | 159 | 6 / 12 | 0.80 (1.08) | 1.9 % (6.3 %) | 2.41 (3.39) | 0.98 | 1.15 |
| `white_A` | 159 | 6 / 16 | 0.98 (1.07) | 3.8 % (6.3 %) | 3.38 (3.33) | 0.70 | 1.07 |
| `late_A` | 159 | 16 / 48 | 0.87 (1.00) | 1.3 % (4.4 %) | 2.31 (2.88) | 0.82 | 1.14 |
| `rate441_A` | 159 | 6 / 12 | 0.91 (1.05) | 3.1 % (5.7 %) | 2.81 (3.22) | 0.80 | 1.05 |
| `rate96_A` | 157 | 6 / 12 | 0.76 (1.03) | 0.6 % (5.1 %) | 2.05 (3.17) | 0.97 | 1.06 |
| `sweep_M25` | 508 | 6 / 12 | 0.78 (1.06) | 2.0 % (5.5 %) | 2.59 (3.47) | 0.93 | 1.16 |
| `sweep_M50` | 507 | 6 / 12 | 1.07 (1.07) | 4.7 % (6.1 %) | 3.11 (3.51) | 0.50 | 1.04 |
| `sweep_M75` | 508 | 6 / 12 | 0.92 (1.06) | 2.2 % (6.1 %) | 2.53 (3.49) | 0.78 | 1.01 |
| `sweep_M90` | 508 | 6 / 12 | 1.07 (1.06) | 5.9 % (6.3 %) | 3.94 (3.61) | 0.49 | 1.02 |
| `sweep_M100` | 505 | 6 / 12 | 1.23 (1.08) | 9.5 % (6.5 %) | 4.68 (3.68) | 0.22 | 1.04 |
| `start_A`, windows from 0.5 s on | 25 | 10 / 12 | 1.00 (0.92) | 0 % (0 %) | 1.71 (1.87) | 0.42 | see 6 |

The last column places the plug-in's own render among the engine seeds; an engine seed placed
among the other seeds reads 0.98 to 1.27 (median per case). The plug-in's seed is an ordinary
one.

**A result that did not hold.** With the first 6 realisations and 12 seeds `late_A` read rms t
1.67, p 0.004: the engine about 0.1 dB lower in most bands below 1 kHz. With 16 realisations and
48 seeds it reads 0.87, p 0.82, and no third-octave band differs by more than 0.11 dB (0.04 dB
rms). The first six realisations happened to lie unusually close together (sd of the level
0.015 dB; 0.028 dB with 16). What remains at that age is a spread of the overall level that is smaller in the reference
(0.028 dB, 16 realisations) than in the engine (0.047 dB, 120 seeds), hundredths of a decibel.

### 3.2 Long-term spectrum (steady pink noise, 97 s after the build-up)

| Third octave | reference mean ± sd, dB | engine seeds mean ± sd | engine − reference, dB | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| 40 Hz | -41.27 ± 0.14 | -41.29 ± 0.10 | -0.02 | -0.17 | -0.5 | -41.36 | -0.64 |
| 63 Hz | -44.58 ± 0.14 | -44.60 ± 0.11 | -0.02 | -0.16 | -0.4 | -44.49 | +0.66 |
| 125 Hz | -39.47 ± 0.08 | -39.50 ± 0.10 | -0.02 | -0.27 | -0.6 | -39.36 | +1.53 |
| 250 Hz | -40.35 ± 0.08 | -40.36 ± 0.08 | -0.01 | -0.18 | -0.4 | -40.31 | +0.50 |
| 500 Hz | -39.87 ± 0.07 | -39.90 ± 0.09 | -0.04 | -0.52 | -1.2 | -39.84 | +0.37 |
| 1 kHz | -38.70 ± 0.05 | -38.69 ± 0.07 | +0.00 | +0.06 | +0.1 | -38.61 | +1.61 |
| 2 kHz | -35.44 ± 0.13 | -35.57 ± 0.22 | -0.14 | -1.06 | -2.0 | -35.78 | -2.66 |
| 4 kHz | -39.17 ± 0.22 | -39.23 ± 0.23 | -0.05 | -0.25 | -0.6 | -38.90 | +1.26 |
| 8 kHz | -47.82 ± 0.22 | -47.66 ± 0.28 | +0.16 | +0.73 | +1.6 | -47.61 | +0.97 |
| 12.5 kHz | -59.69 ± 0.24 | -59.68 ± 0.24 | +0.00 | +0.01 | 0.0 | -59.99 | -1.26 |
| 16 kHz | -70.30 ± 0.15 | -70.32 ± 0.19 | -0.02 | -0.12 | -0.3 | -70.55 | -1.61 |
| whole level | -25.39 ± 0.05 | -25.42 ± 0.07 | -0.04 | -0.81 | -1.7 | -25.34 | +0.91 |

Over the 27 bands from 40 Hz to 16 kHz: engine − reference 0.064 dB rms, largest +0.16 dB
(0.46 sd rms); plug-in's one render 0.19 dB rms, largest -0.35 dB (1.36 sd rms). With 120 engine
seeds the level is -25.402 ± 0.046 dB against the reference's -25.387 ± 0.047 dB: **-0.015 dB,
1.0 se**. The plug-in's own seed lies at the 92nd percentile of those seeds (+0.06 dB).
Nothing here is audible; no level trim is needed for an A/B.

### 3.3 The level cycle (swells)

Under steady pink noise the level of one output swings by 2.3 dB, and by 6.4 dB above 2 kHz: the
cycle is mostly a cycle of brightness. Swells above 2 kHz come every 8.4 s per channel (half a
voice cycle), with a spread of 2.5 s, and the two channels move independently.

| Descriptor | reference mean ± sd | engine seeds mean ± sd | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| level swing (95th − 5th percentile, 1 s windows), left, dB | 2.259 ± 0.068 | 2.319 ± 0.067 | +0.059 | +0.87 | +2.2 | 2.366 | +1.57 |
| the same, right | 2.390 ± 0.111 | 2.408 ± 0.078 | +0.018 | +0.16 | +0.4 | 2.381 | -0.08 |
| the same, both channels together | 1.765 ± 0.122 | 1.720 ± 0.179 | -0.045 | -0.37 | -0.8 | 1.781 | +0.13 |
| swing above 2 kHz, left, dB | 6.393 ± 0.132 | 6.347 ± 0.121 | -0.045 | -0.34 | -0.9 | 6.330 | -0.47 |
| swing above 2 kHz, right, dB | 6.414 ± 0.237 | 6.406 ± 0.152 | -0.008 | -0.03 | -0.1 | 6.404 | -0.04 |
| floor above 2 kHz, left, dB | -35.283 ± 0.091 | -35.262 ± 0.073 | +0.021 | +0.23 | +0.6 | -35.200 | +0.91 |
| top above 2 kHz, left, dB | -28.890 ± 0.073 | -28.914 ± 0.102 | -0.025 | -0.33 | -0.7 | -28.870 | +0.27 |
| spacing of swells above 2 kHz, s | 8.443 ± 0.320 | 8.544 ± 0.369 | +0.101 | +0.32 | +0.7 | 8.430 | -0.04 |
| spread of that spacing, s | 2.532 ± 0.316 | 2.391 ± 0.364 | -0.141 | -0.44 | -1.0 | 2.264 | -0.85 |
| swells above 2 kHz per 100 s and channel | 11.49 ± 0.48 | 11.23 ± 0.56 | -0.27 | -0.56 | -1.3 | 11.39 | -0.22 |
| period of the level by autocorrelation, s | 16.85 ± 2.04 | 17.92 ± 4.33 | +1.08 | +0.53 | +0.9 | 19.63 | +1.37 |
| correlation of the two channels' level curves above 2 kHz | -0.064 ± 0.158 | -0.111 ± 0.119 | -0.047 | -0.30 | -0.8 | -0.216 | -0.97 |
| left minus right level above 2 kHz, sd in dB | 2.992 ± 0.235 | 3.090 ± 0.181 | +0.097 | +0.41 | +1.1 | 3.212 | +0.93 |
| left minus right level, sd in dB | 1.064 ± 0.107 | 1.127 ± 0.092 | +0.063 | +0.59 | +1.5 | 1.199 | +1.26 |

The period by autocorrelation is a weak estimator on 97 s (it picks the half or the whole voice
cycle); the spacing of swells is the sharper one. The ensemble mean of the level curve by
instance age agrees as well: the two mean curves differ by 0.24 and 0.25 dB rms (left, right),
1.00 and 0.95 in se (`t04_level_curves.py`). Nothing audible.

### 3.4 Decay time per band (tail of the noise burst)

A Decay of 5 s measures as a T30 of about 3.9 s on a pink burst, in the reference and in Ebb.

| Band | reference T30 mean ± sd, s | engine seeds | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| 125 Hz | 3.499 ± 0.109 | 3.584 ± 0.096 | +0.085 (+2.4 %) | +0.78 | +1.9 | 3.597 | +0.90 |
| 250 Hz | 3.760 ± 0.127 | 3.849 ± 0.104 | +0.089 (+2.4 %) | +0.70 | +1.8 | 3.863 | +0.81 |
| 500 Hz | 3.706 ± 0.120 | 3.800 ± 0.103 | +0.095 (+2.6 %) | +0.79 | +2.0 | 3.806 | +0.84 |
| 1 kHz | 4.114 ± 0.106 | 4.079 ± 0.080 | -0.035 (-0.9 %) | -0.33 | -0.9 | 4.000 | -1.08 |
| 2 kHz | 4.185 ± 0.424 | 4.313 ± 0.250 | +0.127 (+3.0 %) | +0.30 | +0.8 | 4.430 | +0.58 |
| 4 kHz | 3.647 ± 0.412 | 3.885 ± 0.321 | +0.238 (+6.5 %) | +0.58 | +1.5 | 3.679 | +0.08 |
| 8 kHz | 2.696 ± 0.334 | 2.681 ± 0.178 | -0.015 (-0.6 %) | -0.05 | -0.1 | 2.719 | +0.07 |
| all | 3.918 ± 0.094 | 3.983 ± 0.040 | +0.065 (+1.7 %) | +0.70 | +2.1 | 4.008 | +0.96 |
| EDT, all | 3.994 ± 0.080 | 3.955 ± 0.135 | -0.039 | -0.49 | -0.8 | 4.037 | +0.54 |

Over all 63 T30 values of the programme (seven sounds, eight bands and broadband) the engine's
mean is 2.5 % rms from the reference's (0.43 sd rms, 1.02 se rms; the reference's own spread is
3.7 %).

**Is the engine's tail systematically longer?** The table shows +2.4 % at 125 to 500 Hz, about
2 se each. I followed it up in two ways.

- *All sounds, all cases* (`t17_decay_bias.py`): per render the mean of ln T30 over every sound
  of the case. Engine − reference at 125 to 500 Hz: +0.14, +0.49, +0.05, +1.66 % in `prog_A` to
  `prog_D` and +0.49, +0.87, +1.15, +0.51, +0.28 % at Macro 25 to 100 %: nine of nine positive,
  pooled **+0.50 % (3.5 se)**. At 1 to 2 kHz +0.42 % (2.1 se), at 4 to 8 kHz +0.22 % (0.8 se),
  broadband +0.50 % (2.7 se). The largest single case is `prog_D` (Decay 10 s, Size 180.1 %):
  +1.98 % broadband (3.4 se).
- *A test built for it* (`t18_decay_test.py`): one burst and its tail, 16 realisations and
  48 seeds per setting, spread evenly over eight warm-ups from 10 to 31 s so that both ensembles
  cover the same instance ages.

| Setting | reference T30 broadband, s | engine − reference: 125 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 8 kHz | broadband |
|---|---|---|---|---|---|---|---|---|---|
| 5 s, 100 % | 4.004 | +1.1 % (1.3 se) | +0.3 % (0.4) | +0.5 % (0.6) | +0.4 % (0.4) | -7.3 % (-2.8) | +5.8 % (1.9) | +0.5 % (0.2) | -1.8 % (-2.6) |
| 10 s, 100 % | 6.759 | -0.3 % (-0.2) | -0.2 % (-0.2) | -0.2 % (-0.1) | +0.5 % (0.5) | -4.4 % (-1.0) | -1.6 % (-0.5) | +2.6 % (0.6) | -1.5 % (-1.0) |
| 10 s, 180.1 % | 7.682 | +0.5 % (0.4) | +0.9 % (0.7) | +0.7 % (0.7) | -0.1 % (-0.1) | +2.6 % (0.6) | +1.4 % (0.3) | -6.1 % (-1.7) | +1.0 % (0.7) |
| 20 s, 100 % | 10.239 | +0.5 % (0.5) | +0.5 % (0.5) | +0.4 % (0.4) | -0.9 % (-0.7) | +4.5 % (0.9) | +3.3 % (1.0) | -5.7 % (-1.7) | +1.3 % (0.6) |

The test neither confirms nor excludes the half percent: at 125 to 500 Hz it reads +0.5 ± 0.5 %
over the four settings, with one setting negative, and broadband the signs are mixed (the
-1.8 % at 5 s is the opposite of `prog_A`'s +1.7 %). No band has a sign that repeats across the
settings, and the two readings beyond 2.5 se, both at 5 s, are contradicted by `prog_A` (2 kHz:
-7.3 % here, +3.0 % there; broadband: -1.8 % here, +1.7 % there). One pair agrees: 4 kHz at 5 s, 100 % reads +5.8 % here and +6.5 % in `prog_A`
(1.9 and 1.5 se); the other settings read -1.6, +1.4 and +3.3 % there.

So: **if the engine's tail is longer at all, it is by about half a percent below 1 kHz**, a tenth
of the 5 % a listener needs, and the 4 kHz reading at this one setting is open at about 6 % with
a reference that itself spreads by 11 % there. What these tests can resolve of a mean decay time
is about 1 % below 2 kHz and 3 to 5 % at 2 to 8 kHz, where the voices sweep through the tail.

### 3.5 Onset and early pattern (impulse on both channels, instance age 10.5 s)

| Descriptor | reference mean ± sd | engine seeds | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| first sound (-60 dB re the early peak), ms | 38.27 ± 0.02 | 38.28 ± 0.08 | +0.01 | +0.51 | +0.5 | 38.25 | -1.00 |
| onset (-20 dB re the early peak), ms | 48.57 ± 12.86 | 46.77 ± 12.11 | -1.81 | -0.14 | -0.3 | 41.42 | -0.56 |
| early peak, ms | 67.93 ± 1.19 | 67.97 ± 1.19 | +0.03 | +0.03 | +0.1 | 67.02 | -0.77 |
| centre time, ms | 315.9 ± 10.1 | 314.7 ± 13.3 | -1.2 | -0.12 | -0.2 | 301.5 | -1.42 |
| energy 0 to 40 ms, dB re the whole response | -31.90 ± 5.23 | -31.50 ± 8.86 | +0.40 | +0.08 | +0.1 | -29.37 | +0.48 |
| 40 to 80 ms | -9.06 ± 0.94 | -8.95 ± 0.92 | +0.11 | +0.12 | +0.3 | -7.88 | +1.27 |
| 80 to 160 ms | -6.23 ± 0.17 | -6.22 ± 0.14 | +0.01 | +0.07 | +0.2 | -6.45 | -1.31 |
| 160 to 320 ms | -5.57 ± 0.13 | -5.60 ± 0.12 | -0.03 | -0.19 | -0.5 | -5.71 | -1.05 |
| 320 to 640 ms | -6.14 ± 0.19 | -6.18 ± 0.22 | -0.05 | -0.24 | -0.5 | -6.21 | -0.40 |
| energy of the response, dB | -10.17 ± 0.27 | -10.38 ± 0.42 | -0.21 | -0.80 | -1.5 | -9.78 | +1.48 |
| kurtosis 50 to 150 ms | 45.4 ± 13.6 | 45.1 ± 10.5 | -0.3 | -0.02 | -0.1 | 26.8 | -1.37 |
| kurtosis 300 to 600 ms | 2.93 ± 0.04 | 2.95 ± 0.03 | +0.02 | +0.61 | +1.5 | 2.94 | +0.27 |

The arrivals come at the same times (the first sound of the left-only and right-only impulses:
37.49 against 37.47 ms and 39.21 against 39.22 ms). How loud the first arrivals are depends on
where the voices stand at that moment, which is why the first 40 ms spread by 5 dB in the
reference itself. The early pattern in 4 ms bins (180 values) reads 0.52 sd rms, 1.25 se rms.

The largest single reading of the whole review sits here: after the right-only impulse (age
22.5 s) the left/right energy balance was -7.41 ± 0.67 dB in the reference and -8.49 ± 0.63 dB
in the engine (-1.09 dB, -1.62 sd, -3.9 se), and the energy of the first 40 ms was 4.1 dB lower
(-3.4 sd). I followed it up twice and it did not hold: at Decay 10 s, Size 180.1 % the same
balance reads +1.34 dB the other way (+2.9 se), and a probe built for the question finds no
difference at any age from 0.75 to 40 s (section 8). With about 800 descriptors per case one
reading at 3.9 se is expected by chance (the permutation p of the largest t in `prog_A` is 0.27).

### 3.6 Stereo

| Descriptor | reference mean ± sd | engine seeds | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| steady noise: channel correlation | 0.073 ± 0.006 | 0.072 ± 0.005 | -0.001 | -0.12 | -0.3 | 0.072 | -0.20 |
| at 125 Hz | 0.159 ± 0.014 | 0.158 ± 0.012 | -0.000 | -0.01 | 0.0 | 0.159 | +0.02 |
| at 1 kHz | 0.006 ± 0.001 | 0.006 ± 0.001 | +0.000 | +0.03 | +0.1 | 0.005 | -1.18 |
| at 8 kHz | 0.000 ± 0.001 | -0.001 ± 0.001 | -0.001 | -0.58 | -1.5 | -0.000 | -0.37 |
| steady noise: side/mid, dB | -0.637 ± 0.052 | -0.631 ± 0.043 | +0.006 | +0.12 | +0.3 | -0.627 | +0.20 |
| steady noise: left/right balance, dB | 0.056 ± 0.072 | 0.017 ± 0.127 | -0.039 | -0.55 | -1.0 | 0.099 | +0.59 |
| burst: correlation | 0.157 ± 0.034 | 0.158 ± 0.029 | +0.001 | +0.04 | +0.1 | 0.174 | +0.49 |
| chord: correlation | -0.053 ± 0.007 | -0.055 ± 0.009 | -0.002 | -0.31 | -0.6 | -0.065 | -1.74 |
| chord: side/mid, dB | 0.456 ± 0.062 | 0.475 ± 0.079 | +0.019 | +0.30 | +0.6 | 0.563 | +1.73 |
| drums: correlation | 0.084 ± 0.015 | 0.071 ± 0.013 | -0.013 | -0.83 | -2.1 | 0.065 | -1.25 |
| drums: side/mid, dB | -0.724 ± 0.133 | -0.614 ± 0.113 | +0.110 | +0.83 | +2.1 | -0.556 | +1.27 |

All differences are a few thousandths of correlation or about 0.1 dB of side/mid. Not audible.

### 3.7 Modulation of band levels (steady noise, level of an octave band in 43 ms frames)

| Octave | descriptor | reference mean ± sd | engine seeds | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|---|
| 125 Hz | depth, dB sd | 2.462 ± 0.031 | 2.452 ± 0.026 | -0.010 | -0.32 | -0.9 | 2.491 | +0.93 |
| 125 Hz | of it slower than 0.15 Hz | 0.827 ± 0.132 | 0.811 ± 0.122 | -0.015 | -0.11 | -0.3 | 0.898 | +0.54 |
| 1 kHz | depth, dB sd | 1.099 ± 0.027 | 1.106 ± 0.021 | +0.007 | +0.25 | +0.7 | 1.084 | -0.55 |
| 1 kHz | slower than 0.15 Hz | 0.405 ± 0.069 | 0.407 ± 0.050 | +0.002 | +0.03 | +0.1 | 0.373 | -0.47 |
| 1 kHz | mean rate, Hz | 5.91 ± 0.30 | 5.89 ± 0.30 | -0.02 | -0.07 | -0.2 | 6.31 | +1.34 |
| 4 kHz | depth, dB sd | 2.374 ± 0.210 | 2.231 ± 0.196 | -0.143 | -0.68 | -1.7 | 1.923 | -2.15 |
| 4 kHz | slower than 0.15 Hz | 1.830 ± 0.382 | 1.754 ± 0.274 | -0.076 | -0.20 | -0.5 | 1.542 | -0.75 |
| 4 kHz | mean rate, Hz | 0.642 ± 0.176 | 0.694 ± 0.149 | +0.052 | +0.29 | +0.8 | 0.775 | +0.75 |
| 8 kHz | depth, dB sd | 3.110 ± 0.284 | 2.930 ± 0.286 | -0.179 | -0.63 | -1.6 | 2.465 | -2.27 |
| 8 kHz | slower than 0.15 Hz | 2.511 ± 0.473 | 2.353 ± 0.330 | -0.158 | -0.33 | -0.9 | 2.120 | -0.83 |
| 8 kHz | 0.15 to 1 Hz | 1.451 ± 0.319 | 1.342 ± 0.251 | -0.109 | -0.34 | -0.9 | 1.585 | +0.42 |
| 8 kHz | mean rate, Hz | 0.367 ± 0.115 | 0.379 ± 0.075 | +0.012 | +0.11 | +0.3 | 0.399 | +0.28 |
| 16 kHz | depth, dB sd | 3.540 ± 0.272 | 3.352 ± 0.354 | -0.189 | -0.69 | -1.5 | 2.871 | -2.46 |
| 16 kHz | slower than 0.15 Hz | 2.816 ± 0.561 | 2.695 ± 0.347 | -0.122 | -0.22 | -0.6 | 2.425 | -0.70 |
| 16 kHz | 0.15 to 1 Hz | 1.898 ± 0.383 | 1.626 ± 0.221 | -0.271 | -0.71 | -2.0 | 1.994 | +0.25 |

Over the 45 depth values (nine octaves, total and four ranges of rate): 0.073 dB rms, largest
-0.27 dB, 0.53 sd rms, 1.22 se rms. The engine's mean depth above 4 kHz is 0.14 to 0.19 dB under
the reference's (1.5 to 1.7 se, not significant); the plug-in's own seed moves 0.45 to 0.67 dB
less there over these 97 s (2.2 to 2.5 sd of the reference; 1.4 to 1.6 sd among the engine's
own seeds, so about one seed in fifteen moves as little). A depth of 2.5 against 3.1 dB sd in one
pass is at the edge of what can be heard.

### 3.8 The sounds of the programme

| Descriptor | reference mean ± sd | engine seeds | engine − reference | in sd | in se | plug-in | plug-in in sd |
|---|---|---|---|---|---|---|---|
| burst: wet level while it sounds, dB | -23.79 ± 0.40 | -24.03 ± 0.40 | -0.24 | -0.61 | -1.4 | -24.22 | -1.08 |
| plucks: level while they sound | -27.91 ± 0.48 | -27.94 ± 0.35 | -0.02 | -0.04 | -0.1 | -27.45 | +0.95 |
| plucks: level in the second after | -33.97 ± 0.69 | -33.90 ± 0.42 | +0.07 | +0.09 | +0.3 | -34.39 | -0.61 |
| chord: level while it sounds | -21.99 ± 0.49 | -22.05 ± 0.52 | -0.06 | -0.12 | -0.3 | -21.98 | +0.01 |
| chord: level in the second after | -29.18 ± 0.55 | -29.30 ± 0.49 | -0.12 | -0.21 | -0.5 | -27.95 | +2.22 |
| chord: sd of the 50 ms level, dB | 1.066 ± 0.184 | 1.044 ± 0.150 | -0.022 | -0.12 | -0.3 | 1.052 | -0.08 |
| chord: spectral centroid, octaves re 1 kHz | -1.372 ± 0.086 | -1.392 ± 0.065 | -0.020 | -0.23 | -0.6 | -1.500 | -1.48 |
| drums: level while they sound | -32.93 ± 0.24 | -32.76 ± 0.21 | +0.17 | +0.71 | +1.8 | -32.62 | +1.26 |
| drums: level in the second after | -43.11 ± 0.81 | -43.28 ± 0.92 | -0.17 | -0.21 | -0.5 | -43.45 | -0.42 |
| drums: sd of the 50 ms level, dB | 2.982 ± 0.065 | 2.968 ± 0.077 | -0.014 | -0.22 | -0.5 | 2.947 | -0.54 |
| drums: crest factor, dB | 14.71 ± 0.69 | 15.18 ± 0.57 | +0.47 | +0.67 | +1.7 | 15.28 | +0.81 |

Third-octave spectra of the seven sounds (189 values, 40 Hz to 16 kHz): engine − reference
0.73 dB rms against a reference sd of 0.69 dB (0.61 sd rms, 1.24 se rms, 8 % beyond 2 se). The
plug-in's one render: 1.57 dB rms, 1.28 sd rms. Its largest departures are in the drum segment
(instance age 56 to 70 s): -2.9 dB at 10 kHz (-4.7 sd; outside all 10 realisations and all 12
seeds), -1.7 dB at 8 kHz, +1.4 to +1.7 dB at 3 to 4 kHz. That is this seed at that moment, not
the engine (the seeds' mean there is -0.12 dB from the reference's).

### 3.9 What one A/B pass shows

How far one render is from another at the same instance age (`t13_single_pass.py`):

| Measure | two instances of the reference | plug-in and a reference instance | an engine seed and a reference instance |
|---|---|---|---|
| steady noise, level per channel in 0.5 s windows: rms difference over 97 s | 0.95 dB (0.68 to 1.05) | 0.93 dB (0.86 to 1.06) | 0.94 dB |
| largest momentary difference | 2.7 dB (up to 3.2) | 2.8 dB (up to 3.1) | 2.7 dB |
| the same above 2 kHz: rms | 2.77 dB (2.27 to 3.04) | 2.70 dB (2.49 to 2.90) | 2.79 dB |
| largest momentary difference above 2 kHz | 7.2 dB (up to 7.7) | 7.2 dB (up to 7.7) | 7.2 dB |
| burst, third-octave spectrum, rms over bands | 2.51 dB (0.49 to 3.91) | 2.25 dB (1.26 to 3.48) | 2.47 dB |
| plucks | 2.21 dB (0.60 to 4.10) | 1.97 dB (0.94 to 2.45) | 2.08 dB |
| chord | 1.34 dB (0.51 to 2.50) | 1.31 dB (0.20 to 2.32) | 1.23 dB |
| drums | 0.74 dB (0.34 to 1.46) | 1.17 dB (0.62 to 1.56) | 0.76 dB |
| waveform correlation, steady noise | 0.705 (0.656 to 0.737) | not measured | 0.713 (0.653 to 0.795) |

The last row is the strongest single statement of the review: sample for sample, an engine
render correlates with a reference render as two reference renders correlate with each other
(two engine seeds: 0.724). The same holds at every Macro step (section 5).

## 4. Other Decay and Size settings, host rates, instance age

Engine seeds against reference realisations, by family (rms of the difference of the ensemble
means; own unit / in sd / in se):

| Family | `prog_B` 1.5 s, 60 % | `prog_C` 2.5 s, 150 % | `prog_D` 10 s, 180.1 % |
|---|---|---|---|
| third-octave spectra (189), dB | 1.11 / 0.72 / 1.18 | 0.47 / 0.41 / 0.72 | 0.87 / 0.72 / 1.04 |
| levels (26), dB | 0.52 / 0.76 / 1.18 | 0.18 / 0.44 / 0.69 | 0.33 / 0.51 / 0.95 |
| EDT (63), % | 2.4 / 0.78 / 1.17 | 3.2 / 0.43 / 0.74 | 5.1 / 0.65 / 1.00 |
| T20 (63), % | 1.8 / 0.74 / 1.12 | 2.4 / 0.41 / 0.77 | 4.8 / 0.59 / 0.99 |
| T30 (63), % | 1.5 / 0.62 / 1.10 | 2.0 / 0.32 / 0.65 | 5.0 / 0.67 / 1.16 |
| channel correlation (34) | 0.041 / 0.52 / 0.96 | 0.014 / 0.52 / 0.81 | 0.018 / 1.02 / 1.41 |
| side/mid and balance (14), dB | 0.34 / 0.65 / 1.04 | 0.09 / 0.33 / 0.51 | 0.57 / 1.06 / 1.72 |
| early energy windows (18), dB | 2.36 / 0.74 / 1.24 | 0.70 / 0.12 / 0.24 | 1.65 / 0.80 / 1.24 |
| early pattern (180), share per 4 ms | 0.0034 / 0.89 / 1.15 | 0.0009 / 0.29 / 0.47 | 0.0084 / 1.56 / 1.53 |
| envelope of chord and drums (6), dB | 0.07 / 0.24 / 0.50 | 0.14 / 0.57 / 1.00 | 0.03 / 0.25 / 0.45 |

T30 of the burst, broadband: 1.347 ± 0.013 against 1.340 ± 0.008 s (B), 2.271 ± 0.014 against
2.268 ± 0.020 s (C), 7.618 ± 0.176 against 7.637 ± 0.204 s (D); the plug-in reads 1.338, 2.238
and 7.896 s. At 125 to 500 Hz the engine's T30 after the burst is +0.9 to +1.5 % in B (up to
2.9 se at 125 Hz), -0.3 to -0.7 % in C and +1.9 to +2.2 % in D (1.0 se); section 3.4 follows this
up. The two higher readings of `prog_D` (side/mid 1.72 se rms, early pattern 1.53 se rms) do not make the case
significant as a whole (p 0.28) and have no counterpart at the other settings. Size 180.1 % is
one of the five knob positions where two of the 32 lines are a sample off the reference's
(integration report); statistically it reads like the others.

- **44.1 and 96 kHz hosts** (steady noise, 6 realisations, 12 seeds each): rms t 0.91 and 0.76,
  p 0.80 and 0.97. Largest third-octave difference 0.17 and 0.28 dB.
- **Instance age**: 18 to 115 s (`steady_A`) and 68 to 165 s (`late_A`) both agree (p 0.39 and
  0.82). 165 s is the oldest instance the Analyzer can render (60 s of warm-up plus 110 s).
  Nothing is known about either plug-in beyond that age, where the comb's 200 s triangle and the
  phase generator are extrapolations.
- **White noise** (`white_A`): rms t 0.98, p 0.70; swell spacing above 2 kHz 8.54 ± 0.28 s
  against 8.61 ± 0.39 s.

## 5. Macro (Evolution) at 0, 25, 50, 75, 90, 100 %

**Macro 0** is repeatable, so the plug-in is nulled against the reference with nothing fitted
(`t08_macro0.py`, 58 s sweep programme, Decay typed as 5.00 s and Size as 100 % in both):

- whole response **-110.67 dB** (impulse -99.98, burst -110.55, steady noise -109.55, chord
  -119.53 dB); at the positions where both work with the same single-precision numbers -110.68 dB;
- Mix 35 %: -117.88 dB for the output, -110.67 dB for the wet part alone;
- Pre-delay 20 ms on both (Ocean's default): -110.54 dB, and -117.64 dB at Mix 35 %; both delay
  by 959 frames;
- largest difference in any of my descriptors: 0.001 dB.

**Above Macro 0** (6 realisations, 12 seeds per step; `t12_macro_table.py`). Reference mean ± sd /
engine mean ± sd / difference in sd; the plug-in's value follows in brackets:

| Descriptor | 25 % | 50 % | 75 % | 90 % | 100 % |
|---|---|---|---|---|---|
| wet level under noise, dB (Macro 0: -24.127 in both) | -24.98 ± 0.22 / -24.91 ± 0.23 / +0.30 (-24.80) | -24.99 ± 0.17 / -24.98 ± 0.20 / +0.06 (-24.95) | -25.20 ± 0.08 / -25.27 ± 0.19 / -0.83 (-25.34) | -25.14 ± 0.18 / -25.18 ± 0.19 / -0.23 (-25.25) | -25.03 ± 0.13 / -25.03 ± 0.18 / +0.02 (-25.08) |
| 8 kHz octave level, dB (Macro 0: -35.960) | -32.50 ± 0.26 / -32.49 ± 0.33 / +0.03 (-32.19) | -32.31 ± 0.28 / -32.38 ± 0.29 / -0.24 (-32.27) | -37.41 ± 0.48 / -37.70 ± 0.79 / -0.60 (-37.54) | -39.66 ± 0.51 / -39.82 ± 0.69 / -0.30 (-39.04) | -41.40 ± 0.45 / -40.88 ± 0.72 / +1.16 (-39.78) |
| 1 kHz octave level, dB (Macro 0: -33.024) | -34.27 ± 0.26 / -34.20 ± 0.24 / +0.28 | -33.84 ± 0.16 / -33.85 ± 0.19 / -0.05 | -33.26 ± 0.09 / -33.34 ± 0.19 / -0.78 | -32.64 ± 0.17 / -32.69 ± 0.18 / -0.28 | -31.75 ± 0.07 / -31.67 ± 0.10 / +1.05 |
| 8 kHz level modulation depth, dB sd (Macro 0: 0.319) | 0.99 ± 0.12 / 1.01 ± 0.23 / +0.15 (0.42) | 1.24 ± 0.30 / 1.37 ± 0.32 / +0.43 (0.89) | 2.58 ± 0.39 / 2.48 ± 0.54 / -0.25 (1.23) | 2.46 ± 0.76 / 2.74 ± 0.61 / +0.37 (1.33) | 3.56 ± 0.87 / 2.87 ± 0.77 / -0.78 (1.38) |
| channel correlation under noise (Macro 0: 0.259) | 0.154 ± 0.024 / 0.158 ± 0.026 / +0.15 | 0.116 ± 0.019 / 0.122 ± 0.021 / +0.31 | 0.086 ± 0.020 / 0.091 ± 0.015 / +0.26 | 0.070 ± 0.014 / 0.079 ± 0.012 / +0.64 | 0.080 ± 0.007 / 0.075 ± 0.012 / -0.61 |
| impulse: centre time, ms (Macro 0: 230.74) | 198.9 ± 15.0 / 203.7 ± 15.4 / +0.32 | 260.8 ± 19.2 / 255.3 ± 14.2 / -0.29 | 294.7 ± 14.5 / 296.5 ± 11.6 / +0.13 | 314.3 ± 14.5 / 310.2 ± 12.6 / -0.28 | 322.6 ± 15.3 / 314.7 ± 13.3 / -0.52 |
| T30 after the burst, s (Macro 0: 3.848) | 3.78 ± 0.13 / 3.76 ± 0.14 / -0.16 | 3.82 ± 0.08 / 3.77 ± 0.14 / -0.72 | 3.80 ± 0.07 / 3.85 ± 0.11 / +0.67 | 3.92 ± 0.14 / 3.89 ± 0.10 / -0.21 | 3.92 ± 0.17 / 3.92 ± 0.14 / +0.04 |
| chord: wet level, dB (Macro 0: -22.994) | -25.12 ± 0.50 / -24.99 ± 0.40 / +0.26 | -24.84 ± 0.53 / -24.62 ± 0.38 / +0.43 | -24.16 ± 0.30 / -23.92 ± 0.31 / +0.81 | -23.36 ± 0.26 / -23.42 ± 0.29 / -0.22 | -22.95 ± 0.40 / -23.02 ± 0.24 / -0.18 |
| waveform correlation of two renders, noise: ref and ref / eng and eng / ref and eng | 0.848 / 0.871 / 0.864 | 0.866 / 0.848 / 0.858 | 0.826 / 0.822 / 0.804 | 0.730 / 0.775 / 0.745 | 0.740 / 0.732 / 0.734 |

The law of the knob is reproduced at every step (the 8 kHz level rises by 3.5 dB to Macro 25 %
and falls by about 9 dB from 50 to 100 % in both), and no difference of the means in this table
exceeds 1.2 sd. Whole cases: p 0.93, 0.50, 0.78, 0.49, 0.22.

One thing about the plug-in's own seed shows in every step: in the 20 s of noise of this
programme (instance age 27 to 48 s) its voices move less than a typical instance's. Depth at
8 kHz 0.42 to 1.38 dB against the reference's 0.99 to 3.56 dB (-1.2 to -4.6 sd), level swing
0.93 to 1.53 dB against 1.65 to 2.31 dB. Over the 97 s of section 3.3 the same seed is ordinary.

## 6. The first second after the start

A fresh instance with noise on its very first frame, no warm-up (`start_A`, `t06_start_detail.py`,
`t02_start_identity.py`). In the Analyzer the parameters reach a plug-in after it is created;
a host that restores a session before it prepares the plug-in may behave differently.

- **Reference, first 16 ms**: it passes the input straight through and fades it out: gain
  1.000 in the first millisecond, 0.93 and 0.90 (left, right) in the second, 0.50 and 0.38 at
  2 to 4 ms, 0.10 and 0.08 at 4 to 8 ms, 0.006 and 0.012 at 8 to 16 ms, the same in all 10
  realisations.
  Level of the first 50 ms: -32.74 ± 0.16 dB.
- **Engine, first 35.6 ms**: silence until its first arrival; level of the first 50 ms
  -45.6 ± 2.8 dB.
- **Built plug-in, first 200 ms**: in the Analyzer's order it starts as its default (Character
  Default, Mix 35 %) and crossfades to Ebb: straight-through gain 0.64 in the first millisecond,
  0.04 to 0.06 at 16 to 24 ms and 0 after; until 200 ms its wet differs from the engine's (the
  difference lies at -32 to -37 dB under a wet of -32 to -28 dB, the plug-in 0.8 to 1.1 dB lower
  from 50 ms on). From 200 ms on it equals the engine bit for bit (0 differing samples with a
  warm-up of 0.25 s or more). `FDNReverb::prepare` takes the Character and Mix that are set at that moment, so an
  instance prepared with Ebb already selected should have no crossfade; I did not test that order.
- **0.1 to 0.5 s**: the engine is louder in the mids and duller on top than the reference:
  300 Hz to 3 kHz +1.60 dB (4.6 sd, 8.8 se) at 0.1 to 0.25 s and +0.74 dB (2.7 sd, 3.9 se) at
  0.25 to 0.5 s; above 3 kHz -1.66 dB (-2.2 sd, -3.8 se) and -1.07 dB (-1.0 sd, -2.1 se). The
  verification of the phase packet found that the reference follows the voice law of a lower
  Macro during its first 0.6 s; this is the same thing seen in levels.
- **From 0.5 s on** nothing differs: whole level -0.08 dB (0.5 to 1 s), -0.17 dB (1 to 2.2 s),
  -0.15 dB (2.2 to 4 s), all within 0.5 sd; the three broad bands within 0.85 dB and 1.7 se;
  the 25 descriptors from 0.5 s on read rms t 1.00 (p 0.42). The probe agrees: of its 80
  impulses only the first, at 0.25 s, differs beyond chance (section 8).
- The plug-in's own seed starts a little loud: +0.65 dB at 0.5 to 1 s and +0.72 dB at 1 to 2.2 s
  against the reference's mean (2.8 and 2.1 sd).

Audible only when a pass starts on the frame the instance is created: a 16 ms blip of dry sound
in the reference that Ebb does not have, and about 1.6 dB of tone difference for the first half
second.

## 7. Ocean's defaults against the neutral A/B state

The plug-in at Decay 5 s, Size 100 %, Evolution 100 % with Ocean's own controls at their default
positions, one at a time and together (`t09_defaults.py`; read-back `80`, `9000`, `100.0`,
`35.0`). Same voice seed, so every difference is the control's. With Mix at 35 % the wet part is
taken out of the output (output minus dry, over the wet gain 0.7).

| | neutral | Low Cut 80 Hz | High Damping 9 kHz | Focus 100 % | all three and Mix 35 % (wet part) |
|---|---|---|---|---|---|
| wet level, steady noise, dB | -25.34 | -26.46 (-1.11) | -25.80 (-0.46) | -27.13 (-1.79) | -29.11 (-3.77) |
| burst, 50 Hz third octave, dB | -42.2 | -52.1 (-9.9) | -42.2 | -42.3 | -52.2 |
| 80 Hz | -46.3 | -51.5 (-5.1) | -46.3 | -46.5 | -51.7 |
| 125 Hz | -44.5 | -47.6 (-3.1) | -44.5 | -44.9 | -48.0 |
| 1 kHz | -43.1 | -43.4 (-0.3) | -43.3 (-0.2) | -44.6 (-1.6) | -45.1 (-2.1) |
| 4 kHz | -46.1 | -46.2 | -47.9 (-1.8) | -48.0 (-1.9) | -50.0 (-3.9) |
| 8 kHz | -52.2 | -52.3 | -54.6 (-2.4) | -53.4 (-1.2) | -55.8 (-3.6) |
| T30 at 125 Hz, s | 3.60 | 1.85 (-49 %) | 3.60 | 3.61 | 1.89 (-48 %) |
| T30 at 500 Hz | 3.81 | 3.38 (-11 %) | 3.76 (-1 %) | 3.84 | 3.36 (-12 %) |
| T30 at 1 kHz | 4.00 | 3.69 (-8 %) | 3.78 (-6 %) | 4.07 | 3.57 (-11 %) |
| T30 at 2 kHz | 4.43 | 4.19 (-5 %) | 3.73 (-16 %) | 4.41 | 3.62 (-18 %) |
| T30 at 4 kHz | 3.68 | 3.51 (-5 %) | 2.59 (-30 %) | 3.75 | 2.65 (-28 %) |
| T30 at 8 kHz | 2.72 | 2.63 (-3 %) | 1.46 (-46 %) | 2.80 | 1.52 (-44 %) |
| T30 broadband | 4.01 | 3.84 (-4 %) | 3.72 (-7 %) | 4.06 (+1 %) | 3.48 (-13 %) |
| drums, wet level while they sound, dB | -32.62 | -36.45 (-3.8) | -32.72 | -33.00 (-0.4) | -37.19 (-4.6) |
| drums, level in the second after | -43.45 | -49.21 (-5.8) | -43.86 | -43.56 | -51.05 (-7.6) |
| chord, wet level while it sounds | -21.98 | -23.96 (-2.0) | -22.12 | -23.06 (-1.1) | -25.37 (-3.4) |
| chord, level in the second after | -27.95 | -32.50 (-4.5) | -28.24 | -28.74 (-0.8) | -34.61 (-6.7) |

In units of the reference's spread (programme / steady noise):

| Control | third-octave spectra, rms (largest) | T30, rms (largest) | levels, rms | channel correlation, rms (largest) |
|---|---|---|---|---|
| Low Cut 80 Hz | 8.7 sd (50) / 27 sd (100) | 13.1 sd (41) | 6.6 sd / 24 sd | 2.7 sd (11) |
| High Damping 9 kHz | 0.8 sd (3.8) / 6.2 sd (15) | 2.5 sd (8.6) | 1.8 sd / 9.8 sd | 0.5 sd (1.4) |
| Focus 100 % | 1.9 sd (10.6) / 18 sd (40) | 0.1 sd (0.6) | 2.4 sd / 38 sd | 12.7 sd (48) |
| all three, wet part at Mix 35 % | 9.0 sd (51) / 37 sd (101) | 13.5 sd (41) | 9.8 sd / 80 sd | 13.7 sd (48) |

- **Low Cut 80 Hz** is in the loop, so it is far more than a cut at 80 Hz: the tail below 125 Hz
  lasts half as long and even at 1 kHz it is 8 % shorter. Clearly audible on anything with bass.
- **High Damping 9 kHz**: the tail above 4 kHz is 30 to 46 % shorter and 2 dB darker. Audible.
- **Focus 100 %** turns the wet down while the source sounds: -1.8 dB under steady noise, up to
  -5.3 dB in the 2 kHz third octave, and it changes the channel correlation (0.072 to 0.104 under
  noise, -0.31 at 2 kHz on the drums). Decay times stay. Audible as a quieter, tucked-in wet.
- **Mix 35 %** by itself leaves the wet exactly as it is (every descriptor of the wet part reads
  0.00 from neutral) and follows the reference's law: wet at 0.7 (-3.10 dB), dry at unity. Under
  steady noise the wet then lies 7.3 dB under the dry. It is not a departure from the reference as
  long as the reference's Mix is at 35 % too, but it hides most of what an A/B is meant to show.
- Two more defaults are not neutral: **Pre-delay 20 ms** (the reference's baseline is 0; at 20 ms
  on both they null, section 5) and **Evolution 35 %** (the listening point is 100 %).

## 8. The plug-in's one voice seed

- The seed is an ordinary one: among the engine's seeds its descriptors read 0.95 to 1.20 rms z
  per case (a seed among the other seeds: 0.98 to 1.27), its mean level lies at the 92nd
  percentile of 120 seeds (+0.06 dB) at ages 18 to 115 s and at the 98th (+0.10 dB) at 68 to
  165 s (`t07_seed_population.py`).
- In single stretches it stands out, as any seed does somewhere: little voice movement at ages
  27 to 48 s (section 5), a dark top on the drums at 56 to 70 s (section 3.8), a deep dip of the
  left voices around 22 to 24 s (1st and 2nd percentile of 240 seeds in the probe).
- **It is always the same.** Every instance and every prepare or reset replays this trajectory
  from its start, while every instance of the reference draws a new one. Two consequences a
  listener can meet:
  - two plug-in instances that were prepared together and get the same input return the same
    samples (two fresh instances on the 58 s programme: 0 of 5,568,000 samples differ): their
    sum is +6.02 dB over one, where two instances of the reference correlate at
    0.705 (0.656 to 0.737) and sum to +5.33 dB; on different inputs their swells still coincide
    in time, which the reference's never do;
  - a bounce of Ebb repeats exactly; a bounce of the reference never does.
- **The voices of the engine move like the reference's at every instance age** (`t10_voice_probe.py`:
  80 impulses, energy of each response per output and its share above 3 kHz, 24 realisations
  against 240 seeds, two-sample Kolmogorov-Smirnov per impulse). Share of the 320 comparisons with
  p below 0.05 / 0.01 / 0.001: 4.7 % / 1.3 % / 0.3 %; for 24 engine seeds against the other 216
  (200 random choices) 4.7 % / 0.8 % / 0.0 % on average, 95th percentile 12.8 % / 3.5 % / 0.3 %.
  Mean differences by age: 0.31 and 0.33 dB rms (energy, left and right), 0.61 and 0.43 dB rms
  (share above 3 kHz); spread ratio 1.00 and 0.98 in the median. The one clear difference is the
  impulse at 0.25 s (p 2.4e-6), inside the first 0.6 s of section 6.

## 9. What a listener could hear

Named in the order in which the owner is likely to meet them.

1. **Ocean's defaults** (section 7). Low Cut 80 Hz, High Damping 9 kHz and Focus 100 % move the
   wet by 9 to 14 of the reference's spreads in the rms and by 40 to 100 at the worst band: bass
   tail halved, top of the tail 30 to 46 % shorter, wet 1.8 dB down and up to 5 dB at 2 kHz while
   the source plays. With Mix at 35 % the wet is another 3.1 dB down under a full dry signal.
   This is the first thing to set right before an A/B.
2. **The swells are elsewhere in time.** At any moment the brightness of a channel differs by
   2.7 dB rms and up to 7 to 8 dB between the two plug-ins, and the spectrum of a short phrase by
   1 to 2.5 dB rms. Exactly as much as between two instances of the reference (section 3.9).
   Audible, expected, not a fault.
3. **Ebb repeats, the reference does not** (section 8): identical bounces, and two instances
   that swell together; two instances on one source sum 0.7 dB louder than two of the reference.
4. **The first half second of a fresh instance** (section 6): the reference's 16 ms of dry sound,
   and 1.6 dB more mids and 1.7 dB less top in Ebb from 0.1 to 0.25 s. In the Analyzer's order
   the plug-in also crossfades from its Default Character for 200 ms.
5. **Not audible, though measured**: level -0.015 dB; long-term spectrum within 0.16 dB per third
   octave; level swing within 0.06 dB; swell spacing 8.54 against 8.44 s; decay times within
   2.5 % rms, with a possible excess of half a percent below 1 kHz (section 3.4); first arrivals
   at the same times within 0.02 ms; channel correlation within 0.002 under noise; modulation
   depth within 0.27 dB.
6. **Not compared, so unknown**: what happens while a knob moves; hot input that works the level
   stage (peaks above -5 dBFS); Freeze; instances older than 165 s.

## 10. Knob settings for a fair A/B in the owner's host

Insert both plug-ins on the same source, with the host's delay compensation on. Rev OCEAN reports
a latency (44, 48 and 96 samples at 44.1, 48 and 96 kHz); Ebb reports none and is already
aligned to the reference's compensated output.

**Amanita Ocean** (every control; the ones that differ from its defaults in bold):

| Control | Value | Default |
|---|---|---|
| **Character** | **Ebb** | Default |
| **Mix** | **100.0 %** | 35.0 % |
| Decay | the same seconds as the reference, for example 5.00 s | 5.00 s |
| Size | the same percentage as the reference, for example 100.0 % | 100.0 % |
| **Pre-delay** | **0.0 ms** | 20.0 ms |
| **Low Cut** | **20 Hz** (fully down) | 80 Hz |
| **High Damping** | **20000 Hz** (fully up) | 9000 Hz |
| **Evolution** | **100.0 %** | 35.0 % |
| Width | 100.0 % | 100.0 % |
| **Focus** | **0.0 %** | 100.0 % |
| Freeze | Off | Off |
| Harmony | 0.0 % | 0.0 % |
| Mono Safe | Off | Off |
| Bypass | Off | Off |

**Rev OCEAN 1.0.0.5848**: factory preset "Cleaner Tides" (its Macro Mode is Tide; the mode is not
a host parameter and was never changed in the campaign), then, as the plug-in displays them:

| Control | Value |
|---|---|
| Mix | 100 |
| Macro | 100 |
| Decay | the same seconds, for example 5.00 |
| Size | the same percentage, for example 100 |
| Predelay | 0.000 ms (free, not synced) |
| Brightness | 0.000 |
| HPF | 20.0 Hz |
| LPF | 20000 Hz |
| Transients | 0.000 dB |
| Ducking | 0.000 |
| Return | 0.000 dB |
| Master Volume | 0.000 dB |
| Width | 100 |
| On/Off | Active |

Rules for the comparison:

- **No level trim**: the two are level with each other within 0.02 dB on average.
- **Ranges where the same number means the same thing**: Decay 0.5 to 30 s; Size 50 to 200 %
  (below 50 % Ocean's knob follows another curve: the reference's 30 % is Ocean's 26.8 %);
  Pre-delay 0 to 250 ms in the same milliseconds; Mix the same percentage; Width only at 100 %
  (and 0 %): Ocean's 150 % is the reference's 129.3 %, its 200 % the reference's 150 %. I measured
  Decay 0.5 to 20 s, Size 60 to 180.1 %, Pre-delay 0 and 20 ms, Mix 35 and 100 %, Width 100 %; the
  rest of these mappings is the integration engineer's.
- **Let both run for a second** after inserting them or loading the session before judging.
- **Do not expect the swells to line up.** Listen for a minute or two, or to several passes, and
  judge how deep, how fast and how bright the movement is, not where it falls. To hear how much
  of a difference is the reference's own, compare two instances of the reference the same way.
- **For a check that needs no judgement**, turn Macro and Evolution to 0: the two then null at
  -110 dB.
- Keep the source's peaks under -5 dBFS if the comparison should stay inside what this review
  measured; above that both plug-ins turn the wet down (the reference's level stage, which Ebb
  carries; engineers' nulls at Macro 0, not re-measured here).

## 11. Not done, and limits

- Nothing was listened to. No host was opened; only the VST3 was rendered (AU and CLAP were not).
- The plug-in could be rendered from one voice seed only; other seeds are the engine's renderer,
  tied to the plug-in by bit identity on its own seed (16 renders, 109.6 million samples).
- Input peaks at or below 0.5 throughout: the level stage and the clipper never worked.
- 48 kHz except for one steady-noise case each at 44.1 and 96 kHz; 88.2 kHz not rendered.
- Ages of an instance up to 165 s. Controls in motion, Character switches in a host, Freeze,
  Harmony, Mono Safe and the reference's own non-neutral controls were not compared.
- Whether a host that restores state before preparing the plug-in avoids the 200 ms crossfade at
  the start was read from the code, not measured.
- The reference's realisations of a case were rendered together within seconds. The phase
  verification found the tie between the two outputs' start levels to depend on the batch; my
  spreads are spreads within my batches.
- A difference of two ensemble means is resolved (2 se) from about 1.0 sd with 6 realisations
  and 12 seeds, 0.9 sd with 10 and 12, and 0.6 sd with 16 and 48. Smaller systematic differences
  than that are not excluded; by the thresholds of section 2 they would not be audible.
- I did not run the campaign's `descriptors.py` or `score_tide.py`; the integration engineer's
  figures from them stand unverified by me.
- Other reviewers were writing to the campaign's capture cache and to `findings/` while I worked;
  `build-ebb` did not change during the review (same binary hash at the first and last render).

## 12. Files

Everything is under `Analyzer/Results/RevOceanCharacterization/work/review_listening/`. The
review wrote nowhere else except this report: `revocean`'s cache and scratch folder and the
candidate driver's scratch folder were pointed into that folder (`rl_common.py`).

- `rl_common.py` (sources), `rl_stimuli.py`, `rl_analysis.py` (descriptors), `rl_run.py` (cases),
  `rl_compare.py` (units), `t01_selftest.py` (descriptors on synthetic signals);
- `descriptors_v2/<case>/<source>_<index>.json`: every descriptor of every render;
- results as text: `permutation.txt`, `tables.txt`, `macro_table.txt`, `macro0.txt`,
  `predelay_default.txt`, `single_pass.txt`, `two_instances.txt`, `start_rows.txt`,
  `start_detail.txt`, `start_identity.txt`, `defaults.txt`, `seed_population.txt`,
  `voice_probe.txt`, `first_arrival.txt`, `decay_bias.txt`, `decay_test.txt`,
  `two_plugin_instances.txt`; the script of each
  is the `t*.py` named in its section;
- `captures/`: the 213 captures of the reference, **3.7 GB**. The reference is not repeatable
  above Macro 0, so these back the numbers above; they can be deleted once the report is accepted
  (the disk had 39 GB free at the end).
