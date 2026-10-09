# Review of the Ebb engine (`Source/dsp/Ebb*`)

Adversarial review of the C++ port against `SPEC.md` and `reference_render.py`,
7 October 2026. Read-only for the source tree. Scratch files, logs and the
JSON behind every number are in
`Analyzer/Results/RevOceanCharacterization/work/review_engine/`; builds are in
`build-ebb-review-engine/` and `build-ebb-review-engine-contract/`.

## Verdict

The port is faithful. I found no place where the engine departs from the
specification at rest, no undefined behaviour, no allocation or unbounded loop
on the audio path, and my own scores reproduce the engineers' numbers.

Four things are wrong or weaker than reported, none of them in the steady
state at the measured host rates:

| # | Severity | Finding |
| --- | --- | --- |
| 1 | major | Turning Size while sound plays leaves broadband noise 26 to 34 dB under the wet tone; the Default Character leaves it 73 to 79 dB under |
| 2 | minor | At 64 kHz and 384 kHz the engine nulls against the reference at -77 dB; the campaign's model with its simulated clock signs reaches -117 and -119 dB there |
| 3 | minor | The filter states are not floored: after a tail has died every frame computes subnormal numbers, for ever |
| 4 | minor | `EbbExactArithmetic.h` does not hold under `-ffp-contract=fast` (or `-ffast-math`, `-Ofast`); the null then drops to -102 to -110 dB |
| 5 | note | Host rates outside 22.05 to 384 kHz are processed as the nearest bound, so time and pitch scale |

## Independent scores

