# Abyss: state of the model before the C++ layer is written

8 October 2026. For the programmer who will add the layer to the engine. Black box only: recorded audio of two
attended sessions (D and E, the reference's Audio Unit at 48 kHz, blocks of 512, 120 BPM) and the campaign's code.

**Decision of the owner (8 October 2026):** the replica becomes the seventh Character of Amanita Ocean, built the way
Fathom was, under a name of its own (not "Abyss"), with Evolution in the place of the reference's Macro.

**Executable form:** `model_structural/abyss_model.py` (`render(stimulus, sample_rate, warmup_seconds, decay_seconds,
size_percent, macro, context)`; C loops in `abyss_core.c`). At Macro 0 with origin 0 it is `network_model.render`
bit for bit. Evidence: `model_structural.md` (model), `score.md` (independent score), `../analysis/ABYSS_FIRST_LOOK.md`
(measurements). Marks below: **E** measured exactly, **F** fitted (a decimal that minimised a residual),
**G** guessed (assumed, not tested on its own).

## 1. What Abyss is

Abyss = base(x) + base(P(x)). The base is Fathom's core at Macro 0 (Tide's comb out, voices at rest), at gain 1 at
every Macro. P is a second signal made from the input and added to it in front of the equaliser, per channel:

    host in -> pre-delay -> converter -> u --+-----------------------------(+)-> equaliser -> 44 samples -> lines -> ... -> converter -> wet
                                             |                              ^
                                             +-> reverse 2/3 s ------------>| x gR                         same pitch
                                             +-> reverse 1 s -> comb 1 s -> x gU -> grain reader (up) ---->|   octave up
                                             +-> reverse 4/3 s -----------> x gD -> grain reader (down) -->|   octave down

In the words of `SPEC.md`, section 15: `e[k]` is computed from `u[k] + P[k]` instead of `u[k]`. Nothing else changes.
Everything of P runs at the internal 44.1 kHz, so it is the same at every host rate (**G**: only 48 kHz was recorded).
That the pre-delay sits in front of the tap, as it does in front of the converter in Fathom, is **G** as well: no
recording has moved it in Abyss.

