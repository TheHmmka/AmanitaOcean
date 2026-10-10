# Fathom: development characterisation

The sixth Character of Amanita Ocean. This document says what it is, how it
was made, how far it has been checked and what has not been checked. It is a
development record, not a product description.

## Status

A local development model in the working tree, not a release. On 7 October
2026 the owner raised the plug-in version to 0.22.0 and had this build
installed locally for listening; it is not signed for distribution or
published. The validation below was run on the build of 18:49 that day, which
carried the version string 0.21.0; raising the version changed no DSP source.
Every statement below is a measurement, not a listening result. The owner named the Character Fathom on
7 October 2026 and gave it its accent, deep blue `#2F7FE0`; until then it went
by a working name, which the campaign's findings of before that day keep.

Five independent reviews have been through it: of the engine, of the
integration, a listening review made by measurement, of the behaviour after
the owner's decisions, and of the editor. What they found is fixed, documented
here as a limit, or listed under "Decisions for the owner". The owner's
decisions so far are in this revision: the name and the accent; Ocean's own
Mix; a voice phase of its own for every instance of the plug-in; the
reference's level stage and clipper kept as the reference has them; and the
editor, a Character drop-down over the description of the selected Character
and the Evolution knob.

`build-ebb/listening-build-01.json` describes the build of this revision that
would be installed for a first listening: the three bundles with their
hashes, the sources they were built from, the tests and scores behind them
and the known limits. It is a description; nothing is installed.

**Since 0.23.0.** Fathom is in 0.22.0 and 0.23.0 as this document describes
it. One rule of Ocean's own has changed in the working tree since, on the
owner's decision of 10 October 2026: under Freeze the comb of the Tide layer
takes no new input (see "Properties and limits", Freeze). With Freeze off
every sample is that of 0.23.0, to the bit; the rest of this document stands
as it was written.

## Scope and identity

| | |
| --- | --- |
| Character | index 5 of the host parameter "Character" (the sixth choice; raw value 5, normalised 1.0); `ReverbMode::fathom` in `Source/dsp/FDNReverb.h` |
| Engine | `Source/dsp/FathomEngine.h` (interface and timing contract), `FathomEngine.cpp`, `FathomNetwork.*`, `FathomConverter.*`, `FathomEngineConstants.h` (generated) |
| Reference | Arturia Rev OCEAN 1.0.0.5848, VST3, in its Tide mode (factory preset "Cleaner Tides") |
| Reference binary | SHA-256 `eb43f0bde6dacc5e566784829bff5f9c50d1f901f8158fbbdc6b95bb94b5ad26`, and of the processor library it loads `ec29614feb7d274773d0a4d557b874b609afebdcde623c06ba43953a7c3ca433`, both as pinned in `Analyzer/Campaigns/RevOceanCharacterization/revocean.py` |
| Measuring host | Amanita Analyzer 0.3, SHA-256 `9eeb76c3a2c84961120307fc9a9d172aec133b6f867330db185a2e2d4d04b697` (pinned in the same file) |
| Campaign | `Analyzer/Campaigns/RevOceanCharacterization`; its `README.md` is the source of truth for the measurements, `SPEC.md` for the model |
| Editor | `Source/ui/CharacterSelector.*` (the drop-down and the description block with its one table of texts), `PluginEditor.*` (the composition), `OceanLookAndFeel.*`, `ParameterKnob.*`; see "The editor" |

The reference was measured as a black box: rendered through its public host
interface in its normal demo mode, from input and output alone. Its code was
never disassembled or inspected, its files were not read (they are hashed for
identity only, by the campaign's capture function), and its licensing was not
worked around. The five other Characters are Amanita Ocean's own algorithms
and have nothing to do with the reference.

## What the Character is

A structural model of the reference with every control of the reference other
than Decay, Size, Pre-delay, Width and Macro fixed: at neutral (Brightness
0 %, input filter open at 20 Hz and 20 kHz, Transients 0 dB, Ducking 0 %,
Return and Master 0 dB) and at Mix 100 %. It is driven by Ocean's existing
controls only.

| Ocean control | In Fathom | Law |
| --- | --- | --- |
| Decay, 0.2 to 30 s | the reference's Decay | 60 dB per Decay seconds of line length. Measured from 0.5 s up; below 0.5 s the attenuation law continues and the output tap weights stay at their 0.5 s values (the engine's choice) |
| Size, 0 to 200 % | the reference's Size | line length `floorf(fl(P s) + 0.5f)` with `s` Ocean's scale: the percentage / 100 from 50 % up, Ocean's compact curve below (0.15 at 0 %). The reference's range is 0.3 to 2; below 0.3 the same laws are extrapolated |
| Pre-delay, 0 to 250 ms | the reference's Pre-delay | `floor(P fs / 1000) - 1` whole frames in front of the engine, none below two frames; the dry signal is not delayed |
| Width, 0 to 200 % | the reference's Width | mid/side on the wet, mid gain `sqrt(2 / (1 + s))`, side gain `s` times that, `s` = Width / 100. Ocean's 200 % is the reference's maximum of 150 %; at 100 % the wet passes as it is |
| Mix, 0 to 100 % | Ocean's own | dry `1 - m`, wet `m`, the sum every Character leaves through; at 100 % the wet passes as it is. The reference's law (dry `min(1, 2 (1 - m))`, wet `min(1, 2 m)`) is not used by the plug-in: the two meet at 0 and at 100 % only (see "Properties of the integration") |
| Evolution, 0 to 100 % | the reference's Macro | the knob itself, not the smoothed curve the other Characters use |
| Low Cut, High Damping | Ocean's own, inside the loop | exactly out of the circuit at their end stops, 20 Hz and 20 kHz, and fully in it one step away |
| Focus, Harmony | Ocean's own, on the wet | exactly out of the circuit at 0 % |
| Mono Safe | Ocean's own | on Fathom only the 145 Hz Sub Anchor, behind the Width law; off by default |
| Freeze | Ocean's own hold | not the reference's Freeze, which was not measured. Held, the network takes no input, and since 10 October 2026 neither does the comb of the Tide layer, by the same glide of 50 ms; in 0.22.0 and 0.23.0 the comb went on taking it |

Two things of the reference that have no knob in Ocean are part of Fathom:

- **The level stage.** The reference turns its wet down while an input sample
  exceeds 0.5629 (-4.99 dBFS) although its Ducking reads 0 %: a soft-knee
  compressor on the wet keyed by the raw input, slope 5/7, 5 ms attack, 300 ms
  release. Fathom keeps it, so loud input ducks the tail as it does in the
  reference.
- **The output clipper.** Unity up to +8 dBFS, a quadratic knee, a ceiling of
  +12 dBFS, on dry and wet together, behind the Mix.

The owner has decided to keep both as the reference has them (7 October 2026).

With Ocean's own controls neutral, Width 100 % and Mix 100 % the plug-in's
output is the engine's wet through the level stage, bit for bit.

Ocean's defaults are not neutral. A fresh instance with Fathom selected differs
from the reference at its neutral baseline in six controls: Mix 35 %,
Pre-delay 20 ms, Low Cut 80 Hz, High Damping 9 kHz, Evolution 35 % and Focus
100 %. Three of them are Ocean's own and change the sound far beyond the
reference's own spread between instances (see "A/B comparison").

## Structure in brief

```
in -- pre-delay -- converter -- equaliser -- cos/sin(90 deg x Macro) mix with a comb -- 44 samples
   -- 2 x 16 modulated lines closed through a Hadamard matrix and a loop filter
   -- per output: lines 1-8 -> voice A, lines 9-16 -> voice B -- converter -- wet
shaped = width( level stage(in) * wet )
out    = clip( in + mix * (shaped - in) ),  at Mix 100 %: clip( shaped )
```

- The core between the two converters runs at 44.1 kHz at every host rate. The
  converters are Kaiser-windowed sinc filters read from one table without
  interpolation; at a 44.1 kHz host there are none.
- Two groups of sixteen delay lines, one per output, of prime lengths (1031 to
  7589 samples), each read at a length that a 0.6 Hz single-precision
  oscillator moves by 0.88 ms.
- The Tide layer acts above Macro 0: a comb per input whose delay follows a
  200 s triangle from 4.43 to 15.13 ms, and two voices per output (a two-pole
  low-pass with a gain, set every 44 samples) driven by one phase per output.
  That phase is a ramp plus slow value noise. The reference draws it at random
  per instance, and so does the plug-in: every instance draws a 64-bit seed
  for it when it is created. The engine on its own (the tests, the renderer)
  starts from a fixed default seed.

`Analyzer/Campaigns/RevOceanCharacterization/SPEC.md` is the full
specification in processing order with every constant and every place where
single precision matters; `reference_render.py` is its executable form.

Inside the plug-in (`Source/dsp/FDNReverb.cpp`) Fathom is a second network
beside the FDN. It is fed by the dry input, its wet replaces the decoded FDN
wet in front of Harmony, the reference's Width law takes the place of Ocean's,
the Mix is Ocean's, and the reference's clipper follows it. While another
Character is selected the engine only keeps its clocks running.

## The editor

Decided by the owner on a mock-up and on rendered pictures (7 October 2026).
Numbers are design pixels of the 960 x 640 canvas, whose header rule lies at
80, footer rule at 484 and vertical axis at 480.

- **No word "Character" is drawn**: no label over the drop-down and no index
  line. Assistive technology is told "Reverb character" as the drop-down's
  title.
- **The drop-down**, 340 x 40, on the axis, 32 under the header rule (310,
  112). The arrow keys step through the six Characters and wrap, one host
  gesture each; Return or Space opens the list, which opens under its field at
  the field's width. Only the bare keys do: with a modifier held they are
  passed on to the host. While the list is open the keys are the list's, and
  Escape leaves the Character as it was.
- **The description block** of the selected Character, 248 wide, left of the
  axis: the name (20 pt bold), a subtitle, a hairline, a paragraph. The six
  texts are one table in `CharacterSelector.cpp`, the wording the owner
  approved; the state tests hold them character for character. The text is
  Ocean's three neutral tones and never the accent.
