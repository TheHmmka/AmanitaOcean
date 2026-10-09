# Abyss: first look

Synthesis of 8 October 2026. Black box only: recorded audio of the attended sessions and the campaign's own model.
Nothing here was measured anew except one comparison marked "checked here" and one derivation marked "derived here".

**Read this first.** The brief for this note speaks of session D alone and calls the next session "E". Since it was
written, a session E has been run (`probe5.py`, folder `session_E/`) and a second wave of four packets has studied D
and E together (`../analysis2/`). This note uses both waves. Every statement carries its source:

- **V1**: confirmed or corrected by a skeptic of the first wave (the four `*.verification.md` in this folder; session
  D, several with session E as out-of-sample data).
- **V2**: confirmed or corrected by a skeptic of the second wave (`../analysis2/clock.verification.md`,
  `kernel.verification.md`, `macro_law.verification.md`, `tone_sweep.verification.md`; sessions D and E).

Claims that only an analyst made are not in section 1. "Samples" are internal samples at 44.1 kHz counted from the
instance's first processed frame; T is processed time in seconds on the same count; m is Macro as a fraction.

## 1. What is established

### 1.1 Abyss adds a layer in front of the base network; the base itself is untouched

- **Abyss = base(x) + base(P(x)).** The base network of Macro 0 is in every Abyss recording at gain 1, at Macro 25,
  50, 75 and 100 %: before the layer arrives, recording minus model is -100 to -134 dB with nothing fitted, a fitted
  gain is 1 within 3e-6, and while the layer sounds the base's share in the remainder is -8e-7 to +6e-6 (V1, V2).
  Macro is an addition, not Tide's cos/sin crossfade.
- **Everything Abyss adds is the base network's own response to a second signal P at the network's input, on the
  channel it came from.** First event of five cases fitted with 240 free input samples: -126 to -131 dB; the whole
  layer from 0.83 to 3.35 s behind an impulse as 9 or 10 input pulses: -104.5 to -111.4 dB re the layer (V1); 2580
  bursts behind 149 impulses leave -61.8 dB (median per impulse, V2).
- **P enters in front of the network's equaliser** (-131 to -136 dB against -31 dB behind it, V2) and is made from the
  input converter's output, not from the equalised signal (V1).
- **The network's output is not shifted.** A delayed, half-speed or double-speed copy of the base response explains
  2 to 7 % of the layer (V1, V2). On tones the octave components follow the model's response to a tone of their own
  frequency at the same instant (phase wander 3.6 and 9.4 degrees against 28 and 94 for the network alone, V2).
- **Left and right are separate.** In band, the other input's share of P is -132 dB for the reversed copies, -115 dB
  for the octave-down bursts and -67 dB for the octave-up clicks, which is as far as those can be solved (V2). At the
  output the layer's left-minus-right level is the base's (+7.6 dB in both, V2): the network's own cross-feed.
- **Neither Decay nor Size touches P** (Size 60, 100, 150 %, Decay 2 and 8 s: same delays, gains, repeat factor and
  ring-out in both sessions; V1, V2).

### 1.2 A change of mode restarts the base's oscillators and nothing else of the base

- Macro 0 recorded in Abyss does not null against `network_model.render` at the counted warm-up (+2.9 to +4.2 dB).
  With the 32 line oscillators counted from an origin R and the converters left on the instance's frame count it
  does: session D, R = 31 574 180 (715.968 s): c01 -110.06 dB, c16 -115.94 dB; session E, R = 497 448 424
  (11 280.0096 s): c01 -106.99 dB, c16 -116.48 dB, x05_00 -122.41 dB; one sample to either side -48 dB (V1, V2).
- R is known modulo 13 159 175 samples (298.394 s), the exact period of the single-precision accumulators (one lap
  of an oscillator is 73 515 samples, not 73 500). Both origins are multiples of 44, which only one member in 44 of
  such a family is (V1, V2).
- D's restart fell in the middle of a job. E's fell 461.5 host frames behind a job boundary, in the second 512-frame
  block of that job; the host processes nothing between two jobs, so that click almost certainly came in the pause
  between two jobs (V2).
