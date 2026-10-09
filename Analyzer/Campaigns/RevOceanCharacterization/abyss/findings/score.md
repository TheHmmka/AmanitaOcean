# Abyss model: independent score on the holdout (session D's batch)

8 October 2026. Black box only: recorded audio of sessions D and E, the campaign's code, the modeler's module. No
session was opened, no capture made. Nothing was fitted: the module `model_structural/abyss_model.py` was imported
and called. Scripts, logs and numbers are in `analysis2/score/`.

## 1. Result

1. **In sample (session E): the modeler's numbers are right.** All 32 recordings of the common sequence re-scored
   with my own script; every null, base-alone null and layer null agrees to 0.00 dB.
2. **Holdout with the oscillator origin alone (`SESSION_D_STRICT`): fails.** 11 of the 14 recordings with Macro
   above 0 null at -0.7 to -19.4 dB (base alone: -2.5 to -4.2 dB). The other three null at -65.0 to -77.8 dB.
3. **Holdout with the origin plus the modeler's late-chunk rule (`SESSION_D`): passes at session E's level.** The
   same 14 recordings: -56.3 to -77.8 dB, median -65.1 dB (session E on the same cases: -59.4 to -68.0 dB,
   median -63.9 dB). Macro 0: -110.1 and -115.9 dB.
4. **Determinism: no sign of randomness in any voice.** What separates D from E is not a per-session anchor and
   is not tied to the mode switch. It is a pattern in processed time: some chunk boundaries sit exactly one
   single-precision step of the time late, and the pattern changes at 1024 s and at 2048 s. An exact replica
   looks possible. It is not there yet for two reasons that are the same in both sessions: the late-chunk
   arithmetic is known only as a two-entry table, and the model stops at about -60 dB (section 6).

The caveat that matters: sessions D and E never overlap in processed time while in Abyss (D 730 to 3620 s, E
11 290 to 14 050 s). "The instances agree" is measured directly only where the E-fitted law needs nothing extra:
D's probes from 1030 to 2040 s and the holdout recordings in which no late chunk holds sound. For the late chunks
themselves it is inferred from the form of the pattern.

## 2. What was scored, and with what

`score/holdout.py D` renders every case of D's batch from its own stimulus file, with warm-up =
`firstFrame / 48000` and Decay, Size and Macro taken from `info.json` (normalised host values turned back into
display values and checked against `revocean.normalised`; every other control verified to be at the baseline). The
null is `revocean.null_db` over the whole recording, both channels.

| Context | Content | Where it comes from |
|---|---|---|
| base | Macro forced to 0, origin 31 574 180 | floor to beat |
| strict | `SESSION_D_STRICT`: origin 31 574 180 | the origin was given. I checked it on D's probes, not on the batch: the base model on 0.50 to 0.66 s of probes 25, 40, 60, 80, 99 nulls at -114 to -121 dB at this origin, -60 dB one sample to either side, -27 dB 44 samples off, +1.4 dB with origin 0 (`origin_D.log`) |
| rule | `SESSION_D`: origin plus `late_rule = "block_grid"` | the modeler read the rule from D's probes. I checked it on the probes myself (section 5). Host block grid: 512 frames from the first frame of each recorded step, the module's default; the session host's source confirms that every step starts its own grid |

Nothing else per session, per recording or per voice.

## 3. Was the holdout opened by the modeler?

- **No script loads it.** Every `np.load` and every `session_` path in `model_structural/` and `model_kernel/` was
  read. From `session_D/` they open only `probe.stimulus.npy` and `pNNN.npy`.
- **Not checked:** the modeler's cache files lie in a scratch folder outside the repository, which I may not
  read. By the code that writes them they are built from `session_E/` only.
- **The holdout was not blind in one point.** `analysis2/clock.verification.md`, written before the batch was
  locked, lists the late unison copies of D's c02 by chunk index (4571 + 6 n) and by size (2^-12 s). The modeler
  cites this as a check of the rule. So the rule's entry for 2048 to 4096 s was not tested blind on the unison
  voice of c02. The octave-down voice and the other recordings were blind as far as the files show.
- The structure and several constants (comb gain, unison gain, windows) come from earlier reports that analysed
  D's batch. That is disclosed in the reports and does not touch the determinism question.

