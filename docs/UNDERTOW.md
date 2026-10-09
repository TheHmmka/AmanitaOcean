# Undertow: development characterisation

The seventh Character of Amanita Ocean. This document says what it is, what of
it was measured and what is Ocean's own, how far it has been checked and what
has not been checked. It is a development record, not a product description.

## Status

A local development model in the working tree, not a release. Nothing is
installed, published or signed, the version is unchanged, and nothing was
listened to: every statement below is a measurement. The numbers are in
`docs/UNDERTOW_VALIDATION.json`.

The DSP is in: the layer, the engine that carries it, the plug-in's routing,
the tests, the offline renderer and the scores. The editor, the processor's
side of the host transport and the state are a colleague's work and are not
described here.

## Scope and identity

| | |
| --- | --- |
| Character | index 6 of the host parameter "Character" (the seventh choice); `ReverbMode::undertow` in `Source/dsp/FDNReverb.h` |
| Layer | `Source/dsp/UndertowLayer.h` (what it is and its contract), `UndertowLayer.cpp`, `UndertowConstants.h` (generated) |
| Engine | Fathom's: `Source/dsp/FathomEngine.h` with `Layer::undertow`, `setTransport`, and the two test hooks `setReferenceArithmetic` and `setClockOriginsForTesting` |
| Reference | Arturia Rev OCEAN in its Macro Mode Abyss; the instance and the host of the recordings are those of the campaign (`docs/FATHOM.md`, "Scope and identity") |
| Campaign | `Analyzer/Campaigns/RevOceanCharacterization`; everything of Abyss is in its folder `abyss/`, and its `README.md` has a section "Abyss (Undertow)" |
| Model | `abyss/model/abyss_model.py` with `hostclock.py`, `base.py`, `abyss_core.c` |
| Findings | `abyss/findings/`: `ABYSS_STATE.md` (the layer), `tempo.md` (tempo, play head, stopped transport), `controls.md` (the outer controls and Macro in motion); behind them `ABYSS_FIRST_LOOK.md`, `model_structural.md`, `score.md`, `verification.md`, and the model's own scores, `tempo_scores.json` |
| Sessions | `abyss/sessions/` (the scripts that made the recordings) and `abyss/sessions.json` (tempo, reported position and clock origins of each session); the recordings themselves are under `Analyzer/Results/RevOceanCharacterization/work/abyss/`, which is not in Git |
| Scripts | `emit_undertow_header.py` (constants and golden vectors), `score_undertow_engine.py` (the engine against the recordings), both in the campaign folder |

The reference was measured as a black box, from recorded audio of attended
sessions alone, as for Fathom. Its name appears in documentation only.

## What the Character is

