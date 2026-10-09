# Integration review: the sixth Character in the plug-in

Independent review of the uncommitted change that adds Ebb to Amanita Ocean,
against the last commit (4a0ac08). Run on 7 October 2026 on the working tree as
built in `build-ebb` (VST3 binary SHA-256 `e614cbff...02c7`, the one the
engineers scored). The source tree was not changed. Probes, logs and images are
in `Analyzer/Results/RevOceanCharacterization/work/review_integration/`.

## Verdict

No blocker. The five existing Characters are bit-identical, saved projects load
unchanged, Ebb reaches the reference through the plug-in as claimed, and the
tests are sensitive. Three things need the owner's decision before this ships
(findings 1 to 3) and one documented consequence should be confirmed (finding
4); the rest are gaps in tests and documents.

## What was run

| Check | How | Result |
| --- | --- | --- |
| Five Characters, DSP | the committed DSP sources exported with `git archive` and built with the flags of `build-ebb`; one harness against that library, a fresh compile of the working tree and `build-ebb/libAmanitaOceanDSP.a` | 96 of 96 renders bit-identical (19 cases per Character at 22.05 to 192 kHz: defaults, wet only, Freeze toggled, Mono Safe on and toggled, Harmony manual and Auto, Focus, automation of Size and Evolution and of every control, hostile input, switching among the five; plus reset and re-prepare) |
| Five Characters, processor | `AmanitaOceanAudioProcessor` built from the committed sources and from the working tree | 5 states saved by the committed build load with the same index, render the same hash (4 s, 48 kHz) and save back to the same bytes |
| Five Characters, GPU background | committed and working shaders in an offscreen OpenGL 4.1 context | 15 of 15 frames byte-identical (five Characters, three Evolution values) |
| Built VST3 on the locked holdout | `score_network.py` with the campaign's driver | -118.37, -116.35, -114.06, -118.92, -115.35, -115.18, -119.19 dB; worst -114.06, mean -116.77 dB, as documented |
| Tests | both executables from `build-ebb` | DSP 72 passed, 0 failed in 58.9 s (limit 420 s); state tests passed in 4.0 s |
| Sensitivity of the Ebb tests | 21 mutations of a scratch copy of `FDNReverb.cpp` | 15 make a test fail, 6 do not (finding 5) |

## Facts for the owner

**Host automation of Character.** The choice parameter now has six steps.

| Format | What a host stores | Effect on existing automation |
| --- | --- | --- |
| VST3 | the normalised value | 0 / 0.25 / 0.5 keep Default / Bloom / Drift; **0.75 was Veil and is now Current; 1.0 was Current and is now Ebb** |
| AU, CLAP | the index (read in the wrapper sources, not run in a host) | 0 to 4 keep their meaning; the range grows to 5 |

New normalised positions: 0, 0.2, 0.4, 0.6, 0.8, 1.0.

**State.** Saved states hold the index, so projects saved with 0 to 4 are
unaffected and 5 round-trips. A project saved with Ebb opens as Current in the
0.21.0 build, silently. An index above the range now opens as Ebb (before: as
Current); below 0, NaN or text opens as Default. Nothing crashes.

## Findings

Severity: major = audible or project-breaking and needs a decision; minor =
gap in tests or documents; note = fact worth knowing.

| # | Severity | Finding | Where |
| --- | --- | --- | --- |
| 1 | major | Existing VST3 automation of Character is remapped for Veil and Current | `Source/PluginProcessor.cpp:286-288` |
| 2 | major | Selecting Ebb below Mix 100 % raises the dry signal by up to 6.02 dB | `Source/dsp/FDNReverb.cpp:858-871` |
| 3 | major | Moving Size in Ebb adds broadband noise 50 dB above what Default adds | `Source/dsp/EbbNetwork.cpp:415-419` |
| 4 | note | Entering Ebb ends a sounding tail within 200 ms | `Source/dsp/FDNReverb.cpp:780-797` |
| 5 | minor | No test fails when Low Cut, High Damping or `reset()` do not reach the engine | `Tests/DspTests.cpp`, `Tests/StateTests.cpp` |
| 6 | minor | Low Cut and High Damping jump at their neutral ends | `Source/dsp/EbbNetwork.cpp:357-360` |
| 7 | minor | README still describes five Characters and FDN laws only; one sentence was replaced, not added | `README.md` |
| 8 | minor | Four statements of `docs/EBB.md` are not what the numbers say | `docs/EBB.md:134, 182-186, 380-382, 383-385` |
| 9 | minor | The Mix sum's bit identity depends on the compiler | `Source/dsp/FDNReverb.cpp:140-145` |
| 10 | note | Cost of Ebb when it is not selected and when it is | `Source/dsp/FDNReverb.cpp:350-353, 796` |
| 11 | note | Level of Ebb against the other Characters | - |
| 12 | note | UI: selector, accent, accessibility, backgrounds | `Source/ui/` |