- **The Evolution knob** right of the axis, its dial centred on (614, 296),
  its ring 180 across, its label and value under it.
- **The mirror.** The block ends as far left of the axis as the ring begins
  right of it, 44. At every editor size the block takes the whole pixel
  nearest to the mirror image of the ring's left edge as the knob is laid out.
- **The vertical centre.** The middle of the block's text, from the top of
  the name's capitals to the baseline of the paragraph's last line, lies on
  the middle of the whole Evolution control, from the top of its ring to the
  baseline of its value (206.3 to 430.8, middle 318.5).
- **The accent** of the selected Character (Fathom: deep blue `#2F7FE0`)
  colours the word OCEAN of the title, the arcs of the knobs, the border of
  the drop-down while it has the keyboard or its list is open, the highlight
  of the list and the label of a toggle that is on. The text on the
  highlighted list item is Ocean's darkest tone or white, whichever stands
  out more: white for Fathom.
- **The background** gathers round the Evolution dial and lies low behind the
  block (the calm region of the GPU shader), wherever a size has put them.
- **The keyboard** walks, by Tab, from the drop-down through the Evolution
  dial and its value and the nine dials of the lower row, each with its
  value, to Mono Safe and Freeze and back to the drop-down, and by Shift+Tab
  the same way back. A value opens for typing when it is tabbed to; Return in
  it ends the edit on its dial.

Measured by the state tests at 804, 960 and 1440 wide: axis to block 37 / 44
/ 67 px and axis to ring 37 / 44 / 67 px; ring 150 / 180 / 269 px across; 45
/ 54 / 81 px of air between the drop-down and the ring; 39.35 / 47 / 71 px
between the value's field and the footer rule; the middle of the text
-0.0625, +0.0625 and +0.0625 to +0.125 px from the middle of the control for
the six Characters, read at eight pixels per point. At five widths in between
(852, 900, 1026, 1200, 1338) block and ring are 39 and 39.5, 41 and 40.875,
48 and 47.75, 56 and 56.125, 62 and 62.125 px from the axis. Every paragraph
takes four lines; the last line is 35 to 98 % of the block's width.

Contrast of the block (WCAG ratio): on Ocean's plain background name 15.2,
subtitle 5.9, paragraph 10.3 : 1 (state tests). Over the GPU field the editor
review read, as the lowest value at any glyph pixel in 3072 frames, name
14.01, subtitle 5.48, paragraph 9.49 : 1 with the calm region and 1.79, 1.00,
1.14 : 1 without it; over the CPU fallback 13.45, 5.40 and 9.04 : 1. Those
readings stand: the block's place and the shader have not changed since,
except that at 1440 wide the block now sits one point further left.

The editor review (`findings/editor_review.md`) found no blocker. What was
changed for it in this revision:

- **Tab never got past the Evolution value** (major, older than this work). A
  value that was left handed the keyboard to its dial one message later
  whoever held it by then, which pulled the focus back from the control Tab
  had moved to. The dial now takes the keyboard only if no other control has
  it.
- **Tab went from the drop-down to Mono Safe and Freeze first.** Focus orders
  rank the children of one parent, and the knobs' orders were set on their
  dials. Each knob now carries its order itself.
- **Left and Right changed the Character behind its open list**, and Escape
  then did not cancel. The drop-down leaves the Character alone while its
  list is open.
- **Arrows and Space were taken with any modifier held.** Only the bare keys
  are taken now.
- **A help text that named the six Characters reached no assistive
  technology** (a combo box reports its tooltip as help). It is removed; the
  state tests hold what assistive technology does receive: the drop-down's
  role, title and description, the Character it shows, the block's text and
  the names of the six list items.
- **No test held the approved wording or the block's colours.** Both are
  pinned now.
- **The widow control of the paragraph** runs for none of the six texts at
  the final width with the macOS font; it is kept for other fonts and texts
  and has a test of its own now.
- **The mirror was set from design constants**, which left up to 1.2 design
  pixels at sizes between the pinned ones and 0.6 at 1440 wide. It is taken
  from the knob as laid out now.
- Removed as unused: the look-and-feel's font and colour for menu section
  headers, and a 35 pt start value of the Evolution value's font.

The reviewer's own probe, built against this revision, walks all 34 of its
Tab presses control by control, leaves the Character alone under Right,
Right, Left with the list open, and takes none of the six modified keys it
tries. Fifteen faults put into the editor's sources one at a time, among them
each of these defects as the review found it, make the state tests fail. The
findings that are the owner's to decide are entries 13 to 17 of "Decisions
for the owner".

Final pictures of this revision are in `build-ebb/ui/final`: the six
Characters at the default size without a window (the CPU fallback) and live
over the GPU background, and the open list. At 804 and 960 wide the pictures
without a window are byte for byte those of the wave before; at 1440 wide the
block's text stands one point further left.

## Method

1. **Measurement.** Stimuli are rendered through the reference by the Amanita
   Analyzer (a fresh instance per capture, all fifteen host parameters set, a
   warm-up of silence first, 10 s as a rule). At Macro 0 every capture is
   rendered twice and kept only if both renders are the same bits.
2. **Model.** Packets of the campaign recovered the structure and its constants
   from those captures; each packet was checked by an independent verification.
   A constant is fitted freely first and replaced by a round value when it
   lands on one. Locked holdouts are read by the scorers only.
3. **Port.** The C++ engine implements `SPEC.md` behind `FathomEngine.h` and is
   scored by `score_engine.py`: against the reference on captures, and against
   the Python model where no capture can be taken.
4. **Plug-in.** `render_candidate.py` renders the built VST3 through the same
   Analyzer with every host parameter set, and `score_engine.py plugin` scores
   that output against the reference and against the engine's own renderer
   (`Tools/FathomRender.cpp`). Every render is a new instance with a voice seed
   of its own: at Macro 0, where no voice moves, the plug-in must equal the
   renderer to the bit; above it `score_engine.py plugin_statistics` scores
   an ensemble of instances against the reference's realisations.
5. **Review.** Five independent reviews of the result, each with stimuli,
   captures or probes of its own: `findings/engine_review.md`,
   `integration_review.md`, `listening_review.md`, `behaviour_review.md` and
   `editor_review.md` of the campaign.

A null is `20 log10(rms(candidate - reference) / rms(reference))` with no
gain, delay or polarity fitted. Above Macro 0 the phase of the voices is the
one thing that is fitted, because the reference draws it at random.

## Validation

Figures of the campaign are from its `README.md` and `SPEC.md`; figures of the
engine and of the plug-in are from `score_engine.py`, run in full on 7 October
2026 with the final build of this revision (Release, arm64, Apple clang 21,
macOS 26.7; VST3 binary SHA-256 `cf73a490...e54b`, renderer
`3b9a5af3...078b`). `docs/FATHOM_VALIDATION.json` holds the figures
in machine-readable form.

Since the reviews the engine's sources have changed in comments, in the
rename and in two entries of a table (the clock signs at 64 and 384 kHz,
below), and in no arithmetic. Every figure of the engine in this run equals
the run of before the rename leaf by leaf: 4292 figures in ten parts of the
report, with five labels that read "measured" where they read "simulated".
So do the 628 figures of the plug-in at Macro 0, and the DSP executable
prints the report of before line for line, every measured figure included.
The plug-in's figures above Macro 0 are of the instances a run draws.

### Macro 0: whole responses against the reference

| Evidence | Campaign model | Engine renderer | Built VST3 |
| --- | --- | --- | --- |
| Locked holdout, 7 captures (worst / mean) | -114.07 / -116.79 dB | -114.06 / -116.78 dB | -114.06 / -116.77 dB |
| Fresh captures at 44.1, 48, 88.2, 96 kHz | 46 captures: -108 to -121 dB | 27 captures: -111.98 to -120.80 dB | 6 captures: -113.04 to -119.37 dB |
| Pre-delay, Width and Mix moved, input peak at or below 0.5 | whole chain -117.7 to -127.7 dB | 8 captures: -113.40 to -128.40 dB | 2 captures with Pre-delay and Width moved, Mix 100 %: -115.10 and -116.32 dB |
| Input peak 6.0: level stage and clipper working | whole chain -112.7 to -123.5 dB | 7 captures: -112.73 to -126.53 dB | 2 captures at Mix 100 %: -114.11 and -112.57 dB |
| 64 and 384 kHz, whose clock signs joined the measured table in this revision | -117.47 and -118.78 dB | -117.46 and -118.77 dB | not rendered |

- The holdout was scored through `score_network.py` with the plug-in's knobs
  at the positions nearest to what the reference works with: Decay within
  1.6e-7, Size within 1.6e-7 (relative), all 32 line lengths equal in every
  case. Per case: -118.37, -116.35, -114.06, -118.92, -115.35, -115.18,
  -119.19 dB.
- The plug-in's own captures (programme seed 16180) use knob positions at which
  plug-in and reference work with the same single-precision numbers. On all
  ten the campaign model scores within 0.01 dB of the plug-in.
- All ten are at Mix 100 %, where plug-in and reference follow the same laws.
  The reference's Mix law is scored on the engine's renderer (`--mix`, the
  middle column). The plug-in's Mix below 100 % is Ocean's and is checked
  against the renderer and against its own law, not against the reference.
- Late in a response the null is shallower: the stretch from 6 to 14 s of the
  holdout reads -101.4 to -104.3 dB for the engine and -100.7 to -104.2 dB for
  the plug-in, whose knob positions are up to 1.6e-7 off. One stretch of
  the engine's shell captures reads -89.30 dB (96 kHz, Pre-delay 250 ms, Size
  140 %, 0.25 to 1.4 s); the model reads the same there.
