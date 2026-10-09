# Abyss: the outer controls and a moving Macro

9 October 2026. Black box only: recordings of session F (`session_F/`, part K, part S4, the anchors, four cases of
part B) and of the dry run in Tide (`session_dry/`), the campaign's code, and the scored Abyss model
(`analysis2/model_structural/abyss_model.py`, imported, not edited). No session was opened, no capture made, the
holdout `h1..h3` was not loaded. Scripts, logs and numbers: `analysis3/controls/`; `make_table.py` prints the table of
section 1 from the stored numbers (`--run` runs everything again, about 12 minutes).

Marks: **M** measured (a null or a level decides it), **F** fitted (a number that minimised a residual),
**G** guessed (assumed, not tested on its own). Null = `revocean.null_db`, whole recording unless a window is named,
nothing fitted per recording unless said. "Layer null" is model minus base against recording minus base.

## 0. What the analysis stands on

- **Session F's oscillator origin is 1 323 432** (44 x 30 078; taken from `analysis3/session_F_origin.json`, written
  by the tempo colleague, and checked here, `controls/a00_check.log`): the base model on eight Macro 0 recordings from
  3010 s to 8224 s nulls at -96.9 to -119.2 dB; one sample off -48.2 dB, 44 samples off -15.6 dB. **M**
- **Neutral reference of part K**: k00 and k17 (Macro 100 %, nothing moved) null at **-59.4 and -57.8 dB** with the
  origin alone; one least-squares gain per voice gives 1.0000, 1.0001, 0.9998 to 1.0006. This is the ceiling of
  every routing test below (base alone: -2.7 dB).
- **Late-chunk context: not used anywhere in this report.** Part K and fifteen of the sixteen Macro steps of S4 need
  none. Three stretches cannot be nulled without it and are left out: S4's Macro step 8 (steady part -4.2 dB), Size
  step 16 and Decay step 24. See section 5 for what was seen of the displaced chunks.

## 1. Table: routing of every outer control

Whole-recording nulls in dB. Candidate letters are explained per control below.

| case | control | nulls of the candidates | routing found |
|---|---|---|---|
| k01 | Pre-delay 100 ms (4799 frames) | A -55.7; B +0.2; C +0.1; N +2.8 | A: P is tapped behind the pre-delay |
| k16 | Pre-delay 333 ms (15 982 frames) | A -59.2; B +0.5; C +0.6; N +3.4 | A |
| k02 | HPF 1 kHz | A -61.2; B -11.0; C -8.1; N -8.2 | A: filter in front of the tap |
| k03 | LPF 2 kHz | A -64.7; B +1.4; C -1.6; N +5.5 | A |
| k04 | Width 0 % | A -60.9; B -2.5; C -6.3; D -4.8; E -1.7 | A: the Tide shell on the wet sum |
| k05 | Width 150 % | A -60.2; B -8.9; C -12.3; D -11.5 | A |
| k06 | Mix 50 % | A -69.9; B -6.0; C -10.7; D -0.2 | A: the Tide law (dry 1, wet 1) |
| k09 | Ducking 50 % | A -60.0; B -39.1; C -59.0; N -38.3 | A: on the wet at output time, keyed by the raw input |
| k07, k08 | Brightness +100, -100 % | no null (no law); band levels, section 2.4 | inside the network, behind P's return |
| k10 | Transients at full | no null (no law); levels and onsets, section 2.6 | behind P's return; its detector hears the layer |
| k11, k12 | Size 30, 200 % | model -61.2, -57.7 (base alone -4.2, -4.3) | P untouched |
| k13 | Decay 20 s | model -58.0 (base alone -3.1) | P untouched |
| k14, k15 | Macro 80, 30 % | model -62.5, -62.5 | the model's ramps hold |

## 2. Control by control

### 2.1 Pre-delay (k01, k16) — M

`D = max(0, floor(P fs / 1000) - 1)` whole host frames with the single-precision arithmetic of
`reference_render.predelay_samples`: 4799 frames for 100 ms, 15 982 for the knob's "333 ms" (332.99994 ms).

