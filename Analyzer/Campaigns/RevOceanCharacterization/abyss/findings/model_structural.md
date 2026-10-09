# Abyss: the structural model

8 October 2026. Black box only: recorded audio of sessions D and E, the campaign's own code and notes. No session
was opened, no capture made, nothing outside `analysis2/` written. Session D's batch (c01..c16, c15) was never
loaded; section 7 says exactly what was taken from session D.

The module is `model_structural/abyss_model.py`:

    render(stimulus, sample_rate, warmup_seconds, decay_seconds, size_percent, macro, context=None) -> output

It nulls against every Macro > 0 recording of session E at **-59.4 to -90.8 dB** (whole recordings,
`revocean.null_db`, nothing fitted per recording) and is the base model at Macro 0 (-107 to -122 dB).

Two things a reader must know before using it:

1. **Session D has "late chunks" that session E does not have.** In D, between 512 and 1024 s and between 2048 and
   4096 s of processed time, one chunk boundary in six is placed one single-precision step of the time later than
   the law of session E says. Without them the model scores -3 to -13 dB on D's probes in those ranges; with them
   -67 dB on all sixteen. D's batch lies at 3010..3620 s, inside the second range. The rule that predicts them was
   read from D's probes, not from session E, so it is **off by default** and switched on by the context
   (`abyss_model.SESSION_D`; `SESSION_D_STRICT` is the anchor alone). Section 4.
2. **This is a merge.** The interrupted modeler's module scored -50 to -68 dB. The second attempt
   (`model_kernel/`) had the same structure with better numbers (exact single-precision phasor increments, a Macro
   lead of one block). Its constants are now in the module here; the result reproduces its logged nulls to
   0.01 dB. The earlier module is kept as `abyss_model_interrupted.py`; the scripts t00..t19 were written against
   it and do not run against the new one.

## 1. Structure as implemented

Everything Abyss adds runs at the internal rate of 44.1 kHz on the network's input stream x (the input converter's
output), per channel, and is added to that stream in front of the equaliser. m is the internal sample index from
the instance's first sample, in the numbering of `converters.to_internal`.

    host in -> converter -> x --+--------------------------------------------(+)-> equaliser -> 44 samples -> base network -> low-pass -> converter -> out
                                |                                             ^
                                +-> reversed reader 2/3 s ---------------------| x gR(Macro)                      unison
                                +-> reversed reader 1 s -> comb 1 s -> x gU(Macro) -> grain reader (up) --------|   octave up
                                +-> reversed reader 4/3 s ----------> x gD(Macro) -> grain reader (down) -------|   octave down

- **Base path**: gain 1 at every Macro. The 32 line oscillators count from `oscillator_origin`; the converters
  count from the instance's first frame.
- **Reversed reader** with chunk length T: chunk k has

      b = 44100 * fl32(T) * k;  tick = floor(b / G);  o = b - G * tick;  G = 44100 / 1024
      s = 44 * ceil((G * tick - 42) / 44);  X = 2 s + o + floor(o) - 2

  and delivers `y[m] = w(m - X/2) * lin(x, X - m)` from m = ceil(X/2) until the next chunk takes over (lin: linear
  interpolation; w: raised-cosine fade in, raised-cosine fade out). These clocks count from the instance's first
  sample and do not restart at a mode switch.
- **Comb** of the octave-up voice, in front of its grain reader: `y[m] += 0.198 * y[m - 44100]`.
- **Grain reader**: a single-precision phasor p (`use p; p += inc; if (p >= 1) p -= 1`) that is 0.5 at internal
  sample `oscillator_origin - 2`. Two taps with phases p and p + 0.5. A tap with phase q has window `sin(pi q)` and
  reads the stream `dmin + W q` samples back (down) or `dmin + W (1 - q)` (up), linear interpolation. Octave down:
  one reader for both inputs. Octave up: a different reader for each input.
- **Macro**: three clamped straight ramps on the voice gains. The Macro value is taken 44 samples early and smoothed
  by a one-pole of 10 ms. The gains of the octave voices act on the streams in front of the grain readers.
- Buffers, comb and phasors run at every Macro; Macro only scales.

## 2. Parameters and their status

"Exact" means measured to the sample or to the bit; "fitted" means a decimal that minimised a residual on session
E; "guessed" means not separately tested.

