# Rev OCEAN characterisation (Tide)

Black-box measurement campaign for a structural model of Arturia Rev OCEAN
1.0.0.5848 in its Tide mode. The model becomes a sixth Character of Amanita
Ocean, driven by the existing Ocean controls.
Its working name, Ebb, became Fathom on 7 October 2026; older findings keep it.

This file is the campaign's source of truth. Packet findings live in
`findings/`, measured numbers in `tide_structural_data/`.

## Target agreed with the owner (6 October 2026)

- A new Character with its own name; the reference is named in documentation only.
- Controls: the existing Ocean knobs. Decay, Size, Pre-delay, Width and Mix follow
  the measured laws of the reference; **Evolution is the reference's Macro, 0 to
  100 %**. Low Cut, High Damping, Focus, Harmony and Mono Safe stay Ocean's own.
  (Since 7 October the plug-in's Mix is Ocean's own as well; see wave 3.)
- Reference state for every other control: neutral (Brightness 0 %, input filter
  open at 20 Hz and 20 kHz, Transient 0 dB, Ducking 0 %, Return 0 dB).
- The main listening point is **Macro 100 %, Mix 100 %**. Macro 0 is the
  foundation the rest is built on.

## Boundary

- Only black-box measurement: the plug-in is rendered through its public host
  interface in its normal demo mode. Its code is never disassembled or
  inspected, its files are not read, and its licensing is not worked around.
- Model constants come from measurements. The ranges and display laws of the
  fifteen host parameters are the ones the plug-in declares to the host.

## Tools