- The sound had left Tide at least 5.7 s (D) and 9.7 s (E) of processed audio before the restart. One switch with
  two effects, or two presses through another mode: not decidable, the probes concerned were not saved (V1, V2).
- **The layer's clocks do not restart** (section 1.6).
- **A mode chosen before Start does not reach the sound.** Session E: the label showed Abyss, 375 probes were Tide;
  switching away and back while probes ran gave Abyss (campaign README). The order is Demo, Start, then the mode.
- Tide at Macro 100 % does not repeat between instances (its voices are seeded per instance): p000 of D against p000
  of E, same jobs, -4.8 dB (checked here). Whether Abyss repeats is not known; see section 4, question 1.

### 1.3 Three voices, all of them the past played backwards

A steady tone f comes back as three components and nothing else: near f/2, at f, near 2f. In 100 s of a 1 kHz tone:
-9.1 / -6.2 / -17.5 dB re the input at 500 / 1000 / 2000 Hz, every other ratio from f/8 to 8f at -94 dB and lower
(V1, V2). One impulse becomes three kinds of bursts in P (V1, V2):

| voice | chunk | what an impulse becomes | DC gain | recirculation |
|---|---|---|---|---|
| reversed, same pitch | 29 400 samples (2/3 s) | one copy of the impulse, mirrored in time | 0.69280 | none |
| octave up | 44 100 samples (1 s) | four full-band clicks 1322.06 (left) or 1328.01 (right) samples apart under a half sine | 0.246 at the peak, 0.59 to 0.66 the four together | 44 100 samples, x 0.198 per pass |
| octave down | 58 800 samples (4/3 s) | one click stretched by two (-4 dB at 8 kHz, -14 dB at 10 kHz) | up to 1.417 | none |

All three return material in reverse order at unit time scale: on a sweep the lag of every voice grows by 1.96 to
1.98 s per second inside a cycle and falls back at its end (V2); four notes 0.3 s apart come back 0.3 s apart in
reverse order in both octave voices (V1). They are three reverse readers, not one reversed stream read three times:
the octave-down voice has its own mirror schedule, and the octave-up voice reads the mirrored samples without the
reversed copy's interpolation fraction (V2).

### 1.4 The reversed voice: exact

- An input at time t comes out at 2 M - t, M the mirror point at the end of its chunk. The copy is the input's own
  samples read backwards with a two-point (linear) interpolation: -72 dB (median of 144 copies), best -137 dB; a
  third tap stays below 0.0008 (V2).
- **Mirror law** (V2, constants from session E alone; X = output index + read index = 2 M):

      L   = 44100 * fl32(2/3) = 29400.000876 samples          b_k = L k          (k = 0 at sample 0)
      G   = 44100 / 1024      = 43.06640625 samples           m_k = floor(b_k / G),  o_k = b_k - G m_k
      s_k = 44 * ceil((G m_k - a) / 44),   41.645 <= a < 42.355
      X_k = 2 s_k + o_k + floor(o_k) - 2;      interpolation fraction = frac(X_k)

  267 copies of session E lie within 0.019 sample of it (rms 0.0015); session D, nothing re-fitted: 53 of 58 within
  0.008 sample, at Macro 25 to 100 %, Decay 0.5 to 8 s, Size 60 to 150 %, either input. So the clock's zero is the
  same internal sample in two instances whose modes were switched 10 564 s apart. Speed 1 within 2e-8.
- **Five exceptions** in session D (c02, impulses 3, 7, 11, 15, 19 at T = 3047.25 + 4 n s): X is 21.766 samples
  above the law, which is o + G/4 = 2^-12 s. Not a measurement error, not explained (V2).
- **The interpolation fraction is a single-precision number.** In the copies that fit below -100 dB it is a whole
  multiple of the single-precision spacing of half the delay (1/2048 for half-delays of 4096 to 8192 samples, 1/1024
  up to 16 384, 1/512 above) and hops by one such step from sample to sample. A replica that interpolates with an
  exact fraction cannot null a copy below about -65 dB (V2).
