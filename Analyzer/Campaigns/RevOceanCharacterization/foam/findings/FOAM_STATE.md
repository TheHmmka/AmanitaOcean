# Foam: state of the model before the C++ layer is written

10 October 2026. For the programmer who will add the layer to the engine. Black box only: recorded audio of two
attended sessions with two instances of the reference's Audio Unit at 48 kHz, blocks of 512 (A: 120 BPM, play head =
frames processed; T90: 90 BPM, play head 0.25 s ahead), and the campaign's code. Nothing was disassembled.

**Executable form:** `foam_model.py` (`render(stimulus, first_frame, decay, size, macro)`), the diffuser alone in
`foam_structure.py` with the constants of `structure_final.json`. At Macro 0 it is the base model with the session's
oscillator origin. Evidence: `FOAM_FIRST_LOOK.md` (where Foam sits, the measured kernel), the scripts `b00` to `b10`
(structure, session A), `FOAM_T90.md` and `t01` to `t09` (the blind test on the second instance, the outer controls,
Macro in motion, the change of mode). Marks: **E** measured exactly, **F** fitted (a number that minimised a
residual), **G** guessed. "Two instances" = holds in session A and, with nothing re-fitted, in T90.

## 1. What Foam is

Foam = base(x'), x' = gDry x + gWet F(x), targets gDry = cos(pi/2 Macro), gWet = sin(pi/2 Macro). The base is
Fathom's core at Macro 0 (Tide's comb out, voices at rest). F is a fixed diffuser of 28 all-passes, one per channel,
no modulation, no clock, no noise.

    host in -> pre-delay -> HPF, LPF -> converter -> u -> Transients --+------ x gDry --(+)-> equaliser -> 44 samples -> lines -> ... -> converter -> wet
                                                                       |                 ^
                                                                       +-> F (28 all-passes) -> x gWet

In the words of `SPEC.md`, section 15: `e[k]` is computed from `gDry u[k] + gWet F(u)[k]` instead of `u[k]`. Nothing
else changes. Everything of F runs at the internal 44.1 kHz (**G** for other host rates: only 48 kHz was recorded).

| Element | Evidence | Mark |
|---|---|---|
| F acts on the network input, nothing behind or inside the network changes | one free input signal explains both outputs at -107.8 to -118.2 dB; a filter behind the network would leave -6 to -7 dB | E |
| Tap and return in front of the equaliser | Macro steps: -120 to -122 dB with the crossfade in front, -87 to -107 dB behind | E |
| Left and right separate, the same F on each | a left impulse needs a signal on the left input only; one structure nulls L, R and both | E, two instances |
| Decay and Size do not enter F | Decay 0.5 to 20 s, Size 30 to 200 %: -100 to -107 dB | E, two instances |
| Tempo, play head and host block grid do not enter F | T90 (90 BPM, play head ahead, block grids shifted): -97 to -118 dB with session A's constants | E |
| Linear, silence in gives exact silence out | noise 0.05 rms to impulses of 0.5; x01 peak 8e-37 | E |
| Pre-delay and input filter in front of the converter, so in front of F | pre-delay 50 / 200 ms -103 dB, one frame off 0 dB; HPF 1 kHz / LPF 2 kHz -102 / -108 dB | E |
| The Transients stage in front of F | section 4 | E |

## 2. The diffuser F

Seven stages of four parallel all-passes; between stages the four channels are mixed by the Sylvester Hadamard
matrix of order 4 times 1/2. Per channel of audio:

    AP(D, g):   y[n] = g x[n] + x[n - D] - g y[n - D]            (Schroeder all-pass, g > 0)

    v[0..3] = 0.5 * u                                             (the input to all four channels)
    for s = 0 .. 6:
        v[c] = AP(D[s][c], g[s]) applied to v[c],  c = 0 .. 3
        if s < 6:  v = 0.5 * H4 * v          H4 = [[+ + + +], [+ - + -], [+ + - -], [+ - - +]]
    F(u) = v[0] + v[1] + v[2] + v[3]