### 1. VST3 automation of Character is remapped

A VST3 project with a Character lane written by 0.21.0 plays Current where it
had Veil and Ebb where it had Current. Measured on the processor built from
both trees: host value 0.75 gives index 3 before and 4 now; 1.0 gives 4 before
and 5 now; 0.5 gives 2 in both (2.5 rounds to 2). `Tests/StateTests.cpp:34-41`
was changed to the new positions, and line 437 from 0.50 to 0.40.
`docs/EBB.md:383-385` states the two changed positions.

Options: accept it and say so in the release notes; or keep the first five on
their old positions (a parameter with its own mapping, or a new parameter for
the sixth choice).

### 2. The reference's Mix law changes the dry level on a Character switch

Ebb uses dry `min(1, 2(1 - m))`, wet `min(1, 2m)`; the other Characters use
`1 - m` and `m`. Measured dry gain on a 1 kHz tone:

| Mix | Default | Ebb | Step when Ebb is selected |
| --- | --- | --- | --- |
| 20 % | 0.80 | 1.00 | +1.94 dB |
| 35 % (Ocean's default) | 0.65 | 1.00 | +3.74 dB |
| 50 % | 0.50 | 1.00 | +6.02 dB |
| 75 % | 0.25 | 0.50 | +6.02 dB |

At the factory defaults a noise burst leaves the plug-in 3.74 dB louder with
Ebb than with Default (+0.29 against -3.45 dB re the input). This follows from
the decision that Mix follows the reference; it is at Mix 100 % (the listening
point) that nothing changes. `docs/EBB.md` gives the law but not this
consequence, and the README table still calls Mix linear.

### 3. Size automation in Ebb is not smooth

The engine moves each line by whole samples while Size glides. A steady 1 kHz
tone, Size swept 80 to 120 % at 0.5 Hz, neutral controls, 48 kHz:

| | Content above 6 kHz, re the wet level |
| --- | --- |
| Ebb, Size at rest | -81.1 dB |
| Ebb, Size moving, Evolution 0 | -35.9 dB |
| Ebb, Size moving, Evolution 100 % | -42.0 dB |
| Default, Size moving | -87.4 dB |

Automation of Decay and Evolution adds nothing (-81.2 and -80.0 dB). The
behaviour is recorded as a choice in `docs/EBB.md:339-342` without a number; the
existing glide test uses a 90 Hz tone and a 60 Hz measure, which do not show
it. A fix that keeps the null at rest: read the lines with interpolation only
while Size moves, or crossfade between the old and the new whole lengths.

### 4. Entering and leaving Ebb

Decay 10 s, a burst, a switch 1.5 s later, level against not switching, in
windows of 0.5 s:

- Default to Ebb: -7.5 dB, then silence. The engine starts empty, as its
  contract says, so the previous tail is gone after the 200 ms fade.
- Ebb to Default: +2.8, +5.3, +6.4, +7.4 dB. The FDN has been running as
  Default behind Ebb and its tail takes over.

No click in either direction. The suite's own test shows every switch at
neutral settings to be the exact crossfade of two renders. On a steady
programme the largest sample step during a fade is 0.12 to 0.23, against 0.15
to 0.23 for the louder of the two Characters alone, for all five Characters, at
neutral settings and at the factory defaults. Twenty seconds of switching every 7 ms with input peak 6.0,
NaN and infinity stay finite, peak 4.96. Documented in `docs/EBB.md:365-369` and
`README.md:22-24`.

### 5. What the tests do not pin

Mutations that leave all 72 DSP tests passing:

| Mutation | Consequence if it were real |
| --- | --- |
| Low Cut never reaches the engine (`FDNReverb.cpp:125`) | the knob does nothing in Ebb |
| High Damping never reaches the engine (`:126`) | the knob does nothing in Ebb |
| `reset()` does not restart the engine (`:429`) | an old tail survives a transport reset |
| `reset()` does not restart the level stage and the Sub Anchor (`:430-431`) | a stale gain reduction after reset |
| no reset of the Sub Anchor when Ebb is left (`:793`) | none audible (1 ms filter, faded in) |
| Sub Anchor in front of the Width law instead of behind (`:832-834`) | none: the two commute, so "behind" in the test's name is not observable |

The state tests cannot catch the first four either: they set Low Cut and High
Damping to neutral or compare a build with itself (`StateTests.cpp:1813, 1870`).
The behaviour itself is correct today: with Ebb selected, `reset()` and a new
`prepare()` give the bits of a fresh instance (0 of 192,000 and 0 of 176,400
samples differ), and the engine's own test covers both filters. Missing is one
plug-in level case with Low Cut and High Damping engaged against
`EbbReferenceChain`, and one that resets after a loud passage.

Detected, among others: the output floor at 1e-20, the level stage or engine
fed under the FDN's bound of 4, Ocean's Width law, no clipper, a fused Mix sum,
Macro from the smoothed curve, an engine that does not keep time, Freeze not
forwarded. No existing assertion was weakened: the DSP test file removes ten
lines (two lists extended; the allocation test holds its toggles once per cycle
of six modes so every mode still meets both states), and no test passes
vacuously (each checks that its stimulus reached the stage under test).
Not covered by any state test: a state with an out-of-range Character.

### 6. Low Cut and High Damping at their neutral ends

They are out of the circuit only at exactly 20 Hz and 20 000 Hz and fully in it
one step away. Broadband time for 60 dB, Macro 0:

| | Decay 5 s | Decay 30 s |
| --- | --- | --- |
| neutral | 4.04 s | 13.56 s |
| Low Cut 20.065 Hz (host value 0.001; the display still reads "20 Hz") | 3.99 s | 13.00 s |
| High Damping 19 999 Hz | 3.98 s | 12.83 s |

The knob at its end stop and a typed "20" give exactly 20.0, so the A/B
settings work as written; automation near the end does not. The step is the
price of the exact neutrality that was asked for.

### 7. README.md

- The wave changed five places; the two owner passages in the diff (ad-hoc
  signing and installer, "macOS preview") are intact. Whether another owner
  edit was lost cannot be shown: no copy from before the wave exists.
- Lines 7 to 14 replace a sentence instead of adding one: "does not reproduce
  the interface, presets, **modes or algorithms** of commercial reverbs" became
  "the interface and presets", followed by the statement that Ebb is a
  structural model of a third-party commercial reverb. The old sentence would
  be false with Ebb in the tree; the owner should confirm the new wording.
- Still "five": lines 519, 600, 852, 857, 863, 864, 909.
- The parameter table (lines 579 to 591) holds for the FDN Characters only:
  Mix "linear", Decay "broadband RT60", Width, Mono Safe "alternative decoder".
- Lines 953 to 956 (SHA-256 fingerprints) were stale before this wave.

### 8. docs/EBB.md

Hashes of the reference and the Analyzer equal `revocean.py`; the validation
figures equal `score_engine.json`; the 38 file hashes of `EBB_VALIDATION.json`
equal the tree; the campaign figures equal the campaign README and SPEC.
Exceptions:

- Line 134 labels the campaign's -112.7 to -127.7 dB "quiet". SPEC section 16:
  quiet -117.7 to -127.7 dB, loud -112.7 to -123.5 dB.
- Lines 182 to 186 are headed "Campaign's 15 cases" but give the means after
  the engine's own refit of the phase (-100.29 and -100.79 dB at Macro 50 and
  25 %). With the campaign's stored curves they are -100.19 and -98.96 dB. The
  worst cases are the same.