| candidate | k01 whole / layer | k16 whole / layer |
|---|---|---|
| A  host in -> pre-delay -> converter -> u -> tap of P (the layer is made from the delayed input) | **-55.7 / -53.2** | **-59.2 / -56.1** |
| B  P taps the undelayed input, its return is not delayed | +0.2 / +2.7 | +0.5 / +3.7 |
| C  P taps the undelayed input, its return is delayed with the base's input | +0.1 / +2.7 | +0.6 / +3.8 |
| N  no pre-delay | +2.8 / +5.3 | +3.4 / +6.5 |
| A with one frame less / more | -0.2 / -0.1 | +0.1 / +0.1 |

The reversed copies therefore come *earlier* by the pre-delay behind the delayed direct sound (the mirror points do
not move), as `ABYSS_STATE.md` predicted (its mark G becomes M).

**For the C++ engine:** keep the pre-delay where Fathom has it (host rate, whole frames, in front of the converter);
the layer reads the converter's output `u` and needs nothing of its own.

### 2.2 Input filter, HPF and LPF (k02, k03) — law F (exact at this depth), routing M

The campaign had no law for the input filter. From the 0.8 s behind the left impulse, which hold no layer yet:

| law tried, null of the base alone in 0.249..1.05 s | HPF 1 kHz | LPF 2 kHz |
|---|---|---|
| first order, bilinear (`k = tan(pi fc / fs)`), at the host rate, corner as displayed | **-123.5** | **-126.6** |
| the same, corner 1 Hz (HPF) / 2 Hz (LPF) off | -72.6 | -64.0 |
| the same at the internal rate (on `u`) | -50.6 | -39.1 |
| one pole `exp(-2 pi fc / fs)` at the host rate | -23.7 | -12.3 |

    k = tan(pi fc / fs);  a1 = (k - 1) / (k + 1)
    low-pass:  y[n] = k / (1 + k) (x[n] + x[n-1]) - a1 y[n-1]        high-pass:  y[n] = 1 / (1 + k) (x[n] - x[n-1]) - a1 y[n-1]

Routing with that law:

| candidate | k02 whole / layer | k03 whole / layer |
|---|---|---|
| A  filter in front of the tap: base(F u) + base(P(F u)) | **-61.2 / -57.9** | **-64.7 / -61.0** |
| B  filter in the base's input only: base(F u) + base(P(u)) | -11.0 / -7.7 | +1.4 / +5.1 |
| C  filter behind P's return: base(F(u + P(u))) | -8.1 / -4.7 | -1.6 / +2.1 |
| N  no filter | -8.2 / -4.9 | +5.5 / +9.3 |

So the voices are made from the filtered input: with LPF 2 kHz the octave-up voice has nothing above 4 kHz, and it
is not filtered again. The order of pre-delay and filter cannot be observed (both are linear and time-invariant).
Limits: one corner each, 48 kHz only; that the filter runs at the host rate at other rates, and its form with both
corners moved or at the ends of the range, are **G**.

**For the C++ engine:** a first-order bilinear high-pass and low-pass per channel on the host-rate input, in front
of the converter (with the pre-delay). Nothing in the layer.

### 2.3 Width and Mix (k04, k05, k06) — M

| candidate | Width 0 % | Width 150 % |
|---|---|---|
| A  Tide's law (`mid sqrt(2/(1+s))`, side `s sqrt(2/(1+s))`) on the wet sum behind the network | **-60.9 / layer -57.0** | **-60.2 / -56.7** |
| B  no width | -2.5 | -8.9 |
| C  the law on the base's wet only, the layer's wet untouched | -6.3 | -12.3 |
| D  the law on the layer's wet only | -4.8 | -11.5 |
| E  mono at the input instead | -1.7 | |

| candidate, Mix 50 % | whole / layer |
|---|---|
| A  Tide's law: dry `min(1, 2(1 - mix))` = 1, wet `min(1, 2 mix)` = 1, dry delayed by the 48 frames of latency | **-69.9 / -54.3** |
| B  linear (0.5, 0.5) | -6.0 |
| C  equal power (0.707, 0.707) | -10.7 |
| D  wet only | -0.2 |

Free gains fitted to k06: dry 1.000000, wet 1.000072. The dry one frame earlier or later: +2.5 dB.

**For the C++ engine:** the Fathom shell unchanged, applied to the wet sum (base + layer).

### 2.4 Brightness (k07, k08) — routing M by band levels; law unknown