Use the Python of the sibling checkout (NumPy and SciPy only):

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
cd Analyzer/Campaigns/RevOceanCharacterization
$PY -m unittest test_revocean
```

- `revocean.py` renders a stimulus through the reference with the Amanita
  Analyzer of AmanitaSaturator (`capture`), pins the identity of both binaries
  and caches every capture by content under `Analyzer/Results/` (not in Git).
- `datasets.py` holds the shared working set and the locked holdout.
- `score_first_order.py` scores a first-pass model on that holdout.
- `reference_render.py` is the complete model as one function, the executable
  form of `SPEC.md`; `emit_engine_constants.py` writes the constants and golden
  vectors for the C++ port into `tide_structural_data/`.
- `emit_engine_header.py` writes the two C++ headers of the port from those
  files (`Source/dsp/FathomEngineConstants.h`, `Tests/FathomGoldenVectors.h`);
  `--check` tells whether they are up to date.
- `score_engine.py` scores the C++ engine (through its renderer,
  `Tools/FathomRender.cpp`) and the built plug-in against the reference and
  against the model; `render_candidate.py` renders the built VST3 through the
  Analyzer with the sixth Character selected, as a candidate for the scorers.

## Capture rules

- Every capture sets all fifteen host parameters; unset ones read back as 0 and
  cannot be trusted. `revocean.BASELINE` is the neutral state, 100 % wet.
- Warm-up is 10 s of silence. With 0.5 s two renders differ at about -100 dB.
- At Macro 0 a capture is rendered twice and kept only when both renders are
  bit-identical. Above Macro 0 the reference is not repeatable; captures are
  realisations (`realisation=0, 1, ...`).
- The stimulus carries its own tail; a case is at most 110 s.
- Decay at or above 0.999 normalised is Freeze and is outside this campaign.
- About 1.2 renders per second in total, however many run in parallel. Put
  many impulses in one stimulus. At Decay 0.5 s impulses 0.5 s apart do not
  disturb each other (earlier tails are 60 dB down per 0.5 s).

## Reconnaissance (lead, 6 October 2026)

Measured at 48 kHz, block 512, unless stated. "Raw" sample indices count from
the impulse and include the 48 samples of reported latency. Items marked
PRELIMINARY are single observations that a packet must confirm.

1. A fresh headless instance starts in the factory preset "Cleaner Tides",
   whose Macro Mode is Tide (re-asserting that preset's knob values leaves the
   output bit-identical). The mode is not a host parameter, so every capture is
   Tide. The program-change parameters have no effect.
2. Macro 0, all parameters set, warm-up 10 s:
   - bit-identical between fresh instances, also with 14 Analyzer processes at once;
   - linear: scaling by 0.1 and adding L-only to R-only responses both null at
     about -130 dB; the inverted impulse gives the bit-inverted output;
   - independent of the block size (64, 128, 256, 512, 960 are bit-identical);
   - repeatable at 44.1, 48, 88.2 and 96 kHz; reported latency is 1 ms at each
     (44, 48, 88, 96 samples).
3. The response is strongly time-varying inside an instance: moving the impulse
   by 1 sample changes the aligned response at -48 dB, by 48 samples at -15 dB,
   by 480 samples it is uncorrelated. The same holds for the earliest window.
4. Early response, Size 100 %, Macro 0. Nothing arrives before raw sample
   ~1260. Each arrival is preceded by about 36 samples of alternating-sign
   pre-ringing. Strong first arrivals for an impulse at 1.000 s:
   L output 1298, 1437, 1454, 1639, 1737, 1920, 2171, 2370;
   R output 1358, 1407, 1484, 1654, 1717, 1980, 2124, 2427.
5. The R->L response has its arrivals where L->L has them. Around the first
   arrival the ratio R->L / L->L is 0.5723 sample for sample; arrivals such as
   2171 and 2370 are far weaker in R->L. Over longer windows R->L is not a
   multiple of L->L. The same holds for L->R against R->R.
6. Delay modulation. The positions of three isolated L->L arrivals follow
   `p(t) = D + A sin(2 pi f (t + p)/fs + phi)` over 98 s (t = impulse time, so
   the delay is evaluated when the arrival is read):
   D = 1606.42, 1752.27, 1926.41 raw samples; A = 42.256 samples (0.8803 ms) for
   all three; f = 0.59988 Hz for all three; phi = 4.650, 2.019, 5.668 rad
   (steps of 0.581 cycle). The residual, 0.08 samples rms, is the limit of the
   peak picking used. PRELIMINARY: t counts from the end of the 10 s warm-up;
   where the oscillator's time origin lies is not established.
7. Arrival shape. The sum of an arrival's samples (its DC gain) is constant
   within about 3 %, but its high-frequency content changes from impulse to
   impulse: at 14 kHz between +0.9 and -5.8 dB relative to the low-frequency
   gain, at 17 kHz between +1.2 and -9.5 dB. This does not follow linear
   interpolation at the fractional part of the arrival's position
   (correlation 0.0). The cause is open.
8. Sample rate. Spacings between arrivals are the same in milliseconds at all
   four rates. PRELIMINARY: the whole pattern sits 2.235 ms earlier at 44.1 kHz,
   1.09 ms earlier at 88.2 kHz and 0.104 ms earlier at 96 kHz than at 48 kHz.
9. Macro above 0 is not repeatable between instances (null about -2 dB), and
   its output is not a slowly varying filter of the Macro 0 output (short-time
   coherence 0.3 to 0.5 at Macro 50 %, below 0.2 at 100 %). Band statistics of
   two instances agree within 0.4 dB.
10. PRELIMINARY, Macro 100 %, Decay 0.5 s: the first arrivals keep their
    pattern but come about 526 samples (11 ms) later, are 4 to 5 times lower,
    and each is followed 2 to 3 samples later by a negative lobe. Band energy of
    a response falls and wanders: 6.4 to 12.8 kHz averages -24 dB (Macro 0:
    -15 dB) with 5 dB standard deviation from impulse to impulse; below 1.6 kHz
    the deviation is 2 to 2.5 dB (Macro 0: under 1 dB).

## Results of wave 1 (6 October 2026, each packet checked by an independent skeptic)

Details, evidence and limits are in `findings/`; constants in
`tide_structural_data/`. Where a verification corrected a packet, the
verification is right. Items above that these results supersede: item 2
(linearity has a limit, see the shell below), item 4 (the window also holds
the earliest second passes), items 6 to 8 and 10 (explained below). Lines
marked (wave 2) were corrected after wave 2 superseded them.

**Network at Macro 0** (`findings/first_order.md`, `first_order_verification.md`,
model `first_order_model.py`): -80 dB on clean fresh responses at 48 and
44.1 kHz, any warm-up, with an independent re-implementation agreeing at
-307 dB. A free refit of 132 numbers gains 0.34 dB.

- The core runs at 44.1 kHz at every host rate, between two converters: a
  Kaiser-windowed sinc (0.9 of Nyquist, 17 crossings, beta 6) read from a
  table of 4096 entries per sample without interpolation, on exact lattice delays.
- Two independent groups, one per output channel, of 16 delay lines each.
  Lengths are primes in 44.1 kHz samples (left 1031 ... 7589, right 1039 ... 7589).
- Each line is read with linear interpolation at a single-precision length
  `P + 38.808 sin(theta)` (0.88 ms). `theta` is a single-precision accumulator
  per line, 0.6 Hz nominal, starting at `11.25 k` rad (k = L1, R1, L2, ...),
  counted from the first processed sample, warm-up included.
- First-pass gains: one coefficient per line index, a cross-feed law at the
  line inputs (`w` = 0.272 on lines 1 to 6, falling from 1 to 0.272 over lines
  7 to 16), and a fixed filter: an equaliser in front of the lines and a 20 kHz
  low-pass with Q 1 behind them (wave 2).
- Feedback: a Hadamard matrix of exactly 1/4 and a loop kernel of two shelving
  sections; the whole matrix, the output weights of all 16 lines and the later
  passes are measured (wave 2).
- The residual at -80 dB was the equaliser taken behind the moving read
  instead of in front of it (wave 2).

**Size, Decay, sample rate** (`findings/laws.md`, `laws_verification.md`):
exact at -117 to -131 dB.

- Size: line length `floorf(fl(P s) + 0.5f)` whole internal samples in single
  precision, `s` = Size/100 as the plug-in's single-precision factor (wave 2).
  Depth, rate and phase do not change. The fixed delay around the lines does
  not scale (88 samples at a 44.1 kHz host).
- Decay: gain `10^(-3 P s / (44100 T))` with the unrounded length, applied
  before the output tap, and T the displayed Decay. Each output tap also has a
  weight that moves with Decay up to exactly 6 s and is constant above: one law
  for all 16 lines (wave 2). The weight belongs to the line a pass leaves
  through.
- Everything is fixed in seconds or hertz; rate-dependent shifts are the converters.

**Shell** (`findings/io.md`, `io_verification.md`): the complete outer chain
predicts a capture with every outer control moved at -132 dB.

- Pre-delay: `floor(P fs/1000) - 1` whole samples (P in single precision),
  before the network, no interpolation; the dry path is not delayed.
- Width: mid/side on the wet output, `s = -log2(1 - Width/200)`, mid
  `sqrt(2/(1+s))`, side `s sqrt(2/(1+s))`. Return: gain on the wet. Mix:
  dry `min(1, 2(1-m))`, wet `min(1, 2m)`, dry delayed by the reported latency.
  Master: gain on both.
- **Ducking at 0 % is not off**: the wet is turned down when an input sample
  exceeds 0.5629 (-4.99 dBFS): soft-knee, slope 5/7, knee 9.983 dB, 5 ms
  attack, 300 ms release, keyed by the raw input. The reference is linear only
  below that level; campaign stimuli stay at or below 0.5.
- An output clipper comes last: unity to +8 dBFS, quadratic knee, ceiling +12 dBFS.

**Tide layer, Macro above 0** (`findings/tide.md`, `tide_verification.md`):
three things around an unchanged network (the network of the Macro 0 model
explains the first arrivals at Macro 100 % at -47 to -52 dB once it is driven
at the exit of the comb).

- A comb behind the network's input equaliser, one per input: a delay that
  follows a 200 s triangle from 4.43 to 15.13 ms, counted from the start of
  processing (the right input a quarter period ahead), inverted feedback 0.4,
  a 30 Hz high-pass and a 20 kHz low-pass in the delayed path, inside the loop
  (wave 2). It is the same in every instance. Macro crossfades the undelayed
  and the comb path with `cos/sin(90 deg x Macro)`.
- Two voices per output after the network, each a gain times a two-pole
  filter driven by one phase: gain `sqrt(2) sin^2(pi phi)`, cut-off falling
  from 20 kHz to a Macro-dependent lower end (464 Hz at 100 %) and jumping
  back while the gain is zero; voice B half a cycle after voice A. Lines 1 to 8
  feed voice A, lines 9 to 16 voice B (wave 2).
- The phase runs at 0.0564238 cycles/s at Macro 100 %. It is the only thing
  that differs between instances; its generator is described below (wave 2).
- At Macro 100 % the wet level cycles between about -36.8 and -31.3 dB with a
  16 to 19 s period under steady noise (Macro 0: -32.4 dB flat), and the wet
  does not pass DC.

**Targets** (`findings/stats.md`, `stats_verification.md`): `descriptors.py`
computes decay times per band, levels, echo density, coherence and modulation
on ensembles; `tide_structural_data/targets.json` holds the reference values
for 37 cases and `descriptors.compare_to_targets` scores a candidate in units
of the reference's own spread. The decay law carries over to Macro 100 %; the
level law does not.

## Results of wave 2 (7 October 2026, each packet checked by an independent skeptic)

The model is complete. `SPEC.md` is the implementation specification in
processing order, `reference_render.py` its executable form with every
verified correction, `tide_structural_data/engine_constants.json` and
`engine_golden.npz` the constants and golden vectors of the port. Where a
verification corrected a packet, the verification is right. Besides the lines
marked above, wave 2 supersedes reconnaissance item 2: the reported latency is
not 1 ms (see the converters).

**Network at Macro 0** (`findings/network.md`, `network_verification.md`;
`network_model.py`, `score_network.py`): locked holdout worst -114.07, mean
-116.79 dB over whole responses; 46 fresh captures -108 to -121 dB; about
-100 dB on steady noise and on recirculated sound alone.

- One structure, one fitted number (input gain of line 1, 0.24397766): an
  equaliser of two peaking sections, 2 x 16 lines, attenuation, output taps on
  two S-curves, a Sylvester Hadamard matrix (rows reversed, times 1/4), a loop
  kernel of two shelving sections, a 20 kHz low-pass. Every other constant is
  round; free fits return them within 2.3e-5.
- The null needs the single-precision details: accumulators, a sine reduced
  with the single-precision pi/2, depth 38.808002, three roundings of a length.
- Correction: Size is a factor `fma(1.7f, r, 0.3f)` formed with a reciprocal,
  and a length is `floorf(fl(P s) + 0.5f)`. The first rule put a line one
  sample off at one knob position in 200 (80.1, 162.1, 180.1 %: -10 to -34 dB
  instead of -113 dB). It is in `network_model.py`; the holdout score did not
  move.
- OPEN: a lag of 2e-7 samples per pass in the feedback path (-83 dB after
  100 s at Decay 20 s); the last place of the oscillators' sine.

**Converters** (`findings/converters.md`, `converters_verification.md`;
`converters.py`): exact between captures at -126 to -132 dB at 66 host rates
from 44.101 to 384 kHz (at 383999 Hz once the size of the clock error is used).

- Latency `4 floor(11 rate / 44100 + 0.5)` samples. One table for both
  converters, lattice delays in closed form, the output held one block for a
  whole rate ratio and two otherwise.
- Table entries that are hit exactly follow the rounding sign of each
  converter's clock. Correction: the signs are measured at 48, 56, 88.2, 96,
  176.4 and 192 kHz and tabulated; the simulation behind `clock_signs` is right
  for 121 of 122 clocks and wrong at 56 kHz. (64 and 384 kHz joined the table
  in wave 3.)
- Not measurable: rates below 44.1 kHz, the first 1.5 s of an instance.

**Tide stage** (`findings/tide_stage.md`, `tide_stage_verification.md`;
`tide_stage.py`): comb and voice in closed form; the stage alone -96 dB
(median), the voice alone -110 dB.

- Comb: all-pass read, feedback -0.4, both filters inside the loop, delay
  `431.2075 + 236.0360 tri` samples at 44.1 kHz with its origin at 85.7
  samples, held in single precision.
- Voice: the network's 20 kHz low-pass, moved. A state-variable low-pass and a
  gain, set once per 44 internal samples; cut-off
  `20000 - 19990 E(0.88 m) E(phi)`, Q piecewise linear in the cut-off and 9.5
  blocks late. Voice B is measured: the same law half a cycle away.
- OPEN: the arithmetic of the comb delay. A few percent of its tap changes
  fall one sample off, which keeps steady material at -80 to -93 dB at Macro
  100 %.

**Phase generator** (`findings/tide_phase.md`, `tide_phase_verification.md`;
`tide_phase.py`): per output a ramp plus value noise on a jittered grid, from
the first processed sample: one knot per cell of 7.353 s (left) and 6.25 s
(right), raised cosines to uniform targets in [0.184, 0.591] and
[-0.007, 0.433], the start value held until the first knot. Random per
instance: the start values (tied between the outputs, by no constant law),
every jitter, every target. Correction (`tide_model`): the rate is
`0.0431885 + 0.0070490 e^(0.63 m)` cycles/s, 0.0564238 at Macro 100 %.

**Assembled model** (`findings/tide_model.md`, `tide_model_verification.md`;
`tide_model.py`, `score_tide.py`; with the shell `reference_render.py`):

- Measured on whole responses: lines 1 to 8 feed voice A and lines 9 to 16
  voice B, the cross-fed part with its line; the equaliser is in front of the
  comb; loop and tap weights do not change with Macro.
- Exact at Macro 0. Exact up to the phase above it: with the phase of each
  output fitted, -88.8 to -106.8 dB at Macro 100 % (fifteen cases), and -98.9
  to -108.0 dB at Macro 100, 50 and 25 % on eleven fresh captures at four host
  rates. Statistical in the phase: with its own generator the model is another
  draw of the reference on 89 listening descriptors.
- With the shell: all outer controls moved at once -144.8 to -150.5 dB from a
  captured wet (level stage and clipper working: -135.9 to -141.6 dB), -112.7
  to -127.7 dB for the whole chain.
- Not in the model: the first 2.2 s of an instance, controls that move,
  Freeze, anything off the neutral baseline.

## Results of wave 3 and of the finishing wave (7 October 2026)

The model became the sixth Character of Amanita Ocean. `docs/FATHOM.md` is its
development record and `docs/FATHOM_VALIDATION.json` the same figures for a
machine; the figures below are of the last full run of `score_engine.py`.
Tools added since wave 2: `reference_render.py`, `emit_engine_constants.py`,
`emit_engine_header.py`, `score_engine.py`, `render_candidate.py`.

**Specification and reference render** (`SPEC.md`, `reference_render.py`;
evidence in `Analyzer/Results/.../work/specification`): the whole chain as one
function, exact at Macro 0 and exact up to the voice phase above it; constants
and golden vectors for the port from `emit_engine_constants.py`.

**Engine** (`Source/dsp/Fathom*`, renderer `Tools/FathomRender.cpp`,
`score_engine.py`):

- Locked holdout worst -114.06, mean -116.78 dB (the model: -114.07 and
  -116.79); 27 fresh Macro 0 captures -111.98 to -120.80 dB; outer laws quiet
  -113.40 to -128.40 and loud -112.73 to -126.53 dB.
- Against the model -143.15 to -149.11 dB on 18 whole responses at host rates
  from 22.05 to 384 kHz, and -145.10 to -149.60 dB above Macro 0 with the same
  phase in both.
- Phase of a capture given to the engine (worst / mean): Macro 100 % -88.78 /
  -98.50 dB (8 cases), 50 % -91.62 / -100.29 (4), 25 % -96.09 / -100.79 (3);
  eleven fresh captures -98.94 to -107.95 dB.
- Running free from a seed the engine is another draw of the reference: 20
  impulse descriptors 0.14 to 0.40 in the reference's own spreads (reference
  against itself 0.28 to 0.48); level cycle under noise floor -36.60, top
  -31.63 dB, period 17.5 s (reference -36.57, -31.67 dB, 16.9 s).

**Plug-in** (the built VST3 through the Analyzer, `render_candidate.py`):

- Macro 0, Mix 100 %: locked holdout worst -114.06, mean -116.77 dB; ten
  captures of its own -112.57 to -119.37 dB; 36 renders equal the engine's
  renderer to the bit.
- Macro 100 %, eight new instances per target case: impulse descriptors 0.18
  to 0.45 in the reference's spreads, level fluctuation 1.51 (reference
  against itself 1.51), modulation rates 1.00 (1.47), time variance 0.18
  (0.43). These move with the seeds the instances draw: nine draws read 0.14
  to 0.46, 1.20 to 1.61, 0.87 to 1.08 and 0.12 to 0.33.
- The five other Characters render the bytes of the last commit.

**Reviews** (`findings/engine_review.md`, `integration_review.md`,
`listening_review.md`, `behaviour_review.md`, `editor_review.md`): each with
stimuli, captures or probes of its own. What they found is fixed, documented
as a limit or listed for the owner in `docs/FATHOM.md`.

**Decisions of the owner** (7 October 2026):

- The Character is named Fathom; its accent is deep blue `#2F7FE0`.
- Mix is Ocean's linear law in the plug-in (dry `1 - m`, wet `m`), so a switch
  of Character leaves the dry level where it is. The reference's law stays in
  the engine for the renderer and the scoring; plug-in and reference meet at
  Mix 0 and 100 % only.