- **Window of one replay** (V1 at 24 positions; V2 from impulses and from a tone): zero at the chunk edge, raised
  cosine up over the first 136.9 ms (6036 samples; 50 % at 68 ms), plateau, down over the last 47.1 ms (2075.5
  samples). Chunks do not overlap: where a chunk running on would put a second copy the gain is 0.000 (V2).
- **Gain** on the plateau **0.6927952 +- 0.0000001** in the 16 cleanest copies, both inputs (V2). That excludes
  0.98/sqrt(2) and ln 2 and lies 36 ppm below 1.2/sqrt(3) = 0.6928203. Copies whose fraction hops read 0.6915 to
  0.6941, which is the "0.2 % scatter" of the first wave.
- On a tone the component is exactly at f inside a chunk and its phase steps by -109.11 degrees per chunk at 1 kHz,
  which is the mirror law (V2).

### 1.5 The octave voices: detuned two-grain readers behind a reversal

- **Each is a reader with two overlapping grains, a half-sine window two grain periods long, and a read speed that
  is not an octave** (V2):

  | voice | grain period (samples) | read speed | successive reads of one input sample |
  |---|---|---|---|
  | octave down, both inputs on one clock | 5302.5 | 0.4990 (-3.5 cent) | 5323.84 apart; one read per sample |
  | octave up, left | 2653.9 | 1.9926 (-6.4 cent) | 1322.06 apart; four reads |
  | octave up, right | 2638.9 | 2.0130 (+11.3 cent) | 1328.01 apart; four reads |

  The octave-down speed rests on three independent measurements (burst shapes null best at 0.4990: -58 to -71 dB
  against -47 to -52 dB at 0.5; a 44-sample step of the mirror moves the burst 176.357 samples; the step per
  impulse). The octave-up speeds agree with the first wave's 1.99266 and 2.01304, which were coherent over 450 s of
  session D. The grain clocks run free of the mirrors: a step of a mirror moves a burst by 88 / speed, never by a
  grain.
- **Octave down.** A click at phase p of the 4/3 s cycle returns at delay 8/3 s - 2 p + lag, lag between 0 and 0.12 s
  in a sawtooth: all 34 copies of session D and 138 of session E's left map (V1). Peak sum 1.4169 +- 0.005 (left),
  1.4141 +- 0.006 (right): a gain of 1/sqrt(2) on a click twice as long. Over the chunk, in 8000-sample bins of the
  delay: 0.03, 0.13, 0.35, 0.62, 0.85, 0.98, then 1.00 +- 0.03 from 48 000 to 104 000, 0.60, 0.16: a long fade at the
  start of a replay, a short one at its end (V2). It reads with two points: an image at 22 050 Hz minus its
  frequency, 26 to 29 dB down (V2). In every second scan cycle its fraction is the reversed copy's minus 0.355 (V2).
- **Octave up.** Input reversed in 1 s chunks (about 70 positions, V1), ages 2 (1 s - p) + 0 to 120 ms. Half sine
  over the four clicks: peak 0.246 (left) and 0.242 (right), length 5260 and 5360 samples of age (V2; first wave:
  119.8 and 120.9 ms). Flat to 19 kHz of output; above 19.8 kHz the recording says nothing.
- **Its recirculation** (V1, V2): 44 100.6 +- 0.5 samples in the stream, 14.066 dB per pass (0.198; on impulses
  0.1996 +- 0.007 left, 0.2019 +- 0.007 right, then 0.037), followed from -70 dB down to -590 dB. It sits in front of
  the reader (every pass is read again on the reader's own lattice, 1011.49 or 981.51 ms behind the pass before,
  never 1000.00 ms), in front of the voice's Macro gain, and it runs while Macro is 0. It ignores Decay, Size and
  Macro, and it does not shift again: behind a 1 kHz tone the tail stays at 2 kHz for eleven passes with every other
  band 70 dB down. The other two voices have no return (none found behind any reversed copy).
