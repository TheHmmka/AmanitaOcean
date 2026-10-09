# Behaviour review: Ocean's Mix, a voice seed per instance, and the rename to Fathom

Independent review of the uncommitted tree after two decisions of the owner
(Fathom leaves through Ocean's linear Mix; every plug-in instance draws a voice
seed of its own) and after the rename of the sixth Character from its working
name to Fathom. Run on 7 October 2026, 17:20 to 17:50, on the working tree as
it stood then (the hashes of the 51 source files are in
`r2/sources_at_start.sha256`; none changed during the review, although another
wave was editing `Source/ui` and `Tests/StateTests.cpp` shortly before).

The source tree was not changed. Everything was built into
`build-ebb-review-behaviour` (Release, arm64; a ThreadSanitizer configuration
in its subfolder `tsan`). Probes, logs and listings are in
`Analyzer/Results/RevOceanCharacterization/work/review_behaviour/r2/`; the
files beside `r2` are an earlier pass of this review on the tree before the
rename. The reference was read from the campaign's capture cache only: the
drivers refuse to render through it.

Binaries of this review: VST3 SHA-256 `b5cb8589...3e75`,
`AmanitaOceanFathomRender` `bae038f1...9660` (the same bytes as the rename
engineer's, from another build folder), and `AmanitaOceanEbbRender`
`b255b626...f66d`, built into the same folder at 14:21 from the tree before the
rename (the same bytes as the rename engineer's baseline).

## Verdict

No blocker and no major finding. Both decisions are in the code as stated, the
rename changed no sample, and the five other Characters still render the bytes
of commit 4a0ac08. One limit of decision 1 is real and already listed for the
owner (finding 1); the rest are stale texts and notes.

## What was run

| Check | How | Result |
| --- | --- | --- |
| Dry gain in Fathom against Default, Mix 0, 10, 35, 50, 70, 90, 100 % | dry alone (Pre-delay 250 ms) through `FDNReverb`, through the processor, and through the built VST3 in the Analyzer | 0 of 19200 samples differ from Default (and from Bloom, Drift, Veil, Current) at every Mix, at all three levels; gain by least squares 1.000000000, 0.900000004, 0.650000006, 0.500000000, 0.300000012, 0.100000024, 0 = `1 - m` of the single-precision `m` |
| Wet gain | tail after the input against `fl(m x wet at Mix 100 %)`; whole output against `fma(m, wet - dry, dry)` | 0 samples differ (134400 per Mix in the DSP, 72000 frames through the processor and through the VST3); wet share 0.0999999888, 0.3499999924, 0.5000000001, 0.6999999889, 0.8999999775, 1 (VST3) |
| Mix next to the ends | 0.1, 1, 20, 25, 30, 75, 99, 99.9 % | 0 samples differ from Default; gain `1 - m` to nine places |
| Dry level through a switch, wet silent | 30 switches among the six Characters 10 ms into noise, 7 Mix values, input peak 0.5 and 2.0 | 0 pairs differ from Default held, in any sample |
| Dry level through a switch, wet sounding | the same switch rendered at Mix `m` and at Mix 100 %, 10 switches with Fathom, 7 Mix values | largest `abs(out(m) - ((1 - m) dry + m out(100 %)))` 4.1e-8 at a peak of 0.4; dry share over the 200 ms morph 1.0, 0.9, 0.65, 0.5, 0.3, 0.1, 0 to seven places |
| Mix moved through 100 % in Fathom | 90 % to 100 % and back while dry and wet sound | bit for bit the sum before, the wet itself at 100 %, the sum after; no jump of the dry share on the ramp. The wet taken as it is lies at most 1.2e-7 from the sum the other Characters keep (-142 dB re the dry sample) |
| Mix 100 %, Macro 0 against the engine's renderer | `score_engine.py plugin` and `plugin_statistics` on the VST3 of this review | 36 renders (7 holdout, 10 own captures, 6 below Mix 100 %, 13 statistics stimuli at Evolution 0), 0 differ |
| `FDNReverb` at Mix 100 % against `FathomEngine` + level stage + clipper | 44.1, 48, 96 kHz, Evolution 0 and 100 %, input peak 0.5 and 3.0 | 0 of 4,514,400 samples differ |
| Macro 0 holdout | `score_engine.py holdout` on the renderer of this review; the plug-in through `score_network.py` | engine worst -114.06, mean -116.78 dB (-118.39, -116.38, -114.06, -118.92, -115.35, -115.20, -119.19), the same lines as before the rename; plug-in worst -114.06, mean -116.77 dB |
| Macro 100 % ensemble | 13 target cases, 8 new VST3 instances each, `descriptors.compare_to_targets` | 20 impulse descriptors 0.156 to 0.376 in the reference's spreads (reference against itself 0.279 to 0.482); fluctuation 1.542 (1.513); modulation rates 1.023 (1.47); time variance 0.331 (0.426); largest value in errors of the mean 1.238; all 8 renders differ in each of the 13 cases |
| Two instances on steady noise | 6 new VST3 instances, 50 s, against 6 cached reference realisations | pairwise correlation 0.575 (0.497 to 0.724), a pair sums to +4.98 dB; reference 0.558 (0.486 to 0.656), +4.93 dB; at Evolution 0 two instances differ in 0 samples, +6.02 dB |
| Seeds, one process | 100000 instances in two loops; 32000 on 8 threads at once | 0 duplicates; share of ones per bit 0.495 to 0.505 (the first 82000); lag-1 correlation of the start levels and first knots of neighbouring instances within 0.0033 over the 100000 (one sd 0.0032); mean Hamming distance of neighbouring seeds 32.02 |
| Seeds, processes at the same moment | 640 processes, 64 started at once, two instances each | 1280 seeds, 0 duplicates, none shared with the 132000 above; 150 of 639 neighbouring start times under 100 us apart, 4 equal to the microsecond |
| Seed and state | processor probe (`r2/proc/life.log`) | two instances with equal settings save the same 524 bytes: 13 `PARAM` children, no attribute; the seed is not in them as text or as binary |
| Seed and lifetime | the same probe | the same render after `prepareToPlay()` again, after `releaseResources()`, after other rates and block sizes, after another instance's state was restored; `reset()` leaves the seed; a new instance opened from the state has another seed and another realisation |
| Seed in the DSP classes | `r2/dsp/seedprobe.log` | no seed set is `0x45626245`; a seed given while processing changes nothing until `reset()` or `prepare()`; Evolution 0 and the five other Characters do not hear it |
| Seed and the audio thread | every use in `Source/` listed | drawn at one place, the constructor's initialiser (`PluginProcessor.cpp:164`); `std::random_device` and the clock appear nowhere else in `Source/`; the phase generator on the audio thread is counter-based arithmetic |
| Hand-off under ThreadSanitizer | created on one thread, prepared on a second, processed on a third while others read the seed, save and restore the state, move parameters and create 160 instances; three rounds | 0 reports. Control: `releaseResources()` during `processBlock()` gives 324 reports, so the instrumentation sees the plug-in's code |
| Start phase over many seeds | `FathomVoicePhase` for the 83280 seeds the plug-in drew, and for three sequences of 100000 | left 0.1840 to 0.5910, right -0.0070 to 0.4330 (law 0.184 to 0.591 and -0.007 to 0.433; measured 0.190 to 0.593 and -0.007 to 0.431); uniform (KS p 0.48 and 0.20); same half 0.8295 (campaign 0.833); correlation 0.496 (campaign 0.535); first knots 7.353 to 14.706 s and 6.250 to 12.500 s, the cells; the engine run from a seed equals the engine given that phase bit for bit |
| Five other Characters against 4a0ac08 | the export `work/review_integration/head` checked file by file against `git show` (41 files equal), built with the Release flags; three harnesses | 84 renders (12,902,100 frames, FNV `23b4769e14092b8c`), the 96 cases of the integration harness, and 950 cases of this review (90,317,250 frames: 7 Mix values, 9 input levels from 1e-30 to hostile, 3 rates, Mix automated through 100 %): all the same bytes |
| Rename: renderer | the rename engineer's set with both renderers of this review; a set of this review (the holdout settings with their warm-up, Macro 25 to 100 % with eight seeds, both Mix laws quiet and loud, 22.05 to 384 kHz) | 84 renders, 9 inputs and 6 benchmark checksums equal the recorded listings; 85 renders of the own set (26,700,600 frames), 0 differ |
| Rename: sixth Character through `FDNReverb` | the DSP sources of before the rename (16 of the 17 hashes the wave before recorded match that copy) against the present library | 323 cases, 34,766,400 frames: every Mix, three seeds, switches both ways with every Character and moving controls, `reset()` and a second `prepare()`; the same bytes |
| Rename: generated headers | `emit_engine_header.py --check`, and the generator's text written to a scratch file | both up to date and byte-identical; against the headers of before the rename 3 lines differ in each (the title and the two namespace lines) |
| Rename: leftovers | the search over `Source Tests Tools CMakeLists.txt docs README.md`, file names, test reports, strings of the built binaries | only `build-ebb`, `build-ebb-contract`, `build-ebb-sanitize` (20) and three hexadecimal literals (finding 4) |
| Tests | `ctest`, both executables, `--test-fathom` | 2 of 2 passed; DSP 77 passed, 0 failed, 25 about Fathom, its 228 report lines equal the rename engineer's and the mapped ones of before; state tests passed; 26 of 26 |

## Findings

| # | Severity | Finding | Where |
| --- | --- | --- | --- |
| 1 | minor | Above +8 dBFS Fathom does not leave the dry level where it is | `Source/dsp/FDNReverb.cpp:866-867` |
| 2 | minor | Three texts still say the plug-in has the reference's Mix law, one names words the selector no longer has | `Source/dsp/FathomEngine.h:74, 83-85`; `README.md:123`; `docs/FATHOM.md:837-839` |
| 3 | note | Below 1e-20 Fathom passes a dry sample the other Characters flush | `Source/dsp/FDNReverb.cpp:144-149, 869-873` |
| 4 | note | The working name survives in four numbers | `Source/dsp/FathomEngine.h:42`; `Tests/DspTests.cpp:3527, 4390`; `Tools/FathomRender.cpp:383` |
| 5 | note | Without system entropy two processes are told apart by the clock alone | `Source/PluginProcessor.cpp:136-156`; `docs/FATHOM.md:662-666` |
| 6 | note | The tie of the two start levels is the campaign packet's, at the low end of what its verification read | `Source/dsp/FathomNetwork.cpp:165-183` |
| 7 | note | Loose ends outside the code | see below |

### 1. Above +8 dBFS Fathom does not leave the dry level where it is

The reference's clipper follows the Mix sum, so with Mix below 100 % it acts on
the dry signal, and only in Fathom. Dry alone, sine and square of peak 4.0
(+12 dBFS), Mix 0 %: Default returns a peak of 4.000, Fathom 3.623
(-0.859 dB); at Mix 10 % 3.600 against 3.399 (-0.500 dB); at Mix 35 % 2.600
against 2.599. A switch from Default to Fathom 10 ms into noise of peak 3.9
changes 7860 of 23040 samples at Mix 0 %, the largest by -0.762 dB, and 6310 at
Mix 10 % (-0.430 dB); at Mix 50 % and above none, because the dry share stays
under the knee. By the clipper's formula a dry sine at +12 dBFS leaves Fathom
at Mix 0 % with 3.6 % of harmonics (-28.9 dB), at +10 dBFS with 0.9 %; Default
adds none below +12 dBFS. Up to +8 dBFS (an output of 2.512) the dry level is
the same bit for bit in every probe above.

This is the open half of decision 2 in `docs/FATHOM.md:796-806`; the document
says it correctly. The owner's sentence "like the other Characters" holds up
to +8 dBFS. Putting the clipper on the wet alone would be identical at Mix
100 % and would close it; that is the owner's choice.

### 2. Stale texts

- `Source/dsp/FathomEngine.h:74`: "The reference's outer laws, shared by the
  plug-in and the tests." and `:83-85`: "so the plug-in, the renderer and the
  tests agree to the bit". The plug-in calls neither `mixGains` nor `mix` any
  more (`FDNReverb.cpp:850-868`); they serve the renderer's `--mix` and the
  engine tests. Both engineers reported this and neither had the file.
- `README.md:123`: "own Width/Mix laws". The Mix is Ocean's.
- `docs/FATHOM.md:837-839` (decision 10) quotes "Tidal / Swell" and "slow tidal
  swell" from the selector. The selector reads "MODELLED 16-LINE TIDAL NETWORK"
  (`Source/ui/CharacterSelector.cpp:42`); the quoted words are gone.

Otherwise `docs/FATHOM.md` says what the code does in everything this review
measured: the Mix row and the Mix paragraph, the structure, the voice seed
(source, lifetime, state, default of the DSP classes), the input bounds, the
tests it lists, the options and tools it names.

### 3. Below 1e-20 Fathom passes a dry sample the other Characters flush

A settled Fathom leaves through `guardFathom`, which passes anything down to
the smallest normal float; the other Characters leave through `flushDenormal`,
which returns zero under 1e-20. Dry alone at a peak of 1e-21, 1e-30 and 1e-37:
Default returns silence, Fathom the dry signal times `1 - m` in 19199, 19184
and 18480 of 19200 samples. Nothing audible (-400 dBFS and below) and no
denormal leaves either. Two statements are therefore narrower than they read:
the DSP test "the same bits before any wet arrives" holds for its stimulus,
and "passes samples down to the engine's floor of 1e-30" (`docs/FATHOM.md:736`)
is true of the wet; the dry share passes down to 1.2e-38.

### 4. The working name survives in four numbers

`FathomEngine::defaultVoiceSeed = 0x45626245` is the ASCII text "EbbE"; the
search the task prescribes cannot see it. It is also in
`score_engine.py:115`, in `docs/FATHOM.md` and `docs/FATHOM_VALIDATION.json`.
The three literals the rename engineer kept (`0x0ebb5eedu`, `0x0ebb71deu`,
`0x0ebbbe7cu`) spell it in hexadecimal digits. All four are numbers: changing
the first changes every render made from the default seed (in the recorded
listing `m100_default_seed` and `m70_default_seed_warmup` at each rate),
changing the others the test stimuli and the benchmark checksum. Keeping them is consistent with "no numeric
change"; whether the name may stay there is the owner's call.

### 5. Without system entropy two processes are told apart by the clock alone

Inside one process the count of instances separates the seeds whatever the
entropy and the clock do (`scrambleBits` is one-to-one). Between two processes
with no entropy only `high_resolution_clock` differs. On this machine it steps
by 41 ticks of a nanosecond, and two readings in a row are equal in 56 % of
2 million tries. A copy of the function with the entropy taken away, 2560
processes started 64 at a time: 0 equal seeds, 0 equal readings, smallest gap
1166 ns. On macOS `std::random_device` draws from `arc4random` and cannot fail,
so this branch is not reached here. `docs/FATHOM.md:662-666` reads as if the
count covered instances "made at the same moment" in general; the comment in
the code is the precise one.

### 6. The tie of the two start levels

The engine puts both start levels in the same half of their ranges with
probability 0.83 and in opposite halves otherwise: 0.8295 over 83280 seeds,
correlation 0.496. `tide_phase.md` measured 0.833 and 0.535 (single sets 0.49
to 0.67); `tide_phase_verification.md` read 0.923 +- 0.019 and 0.68 +- 0.04 in
its own batches and concluded that the tie is not a constant of the reference
and that 0.5 to 0.7 is as right as the rule. The engine is the packet's rule,
at the low end of that band. Ranges, uniformity, hold and first knots are as
measured.

### 7. Loose ends outside the code

- `work/review_integration` has no INDEX file. Its export `head` was checked
  against `git show 4a0ac08:<file>` instead: 41 files, all equal.
- `emit_engine_constants.py:436` and the note it writes into
  `tide_structural_data/engine_golden.json` still name `EbbEngine.h`, a file
  that no longer exists (reported by the rename engineer; outside the folders
  of the search).
- `docs/FATHOM_VALIDATION.json` records a SHA-256 for `Tests/StateTests.cpp`
  that is not the file's any more (edited at 17:18 by the wave that followed
  the rename); the 16 other source hashes and the 21 of the campaign match.
- The behaviour engineer's report says the other Characters pass a dry signal
  "up to +18 dBFS". They bound the dry at +12 dBFS (`FDNReverb.cpp:576-577`);
  +18 dBFS is the bound of their output sum. `docs/FATHOM.md:729-735` has it
  right.

## Not done

- No new capture of the reference. Of `score_engine.py` only `holdout`,
  `plugin` and `plugin_statistics` were run; the other nine parts were not.
- AU and CLAP were not built or rendered; no DAW, validator or listening.
- The sanitized DSP run (33 minutes) was not asked for and not run; nothing was
  rebuilt for x86_64 or with `-ffp-contract=fast`.
- The plug-in of before the rename was not rebuilt. At the level of the VST3
  the rename is covered through the renderer (identical before and after) and
  the 36 renders in which the VST3 equals it at Macro 0; above Macro 0 through
  `FDNReverb` with given seeds.
- The entropy-free branch was run on a copy of the function, not in the
  plug-in's binary.
- ThreadSanitizer saw the probe's scenario only, not a host.

## Reproduction

```sh
R=Analyzer/Results/RevOceanCharacterization/work/review_behaviour/r2
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cmake --build build-ebb-review-behaviour --target AmanitaOceanDSP \
      AmanitaOceanFathomRender AmanitaOceanDSPTests AmanitaOceanStateTests \
      AmanitaOcean_VST3 --parallel
$R/dsp/build.sh mixprobe seedprobe phasestats serial && $R/dsp/mixprobe
$PY $R/proc/build_probe.py procprobe && $R/proc/procprobe life
$R/five/build.sh                      # then the six *_head and *_work programs
$R/rename/build_fdn_sixth.sh; $PY $R/rename/own_set.py
$PY $R/score/driver.py holdout plugin plugin_statistics
$PY $R/vst3/vst3_mix.py; $PY $R/vst3/two_instances.py
$PY $R/tsan/build_probe.py handoff && $R/tsan/handoff
```