Undertow is Fathom's network with another layer in front of it. Fathom's layer
(the reference's Tide) is a comb at the input and two voices at each output;
Undertow's makes three voices from the input and adds them to it:

```
in -- pre-delay -- converter -- u --+--------------------------------------------(+)-- Fathom's network at Macro 0 -- converter -- wet
                                    |                                             ^
                                    +-- reversed, 4/3 quarter notes ------ gain -->|   at pitch
                                    +-- reversed, 2 -- recirculation -- gain -- grain reader -->|   octave up
                                    +-- reversed, 8/3 ---------------- gain -- grain reader -->|   octave down
```

- **A reversed reader** plays the input backwards in chunks of a note value.
  Each chunk mirrors the input about the point where it begins, under a window
  that fades in over 21 to 38 % of the chunk and out over 7 to 15 %. A sound
  therefore returns reversed, one to two chunks after it was played.
- **The recirculation** of the octave-up voice feeds its reversed stream back
  over one chunk at 0.198 a pass: 14.1 dB per second at 120 BPM, whatever
  Decay is.
- **A grain reader** reads its stream through two taps half a cycle apart,
  each under a sine window and with a delay that sweeps over 5300 samples:
  rising delay an octave down, falling delay an octave up (not exact octaves:
  0.499 and 1.99 / 2.01). Left and right have separate octave-up readers.
- **Evolution** brings the voices in one after the other: at pitch from 0 to
  30 %, octave up from 14.6 to 46 %, octave down from 60.4 to 100 %. Each gain
  follows its ramp through a one-pole of 10 ms. Buffers, recirculation and
  phasors run at every Evolution; at 0 % nothing is added and the Character is
  Fathom at Evolution 0 to the bit.
- **Everything of the layer follows the tempo** that should: chunk lengths,
  their windows and the recirculation delay. The grain readers and the gains
  do not.

Ocean's controls act as in Fathom, because the layer is part of the input:

| Ocean control | In Undertow |
| --- | --- |
| Decay, Size, Low Cut, High Damping, Freeze | Fathom's network; none of them reaches the layer. Under Freeze the network takes no input, so nothing of the layer enters either |
| Pre-delay | in front of the layer: the reversed copies are made from the delayed input |
| Evolution | the knob itself, as in Fathom; here it moves the three voice gains |
| Width, Mix, Focus, Harmony, Mono Safe | exactly Fathom's routing: engine wet, level stage, Harmony, Width, Focus, Ocean's linear Mix, the reference's clipper |

The plug-in leaves through Ocean's own linear Mix for Undertow exactly as for
Fathom. The reference's Mix law is in the offline renderer and the tests only.

## What was measured and what is Ocean's own

| | Source | Status |
| --- | --- | --- |
| Base untouched, the layer added in front of the equaliser, left and right separate | `ABYSS_STATE.md` 1 | measured |
| Chunk lengths 4/3, 2 and 8/3 quarter notes, held as single-precision numbers | `tempo.md` 4 | measured at 90 and 120 BPM |
| Mirror law of a chunk (which block takes a boundary and where the mirror point lies) | `tempo.md` 3.1 and 5 | measured: 537 copies within 0.01 sample |
| Windows of the chunks | `ABYSS_STATE.md` 2 | fitted; that they are fixed shares of the chunk is measured |
| Recirculation: one chunk, 0.198 a pass | `tempo.md` 4 | delay measured, gain fitted |
| Grain readers: increments, sweeps, shortest delays, start of the phasors | `ABYSS_STATE.md` 2 and 3 | fitted |
| Voice gains and the three ramps | `ABYSS_STATE.md` 4 | fitted at 120 BPM; only the knee at 30 % is firmly pinned |
| One-pole of 10 ms on each gain, behind the reversed readers, in front of the grain readers | `controls.md` 3 | fitted on sixteen steps of one recording |
| Pre-delay, filter, Width, level stage in their places around the layer | `controls.md` 2 | measured |
| The position read once per host block and per converter block in single precision | `tempo.md` 3 | measured; kept as a test hook, see "The clock" |
| A stopped transport: the layer counts by itself at the host's tempo | `tempo.md` 5 | measured on one session |
| **A transport that starts, stops, jumps, loops or changes its tempo, and a position that tells nothing** | | **Ocean's own** |
| **No tempo from the host: 120 BPM. Tempi outside 20 to 999 BPM: the nearer end** | | **Ocean's own** |
| **The position in double precision on the input's own time** | | **Ocean's own** (the specification of this Character) |
| **Low Cut, High Damping, Freeze, Focus, Harmony, Mono Safe, Ocean's Mix** | | **Ocean's own**, as in Fathom |

In the code everything that is Ocean's own is marked as such where it stands
(`UndertowLayer.cpp`), and its constants have a block of their own at the end
of `UndertowConstants.h`.

## The clock

The chunks are note values on the host's timeline, so the layer needs the
host's position. `FDNReverb::setHostTransport` takes it in front of every
block (position of the block's first frame in quarter notes, tempo, whether
the transport runs, whether there is a tempo) and hands it to the engine.

**The engine's own arithmetic** (what the plug-in runs). The position is a
line in double precision through the first block the host announced: internal
sample against quarter note, on the input's own time (an internal sample has
the position of the host frame it is centred on). A chunk begins in the
internal block of 44 samples that holds its boundary, with the mirror law the
reference shows where its own clock is exact (a stopped transport). As long as
the host runs on by its frames, the line is not touched: the render does not
depend on the length of the host's blocks, to the bit, and hours into a
session a chunk still begins on its note value to a millionth of a sample.
With a stopped transport, or without a position, the layer counts quarter
notes itself from `reset()` at the tempo it is given.

**The reference's arithmetic** (`FathomEngine::setReferenceArithmetic(true)`;
a test hook). The reference takes the position as a single-precision number
once per host block, adds the way to each of its converter blocks of 48 frames
in single precision again, and lets that stamp stand for a block of 44 internal
samples that lies up to a millisecond elsewhere. The engine repeats this
exactly, with the block layout the host has, and with three origins of the
instance's clocks (`setClockOriginsForTesting`): this is what lets it be
nulled against a recording. It is not what the plug-in runs, for two reasons.
The single-precision stamps move single chunks by a step of the position that
grows with it: 0.06 ms after ten minutes at 120 BPM, 0.5 ms after ninety, 2 ms
and more behind 4.6 hours. And the stamp belongs to the converter block, not to
the samples, so the boundaries wander by up to a millisecond from chunk to
chunk even where the stamps are exact.

The two arithmetics are two clocks of one grid. Their boundaries lie up to a
millisecond or two apart, so **the plug-in in its own arithmetic does not null
against the reference above Evolution 0**; only the reference's arithmetic
does. With a stopped transport the two are the same arithmetic and the same
bits up to 512 quarter notes; behind that the reference shows a drift of
0.00005 sample per quarter note that nobody has explained, which the
reference's arithmetic carries as a fitted curve and the engine's own does not
have.