Two more checks of the artefact: at Macro 0 with origin 0 the module equals `network_model.render` bit for bit,
and a fresh build of `abyss_core.c` renders a holdout case identically to the shipped library (`selfcheck.log`).

## 4. Numbers

### 4.1 Session E, in sample (`score/scores_E.json`, `insample_E.log`)

All 32 recordings, context `SESSION_E`. Largest difference from `model_structural/scores_E.json`: 0.00 dB.

| Recording | Mine | Modeler |
|---|---|---|
| c01, c16, x05 step 0 % (Macro 0) | -106.99, -116.48, -122.41 | the same |
| c02 to c07 | -60.40, -67.15, -67.14, -60.25, -67.82, -67.96 | the same |
| c08, c09, c10 (Macro 50, 25, 75 %) | -63.57, -64.24, -63.53 | the same |
| c11 to c14 | -59.37, -60.49, -62.63, -67.64 | the same |
| x02, x03, x04, x06 | -61.51, -61.30, -67.53, -66.05 | the same |
| x05 steps 10 to 100 % | -90.81, -82.86, -74.75, -69.83, -72.17, -72.28, -70.37, -69.60, -68.89, -67.27 | the same |
| c15 burst; silence whole; silence 3 to 20 s | -66.11; -13.69; -58.34 | the same |

### 4.2 Session D, holdout (`score/scores_D.json`, `holdout_D.log`)

Null in dB. "Layer" is model minus base against recording minus base. "Late" is the number of chunks the rule
marks late inside the recording (unison / octave down; the octave-up voice has none anywhere).

| Case | Macro | Base alone | Strict | Rule | Layer, rule | Late | Session E, same case |
|---|---|---|---|---|---|---|---|
| c01 train, Decay 0.5 s | 0 | -110.06 | -110.06 | -110.06 | | | -106.99 |
| c16 programme | 0 | -115.94 | -115.94 | -115.94 | | | -116.48 |
| c02 train | 100 % | -3.87 | -13.46 | **-62.28** | -58.41 | 6 / 0 | -60.40 |
| c03 three impulses | 100 % | -2.69 | -8.82 | **-57.76** | -55.07 | 7 / 7 | -67.15 |
| c04 programme | 100 % | -2.49 | -0.65 | **-65.32** | -62.82 | 7 / 7 | -67.14 |
| c05 noise | 100 % | -3.73 | -7.95 | **-60.32** | -56.60 | 7 / 7 | -60.25 |
| c06 1 kHz | 100 % | -3.14 | -5.78 | **-68.04** | -64.90 | 4 / 4 | -67.82 |
| c07 220 Hz | 100 % | -3.43 | -15.75 | **-67.21** | -63.78 | 4 / 4 | -67.96 |
| c08 three impulses | 50 % | -4.20 | -65.74 | **-65.74** | -61.54 | 7 / 7 | -63.57 |
| c09 three impulses | 25 % | -6.01 | -77.79 | **-77.79** | -71.78 | 7 / 7 | -64.24 |
| c10 three impulses | 75 % | -4.23 | -19.39 | **-68.22** | -63.99 | 7 / 7 | -63.53 |
| c11 three impulses again | 100 % | -2.62 | -7.10 | **-66.11** | -63.48 | 7 / 7 | -59.37 |
| c12 Size 60 % | 100 % | -4.12 | -12.05 | **-60.15** | -56.03 | 7 / 7 | -60.49 |
| c13 Size 150 % | 100 % | -3.26 | -12.50 | **-64.23** | -60.96 | 7 / 7 | -62.63 |
| c14 Decay 8 s | 100 % | -2.49 | -6.18 | **-56.29** | -53.80 | 7 / 7 | -67.64 |
| c15 burst | 100 % | -5.94 | -64.97 | **-64.97** | -59.03 | 0 / 0 | -66.11 |
| c15 silence, whole | 100 % | -0.42 | -13.84 | -13.84 | | 10 / 0 | -13.69 |
| c15 silence, 3 to 20 s | | 0.00 | -58.13 | -58.13 | | | -58.34 |

- The campaign's own model (oscillators counted from the first frame) scores +2.91 and +2.92 dB on c01 and c16.
  The origin is needed, and with it Macro 0 is at the base's own level.