The campaign has no law for Brightness, and none was fitted here, so there is no null. What Brightness does to the
base (recording against the neutral base model, octave bands 0.5 / 1 / 2 / 4 / 8 / 16 kHz, mean of the three events):

| | 25..105 ms behind the event | 105..400 ms |
|---|---|---|
| +100 % | -0.35 / -0.11 / +0.40 / +1.30 / +2.51 / +3.91 dB | -3.8 / -2.5 / -0.7 / +0.9 / +2.3 / +3.8 dB |
| -100 % | 0.0 / 0.0 / -0.3 / -0.6 / -0.8 / -0.8 dB | -0.2 / -0.7 / -2.3 / -5.3 / -8.4 / -9.0 dB |

So +100 % is a fixed high boost (the same early and late) plus a faster decay of the lows; -100 % is a faster decay
of the highs and almost nothing at the first pass. It lives in the loop and in a fixed stage of the network.

The layer's copies, measured the same way against the neutral Abyss model (which places and scales them to 0.1 %):

| +100 %, bands 4 / 8 / 16 kHz | measured | A: the copy passes the same network as the direct sound | B: the fixed part acts in front of the tap (octave up shows S(f/2), octave down S(2f)) |
|---|---|---|---|
| unison copy, 25..105 ms | +1.28..1.34 / +2.49..2.50 / +3.84..3.96 | +1.29..1.33 / +2.48..2.54 / +3.86..3.93 | the same (not decisive) |
| octave-up copy | +1.33..1.46 / +2.51..2.58 / +3.99..4.01 | as the direct sound | +0.0..0.1 / +1.1 / +2.3..2.4 |
| octave-down copy | +0.99..1.13 / +2.10..2.21 / +3.77..3.83 | as the direct sound | +2.1..2.2 / +3.7..3.8 / - |

At -100 % the unison copy follows the direct sound in both windows within 0.3 dB (for instance 105..400 ms:
-5.2..-5.4 / -8.2..-8.4 / -9.0..-9.2 dB against -5.2..-5.4 / -8.3..-8.5 / -8.8..-9.1 dB). A is right: Brightness
does not touch P; P's return goes through the brightened network like any input. Resolution about 0.3 dB; B is
1.0 to 1.7 dB away on six band readings per event.

**For the C++ engine:** route Ocean's own tone controls of the Character behind the point where the layer joins
the network input (or in the loop); none in front of the tap. The reference's Brightness law itself is not known.

### 2.5 Ducking, the level stage (k09) — routing M, threshold F

Tide's level stage with its threshold moved (key: raw input; 5 ms / 300 ms in dB; applied to the wet 48 frames
later). One number fitted on the windows without a layer: threshold **-13.4 dBFS** at Ducking 50 % (0 dBFS at 0 %).
It gives -0.021 dB behind a 0.5 impulse and -0.176 dB behind the 5 ms bursts, which the per-window gains show
directly (-0.02 and -0.17 dB, equal on both channels, recovering with 300 ms).

| candidate | whole | direct windows | unison copy of the bursts, 9.07..9.70 s |
|---|---|---|---|
| A  reduction on the wet sum at output time | **-60.0** | -69.6 | **-57.3** |
| B  reduction on the input in front of the tap | -39.1 | -41.0 | -38.2 |
| C  reduction on the base's wet only | -59.0 | -69.6 | -54.7 |
| N  none | -38.3 | -35.0 | -54.7 |

- B is excluded by 19 dB: the stage is not in front of P. **M**
- A against C rests on 2.6 dB in one window: when the copies arrive 0.83 s later the reduction has recovered to
  0.011 dB. Weak but in favour of "wet sum". **M, thin**
- The copies do not trigger a reduction of their own (A's -57.3 dB is the model's floor; a fresh 0.1 dB would leave
  -39 dB): the key is the raw input, not the layer. **M**
- The direct windows stop at -69.6 dB, not at the base's -110 dB: a moved threshold is not the whole law of
  Ducking 50 % (knee or ratio may move too). Not pursued.

**For the C++ engine:** Fathom's level stage as it is: keyed by the raw input, on the wet sum.

### 2.6 Transients (k10) — placement M by levels and onset shapes; law unknown