| Parameter | Value | Status |
|---|---|---|
| Chunk lengths | fl32(2/3) s, 1 s, fl32(4/3) s | exact. Unison: 44100 x fl32(2/3) (clock verification). The other two are confirmed by the null over 2700 s: an error of 1e-9 relative would move a boundary by 0.6 sample |
| Mirror law (tick 1/1024 s, block 44, a = 42) | section 1 | exact (clock verification: 320 of 325 copies within 0.03 sample); any a in [41.645, 42.355) is the same law |
| Fades, unison | in 6036.5, out 2075.5, end 29400.05 samples | fitted (raised cosines; gain error 0.0003 rms) |
| Fades, octave up | in 12545.78, out 5794.48, end 44099.96 | fitted; the raised-cosine form is guessed |
| Fades, octave down | in 22270.67, out 9006.18, end 58798.07 | fitted; the raised-cosine form is guessed |
| Comb delay | 44100 samples | exact (second-pass clicks 44100.00 +- 0.02) |
| Comb gain | 0.198 | fitted (ring-out 0.1980 in both sessions; 0.2 is excluded) |
| Phasor increments (single precision) | down 0x38C5C3E0, up left 0x39458B10, up right 0x3946A910 | fitted; periods 10604.2553, 5308.0842, 5278.2336 samples. Neighbouring bit patterns within about 8 units are not separated |
| Delay sweep W | 5313.13373, 5268.90989, 5346.74391 samples | fitted |
| Shortest delay dmin | 45.58356, 44.52724, 44.53402 samples | fitted |
| Phasor state at the switch | 0.5 at `oscillator_origin - 2` | fitted on E, confirmed on D's probes (section 4) |
| Grain window | sin(pi q) | fitted form; a free scale and offset of the window's phase gain 0.3 dB (section 5) |
| Voice gains at full Macro | unison 0.6927952, up 0.487907, down 0.707228 | unison measured (16 clean copies, +- 1e-7); the others fitted |
| Macro ramps (foot, knee) | unison 0 / 0.300, up 0.1461 / 0.4596, down 0.6041 / 1 | fitted on eleven Macro positions; only 0.300 is firmly pinned |
| Macro smoothing, lead | one pole 10 ms, 44 samples | fitted on steps of 0.1 upward that all fall on a chunk edge. Guessed for any other movement |

A reading of the reader constants that does not hold: read speed = 1 +- W x inc gives 0.4989621, 1.9926199 and
2.0129798, which are -3.597, -6.400 and +11.199 cents against an exact octave. With W taken from exactly -3.6, -6.4
and +11.2 cents (dmin unchanged) the bursts of the voice null at -45.7 dB instead of -54.2 (down), -53.2 instead of
-53.2 (up left) and -49.8 instead of -59.1 (up right) (`detune_check.log`). Only the up-left value survives, so the
fitted W stay. A change of 0.005 sample in W is visible at this depth.

## 3. What a session must supply

| Context key | What | How to obtain |
|---|---|---|
| `oscillator_origin` | at-lines internal sample at which the last mode switch restarted the 32 line oscillators; the grain phasors restart with it. E: 497448424, D: 31574180 | Null the base model against a stretch that holds no layer yet (any Macro 0 recording, or the first 0.19 s of a tone) and scan the origin: -107 to -130 dB at the origin, -48 dB one sample off, -15 dB 44 samples off. It is a multiple of 44 and defined modulo 13159175 samples. Scripts: `macro_law/osc_origin.py`, `kernel/k00_base_clock.py` |
| `late_chunks` or `late_rule` | chunks whose boundary is one single-precision time step late | section 4. Either a list per voice, or `"late_rule": "block_grid"` with the host's block grid (`block_size`, default 512, counted from `block_origin_frame`, default the first frame of the stimulus) |
| `preceding`, `preceding_macro` | audio processed immediately before the stimulus | only when the layer still holds sound: its buffers reach 2.7 s back and the comb loses 14.07 dB per second |

`abyss_model.SESSION_E`, `SESSION_D` and `SESSION_D_STRICT` are ready-made contexts.

## 4. Late chunks (session D only)

**Observation.** With the law of section 1 and D's anchor, D's sixteen saved Abyss probes score:

| Processed time | Probes | Strict (anchor only) | With the rule |
|---|---|---|---|
| 760, 910 s | 25, 30 | -13.4, -8.6 dB | -67.8, -67.2 dB |
| 1060..1960 s | 35..65 (seven) | -67.2 to -67.6 dB | the same |
| 2110..2980 s | 70..99 (seven) | -3.2 to -8.9 dB | -67.0 to -67.9 dB |

Session E's thirteen saved Abyss probes (11290..11980 s) score -67.3 to -67.6 dB with no late chunk
(`probes_E.log`, `probes_D_strict.log`, `probes_D_late.log`).

**What a late chunk is.** Its `o` is larger by one single-precision step of the time in seconds: 2^-14 s (2.69
samples) between 512 and 1024 s, 2^-12 s (10.77 samples) between 2048 and 4096 s. X grows by that plus the change
of floor(o): 5.69 and 20.77 or 21.77 samples. The clock skeptic had found five such copies in D's c02
("+21.766 in X, unexplained"); `chunk_phase_D.log` of the interrupted modeler shows them in the probes.