- Lines 380 to 382: "about 3 dB below Default at Evolution 100 %" is one burst.
  On 20 s of steady noise I read 4.5 to 6.4 dB below at Evolution 100 % and 5.1
  to 5.6 dB below at Evolution 0 (Decay 2, 5 and 12 s); on a 0.3 s burst at
  Decay 5 s, 5.2, 2.1 and 5.0 dB below at Evolution 0, 35 and 100 %.
- Lines 383 to 385 do not say that an Ebb project opens as Current in 0.21.0.

### 9. The Mix sum and the compiler

`mixEbb` keeps its two products apart by writing them as separate statements.
That holds with Apple clang (a fused version fails the Mix 70 % test). The
renderer it must equal switches contraction off for its whole file
(`Tools/EbbRender.cpp:1`); `FDNReverb.cpp` cannot, because the five Characters
would change. A compiler that fuses across statements (GCC on arm64 by
default) would leave plug-in and renderer one unit in the last place apart.
Nothing but Apple clang on arm64 was built.

### 10. Cost

Share of one core, M4 Max, parameters as the processor sends them, best of five
runs of 20 s, other work running on the machine:

| Host rate | Default, last commit | Default, now | Ebb, factory defaults | Ebb, neutral |
| --- | --- | --- | --- | --- |
| 44.1 kHz | 2.72 % | 2.81 % | 4.28 % | 4.03 % |
| 48 kHz | 3.02 % | 3.06 % | 4.68 % | 4.53 % |
| 96 kHz | 5.44 % | 5.63 % | 7.42 % | 7.15 % |