- Every instance of the plug-in draws a voice seed of its own and does not
  store it, as every instance of the reference has a phase of its own.
- The reference's level stage (the wet turned down by input above -5 dBFS at
  Ducking 0 %) and its output clipper are kept, the clipper behind the Mix.
- The editor: a Character drop-down, the description of the selected
  Character to its left under it, the Evolution knob to the right.

**Clock signs at 64 and 384 kHz** (finishing wave): both pairs joined the
measured table (`converters.MEASURED_CLOCK_SIGNS`, `engine_constants.json`,
the generated header). On the engine review's captures (programme seed 60228,
Decay 2 s, Size 100 %) all nine pairs were tried with `reference_render.py`:
at 64 kHz (low, low) nulls at -117.47 dB and the next best pair at -90.63 dB;
at 384 kHz (high, low) at -118.78 dB and the next best at -98.15 dB. They are
the pairs the simulation gives, so no render changed (78 renders of the
engine at 13 host rates, the same bytes before and after).

## Session captures (8 October 2026)

The Macro Mode (Abyss, Foam, Tide) is not a host parameter and the demo keeps no state, so a mode other
than Tide can only be measured inside the instance whose editor a person has used.
`Analyzer/Tools/RevOceanSession` hosts one instance with its editor on screen and, after Start, runs
capture jobs in it; `revocean_session.py` drives it (`first_case`, `flush`, `case`). A recording carries
the number of frames the instance had processed before it; divided by the rate that is the warm-up of
`network_model.render` and `reference_render.render_knobs`.