- **Frequencies on tones** (V2). Inside one cycle each octave component is a sinusoid at a fixed frequency that is
  not f/2 or 2f: 1 kHz gives 500.98 to 501.00 Hz and 1996.99 to 1997.04 Hz; 3 kHz on the right gives 1494.62 to
  1494.71 Hz and 6041.43 to 6041.45 Hz; 220 Hz gives 111.875 Hz and about 436.0 (left) / 437.0 Hz (right). At every
  cycle boundary the phase steps (+136.1 degrees per 4/3 s at 500 Hz, -161.17 degrees per 1 s at 2 kHz), so the
  lines of a long spectrum are at 501.2835 + 0.75 n Hz and 1996.5523 + n Hz. The frequencies fit f - j g (down,
  g = 8.317 Hz) and f + j g (up, g = 16.617 Hz left, 16.711 Hz right) with whole j; a lattice j g - f does not.
- **Sidebands** (V1). The octave voices add lines the base and Tide do not have: 15 to 28 dB above the model's skirt
  20 to 60 Hz from the 500 Hz component, and a comb 16.7 Hz apart around the octave-up component of a 220 Hz tone.

### 1.6 Clocks

- Three periods: 2/3 s, 1 s (1.000000 s +- 2.5e-6; 0.5 s excluded), 4/3 s. Together they repeat every 4 s (V2).
- **All three count from the instance's first processed frame, not from the change of mode**: the reversed voice to
  0.001 sample in two sessions; the octave-up voice within about 10 ms; for the octave-down voice a restart at the
  switch (41.8 ms) is excluded, at a resolution of 20 to 35 ms (V2).
- The session host hands the plug-in a transport that plays at 120 BPM with position = processed frames. "First
  frame" and "transport position" are the same thing in this data; 2/3, 1 and 4/3 s are a third, a half and two
  thirds of a bar (V1, V2).
- **Odd and even probes**: 30 s is 45 chunks, 30 cycles and 22.5 cycles. Only the octave-down voice sees successive
  probes half a cycle apart (V1, V2). The slower swing of the band shares (period 11.3 to 11.9 probes) is the reversed
  copy of the 3 kHz tone beating against the base; the mirror law predicts it (98 % in E) with a period of 11.70
  probes (V2).

### 1.7 The Macro law

Three gains, each a straight ramp with hard ends (eleven steps of 10 % on a tone, both outputs; impulse cases at 25,
50, 75 % agree; V2):

| voice | gain re its full level | check |
|---|---|---|
| reversed | min(1, m / 0.300) | 0.3334, 0.6668 at 10, 20 %; impulses at 25 %: 0.832 to 0.834 |
| octave up | clamp((m - 0.1461) / (0.4596 - 0.1461), 0, 1) | 0.172, 0.490, 0.810 at 20, 30, 40 %; impulses at 25 %: 0.331 to 0.332 |
| octave down | max(0, (m - 0.6041) / (1 - 0.6041)) | 0.242, 0.495, 0.747 at 70, 80, 90 %; impulses at 75 %: 0.364 to 0.375 |

- Below its foot a voice is off, not quiet (octave up at 10 %: -101 dB re its full level; octave down at 60 %: -94 dB).
- Delays, grain grid and frequencies do not move with Macro.
- The gain acts behind the voices' buffers: sound stored at Macro 0 plays at the new gain at once. A smoothing of
  Macro longer than about 40 ms is excluded. The "100 ms glide" of the report is the fade-in every chunk has (V2).

### 1.8 Levels, noise, silence

- Output over base on three impulses: +1.26, +2.08, +2.06, +3.36 / +3.44 dB at Macro 25, 50, 75, 100 % (Tide: +0.4
  to -2.7 dB) (V1). 1 kHz tone at Macro 100 %: +3.3 dB (left), +2.8 dB (right) (V2).
- White noise at Macro 100 %: +3.0 to +3.8 dB up to 4 kHz, +2.0 dB at 8 kHz, +1.2 to +1.5 dB above 10 kHz; none of
  Tide's high-frequency loss; the tail falls 14 to 18 dB/s against Tide's 33 dB/s (V1).