**Ocean's own rules**, where nothing was measured:

- **A start, a stop and a jump of the position** begin every reader anew: the
  next chunk begins at the next internal block and ends where the next one of
  the grid begins. The position has jumped when it lies more than two
  internal blocks (2 ms) from where the tempo lets it be expected, or more
  than 4 % of the time since the host's last word if that is more.
- **A chunk that is cut** while its window is above -60 dB fades out over 5 ms
  beside the chunk that replaces it, and a reader that still has such a chunk
  fading waits for it before it begins anew. So nothing clicks.
- **The tempo.** Every change is followed at once: each chunk takes the tempo
  of the moment it begins, and the recirculation takes a new delay through a
  crossfade of 5 ms. Whether the readers also begin anew is decided by what
  the tempo does over time, not by the size of one change. A change that has
  no other change within a quarter of a second, nor at the host block before
  or behind it, is a step: if it is larger than 1 %, every reader begins anew
  once, when that quarter second has passed. Changes nearer to each other
  than that are a ramp and are only followed. A quarter of a second is longer
  than a host block of 8192 frames (186 ms at 44.1 kHz) and than a sixteenth
  note at 60 BPM, the stairs in which a sequencer may draw a tempo curve; so a
  ramp is a ramp in host blocks of any length, smooth or in stairs. While the
  tempo moves the position is expected anywhere between where the old and
  where the new tempo put it.
- **A position that drifts** by less than a jump is followed without a new
  start. What it strays by is summed, and so is the time it had for that,
  both with a memory of 50 ms; a sum beyond 2 ms and beyond 4 % of its time
  counts as a jump. So a position that runs more than 4 % faster or slower
  than the tempo is found out in host blocks of any length, and one that runs
  off by less is followed.
- **A position that tells nothing.** After three jumps in a row, each less
  than a quarter of a second behind the one before or at the next host block,
  the layer stops following the host. It keeps time itself at the host's
  tempo, exactly as under a stopped transport, with one new start of its
  readers; in the end it gives the stopped transport's signal. It follows
  the host again, with one new start, once the reported position has run
  for one second without a jump. This is what happens under a host that says
  its transport runs while its position stands, whose position is another one
  at every block, or whose position runs while it reports no tempo and the
  true tempo is not 120 BPM. The three numbers:
  - *Three jumps.* One is a locate or a loop; two in a row can be a locate
    that lands on the end of a loop; three in a row are no transport that
    plays. One or two behave as a jump always did.
  - *A quarter of a second between them.* A transport that jumps more often
    leaves no reader the time to sound (the shortest fade-in of a chunk is
    137 ms at 120 BPM), so a loop shorter than a quarter second is taken as a
    position that tells nothing. Loops of a quarter second and longer are
    followed as loops.
  - *One second to return.* Four times that quarter: a position that stutters
    is not taken up between two stutters, and a host that is in order again
    is followed within a bar.
- No tempo is 120 BPM. A tempo outside 20 to 999 BPM is the nearer end; the
  layer's memory is sized for 20 BPM. A position that is no number is a
  stopped transport.
- While another Character is selected the engine only keeps time, the layer's
  clocks with it; Undertow returns from silence with its chunks where a fresh
  instance that had idled as long would have them.

## Method

1. **Model.** The campaign's packets recovered the layer from recordings of
   five attended sessions (D, E, F at 120 BPM; T90 at 90 BPM with the play
   head a quarter second ahead; S90 at 90 BPM with the transport stopped). The
   executable form is `abyss_model.render` with `context["clock"] = "host"`.
2. **Constants.** `emit_undertow_header.py` imports that model and writes
   `Source/dsp/UndertowConstants.h` from its constants, with the hashes of the
   model's files in the head; Ocean's own constants are set in the script.
3. **Port.** `UndertowLayer` implements the model at the engine's internal
   44.1 kHz and is checked against it on golden vectors the same script
   renders from the model (`Tests/UndertowGoldenVectors.h`).
4. **Recordings.** `Tools/FathomRender.cpp` renders a job of a recorded
   session: the host's blocks of 512 frames that begin anew with every step of
   the job, its tempo, the position it reported, whether its transport ran,
   and the origin of the instance's oscillators, phasors and free-running
   clock. `score_undertow_engine.py` nulls those renders against the
   recordings.

A null is `10 log10(sum (candidate - reference)^2 / sum reference^2)` over a
whole recording and both channels, with no gain, delay or polarity fitted.

## Validation

### The port against the model