| Element | Value | Mark |
|---|---|---|
| Base untouched, P added (not Tide's cos/sin crossfade) | base gain 1 within 3e-6; -100 to -134 dB before the layer arrives | E |
| Tap: converter output, not equalised. Return: in front of the equaliser | -131 to -136 dB there, -31 dB behind it | E |
| Left and right separate (left voices feed the left input only) | other input's share -132 / -115 / -67 dB (same pitch / down / up) | E |
| Decay and Size do not enter P | Size 60 to 150 %, Decay 0.5 to 8 s | E |

## 2. The three voices

All lengths in internal samples (44.1 kHz). `fl32` is rounding to single precision.

**Reversed reader** (three separate ones). Chunk k of a reader with chunk length T seconds:

    b = 44100 * fl32(T) * k          G = 44100 / 1024 (a tick of 1/1024 s)
    tick = floor(b / G)              o = b - G * tick
    s = 44 * ceil((G * tick - 42) / 44)          X = 2 s + o + floor(o) - 2
    y[m] = w(m - X/2) * lin(u, X - m)      from m = ceil(X/2) until the next chunk takes over

`lin` is two-point interpolation, `w` the chunk's window over the position behind the mirror point X/2. Sample
index m and chunk index k count from the instance's first processed frame (`converters.to_internal` numbering).
The reader needs two chunks of input history.

| | Same pitch | Octave up | Octave down | Mark |
|---|---|---|---|---|
| Chunk T | fl32(2/3) s = 29400.000876 | 1 s = 44100 | fl32(4/3) s = 58800.001752 | E |
| Mirror law above | 320 of 325 copies within 0.03 sample | same formula | same formula | E for same pitch; for the other two the formula is assumed and the null confirms it (burst timing right to 0.0015 sample) |
| Window: raised cosine in / out, zero from `end` | 6036.5 / 2075.5, end 29400.05 | 12545.78 / 5794.48, end 44099.96 | 22270.67 / 9006.18, end 58798.07 | F; the raised-cosine form is G for the two octave voices |
| Recirculation in front of the grain reader | none | `y[m] += 0.198 * y[m - 44100]` | none | delay E, gain F (0.2 excluded) |
| Gain at full Macro | 0.6927952 (+- 1e-7) | 0.487907 | 0.707228 | E / F / F |

**Grain reader** (octave voices only; behind the reversed reader, the comb and the Macro gain). A single-precision
phasor: `use p; p += inc; if (p >= 1) p -= 1`. Two taps at phases p and p + 0.5 (wrapped). A tap at phase q has
window sin(pi q) and reads the stream `dmin + W q` samples back (down) or `dmin + W (1 - q)` (up), two-point
interpolation. One reader serves both inputs for octave down; octave up has a different reader per input.

| | inc (bits) | grain period | W | dmin | read speed | Mark |
|---|---|---|---|---|---|---|
| Octave down | 0x38C5C3E0 | 5302.13 | 5313.13373 | 45.58356 | 0.49896 | F |
| Octave up, left | 0x39458B10 | 2654.04 | 5268.90989 | 44.52724 | 1.99262 | F |
| Octave up, right | 0x3946A910 | 2639.12 | 5346.74391 | 44.53402 | 2.01298 | F |

Neighbouring bit patterns of inc within about 8 units are not separated; a change of 0.005 sample in W is visible.
The speeds are not exact octaves and not round cents (tested: round cents cost 8 to 9 dB on two voices).

## 3. Clocks and what starts them

| Clock | Counts from | Mark |
|---|---|---|
| Three chunk clocks (and the 44-sample grid inside the mirror law) | the instance's first processed frame; a change of mode does not restart them | E for same pitch (0.001 sample, two instances switched 10 564 s apart); by null for the other two |
| Three grain phasors | the change of mode: p = 0.5 at internal sample `origin - 2` of the stream u | F on session E, confirmed on D (one sample off: -8 dB) |
| The base's 32 line oscillators | the change of mode: start phases when the lines are at index `origin`, which is sample `origin - 44` of u (the lines see u 44 samples late). `origin` is a multiple of 44; D: 31 574 180, E: 497 448 424 | E |
| Converters | the first processed frame | E |

So one number per instance, the oscillator origin, and nothing random: no seed, unlike Tide.
"First processed frame" and "host play head" were the same thing in both sessions (open question 1).

**Late chunks (session D only).** Some boundaries of the same-pitch and octave-down readers sit exactly one
single-precision step of the processed time (in seconds) later: `o` grows by 2^-14 s between 512 and 1024 s, by
2^-12 s between 2048 and 4096 s. Seen so far: a boundary in the middle of a host block is late when it is
2/3 s modulo 2 s (512 to 1024 s) or 4/3 s modulo 2 s (2048 to 4096 s). None between 1024 and 2048 s, none in session
E (11 290 to 14 050 s); 4096 to 8192 s was never recorded. The step is **E** (1.000 +- 0.001); the rule is a
two-entry table read from one block layout (**F**), the arithmetic behind it is unknown. It says the chunk clock
reads something the host gives per block, in single precision. Inaudible (a copy moves by 0.06 or 0.24 ms), but
without it a null test collapses (section 5).

## 4. Macro (Evolution), and rest

    gR = 0.6927952 * clamp(m / 0.300, 0, 1)
    gU = 0.487907  * clamp((m - 0.1461) / (0.4596 - 0.1461), 0, 1)
    gD = 0.707228  * clamp((m - 0.6041) / (1 - 0.6041), 0, 1)

- Three straight ramps with hard ends: the voices come in one after the other (**F** on eleven positions and three
  impulse cases; only 0.300 is firmly pinned). Below its foot a voice is off (-94 to -101 dB), not quiet.
- The gains act behind the reversed readers and the comb, in front of the grain readers (**G** for the last part).
  Buffers, comb and phasors run at every Macro, also at 0: sound stored at Macro 0 plays at the new gain at once (**E**).
- In motion: the Macro value acts 44 samples early through a one-pole of 10 ms (**F** on steps of 10 % upward that
  all fell on a chunk edge; **G** for any other movement).
- At rest: silence in gives exact silence out; no self-noise (**E**). The 1 s comb never empties by itself, it
  loses 14.07 dB per second whatever Decay is (**E**, followed to -590 dB).
- Level against the base at Macro 100 %: +3.4 dB on impulses, +3.3 / +2.8 dB (left / right) on a 1 kHz tone; white
  noise +3.0 to +3.8 dB up to 4 kHz and +1.2 to +1.5 dB above 10 kHz.

## 5. How close the model is

Null = `20 log10(|model - recording| / |recording|)`, whole recordings, no gain or delay fitted.

| | Session E (the model was built on it) | Session D (holdout, another instance) |
|---|---|---|
| Macro 0 (base with the origin) | -107.0 to -122.4 dB | -110.1 and -115.9 dB |
| Macro 10 to 90 % steps on a tone | -68.9 to -90.8 dB | not recorded |
| Macro 25 to 100 %, impulses, tones, noise, programme, sweep (E: 18 recordings; D: 14) | -59.4 to -68.0 dB | -56.3 to -77.8 dB, median -65.1 dB, with the origin and the late-chunk table |
| Same, with the origin alone | (E has no late chunk) | 11 of 14 at -0.7 to -19.4 dB, 3 at -65.0 to -77.8 dB |
| Base alone on the same recordings (what the layer is worth) | -2.2 to -6.4 dB | -2.5 to -6.0 dB |

Where the remaining error is (impulse maps): octave down 72 %, same pitch 23 %, octave up 4 %. Timing is right;
single copies differ in shape where the read position passes 16 384 and 32 768 samples: the reference holds the
read position of its reversed readers in single precision, the model does not (two guesses of the rule made it worse).

**Determinism.** Probably yes. Nothing random was found in any voice. The model built on one instance predicts
another at the same depth from one integer plus the late-chunk table, and that table was read from the other
instance's own probes, not predicted. Voice gains fitted on the holdout are 0.9986 to 1.0009. Not shown:
that two instances agree where chunks are late (D and E never overlap in processed time in Abyss), and anything below
-60 dB. A replica that sounds the same is possible now; a replica that nulls like Fathom's base (-110 dB) is not.

## 6. Open questions, most important first

"Ear" = a musician can hear the difference; "null" = it decides whether an engineer's null test passes.

1. **Seconds or note values; processed frames or play head** (ear and null). 2/3, 1 and 4/3 s are a third, a half
   and two thirds of a bar at 120 BPM, and every recording was made at 120 BPM with the play head at "frames
   processed". If the chunks follow the tempo, a replica in seconds is wrong at every other tempo. The 1/1024 s tick
   could be 1/512 of a quarter note in the same way. Needs the tempo session (`../probe7_tempo.py`).
2. **The late-chunk arithmetic** (null). Known as a two-entry table for one block layout and two time ranges.
   Outside them (the first 730 s an instance processes, 4096 to 8192 s, other block sizes) a null test can fail on one
   chunk in six and nobody could say whether the replica or the table is wrong.
3. **The read position in single precision** (null). Holds both sessions near -60 dB. Needs noise bursts instead of
   impulses (part S1 of `../probe4.py`).
4. **Pre-delay and Width in Abyss** (ear if wrong). The structure predicts them (pre-delay in front of the tap, Width
   on the wet behind the network); no recording has moved an outer control in Abyss.
5. **Macro in motion** (ear: clicks; null under automation). Only steps on a chunk edge were measured. Fathom glides
   its Macro over 200 ms of its own; the same choice is open here.
6. **How a voice starts.** The reference cannot be recorded in Abyss from its first frame (a mode chosen before the
   first block does not take effect), and the switch itself was never recorded under signal. For the engine:
   restart the 32 oscillators and the three phasors when the Character is selected (what the reference does), keep
   the chunk clocks on the frame count, and give the tests a hook for the origin, as `setVoicePhaseForTesting` does
   for Tide.
7. **Forms that are assumed**: raised-cosine fades of the octave voices and their place in front of the grain readers.
8. **Not recorded**: other host rates and block sizes, input above 0.5 (the level stage and the clipper are
   Fathom's), input above 12 kHz into the octave-up voice, more than 16 384 s of processed time, Foam.

## 7. For the port

- **Base**: Fathom's network and converters at Macro 0, unchanged. The layer is new code on `u[k]`, per channel.
- **Memory per channel**: two chunks of input plus about 130 samples for each reversed reader (58 800, 88 200 and
  117 600 samples; a mirror point lies up to 65 samples in front of its boundary), 44 100 for the comb, about 5 400
  for the longest grain delay.
- **Single precision where the model has it**: the chunk lengths `fl32(2/3)` and `fl32(4/3)`, and the three phasors
  with their increments as bit patterns. Everything else as the Python model computes it (double).
- **Clocks**: chunk clocks on the engine's frame count from `prepare()` or `reset()`, kept running by
  `advanceIdle()`; oscillators and phasors from the origin (open question 6). The engine needs a test hook that sets
  the origin, or no recording can be nulled.
- **Late chunks stay out of the product.** They belong in the test harness, which applies the table when it nulls
  against recordings made in session D's time ranges.
- **Order of work**: the port against the Python model first (Fathom reached -143 to -151 dB that way), then
  against recordings: Macro 0, then 10 % (same pitch alone), then 50 %, then 100 %. The model's own nulls of section 5
  are the ceiling.
- **Do not hard-code seconds** before the tempo session is read: keep the three chunk lengths and the 1/1024 s tick
  as parameters of the layer.

## 8. Sessions still asked for

| Session | Command (from `work/abyss/`) | Settles | Owner's time |
|---|---|---|---|
| T90 | `probe7_tempo.py T90 90 0.25` | question 1; first reading by `analysis2/tempo_quicklook.py T90` | Demo, Start, switch; about 2 minutes |
| F | `probe4.py F`, unchanged | questions 2 (30 s to 8300 s in one instance, against D frame for frame), 3, 4, 5, level and a holdout | Demo, Start, switch in the 20 s pause; then 5 to 12 minutes that need nobody |

A dry run that needs nobody comes first for each (`probe7_tempo.py dry90 90 0.25 --unattended`,
`probe4.py dry --unattended --probes 2`): neither script has run against the real host yet, and the host's tempo
and offset have never been tried with the plug-in.

Only if T90 moves something: a second tempo (`probe7_tempo.py T140 140 0.25`) to confirm the scaling. If the clock
follows the play head, an offset puts it into any time range in two minutes (`probe7_tempo.py P4100 120 4100.25`),
the cheap way to the late-chunk arithmetic; `--block 64` and `--block 1024` vary the host block for the same
question. Not asked for: a bit-for-bit comparison of two instances. The holdout answered determinism as far as the
model reaches, and F against D already puts two instances on the same frames where chunks are late.