- At 64 and 384 kHz the clock signs were not among the campaign's six
  measured pairs; the two captures are the engine review's. All nine pairs of
  signs were tried on each with `reference_render.py`: at 64 kHz (low, low)
  reads -117.47 dB and the next best pair -90.63 dB; at 384 kHz (high, low)
  reads -118.78 dB and the next best -98.15 dB; both clocks taken as exact,
  as the engine had them at first, read -77.36 and -77.57 dB. They are the
  pairs the campaign's simulation gives. Both joined the measured table
  (`converters.MEASURED_CLOCK_SIGNS`, `engine_constants.json`,
  `FathomEngineConstants.h`), which changed no output: 78 renders of the
  renderer at 13 host rates from 22.05 to 384 kHz are the same bytes before
  and after.
- At 176.4 kHz there is no whole-response capture; the evidence is half a
  second of impulses in the golden vectors (model against reference -109.3 dB,
  and -108.0 dB at 192 kHz) and the engine against the model.
- The engine review's four captures of its own, read again with the present
  engine: -116.10 dB for a whole response at 192 kHz, and -112.53, -119.33 and
  -121.87 dB at 48, 96 and 44.1 kHz on Sizes where the single-precision length
  rule and double precision put a line on different whole lengths, the last
  through the whole chain with an input peak of 3.0. The model reads within
  0.01 dB of each.

### The port against the model

| Evidence | Result |
| --- | --- |
| Engine against `reference_render.py`, Macro 0, 18 whole responses at host rates from 22.05 to 384 kHz | -143.15 to -149.11 dB |
| The same on the 42 captures above | -144.17 to -151.01 dB |
| The same at 64 and 384 kHz with their measured clock signs and at 128 kHz with the simulated ones | -148.00, -147.96 and -147.99 dB |
| Above Macro 0 with the same phase in both, 26 captures | -145.10 to -149.60 dB |
| Engine running free from a seed against the model driven by a Python form of the engine's phase generator, 18 responses, 22.05 to 192 kHz, Macro 3 to 100 % | -145.22 to -149.47 dB |
| Pre-delay law at every position of Ocean's knob (2501) and of the reference's (2001), six rates | 0 positions a frame off |
| Clock signs of the engine against `converters.clock_signs`, 432 host rates from 22.05 to 384 kHz | 0 rates differ |

Where the rounding of the converter clocks was not measured, model and engine
now take the same simulated signs; before, the model was told to take them as
exact, as the engine did.

### The plug-in against the engine's renderer

At Macro 0 no voice moves and every instance returns the same samples. There
the VST3 rendered through the Analyzer and `AmanitaOceanFathomRender` given the
numbers `render_candidate.physical` derives from the host values produced the
same bits in all 36 renders compared: the 7 holdout cases, the 10 captures
above, 6 renders below Mix 100 % (Mix 0, 10, 30, 35, 60 and 70 %, two of them
with an input peak of 6.0), for which the renderer applies Ocean's Mix
(`--ocean-mix`), and the 13 statistics stimuli of up to 80 s rendered at
Evolution 0. That covers 44.1, 48, 88.2 and 96 kHz, quiet and loud input, and
Size above and below 50 %.

Above Macro 0 the instance's seed decides, and no renderer knows the seed an
instance drew, so the built VST3 cannot be compared with the renderer to the
bit there. What carries the engine's scores to the plug-in is the state test
that hands an engine the seed the processor reports: at 44.1, 48, 88.2 and
96 kHz and Evolution 100 % the processor returns that engine's wet through
the level stage bit for bit from the first frame, and so does `FDNReverb`
against an engine of the seed it is handed. Through the Analyzer, four
settings at Macro 25, 50 and 100 % were rendered twice: the two renders differ
in 93.3 to 96.5 % of their samples, and the second lies -1.7 to -12.5 dB from
the first (`rms(second - first) / rms(first)`; +1.4 to -17.1 dB over the runs
of the scorer, each with new instances).

Below Mix 100 % the plug-in's output holds the shares the linear law names:
by least squares over each of the six renders, dry and wet shares within
1.4e-9 of `1 - m` and `m`, and no sample further than 1.2e-7 from
`(1 - m) dry + m wet` under the clipper's threshold (two samples of the six
renders lie above it, at Mix 10 %). The sum is Ocean's own expression,
`dry + mix (wet - dry)`, shared with the five other Characters: the arm64
build fuses its product into the addition and an x86_64 build rounds it
first, so below Mix 100 % the last bit of the output depends on the build, as
it does for the other Characters. The renderer's `--ocean-mix` repeats the
expression in its own file, so the identity below Mix 100 % holds while both
files are compiled alike; the DSP tests read from Default how a build forms
the sum and pass with `FDNReverb.cpp` alone compiled with contraction off or
fast. At Mix 100 % no sum is taken. The
reference's Mix law stays in the engine (`FathomEngine::mixGains`,
`FathomEngine::mix`, compiled without contraction) for the renderer's `--mix`
and the scoring of the outer laws; the plug-in no longer calls it.

### Above Macro 0: the phase of a capture given to the engine

The plug-in takes no phase, so these are engine results (test hook
`setVoicePhaseForTesting`). The plug-in is that engine with the seed of its
instance, as the state test above shows.

| Macro | Campaign's 15 cases, the campaign model with its stored curves (worst / mean) | The same cases, the engine after two steps of its own phase fit (worst / mean) | 11 fresh captures at four host rates, the engine |
| --- | --- | --- | --- |
| 100 % | -88.78 / -98.50 dB (8 cases) | -88.78 / -98.50 dB | -98.94 to -107.95 dB (5) |
| 50 % | -91.62 / -100.19 dB (4) | -91.62 / -100.29 dB | -103.45 to -105.09 dB (3) |
| 25 % | -96.10 / -98.96 dB (3) | -96.09 / -100.79 dB | -102.82 to -107.06 dB (3) |

The campaign model given the engine's final phase scores within 0.02 dB of
the engine in every case; the means of the two columns differ because the
engine's own fit moves the phase of two cases (Macro 50 %: -101.71 to
-102.14 dB; Macro 25 %: -99.99 to -105.50 dB).

### Macro 100 %, Mix 100 %: running free

`descriptors.compare_to_targets` on the 13 target cases of Macro 100 % (10
impulse cases, 3 noise cases), as the root mean square of the difference from
the reference's mean in units of the reference's own spread.

| | Built VST3, 8 instances | Engine, 8 seeds | Reference against itself |
| --- | --- | --- | --- |
| 20 impulse descriptors (decay times, levels, spectrum, echo density, coherence) | 0.18 to 0.45 | 0.14 to 0.40 | 0.28 to 0.48 |
| Level fluctuation under noise | 1.51 | 1.63 | 1.51 |
| Modulation rates | 1.00 | 1.04 | 1.47 |
| Time variance | 0.18 | 0.31 | 0.43 |

Every case was rendered through the Analyzer eight times, each time by a new
instance of the plug-in with a seed of its own, and the eight renders were
scored together, as the engine's eight seeds are. The scorer's own guide for
"another draw of the reference" is 0.3 to 0.9. In errors of the mean the
plug-in's largest value is 1.11 (level fluctuation under noise; the engine
with 8 seeds: 1.39), every other descriptor is at or below 0.88. The figures
move with the seeds the instances draw: eight other draws of eight instances
per case (earlier runs of this scorer and of `score_tide.py
render_candidate.py --statistics --seeds 8` on this build and the builds
before, and the behaviour review's) read 0.14 to 0.46 on the impulse
descriptors, 1.20 to 1.61 on the fluctuation, 0.87 to 1.08 on the modulation
rates and 0.12 to 0.33 on the time variance.

The eight renders of a case all differ, in each of the 13 cases; another
instance lies -2.2 to +1.0 dB from the first (`rms(other - first) /
rms(first)`), about where two instances of the reference lie from each other.
Rendered once more at Evolution 0, each of the 13 cases equals the renderer
to the bit.

Level cycle under steady noise at Macro 100 % (engine, 40 seeds, against 8
reference realisations after a 10 s warm-up; reference / engine): floor
-36.57 / -36.60 dB, top -31.67 / -31.63 dB, swing 4.90 / 4.97 dB, mean power
-33.60 / -33.61 dB, period 16.9 / 17.5 s; the largest difference is 1.49
standard errors. Macro 0 level for the same noise: -32.363 dB for both.

At Macro 50 % the engine reads 0.14 to 1.57 on the impulse descriptors and
7.17 on level times (campaign model: 0.15 to 1.15 and 5.09); the campaign
attributes the large entries to two cases whose targets hold a single render.
The plug-in was not scored at Macro 50 %.

### The listening review

`findings/listening_review.md`: the built VST3 against the reference at
Evolution = Macro = 100 %, Mix 100 %, neutral otherwise, with stimuli and
descriptors of the reviewer's own and 213 fresh reference captures, 6 to 24
realisations per case. Nothing was listened to by ear. Its results:

- Over 16 cases (four Decay and Size settings, 44.1, 48 and 96 kHz, Macro 25 to
  100 %, instance ages up to 165 s, up to 805 descriptors per case) the
  engine's seeds cannot be told from the reference's realisations:
  label-permutation p between 0.20 and 1.00 in every case.
- Mean level -0.015 dB from the reference's (120 seeds against 10
  realisations); long-term third-octave spectrum within 0.16 dB; level swing
  above 2 kHz 6.35 against 6.39 dB; swell spacing 8.54 against 8.44 s; decay
  times (T30) within 2.5 % rms.
- At Evolution 0 the plug-in nulls against the reference at -110.67 dB on a
  58 s programme, at -117.88 dB with Mix at 35 % on both and at -110.54 dB
  with Pre-delay 20 ms on both. The second figure is of the build the review
  judged, whose Mix followed the reference; with Ocean's Mix the two null at
  Mix 100 % only.
- One render of the plug-in is as far from one reference instance as two
  reference instances are from each other: above 2 kHz the level of a channel
  differs by 2.70 dB rms moment by moment (reference against itself 2.77 dB),
  up to 7.7 dB at single moments. The swells do not fall at the same times.