Driver: `review_score.py` around `build-ebb-review-engine/AmanitaOceanEbbRender`
(Release, the project's flags). Nothing fitted; the engine's output is moved
back by the reported latency. Nulls are
`10 log10(sum (candidate - reference)^2 / sum reference^2)`.

### Locked network holdout

Worst -114.06 dB, mean -116.78 dB. The engineers report the same.

| case | overall | 0.25-1.4 s | 1.4-4 s | 4-6 s | 6-14 s |
| --- | --- | --- | --- | --- | --- |
| 48 kHz, 0.5 s, 100 % | -118.39 | -110.08 | -118.40 | -118.02 | -101.92 |
| 48 kHz, 2.0 s, 100 % | -116.38 | -105.59 | -116.40 | -106.32 | -102.25 |
| 48 kHz, 7.3 s, 100 % | -114.06 | -103.89 | -114.90 | -107.48 | -104.25 |
| 48 kHz, 1.4 s, 62 % | -118.92 | -106.52 | -118.93 | -109.74 | -101.37 |
| 48 kHz, 3.1 s, 157 % | -115.35 | -92.62 | -115.59 | -105.46 | -101.95 |
| 44.1 kHz, 2.0 s, 100 % | -115.20 | -109.23 | -115.21 | -106.10 | -102.16 |
| 48 kHz, 1.0 s, 88 %, warm-up 12.5 s | -119.19 | -116.67 | -119.19 | -113.51 | -102.07 |

With the warm-up passed through `advanceIdle()` all seven renders are
bit-identical.

### Fresh captures at Macro 0 (programme seeds 60221 and 60222, my own settings)

Three of the four sit on a Size where the single-precision length rule and
double precision disagree (found by `find_ties_fine.py`; none is a position
the engineers used).

| capture | engine | model | engine against model | check |
| --- | --- | --- | --- | --- |
| 48 kHz, Decay 12.7 s, Size 121.29 % (tie on left line 1: 1250.49994 becomes 1251), warm-up 7.3 s | -112.53 | -112.54 | -145.75 | Size one float step lower: -13.41 dB |
| 96 kHz, Decay 0.83 s, Size 54.84 % (tie on line 12 of both groups), warm-up 4.1 s | -119.33 | -119.34 | -148.53 | Size one float step lower: -21.54 dB |
| 44.1 kHz, Decay 3.3 s, Size 166.62 % (tie on right line 7), Pre-delay 61 ms, Width 70 %, Mix 40 %, input peak 3.0, warm-up 5.7 s; engine's own level stage, width, mix and clipper | -121.87 | -121.88 | -150.25 | without the level stage: -22.78 dB; 212 samples above the clipper's threshold |
| 192 kHz, Decay 2.4 s, Size 93.7 %, warm-up 3.3 s (the one standard rate whose output clock reads low) | -116.10 | -116.10 | -147.49 | first whole-response capture at this rate |

### Engine against `reference_render.py` with one phase (my own phase function)

Phase per output: start level, the model's rate, and a slow sine wander; two
to four voice cycles start inside each stimulus.

| case | overall | worst stretch |
| --- | --- | --- |
| 48 kHz, Decay 4.7 s, Size 1.13, Macro 100 % | -147.30 | -141.39 |
| 44.1 kHz, 2.2 s, 0.71, Macro 100 % | -147.61 | -138.74 |
| 96 kHz, 9.0 s, 1.2129 (the tie), Macro 100 % | -146.76 | -141.87 |
| 88.2 kHz, 1.1 s, 1.87, Pre-delay 33 ms, Width 1.6, Mix 0.8, input peak 3.0, whole chain, Macro 100 % | -147.86 | -141.72 |
| 48 kHz, 0.6 s, 0.33, warm-up 11 s, Macro 100 % | -148.38 | -138.90 |
| 48 kHz, 30 s, 2.0, Pre-delay 250 ms, Macro 100 % | -145.24 | -141.37 |
| 48 kHz, 3.0 s, 1.0, Macro 50 % | -146.13 | -139.89 |
| 176.4 kHz, 5.0 s, 1.0, Macro 100 % | -146.07 | -139.80 |

Engine running free from a seed against the model driven by my own Python
reading of `EbbVoicePhase`: -147.58, -145.19 and -149.42 dB (48, 44.1, 96 kHz;
seeds 0x45626245, 987654321987654321, 7). The next seed reads -14.23, -4.49
and -9.37 dB.

Other host rates against the model, Macro 0 and Macro 100 %, the model told to
take unmeasured clocks as exact: -147.54 to -148.49 dB at 22.05, 32, 37.8, 50,
64, 352.8 and 384 kHz, and -147.61 to -148.47 dB at 44101, 22051, 48001 and
383999 Hz, where the engine computes its converter branches per read.

### Other checks that passed

- **Constants.** 33 groups of `EbbEngineConstants.h` recomputed from the
  campaign's laws, the single-precision literals compared bit for bit
  (`check_constants.py`): all equal.
- **Sanitizers.** `review_stress.cpp` under AddressSanitizer and
  UndefinedBehaviorSanitizer, no report:
  - the converter schedule replayed at 421 host rates: a read never needs a
    frame that is not written (at least 16 frames to spare), never reaches
    past the history (19 frames to spare), at most 2 core samples per frame;
  - Size 0.15 and 2.0, Decay 0.2, 30 and 60 s, Pre-delay 0, 0.25 and 2 s,
    Macro 0 and 1 at 17 rates including 22050, 22051, 44101, 383999, 384000,
    8000, 1e7, 0, a negative rate, NaN and infinity: finite, wet peak 178 for
    an input peak of 64;
  - NaN, infinities, `FLT_MAX` and subnormals at the input with every control
    (and non-finite control values) jumping every third frame, `reset()` and
    `advanceIdle()` in between: finite, wet peak 8.89;
  - `reset()` in the middle of a tail with glides under way, then `prepare()`
    at another rate (six pairs): a fresh engine bit for bit;
  - `advanceIdle()` against processed silence at 44101, 383999, 22051 and
    50000 Hz: bit for bit.
- **Architectures.** The tool built for x86_64 and run under Rosetta gives the
  arm64 build's output bit for bit on four renders (6.6 million samples).
- **Unit tests** in my build: 72 pass, 0 fail, 46.7 s.
- **CPU**, Apple M4 Max: 1.31, 1.38 and 1.47 % of a core at 44.1, 48 and
  96 kHz at Macro 100 %. A control that moves at every one-frame block adds
  154 to 168 ns per call (2.18 against 1.44 % at 48 kHz in one run, 2.42
  against 1.61 % in another). Whole renders: 3.25 % at
  384 kHz, 5.4 % at 383999 Hz.
- **Pre-delay.** Ocean's 2501 knob positions through `preDelayMs * 0.001f`
  and `preDelayFrames` give the frame count of the measured law on the same
  milliseconds at six rates: 0 positions off.

## Findings

### 1. Size in motion steps every line by whole samples (major)

`Source/dsp/EbbNetwork.cpp`, lines 415 to 419 (`advanceGlides`) and 385 to 395
(`updateLengths`).

While Size glides, `updateLengths` is called every sample with the gliding
value, and each line length is `floorf(fl(P s) + 0.5f)` of it. The read
position of every line therefore jumps by one sample at a time, at its own
moments. The engineers name this ("fine grain on bright material") but did not
measure it or put it to the owner.

Scenario: a steady tone through the whole DSP at Mix 100 %, Decay 2 s, the
Size knob turned from 100 to 110 % in 0.1 % steps over 2 s (`review_moves.cpp`,
48 kHz). Measure: energy more than 0.68 octave from the tone, against all
energy, while the knob moves.

| tone | Default | Ebb | Ebb, Evolution 100 % | Ebb with the experiment below |
| --- | --- | --- | --- | --- |
| 1 kHz | -78.8 dB | -33.9 dB | -32.4 dB | -72.8 dB |
| 3 kHz | -76.9 dB | -28.2 dB | -27.5 dB | -68.4 dB |
| 8 kHz | -73.3 dB | -25.6 dB | -18.9 dB | -65.7 dB |

One jump from 100 to 130 %, during the 250 ms glide: Ebb -25.3, -17.9 and
-12.8 dB against Default's -45.6, -39.3 and -31.8 dB; at Evolution 100 % an
8 kHz tone reads -2.5 dB.

The existing tests do not see it: "Ebb Size glide" reads 3.442 of the wet
peak with the engine as it is and 3.437 with the experiment.

Experiment (scratch copy `smoothsize/dsp/EbbNetwork.cpp`, not in the tree):
while Size glides, each line moves in a straight line to the whole length of
the target and lands on it exactly. Result: the last column above; all 72 DSP
tests pass; renders at rest and the output before the move are bit-identical.

Decay, Pre-delay, Low Cut, Freeze and Macro steps left nothing comparable:
off-tone energy stays at or below -52 dB in Ebb.

### 2. Converter clocks at host rates without a measured sign (minor)

`Source/dsp/EbbConverter.cpp`, lines 89 to 96;
`Source/dsp/EbbEngineConstants.h`, lines 48 to 51.

The engine uses the six measured sign pairs and takes every other rate as
exact. Two fresh captures (Decay 2 s, Size 100 %, 6 s of programme):

| host rate | engine against reference | model with the campaign's simulated signs against reference |
| --- | --- | --- |
| 64 kHz | -77.36 dB | -117.47 dB, signs (low, low) |
| 384 kHz | -77.57 dB | -118.78 dB, signs (high, low) |

At 128 kHz the engine is -77.9 dB from the model with simulated signs (no
capture). The engineers' "-143 dB at every host rate from 22.05 to 384 kHz"
is against a model told to use exact signs, so it does not show this.
Nothing of it is audible. Porting `converters.clock_signs` into
`EbbRateLattice::at`, or adding the pairs for 64, 128 and 384 kHz to the
table, would close it; the two captures now support (low, low) and
(high, low).

### 3. Filter states are not floored (minor)

`Source/dsp/EbbNetwork.cpp`: `equalise` (549 to 561), `filterLoop` (564 to
580), `Voice::process` (248 to 256), `shapeLoop` (586 to 603);
`Source/dsp/EbbConverter.cpp`: `write` (160 to 168).

Lines, comb and output are floored at 1e-30. The double-precision states of
the equaliser, the loop filter, the voices, Ocean's two loop filters and the
converter histories are not.

- Replica of the two recursions with the engine's coefficients
  (`review_timing.cpp`): after one input sample the equaliser's output is
  subnormal from sample 46,802 (1.06 s) and its four states stay at about
  1e-322 from sample 48,964 on; the loop filter is subnormal after 0.117 s and
  never reaches zero in 60 s.
- The engine itself (`review_subnormal.cpp`: 1 s of noise, then zeros, Decay
  0.5 s, 44.1 kHz): the underflow flag is raised in 41,596 of the 44,100
  frames between 2 and 3 s and in every frame of every later second, still
  29 s in. The output is exactly zero from 5.64 s on.

On this machine it costs nothing: at 44.1 kHz 290 to 295 ns per frame in
every second of one run and 325 to 341 ns in a second run, with no change in
either when the subnormal numbers start.
The plug-in runs under `juce::ScopedNoDenormals`, so the product is covered.
The DSP tests, the offline renderer and any other host of the library are
not, and the README lists Windows and Linux x64 builds whose CI runs the DSP
tests. What that costs on an Intel processor I could not measure (Rosetta
does not reproduce it). The existing code floors every recursive state
(`FDNReverb::flushDenormal`).

### 4. The contraction pragma is weaker than its comment says (minor)

`Source/dsp/EbbExactArithmetic.h`, lines 1 to 12.

Clang's `-ffp-contract=fast`, which `-ffast-math` and `-Ofast` imply,
disregards `#pragma clang fp contract(off)`. Four renders against the Release
build (`review_builds.py`):

| build | samples that differ | null |
| --- | --- | --- |
| arm64, `-ffp-contract=fast`, pragma in place | 63 to 98 % | -101.64, -104.19, -103.37, -110.24 dB |
| arm64, pragma removed, `-ffp-contract=fast` | the same samples | the same |
| x86_64, `-mfma -mavx2 -ffp-contract=fast`, pragma in place | the same counts within 22 samples | the same |
| arm64, pragma removed, clang's default | 2 to 44 % | -147.50 to -151.87 dB |
| x86_64 without FMA | 0 | bit-identical |

The project's flags (`-O3`) are safe, and the unit tests catch the case: built
with `-ffp-contract=fast`, 2 of 72 fail (golden vectors at -98.24 dB, level
stage at frame 52). The GCC and MSVC branches of the header were not compiled
by anyone.

### 5. Host rates outside 22.05 to 384 kHz (note)

`Source/dsp/EbbEngine.cpp`, lines 17 to 19 and 60 to 66.

`hostRateOf` clamps the rate, and the engine then processes frames as if they
came at the bound. Renders at 768 kHz and 384 kHz of the same samples are
identical frame for frame, and so are 16 kHz and 11.025 kHz against
22.05 kHz. At 768 kHz Ebb's tail is half as long and an octave up; at 16 kHz
it is 1.38 times longer. The other Characters accept any rate above 1 kHz.

## Not done

- Nothing was listened to.
- No phase-fitted null against the reference above Macro 0; I compared engine
  and model with one phase, as asked.
- No Intel hardware, no GCC and no MSVC: findings 3 and 4 are measured on
  Apple silicon and under Rosetta only.
- No capture below 44.1 kHz (the host tool refuses those rates) and none at
  128 kHz.
- `Tests/DspTests.cpp` and `FDNReverb.cpp` were read only as far as the engine
  needed; the integration has its own review.

## Left behind

- Six reference captures added to the cache, about 65 MB: the four of the
  fresh table, and 64 and 384 kHz.
- `build-ebb-review-engine/` (Release build of the library, tool and DSP
  tests, plus `scratch/` with the probe binaries) and
  `build-ebb-review-engine-contract/` (the DSP tests with
  `-ffp-contract=fast`). Both are ignored by Git; delete them at will.
- Two scratch copies of the engine sources under the work folder
  (`nopragma/`, `smoothsize/`), marked as experiments.