- On a sweep, in front of the network and re the dry input: reversed -4.8 / -3.9 dB, octave down -5.5 / -5.2 dB,
  octave up -7.3 / -7.5 dB (left / right), flat within about 1.5 dB per third octave from 400 Hz (V2).
- No self-noise: with silence in, the output is the instance's floor (rms 6.9e-37). But the layer never rests: the
  1 s loop goes on at 14.07 dB per second, and the standard flush (8 s + 2 s) only waits 140.7 dB of it. What a
  recording of sessions D and E inherits is -259 to -529 dB (V2).

### 1.9 Statements that did not hold

So that nobody builds on them: the first component is a forward copy of the pulse (it is mirrored); "-16 samples per
chunk" as the boundary rule (true only for chunks three apart); the octave-down voice plays half of its cycle (it
plays all of it; the probes' left impulse sits on a chunk boundary); a 2/3 s chunk at half speed (it is a 4/3 s chunk
at unit time scale); ring-out passes are not copies of each other (they correlate 0.90 to 0.94 below 100 Hz, as much as
the network allows); the click train repeats every 44 100 samples at x 0.2 (the reversed signal does, the clicks are
read again); no grain sidebands; the lattice j g - f with g = 8.2928 / 16.8371 Hz; the chunk is the single-precision
number 666.6667 ms (it is fl32(2/3) seconds); a 100 ms glide of Macro; read speeds of exactly 0.5 and 2; one reversed
stream shared by the three voices; a grain window of 5235 samples (it is two grain periods).

## 2. Suggested, not settled

- **How the two kinds of evidence about the octave readers fit together** (derived here, not checked by a skeptic).
  Take the reader of section 1.5 with grain period H, speed v, and every grain starting a fixed delay behind real
  time. A tone f in its input then comes out on the lines f + j (44100 / H), j any whole number, each weighted by
  the transform of the grain window centred at v f. The strongest line is the one nearest v f:

  | voice | 44100 / H | v f for 220 / 1000 / 3000 Hz | nearest line | measured inside a cycle |
  |---|---|---|---|---|
  | down (5302.4, 0.49899) | 8.3170 Hz | 109.8 / 499.0 / 1497.0 | 111.88 / 500.98 / 1494.62 | 111.875 / 500.98 / 1494.62 |
  | up, left (2653.98, 1.9926) | 16.6166 Hz | 438.4 / 1992.6 / - | 436.02 / 1996.99 / - | 436.0 / 1997.0 / - |
  | up, right (2638.80, 2.0132) | 16.7121 Hz | 442.9 / - / 6039.5 | 437.26 / - / 6041.61 | 437.0 / - / 6041.44 |

  Seven of seven within 0.26 Hz, among them the two that the tone packet calls unexplained (182 steps above 3 kHz
  instead of 180; the second nearest line to 1500 Hz). The same reader gives the click gaps (H (1 - 1/v) = 1322.10 and
  1327.95), the number of reads of one input sample (2 v: one and four) and the sum of four clicks under a half sine
  (0.246 x 2.41 to 2.61 = 0.59 to 0.64). The skeptic of the kernel packet expects lines at j / P - f (497.04 or
  505.36 Hz for 1 kHz) and asks for a reconciliation; those lines exist for a real tone but carry the window's
  weight at -v f. Part S2 of the next session (tones in steps of 1 Hz) decides: lines that move 1 Hz per Hz and jump
  by 8.3 or 16.6 Hz, against 0.5 or 2 Hz per Hz for a plain ratio.
- **Where the grain clocks start.** The octave-down lag grows 0.780 ms per probe in session D with steps of -4.0 ms,
  and far less in session E; in four late stretches of session D the 500 Hz phase is half a period away from session
  E's. Counted from the first frame, tied to the switch, or seeded per instance: the main unknown for a replica.