The campaign has no law for Transients. At full (-10 dB) the wet is not the neutral wet times a gain, nor a short
filter of it (per-window gains have no coherence; a 23 ms FIR leaves -2.5 dB). What is seen, energy of the recording
against the neutral Abyss model (`k08_transients_levels.log`, `k11_transients_voices.log`):

| event | direct | unison copy | octave-up copy | octave-down copy |
|---|---|---|---|---|
| left impulse (one sample) | -0.28 dB | -0.20 | -0.01 | 0.00 |
| right impulse | +0.12 | -0.23 | -0.57 | -0.23 |
| noise bursts (5 ms) | **-7.24** | **-7.18** | **-0.30** | **-6.96** |

- Nonlinear: a single-sample impulse keeps its energy, a 5 ms burst loses 7.2 dB.
- The onset of every response is rebuilt: 5 ms levels behind the left impulse read -123, -91, -71, -64, -58 dBFS
  where the neutral model has -54 dBFS at once; later the recording is 3 to 4 dB above the model.
- **The unison copy shows the same rebuilt onset, forwards in time** (-132, -99, -81, -69, -64 dBFS from 1.105 s)
  and nothing arrives before the copy's nominal time. A stage in front of the tap would be mirrored by the reversed
  reader. So the stage is behind P's return.
- **Each copy is judged on its own shape**: the 5 ms and 10 ms copies of the burst (unison, octave down) lose 7 dB
  like the direct sound, while the octave-up copy, which the model shows as 2.5 ms grains spread over 92 ms at the
  network input, loses 0.3 dB. A gain decided in front of the tap would take 7.2 dB from all three. So the detector
  hears the layer.
- Tide (dry run, same stimulus): impulse -0.1 dB, bursts -9.6 dB, the same slow onset (+-3 dB, the Tide voices are
  at another phase in each case). The stage is not special to Abyss.

Whether the stage sits at the network input (on `u + P`) or on the wet output is not decided, and neither is its law.

**For the C++ engine:** whatever the Character uses for Transients must work on the sum of input and layer return
(or on the wet), never on the input in front of the tap.

### 2.7 Size, Decay, Macro at rest (k11..k15) — M

| case | whole / layer null | fitted voice gains (unison, up, down) |
|---|---|---|
| Size 30 % | -61.2 / -57.0 | 1.0000, 1.0001, 0.9978 |
| Size 200 % | -57.7 / -53.4 | 0.9998, 1.0000, 0.9980 |
| Decay 20 s | -58.0 / -54.9 | 1.0000, 1.0001, 0.9993 |
| Macro 80 % | -62.5 / -58.4 | 1.0001, 1.0002, 1.0006 |
| Macro 30 % | -62.5 / -56.9 | 0.9999, 0.9992 (octave down off) |

The octave-down gain reads 0.998 at neutral Size as well (k04), so that is the model's scatter, not Size.
P does not depend on Size from 30 to 200 % nor on Decay up to 20 s; the ramps hold at 30 and 80 %.

**For the C++ engine:** Size and Decay stay out of the layer.

## 3. Macro in motion (S4, sixteen steps of 20.25 s under a running 1 kHz / 3 kHz tone)

Method: the fourteen steps in which a gain moves, the first 0.25 s behind each (layer null, dB). Steady parts of
the same steps null at -63 to -75 dB (layer null, `s01_steps_default.log`).

1. **Shape, without assuming one** (`s03_step_fit.log`). The recording behind each step was fitted as a mixture of
   model renders with an instant change at -2, 0, 2, 4, 6, 8, 11, 14, 18, 24, 32, 48 ms. The cumulative weights are
   the same in all fourteen steps, up or down, for every voice, on a chunk edge or not:

       0.00  0.15  0.30  0.43  0.53  0.63  0.73  0.81  0.88  0.94  0.98  1.00      (spread over the steps: +-0.02)

   Read at the midpoints between the instants this is `1 - exp(-(t + 0.6 ms) / 10.0 ms)`. **F**: one pole, 10 ms.
   No latch at a chunk edge, no ramp, no difference between up and down. **M**