- Not selected: 0.04 to 0.19 % of a core more than the last commit. Every
  instance also holds the engine's memory from `prepare()` on (2.1 MB of lines
  and 0.77 MB of pre-delay at 48 kHz, as documented). `prepare()` takes 0.037 ms
  at 48 kHz (before: 0.008 ms).
- Selected: 45 to 53 % more than Default at 44.1 and 48 kHz, 28 to 32 % at
  96 kHz, because the FDN, the harmonic analyser and Focus keep running behind
  Ebb.

### 11. Level, timing, laws, neutrality

- First output with Pre-delay 0 at 48 kHz: frame 1210 (25.2 ms); the FDN
  Characters: 1416 to 1430. The processor reports no latency.
- Width: gains equal `sqrt(2/(1+s))` and `s sqrt(2/(1+s))` to six places at 0,
  50, 100, 150, 200 %; the reference percentages 0, 58.6, 100, 129.3, 150 are
  right.
- Pre-delay: at all 2501 knob positions and six rates the plug-in's path gives
  the frame count of the reference's law.
- Neutrality from a non-neutral position: bit-identical to engine plus level
  stage from 50 ms (Focus, Width), 150 ms (Harmony with Auto on), 30 ms (Mono
  Safe), 20 ms (Mix) after the control returns, and through the whole render
  for Low Cut, High Damping and Freeze when the reference chain gets the same
  values at the same frames.

### 12. UI

Rendered at 804, 960 and 1440 px for Ebb, Bloom, Current and Default, and at
804 and 960 px from the committed build.

- The six segments fit and read at all three sizes; the selector keeps its
  width, each segment is one sixth of it. Separators and the selected label are
  in place; "TIDAL / SWELL" sits under the selector.
- Accent `#c7b773`: contrast 9.3 : 1 on the panel. Its nearest neighbour is
  Bloom (CIE76 distance 24.9); two existing pairs are closer (Default and
  Current 22.6, Drift and Veil 24.0).
- Keyboard and screen reader: the selector stays one focus stop, left and right
  wrap over six; each segment has a title, a description and help text; the
  help text is built from the names. The display name appears once in the
  processor and once in the selector and no behaviour reads it.
- The words "Tidal / Swell" and "slow tidal swell" echo the reference's mode
  name; the campaign's rule is that the reference is named in documents only.
- CPU background renders for Ebb. The test facility has no GPU render; the
  engineer's offscreen program, rebuilt against the current shader header,
  compiles and links all three programs, finds `uEbbBlend` active and renders
  Ebb (mean luma 29.9, 42.1, 46.8 at Evolution 0, 68, 100 %; Default 35.6,
  43.5, 52.1).

## Not done

- Nothing was listened to; no DAW, no validator, no host run of AU or CLAP.
- The UI was judged from snapshots, not in a window; the GPU background was not
  run through `OceanShaderBackground` itself.
- The state tests were not rebuilt under mutation (read only).
- Of the scorers only the holdout through `score_network.py` was run again.
- No sanitizer run, no Windows or Linux build.