**The rule (empirical).** A boundary is late when it lies in the middle of a host block whose index, counted from
the start of the step being processed, is 62 modulo 375 (512..1024 s) or 312 modulo 375 (2048..4096 s). 375
blocks of 512 frames are 4 s. It holds for all three voices: the octave-down voice is late exactly when one of its
boundaries falls on such a block (even-numbered probes), which is why odd and even probes differ.

One check without opening the batch: for the time range of D's c02 (3044..3068 s) the rule names the unison chunks
4571 + 6n. Those are the five copies the clock skeptic reported as off (impulses 3, 7, 11, 15, 19), found before
the batch was locked.

**What this means.** The chunk clock is not a pure count of internal samples. It reads something the host gives it
once per host block, in single precision. That is new, and it bears on the open question "first processed frame or
host transport position": the plug-in looks at per-block time information. Which quantity (seconds or beats) and
the arithmetic are not known.

**Limits of the rule.** It is a table with two entries, read from probes that all start on a whole even second.
Nothing is known for 4096..8192 s (never recorded). No late chunk was seen at 1024..2048 s or 8192..16384 s on the
boundary positions the probes reach. Other block sizes, other host rates and steps that start off a whole second
are outside it. D's batch starts on whole even seconds at 3010..3560 s, which is the case the rule covers.

## 5. Numbers (session E, in sample)

`revocean.null_db` on whole recordings, context `SESSION_E`, nothing fitted per recording. "Layer" is model minus
base against recording minus base.

| Recording | Null dB | Layer dB |
|---|---|---|
| c01 Macro 0, train | -106.99 | |
| c16 Macro 0, programme | -116.48 | |
| x05 step 0 % | -122.41 | |
| x05 steps 10, 20, 30 % | -90.81, -82.86, -74.75 | -76.7, -74.4, -69.2 |
| x05 steps 40, 50, 60 % | -69.83, -72.17, -72.28 | -64.7, -67.2, -67.3 |
| x05 steps 70, 80, 90, 100 % | -70.37, -69.60, -68.89, -67.27 | -65.7, -65.5, -65.5, -64.5 |
| c02 train, Decay 0.5 s | -60.40 | -56.5 |
| c03 three impulses | -67.15 | -64.1 |
| c04 programme | -67.14 | -64.2 |
| c05 noise | -60.25 | -56.5 |
| c06 1 kHz | -67.82 | -64.9 |
| c07 220 Hz | -67.96 | -63.7 |
| c08, c09, c10 (Macro 50, 25, 75 %) | -63.57, -64.24, -63.53 | -58.9, -57.8, -59.3 |
| c11 three impulses again | -59.37 (L -63.9, R -57.2) | -56.9 |
| c12, c13 (Size 60, 150 %) | -60.49, -62.63 | -57.1, -59.3 |
| c14 Decay 8 s | -67.64 | -65.4 |
| x02 impulse map left (600 s) | -61.51 | -57.5 |
| x03 impulse map right (300 s) | -61.30 | -57.2 |
| x04 1 kHz, 120 s | -67.53 | -64.7 |
| x06 sweep | -66.05 | -62.7 |
| c15 burst (Decay 2 s) | -66.11 | |
| c15 silence, 40 s | -13.69 whole; -58.34 over seconds 3..20 | |

The c15 silence is not a fair whole-recording test: the reference changed Decay from 2 s to its minimum at the
start of the silence and the model has one Decay per render, so the network's memory of the burst is wrong in the
first second, which carries the energy. The level per second follows the recording to 0.1 dB down to -590 dB.

Against the interrupted module: x03 -52.8 to -61.3, c03 -55.0 to -67.2, x05 steps 70/80/100 % -49.7/-50.0/-51.1
to -70.4/-69.6/-67.3, c14 -59.2 to -67.6 dB. A render of 20 s takes 2.5 to 4 s.

**Where the remaining error is** (`t20_budget.py`, deconvolved bursts of the impulse maps):

| Voice | Null of the voice | Share of the layer's energy | Share of the error |
|---|---|---|---|
| Unison | -61.9 dB | 62 % | 23 % |
| Octave up | -59.8 dB | 7 % | 4 % |
| Octave down | -54.1 dB | 31 % | 72 % |

Timing of the octave-down bursts is right to 0.0015 sample (sd over 130 bursts). The error is in the shape of each
burst and grows with the position p of the read in the reversed chunk: median -60 to -66 dB for p below 16384,
-54 to -60 dB from 16384 to 32768, -48 to -53 dB above 32768 (`t21_D_detail.py x02 D`). Those are the points
where the spacing of a single-precision number of the size of p doubles. The kernel skeptic found the same for the
unison voice. So the reference holds the read position in single precision and the model does not.