- Open: a possible excess of half a percent in the decay time below 1 kHz
  (pooled over nine cases +0.50 %, 3.5 standard errors; a test built for it
  reads +0.5 +- 0.5 %), and one reading at 4 kHz, Decay 5 s, Size 100 % of
  +5.8 and +6.5 % in two tests (1.9 and 1.5 standard errors, in a band where
  the reference itself spreads by 11 %). Neither was measured again here.

The review judged an earlier build (VST3 `e614cbff...02c7`), in which every
instance ran from the engine's default seed and Mix followed the reference. It
used 44.1, 48 and 96 kHz with every control at rest and Mix at 100 %, where
the present engine returns the same samples: 25 renders of the renderer of
that build and of the one that followed the reviews (17.7 million samples at
six host rates, Macro 0 to 100 %, with and without the outer laws and Ocean's
loop filters) are the same bits wherever a sample lies above 1.3e-23, and
differ by at most 7e-29 below; the engine's sources have not changed since.
Its statistics were taken over the engine's seeds, which is what the
instances of the plug-in now are.

### The behaviour review

`findings/behaviour_review.md`: Ocean's Mix, the voice seed and the rename,
with probes of its own at three levels (`FDNReverb`, the processor, the built
VST3 through the Analyzer) and a ThreadSanitizer build. No blocker and no
major finding. Its results:

- Mix: at Mix 0, 10, 35, 50, 70, 90 and 100 % the dry signal of Fathom equals
  that of each of the five other Characters in every sample, at all three
  levels, and the whole output equals the sum `dry + m (wet - dry)` as the
  build forms it; 30 switches among the six Characters with the wet silent
  leave no sample different from Default held.
- Seed: 133,280 seeds drawn by instances in loops, on eight threads at once
  and in 640 processes started 64 at a time, none twice; the seed is not in
  the saved state as text or as binary; it survives `prepareToPlay()`,
  `reset()` and a restored state; ThreadSanitizer reports nothing while one
  instance is created, prepared and processed on three threads (324 reports
  for a deliberate race, as a control).
- The start levels of the voice phase over 83,280 drawn seeds lie in the
  measured ranges, uniform, in the same half of both ranges in 82.95 % of the
  seeds (campaign 83.3 %).
- The rename changed no sample: 85 renders of its own and 323 cases through
  `FDNReverb` against the sources of before.

Its one finding about the sound, the clipper on the dry signal above +8 dBFS,
is the owner's decision 2 below. Its stale texts are corrected in this
revision: two comments of `FathomEngine.h` and a line of the project's
`README.md` gave the plug-in the reference's Mix law, and this document
quoted selector words that are gone. Its notes are in "Properties and limits"
(the dry signal under 1e-20, the seed without system entropy) and in entry 16
of "Decisions for the owner" (the working name in four numbers).

### Tests

`ctest` in `build-ebb` (Release): 2 of 2 passed, DSP 52.28 s, state 4.74 s
(while the sanitized DSP run used a core). The DSP executable reports 77
passed, 0 failed. Of these, 25 are about Fathom; those at plug-in level check:

(Since 0.23.0 a twenty-sixth, "Fathom under Freeze takes no new input", holds
the rule of the comb under Freeze; it is described behind this list.)

- at 44.1, 48, 88.2 and 96 kHz and at Evolution 0 and 100 %, with Ocean's own
  controls neutral, the output equals the engine's wet through the level
  stage bit for bit, also from host values through the processor, from the
  first frame of an instance that is prepared with Fathom selected;
- Low Cut and High Damping, engaged from the start and moved while sound
  plays, give the bits of an engine that holds the same filters, and change
  the tail against the open loop; `reset()` in a loud passage, with the level
  stage turned down and Mono Safe on, leaves a fresh instance;
- Width at 0, 50 and 200 %, bit for bit against the reference's law; Mix at 0,
  25, 30, 35, 50, 70 and 75 %, bit for bit against Ocean's sum of the dry
  signal and the engine's wet as the build forms it for Default, and within
  5e-7 of `(1 - m) dry + m wet`; the clipper with an input peak of 5.9 at Mix
  0, 20 and 100 %, in its knee for 296, 242 and 309 frames. Below Mix 100 %
  the dry signal enters under the bound of 4 that the FDN's input observes,
  so the loudest sample at Mix 0 % is `clip(4)` = 3.623 and the ceiling of the
  clipper is not reached; the ceiling itself is held by the engine's test of
  the clipper;
- until the first wet arrives (Pre-delay 250 ms), Fathom, each of the five
  other Characters and a switch between the two in either direction return the
  same bits at Mix 0, 20, 35, 50, 75 and 100 %: the dry signal times `1 - m`;
- a voice seed handed to `FDNReverb` is the seed of its engine from the next
  `prepare()` or `reset()` on, bit for bit against an engine of that seed, and
  not before; `reset()` starts the same phase again; at Evolution 0 and in
  the five other Characters two seeds give the same bits;
- every switch between Fathom and the five other Characters, in both directions
  with tails sounding, is the 200 ms linear crossfade of two renders that
  exist on their own, within 2.3e-8 at peaks of 0.12 to 0.18, and bit for bit
  the new Character alone afterwards;
- a return to Fathom after its fade has ended starts from an engine that has
  only kept time (nothing of the earlier visit); a return inside the fade keeps
  the tail that was sounding;
- Focus, Harmony and Mono Safe change the output while engaged and are out of
  the circuit bit for bit once back at neutral; Freeze holds and releases;
- a state saved with Fathom selected opens in another instance with the saved
  settings and that instance's own seed: bit for bit what the DSP renders
  from both, another realisation than the saved instance's at Evolution 80 %,
  and the same bits at Evolution 0;
- two instances with the same state differ in Fathom at Evolution 100 % and
  return the same bits in Fathom at Evolution 0 and in each of the five other
  Characters; one instance returns the same bits again after processing was
  stopped and prepared anew and after its own state was loaded back; two
  instances with the same settings save the same bytes, which hold the
  thirteen parameters and nothing else;
- the seeds of 1000 instances made one after another are all different, and
  the voice phases they give start inside the ranges the campaign measured,
  spread over them, in the same half of both ranges in 83 % of the instances
  (four runs: 80.5 to 83.7 %); the engine's own test reads the same over
  three families of 4000 seeds, among them seeds spread over all 64 bits and
  seeds that differ in their upper half alone;
- a state whose Character is no choice of this build opens as the last choice
  (a number past it) or as Default (a negative number, text) and renders; the
  tail reported to the host covers Fathom's decay; processing allocates
  nothing.

Since 0.23.0, in `build-spume-dsp` (Release, arm64), where the DSP executable
reports 102 passed and 0 failed in 94 s: "Fathom under Freeze takes no new
input" holds the rule of the comb. At Evolution 100 %, 48 kHz and a Freeze of
five seconds, a burst of a quarter second played wholly inside the Freeze
leaves the output as it is without the burst, to the bit, from the first
frame to the last, whether it ends 1.05 s or 50 ms in front of the release,
in the engine and through `FDNReverb`. A sound that began before Freeze and
went on two seconds into it is, to the bit, that sound ended where the
hold's glide ends. A burst played under Freeze at Evolution 0, with Evolution
raised to 100 % at the release, leaves the output as it is without the burst.
At Evolution 0 Undertow and Spume are Fathom to the bit through a Freeze and
across both its edges at 44.1, 48 and 96 kHz. An input that the equaliser
turns into an impulse, placed on the hold's glide behind the release, gives
the engine that never held, given that input by the share of its sample, to
-142.9 dB, and by the share of the next sample -66.8 dB: the comb's share is
the lines' own to the sample. The comb lies inside the network, so the click
measure of the two layers has no place to be taken; at the engine's output
the largest second difference of the wet of two steady tones through a hold
of four seconds is 0.00102, against 0.00107 for the same tones with no hold.
Six faults put into a copy of the network one at a time, outside the tree
(the comb as in 0.23.0, its share taken at once at either end of the glide,
the share of the sample before and of the sample behind, the comb muted at
its output in place of its input), each make the test fail. Under the
address and undefined-behaviour sanitizers (`build-spume-sanitize`, Debug)
the three Freeze tests of Fathom, Undertow and Spume (`--test-freeze`) pass
with no report, in 8 min 40 s; Fathom's other tests were not run there again.