- Strict equals rule in c08, c09 and the c15 burst because no late chunk holds sound there. In the
  three-impulse stimulus the only event a late chunk touches is the octave-down copy of the right impulse, and
  that voice is off at Macro 25 and 50 %.
- In c04 the strict model is worse than no layer at all (-0.65 against -2.49 dB).
- The c15 silence is not a fair whole-recording test in either session: the reference changed Decay at the start
  of the silence and the module has one Decay per render. Level per second follows the recording within 0.1 dB
  from -45 dB down to -590 dB in both sessions.
- Rule minus session E, case by case: from -13.6 dB (c09, D better) to +11.4 dB (c14, D worse); mean -0.4 dB.

## 5. Checks of the per-session element

**The grain phasors hang on the oscillator origin** (`phasor_D.log`). The module restarts them at origin - 2. Moving
that restart alone, on two holdout cases:

| Restart at origin + | -2 (module) | -3 | -1 | -46 | +42 | counted from the first frame |
|---|---|---|---|---|---|---|
| c03, Macro 100 % | -57.76 | -8.47 | -8.03 | -1.47 | -1.38 | -2.39 |
| c08, Macro 50 % | -65.74 | -9.90 | -11.27 | -8.08 | -8.56 | -7.84 |

The octave voices of session D need no anchor of their own.

**The lateness is exactly one single-precision step of the time** (`ulp_D.log`). Scaling the step, with the rule's
chunk lists unchanged:

| Step | 0 | 0.5 | 0.9 | 0.99 | 0.999 | **1** | 1.001 | 1.01 | 1.1 | 2 |
|---|---|---|---|---|---|---|---|---|---|---|
| c10 | -19.39 | -19.16 | -20.22 | -39.66 | -59.27 | **-68.22** | -58.90 | -39.62 | -20.22 | -19.65 |
| c06 | -5.78 | -8.57 | -22.42 | -48.16 | -65.73 | **-68.04** | -64.45 | -47.90 | -22.40 | -5.78 |
| c02 | -13.46 | -13.28 | -12.71 | -33.56 | -52.98 | **-62.28** | -53.07 | -33.57 | -13.21 | -12.86 |

**Both voices are late, independently** (`which_voice_D.log`):

| Case | none | unison only | octave down only | both |
|---|---|---|---|---|
| c02 | -13.46 | -62.28 | -13.46 | -62.28 |
| c06 | -5.78 | -7.12 | -11.54 | -68.04 |
| c05 | -7.95 | -9.91 | -12.37 | -60.32 |
| c04 | -0.65 | -2.60 | -5.07 | -65.32 |

**The rule on D's probes** (not holdout). Saved audio, nine probes re-scored (`probes.log`): 760 and 910 s strict
-13.4 and -8.6 dB, rule -67.8 and -67.2 dB; 1210, 1360, 1810, 1960 s strict = rule = -67.3 to -67.5 dB; 2110,
2260, 2980 s strict -3.2 to -8.9 dB, rule -67.0 to -67.7 dB. These agree with the modeler's logs.

The info file holds ten descriptors (0.1 dB each) of every probe, saved or not. Computed from the model
(`descriptors.py`, `descriptors.log`):

| | Probes whose ten descriptors all agree within 0.1 dB |
|---|---|
| Session D, rule | **76 of 76** (probes 24 to 99, 60 of them never saved) |
| Session D, strict | 35 of 76: probes 33 to 67 (1000 to 2040 s) |
| Session E | 24 of 24 (probes 376 to 399) |

**Where the pattern changes** (`border.log`). Forcing each of the rule's two entries on the probes around the two
borders: the 512-to-1024 s pattern is needed up to the probe at 970 to 990 s and wrong from the probe at 1030 s
(the probe at 1000 to 1020 s decides weakly, 0.1 against 0.5 dB summed over the descriptors). The 2048-to-4096 s
pattern is wrong at 2020 to 2040 s and needed from 2050 s. So the changes lie in 990 to 1030 s and in 2040 to
2050 s. 1024 s and 2048 s, where the single-precision spacing of a time in seconds doubles, are inside both.