`emit_undertow_header.py --engine` renders the eight cases of the golden vectors through the engine's renderer, in
the reference's arithmetic, and nulls each whole render (3.3 or 4.2 s) against the model:

| Case | Host rate | Tempo | Macro | Transport | With the layer | The base alone | The layer alone |
| --- | --- | --- | --- | --- | --- | --- | --- |
| impulses48000 | 48 kHz | 120 | 100 % | running | -146.52 | -147.71 | -145.03 |
| burst48000 | 48 kHz | 120 | 60 % | running | -147.20 | -147.47 | -146.53 |
| tone48000 | 48 kHz | 90 | 100 % | running, play head 0.25 s ahead | -147.93 | -148.20 | -147.64 |
| stopped48000 | 48 kHz | 90 | 30 % | stopped | -146.93 | -147.71 | -145.35 |
| late48000 | 48 kHz | 120 | 100 % | running; first frame 28 800 000, origin 1 323 432 | -147.14 | -147.42 | -146.55 |
| impulses44100 | 44.1 kHz | 120 | 60 % | running | -147.25 | -147.03 | -147.60 |
| burst44100 | 44.1 kHz | 90 | 100 % | stopped | -147.28 | -147.29 | -147.27 |
| tone44100 | 44.1 kHz | 120 | 30 % | running | -148.07 | -148.05 | -148.10 |

"The layer alone" is the difference of the renders at that Macro and at Macro 0, on both sides. The engine with the
layer stands where the engine without it stands, at -146.5 to -148.1 dB. What limits it is the engine: Fathom's
lines hold their signals in single precision (`docs/FATHOM.md`: about 147 dB under its model), and the layer keeps
its input and its recirculation in single precision too. The layer's interpolation costs nothing, because it is
the model's own: two points, in double precision, at the model's positions. The -114 dB of Fathom's null against the
reference is not the measure here; against its model Fathom stands at this depth as well.

`Tests/UndertowGoldenVectors.h` holds three excerpts of 160 frames from each case, cut where the layer is
strongest and at least 0.3 s apart (in every one the layer is the excerpt, to 0.4 dB). The test's limit is -130 dB;
the worst excerpt is at -142.39 dB. Macro rests in every vector: the model file smooths Macro in front of its
ramps, which `controls.md` corrects, so a moving Macro has no model to be held against (see "Macro in motion").

### The engine against recordings of the reference

`score_undertow_engine.py`, the reference's arithmetic, whole recordings, the
model's own score (`abyss/findings/tempo_scores.json`) beside it.

