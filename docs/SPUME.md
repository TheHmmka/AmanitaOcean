# Spume: development characterisation

The eighth Character of Amanita Ocean. This document says what it is, what of
it was measured and what is Ocean's own, how far it has been checked and what
has not been checked. It is a development record, not a product description.

## Status

A local development model in the working tree, not a release. Nothing is
installed, published or signed, the version is unchanged, and nothing was
listened to: every statement below is a measurement. The numbers are in
`docs/SPUME_VALIDATION.json`.

The DSP is in: the diffuser, the layer, the engine that carries it, the
plug-in's routing, the tests, the offline renderer and the scores. The editor,
the processor and the state are a colleague's work and are not described here.

## Scope and identity

| | |
| --- | --- |
| Character | index 7 of the host parameter "Character" (the eighth choice); `ReverbMode::spume` in `Source/dsp/FDNReverb.h` |
| Layer | `Source/dsp/SpumeDiffuser.h` and `.cpp` (the diffuser), `SpumeLayer.h` and `.cpp` (the crossfade and its gains), `SpumeConstants.h` (generated) |
| Engine | Fathom's: `Source/dsp/FathomEngine.h` with `Layer::spume`, and the test hooks `setClockOriginsForTesting` and `nextCoreSampleForTesting` |
| Reference | Arturia Rev OCEAN in its Macro Mode Foam; the instance and the host of the recordings are those of the campaign (`docs/FATHOM.md`, "Scope and identity") |
| Campaign | `Analyzer/Campaigns/RevOceanCharacterization`; everything of Foam is in its folder `foam/`, and its `README.md` has a section "Foam (Spume)" |
| Model | `foam/model/foam_model.py` with `foam_structure.py`, `structure_final.json`, `fcommon.py`, and `base.py` and `abyss_core.c` of `abyss/model/` |
| Findings | `foam/findings/`: `FOAM_STATE.md` (the mode on one page, with a mark on every statement: measured exactly, fitted, guessed), `FOAM_FIRST_LOOK.md`, `FOAM_T90.md` (the second instance), and the model's own scores, `model_scores.json` |
| Sessions | `foam/sessions/` (the scripts that made the recordings) and `foam/sessions.json` (tempo, reported position and oscillator origin of each session); the recordings themselves are under `Analyzer/Results/RevOceanCharacterization/work/foam/`, which is not in Git |
| Scripts | `emit_spume_header.py` (constants and golden vectors), `score_spume_engine.py` (the engine against the recordings), both in the campaign folder |

The reference was measured as a black box, from recorded audio of attended
sessions alone, as for Fathom and Undertow. Its name appears in documentation
only.

## What the Character is