Eight of the tests of 0.23.0 put Fathom under Freeze ("Fathom engine
allocates only in prepare", "Fathom engine determinism, clocks and parameters
at rest", "Fathom engine silence, hostile input and sample rates", "Fathom
Freeze hold", "Fathom under Focus, Harmony, Mono Safe and Freeze", "Fathom
parameter glides", "Fathom sample rates and stability", "Fathom Tide layer
under hostile input and Macro automation"). All pass unchanged. One of their figures moved: the
wet peak of the last of them at 48 kHz, under hostile input with Freeze
switched every few milliseconds, from 5.41061 to 5.36158 (its limit is the
clipper's range). Every other `[METRIC]` line of the 77 tests of this
document, and of the twelve that came with Undertow, is that of 0.23.0,
character for character: the figures of "Fathom Freeze hold", of the Freeze
glide and of Freeze in the plug-in among them, whose input is silent under
Freeze. Forty-two renders of the renderer, 30 of them with the Tide layer
at three rates, Macro 0 to 100 %, with and without the outer laws, the others
with Undertow's, are the same bytes as from the renderer of 0.23.0; the
renderer has no Freeze, so every score that goes through it stands. `score_engine.py` itself was not
run again: it reads the reference's files to check its pins, which this
work may not.

The state tests of the editor check, besides what "The editor" lists: the
bounds of drop-down, block and knob at the default size (310, 112, 340 x 40;
188, 239, 248 x 160; 510, 199, 208 x 238); that no control overlaps another
or leaves the window at three sizes; that all six texts fit their block
unshortened; that the editor paints nothing of its own between the header
rule and the drop-down; the accent in title, ring, engaged border (3 : 1 or
more against its field) and list highlight (text at 4.5 : 1 or more) for six
Characters and through a morph; the CPU background gathered round the dial;
and, in a window of its own, what assistive technology is told and the walk
of the keyboard: 49 keys, once round forwards, once round backwards, then
into the Evolution value and Return. The walk needs the keyboard focus of a
window; on a desktop that gives the test's window none it is skipped with a
line that says so. It ran in every run reported here.

For the editor review the state tests grew from 303 to 327 assertions, and one
was replaced: the pin of the drop-down's help text, a string no assistive
technology received, gave way to the pins of what it does receive. Fifteen
faults put into the editor's sources one at a time (the defects as the review
found them, a changed word of a subtitle and of a paragraph, each text tone
changed, the widow control removed or run needlessly, the mirror taken from the
design constants) each make the state tests fail; the tests as the review found
them pass with a changed word in a subtitle and in a paragraph and with the
subtitle dimmed.

Three assertions were rewritten for the owner's decisions about Mix and seed;
nothing else was removed or weakened. The Mix cases hold Ocean's law where they
held the reference's. The case that drove the dry signal to the clipper's
ceiling at Mix 50 % became the two driven cases below Mix 100 % described
above. And "a restored state renders what was saved" became the statements
about a restored instance. The state tests compare the processor with the DSP
given the seed the processor reports, where they used the default seed.

Engine tests added after the reviews: Size turned under a steady tone leaves
at most -54.5 dB of all energy away from the tone (limit -50 dB; with lines
that step by whole samples the first case reads -33.1 dB); a tail that has
died leaves exact silence and no arithmetic in the denormal range; the clock
signs at ten host rates equal the campaign's simulation; the Mix sum rounds
each product on its own. Each of the first three fails on the engine as the
reviews found it.

Mutations, one at a time in a scratch copy: the eighteen of the integration
wave each made a plug-in level test fail. The integration review tried 21 and
found four that no test caught (Low Cut not handed to the engine, High Damping
not handed over, `reset()` without the engine, `reset()` without the level
stage and the Sub Anchor); each of the four, the last also split in two, and
a fused Mix sum now makes a test fail. Two of the review's survivors remain
and are harmless: no reset of the Sub Anchor when Fathom is left, and the Sub
Anchor in front of the Width law, which commutes with it.

For the owner's two decisions, again one at a time in a scratch copy. Eight
changes of the routing each make a DSP test fail: the reference's Mix law in
place of Ocean's, no pass of the wet at Mix 100 %, the pass from Mix 70 % on,
the dry signal under the wide bound of the engine's input, a sum whose
products are rounded one by one, the clipper at Mix 100 % only, a seed that
is not forwarded to the engine, and a seed that takes effect at once. Five
changes of the processor each make the state tests fail in three runs of
three: one seed for every instance, the seed not handed to the DSP, a seed
from the seconds of the clock alone, the seed saved with the state, and a new
seed at every `prepareToPlay()`. With the entropy and the clock taken away
the count of instances alone keeps the seeds apart and the state tests pass.

The five other Characters render the same bytes as the last commit (4a0ac08)
with the present sources: 84 renders, 12,902,100 stereo frames, covering
impulse responses at four rates, every Ocean stage, all twenty switches among
the five and rapid automation, and the 96 cases of the integration review's
harness; the committed DSP sources were exported and compiled with the flags
of `build-ebb` for the comparison.

A build with `-ffp-contract=fast` for every file (`build-ebb-contract`) passes
all 77 DSP tests; before the fixes that followed the reviews, two tests
failed there (golden vectors at -98.24 dB, the level stage) and the renderer
sat at -102 to -110 dB. Built for x86_64 and run under Rosetta, all 77 pass
as well; there the sum of Ocean's Mix is not fused, and the tests read from
Default which of the two forms a build uses. The identity of the renderers of
those two builds with the Release one (seven and ten renders) was shown for
the build before and not repeated; the engine's sources are unchanged.

AddressSanitizer and UndefinedBehaviorSanitizer (Debug, `build-ebb-sanitize`,
the configuration of the project's README), built from the final sources and
run directly: the state tests passed in 2 min 26 s and the whole DSP
executable, 77 tests, in 36 min 43 s, with no report from either sanitizer.
Both ran while other work loaded the machine; the whole DSP executable took 33
min 2 s alone in the build before the owner's decisions. See "Not run and open"
for `ctest` there.

## What is exact, and how far

| | Meaning | Depth |
| --- | --- | --- |
| **Exact** | Macro 0: the network, the converters, Decay, Size, Pre-delay, Width, the level stage and the clipper. Mix: the reference's law in the engine's renderer; in the plug-in at 100 %, where Ocean's Mix and the reference's are the same | whole responses null at -113 to -127 dB against the reference at 44.1, 48, 88.2 and 96 kHz, for model, engine and plug-in, and at -117 and -119 dB at 64 and 384 kHz for model and engine; about -100 dB late in a tail and on steady noise |
| **Exact up to the random voice phase** | Macro above 0: comb, voices and their laws | -89 to -108 dB once the phase of a capture is given to the engine. Without it two instances of the reference differ from each other at about -2 dB, and so does Fathom from either |
| **Statistical** | the phase itself: start values, knot positions and targets of its slow value noise | Fathom's generator has the reference's form, grid, bounds and rate law and its own random numbers, from a seed every instance of the plug-in draws for itself; listening descriptors and the level cycle agree within the reference's own spread, for the campaign's scorer and for the listening review's |

Known residue of the model, all below what the nulls above show: the comb's
delay arithmetic puts a few percent of its tap changes one sample off, which
keeps steady material at -80 to -93 dB at Macro 100 %; the feedback path lags
by about 2e-7 samples per pass (-83 dB after 100 s at Decay 20 s); the last
place of the oscillators' sine is unknown.

## A/B comparison against the reference

Insert both on the same source with the host's delay compensation on.

**Rev OCEAN 1.0.0.5848**: factory preset "Cleaner Tides" (its Macro Mode is
Tide; the mode is not a host parameter), then Brightness 0, HPF 20 Hz, LPF
20000 Hz, Transients 0 dB, Ducking 0, Return 0 dB, Master Volume 0 dB,
Predelay free (not synced), Active.

**Amanita Ocean**, every control; the ones that differ from Ocean's defaults
in bold:

| Control | Set | Default |
| --- | --- | --- |
| **Character** | **Fathom** | Default |
| Decay | the reference's seconds, between 0.5 and 30 s | 5.00 s |
| Size | the reference's percentage, between 50 and 200 %. Below that the reference's percentage is 100 x Ocean's scale: its minimum of 30 % is 26.8 % on Ocean's knob | 100.0 % |
| **Pre-delay** | the reference's milliseconds, up to 250 ms; **0.0 ms** at the reference's baseline | 20.0 ms |
| **Evolution** | the reference's Macro, the same percentage; **100.0 %** at the listening point | 35.0 % |
| **Mix** | **100.0 %** on both, the listening point. Below it the two cannot be matched by percentage: Ocean's Mix is dry `1 - m`, wet `m`, the reference's dry `min(1, 2 (1 - m))`, wet `min(1, 2 m)` | 35.0 % |
| Width | reference Width = `200 (1 - 2^(-Ocean Width / 100))` %: Ocean 0 / 50 / 100 / 150 / 200 % is reference 0 / 58.6 / 100 / 129.3 / 150 % | 100.0 % |
| **Low Cut** | **20 Hz**, the knob fully down | 80 Hz |
| **High Damping** | **20000 Hz**, the knob fully up | 9000 Hz |
| **Focus** | **0.0 %** | 100.0 % |
| Harmony, Mono Safe, Freeze | 0.0 %, off, off | the same |

What Ocean's own defaults do to Fathom, measured by the listening review at
Decay 5 s, Size 100 %, Evolution 100 % with the same voice seed (in brackets:
the same on a programme of this revision's own):

| Left at its default | Effect |
| --- | --- |
| Low Cut 80 Hz | it sits in the loop: the tail at 125 Hz lasts 1.85 s instead of 3.60 s (-49 %; here -46 %), at 1 kHz -8 % (-7 %); wet -1.11 dB under steady noise |
| High Damping 9 kHz | the tail is 30 % shorter at 4 kHz and 46 % at 8 kHz (-26 and -42 %); wet -0.46 dB |
| Focus 100 % | the wet is turned down while the source plays: -1.79 dB under steady pink noise (-2.65 dB under white), -5.25 dB in the 2 kHz third octave; channel correlation 0.072 becomes 0.104; decay times stay |
| all three | wet -3.77 dB (-4.07 dB), broadband T30 4.01 s becomes 3.48 s |
| Mix 35 % | Ocean's law, as in the other Characters: dry gain 0.65, wet gain 0.35. The reference at 35 % has its dry at unity and its wet at 0.7: Fathom's dry lies 3.74 dB and its wet 6.02 dB below |

- **Timing.** The reference reports a latency of `4 floor(11 fs / 44100 + 0.5)`
  samples (44, 48, 88 and 96 at the four rates, 1 ms) and delays its dry signal
  by it. Fathom reports none and returns the same audio that much earlier. A
  host with delay compensation aligns the two; without it, delay Ocean by that
  number of samples.
- **Level.** Nothing to match: the wet level, the level stage and the clipper
  are the reference's. The listening review reads -0.015 dB between the two on
  average.
- **At Macro 0** and Mix 100 % the two null. The reference is not repeatable
  in the first 1.5 s after it is instantiated and settles over about 2.2 s;
  compare after that. With Mix at the same lower percentage on both they
  differ in level: up to 50 % Fathom's wet lies 6.02 dB below the reference's,
  from 50 % on its dry does.
- **Above Macro 0** nothing nulls, not even two instances of the reference or
  two instances of Fathom, and the swells do not fall at the same times. Listen
  for a minute or two, or to several passes, and judge how deep, how fast and
  how bright the movement is, not where it falls. Two instances of the
  reference compared the same way show how much of a difference is the
  reference's own. The owner's listening point is Macro 100 %, Mix 100 %.
- **Repeating a pass.** An open instance of Fathom repeats its movement
  whenever the host restarts processing, where the reference does not. A
  project that is opened again, a duplicated track and an export the host
  renders in an instance of its own each get another movement, as with the
  reference.
- **Input level.** The listening review kept its peaks at or below 0.5; above
  -5 dBFS both turn the wet down (the level stage, nulled at Macro 0 with an
  input peak of 6.0).
- **A null needs the same numbers, not the same display.** Line lengths are
  whole samples of `P s`. Of the 1501 positions of Ocean's Size knob from 50 to
  200 %, 1496 give all 32 lengths of the reference at the same percentage set
  through a single-precision host value; at 80.1, 119.1, 162.1, 180.1 and
  188.2 % two lines differ by one sample, which leaves a null of -10 to
  -34 dB between two responses that differ in one line length in 5000. A
  displayed tenth of a percent of the reference spans many host values, so
  the reference set by hand can fall on either side of such an edge as well.

## CPU and memory

Share of one core of an Apple M4 Max for a stereo stream, Release, 20 s of
noise per figure, measured on 7 October 2026 with the build that followed the
reviews; the present build was measured in a session of its own (below).

Engine alone (`AmanitaOceanFathomRender --benchmark --macro 1`, three runs):
1.28 to 1.29 % at 44.1 kHz, 1.41 to 1.42 % at 48 kHz and 1.47 to 1.51 % at
96 kHz. While another Character is selected (`advanceIdle`): 0.05 to 0.08 %.
Flooring the filter states costs nothing that these figures show: the build
before read 1.28 to 1.31 %, 1.42 to 1.45 % and 1.51 to 1.53 % in the same
session.

Whole DSP of the plug-in (`FDNReverb::process`, blocks of 512, Evolution
100 %): the best of nine runs in three sessions and, in brackets, their
median; other work on the machine disturbed single runs. The FDN, the
harmonic analyser and Focus keep running behind Fathom, so Fathom costs what
Default costs plus the engine:

| Host rate | Default | Fathom | Fathom with Ocean's defaults (Focus 100 %) |
| --- | --- | --- | --- |
| 44.1 kHz | 2.77 % (2.82 %) | 3.96 % (4.04 %) | 4.10 % (4.18 %) |
| 48 kHz | 3.05 % (3.10 %) | 4.35 % (4.53 %) | 4.56 % (4.64 %) |
| 96 kHz | 5.56 % (5.68 %) | 7.15 % (7.33 %) | 7.26 % (7.47 %) |

The build before read the same within that scatter in the same sessions (best
of nine: Default 2.77 / 3.05 / 5.50 %, Fathom 3.99 / 4.45 / 7.05 %, Fathom with
Ocean's defaults 4.10 / 4.55 / 7.16 %).

The present build, in which Fathom leaves through Ocean's Mix and the seed is
drawn when an instance is made, read in nine runs of a later session (best,
median in brackets): Default 2.74 % (2.76 %), 2.97 % (3.04 %) and 5.43 %
(5.50 %) at the three rates, Fathom 3.91 % (3.94 %), 4.30 % (4.34 %) and 6.97 %
(7.01 %), Fathom with Ocean's defaults 4.06 % (4.10 %), 4.47 % (4.51 %) and
7.04 % (7.15 %); the engine alone 1.29 to 1.31 %, 1.43 % and 1.51 to 1.52 %.
Fathom costs 1.17, 1.33 and 1.54 % of a core more than Default, where the table
above has 1.19, 1.30 and 1.59 %: the same within the scatter of a session.
The CPU was not measured again for the final revision, whose DSP differs from
that build in names, comments and two entries of a table.

Editor, host and format wrapper are not in these figures. The integration
review, with the parameters as the processor sends them and other work on the
machine, read 45 to 53 % over Default for Fathom at 44.1 and 48 kHz and 28 to
32 % at 96 kHz, and 0.04 to 0.19 % of a core more than the last commit for an
instance in which Fathom is not selected.

Memory per instance at 48 kHz: 2.1 MB of delay lines and 0.77 MB of pre-delay
(sized for the reference's 2 s; about 6 MB at 384 kHz); one 272 KB converter
table is shared by all instances. Everything is allocated in `prepare()`,
whichever Character is selected. At a host rate without measured clock signs
`prepare()` also runs the clock simulation, about a millisecond.

## Properties and limits

Not measured in the reference, and therefore Fathom's own behaviour:

- **Moving controls.** Every capture holds its settings. In Fathom, Decay and
  Size glide over 250 ms, Macro over 200 ms, a new Pre-delay crossfades in
  over 50 ms, Low Cut, High Damping and Freeze fade over 50 ms. While Size
  glides each line moves in a straight line from one whole length to the next
  through the fractions in between and lands on the whole length exactly, so
  the measured length rule holds at rest and a turn of the knob bends the
  pitch, as it does in the five other Characters. Energy away from a steady
  tone while the knob goes from 100 to 110 % in 2 s (1 / 3 / 8 kHz, whole DSP,
  48 kHz): -72.9 / -68.3 / -65.5 dB at Evolution 0 and -72.0 / -65.8 /
  -60.5 dB at 100 %; Default -78.8 / -76.9 / -73.3 dB. Before, when the lines
  stepped by whole samples: -33.9 / -28.2 / -25.6 dB and -32.4 / -27.5 /
  -18.9 dB. During one jump from 100 to 130 %: -39.2 / -40.9 / -31.7 dB
  (Default -45.6 / -39.3 / -31.8 dB); at Evolution 100 % -37.7 / -46.7 /
  -18.7 dB, where the voices take most of an 8 kHz tone and leave what the
  bend puts below it.
- **The first seconds of an instance.** The reference's first 2.2 s are not
  modelled; Fathom starts at its settled laws with empty lines. The listening
  review, with noise on the first frame of a fresh instance: the reference
  passes the dry input for about 16 ms (gain 1.00, 0.93, 0.50, 0.10, 0.006 over
  0-1, 1-2, 2-4, 4-8, 8-16 ms) and is brighter until about 0.5 s (the engine
  +1.60 dB at 300 Hz to 3 kHz and -1.66 dB above from 0.1 to 0.25 s); from
  0.5 s on nothing differs. An instance that is prepared with Fathom selected
  starts as Fathom (the state test compares it with the engine from frame 0). A
  host that sets the parameters after preparing, as the Analyzer does, hears
  the 200 ms crossfade from the default Character first.
- **Freeze.** Ocean's hold, not the reference's (its Decay knob at the top was
  outside the campaign). Lows and mids hold, the top wears down because the
  lines keep moving (measured on the engine: about 0 dB/s below 1 kHz, -3.6
  dB/s at 4 kHz); at Evolution 100 % the held level follows the voices. Loud
  input still ducks a held tail through the level stage.
- **Freeze and the comb** (Ocean's own; changed after 0.23.0 on the owner's
  decision of 10 October 2026, for Fathom, Undertow and Spume alike). Held, a
  line of the network loses nothing in a pass and takes no input; between the
  two states the hold moves in a straight line over 50 ms, 2205 internal
  samples, the first step in the first internal sample behind the switch,
  and lands on its end exactly. In 0.22.0 and 0.23.0 the comb of the Tide
  layer went on taking the equalised input under Freeze. Its feedback kept
  what it took for a quarter of a second, so the end of a sound played under
  Freeze came back with the release: by the measure of the test, a burst of
  a quarter second that ended 10 ms in front of the release returned at
  -29.4 dB of the same burst played with no Freeze at Evolution 100 %
  (-32.0 dB at 50 %), one that ended 50 ms in front of it at -52.3 dB
  (-54.5), 100 ms at -80.2 dB, 250 ms at -161.4 dB, a second and more
  nothing. Now the comb takes its input by the share the lines are given in
  the same internal sample, one less the hold: it stops listening over the
  50 ms in which the lines do, takes exactly nothing while Freeze is fully
  on, and listens again over the 50 ms of the release, at every Evolution,
  also at 0, where it only keeps its history for the time Evolution rises.
  Of the same bursts nothing comes back, to the bit, down to 50 ms in front
  of the release. What the comb took before Freeze runs out as it did, and
  with Freeze off the share is one and nothing has changed. One thing is
  left: the input equaliser in front of comb and lines is not held, and it
  rings for some ten milliseconds, so a burst that ends 10 ms in front of
  the release leaves -152.9 dB at Evolution 100 % and -138.5 dB at
  Evolution 0, where it was -138.5 dB before as well.
- **Non-neutral reference controls.** Brightness, the input filter,
  Transients, Ducking above 0 %, Return, Master, the other Macro modes.
- **The level stage.** Keeping the reference's reduction at Ducking 0 % is
  the owner's decision; it makes Fathom the only Character whose wet is ducked
  by input above -5 dBFS with Focus at 0.
- **The voice seed.** Every instance of the plug-in draws a 64-bit seed when
  it is created, off the audio thread: the system's entropy
  (`std::random_device`) mixed with the number of instances the process has
  made and with a high-resolution clock. The count keeps the instances of one
  process apart whatever the entropy and the clock do. Two processes without
  system entropy would be told apart by the clock alone; on macOS the entropy
  cannot fail, so that case does not arise there (the behaviour review ran it
  on a copy of the function: 2560 processes started 64 at a time, no seed
  twice).
  The instance keeps the seed for its lifetime: `prepareToPlay()` and a reset
  start the same phase again, so an open instance repeats its movement on
  every restart of processing, which the reference does not. The seed is not
  part of the state: a duplicated track, a project that is opened again and
  an export the host renders in an instance of its own get a new one, as with
  the reference, so a bounce no longer repeats across sessions. Two instances
  no longer cycle in unison. On 50 s of steady noise at Decay 5 s, Size 100 %,
  Macro 100 %, Mix 100 %, six instances of the built VST3 correlate pair by
  pair at 0.52 and 0.57 on average in two runs (0.39 to 0.64) and a pair sums
  to +4.82 and +4.97 dB over one of them (+4.43 to +5.16 dB); six
  realisations of the reference read 0.56 (0.49 to 0.66) and +4.93 dB (+4.73
  to +5.20 dB). At Evolution 0 two instances return the same samples and sum
  to +6.02 dB, as two instances of the reference do at Macro 0. Before, every
  instance ran from one seed and two of them summed to +6.02 dB at any
  Evolution. The DSP classes on their own (`FathomEngine`, `FDNReverb`, the
  renderer) keep `FathomEngine::defaultVoiceSeed` until a seed is handed to
  them, so tests and scores of the engine repeat.
- **Ranges beyond the reference's.** Decay 0.2 to 0.5 s, Size scale 0.15 to
  0.3 and host rates below 44.1 kHz are checked against the model only.
- **Host rates.** The engine works from 22.05 to 384 kHz. A rate outside is
  processed as the nearest bound, so time and pitch scale with it: at 768 kHz
  Fathom's tail is half as long and an octave up, at 16 kHz 1.38 times longer
  and lower. The other Characters accept any rate above 1 kHz.
- **Converter clocks.** Their rounding signs are measured at 48, 56, 64, 88.2,
  96, 176.4, 192 and 384 kHz (at 64 and 384 kHz on whole responses, in this
  revision). At any other rate the engine runs the campaign's simulation of
  the reference's block clocks when it is prepared. The simulation gives the
  measured pair at seven of those eight rates and a wrong output clock at
  56 kHz, where the measured pair is used, so an unmeasured rate may still be
  off: such a rate nulls at about -77 dB instead of -117 dB, which is not
  audible. The engine's signs equal the campaign's at 432 host rates from
  22.05 to 384 kHz.
- **Low Cut and High Damping at their end stops.** They are out of the circuit
  only at exactly 20 Hz and 20000 Hz. One step away they are whole filters in
  the loop, so the decay time steps there: broadband time for 60 dB at Macro
  0, Decay 5 s, 3.78 s neutral, 3.73 s at Low Cut 20.065 Hz, 3.71 s at High
  Damping 19999 Hz; at Decay 30 s 12.44, 11.94 and 11.81 s (the integration
  review, by its own measure: 4.04 / 3.99 / 3.98 s and 13.56 / 13.00 /
  12.83 s). The knob at its end stop and a typed "20" give the neutral value;
  the Low Cut readout rounds to whole hertz and reads "20 Hz" up to 20.5 Hz.
  The step is the price of exact neutrality.

Properties of the integration:

- **Entering and leaving.** A switch is a 200 ms crossfade. The engine starts
  from silence when Fathom is selected, so a tail that was sounding in another
  Character fades out and Fathom answers only to input from then on. The FDN
  keeps running as Default behind Fathom, so leaving Fathom fades into a live
  FDN tail. Decay 10 s, a switch 1.5 s after a burst, level against not
  switching in half seconds: Default to Fathom -7.5 dB, then silence; Fathom to
  Default +2.8 to +7.4 dB (integration review; a burst of this revision's own
  reads -7.2 dB and +4.1 to +9.6 dB). There is no click in either direction.
- **Mix.** Fathom leaves through the sum every Character leaves through, dry
  `1 - m` and wet `m`, so selecting Fathom leaves the dry level where it is at
  every Mix: dry gain Default / Fathom 0.80 / 0.80 at Mix 20 %, 0.65 / 0.65 at
  Ocean's default of 35 %, 0.50 / 0.50 at 50 %, 0.25 / 0.25 at 75 % (before,
  with the reference's law: 1.00, 1.00, 1.00 and 0.50 for Fathom, up to
  +6.02 dB on selecting it). At the factory defaults a noise burst leaves the
  plug-in at -3.53 dB re the input with Fathom against -3.38 dB with Default
  (before: +0.36 dB). At Mix 100 % nothing has changed: the wet passes as it
  is, and at Macro 0 the plug-in still equals the engine's renderer there bit
  for bit. Against the reference at the same percentage Fathom is now quieter:
  dry / wet -1.94 / -6.02 dB at 20 %, -3.74 / -6.02 dB at 35 %, -6.02 /
  -6.02 dB at 50 %, -6.02 / -2.50 dB at 75 %, equal at 0 and at 100 %. The
  reference's clipper stays behind the Mix, by the owner's decision: a sum
  above +8 dBFS is turned down in Fathom at any Mix, where the other
  Characters pass it up to +18 dBFS. So with Mix below 100 % a dry signal
  above +8 dBFS is turned down in Fathom and in no other Character. The
  behaviour review, and a probe of this revision after it, with the dry
  signal alone at a peak of 4.0 (+12 dBFS): Mix 0 %
  leaves Default at 4.000 and Fathom at 3.623 (-0.86 dB), Mix 10 % at 3.600
  and 3.399 (-0.50 dB), Mix 35 % at 2.600 and 2.599; by the clipper's law a
  dry sine at +12 dBFS leaves Fathom at Mix 0 % with 3.6 % of harmonics, at
  +10 dBFS with 0.9 %. Up to +8 dBFS the dry level is the same bit for bit.
- **Input level.** Fathom's engine and level stage read the input up to
  +18 dBFS (the other Characters up to +12 dBFS), because the level stage is a
  law of the input level and was measured up to a peak of 6.0. The dry signal
  enters the Mix under the bound of +12 dBFS that every Character observes;
  before, Fathom's dry signal was bounded at +18 dBFS.
- **Output floor.** A settled Fathom passes its wet down to the engine's floor
  of 1e-30; the reference decays to about 1e-36. Its dry share passes down to
  the smallest normal single-precision number, 1.2e-38, where the other
  Characters flush everything under 1e-20: with Mix below 100 % a dry signal
  under 1e-20 (-400 dBFS) leaves Fathom and none of the others, so "the same
  bits as the other Characters" holds for the dry signal from 1e-20 up to
  +8 dBFS. The engine's filters hold
  nothing under that floor either, so a tail that has died leaves exact
  silence and no arithmetic on denormal numbers, with or without the host's
  flush-to-zero. Before, every frame after a tail raised the underflow flag,
  for as long as the engine ran. What is left is the moment a tail crosses the
  floor at a host rate with converters: for 240 to 290 frames the conversion
  of the output to single precision forms a denormal that the floor discards.
- **Tail length.** The host is told Decay + 0.5 s for every Character. Fathom's
  measured time to 60 dB below its loudest 20 ms is at most Decay + 0.34 s
  (Decay 0.5 s, Pre-delay 250 ms, Size 200 %, Evolution 100 %) and much
  shorter than Decay at long settings (at most 18.6 s at Decay 30 s), on
  impulses, noise bursts and bursts around 500 Hz and 3 kHz at 48 kHz. Those
  figures are of the default seed. Over 1500 seeds the burst of the state
  test at Pre-delay 250 ms, Size 200 %, Evolution 100 % is 60 dB down 0.48 to
  0.52 s after the input at Decay 0.2 s (the host is told 0.70 s) and 0.74 to
  0.82 s at Decay 0.5 s (1.00 s).
- **Level against the other Characters.** Not matched. Under 20 s of steady
  noise at Mix 100 % with neutral controls Fathom's wet lies 5.1 to 5.6 dB
  below Default's at Evolution 0 and 4.6 to 6.3 dB below at Evolution 100 %
  (Decay 2, 5 and 12 s). The level at Evolution 100 % cycles over about 17 s,
  so a single burst reads what the cycle gives it: 5.2, 2.1 and 5.0 dB below on
  a 0.3 s burst at Evolution 0, 35 and 100 % (integration review, Decay 5 s).
- **Automation of Character.** A sixth choice moves the normalised positions
  of the others to 0, 0.2, 0.4, 0.6, 0.8 and 1.0. A VST3 host stores the
  normalised value: an automation lane written by 0.21.0 keeps Default, Bloom
  and Drift (0, 0.25, 0.5), and plays Current where it had Veil (0.75) and
  Fathom where it had Current (1.0). AU and CLAP expose the index (read in the
  wrapper sources by the integration review, not run in a host). Saved states
  store the index and are unaffected.
- **States across versions.** A project saved with Fathom opens as Current in
  0.21.0, without notice. In this build a stored Character past the last
  choice opens as Fathom (in 0.21.0: as Current), a negative number or text as
  Default.
- **Compiler flags.** The engine is defined by its roundings.
  `Source/dsp/FathomExactArithmetic.h` switches contraction off by pragma, the
  build adds `-ffp-contract=off` for the engine's three sources, and
  fast-math is refused at compile time. The renderer returns the same bits
  built for arm64 with Apple clang, built with `-ffp-contract=fast`
  everywhere, and built for x86_64 and run under Rosetta (ten renders, three
  of them at host rates with simulated clock signs). The sum of Ocean's Mix
  lies outside the engine and is compiled as the rest of the plug-in is: with
  Mix below 100 % the last bit of Fathom's output differs between a build that
  fuses its product into the addition (arm64) and one that does not (x86_64),
  as it does for the five other Characters. At Mix 100 % it does not.
- **Platforms.** Built and run on macOS arm64 with Apple clang only. The GCC
  and MSVC branches of the pragma were never compiled, and an MSVC build gets
  no flag from CMake.

## Decisions for the owner

Findings of the reviews that a code change does not settle. Entries marked
"Decided" hold what the owner has decided and what that left open; nothing
was changed for the others.

1. **VST3 automation of Character is remapped** for Veil and Current
   (integration review, major; see "Automation of Character"). Accept it and
   say so in the release notes, or keep the first five on their old positions
   with a mapping of the parameter's own or a separate parameter for the
   sixth choice.
2. **Decided: Fathom takes Ocean's linear Mix, and the reference's level
   stage and clipper stay as they are.** Selecting Fathom below Mix 100 %
   raised the dry signal by up to 6.02 dB, 3.74 dB at Ocean's default Mix
   (integration review, major), because Mix followed the reference. In the
   plug-in Fathom now leaves through the sum of the other Characters at every
   Mix; the reference's law stays in the engine for the renderer and the
   scoring. It departs from the reference at every Mix except 0 and 100 %,
   and nothing changed at 100 % (see "Mix"). The clipper stays on the sum,
   behind the Mix, as the reference has it. What that leaves: with Mix below
   100 % a dry signal above +8 dBFS is turned down in Fathom and in no other
   Character (behaviour review, minor; figures under "Mix").
3. **Ocean's defaults take Fathom far from the reference** (listening review,
   major; table under "A/B comparison"). Keep the six knob moves as a
   documented step, or set Low Cut, High Damping and Focus to neutral when
   Fathom is selected.
4. **Decided: a voice seed of its own for every instance.** With the fixed
   seed bounces repeated and instances swelled in unison (listening review,
   minor). Every instance of the plug-in now draws a seed when it is created
   and does not store it (see "The voice seed"). Left open by it: an export
   that a host renders in an instance of its own, a frozen or duplicated
   track and a project that is opened again each sound another movement than
   the one that was heard, as with the reference, and nothing in the plug-in
   brings a given movement back; and within one open instance every restart
   of processing repeats the movement, where the reference never repeats.
5. **The step of Low Cut and High Damping at their end stops** (integration
   review, minor). Keep it as the price of exact neutrality, or let the two
   filters come into the loop gradually over the first steps of their knobs;
   and whether the Low Cut readout should show that it has left 20 Hz.
6. **Host rates outside 22.05 to 384 kHz** (engine review, note). Leave Fathom
   scaled in time and pitch there, widen the engine's bounds (its rules are
   closed forms at any rate; the pre-delay memory grows with the rate), or
   keep Fathom out of such sessions.
7. **Entering Fathom ends a sounding tail; leaving it brings up the hidden
   FDN's tail** (integration review, note). Documented behaviour; to be
   confirmed.
8. **Fathom's wet is 4.6 to 6.3 dB below Default's** and not matched (a
   recorded decision; the figures are new).
9. **Cost.** Fathom costs Default plus the engine because the FDN keeps running
   behind it, and every instance holds the engine's memory and steps its
   clocks. Stopping the FDN behind a settled Fathom would change what leaving
   Fathom fades into.
10. **Decided: the words of the editor.** The segment selector, whose words
    "Tidal / Swell" echoed the reference's mode name (integration review,
    note), is gone. The description block carries the texts the owner
    approved; Fathom's read "MODELLED 16-LINE TIDAL NETWORK" and "Evolution
    brings in the tide". The reference itself is named in documents only.
11. **Done: `README.md`** says what this revision is: six Characters wherever
    it counts or names them, the editor as it is, where Fathom follows the
    measured laws of its reference and where it uses Ocean's own, the neutral
    settings for a comparison, and at its top that five Characters are
    Amanita's own and Fathom is a structural model of a black-box reference.
    Left as it was, on the owner's instruction: its list of render
    fingerprints, which holds four Characters of version 0.8.3.
12. **Done: the clock sign pairs at 64 and 384 kHz** are in the campaign's
    measured table, on the strength of all nine pairs tried on the engine
    review's captures (see "Macro 0"). `SPEC.md` still lists the six pairs of
    wave 2 as measured; it was not edited.
13. **Small text in the deep blue** (editor review, minor). The label of a
    toggle that is on (FREEZE, MONO SAFE; 11 pt bold in the accent) reads
    4.21 : 1 for Fathom, under the 4.5 : 1 of small text (the other five
    accents: 6.3 to 8.4); the word OCEAN reads 4.47 : 1, enough for 18 pt
    bold. Keep `#2F7FE0` there, or use a lighter tint of the accent for small
    text.
14. **The calm pool of the GPU background and the labels outside the block**
    (editor review, two minor findings that pull against each other). The
    calm fades over 0.30 of the frame's height beyond the block: in bright
    passages it darkens 44 to 50 % of the centre zone by more than half and
    the left half of the knob stands at about half its luminance, with no box
    or edge to be seen. The EVOLUTION label, outside the block, has a glyph
    pixel under 4.5 : 1 in 15 % of frames at factory settings and in 35 % at
    Evolution 100 %, Focus 0 %; the labels of the lower row behave alike and
    did so before. Drawing the calm margin in (to 0.12 to 0.15 of the height,
    one number in `AbyssalFlowShaders.h`) would bring the field back round
    the knob and change nothing inside the block; it would also take from the
    EVOLUTION label the ground it has from standing near the pool (the
    engineers read that label under 4.5 : 1 in fewer frames each time the
    pool came nearer; the smaller margin itself was not measured). Limiting
    the field under all labels or giving labels a faint ground of their own
    is the other way. Nothing was changed: the owner has seen and corrected
    this composition, and either move changes its look.
15. **Space on the drop-down** (editor review, minor). Space opens the list,
    as asked. The drop-down has the keyboard as soon as the editor's window
    has it, so in a host that offers keys to the plug-in first, Space opens
    the Character list where the earlier selector let it through to the
    transport. No host was run. Keep Space, or leave the list to Return.
16. **The working name survives in four numbers** (both later reviews,
    note): `FathomEngine::defaultVoiceSeed` is `0x45626245`, the ASCII text
    "EbbE", and three seeds of test and benchmark noise spell the name in
    hexadecimal digits (`Tests/DspTests.cpp`, `Tools/FathomRender.cpp`).
    Changing the first changes every render made from the default seed,
    changing the others the test stimuli and the benchmark checksum. Kept;
    also kept are the names of the build folders (`build-ebb`) and a note in
    the campaign's `emit_engine_constants.py` and `engine_golden.json` that
    names the engine's header by its old name.
17. **Recorded choices of the editor's engineers**, to confirm or change: the
    border of the engaged drop-down at 86 % of the accent for all Characters
    (72 % read 2.72 : 1 for Fathom, 86 % reads 3.35 in the state tests); the
    drop-down at y 112, on the 4 px grid, where the mock-up said about 110; the
    foot of the Evolution value taken as the baseline of its text, which round
    digits overshoot by about 0.3 pt; the keyboard's order, which now goes from
    the drop-down to the knobs and then to Mono Safe and Freeze, as the focus
    numbers in the code always said, where before it went to the two toggles
    first.

## Not run and open

- No listening test, no DAW, no plug-in validator (auval, pluginval,
  clap-validator). Only the VST3 was rendered through the Analyzer; AU and CLAP
  were built from the same code and not rendered.
- No Windows or Linux build of this revision, and no GCC or MSVC compile of
  the engine. The editor's line breaks, the fit of its texts and the walk of
  the keyboard were checked with the macOS font and window system only; the
  project's CI runs the state tests on Windows and under xvfb on Linux, where
  the new test of the editor in a window has never run.
- The editor was not opened in a host: nothing may be installed. The live
  pictures come from the test executable, which gives the plug-in's own
  editor a window and an OpenGL context. Not seen: the list inside a host
  window, a host's scale factor, how a host hands the keyboard over, mouse
  interaction, VoiceOver itself.
- The builds with `-ffp-contract=fast` and for x86_64 were not repeated for
  this revision; they stand for the engine as it was before the two table
  entries and the comments.
- `ctest` in the sanitizer build was not run again. It does not pass there:
  it stops the DSP tests at its limit of 420 s and the state tests at 30 s,
  long before either ends in a Debug build with both sanitizers; the
  executables are run directly instead. The limits were already too short
  for the DSP tests before Fathom: the sanitized binary of 27 July in
  `build-sanitize` (52 tests) takes 25 min 22 s on the same machine.
- The plug-in was not scored statistically at Macro 50 %, and its level cycle
  was not measured on its own; for both the engine's results stand in: the
  processor is the engine with the seed of its instance, bit for bit in the
  state tests.
- Above Macro 0 the built VST3 was not compared with the engine to the bit
  through the Analyzer: no renderer knows the seed an instance drew, and the
  plug-in has no way to be told one. The comparison is made in the state
  tests, which link the plug-in's code and read the seed from the processor.
- The seed's source was tested in one process on one machine. That two
  processes without system entropy draw different seeds rests on the clock
  and was not run.
- No capture-based null at 176.4 kHz, with the plug-in at Decay above 7.3 s
  (the engine: up to 20 s), with the plug-in at 64, 192 or 384 kHz, or with
  the engine on steady material at Macro 100 %.
- The listening review was not repeated on the present build; its two open
  readings of the decay time stand as it left them. It did not compare
  controls in motion, input that works the level stage, Freeze, or instances
  older than 165 s.
- The rule of the comb under Freeze (since 0.23.0) was tested in the engine
  and through `FDNReverb`, not in a host and not by ear. The scores against
  the reference were not run again for it; the renders they are made from
  are the same bytes.
- The cost of denormal arithmetic was never measured on an Intel processor;
  the engine now avoids it, which was checked through the underflow flag on
  arm64.
- The contrast of the block over the GPU field was not measured again in
  this revision: the figures are the editor review's and the engineers'.

## Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cmake --build build-ebb --target AmanitaOceanFathomRender AmanitaOcean_VST3 \
      AmanitaOceanDSPTests AmanitaOceanStateTests --parallel
ctest --test-dir build-ebb --output-on-failure
./build-ebb/AmanitaOceanDSPTests --test-fathom
./build-ebb/AmanitaOceanStateTests --render-ui fathom.png 5 960        # the editor without a window
./build-ebb/AmanitaOceanStateTests --render-ui-live fathom_live.png 5  # over the GPU background
cd Analyzer/Campaigns/RevOceanCharacterization
$PY score_engine.py                    # every part; the first run renders captures
$PY score_engine.py clocks             # 64 and 384 kHz (measured pairs), 128 kHz (simulated)
$PY score_engine.py plugin plugin_statistics
$PY score_network.py render_candidate.py                               # the plug-in on the locked holdout
$PY score_tide.py render_candidate.py --statistics --seeds 8 --macro 100   # eight new instances per case
```

The reference captures are cached under `Analyzer/Results` (not in Git); the
scorers need the reference and the Analyzer installed as the campaign pins
them. The report is written to
`Analyzer/Results/RevOceanCharacterization/work/engine/score_engine.json`.