| Stage s | D[s][0..3] in channel order (44.1 kHz samples) | g[s] |
|---|---|---|
| 0 | 1323, 1457, 1604, 1765 | 0.373 |
| 1 | 2591, 2353, 2137, 1943 | 0.466 |
| 2 | 2851, 3137, 3457, 3803 | 0.572 |
| 3 | 5581, 5071, 4607, 4181 | 0.474 |
| 4 | 6143, 6761, 7439, 8189 | 0.409 |
| 5 | 12013, 10919, 9923, 9013 | 0.363 |
| 6 | 13229, 14561, 16033, 17653 | 0.272 |

Channel 0 is the one that receives the sum row of the mix in front of it. The 28 delays ascend along the series;
even stages take theirs in ascending channel order, odd stages in descending order. The same thing said otherwise:
every stage in ascending order, the mix alternately "1/2 H4 with its rows reversed" (the form the base uses for its
order-16 matrix) and its transpose.

| Constant | Status | Mark |
|---|---|---|
| 28 delays | each found by scanning whole samples against the measured kernel (`b02_scan.py`); in the finished structure every one of the 28 gives -84.8 dB at its value and -3.0 to -5.3 dB one sample off (first 1.36 s of the kernel) | E, two instances |
| Rule behind the delays | target 1323 x (400/30)^(k/27), k = 0..27 (30 ms to 400 ms, geometric), rounded to the nearest sample, then moved to the nearest integer (+1, -1, +2, -2, ...) that is coprime with every earlier delay: reproduces all 28 (`b03_delays.py`) | G (the rule), E (that it reproduces them) |
| 7 gains | least squares on the kernel: 0.3729999, 0.4660001, 0.5720001, 0.4740000, 0.4089997, 0.3629999, 0.2720000, each +- 1.2e-7; fitted one by one, the four of a stage agree to 1e-6 (`b04_gains.py`) | E (three-decimal constants; a law along the stages was not found) |
| Mix: H4 x 1/2 after stages 0..5, signs and order as above | every sign enters the kernel fit; stage 1 in ascending channel order instead: +2.0 dB against -84.8 dB | E |
| Input 0.5 to each channel, output the plain sum | direct copy 2 x product of the gains = 0.0038063, measured 0.0038062 | E |
| Number of stages: 7 | the kernel is explained to its end at the floor of the comparison (-84 dB to 3.1 s) | E |
| Arithmetic precision of the all-passes | not visible: double precision nulls at the base model's own depth | open |

What it does to a sound: unit energy, magnitude between 0 and 2 (not an all-pass: four all-pass chains are summed),
no tilt. An impulse becomes a wash that rises for 1 s (energy centroid 1.023 s, 1.6 % before 0.5 s, 49 % before
1.0 s) and falls 28.4 dB per second, whatever Decay is (ring-out with Decay at its minimum: -28.7 to -29.0 dB/s).
The direct copy is 0.0038 (-48 dB). On a steady tone dry and wet add with the phase of F at that frequency: at
1 kHz the level at Macro 50 % is +2.99 dB.

## 3. Macro (Evolution), motion, rest

- **Law**: gDry = cos(pi/2 m), gWet = sin(pi/2 m), m = Macro 0..1. Exact at Macro 0, 10, ..., 100 % (-119 to
  -122 dB in steady state) and at 25 / 50 / 75 % on impulses. **E**, two instances.
- **gWet acts behind F, gDry on u.** F always receives u at gain 1 and runs at every Macro, also at 0: sound played
  at Macro 0 is in the wet path the moment gWet opens. **E**