| Recordings | Count | Macro 0: engine | Above Macro 0: model | Above Macro 0: engine | Largest difference |
| --- | --- | --- | --- | --- | --- |
| E (the model was built on it) | 10 | -106.99 | -60.25 to -67.82 | -60.25 to -67.82 | 0.00 dB |
| D, batch (the model's holdout) | 16 | -110.06 to -115.94 | -56.29 to -77.79 | -56.29 to -77.79 | 0.00 dB |
| F, part A: 99 probes of 20 s, 40 to 2980 s | 99 |  | -66.86 to -67.82 | -66.86 to -67.82 | 0.00 dB |
| F, part B: D's batch again, another instance | 16 | -108.82 to -115.06 | -56.75 to -77.96 | -56.75 to -77.96 | 0.00 dB |
| F, anchors, maps, sweeps, tones, 3652 to 8248 s | 16 | -96.87 to -117.81 | -58.21 to -66.55 | -58.21 to -66.55 | 0.01 dB |
| T90: 90 BPM, play head 0.25 s ahead, running | 13 | -110.94 | -59.47 to -66.65 | -59.47 to -66.65 | 0.00 dB |
| S90: 90 BPM, transport stopped | 12 | -105.45 | -61.88 to -66.52 | -61.88 to -66.52 | 0.01 dB |
| S90, the scan of 486 s (the drift) | 1 |  | -31.10 | -31.10 | 0.00 dB |

On 183 of the 183 recordings the model was scored on, the engine's null is the model's to 0.01 dB;
the largest difference is 0.01 dB. Over the 170 recordings above
Macro 0 outside the stopped scan the engine stands at -56.29 to -77.96 dB, at Macro 0 at -96.87 to -117.81 dB
(12 recordings). The engine lands where the model lands: what holds the null near -60 dB is the model's
floor (see "Properties and limits"), and no recording separates the two. Every row is in
`docs/UNDERTOW_VALIDATION.json`.

The same batch of session D in the engine's own arithmetic, for the record: Macro 0 as above; above Macro 0
2.21 to -3.10 dB. That is not an error of the
port. It is the distance between two clocks of one grid (see "The clock").

### Macro in motion

Part S4 of session F: sixteen steps of Macro, 20.25 s each, under a running
tone of 1 kHz left and 3 kHz right, rendered in one piece with Macro moved at
the step boundaries. The model file is not at these numbers by construction:
it smooths Macro in front of its ramps, and `controls.md` shows on the same
recording that each gain is smoothed behind its ramp, which is what the engine
does.

| Step | Macro behind it | Whole step, 20.25 s | First 0.25 s |
| --- | --- | --- | --- |
| 0 | 0 % | -119.18 | -120.41 |
| 1 | 7 % | -74.29 | -55.16 |
| 2 | 13 % | -75.11 | -56.15 |
| 3 | 17 % | -78.54 | -61.39 |
| 4 | 25 % | -72.46 | -55.72 |
| 5 | 35 % | -69.70 | -58.74 |
| 6 | 43 % | -67.50 | -57.25 |
| 7 | 48 % | -72.07 | -66.67 |
| 8 | 55 % | -71.15 | -74.99 |
| 9 | 65 % | -70.69 | -63.49 |
| 10 | 85 % | -66.98 | -49.10 |
| 11 | 100 % | -67.44 | -70.52 |
| 12 | 62 % | -64.12 | -46.09 |
| 13 | 30 % | -71.90 | -57.23 |
| 14 | 0 % | -63.82 | -44.87 |
| 15 | 100 % | -63.36 | -40.73 |

Whole steps stand at -63.36 to -78.54 dB, the quarter second behind a step at
-40.73 to -74.99 dB. These are nulls of the whole signal; `controls.md` gives the layer alone (the base taken out of
both sides) at -41 to -67 dB behind a step for its form of the law, which is the engine's; the two kinds of
null are not the same measure. Part of what is left behind a step is the lead of Macro, which the engine has about
half a millisecond too long at 48 kHz (see "Properties and limits"); the rest is the form of the smoother, which the
recording does not resolve. Step 0 is the base alone.

### The holdout

`session_F/h1*`, `h2*`, `h3*`: three programme recordings of 32 s that the campaign locked. Nothing of the
layer, the engine or the scripts was tuned on them, and they were loaded once, after everything above was
final (`score_undertow_engine.py --holdout`).

| Recording | Processed time s | Macro | Decay s | engine |
| --- | --- | --- | --- | --- |
| h1_holdout_macro100_decay2_prog5151 | 7296 | 100 % | 2 | -65.60 |
| h2_holdout_macro60_decay4_prog6262 | 7340 | 60 % | 4 | -71.02 |
| h3_holdout_macro30_decay1_prog7373 | 7384 | 30 % | 1 | -73.66 |

The model was never scored on them, so there is no number to hold the engine's against but the range above.

### Tests

`Tests/DspTests.cpp` has 89 tests: the 77 from before Undertow and twelve new ones, which `--test-undertow` runs
alone. One line of an existing test was changed, as the lead asked: "Fathom sample rates and stability" takes
the value behind the last mode for its mode that does not exist, and that is now `undertow + 1`. Result in
`build-undertow-dsp` (Release, arm64): 89 passed, 0 failed, in 67 s; every `[METRIC]` line of the 77 older tests is
the line of the build before the layer, character for character, and nine renders of the Fathom renderer (three
rates, Macro 0, 70 and 100 %, with and without the outer laws) are the same bytes as before.
`AmanitaOceanStateTests` (the colleague's, with the checks of the seventh choice and of the transport reaching the
DSP): passed, exit code 0, 7.7 s.

Two of the twelve tests and the two rules they hold came with the review of 9 October (tempo in host blocks of
any length; a position that tells nothing). Nothing measured moved with them: the golden vectors stand at the
same -142.39 dB, the whole renders against the model at the same -145.03 dB at worst, and 94 recordings scored
again (sessions D, E, T90, S90 and of F the batch, the anchors and the Macro steps) give every number of the
tables above again, to the last digit.

| Test | What it holds |
| --- | --- |
| Undertow engine golden vectors | 24 excerpts of the model at 44.1 and 48 kHz, 90 and 120 BPM, Macro 30, 60 and 100 %, running and stopped, one with the clock origins of a session: every excerpt under -130 dB (worst -142.39) |
| Undertow at Evolution 0 | the engine with the layer is Fathom's engine to the bit at 44.1, 48 and 96 kHz, running and stopped; again after a visit at Macro 100 % once it has fallen silent; and a settled Undertow in the plug-in is a settled Fathom to the bit, with Width, Mix and pre-delay away from neutral |
| Undertow engine determinism, clocks, reset and Freeze | two instances, `reset()`, `prepare()` at another rate, processed silence against `advanceIdle()`, sound followed by 1, 2 and 3000 idle frames: the same bits each time; silence in gives exact silence out at Macro 100 %; under Freeze input does not reach the network through the layer |
| Undertow independence of the host's block length | blocks of 512, 64, 1 and 333 frames give the same bits, running and stopped, at three rates; a stopped transport gives the same bits in both arithmetics; `FDNReverb::process` in blocks of 512, 1, 64 and 100 gives the same bits |
| Undertow transport start, stop, jump, loop and tempo | eleven kinds of transport (start, stop, jump forward and back, loop, tempo step running and stopped, tempo ramp, tempo lost, a jump at every block, positions and tempi that are no numbers) at 44.1 and 48 kHz: finite, peak under 2, the layer back within the last three seconds, and no click; chunk spacing at 90, 120, 61.7 BPM, without a tempo and at the clamps; the engine on noise under a restless transport at three rates |
| Undertow tempo ramps and steps in host blocks of any length | a ramp from 120 to 180 BPM in three seconds, smooth and in stairs of a tenth of a second, in host blocks of 64, 512, 2400 and 8192 frames at 44.1 and 48 kHz: no reader begins anew, the layer stays within 0.35 dB of its level at a steady tempo, no click, the host's position is kept; a step from 120 to 87 BPM begins each of the three readers anew exactly once, 0.25 to 0.72 s behind it (a quarter second and up to three host blocks) |
| Undertow position that tells nothing | a position that stands while the transport runs, another position at every block, a position at 90 BPM without a tempo, at the same four block lengths and two rates: the layer leaves the host after three jumps (nine reader restarts) and does not click, and over the last three of twelve seconds it has the stopped transport's level within 0.000001 dB and its signal within 3.3e-7 of the peak; a position at 97 and at 103 % of the tempo is followed without a new start, at 95 and at 105 % it is left, at every block length; a position that runs again is followed one second later (3.00 to 3.34 s for a position in order from 2 s) with one new start, and the chunks are on the host's note values again; one jump, and two in a row, begin the readers anew and leave the layer on the host |
| Undertow Macro steps | no click under steps of Macro; the gain at pitch is a one-pole of 10 ms on what the reader already holds (distance 2e-18 of a peak of 0.003); the octave above arrives behind its grain reader's shortest delay; the engine stays in range |
| Undertow long run stays on the grid | chunk boundaries off their note values by 2e-10 sample at the start, 7e-8 after an hour's run, none fourteen hours into the timeline; the reference's arithmetic there is 199 samples off, which shows the test sees the grid |
| Undertow silence, denormals and hostile input | the recirculation never reaches the denormal range and the layer falls silent; denormal input is silence; NaN, infinities and full-scale noise with parameters at their extremes stay inside the clipper's range, and the engine returns to exact silence |
| Undertow engine allocation-free processing | no allocation in `processSample`, `advanceIdle`, `setTransport`, `setParameters` and `reset()` in either arithmetic, nor in `FDNReverb::process` across switches between Fathom and Undertow |
| Undertow routing, crossfades and return through the plug-in | at Ocean's neutral controls the plug-in is the engine's wet through its level stage to the bit (four rates, Evolution 0, 45 and 100 %); Fathom and Undertow crossfade directly (distance 0 from the crossfade of the two chains); Default and Undertow crossfade as Default and Fathom do; left for longer than its fade, Undertow returns as an engine that only kept time, to the bit; stress at four rates with Freeze and input that is no signal |

**The click measure.** Two steady tones (220 and 330 Hz, 0.5 each) go into the layer at Macro 100 %; the measure
is the largest second difference of what the layer adds. A tone of amplitude a and angular frequency w gives a w^2,
a step of height h gives h. Under a transport that simply runs the measure is 0.0050; a transport event may not
exceed 1.5 times that. Every event of the three transport tests stays within 1.2 % of the running value (0.0040 to 0.0050 over all of them). With the fade of a cut
chunk taken out of the source, as a check of the measure, the first scenario reads 0.054 and the test fails.

### Sanitizers

`build-undertow-sanitize` (Debug, arm64, `-fsanitize=address,undefined -fno-omit-frame-pointer`), built from the
final sources (one comment in `UndertowLayer.h` was reworded afterwards, and so were the comment lines of the two
generated headers that name the model's files, when the model moved into the campaign folder):
`AmanitaOceanDSPTests --test-undertow`,
the twelve new tests and "no allocations in process", passed with exit code 0 and no report from either sanitizer,
in 9 min 40 s. The 77 older tests and the state
tests were not run under the sanitizers again.

## What is exact, and how far

| | |
| --- | --- |
| The layer against its model | to the depth of the engine itself, about -147 dB: no part of the layer is approximated |
| Evolution 0 | Fathom at Evolution 0 to the bit, in the engine and through the plug-in |
| The reference at Macro 0 | -97 to -118 dB (the base with the origin of its oscillators) |
| The reference above Macro 0, in the reference's arithmetic | -56 to -78 dB: the model's floor, not the port's |
| The plug-in's own arithmetic against the reference above Macro 0 | no null; another clock of the same grid |
| Macro in motion | -41 to -75 dB in the first quarter second behind a step |

## CPU and memory

One core of an Apple M4 Max, stereo, Release, 20 s of noise per figure (`AmanitaOceanFathomRender --benchmark`),
best of three runs with other work on the machine; percent of one core.

| | 44.1 kHz | 48 kHz | 96 kHz |
| --- | --- | --- | --- |
| Engine with Tide at Macro 100 % (Fathom) | 1.34 | 1.48 | 1.54 |
| Engine with Undertow at Macro 100 % | 1.65 | 1.70 | 1.78 |
| Engine with Undertow at Macro 0 | 1.54 | 1.58 | 1.64 |
| `advanceIdle` with Tide | 0.06 | 0.07 | 0.08 |
| `advanceIdle` with Undertow | 0.09 | 0.10 | 0.10 |

The layer costs about a quarter of a percent of a core at full Macro and a tenth at Macro 0, where the readers at
pitch and an octave down rest and the recirculation runs on. In the plug-in every Character now carries one more
idle engine: 0.1 % of a core while Undertow is not selected.

Memory of the layer, allocated in `prepare()` of an engine with `Layer::undertow` only: 12.8 MB (the input of both
channels over 2^20 internal samples in single precision, enough for two chunks of the longest reader at 20 BPM;
the recirculation over 2^19; four grain streams over 2^13 in double precision). With the memory of the second
engine itself (its lines, converters and pre-delay, about 3 MB at 48 kHz) the plug-in holds about 16 MB more per
instance than before. The whole DSP of the plug-in in Undertow was not timed.

## Properties and limits

- **The octave voices hold the null near -60 dB.** Where the remaining error
  is, on impulse maps: octave down 72 %, at pitch 23 %, octave up 4 %. Timing
  is right; single copies differ in shape where the read position passes
  16 384 and 32 768 samples. The reference holds the read position of its
  reversed readers in single precision, the model and the engine do not, and
  two guesses of the rule made the model worse.
- **The drift of the stopped clock is not understood.** It is a fitted curve
  from one session, used in the reference's arithmetic only. It holds the long
  scan of the stopped session at -31 dB where every other stopped recording is
  at -62 to -67 dB.
- **One tempo besides 120 was measured.** Every "follows the tempo" is a
  statement of two points, 90 and 120 BPM. That a chunk is a note value at any
  tempo is assumed.
- **The Macro law was measured at 120 BPM.** The two sessions at 90 BPM hold
  Macro 0 and 100 % only. Of the three ramps only the knee at 30 % is pinned.
- **One host rate was recorded: 48 kHz, in blocks of 512.** The layer runs at
  44.1 kHz inside at every host rate, so nothing in it depends on the rate
  but the reference's arithmetic, whose converter block is 44 frames at
  44.1 kHz and was never seen there. The golden vectors at 44.1 kHz check the
  port against the model, not the model against the reference.
- **Macro acts when the engine gets to it.** At 48 kHz that is about 54
  internal samples ahead of the input it applies to, where the reference fits
  28 to 44; at 44.1 kHz it is none, because the engine reports no latency.
  Half a millisecond, visible only in a null under automation: with Macro
  handed to the renderer 26 frames later, the mean null of the quarter second
  behind the fifteen recorded steps goes from -57.2 to -61.4 dB (10 frames:
  -59.3, 18: -60.6, 36: -60.7). The engine does not delay Macro: the delay
  would be another one at every host rate, and at 44.1 kHz it would have to
  be Macro from the future.
- **A tempo in motion** is followed; chunks then end a little early or late
  against their windows, with the 5 ms fade where it is early (the layer is
  0.3 dB quieter over a ramp from 120 to 180 BPM in three seconds). A step
  begins the readers anew a quarter of a second behind the host's word of it
  and up to two host blocks more, because only then is it known to be a step;
  until then the new tempo is followed as a ramp would be. Stairs of a tempo
  curve that lie more than a quarter second apart and are larger than 1 % are
  steps, each with its new start.
- **A host that reports a running position without a tempo** is taken at
  120 BPM. If its position runs at another tempo by more than 4 %, the layer
  leaves the position after three jumps and keeps time itself at 120 BPM: it
  sounds as under a stopped transport, but not on the host's grid. The tempo
  could be read from the position; it is not.
- **The numbers of Ocean's transport rules are choices**, reasoned in "The
  clock" and held by tests on synthetic transports. No host was tried, and
  nothing of the reference stands behind them.
- **Level.** The layer adds to the base: +1.4 dB at 30 %, +2 dB at 60 %,
  +3.2 to +3.4 dB at 100 % against Evolution 0 (`controls.md` 6). Undertow
  has no trim for it.
- **The reference's Brightness, Transients and Ducking above 0 %** have no law
  in the campaign and no counterpart here, as in Fathom.
- **Not seen in the reference**: a transport event under signal, a mode chosen
  while stopped, more than 4.6 hours of position, input above 0.5.

## Deviations from the specification

1. **`setLayer` takes effect at the next `prepare()`**, not at `reset()`: the
   layer's memory is allocated there and `reset()` may not allocate.
2. **A tempo change restarts the readers only when it is a step**: a change
   of more than 1 % with no other change within a quarter of a second. A
   tempo that keeps changing is followed. The specification restarts on every
   change; that would hold the reversed voices near silence for as long as a
   tempo ramp or a host synchronised to an outer clock keeps moving the tempo.
   The rule looks at the tempo over time, not at the size of one host block's
   change, so it is the same in host blocks of any length (the review of
   9 October; the first form of the rule was not).
3. **A position that keeps jumping is left** after three jumps in a row, and
   the layer keeps time itself until the position has run for a second. The
   specification has every jump begin the readers anew, which under a host
   whose position tells nothing would be at every block, for ever (the same
   review).
4. **A chunk takes over at its first sample or now, whichever is later.** The
   reference's law puts the first sample of some chunks one sample in front
   of the block that decides on them. The chunk's window is zero or within
   -143 dB of it there, so nothing audible or measurable follows.
5. **The plug-in's amount of the two engines is a ramp of its own**
   (`engineAmount_`), beside the amount of each: the two amounts do not sum
   to one in single precision, and their sum would let 0.02 % of the FDN
   through between Fathom and Undertow.

## Not run and open

- No listening test, no DAW, no plug-in validator. No plug-in bundle was built or rendered here: the plug-in's
  routing is checked in the DSP tests through `FDNReverb`, and in the colleague's state tests through the processor.
- The engine's own arithmetic has no reference to be nulled against above Macro 0 (see "The clock"). It is held by
  the tests: the same code path as the reference's arithmetic apart from where the position comes from, the same
  bits with a stopped transport, the same bits for any block length, and the grid test.
- No build for x86_64, with GCC or MSVC, or with `-ffp-contract=fast` on the command line. The layer includes
  `FathomExactArithmetic.h` and is compiled with `-ffp-contract=off` like the engine.
- Under the sanitizers only the new tests ran, not the 77 older ones and not the state tests.
- Host rates in the tests: 44.1, 48, 88.2 and 96 kHz through the plug-in, 44.1, 48 and 96 kHz in the engine, 44 101
  Hz for allocations. No recording of the reference exists at any rate but 48 kHz.
- The long run is an hour of the layer keeping time and fourteen hours of position, not hours of sound through the
  engine.
- A transport event through a host: start, stop, loop, tempo automation and positions that tell nothing were
  exercised on the layer and the engine with synthetic transports, not in a sequencer.
- Macro in motion was scored on one recording (part S4 of session F), at 120 BPM and 48 kHz.
- The level of Undertow against Fathom and against the other Characters was not measured on this build;
  `controls.md` 6 gives the reference's.
- The holdout was scored once and after everything else; no second look.

## Reproduction

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cmake -S . -B build-undertow-dsp -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=/Users/nespesha/Workspace/Juce \
      -DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=$PWD/build-release/_deps/clap_juce_extensions-src
cmake --build build-undertow-dsp --target AmanitaOceanDSPTests AmanitaOceanStateTests AmanitaOceanFathomRender
./build-undertow-dsp/AmanitaOceanDSPTests                    # everything
./build-undertow-dsp/AmanitaOceanDSPTests --test-undertow    # the Undertow tests alone
./build-undertow-dsp/AmanitaOceanStateTests
./build-undertow-dsp/AmanitaOceanFathomRender --benchmark --layer undertow --macro 1 --tempo 120
cd Analyzer/Campaigns/RevOceanCharacterization
$PY emit_undertow_header.py --check        # the two generated headers against the model
$PY score_undertow_engine.py               # every group; about ten minutes
$PY score_undertow_engine.py --table
```

The model, the findings and the session scripts are in the campaign folder
(`abyss/`), so the two generated headers can be made again from the tracked
tree alone. The recordings are under `Analyzer/Results` (not in Git): only
`score_undertow_engine.py` needs them. Its scores are written to
`Analyzer/Results/RevOceanCharacterization/work/abyss/engine/scores.json`, and
the model's compiled helper and its table of oscillator states to
`work/abyss/model_build/` beside it; every row of the scores is also in
`docs/UNDERTOW_VALIDATION.json`.