- **Mirror laws of the 1 s and 4/3 s chunks to the sample.** Boundaries are known to about 2 ms; the octave-down
  voice has long steps of its own and a fraction 0.355 away from the reversed copy's in every second cycle, which is
  one of the three classes of the reversed voice's law.
- **Chunk windows of the octave voices as curves** (binned only; octave up: 0.05 at 4 to 8 thousand samples of delay,
  0.61 to 0.64 from 24 to 80 thousand, gone at 88 200, from the analyst alone).
- **Two skeptics disagree on the reversed read's speed**: exactly 1 with a chunk length of fl32(2/3) s (clock packet,
  two sessions, 0.001 sample), or 8.7 ppm fast (kernel packet, session E alone, one recording per input). The first
  explains the second as the chunk index growing along the scan.
- **Closed forms**: 0.6927952 is not 1.2/sqrt(3) to its own precision; 1/sqrt(2) for the octave-down grain (1.4169 /
  2 = 0.7085, 1.4141 / 2 = 0.7071); 0.49 per octave-up read; knees 0.1461, 0.300, 0.4596, 0.6041.
- **Which side of the 1 s reversal the loop is on**: a loop of exactly one chunk gives the same signal either way.
- **The restart rule of the oscillators**: "first 44-sample block of the host block that takes the change" fits D and
  E with one constant (2.4 to 8.8 samples of delay), from two events.
- **A full kernel on impulses**: base plus reversed copy plus octave-down burst predicts held-out impulses at -8.5 dB
  of the layer; with clicks and returns the analyst reports -14.3 dB, which no skeptic rebuilt.
- **Level independence**: only impulses of 0.4 and 0.5 and tones of 0.2 to 0.25 were measured.
- **The outer shell in Abyss**: every case so far was at the neutral baseline. In Tide the pre-delay sits in front of
  the converter whose output the layer taps, so P should be behind the pre-delay; not tested.

## 3. Hypothesis of the layer

Marks: **M** measured (confirmed by a skeptic), **I** inferred from measured numbers, **G** guessed.

```
per input channel c (left, right), at the internal rate, k = 0 at the instance's first processed frame:

  u_c[k]                 output of the input converter, not equalised                                     M
  in_c[k] = u_c[k] + P_c[k]      -> equaliser -> 44 samples -> the base network of Macro 0, gain 1        M
  P_c = gR(m) R_c  +  gU(m) U_c  +  gD(m) D_c           nothing from the other channel                    M (-132, -67, -115 dB)

  R_c = 0.6927952 * win_R * reverse_R(u_c)                       chunk 29400.000876                       M
  U_c = 0.49 * read( reverse_U(w_c), v_U, H_U )                  chunk 44100                              M: chunk, four reads, speeds;  I: 0.49
        w_c[k] = u_c[k] + 0.198 * w_c[k - 44100]                 the loop, in front of the reader         M: period, gain, place, no second shift
  D_c = 0.7071 * read( reverse_D(u_c), v_D, H_D )                chunk 58800                              M: chunk, unit time scale, speed;  I: 1/sqrt(2)

  gR = min(1, m / 0.300)    gU = clamp((m - 0.1461) / 0.3135, 0, 1)    gD = max(0, (m - 0.6041) / 0.3959) M
  the gains multiply the voices' outputs (behind every buffer); the loop of U runs at every Macro         M
  Decay and Size do not enter P                                                                           M
```