- **Motion: the two gains are smoothed, not Macro.** Once per block of 44 internal samples each gain moves by
  `gain += (target - gain) * (1 - exp(-44/441))` (a one-pole of 10 ms run at the block rate; factor 0.905043) and is
  held for the 44 samples: a staircase. Smoothing Macro and then taking cos / sin gives -63 dB; 439 or 443 instead
  of 441 give -101 dB against -121 dB. Checked on 99 changes: ten steps of +10 % (A: first 0.2 s behind each -119.7
  to -122.4 dB) and, in the second instance, steps of +30, +40, +30, -45, -45, +90, -100, +50 % (first 0.25 s behind
  each -115.6 to -117.7 dB) and a staircase of 81 moves of 2.5 % every 50 ms up and down, where each move arrives
  before the last has settled (-110 to -120 dB per play). Up and down alike; the state carries on. **E**, two
  instances.
- **Block grid**: a new value starts at internal index m = 43 (mod 44) in the numbering of `converters.to_internal`
  (network-input samples counted from the instance's first frame). One sample off: -61 to -105 dB. **E**, two
  instances (both origins are multiples of 44, so the grid may count from the first frame or from the change of
  mode).
- **Which block is the first to use a new value** is the reference's handling of host blocks, not part of the
  effect: the first block that starts later than `b - L`, b the internal time of the host frame h at which the host
  call with the new value begins; L in (111.56, 115.56] internal samples for h = 0 (mod 48) (A), (82.76, 90.76] for
  h = 16 (mod 48) (T90). Both fit `L = L0 + (147/160) ((h - c) mod 48)`: the reference converts in chunks of 48 host
  frames and a block takes the value of the host call in which its chunk is completed (`foam_model.first_block`,
  c = 16, L0 = 84.5; c = 1..16 are possible). **F**. The engine applies a new target at its next block of 44.
- **Rest**: silence in, exact silence out; no self-noise. F empties by itself at 28.4 dB/s. **E**

## 4. The outer controls in Foam (session T90; details and candidates in `FOAM_T90.md`)

| Control | In Foam | Null | As in Abyss |
|---|---|---|---|
| Pre-delay | whole host frames `max(0, floor(P fs / 1000) - 1)` in front of the converter; F is fed from the delayed input | -103.3, -103.1 dB (50, 200 ms) | yes |
| HPF, LPF | first-order bilinear filters at the host rate in front of the converter and of F | -102.2, -107.9 dB | yes |
| Width | Tide's mid / side law on the wet behind the network | -103.4, -103.2 dB (0, 150 %) | yes |
| Mix | dry 1, wet 1 at 50 %; dry delayed by the 48 frames of latency | -112.8 dB | yes |
| Ducking | Tide's level stage on the wet at output time, keyed by the raw input; threshold -13.45 dBFS at 50 % (F, one number) | -101.7 dB | yes |
| Brightness | in the network; F is not touched (band levels follow the base's own response within 0.1 to 1.2 dB) | no law, no null | consistent; front or behind F cannot be seen for a fixed F |
| Transients | **in front of F**, on the network input: at full, an input x confined to 150 ms behind the event explains the 3.9 s wash at -98.8 dB (-95.6 dB on the part the fit has not seen) | no law; solved input | **no**: Abyss's layer returns in front of the stage, F sits behind it |
| Size 30 / 200 %, Decay 20 s | F untouched | -107.0 / -100.0, -100.0 dB | yes |

Order at the network input, from both modes: `u -> (+ P, Abyss) -> Transients -> (F and its crossfade) -> equaliser`.

Transients at full, as the solved inputs show it (**E** for what was solved, the law is not known): a gain that hears
the event (1 for a single sample of 0.5, 0.437 = -7.2 dB for a 5 ms burst at 0.1 rms) followed by a fixed diffusing
kernel of its own (nothing for 7 ms, most energy 10 to 30 ms behind the event, energy -0.29 dB, magnitude 0 to 2 like
F). It rebuilds every onset before F sees it, so with Transients up F's wash starts later and without its direct copy.

## 5. Clocks, and the change of mode

- **No clock in F.** One structure nulls two instances from 220 s to 5050 s of processed time, at 120 and 90 BPM,
  with the play head at and ahead of the frame count, with four host block grids. **E**
- **The change of mode** (on record in T90, 1.235 s into a flush, in silence): the network is emptied (Tide's tail,
  on record at 1e-21, is cut to exact zero within 172 frames; without a change of mode it rings on to 1e-34), the
  32 line oscillators restart there (origin 23 964 028 in A, 9 315 548 in T90, both multiples of 44), and F starts
  empty: had its lines run in Tide and been kept, the flush would hold 1.5e-13 of probe 6; it holds 0.0. **E**
- Not seen: a change of mode under signal, the state of the two gains at the change, a change from Abyss.

## 6. How close the model is

Null = `revocean.null_db(model, recording)`, whole recordings, both channels, nothing fitted per recording.
"Kernel" is the earlier model with the measured 3.77 s kernel (`kernel="measured"`).

Session A (the structure was decoded on it):

| Recording | Structure | Kernel |
|---|---|---|
| c02 train, Decay 0.5 s, Macro 100 | -104.7 dB | -77.8 |
| c03 / c11 impulses L, R, both, Decay 2 s | -103.0 / -102.5 | -77.9 / -77.8 |
| c09 / c08 / c10 Macro 25 / 50 / 75 % | -107.9 / -103.8 / -103.5 | -86.2 / -80.8 / -78.6 |
| c12 / c13 Size 60 / 150 %, c14 Decay 8 s | -104.5 / -102.1, -101.3 | -78.0 / -77.9, -78.0 |
| c04 programme, c05 noise | -116.7, -102.8 | -78.7, -77.9 |
| c06 1 kHz, c07 220 Hz, c15 noise burst | -122.4, -130.1, -99.8 | -88.1, -85.5 |
| x02 impulse map L (600 s), x03 impulse map R (300 s) | -104.1, -104.4 | -77.8 (segments) |
| x04 1 kHz 120 s, x06 sweep | -121.2, -110.7 | -88.9, -78.4 |
| x05 eleven gapless Macro steps, whole run | -121.4 | -93.8 |
| probes 20, 30, ..., 90, 99 (610 to 2980 s) | -114.7 to -117.4 | -82.2 to -82.3 |
| Macro 0 (base alone): c01, c16, x05 step 0 | -104.1, -115.4, -121.0 | |

Session T90 (blind: only the oscillator origin re-fitted):

| Recording | Structure |
|---|---|
| p007, p008, end probe | -117.5, -117.4, -117.8 dB |
| f02 train, f03 time scan (24 impulses L / R) | -104.0, -103.4 |
| f04 tone 64 s in four steps with shifted block grids, and its tail | -116.9 |
| k00, k16 neutral; k12 / k13 Size 30 / 200 %; k14 Decay 20 s; k15 Macro 50 % | -103.5, -97.0; -107.0 / -100.0; -100.0; -100.9 |
| k01 / k02 pre-delay, k03 / k04 filters, k05 / k06 Width, k07 Mix, k10 Ducking (control modelled) | -103.3 / -103.1, -102.2 / -107.9, -103.4 / -103.2, -112.8, -101.7 |
| n01 white noise 10 s | -103.2 |
| m01 Macro in motion, 90 plays and the tail | -115.6 |
| Macro 0 (base alone): f01, n02 | -103.2, -103.1 |

Every Macro > 0 recording of both sessions that the campaign has a law for is at the depth the base reaches at
Macro 0. Not modelled: Brightness and Transients (no law in any mode), the second recording of c15 (Decay changed
during the ring-out).

**Determinism.** Yes: nothing random, nothing moving, two instances, one set of constants. Not shown: another host
rate, another block size.

## 7. Open points, most important first

1. **Transients as a law** (ear): the stage's place is known, its gain law and its kernel are not. The solved inputs
   (`t05_k11_*.npz`) hold the kernel at full; it should fall to the method that decoded F. Positions between 0 and
   full were not recorded.
2. **Brightness as a law** (ear): in the network, F untouched; no law in any mode.
3. **A change of mode under signal**, and the two gains at the change. For the product: clear everything when the
   Character is selected (what the reference does, as far as it is on record).
4. **Host rates other than 48 kHz, block sizes other than 512, input above 0.5** (the level stage and the clipper
   are Fathom's): not recorded. The chunk reading of "which block first" (section 3) rests on two alignments.
5. **Precision.** Whether the reference runs F in single precision cannot be seen at -100 dB.
6. **No law for the seven gains**; the coprime rule for the delays is a reading of 28 numbers, not a measurement.
7. Predelay Synced, Return, Master, Freeze: not moved in Foam.

Closed since the first version: tempo and play head (none), pre-delay (in front), Macro in motion beyond +10 %
steps (the same staircase), the change of mode (everything emptied).

## 8. For the port

- **Base**: Fathom's network and converters at Macro 0, unchanged. F is new code on `u[k]`, per channel, in front
  of the equaliser, at the internal rate.
- **Order**: pre-delay and input filters at the host rate, converter, the Character's transient stage, then F and
  its crossfade, then the equaliser. A transient stage behind F, or on the wet, is not what the reference does in
  Foam. Width, Mix and the level stage stay on the wet as in Fathom. Tone controls go into the network, none into F.
- **Memory per channel**: 28 delay lines, 179 737 samples in all (4.08 s; the longest 17 653). Two channels:
  359 474 samples. Cost per internal sample and channel: 28 all-passes (two multiplies each in the form above, or
  one in the one-multiplier form) and six 4 x 4 Hadamard mixes (additions and a factor 1/2).
- **Constants**: the table of section 2 as integers and the seven gains as written. Do not derive the delays at run
  time from the rule; it is a guess that happens to fit.
- **Gains**: targets cos / sin of pi/2 x Evolution; state per gain; one update per block of 44 internal samples
  with the factor `1 - exp(-44/441)`, value held over the block, up and down alike. Fathom glides its Macro over
  200 ms of its own; if the product keeps that glide in front, the staircase still has to follow it or a null test
  under automation fails (it is inaudible: a step is at most 9.5 % of the remaining difference per millisecond).
- **Always running**: feed F with u at every Evolution value while the Character is selected, so that opening
  Evolution brings the wash of what was played before.
- **Selecting the Character**: empty the 28 lines and the network and restart the oscillators (and in `reset()`).
- **Clocks**: none in F; no tempo, no play head. The tests need the hook for the oscillator origin, as for Abyss,
  and for a null under automation the rule of section 3 for the first block after a change.
- **Order of work**: F alone against `foam_structure.kernel` (impulse, should be exact to rounding), then the
  engine against the Python model, then against recordings: Macro 0, c02 / x02 (impulses, Macro 100), c08 to c10,
  x05 and m01 (motion), part K of T90 (the shell). The nulls of section 6 are the ceiling.

## 9. Files

| File | What |
|---|---|
| `foam_structure.py`, `structure_final.json` | the diffuser and its constants |
| `foam_model.py` | the model (structure by default, `kernel="measured"` for the old path, `macro_gains` and `first_block` for motion) |
| `b00_target.py`, `b01_peel.py`, `b02_scan.py`, `s_*.json` | decoding: target, what a partial structure leaves unexplained, scans of single delays |
| `b03_delays.py`, `b04_gains.py` | the delay rule; the gains by least squares |
| `b05_validate.py`, `b09_final.py` (`validate_structure.log`, `final2.log`) | session A's table |
| `b06_macro.py`, `b07_trajectory.py`, `b08_stair.py`, `b10_order.py` | Macro in motion in session A; equaliser order, ring-out slope, memory |
| `FOAM_T90.md`, `gcommon.py`, `t01` to `t09` | the second instance: blind score, outer controls, Transients, motion, the change of mode |
| `FOAM_FIRST_LOOK.md`, `a00` to `a17` | the first look (where Foam sits, the measured kernel) |

    PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python      (run inside this folder)
    $PY b01_peel.py structure_final.json 166000 0.02 0      structure against the measured kernel
    $PY b05_validate.py; $PY b09_final.py                   session A
    $PY t02_score.py T90; $PY t03_controls.py; $PY t07_motion.py      session T90