**The rule without the step start** (`rule_restated.py`, `rule_restated.log`). The module words the rule as "host
block number 62 or 312 modulo 375, counted from the start of the step". A plug-in cannot know where a step
began. On all 93 recorded steps of session D (probes 24 to 99 and the batch) the rule marks the same chunks as:

    a boundary b is late when it lies in the middle of a host block and
        512 s <= b < 1024 s  and  b = 2/3 s modulo 2 s,   or
       2048 s <= b < 4096 s  and  b = 4/3 s modulo 2 s.

That is a function of processed time and of where the host's block starts, which the plug-in sees.

## 6. Determinism, voice by voice

Model components on the holdout (`voices.log`): each voice rendered alone through the network, one least-squares
gain per voice fitted to the recording (1 = predicted), and the whole remainder expressed under each voice's own
energy (an upper bound of that voice's error). Context "rule" for D.

| Case | Voice | D: gain | D: remainder under the voice | E: gain | E: remainder under the voice |
|---|---|---|---|---|---|
| c09, Macro 25 % | unison | 1.00007 | -71.6 dB | 0.99943 | -57.6 dB |
| | octave up (gain 0.33) | 0.99975 | -57.5 dB | 0.99940 | -44.9 dB |
| c08, Macro 50 % | unison | 0.99994 | -60.6 dB | 1.00049 | -57.7 dB |
| | octave up | 1.00009 | -54.5 dB | 1.00011 | -52.7 dB |
| c10, Macro 75 % | octave down (gain 0.37) | 1.00050 | -54.1 dB | 1.00107 | -47.5 dB |
| c03, c11, c14, c02, c05, Macro 100 % | unison | 0.99901 to 1.00017 | -48.7 to -59.0 dB | 0.99990 to 1.00024 | -52.1 to -60.2 dB |
| | octave up | 0.99988 to 1.00015 | -43.7 to -53.3 dB | 0.99993 to 1.00021 | -46.5 to -55.1 dB |
| | octave down | 0.99856 to 1.00092 | -51.5 to -60.9 dB | 0.99931 to 0.99998 | -52.6 to -63.4 dB |

- **Unison**: predicted in D. Its clock counts from the first frame; no per-session element. Late chunks in the
  two time ranges of section 5.
- **Octave up**: predicted in D with the origin alone (c08 -65.7 dB, the ring-out tail -58.1 dB, levels to
  -590 dB). Its phasors restart with the oscillators. Its 1 s boundaries were never late; they never met the
  rule's condition either (see problems).
- **Octave down**: predicted in D. One phasor, restarted with the oscillators; chunk clock from the first frame;
  late chunks as for the unison voice.

**"The model is still crude" against "the instance differs".**

- D's two worst recordings with the rule, c03 (-57.8 dB) and c14 (-56.3 dB), are about 10 dB under their session E
  twins. `detail_D_rule.log`, `detail_E.log` and `gain_delay.log` place the remainder in the copies of the three
  impulses and say what it is:

  | Window | D c03 | D c14 | D c11 | E c03 | E c14 | E c11 |
  |---|---|---|---|---|---|---|
  | 8.5 to 9.0 s right: octave-down copy of the right impulse (in D it is in a late chunk) | -50.4 | -50.9 | -66.4 | -57.3 | -62.1 | **-49.1** |
  | 1.0 to 1.5 s left: copies of the left impulse | -56.9 | -55.5 | -61.5 | -70.2 | -67.4 | -66.6 |
  | 13.0 to 13.5 s left: copies of the double impulse | -55.6 | -54.0 | -65.2 | -74.4 | -66.6 | -69.1 |

  (remainder in dB of the layer in the window). A gain and a delay fitted per window give gain errors of 0.01 to
  0.24 % and delay errors of at most 0.003 sample, in both sessions. Timing is right; what is left is the shape
  and size of single copies.
- The octave-down copy of the right impulse, the largest single item, spreads over the same range in both
  sessions (-49 to -66 dB) and is no worse where the chunk is late. Its read position is about 47 800 samples into
  the chunk, where the modeler reports -48 to -53 dB for this voice (single-precision read position, not modelled).
- The copies of the left and the double impulse are 5 to 15 dB worse in D's three recordings than in E's three
  (gain errors 0.03 to 0.17 % against 0.01 to 0.03 %). Three recordings a side cannot separate chance from a small
  systematic effect here. Against a systematic effect: session E's own impulse maps, 223 impulses at every position
  of the chunks, have a layer null of -57.5 and -57.2 dB, which is where D's copies sit; and D's c09, the same
  copies of the unison voice at Macro 25 %, is the best recording of either session (-77.8 dB).
- Session E has cases that are worse than their D twins by as much (c09 by 13.6 dB, c11 by 6.7 dB).
- So the shortfall on the holdout is the model's as far as these numbers can tell, at the level session E shows
  in sample. The only thing clearly good in E and bad in D is the strict context, and that is the late chunks.

**What kind of thing the late chunks are.** Not random: exactly one single-precision step, at boundaries that
repeat every 4 s from 730 to 1024 s and from 2048 to 3620 s, on every one of D's 76 Abyss probes and on all 11
holdout recordings in which a late chunk holds sound, with the pattern changing at the two powers of two. Not an
anchor: nothing is chosen per session. Not the switch: D switched at 716 s and the pattern changes at 1024 s and
2048 s. It reads as single-precision arithmetic on a time the host gives per block. I tried simple readings by
hand (rounding or truncation of the block's start time, rounding of the boundary); none covers both ranges of D
and the absence in 1024 to 2048 s. The arithmetic stays unknown.

## 7. Problems

1. **The model as fitted on session E alone does not predict D's batch**: 11 of 14 recordings at -0.7 to
   -19.4 dB. The modeler's report says so in advance ("roughly -3 to -13 dB"); three recordings do null
   (c08, c09, c15 burst), which it did not say.
2. **The late-chunk rule is a two-entry table from the holdout's own session.** The batch repeats the probes'
   geometry: every step starts on a whole even second, so boundaries fall only at 0 or 1/2 of a host block (unison,
   octave down) and at whole seconds (octave up). The holdout tests that the rule persists for another 600 s and
   holds for other signals, Macro, Decay and Size. It does not test another block size, another alignment,
   4096 to 8192 s, or another instance in the same time range.
3. **"It hits all three voices" is not shown.** No 1 s boundary meets the rule's condition in any recorded step.
   What the data shows is that whole-second boundaries are never late at 0, 1/4, 1/2 and 3/4 of a host block.
4. **One entry was not blind** (section 3): the late unison chunks of D's c02 were in an earlier report.
5. **Floor of -56 to -68 dB at full Macro in both sessions**, against -107 to -122 dB for the base. Voice-level
   determinism is shown; a bit-level replica is not.
6. **The ring-out's silence cannot be scored whole** (one Decay per render): -13.8 dB in D, -13.7 dB in E.
7. **A recording of two steps gets the rule for its last step only**: with `preceding`, the module counts the
   block grid from the stimulus and marks nothing in the audio before it. Harmless in c15 (no late boundary
   inside the burst), but an interface limit.
8. **No holdout recording isolates the unison voice.** By the model's own Macro law the octave-up voice is
   already on at 25 % (gain 0.33); only below 14.6 % is the unison voice alone. The voice-by-voice reading above
   therefore uses model components, not Macro alone.
9. **D and E do not overlap in processed time in Abyss.** See section 1.

## 8. What would settle it

- One session switched to Abyss early, with the probe sequence from about 500 s to past 4100 s: compared with
  session D range by range it shows directly whether two instances agree where chunks are late, and it adds
  4096 to 8192 s.
- The modeler's click scan at host blocks of 64, 512 and 1024 frames from steps that start off a whole second:
  the only way to learn what "middle of a host block" really is.
- The noise-burst scan for the single-precision read position, which is what holds both sessions at -60 dB.

## 9. Files

| File | Content |
|---|---|
| `score/holdout.py` | `E`, `D`, `origin`, `phasor`, `ulp`, `which`, `probes`, `voices`, `selfcheck` |
| `score/detail.py` | remainder per 0.25 s window against the layer and each voice |
| `score/rule_restated.py`, `border.py`, `descriptors.py` | the rule without the step start; where the pattern changes; descriptors of every probe |
| `score/gain_delay.py` | gain and delay error of single copies, both sessions |
| `score/scores_D.json`, `scores_E.json`, `voices.json`, `descriptors.json` | numbers |
| `score/*.log` | output of each command as run |
| `score/abyss_core_check.dylib` | the fresh build used by `selfcheck` |