Spume is Fathom's network with another layer in front of it. Fathom's layer
(the reference's Tide) is a comb at the input and two voices at each output;
Undertow's adds three voices to the input; Spume's crossfades the input with a
diffused copy of it:

```
in -- pre-delay -- converter -- u --+------------------- x cos(pi/2 E) --(+)-- Fathom's network at Macro 0 -- converter -- wet
                                    |                                     ^
                                    +-- diffuser F (28 all-passes) -- x sin(pi/2 E)
```

- **The diffuser** is fixed: per input, seven stages of four parallel
  Schroeder all-passes, `y[n] = g x[n] + x[n - D] - g y[n - D]`, with half
  the Sylvester Hadamard matrix of order 4 between two stages. The input goes
  to the four paths at one half each and the output is their sum. The 28
  delays run from 1323 to 17 653 samples of the internal 44.1 kHz (30 to
  400 ms), one gain per stage (0.373, 0.466, 0.572, 0.474, 0.409, 0.363,
  0.272). Nothing in it moves, nothing feeds back around it, and it has no
  clock, no tempo and no noise.
- **What it does to a sound.** An impulse becomes a wash that rises for a
  second and falls by 28.4 dB a second, whatever Decay is: its direct copy is
  0.0038 (-48 dB), 1.6 % of its energy has arrived by 0.5 s and 49 % by 1.0 s,
  the loudest 50 ms lie 0.95 s behind the impulse, and what is still to come
  is 60 dB down after 3.14 s. Its energy is one (0.9987), its magnitude
  between 0 and 2: four all-pass chains are summed, so it is not an all-pass.
- **Evolution** is the crossfade: the plain input times cos(pi/2 E), the
  diffused one times sin(pi/2 E). The diffuser takes the input at every
  Evolution, also at 0, so opening Evolution brings the wash of what was
  played before. At 0 % the Character is Fathom at Evolution 0 to the bit; at
  100 % the network hears the wash alone.
- **Evolution in motion** moves the two gains, not the knob's value: once per
  block of 44 internal samples (1 ms) each gain goes 9.5 % of the way to its
  target and is then held, a one-pole of 10 ms run at the block rate.
- **Nothing of the layer follows the host**: not its tempo, not its position,
  not the length of its blocks.

Ocean's controls act as in Fathom, because the layer is part of the input:

| Ocean control | In Spume |
| --- | --- |
| Decay, Size, Low Cut, High Damping | Fathom's network; none of them reaches the diffuser |
| Freeze | Fathom's hold of the network, which takes no input while it is held; and the diffuser takes none either, by the same glide of 50 ms (see "Freeze"). What is played under Freeze is gone when Freeze ends; what the diffuser took before runs out |
| Pre-delay | in front of the layer: the diffuser hears the delayed input |
| Evolution | the knob itself, as in Fathom; here it is the crossfade |
| Width, Mix, Focus, Harmony, Mono Safe | exactly Fathom's routing: engine wet, level stage, Harmony, Width, Focus, Ocean's linear Mix, the reference's clipper |

The plug-in leaves through Ocean's own linear Mix for Spume exactly as for
Fathom and Undertow. The reference's Mix law is in the offline renderer only.

## What was measured and what is Ocean's own

"Two instances" means: found in session A and, with nothing fitted again but
the oscillator origin, met in session T90, which ran another instance of the
reference at another tempo with its play head elsewhere.

| | Source | Status |
| --- | --- | --- |
| Base untouched; the crossfade in front of the equaliser; left and right separate with the same diffuser | `FOAM_STATE.md` 1 | measured, two instances |
| Decay, Size, tempo, play head and the host's block grid do not enter the diffuser | `FOAM_STATE.md` 1 | measured, two instances |
| The 28 delays | `FOAM_STATE.md` 2 | measured, two instances: each is -84.8 dB at its value and -3 to -5 dB one sample off |
| The seven gains, the Hadamard mixes with their signs and order, input one half per path, output the plain sum, seven stages | `FOAM_STATE.md` 2 | measured (the gains by least squares, each within 1.2e-7 of a three-decimal number) |
| Gains cos and sin of pi/2 Macro; the diffused gain behind the diffuser, which runs at every Macro | `FOAM_STATE.md` 3 | measured, two instances |
| The two gains move, not Macro: a one-pole of 10 ms run once per block of 44 internal samples and held | `FOAM_STATE.md` 3 | measured on 99 changes, two instances |
| A block begins at internal samples 43 modulo 44 | `FOAM_STATE.md` 3 | measured, two instances |
| Pre-delay in front of the converter and so of the diffuser; Width, Mix and the level stage on the wet | `FOAM_STATE.md` 4, `FOAM_T90.md` | measured in the second instance |
| Which block of 44 first uses a new Macro (the reference's handling of its host blocks) | `FOAM_STATE.md` 3 | fitted on two alignments; kept out of the engine, see "Evolution in motion" |
| A change of mode empties the network and the diffuser and restarts the oscillators | `FOAM_STATE.md` 5 | measured once, in silence |
| **A new Evolution enters at the first block of 44 that begins once it has arrived** | | **Ocean's own** |
| **A gain within 1e-12 of its target is on it** | | **Ocean's own** |
| **What a line of the diffuser holds under 1e-30 is silence** | | **Ocean's own** |
| **While another Character is selected the engine only keeps time; it returns with the diffuser and the network empty, the gains on their targets and the oscillators where they would be** | | **Ocean's own** |
| **Under Freeze the diffuser takes no new input, by the share the network gives its own** | | **Ocean's own** (the owner's decision of 10 October 2026, for Spume and Undertow alike) |
| **Low Cut, High Damping, Freeze, Focus, Harmony, Mono Safe, Ocean's Mix** | | **Ocean's own**, as in Fathom |

The specification of this Character expected no rule of Ocean's own beyond
finite and bounded output and safety from denormals. Two were needed besides
the floor of the lines. The floor of the gains: a one-pole never arrives, and a
settled Spume at Evolution 0 has to be Fathom to the bit, so a gain that has
come within 1e-12 of its target (-240 dB of the signal it scales, 277 blocks
or 0.28 s behind a full step) takes the target itself. And the block a new
Evolution enters at, which the next section gives. In the code what is
Ocean's own is marked where it stands: the floor of the gains has a block of
its own at the end of `SpumeConstants.h`, the floor of the lines stands in
`SpumeDiffuser.cpp`, and the block a new Evolution enters at is how
`SpumeLayer` is written.

## Evolution in motion

**The law** (measured). Each gain has a target, cos or sin of pi/2 Evolution.
At the first sample of a block of 44 internal samples each gain keeps
exp(-44/441) = 0.905043 of its distance from its target, and it is held over
the block. A block begins at the internal samples 43 modulo 44, counted from
the instance's first frame (`prepare()` or `reset()`). Up and down alike, and
a change that arrives before the last has settled carries on from where the
gains stand. The largest step of a gain is therefore 9.5 % of what separates
it from its target, once a millisecond; nothing was listened to, and the
reference does the same.

**Where a new Evolution enters** (Ocean's own). `FathomEngine::setParameters`
gives the layer its new targets at once, and the gains start towards them at
the first block that begins at or after that moment: at most a millisecond
later, and in the same block if the change arrives in front of the block's
first sample. A block that has begun keeps its gains. The plug-in hands the
engine the knob's value once per host block, so the output does not depend on
how the host cuts its blocks as long as Evolution moves at the same frames.
While the layer holds no signal (after `reset()`, and while another Character
is selected) a new Evolution is taken at once.

**The reference's block** (a test hook, not in the engine). In the reference
the first block to use a value depends on where the host call that brought it
began: it converts in chunks of 48 host frames on a grid of its own, and a
block takes the value of the host call in which its chunk is completed
(`foam_model.first_block`, fitted on two alignments of the host's blocks). At
48 kHz that block can lie in front of the sample the engine computes next when
the value arrives, because the engine reports no latency where the reference
is 48 frames late. So the rule cannot be the engine's. Measured over 17 000
host frames at 48 kHz: the engine's block is never an earlier one than the
reference's; it is the reference's or the next, a millisecond later. Which of
the two depends on where the host call begins: under a host in blocks of 32
to 2048 frames (the powers of two) it is the reference's block for 56 to 59 %
of the calls, in blocks of 48, 96 or 480 frames for 17 %. To repeat a
recording under automation the renderer and the golden test hand a new Macro
over in front of the block the model names
(`AmanitaOceanFathomRender --macro-at-blocks`, by
`FathomEngine::nextCoreSampleForTesting`); the plug-in does not.

**No clock.** The oscillators of the network are Fathom's. A render that takes
up a session of the reference in its middle sets where they stood
(`setClockOriginsForTesting`: the first frame and the oscillators' origin; the
layer's blocks count from the same first frame). That is the only origin Spume
has; `setTransport` and `setReferenceArithmetic` do nothing to it.

## Freeze

Freeze is Ocean's own hold of Fathom's network; the reference's was never
moved in Foam. Held, a line of the network loses nothing in a pass and takes
no input. Between the two states the network's hold moves in a straight line
over 50 ms, 2205 internal samples, the first step in the first internal
sample behind the switch, and it lands on its end exactly. A network that is
silent takes the switch at once.

**The rule** (Ocean's own, by the owner's decision of 10 October 2026): the
diffuser takes its input by the same number. In every internal sample the
layer is given the share the network applies in that sample to what it writes
into its lines, one less the hold (`FathomNetwork::nextInputShare`), and the
diffuser's input is the layer's input times that share
(`SpumeLayer::process`). So the diffuser stops listening over the same 50 ms
in which the network does, takes exactly nothing while Freeze is fully on, and
listens again over the 50 ms of the release. Nothing else of the layer
changes: the diffuser runs on, its two gains move as they do, and with Freeze
off the share is one and every sample is what it was, to the bit.

Two things follow. What is played under Freeze is nowhere when Freeze ends:
not in the network, which did not take it, and not in the diffuser, whose
wash of it would otherwise come back with the release (before the rule a
burst that ended a second in front of the release returned at -6.6 dB of
itself, one that ended a quarter second in front of it whole). And what the
diffuser took before Freeze runs out as it would, by 28.4 dB a second; under
Freeze the network does not take it, and what is left of it at the release
goes into the network again.

The plain input is not the layer's to mute. The network takes or leaves it
itself, and what it scales in a sample is what left its input pipeline of 44
samples then, so against the input's own time the network's share runs a
millisecond ahead of the diffuser's, under a glide of fifty.

## Method

1. **Model.** The campaign recovered the mode from recordings of two attended
   sessions (A at 120 BPM; T90 at 90 BPM with the play head a quarter second
   ahead, another instance, as a blind test). The executable form is
   `foam_model.render`, the diffuser alone `foam_structure.kernel`.
2. **Constants.** `emit_spume_header.py` imports that model and writes
   `Source/dsp/SpumeConstants.h` from its constants, with the hashes of the
   model's files in the head; Ocean's own constant is set in the script.
3. **Port.** `SpumeDiffuser` implements the diffuser and `SpumeLayer` the
   crossfade at the engine's internal 44.1 kHz. They are checked against the
   model on golden vectors the same script renders from it
   (`Tests/SpumeGoldenVectors.h`): the diffuser's answer to an impulse, and
   renders of the engine at 44.1 and 48 kHz, one of them with Macro in motion.
4. **Recordings.** `Tools/FathomRender.cpp` renders a job of a recorded
   session with `--layer spume`: its first frame in the instance's count, its
   Decay, Size and Macro as the host set them, the origin of the instance's
   oscillators, and every change of Macro with its block.
   `score_spume_engine.py` nulls those renders against the recordings.

A null is `10 log10(sum (candidate - reference)^2 / sum reference^2)` over a
whole recording and both channels, with no gain, delay or polarity fitted.

## Validation

### The diffuser against the model

One `SpumeDiffuser` against `foam_structure.kernel`, the model's answer to a
unit impulse: over the first 12 000 samples, of which 806 are not zero, the
largest distance of a sample is 1.3e-18; the sum of the answer under hashed
weights and its energy, taken every 4000 samples as far as 60 000 (1.36 s,
where the answer is at its height), are the model's to 3.4e-15. The two inputs
go through two of the same, and `clear()` forgets everything.

The model writes an all-pass with two delay lines, input and output. The
engine holds one line per all-pass, `w[n] = x[n] - g w[n - D]`,
`y[n] = g w[n] + w[n - D]`, which is the same filter with half the memory; the
numbers above are of that form against the model's. The lines are double
precision.

### The port against the model

`emit_spume_header.py --engine` renders the eight cases of the golden vectors through the engine's renderer and
nulls each whole render (2.4 or 2.7 s) against the model:

| Case | Host rate | Macro | With the layer | The base alone | The diffused part alone |
| --- | --- | --- | --- | --- | --- |
| impulses48000 | 48 kHz | 100 % | -147.53 | -147.71 | -147.53 |
| burst48000 | 48 kHz | 50 % | -147.42 | -147.47 | -142.54 |
| tone48000 | 48 kHz | 25 % | -148.15 | -148.20 | -137.07 |
| late48000 | 48 kHz | 100 %; first frame 28 800 000, origin 23 964 028 | -147.47 | -147.36 | -147.47 |
| impulses44100 | 44.1 kHz | 50 % | -147.26 | -147.03 | -142.73 |
| burst44100 | 44.1 kHz | 100 % | -147.24 | -147.29 | -147.24 |
| tone44100 | 44.1 kHz | 25 % | -148.04 | -148.05 | -137.03 |
| motion48000 | 48 kHz | 20 %, then 90, 40, 0, 100 and, 16 ms behind that, 65 % | -147.63 | -147.60 | |

"The base alone" is the render at Macro 0 against the model's. "The diffused part alone" is the render less the
base at the plain input's gain, on both sides: the network is linear in its input, so that is its answer to the
diffused input alone; at Macro 25 % it is a small part of the output and stands on the same floor, 10 dB nearer.
The engine with the layer stands where the engine without it stands, at -147.2 to -148.2 dB. What limits it is the
engine: Fathom's lines hold their signals in single precision (`docs/FATHOM.md`: about 147 dB under its model).
Nothing of the layer is approximated.

`Tests/SpumeGoldenVectors.h` holds three excerpts of 160 frames from each case, at least 0.3 s apart: for a resting
Macro where the diffused part is strongest, for the vector that moves Macro behind three of its changes, where the
output differs most from the one whose change comes a block later. The test's limit is -130 dB; the worst excerpt is
at -145.70 dB. With every change of the moving vector handed over one block late, its excerpts stand at -35.09 dB
at best: the vector tells the block of a change from the next.

### The engine against recordings of the reference

`score_spume_engine.py`, whole recordings, the model's own score (`foam/findings/model_scores.json`, to a tenth of a
decibel as its logs give it) beside it.

| Recordings | Count | Model | Engine | Largest difference |
| --- | --- | --- | --- | --- |
| A, cases at fixed controls above Macro 0 (Macro 25 to 100 %, Decay 0.5 to 8 s, Size 60 to 150 %) | 14 | -99.8 to -130.1 | -99.79 to -130.00 | 0.10 dB |
| A, probes of 20 s from 610 to 2980 s (9 of them scored by the model) | 17 | -114.7 to -117.4 | -114.35 to -117.42 | 0.05 dB |
| A, impulse maps of 600 and 300 s, a tone of 140 s, a sweep | 4 | -104.1 to -121.2 | -104.14 to -121.16 | 0.05 dB |
| T90 at fixed controls above Macro 0 (probes, trains, time scan, tone on four block grids, Size 30 and 200 %, Decay 20 s, Macro 50 %, noise) | 13 | -97.0 to -117.8 | -97.05 to -117.81 | 0.05 dB |
| T90, Pre Delay 50 and 200 ms (in the engine) | 2 | -103.34, -103.14 | -103.34, -103.14 | 0.00 dB |
| T90, Width 0 and 150 %, Mix 50 % (the reference's laws in the renderer) | 3 | -103.35, -103.15, -112.80 | -103.35, -103.15, -112.80 | 0.00 dB |
| Macro 0, the base alone (A: c01, c16; T90: f01, n02) | 4 | -103.1 to -115.4 | -103.12 to -115.44 | 0.04 dB |
| A, x05: eleven gapless steps of Macro, 220 s, as one | 1 | -121.4 | -121.40 | 0.00 dB |
| T90, m01: Macro in motion, 90 plays and the tail, 66 s, as one | 1 | -115.6 | -115.55 | 0.05 dB |

Fifty-nine recordings, 43.7 minutes of the reference's output, 51 of them scored by the model as well; with the
parts of the two runs with Macro in motion taken singly the score has 81 rows, 73 of them beside a number of the
model. On all 73 the engine's null is the model's to the tenth of a decibel the model's logs hold (72 of them
within 0.05 dB). Above Macro 0 the engine stands at -97.05 to -130.00 dB, which is the depth the base reaches at
Macro 0: no recording separates the engine from the model, and none separates Spume from Fathom's base. Every row
is in `docs/SPUME_VALIDATION.json`.

Not scored: x01 (silence in; the recording peaks at 8e-37 and there is nothing to null against), the second
recording of c15 (the session turns Decay down at its start, and neither the model nor the renderer moves Decay in a
render), and the six cases of T90 that turn a control of the reference the engine does not have: its high-pass and
low-pass filters in front of the converter (k03, k04), Brightness (k08, k09), Ducking (k10) and Transients (k11).

### Macro in motion

Both runs are rendered in one piece from the first frame of their first recording, with every change handed over at
the block the model names for it.

x05 of session A: eleven gapless steps of 20 s under a running tone of 1 kHz, Macro 0, 10, ..., 100 %.

| Step to | Whole step: model | engine | First 0.2 s: model | engine |
| --- | --- | --- | --- | --- |
| 0 % (the base) | -121.0 | -121.01 | -123.0 | -122.98 |
| 10 % | -121.7 | -121.71 | -121.3 | -121.28 |
| 20 % | -122.1 | -122.13 | -121.2 | -121.21 |
| 30 % | -122.4 | -122.35 | -120.4 | -120.37 |
| 40 % | -120.2 | -120.17 | -121.8 | -121.73 |
| 50 % | -120.2 | -120.20 | -121.1 | -121.07 |
| 60 % | -121.4 | -121.41 | -121.6 | -121.60 |
| 70 % | -122.3 | -122.30 | -122.4 | -122.38 |
| 80 % | -122.2 | -122.19 | -121.0 | -121.00 |
| 90 % | -121.2 | -121.18 | -121.7 | -121.66 |
| 100 % | -122.0 | -121.96 | -119.7 | -119.73 |

m01 of session T90: nine steps of 6.25 s under a running tone of 1 kHz left and 3 kHz right, then a staircase of 81
plays of 50 ms from 0 to 100 % and back, each move arriving before the last has settled, then six seconds of silence
at Macro 0.

| Part | Whole part: model | engine | First 0.25 s: model | engine |
| --- | --- | --- | --- | --- |
| at 0 % | -115.9 | -115.90 | -115.6 | -115.64 |
| 0 to 30 % | -116.3 | -116.32 | -117.3 | -117.30 |
| 30 to 70 % | -117.3 | -117.26 | -117.7 | -117.70 |
| 70 to 100 % | -116.6 | -116.55 | -115.7 | -115.66 |
| 100 to 55 % | -115.5 | -115.53 | -117.6 | -117.57 |
| 55 to 10 % | -115.9 | -115.86 | -117.0 | -116.98 |
| 10 to 100 % | -116.6 | -116.63 | -116.2 | -116.17 |
| 100 to 0 % | -112.1 | -112.10 | -115.7 | -115.71 |
| 0 to 50 % | -115.8 | -115.83 | -115.6 | -115.59 |
| the staircase, 4.05 s | -115.7 | -115.66 | | |
| the tail | -115.6 | -115.64 | | |

The gains in motion stand where the gains at rest stand, in both instances. Unlike Undertow, whose Macro smoothing
was fitted and holds the quarter second behind a step at -41 to -75 dB, the law here is exact as far as the
recordings resolve it.

### Tests

`Tests/DspTests.cpp` has 102 tests: the 89 from before Spume and thirteen new ones. Ten are Spume's, which
`--test-spume` runs alone: the nine of the port and the one of the Freeze rule. One is Undertow's test of the same
rule, which `--test-undertow` runs with Undertow's twelve, and one is Fathom's (`docs/FATHOM.md`); `--test-freeze`
runs the three together. One holds the level stages and Sub Anchors of the three engine Characters and has a flag
of its own, `--test-engine-stages`. One line of an existing test was changed, as
the lead allowed: "Fathom sample rates and stability" takes the value behind the last mode for its mode that does
not exist, and that is now `spume + 1`. No existing test asserted that a layer takes input under Freeze, so none
was changed for the rule: the two older Freeze checks, in the determinism tests of Spume and of Undertow, hold that
input does not reach a held network through the layer, which is true before and after.

Result in `build-spume-dsp` (Release, arm64): 102 passed, 0 failed, in 94 s; every `[METRIC]` line of the 89 older
tests (269 lines) is the line of the committed tree built in the same folder, character for character, but for one
of Fathom's under Freeze, which moved with Fathom's own rule (`docs/FATHOM.md`); and so is every line of the nine
tests of the port after the Freeze rules went in. Twenty-four renders of the renderer with
the Tide and the Undertow layer (three rates, with and without the outer laws, Macro steps, transports running and
stopped) are the same bytes from the renderer of the committed tree and from the final one. Scored again by the
final renderer, 60 of the 81 rows of the Spume score and 52 recordings of the Undertow score (sessions E, T90, S90
and the sixteen Macro steps of F) give every stored number again. `AmanitaOceanStateTests` (the colleague's, with
its checks of the eighth choice): passed, exit code 0, 8.3 s; its metric lines are those of the run before the
rule but for four of Fathom's that follow the random voice seed of each instance and differ from run to run.

| Test | What it holds |
| --- | --- |
| Spume diffuser against the model | the constants agree about themselves; the diffuser's answer to an impulse is the model's over 12 000 samples (1.3e-18) and by its sums to 60 000 (3.4e-15); left and right are two of the same; `clear()` forgets, to the bit |
| Spume engine golden vectors | 24 excerpts of the model at 44.1 and 48 kHz, Macro 25, 50 and 100 %, one with the origin of a session, one with Macro in motion: every excerpt under -130 dB (worst -145.70); with the changes of Macro handed over a block late the moving vector misses by -35 dB and more |
| Spume at Evolution 0 | the engine with the layer is Fathom's engine to the bit at 44.1, 48 and 96 kHz; again after a visit at Macro 100 % once it has fallen silent; and a settled Spume in the plug-in is a settled Fathom and a settled Undertow to the bit, with Width, Mix and pre-delay away from neutral |
| Spume engine determinism, reset, Pre Delay and Freeze | two instances, `reset()`, `prepare()` at another rate, processed silence against `advanceIdle()`, a host that says its transport, sound followed by 1, 2 and 3000 idle frames: the same bits each time; silence in gives exact silence out at Macro 100 %; an engine with 50 ms of Pre Delay is the engine without it on the input 50 ms late, to the bit; under Freeze input does not reach the network through the layer |
| Spume independence of the host's block length | `FDNReverb::process` in calls of 1, 32, 127, 512 and 1000 frames, and of 64 under a host that says another transport at every block, gives the same bits at 44.1, 48 and 96 kHz while Evolution moves five times |
| Spume Evolution steps and ramps | from the output alone, on a constant the diffuser has settled on: the gains move at the samples 43 modulo 44 and nowhere else, by 0.0949574 of the way (1 - exp(-44/441)), on the one-pole of 10 ms at the block rate to 4.5e-13, a new Evolution entering at the first block that begins once it has arrived, and they arrive; at Macro 0 the layer is at rest and hands its input on to the bit. On two tones: the layer is its input and the diffuser's output by the law's gains to 1.1e-12 through 25 steps of Macro and through a ramp in host blocks of 256 frames; no step of Macro puts more into a sample than the law's share, and a ramp nothing at all (see below); the engine stays in range under steps on noise |
| Spume silence, denormals and hostile input | the diffuser never holds a denormal and ends in exact silence 22.6 s behind half a second of tones; denormal input is silence; NaN, infinities and full-scale noise with parameters at their extremes stay inside the clipper's range, and the engine returns to exact silence behind the diffuser's wash |
| Spume engine allocation-free processing | no allocation in `processSample`, `advanceIdle`, `setParameters` and `reset()`, nor in `FDNReverb::process` across switches between Fathom, Spume and Undertow with Evolution moving |
| Spume under Freeze takes no new input | at Evolution 100 %, 48 kHz, a Freeze of five seconds: a burst of a quarter second played wholly inside it leaves the output as it is without the burst, to the bit, from the first frame to the last, whether it ends 1.05 s or 50 ms in front of the release, in the engine and through the plug-in; a sound that began before Freeze and went on two seconds into it is, to the bit, that sound ended where the hold's glide ends; at Evolution 0 the engine is Fathom's to the bit through a Freeze and across both its edges at 44.1, 48 and 96 kHz. The layer alone: what it hands on is its input by one gain and the diffuser's answer to the input by its share by the other (distance 0 and 1.1e-16); no click where the hold begins or ends (see below); at Macro 0 it is at rest under any share. The engine: an impulse on the last steps of the hold's glide gives the engine that never held, given the impulse by the share of its sample, to -142.5 dB, and by the share of the next sample -66.8 dB |
| Engine Characters' own level stages and Sub Anchors through switches | Mono Safe on, Width 150 %, bursts at 0.9 that drive the level stage, Fathom to Spume to Fathom to Undertow to Spume to Undertow with the input running on: the plug-in is the three engines with a level stage and a Sub Anchor each, from rest at every visit, through every crossfade (distance 0, allowed 4e-6 of the peak); every second visit is, to the bit, that of a plug-in that had never been in the Character. The same stages without the restart of the level stages stand 0.0019 away, without that of the anchors 1.6e-5 |
| Spume routing, crossfades and return through the plug-in | at Ocean's neutral controls the plug-in is the engine's wet through its level stage to the bit (four rates, Evolution 0, 45 and 100 %), whatever the host says of its transport; Spume crossfades directly with Fathom and with Undertow, both ways (distance from the crossfade of the two chains 0 to 1.1e-8 at a peak of 0.1); Fathom to Undertow and half a fade later on to Spume is the three chains by their amounts (distance 0, 4799 samples with all three) and nothing of the FDN; Default and Spume crossfade as Default and Fathom do; left for longer than its fade, to Bloom or to Fathom, with the input running on, Spume returns as an engine that only kept time, to the bit; stress at four rates with Evolution in motion, Freeze and input that is no signal |

**The click measure.** Two steady tones (220 and 330 Hz, 0.5 each) go into the layer; the measure is the largest
second difference of what the layer hands the network. A tone of amplitude a and angular frequency w gives a w^2, a
step of height h gives h. With Macro at rest at 62 % the measure is 0.00186. Under 25 steps of Macro between 0 and
100 % it is 0.0976: the first block behind a full step, 9.5 % of the way, as the law has it and the reference does.
The same steps taken at once read 0.849, almost nine times that. Under the ramp (0 to 100 % in three seconds and back to
20 % in two, in host blocks of 256 frames) it is 0.00177, the tones' own.

Through a hold of four seconds, with the share on the network's glide of 2205 samples at either end, the measure is
0.00160 at Macro 100 % (0.00150 without the hold) and 0.00195 at 62 % (0.00186). With the share taken at once, at
a place where the tones are not at zero, it reads 0.0167 and 0.0138. The limit is 1.5 times the measure without
the hold.

### Sanitizers

`build-spume-sanitize` (Debug, arm64, `-fsanitize=address,undefined -fno-omit-frame-pointer`), built from the final
sources but for a comment added to `FathomEngine.h` afterwards. `AmanitaOceanDSPTests --test-spume`, the ten tests of
Spume and "no allocations in process": exit code 0 and no report from either sanitizer, in 10 min 51 s.
`--test-undertow`, Undertow's thirteen: the same, in 11 min 6 s. `--test-engine-stages`: the same, in 50 s. Every
`[METRIC]` line is that of the Release build to the last digit. With Fathom's rule in as well, `--test-freeze`, the
three Freeze tests: exit code 0 and no report, in 8 min 40 s. Before the Freeze rule the nine tests of the port had
run alone, with the same result, in 7 min 39 s. The 89 tests from before Spume, but for Undertow's twelve, and the
state tests were not run under the sanitizers again.

## What is exact, and how far

| | |
| --- | --- |
| The diffuser against its model | to rounding: 1.3e-18 on its answer to an impulse |
| The layer against its model | to the depth of the engine itself, about -147 dB, Macro in motion included: no part of the layer is approximated |
| Evolution 0 | Fathom at Evolution 0 to the bit, in the engine and through the plug-in, also through a Freeze; so is Undertow at Evolution 0 |
| What is played wholly under Freeze | gone when Freeze ends, to the bit |
| The reference at Macro 0 | -103 to -121 dB (the base with the origin of its oscillators) |
| The reference above Macro 0, two instances | -97 to -130 dB: the depth of the base, and the model's numbers to a tenth of a decibel |
| Macro in motion, with each change handed over at the reference's block | -112 to -123 dB, whole steps and the first 0.2 or 0.25 s behind them alike |
| The plug-in under Evolution automation against the reference | the reference's block or the next, a millisecond later, at 48 kHz; in host blocks of 512 frames the next for 41 % of the calls |

## CPU and memory

One core of an Apple M4 Max, stereo, Release, 20 s of noise per figure (`AmanitaOceanFathomRender --benchmark`),
best of three runs with other work on the machine; percent of one core.

| | 44.1 kHz | 48 kHz | 96 kHz |
| --- | --- | --- | --- |
| Engine with Tide at Macro 100 % (Fathom) | 1.40 | 1.51 | 1.56 |
| Engine with Tide at Macro 0 | 1.40 | 1.49 | 1.54 |
| Engine with Undertow at Macro 100 % | 1.67 | 1.70 | 1.80 |
| Engine with Spume at Macro 100 % | 1.53 | 1.63 | 1.80 |
| Engine with Spume at Macro 50 % | 1.54 | 1.64 | 1.80 |
| Engine with Spume at Macro 0 | 1.52 | 1.62 | 1.80 |
| `advanceIdle` with Tide | 0.051 | 0.064 | 0.080 |
| `advanceIdle` with Spume | 0.052 | 0.063 | 0.081 |

The layer costs 0.12 to 0.13 % of a core at 44.1 and 48 kHz and the same at every Evolution, because the diffuser
runs at every Evolution. At 96 kHz it reads 0.24 %, although the diffuser does the same work per second there; the
cause was not looked for (it touches 56 places in 2.9 MB per internal sample, between which the converters of the
higher rate do more). In the plug-in every Character now carries one more idle engine: 0.05 to 0.08 % of a core
while Spume is not selected. The whole DSP of the plug-in in Spume was not timed.

Memory of the layer, allocated in `prepare()` of an engine with `Layer::spume` only: 2.9 MB (28 lines of 179 737
frames in all, 4.08 s, left and right, double precision). With the memory of the third engine itself (its lines,
converters and pre-delay, about 3 MB at 48 kHz) the plug-in holds about 6 MB more per instance than before.

## Properties and limits

- **The wash has its own length.** It falls by 28.4 dB a second whatever
  Decay and Size are, and it rises for a second first. At Evolution 100 % and
  the shortest Decay the tail is the diffuser's: 60 dB down some 3.1 s behind
  the input; the diffuser alone is in exact silence 22.6 s behind half a
  second of tones. The tail the plug-in reports to its host is the
  processor's matter; the colleague's state tests measure 3.26 and 3.34 s to
  -60 dB at Decay 0.2 and 0.5 s.
- **Level.** The diffuser has unit energy, and the two gains are a crossfade
  of equal power: on noise the wet holds its level within 0.02 dB from
  Evolution 0 to 100 % (measured on this build, 12 s at Decay 2 s). On a
  steady tone the plain and the diffused input add with the diffuser's phase
  and magnitude at that frequency, so the level between 0 and 100 % depends
  on the frequency; the campaign measured +2.99 dB at 1 kHz and 50 % in the
  reference.
- **The gains move in stairs**: up to 9.5 % of the way once a millisecond.
  That is the reference's law, measured, and it is in the plug-in as it is.
  It was not listened to. Ocean puts no glide of its own in front of it.
- **Under automation the plug-in is not the reference to the sample.** A new
  Evolution enters at the reference's block or one later (see "Evolution in
  motion"). Only the renderer and the golden test place it exactly.
- **One host rate was recorded: 48 kHz, in blocks of 512.** The layer runs at
  44.1 kHz inside at every host rate, so nothing in it depends on the rate.
  The golden vectors at 44.1 kHz check the port against the model, not the
  model against the reference, and the reference's rule for the block of a
  new Macro was seen at 48 kHz only.
- **The reference's Transients and Brightness are not part of Ocean.** In Foam
  the reference's Transients stage sits in front of the diffuser and, turned
  up, rebuilds every onset before the diffuser sees it; Ocean has no such
  stage, and no law for it exists. Brightness acts in the network and has no
  law either.
- **The reference's input filters are not Ocean's.** Its high-pass and
  low-pass act at the host rate in front of the converter, so in front of the
  diffuser. Ocean's Low Cut and High Damping are filters of its own inside
  the network's loop: the diffuser hears the unfiltered input, and what it
  hands on meets them in the network.
- **A change of mode under signal was never recorded.** The reference was
  seen to empty everything on a change of mode in silence. Ocean crossfades
  the Characters over 200 ms and lets a Character that is left ring out over
  its fade; one that is entered after it was left for longer than its fade
  starts empty. Spume entered from another Character therefore begins without
  the wash of what was played before, and its oscillators stand where time
  put them, not at their start as in the reference.
- **Input above 0.5 was not recorded.** The layer is linear and the engine
  bounds its input at 64; the level stage and the clipper are Fathom's.
- **The precision of the reference's all-passes is not known.** Double
  precision nulls at the base's own depth, about -100 dB; single precision
  would not show there.
- **The delays follow no law the engine uses.** The campaign reads them as a
  geometric series from 30 to 400 ms moved to numbers without common factors;
  that is a reading of 28 numbers. The engine has the 28 numbers.
- **Freeze** is Ocean's own and was not moved in Foam. What the diffuser
  took before Freeze is not added to what is held, because the held network
  takes nothing; it runs out in the diffuser by 28.4 dB a second, and what
  is left of it when Freeze ends is heard again: of the wash of a sound
  just in front of a Freeze of one second about half, of five seconds
  113 dB less. That is the sound from before Freeze, which the network holds
  anyway; nothing played under Freeze is in it. Whether what comes back of
  it behind a short Freeze is welcome was not listened to.
- **Not seen in the reference**: the two gains at a change of mode, a change
  from Abyss to Foam, another block size than 512.

## Deviations from the specification

1. **`setLayer` takes effect at the next `prepare()`**, as for Undertow: the
   diffuser's memory is allocated there and `reset()` may not allocate.
2. **Two rules of Ocean's own beyond the floor of the lines**, where none was
   expected: a gain within 1e-12 of its target is on it, without which a
   settled Spume at Evolution 0 would not be Fathom to the bit; and the block
   a new Evolution enters at, in the specification's words "the block in
   which the change arrives", made exact here as the first block that begins
   at or after the arrival, because a block that has begun holds its gains.
3. **The all-pass has one delay line**, not the model's two (see "The diffuser
   against the model"), in double precision.
4. **`FathomEngine::nextCoreSampleForTesting()`** is new in the engine's
   interface: the internal sample the core computes next. The renderer and
   the golden test need it to hand a change of Macro over at a block; it reads
   a counter and changes nothing.
5. **The third engine is a third amount.** `FDNReverb` blends Fathom and
   Undertow by their amounts and the two with Spume by theirs; the amount of
   the three together is the ramp Undertow brought (`engineAmount_`), which now
   stands at one whenever any of the three is selected.

## Not run and open

- No listening test, no DAW, no plug-in validator. No plug-in bundle was built or rendered here: the plug-in's
  routing is checked in the DSP tests through `FDNReverb`, and in the colleague's state tests through the processor.
- No build for x86_64, with GCC or MSVC, or with `-ffp-contract=fast` on the command line. The diffuser and the
  layer include `FathomExactArithmetic.h` and are compiled with `-ffp-contract=off` like the engine.
- Under the sanitizers only the new tests and Undertow's ran, not the other older ones and not the state tests.
- The Freeze rule was tested in the engine and through `FDNReverb`, not in a host. Fathom's own layer has been
  under the same rule since the same day: its comb takes no input under Freeze either (`docs/FATHOM.md`), so the
  three Characters that are engines agree. What none of them holds back is the network's input equaliser, which
  rings for some ten milliseconds: a burst that ends 10 ms in front of the release leaves -138.5 dB of itself
  with Spume at Evolution 0 and nothing at 100 %, where the plain input is out.
- Host rates in the tests: 44.1, 48, 88.2 and 96 kHz through the plug-in, 44.1, 48 and 96 kHz in the engine, 44 101
  Hz for allocations. No recording of the reference exists at any rate but 48 kHz.
- The second recording of c15 (a ring-out with Decay turned down) was not scored: it needs Decay in motion.
- The cost of the layer at 96 kHz, twice that at 48 kHz, was measured and not explained.
- The level of Spume against the other Characters was not measured beyond the wet on noise against Evolution 0.
- No holdout exists for Foam. Session T90 is the blind test of the model; the engine was scored on everything the
  model was scored on, and nothing of the engine was tuned on any recording.

## Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cmake -S . -B build-spume-dsp -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=/Users/nespesha/Workspace/Juce \
      -DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=$PWD/build-release/_deps/clap_juce_extensions-src
cmake --build build-spume-dsp --target AmanitaOceanDSPTests AmanitaOceanStateTests AmanitaOceanFathomRender
./build-spume-dsp/AmanitaOceanDSPTests                 # everything
./build-spume-dsp/AmanitaOceanDSPTests --test-spume    # the Spume tests alone
./build-spume-dsp/AmanitaOceanDSPTests --test-engine-stages   # the outer stages of the three engine Characters
./build-spume-dsp/AmanitaOceanStateTests
./build-spume-dsp/AmanitaOceanFathomRender --benchmark --layer spume --macro 1
cd Analyzer/Campaigns/RevOceanCharacterization
$PY emit_spume_header.py --check        # the two generated headers against the model
$PY emit_spume_header.py --engine       # whole renders of the engine against the model
$PY score_spume_engine.py               # every group; about a minute and a half
$PY score_spume_engine.py --table
```

The model, the findings and the session scripts are in the campaign folder
(`foam/`, with `abyss/model/base.py` and `abyss_core.c` for the base), so the
two generated headers can be made again from the tracked tree alone. The
recordings are under `Analyzer/Results` (not in Git): only
`score_spume_engine.py` needs them. Its scores are written to
`Analyzer/Results/RevOceanCharacterization/work/foam/engine/scores.json`;
every row of them is also in `docs/SPUME_VALIDATION.json`.