Proven in Tide at Macro 0, unattended, with the demo's Authorization box left open in the editor
(`test_revocean_session.py`; scripts and numbers in `Analyzer/Results/RevOceanCharacterization/work/session`):

- **The first case of an instance is the Analyzer's capture, bit for bit.** Five cases (impulses and a
  noise programme, two settings, 48 kHz; one at 44.1 kHz), 156560 to 204560 frames each, 10 s warm-up;
  also with the tail processed as a step of its own.
- **Two sessions with the same jobs are bit-identical:** 12 recordings, 2304000 frames.
- **Later cases against the model**, each behind `flush` (8 s at Decay 0.5 s): -93.1 to -113.7 dB on 11
  cases (Decay 0.5 to 20 s, Size 30 to 200 %, pre-roll 0 to 3.7 s, 9 to 89 s into the instance, 48 and
  44.1 kHz; five impulses in 4 s, two cases with noise). That is what the model reaches on this
  material, not the session: on the 8 cases the Analyzer can render too (warm-up up to 60 s) the model
  scores the fresh capture within 0.01 dB of the session's.
- **Later cases against fresh instances** (`revocean.capture` at the counted warm-up), 23 cases behind
  a flush of 6 or 8 s. 19 differ from the fresh capture by at most 1.4e-36 and only before the first
  arrival, where a fresh instance gives exact zeros: any signal leaves that floor in an instance for
  good (2.9e-37 rms, still there after 50 minutes of silence). The other 4 differ at -125.8 to
  -135.0 dB. All four follow a change of Size (4 of 16 such changes; the 7 cases without one are all
  among the 19); neither a longer flush nor the model's null moves with it. A fit with the model's
  sensitivities puts it on the attenuation of 1 to 5 of the 32 lines, one last place off (I); the rest
  is the rounding of two loops that are no longer in step.