2. **What is smoothed** (`s04_step_laws.log`). The scored model smooths the Macro value and then applies the
   clamped ramps (M). Smoothing each voice gain behind its ramp (G) is the same on steps that cross no knee and
   much better on those that do:

   | step (Macro after it) | 3 (0.17) | 5 (0.35) | 7 (0.48) | 9 (0.65) | 13 (0.30) | 14 (0.00) | 15 (1.00) |
   |---|---|---|---|---|---|---|---|
   | M: one pole on Macro, then ramps | -44.6 | -35.1 | -49.2 | -40.0 | -30.3 | -28.4 | -18.4 |
   | G: ramps, then one pole on each gain | -55.2 | -52.7 | -63.7 | -61.2 | -54.0 | -40.9 | -43.6 |

   **M**: the smoother acts on the three gains, not on Macro. This corrects the model.
3. **Time constant and lead.** 5 ms and 20 ms instead of 10 ms cost 12 to 24 dB on every step (-22 and -21 dB
   against -40 dB on step 1). 28 samples early instead of 44 gains 0.9 to 5.0 dB on twelve of fourteen steps (step 1:
   -45.0 against -40.0); not scanned finer. **F**: 10 ms +-1; lead between about 28 and 44 internal samples, in the
   model's numbering (Macro of host frame `floor((k + lead) 160 / 147)` acts on network-input sample k).
4. **Where the gains sit.**

   | placement (law G) | steps with the unison voice alone (1, 2) | octave up moves (6) | octave down moves (10, 12) | step 15 (0 to 100 %) |
   |---|---|---|---|---|
   | model: unison gain behind its reversed reader; octave gains behind reversed reader and comb, in front of the grain reader | -40.0, -46.1 | -54.9 | -49.4, -45.9 | -43.6 |
   | octave gains behind the grain readers | the same | -22.1 | -7.3, -8.4 | -7.5 |
   | all gains in front of the reversed buffers | 0.0, -7.2 | -19.4 | -13.2, -5.3 | 0.0 |

   **M**: a step of Macro reaches what is already inside the reversed buffers at once (through the 10 ms), and it
   reaches the octave voices through their grain readers (up to 120 ms of grain delay), as the model assumed.
5. What is left: behind a step the null is up to 25 dB above the steady part (law G: -41 to -67 dB in the first 0.25 s against -63 to -75 dB).
   The smoother's exact form (per sample or per block of 44, the exact lead) is not resolved. Step 14 (the layer
   fades out) reads -41 dB against a vanishing reference.

**For the C++ engine:** `g_v = G_v * clamp((macro - foot_v) / (knee_v - foot_v), 0, 1)` per voice, then
`g += (1 - exp(-1 / (0.010 * 44100))) * (target - g)` per internal sample on each of the three gains; multiply the
unison stream behind its reversed reader, and the octave streams between reversed reader (and comb) and grain
reader. Up and down alike. If Evolution gets a glide of its own, it goes in front of this.

## 4. Size and Decay in motion at Macro 100 % (the rest of S4)

The model has one Size and one Decay per render, so only the last 5 s of each 8.25 s step were nulled, with the
step's own setting (`s05_size_decay_steps.log`):

| step | layer null, last 5 s |
|---|---|
| Size 150, 30, 200, 100 % | -64.4, -63.8, -62.6, -64.1 |
| Decay 2 s | -64.4 |
| Size 60 % (first step), Decay 0.5 s (last step) | -2.7, -7.5: displaced chunks, see section 5, not Size or Decay |
| Decay 8 s, 20 s | not analysed (the network still holds the step before) |

**M**: once a change of Size or Decay has settled, the layer is exactly the one the model makes without knowing
of it. Not tested: the first seconds behind each change, where the base network itself is in transit and no model
of that transit exists. Nothing suggests the layer takes part.

## 5. Seen on the way: displaced chunks between 4096 and 8192 s (for the clock work)

- The Macro 100 % anchors z1..z4 (4284 to 7452 s) null at -6 to -8 dB with the origin alone; the model's one-step
  late rule does not repair them. In z2 the unison and octave-down chunks whose boundary is 8/3 s modulo 4 s have
  their X larger by **8.93 to 8.95 and 9.0 samples** (window 2.95..3.35 s: layer -30 dB with both, +3 dB without;
  `a05_dx.log`). That is not a whole single-precision step (21.53 samples) and cannot be reached by the model's
  `o + floor(o)` form. Found through a runtime wrapper around `abyss_model.mirror_x` (`common.DX`), used only there.