**reverse_X(s)**: chunk j is the input between boundaries j and j + 1; during chunk j + 1 the output at sample n is
the two-point interpolation of s at X - n, X twice the mirror point. Three separate readers (M). For the reversed
voice X is the law of section 1.4, with the single-precision chunk length, the 1/1024 s tick, the 44-sample block and
a fraction quantised to the single-precision spacing of half the delay (M; the five exceptions of 2^-12 s are not in
it). For the other two: the same law with L = 44100 and 58800 (I; boundaries measured to 2 ms; the octave-down
voice's fraction differs by 0.355 in every second cycle and the octave-up voice reads without the fraction, M). All
three count k from the first processed frame (M to 0.001 sample, to 10 ms and to about 30 ms), or from the host's
transport position, which was the same thing (open).

**Windows of a replay**: reversed voice: 0 at the edge, raised cosine up over the first 6036 samples, 1, raised
cosine down over the last 2075.5 samples (M). Octave down: 0.03 / 0.13 / 0.35 / 0.62 / 0.85 / 0.98 in the first six
8000-sample bins of delay, 1 from 48 000 to 104 000, 0.60, 0.16 (M as bins; I: a raised cosine of about 0.5 s and a
short one). Octave up: about 0.35 s of rise, plateau to 80 000 samples of delay, gone at 88 200 (I).

**read(s, v, H)**: grains every H samples, each 2 H long under sin(pi x / 2H), each reading s at speed v with
two-point interpolation from a start a fixed delay behind real time (M: two grains, half sine of two periods, speeds,
periods; I: the fixed delay, from the tone lines of section 2). Down: H = 5302.5, v = 0.4990, one clock for both
inputs. Up: left H = 2653.9, v = 1.9926; right H = 2638.9, v = 2.0130. The grain clocks are independent of the
mirrors (M). Where they start is not known (G: counted from the first frame like everything else).

**Base**: `network_model` with its oscillators counted from the last change of mode: the first 44-sample block of
the host block that takes the change (I, two events); converters and layer clocks untouched (M). A mode chosen
before the first processed block does not take effect (M, one session).

**Not in the hypothesis because nothing is known**: pre-delay, input filter, Brightness, Transients, Ducking, Width
and Mix in Abyss (G: P behind the pre-delay, the shell as in Tide); Macro in motion beyond "no smoothing longer than
40 ms"; other host rates (G: every length above is in internal samples); tempo (G: none).

What a first implementation should null, in this order: Macro 0 (base with the session's origin: -107 to -122 dB);
then the reversed voice at Macro 10 % behind impulses and on a tone, where nothing else is on (copies to -100 dB and
better only with the single-precision fraction); then Macro 50 %, where the octave-up clicks and their loop join;
then Macro above 60 %.

## 4. Open questions

1. **Is Abyss a deterministic function of the processed frames and of the frame of the switch?** Tide is not. The
   reversed voice is; the base is once the origin is known; the grain clocks of the octave voices are unknown. Two
   sessions that switch on the same frame decide it (`SESSION_E_DESIGN.md`).
2. **What a change of mode does**: the block in which the oscillators restart, whether every change restarts them,
   whether the lines or the layer's buffers are cleared, how the layer starts, and why a mode chosen before Start is
   ignored.
3. **The octave voices to the sample**: mirror laws of the 1 s and 4/3 s chunks, chunk windows as curves, grain
   clocks (origin, the 0.78 ms per probe and the 4 ms steps), the readers' fractions, the loop's gain to more than
   three digits, and the reconciliation of impulses and tones (section 2).
4. **Absolute time and single precision**: the five exceptions of 2^-12 s at T = 3047 to 3063 s, the origin of the
   1/1024 s tick, the quantised fraction. No dense scan crosses T = 4096 s or 8192 s yet.
5. **Tempo and transport**: fixed lengths in seconds, or a third, a half and two thirds of a bar. The plug-in has a
   tempo-synced pre-delay, so it reads the play head. Needs a change of the session tool (tempo, a stopped play head,
   a shifted position).
6. **The outer shell in Abyss** (pre-delay, HPF, LPF, Brightness, Transients, Ducking, Width, Mix).
7. **Linearity in level**, and the two inputs at once (the octave-up clicks' cross-talk is bounded only to -67 dB).
8. **Macro in motion**: smoothing shorter than 40 ms, latching at a chunk edge, the knees as closed numbers.
9. **Other host rates and block sizes** (the laws contain 44, 44100/1024 and fl32(2/3) s; block 512 in every session).
10. **Is the reversed read exactly at unit speed** (section 2), and why is its gain 36 ppm under 1.2/sqrt(3)?
11. **Foam** has not been recorded; the state between the click and the oscillator restart in D and E may be it.