- **Flush length.** At Decay 0.5 s the tail falls 123 dB/s and reaches the floor after 5.9 s. A previous
  case that drove the level stage (Ducking 50 %) is still seen at -78, -107, -136 and -165 dB behind a
  flush of 1, 2, 3 and 4 s with 0.5 s of pre-roll (the stage's 300 ms release, 29 dB/s) and not at all
  5.0 s after its end. A change of Size arrives after 1.25 to 1.5 s (-22 dB before, -57 dB at 1.25 s).
  Decay acts at once: a pre-roll of one frame is as good as 2 s. A previous case at Macro 100 % leaves
  nothing behind the flush either. The shortest flush that left no trace is 6 s; `FLUSH_SECONDS` is 8.
  Against the model less is enough: 0.25, 0.5, 0.75, 1 and 2 s behind loud noise at Decay 20 s give -1,
  -24, -55, -86 and -101 dB.
- **A fresh instance needs its first seconds**: a probe at its very first frame scores -3.9 dB against
  the model, and `revocean.capture` rejects a warm-up of 2 s (two renders differ).
- **Demo limit, measured.** Processed audio does not count: 50.6 minutes of audio went through one
  instance in 65 s, and the same probe scored -110.1, -99.3 and -104.8 dB against the model at the
  start, after 25.4 and after 50.6 minutes of audio (fresh instances on that probe: -102.3 to
  -110.1 dB). Wall-clock time counts, from the creation of the instance: one that processed nothing for
  its first 2 minutes and then a 2 s probe every 30 s, every 5 s near the end, answered 50 probes at
  -91.0 to -120.3 dB up to 19:55.5; the 48 probes from 20:00.4 to 23:55 had the peak and rms of the
  stimulus itself. Three recordings at 20:57 were the input bit for bit, without the plug-in's latency,
  at Mix 100 and 50 % and Master 0 and -6 dB. The editor then reads "The time limit of the demo mode
  has been reached"; parameters still read back, the host is not blocked, no window opens. The tool
  ends a session 19 minutes after the creation of its instance, and the driver refuses a recording that
  equals its stimulus.

Not measured: anything with a person at the window (pressing Demo, another mode), and Macro above 0
inside a session beyond one flush behind a case at Macro 100 %.

### Attended sessions host the Audio Unit (8 October 2026)

Three attended sessions with the VST3 (A, B, C under `work/abyss/`) recorded Tide although the owner had
chosen Abyss and the editor showed it: Macro 0 was bit for bit the fresh Tide capture, Macro above 0 had
Tide's statistics, and switching the mode while 151 probes ran changed no descriptor. In JUCE's VST3 host
a mode chosen in the editor does not reach the sound. The session host therefore also loads Audio Units
(a bundle that ends in `.component`), and attended sessions use
`Session.open(plugin_format="AudioUnit")`:

- `/Library/Audio/Plug-Ins/Components/Rev OCEAN.component`, binary SHA-256
  `c4cd49d9b8b36a055471cb612c88725067f67f4872380e511dc3fd0c9a04e3a6`, described by JUCE as
  "Rev OCEAN", "Arturia", version "1.0.0", format "AudioUnit"; 48 samples of latency at 48 kHz.
- Its sound controls carry the VST3's numbers 1 to 14; it has no On/Off parameter (ID 0), and the
  driver leaves that one out.
- Unattended, in Tide at Macro 0 (`work/abyss/au_check.py`): the first case equals the Analyzer's VST3
  capture bit for bit over its 672,000 frames, and a second case behind a flush nulls against
  `network_model.render` at -115.2 dB. The Audio Unit is the instance the campaign has measured.
- Session D (`work/abyss/probe3.py D switch`): the owner pressed Start in Tide and switched to Abyss
  while probes ran; from probe 24 on a 1 kHz tone comes back with strong 500 Hz and weaker 2 kHz. The
  mode reaches the sound. What Abyss is, is the subject of `work/abyss/analysis/`.

- Session E (`work/abyss/probe3.py E before extras`, then `work/abyss/probe5.py E switch <folder>`): the
  owner chose Abyss BEFORE pressing Start. The label showed Abyss, the sound stayed Tide for 375 probes
  (11,250 s of audio). When he then switched the mode away and back while probes ran, Abyss sounded from
  probe 376. A mode chosen before the first processed block does not take effect; a switch during
  processing does. The order for the owner is therefore always: Demo, Start, then the mode. `probe5.py`
  runs identical probes until it hears Abyss (the 400..600 Hz share of the left channel rises from -85 dB
  to -5 dB) and starts its common sequence behind a fixed probe index (400 in session E, processed time
  12,000 s): the batch of session D, the ring-out, and long continuous recordings (silence, impulse maps
  on either side, a 120 s tone, a sweep, eleven steps of Macro).

- 9 October 2026, three more attended sessions with the Audio Unit (the owner: Demo, Start, then the mode):
  - **T90** (`work/abyss/probe7_tempo.py T90 90 0.25`): the host reported 90 BPM and a play head 0.25 s
    ahead of the frame count. The same-pitch voice's chunk became 0.8889 s (it is 0.6667 s at 120 BPM)
    and its lattice sits at phase 639 ms = -0.25 s modulo 0.8889 s in 105 of 120 scanned impulses. The
    chunks of Abyss are note values (1/3, 1/2 and 2/3 of a 4/4 bar) on the host's play head, not seconds
    on the frame count. The base ignores tempo and play head (Tide, Macro 0, same host settings: -104 dB
    against `network_model.render` on the frame count).
  - **S90** (`... S90 90 0.25 --stopped`): the play head said "not playing" and stayed at 0.25 s. The
    chunk is still 0.8889 s and the lattice is as clean (105 of 120), at phase 233 ms: with a stopped
    transport the layer runs on at the host's tempo from an anchor of its own, not yet identified.
  - **F** (`work/abyss/probe4.py F`, 120 BPM): the whole structural programme, 194 recordings, Abyss
    from probe 1 (the switch in the idle pause behind the first probe took effect) to the end: session
    D's jobs again, noise-burst and impulse maps, stepped tones, every outer control moved once (part K),
    Macro, Size and Decay in motion (S4), sweeps, three unseen programmes (part H, a holdout).
  The session host takes `bpm`, `transportOffsetSeconds` and `transportPlaying` in session.json
  (`Session.open(bpm=, transport_offset_seconds=, transport_playing=)`); the defaults are what every
  earlier session reported: 120 BPM, offset 0, playing.

Also since that day: a silence step is limited to one hour of audio (F1 of
`findings/session_tool_verification.md`), and the driver decides "demo time is over" by the clock of the
step, with equality to the stimulus as a second witness that is not consulted at Mix 0 % (F2).

## Abyss (Undertow) (8 and 9 October 2026)

The reference's second Macro Mode, Abyss, became the seventh Character of Amanita Ocean, Undertow:
the base network of Tide with another layer in front of it. Everything of it is in `abyss/`:

- `abyss/model/`: the executable model, `abyss_model.render(stimulus, sample_rate, warmup_seconds,
  decay_seconds, size_percent, macro, context)` with `hostclock.py` (the chunk clock as it reads the
  host), `base.py` (this campaign's network with the oscillators counted from an origin) and
  `abyss_core.c`. It imports `converters.py` and `network_model.py` from this folder. `base.py` compiles
  `abyss_core.c` and caches a table of oscillator states on first use, both into
  `Analyzer/Results/RevOceanCharacterization/work/abyss/model_build/`, which Git ignores
  (`/Analyzer/Results/`), as `network_model.py` does with its own helper.
- `abyss/findings/`: `ABYSS_STATE.md` (the layer on one page), `tempo.md` (tempo, play head, stopped
  transport), `controls.md` (the outer controls, Macro in motion); behind them `ABYSS_FIRST_LOOK.md`,
  `model_structural.md`, `score.md` and `verification.md`; and `tempo_scores.json`, the model's own
  nulls against the recordings (the table of `tempo.md`, section 7).
- `abyss/sessions/`: the scripts that made the attended recordings, as they ran, with a `README.md`;
  `abyss/sessions.json`: tempo, reported position and clock origins of sessions D, E, F, T90 and S90.
- Not here: the recordings, their stimuli and each session's `info.json`. They stay under
  `Analyzer/Results/RevOceanCharacterization/work/abyss/session_<label>/` (not in Git), where the
  files copied into `abyss/` have their originals. The copies are byte for byte, except `base.py`,
  which differs in where it finds this folder and where it puts what it compiles.

Result. Abyss = base(x) + base(P(x)): the layer P makes three voices from the converter's output and
adds them in front of the network, a reversed reader at pitch and two more behind grain readers an
octave up and an octave down, in chunks of 4/3, 2 and 8/3 quarter notes of the host's position. The
model meets the recordings at -56 to -78 dB above Macro 0 (170 recordings of five sessions, the long
scan of the stopped session apart, which stands at -31 dB) and at -97 to -118 dB at Macro 0; what
holds it near -60 dB is the reference's single-precision read position in the octave voices. The C++ engine (`Source/dsp/UndertowLayer.*`, a layer of the Fathom engine) meets the
model at -146.5 to -148.1 dB on whole renders and has the model's null on all 183 recordings the model
was scored on, to 0.01 dB. `docs/UNDERTOW.md` has the Character, what of it is Ocean's own, the tests
and the limits; `docs/UNDERTOW_VALIDATION.json` every number.

```sh
PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python
$PY emit_undertow_header.py --check     # Source/dsp/UndertowConstants.h and Tests/UndertowGoldenVectors.h against the model
$PY emit_undertow_header.py --engine    # the engine's renderer against the model on whole renders (needs AmanitaOceanFathomRender)
$PY score_undertow_engine.py --table    # the engine against the recordings, beside the model; without --table it scores (needs the recordings)
```

## Shared datasets

- `datasets.grid_responses(input_channel)`: 980 unit-impulse responses per
  input channel on a 100 ms grid of impulse times, Decay 0.5 s, Macro 0.
- `datasets.holdout_first_order(...)`: locked holdout, scoring only.
  `score_first_order.py <module>` is the one sanctioned reader.
- `datasets.holdout_network_reference(...)`: locked holdout of whole responses
  at seven settings. `score_network.py <module>` is the one sanctioned reader.
- `score_tide.py <module>` scores a model above Macro 0 on captures of its own
  (phase-fitted null, free-running statistics, level cycle); no holdout.

## Conventions

- A prediction is scored as `20 log10(|candidate - reference| / |reference|)`
  without fitting gain, delay or polarity.
- A constant is first fitted freely; when it lands near a round value it is
  replaced by that value and the model is scored again.
- Holdout folders and loaders are scored only. Nothing is fitted or chosen on them.
- A result that does not hold is kept and reported as such.