- Part K starts every case on a multiple of 4 s too, but its three events are read by chunks with boundaries
  2/3, 1 and 4/3 s modulo 4 s, which are in place.
- In S4 every step starts its own block grid, 0.4375 block further each time. Displaced chunks appear exactly in
  the steps whose grid sits 0 or 1/2 block from the 4 s frame (Macro step 8, Size step 16, Decay step 24); the
  other eighteen steps that hold a layer and could be read have none.

## 6. Levels: Abyss against its own base

Level of the recording minus level of the base model (the model at Macro 0), rms, both channels.

| Macro | tone, S4 steady parts (left 1 kHz / right 3 kHz) | impulses and bursts, part K (Decay 0.5 s) | three impulses, part B (Decay 2 s) |
|---|---|---|---|
| 25 % | +1.0 (+1.0 / +0.9) | | +1.2 |
| 30 % | +1.3 (+1.8 / +1.0) | +1.4 | |
| 50 % | +2.1 at 48 % (+1.9 / +2.3) | | +2.0 |
| 60 % | +1.9 at 62 % (+2.0 / +1.8); model at 60 %: +2.2 | model: +1.9 | |
| 75..85 % | +2.3 at 85 % (+2.4 / +2.2); model at 80 %: +2.4 | +2.2 at 80 % | +2.0 at 75 % |
| 100 % | +3.2 and +3.0 (+3.4, +3.2 / +3.0, +2.9) | +3.4 and +3.2 | +3.3 |

- On a steady tone the unison voice has the base's own frequency, so the sum depends on their phase: the same Macro
  read at another time moves by about 0.5 dB (model at 30 %: +1.9 dB at one time, recording +1.3 dB at another).
- Every stimulus is at or below 0.5: the level stage (knee from 0.5629) and the clipper (+8 dBFS) do nothing in
  these recordings. The level stage is keyed by the raw input and scales base and layer alike, so it would not
  change the difference; the clipper acts on the sum and can only reduce it.

**For the product:** a trim of about -1.4 dB at 30 %, -2 dB at 60 %, -2.3 dB at 80 % and -3.2 dB at 100 % makes the
Character as loud as its base (the rise is not a straight line: the voices come in one after the other).

## 7. What stays unknown

1. The laws of Brightness, of Ducking above 0 % (beyond one threshold at 50 %) and of Transients, in any mode.
2. Transients: network input or wet output; why an echo of the octave-up copy read 3.2 dB down when the copy itself
   read 0.3 dB.
3. Ducking: "wet sum" against "base's wet only" rests on 2.6 dB.
4. The Macro smoother to the sample: lead 28 to 44 samples, per-sample or per-block form.
5. Size and Decay while they move (the first seconds behind a change); Decay 8 and 20 s steps under the tone.
6. Input filter and pre-delay at other host rates and other corners; their order.
7. Predelay Synced, Return, Master, Freeze: not moved in session F.
8. The arithmetic of the displaced chunks (section 5).

## 8. Files

| file | content |
|---|---|
| `controls/common.py` | paths, loading (holdout refused), the model in linear parts, the `DX` wrapper |
| `controls/a00_origin.py`, `a01_neutral.py` | the origin checked; neutral cases with the origin alone |
| `controls/a02_late.py`, `a03_late_step.py`, `a04_look.py`, `a05_dx.py` | displaced chunks on the anchors |
| `controls/k01_predelay.py`, `k02_shell.py`, `k04_filter.py`, `k09_ducking.py` | routing nulls (`*.json`, `*.log`) |
| `controls/k03_explore.py`, `k10_brightness.py` | transfer of unknown controls; Brightness by bands |
| `controls/k05_dyn_explore.py`, `k06..k08`, `k11_transients_*.py` | Ducking and Transients over time |
| `controls/s01_steps_default.py` .. `s05_size_decay_steps.py` | Macro, Size and Decay in motion |
| `controls/l01_levels.py` | section 6 |
| `controls/make_table.py` | the table of section 1 and the motion table from the stored numbers |

The scripts write their large intermediate arrays to `controls/cache/` (465 MB, removed after the run;
`s01_steps_default.py` and `s02_step_basis.py` make them again).