**Attempts on the octave-down voice, both under 1 dB, so I stopped:**

1. A free scale and offset of the window's phase against the delay's phase (`t22_fit_window.py x02 D
   fit=wscale,woff`): -54.64 to -54.98 dB on the fitted half of the impulses, -53.74 to -54.01 dB on the other
   half. Not adopted.
2. A single-precision read position, two guesses (`t20_budget.py x02 "position_rounding='nearest'"` and
   `'product'`): octave down -52.1 and -50.3 dB against -54.1 dB, unison -59.7 and -57.3 against -61.9 dB.
   Worse. Left in the module as an option, default "exact".

The octave-up voice and the Macro steps were not pushed further. The octave-up voice carries 4 % of the error. The
steps now null at the level of the steady parts: every step of x05 is within 5 dB of its last 15 s in its first
second (-62.8 to -89.8 dB; from the second modeler's log `model_kernel/logs/null_x05.log`, whose constants these
are; the whole-step nulls were re-measured here and agree to 0.01 dB, the first-second figures were not).

## 6. Commands

All from `model_structural/`, with `/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python`.

| Command | Result |
|---|---|
| `python score.py --base` | every recording of session E's common sequence; writes `scores_E.json` afresh (about 4 minutes; log of the final run: `score_final.log`) |
| `python score.py c02 x05` | only these, merged into `scores_E.json` |
| `python probes.py E 376 380 399` | probes of session E |
| `python probes.py D 25 80 99 [--late]` | probes of session D, strict or with the late rule (probes only) |
| `python t20_budget.py x02` | error budget by voice |
| `python t21_D_detail.py x02 D` | gain, shift and null per burst against tap phase and chunk position |
| `python t22_fit_window.py x02 D fit=wscale,woff` | fit of reader constants on even impulses, test on odd ones |

`scores_E_interrupted.json` holds the interrupted module's numbers for comparison.

`t20`..`t22` read the kernel skeptic's deconvolved bursts (`kernel_verification/cache/events_x02.npz`) and a cache
of the impulse maps' internal input in the scratch folder named in `dev.py`. `abyss_core.c` is compiled on first
use into the folder.

## 7. What was taken from session D

- The anchor 31574180 (from the earlier reports).
- From D's probes p025..p099 (not the batch): that the phasors restart with the oscillators there too (all sixteen
  probes at -67 dB once the late chunks are in), and the two table entries of the late rule. The rule changes
  nothing in session E. It is not a per-session anchor in the strict sense, which is why it is opt-in:
  `SESSION_D_STRICT` is the model tuned on session E alone, `SESSION_D` adds the rule. A holdout score should say
  which of the two it used; I would report both.
- No script in `model_structural/` or `model_kernel/` opens a c-recording of session D (checked: the only files
  read from `session_D/` are `probe.stimulus.npy` and `pNNN.npy`).

## 8. Limits, and the measurement that would remove each

| Limit | Measurement |
|---|---|
| Floor of -60 to -68 dB at full Macro: the read position of the reversed readers is single precision, rule unknown. It costs the octave-down voice most | Seeded white-noise bursts of 5 ms (0.1 rms) every 4.0137 s, 150 left then 75 right, Macro 100 %, Decay at minimum. Every output sample then shows its own interpolation fraction, and the staircase can be read off |
| Late chunks: the arithmetic is unknown; the rule is two table entries from one host layout | The click scan (4 + 1/192 s spacing) run in one instance at host blocks of 64, 512 and 1024 frames, each from a step that starts off a whole second, early in a session (512..4096 s) and again beyond 4096 s. Shows whether lateness follows the host block and gives enough positions to find the rule |
| Seconds or note values; frame count or transport position | The transport test already prepared: the same impulses at 120 BPM, at 96 BPM and with the reported position 0.25 s ahead |
| Macro in motion: only steps of 0.1 upward, each on a chunk edge | Steps of 20.25 s (mid-chunk), of several sizes, up and down, on a 1 kHz tone at Decay 0.5 s |
| The switch itself: the layer before the oscillators restart is not modelled, and a stimulus must lie behind the switch. One switch per session was seen | A recorded switch inside a steady tone, and a second switch later in the same instance |
| One Decay and one Size per render | none needed for the replica; a render with a Decay schedule would make the c15 silence a fair test |
| Beyond 16384 s the 1/1024 s tick is no longer a single-precision number; behaviour unknown | one probe after 4.6 hours of processed time, if it matters |
| Other host rates and block sizes | 40 clicks of the scan at 44.1 and 96 kHz |
| Level: every stimulus was at or below 0.5 and the model is linear | impulses alternating 0.5 and 0.05 |
| Fades of the octave voices are raised cosines by assumption; they sit in front of the grain readers by assumption | the noise-burst scan above resolves both |
