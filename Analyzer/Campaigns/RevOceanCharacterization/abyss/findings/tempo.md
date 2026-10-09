# Abyss: tempo, play head and a stopped transport

9 October 2026. Black box only: recorded audio of sessions D, E, F (120 BPM), T90 (90 BPM, play head 0.25 s ahead,
playing) and S90 (90 BPM, transport stopped), the campaign's code and the session host's own source. No session was
opened, no capture made. Never loaded: `session_F/h1*, h2*, h3*` (locked holdout), parts K and S4 of session F.
Everything written is in `analysis3/tempo/` (scripts, logs, the extended model in `tempo/model/`), this report, and
`analysis3/session_F_origin.json`.

Marks: **M** measured (a number read from recordings, with its tolerance), **F** fitted (a constant that minimised a
residual), **G** guessed (assumed, not tested on its own).
`PY=/Users/nespesha/Workspace/AmanitaSaturator/Plugin/.venv-tools/bin/python`; commands run inside `analysis3/tempo/`.

## 1. Answers in short

1. **Tempo.** The three chunk lengths are note values: 4/3, 2 and 8/3 quarter notes, held as single-precision
   numbers of quarter notes (`0x3FAAAAAB`, `2.0`, `0x402AAAAB`). With them scale the chunk windows (they are fractions
   of the chunk) and the octave-up voice's recirculation delay (one chunk of its reader, 2 quarter notes). Fixed: the
   grain readers (period, sweep, shortest delay, speed), the three voice gains, the recirculation gain (0.198 per
   pass), the 44-sample block. The "tick of 1/1024 s" is not a constant of the plug-in at all (section 3.4).
2. **Play head.** The chunk clock reads the host's position **in quarter notes, once per host block, in single
   precision**, and adds, also in single precision, the quarter notes from the block start to each of its converter
   blocks of 48 host frames. One line of arithmetic (section 3.1) reproduces all 537 mirror points measured in four
   sessions to 0.01 sample (rms 0.0003 to 0.0011), **and it yields the "late chunks" by itself**: session D's batch
   scores -56.3 to -77.8 dB with no table (the same numbers as with the table of `analysis2`), session F's probes
   and batch likewise, T90's eight block alignments -66.1 to -66.7 dB. Session E has no late chunk because at
   16 384 to 32 768 quarter notes the single-precision step of the position is the grid itself.
3. **Stopped transport.** The layer ignores the frozen position and counts internal samples itself, at the host's
   tempo, **from zero at the mode switch** (the oscillator origin, to the sample), exactly per sample: no host
   blocks, no single-precision steps in the boundaries. A drift of at most 0.025 sample appears behind 512 quarter
   notes (fitted, not explained). Levels, windows and gains are those of a running transport.
4. **Model.** `tempo/model/abyss_model.py` (`context["clock"] = "host"`, keys in section 6). Without that key it is
   the module of `analysis2` bit for bit. Whole-recording nulls: T90 -59.5 to -66.7 dB, S90 -61.9 to -66.5 dB up to
   296 s and for the end probe, -31.1 dB on its 486 s scan; D, E and F at the level of `analysis2` (table in
   section 7).

## 2. Method

**Oscillator origins** (`t00_origin.py`: the base model against the first impulse of a Macro 0 train, every multiple
of 44 in a window of processed time; then the whole recording). **M**

| Session | Origin (at-lines internal sample) | Processed time | First impulse | One sample off | Whole Macro 0 train | Scan |
|---|---|---|---|---|---|---|
| F | 1 323 432 | 30.0098 s (470.2 host frames behind the pause at 1 440 000) | -108.95 dB | -49.0 / -49.3 dB | -108.82 dB | 2004 origins, 29.5 to 31.5 s; next best -16.7 dB |
| T90 | 2 286 108 | 51.8392 s | -129.31 dB | | -110.94 dB | 100 127 origins, 30 to 129.9 s; next best -67.8 dB |
| S90 | 1 147 036 | 26.0099 s | -128.37 dB | | -105.45 dB | 100 027 origins, 0.1 to 99.9 s; next best -68.6 dB |

`analysis3/session_F_origin.json` did not exist and was written. Tempo and play head do not touch the base: T90 and
S90 null at -110.9 and -105.5 dB at Macro 0 with the converters on the frame count.

**Mirror points by nulling** (`measure.py`, `t01_map.py`; the method of `analysis2/clock.verification.md`, rewritten
for any chunk length): the layer behind an isolated impulse is fitted with the base's answer to the mirrored internal
pulse at three neighbouring integers; X = "output index + read index" of the reversed read, to about 0.001 sample.
537 copies with a three-tap null under -40 dB:

| Recording | Tempo, play head | Processed time | Copies |
|---|---|---|---|
| E x02, x03 | 120, frame count | 11 290 to 13 590 s | 126 + 54 |
| F s3, s5 (impulse maps) | 120, frame count | 4784 to 5376 s, 6404 to 6996 s | 121 + 130 |
| T90 t04 (scan), t02 (train) | 90, 0.25 s ahead | 342 to 828 s, 164 to 188 s | 95 + 11 |
| S90 t04 | 90, stopped | 312 to 798 s | 101 (section 5) |

Commands of this section:

    $PY t00_origin.py F c01_macro0_decay0p5_train 29.5 31.5      ($PY t00_origin.py F c01_macro0_decay0p5_train check 1323432)
    $PY t00_origin.py T90 t01_macro0_decay0p5_train 30 129.9      (6.5 minutes; S90: 0.1 99.9)
    $PY t01_map.py E x02_impulse_map_left_macro100_decay0p5 497448424 29400.000876188 0      (x03 likewise)
    $PY t01_map.py F s3_impulse_map_right_macro100_decay0p5 1323432 29400.000876188 0        (s5 likewise)
    $PY t01_map.py T90 t04_scan_macro100_decay0p5 2286108 39200 11025
    $PY t01_map.py T90 t02_macro100_decay0p5_train 2286108 39200 11025 0.5 train
    $PY t01_map.py S90 t04_scan_macro100_decay0p5 1147036 39200.0011683 -1147036
    $PY t08_sched.py        every copy against model/hostclock.py

## 3. The play-head law (question 2)

### 3.1 The arithmetic

Per host block the plug-in takes the position in quarter notes as a single-precision number. Its converter consumes
the host's frames in blocks of 48 (1 ms at 48 kHz, the reported latency) that run through from the first processed
frame, across the host's block edges. Each such block ("tick") gets a position

    p0    = fl32( ppq at the start of the host block in which the tick's first frame arrived )
    stamp = fl32( p0 + fl32( d * bpm / (60 * rate) ) )          d = frames from that host block's start to the tick

A reversed reader with chunk length L quarter notes (single precision) has its boundaries at `k * L`, exactly (what
`fmodf(stamp, L)` gives). With `A = (k L - stamp) * 60 / bpm * 44100` internal samples:

- the chunk k starts in the tick in which `0 <= A < 43` (the boundary lies inside the 44-sample block, with one
  sample behind it);
- a boundary that was not caught that way is taken in the next tick with `A <= 0`;
- the tick with number m (processed frame 48 m) starts the chunk in the first 44-sample block at or after
  `44.1 m - a`: `s = 44 ceil((44.1 m - a) / 44)`, a = 42.2 (**F**, window 42.10 to 42.35), and

      X = 2 s - 2 + A + I        I = floor(A), or -1 for A < 0        same pitch, octave down
                                 I = ceil(A)                          octave up (I = 0 for A < 0 is a guess)

  `y[m] = w(m - X/2) * lin(u, X - m)` from `m = ceil(X/2)`, as before.

`tempo/model/hostclock.py` is this (and the stopped clock of section 5), 160 lines; `chunk_schedule` returns k, the tick, A, I, s and X of every chunk.

### 3.2 Evidence

`$PY t19_variants.py` (`logs/t19_variants.log`): copies within 0.01 sample of the schedule, of 537.

| Arithmetic of the stamp | Copies | Fails on |
|---|---|---|
| **as above** | **537** | |
| p0 of the host block in which the tick ends | 491 | all 46 late copies of F s3, s5 |
| fl32(exact position of the tick) | 477 | T90 (14), F (46) |
| the same in seconds, times bpm/60 | 451 | T90: 3 of 11 and 17 of 95 |
| fl32(p0) + d in double precision | 32 | |
| double precision throughout | 138 | |

Residual of the 537: rms 0.0003 to 0.0011 sample per recording (`logs/t08_sched.log`). So: **quarter notes, not
seconds** (T90 decides; at 120 BPM the two are the same binary numbers); **once per host block**, not interpolated
exactly; **single precision twice** (the block's position, then the sum).

- Chunk length: `fl32(4/3)` quarter notes, product with 60/bpm in higher precision: 39 200.001168 samples at
  90 BPM. Excluded on T90 t04: exact 4/3 (0 of 95), `fl32(fl32(4/3) * fl32(60/bpm))`, `fl32(4/3 * 60/bpm)` seconds
  (`$PY t02_law.py T90_t04 11025 90`). `fl32(8000/9)` ms would move the offsets by 0.026 sample at chunk 927 and
  0.42 sample in session E: excluded. **M**
- Inside the host block the position is exact to the sample: with the play head 0.25 s (11 025 internal samples,
  not a multiple of 44) ahead, the block constant a has to lie in 42.10 to 43.00 on T90 and 41.30 to 42.35 on E
  and F; the windows overlap by 0.25 sample (`$PY t05_ms.py T90_t04 11025 90`, `E_x02 0 120`, `F_s3_ 0 120`). **M**
- Eight alignments of the host block grid (T90 t03, grid moved by 64 frames per step): -66.06 to -66.65 dB. **M**
- Octave up takes `ceil(A)`: at 120 BPM its boundaries are ticks (A = 0) and the two forms are the same; at 90 BPM
  A is 14.36 or 28.71 samples for two chunks in three, and those copies null at -21 to -28 dB with X one sample
  larger and at 0 to -7 dB without (`$PY t15_upx.py T90 24 3`, `40 6`, `66 4`). **M** (only with A > 0)
- The catch window 43: started chunks reach A = 39.75 (F); the octave-up reader at 120 BPM has A = 43.066 in the tick
  before each of its boundaries and does not start there (it starts with A = 0 in the next). Anything from 39.8 to
  43.06 fits; 43 is the reading "one sample of the block behind the boundary". **G** inside **M** bounds.

### 3.3 The late chunks, derived

A tick's exact position is rarely a single-precision number. Rounded twice it lands on the neighbouring value above
or below, and the offset A moves by one single-precision step of the position, in real time. That is the "late
chunk": nothing is late, a stamp is one step low.

| Where | What the arithmetic gives | Check |
|---|---|---|
| D, 512 to 1024 s and 2048 to 4096 s | the chunks of the table of `analysis2`, with X + 5.692 and X + 20.767 (`$PY t10_cmp.py 760`, `2110`) | D probes 25, 30, 70, 99: -67.8, -67.2, -67.0, -67.7 dB (old law without table -13.4, -8.6, -3.2, -8.9); batch: section 7 |
| D, 1024 to 2048 s | none on the positions the jobs reach | probe 40: -67.31 dB, identical to the old law |
| E, 11 290 to 14 050 s | none: the step of the position is 2^-9 quarter note = 1/1024 s, and the tick positions lie 0 to 0.024 step under a multiple of it, so no rounding can go one step low | E: every score identical to `analysis2` to 0.01 dB |
| F, 40 to 512 s (never recorded in Abyss before) | steps of 0.17 to 1.35 samples; stamps one step low up to 256 s, none on these jobs from 256 to 512 s | probes 1 to 8 (40 to 250 s) fail the old law (-12.7 to -18.5 dB), probes 9 to 16 pass it; host clock -67.3 to -67.6 dB on all 16 |
| F, 4096 to 8192 s, behind 8192 s | every anchor train fails the old law (-6 to -8 dB) up to 7452 s; at 8248 s (step 1/1024 s, as in E) old law and host clock are the same | host clock -58.2 to -62.0 dB on all five |
| F, 4096 to 8192 s (step 21.5 samples, half a tick) | the stamp one step low pushes A over 43: the chunk is taken in the next tick with A = o - 43.066 and I = -1. One chunk in six of the same-pitch reader (148 of 892 in s3) | 46 such copies measured in s3 and s5: all within 0.01 sample |
| T90 t02, 164 to 188 s | four of eleven copies one step low: 0.449 sample under 256 quarter notes, 0.897 above (the step of quarter notes; a step of seconds would be 0.673) | all eleven within 0.002 sample |

### 3.4 What the "1/1024 s tick" was

At 120 BPM a millisecond is 1.024 * 2^-9 quarter note and the chunks are 2/3, 1 and 4/3 s. The ticks that hold a
boundary therefore lie 0, 0.008 or 0.016 of 2^-9 quarter note under a multiple of 2^-9 quarter note (= 1/1024 s),
and single precision rounds them onto it wherever its step divides that grid (up to 32 768 quarter notes). The old
law `o = b mod 1/1024 s` is that rounding. It is not general: at 90 BPM the same arithmetic gives nine classes,
`o = frac(k * 888.889) / 1024 s` (95 of 95 copies; the old law with its tick fixed in seconds fits 24, scaled with
the tempo 31; `$PY t02_law.py T90_t04 11025 90`). **There is no tick constant to scale.** The grid that exists is the
converter block of 48 host frames.

## 4. What follows the tempo (question 1)

T90 against the model, `$PY t11_t90.py t02` (`logs/t11_t90_t02.log`; whole recording, 24 s, impulse train, Macro 100 %):

| Variant | Null |
|---|---|
| windows and comb delay follow the tempo, grain readers and gains fixed | **-60.25 dB** (E c02: -60.40, D c02: -62.28) |
| comb delay fixed at 44 100 samples | -23.22 dB |
| windows fixed in samples | -10.35 dB |
| grain readers stretched with the tempo (period and sweep x 120/bpm) | -3.92 dB |
| comb gain 0.198 per second instead of per pass (0.1154), or the other way (0.2968) | -33.27, -31.06 dB |
| base alone | -4.44 dB |

Least-squares gains of the three voices on T90 (1 = the 120 BPM value; `$PY t12_voices.py T90 t04`, `t05`): same
pitch 1.0000, octave up 1.0000, octave down 0.9999 to 1.0000.

| Constant of `ABYSS_STATE.md` | With the tempo | Exact form | Mark |
|---|---|---|---|
| Chunk lengths | **scale** | 4/3, 2, 8/3 quarter notes as single-precision quarter notes: 1.3333333730697632, 2, 2.6666667461395264. In samples: note * 60 / bpm * 44100 (29 400.000876 / 44 100 / 58 800.001752 at 120; 39 200.001168 / 58 800 / 78 400.002337 at 90) | M same pitch (537 copies); M by null for the other two (-60 dB over 486 s; octave up 58 800 exactly on S90) |
| Tick G = 1/1024 s | **does not exist** | section 3.4. The real grid: 48 host frames, fixed in frames | M |
| 44-sample block, offset a | fixed | 44 internal samples; a = 42.2 | M / F |
| Mirror law | replaced | section 3.1 | M |
| Chunk windows (fades) | **scale**: fractions of the chunk | fade in / fade out / end: same pitch 0.20532 / 0.07060 / 1.000002; octave up 0.28448 / 0.13139 / 0.999999; octave down 0.37875 / 0.15317 / 0.999967 (the sample values of `analysis2` over 29 400, 44 100, 58 800) | M that they scale (-60.3 against -10.4 dB; one same-pitch copy inside the fade-in, gain 0.5458 at 5596 samples behind the mirror point, gives a fade of 8050 samples at 90 BPM; 6036.5 * 4/3 = 8048.7); the numbers stay F |
| Recirculation delay of octave up | **scales** | one chunk of that reader: 2 quarter notes (58 800 samples at 90 BPM), not one second | M |
| Recirculation gain | fixed | 0.198 per pass | M |
| Grain readers: increment, W, dmin | fixed | the values of `analysis2`: grain period and read speed do not follow the tempo | M |
| Grain phasors' start | fixed | 0.5 at origin - 2, as before (the octave voices null with it in T90 and S90) | M |
| Voice gains at full Macro | fixed | 0.6927952, 0.487907, 0.707228 | M (gains 0.9999 to 1.0000) |
| Macro law (ramps, smoothing) | unknown | T90 and S90 hold Macro 0 and 100 % only | not measured |
| Comb ring-out per second | follows | 0.198 per 2 quarter notes: 14.07 dB/s at 120, 10.55 dB/s at 90 | consequence |

One tempo was recorded. "Scales" means: equal to the 120 BPM value times 120/bpm at 90 BPM, and the natural form
(a note value) is assumed for other tempi. **G** beyond 90 and 120.

## 5. Stopped transport (question 3)

S90: `isPlaying` false, position frozen at 0.25 s, 90 BPM. `$PY t01_map.py S90 t04_scan_macro100_decay0p5 1147036
39200.0011683 -1147036`, `$PY t16_s90.py`, `$PY t20_s90drift.py`.

- **Anchor.** The mirror points lie at `X = 2 floor(b) - 2 + frac(b)`, `b = 1 147 036 + k * 39 200.00117`: the
  position is zero at network-input sample 1 147 036, which is the oscillator origin of S90 found independently on
  the base (a multiple of 44; the mode switch). With that zero all 101 copies lie within 0.03 sample (41 within
  0.01; the rest is the drift below); with the zero one sample to either side, none within 1.9. The frozen
  position (0.25 s) and the first processed frame play no part. **M**
- **No host grid.** The form above is the law of section 3.1 with every 44-sample block stamped by its own exact
  sample count: `s = 44 floor(b / 44)`, `A = b - s`. No 48-frame ticks, no single-precision step in the boundary:
  at 430 to 1150 quarter notes a step would be 0.9 to 3.6 samples, the copies scatter by 0.001. **M**
- **Tempo.** The host's 90 BPM is used (chunk 39 200; with 120 BPM it would be 29 400). **M**
- **Chunk lengths.** The same single-precision note values. Octave up: 58 800 exactly (with 58 800.0018, the
  millisecond form, every copy of that voice comes out one sample off: `$PY t18_s90up.py U`). Same pitch:
  39 200.00117 per chunk up to 512 quarter notes. **M**
- **A drift that is not explained.** Behind 512 quarter notes the same-pitch boundaries fall later by 4.93e-5 sample
  per quarter note (0.025 sample at 1024), behind 1024 the drift turns round (-4.9e-5 per quarter note to the last
  copy at 1150). The turns are where the single-precision step of a position doubles. The model carries it as a
  fitted curve (`hostclock.free_run_drift`), common to the three readers: it takes the end probe at 808 s from
  -19.2 to -65.6 dB, because the octave-up reader's `ceil(A)` turns on the sign of that thousandth of a sample.
  The 486 s scan stays at -31.1 dB: near 512 and 1024 quarter notes the sign is not resolved. **F**, arithmetic unknown.
- **Nothing else differs.** S90 t02 (24 s, 108 s behind the switch): -61.88 dB with the gains, windows, comb and
  readers of T90; voice gains 1.0001 / 1.0000 / 0.9987. **M**

Not recorded: a transport that starts or stops under signal, a position that jumps or loops, a mode chosen while
stopped and then a start. What the stopped clock did before the mode switch is not visible (the mode was Tide).
That a second switch would set the position to zero again is **G**.

## 6. The model

`tempo/model/abyss_model.py` is the copy of `analysis2/model_structural/abyss_model.py` with the host clock added;
`hostclock.py` is new; `abyss_core.c` and `base.py` gained a table-driven start of the oscillators (same samples,
no walk; checked bit for bit by `$PY t09_quick.py same`).

| Context key | Default | Meaning |
|---|---|---|
| `clock` | absent | `"host"`: the clock of section 3. Absent: the module of `analysis2`, bit for bit |
| `bpm` | 120 | tempo the host reports |
| `playhead_offset_seconds` | 0 | reported position minus processed time (T90: 0.25) |
| `host_block` | 512 | frames per host block |
| `steps` | [first frame of the stimulus] | absolute processed frames at which the host began a new run of blocks. The session host starts a grid with every job: a case is `[S - 480000, S - 96000, S]` (flush, pre-roll, recording) |
| `playing` | True | False: section 5 |
| `free_run_origin` | `oscillator_origin` | network-input sample at which the stopped clock is zero |
| `free_run_drift` | True | the fitted drift of section 5 |
| `constants["tempo_scales"]` | window and comb True | switches of section 4 |

Ready-made: `SESSION_T90`, `SESSION_S90`, `SESSION_F`, `SESSION_D_HOST`, `SESSION_E_HOST` (add `steps`).
What a session must supply beyond tempo and offset: the oscillator origin, and the host's block layout.

## 7. Scores (question 4)

`$PY score_tempo.py [group ...]`, `$PY score_tempo.py --table` (`scores.json`, `logs/score_*.log`).
`revocean.null_db` over whole recordings, both channels, nothing fitted. "old" = law of `analysis2` with the origin
alone; "rule" = plus its late-chunk table; "host" = this report's clock, no table. Recordings longer than 120 s are
rendered in pieces of 120 s with the 10 s before each as the layer's memory.

**Session E (in sample; must not get worse)**

| Recording | Processed time s | Length s | Macro | old | host |
|---|---|---|---|---|---|
| c01_macro0_decay0p5_train | 12010 | 24 | 0.00 | -106.99 | -106.99 |
| c02_macro100_decay0p5_train | 12044 | 24 | 1.00 | -60.40 | -60.40 |
| c03_macro100_decay2_imp3 | 12078 | 30 | 1.00 | -67.15 | -67.15 |
| c05_macro100_decay2_noise | 12158 | 30 | 1.00 | -60.25 | -60.25 |
| c06_macro100_decay2_sine1k | 12198 | 18 | 1.00 | -67.82 | -67.82 |
| c10_macro75_decay2_imp3 | 12334 | 30 | 0.75 | -63.53 | -63.53 |
| c12_macro100_decay2_size60_imp3 | 12414 | 30 | 1.00 | -60.49 | -60.49 |
| c14_macro100_decay8_imp3 | 12494 | 30 | 1.00 | -67.64 | -67.64 |
| x03_impulse_map_right_macro100_decay0p5 | 13290 | 300 | 1.00 | -61.30 | -61.30 |
| x04_tone1k_120s_macro100_decay2 | 13600 | 140 | 1.00 | -67.53 | -67.53 |

**Session D, batch (the holdout of analysis2)**

| Recording | Processed time s | Length s | Macro | old | rule | host |
|---|---|---|---|---|---|---|
| c01_macro0_decay0p5_train | 3010 | 24 | 0.00 | -110.06 | -110.06 | -110.06 |
| c02_macro100_decay0p5_train | 3044 | 24 | 1.00 | -13.46 | -62.28 | -62.28 |
| c03_macro100_decay2_imp3 | 3078 | 30 | 1.00 | -8.82 | -57.76 | -57.76 |
| c04_macro100_decay2_prog | 3118 | 30 | 1.00 | -0.65 | -65.32 | -65.32 |
| c05_macro100_decay2_noise | 3158 | 30 | 1.00 | -7.95 | -60.32 | -60.32 |
| c06_macro100_decay2_sine1k | 3198 | 18 | 1.00 | -5.78 | -68.04 | -68.04 |
| c07_macro100_decay2_sine220 | 3226 | 18 | 1.00 | -15.75 | -67.21 | -67.21 |
| c08_macro50_decay2_imp3 | 3254 | 30 | 0.50 | -65.74 | -65.74 | -65.74 |
| c09_macro25_decay2_imp3 | 3294 | 30 | 0.25 | -77.79 | -77.79 | -77.79 |
| c10_macro75_decay2_imp3 | 3334 | 30 | 0.75 | -19.39 | -68.22 | -68.22 |
| c11_macro100_decay2_imp3_again | 3374 | 30 | 1.00 | -7.10 | -66.11 | -66.11 |
| c12_macro100_decay2_size60_imp3 | 3414 | 30 | 1.00 | -12.05 | -60.15 | -60.15 |
| c13_macro100_decay2_size150_imp3 | 3454 | 30 | 1.00 | -12.50 | -64.23 | -64.23 |
| c14_macro100_decay8_imp3 | 3494 | 30 | 1.00 | -6.18 | -56.29 | -56.29 |
| c16_macro0_decay2_prog_late | 3534 | 14 | 0.00 | -115.94 | -115.94 | -115.94 |
| c15_ringout_0 | 3558 | 2 | 1.00 | -64.97 | -64.97 | -64.97 |

**Session F, part A (probes; Decay 2 s, Macro 100 %)**

| Probes | Processed time s | Count | old: range | old: probes better than -60 dB | host: range | host: median |
|---|---|---|---|---|---|---|
| p001 .. p016 | 0 .. 512 | 16 | -12.66 .. -67.59 | 8 | -67.28 .. -67.64 | -67.38 |
| p017 .. p033 | 512 .. 1024 | 17 | -8.10 .. -14.02 | 0 | -67.16 .. -67.62 | -67.32 |
| p034 .. p067 | 1024 .. 2048 | 34 | -67.24 .. -67.64 | 34 | -67.24 .. -67.64 | -67.40 |
| p068 .. p099 | 2048 .. 4096 | 32 | -3.20 .. -9.10 | 0 | -66.86 .. -67.82 | -67.45 |

**Session F, part B (session D's batch again, another instance)**

| Recording | Processed time s | Length s | Macro | old | host |
|---|---|---|---|---|---|
| c01_macro0_decay0p5_train | 3010 | 24 | 0.00 | -108.82 | -108.82 |
| c02_macro100_decay0p5_train | 3044 | 24 | 1.00 | -13.19 | -62.36 |
| c03_macro100_decay2_imp3 | 3078 | 30 | 1.00 | -9.16 | -57.77 |
| c04_macro100_decay2_prog | 3118 | 30 | 1.00 | -1.73 | -65.92 |
| c05_macro100_decay2_noise | 3158 | 30 | 1.00 | -7.99 | -60.33 |
| c06_macro100_decay2_sine1k | 3198 | 18 | 1.00 | -4.28 | -67.92 |
| c07_macro100_decay2_sine220 | 3226 | 18 | 1.00 | -16.27 | -67.51 |
| c08_macro50_decay2_imp3 | 3254 | 30 | 0.50 | -65.39 | -65.39 |
| c09_macro25_decay2_imp3 | 3294 | 30 | 0.25 | -77.96 | -77.96 |
| c10_macro75_decay2_imp3 | 3334 | 30 | 0.75 | -20.13 | -69.72 |
| c11_macro100_decay2_imp3_again | 3374 | 30 | 1.00 | -7.09 | -65.88 |
| c12_macro100_decay2_size60_imp3 | 3414 | 30 | 1.00 | -11.53 | -61.04 |
| c13_macro100_decay2_size150_imp3 | 3454 | 30 | 1.00 | -13.32 | -63.79 |
| c14_macro100_decay8_imp3 | 3494 | 30 | 1.00 | -6.24 | -56.75 |
| c16_macro0_decay2_prog_late | 3534 | 14 | 0.00 | -115.06 | -115.06 |
| c15_ringout_0 | 3558 | 2 | 1.00 | -64.99 | -64.99 |

**Session F, parts Z, S1, S3, S5, S6, S7**

| Recording | Processed time s | Length s | Macro | old | host |
|---|---|---|---|---|---|
| s1_burst_map_macro100_decay0p5 | 3652 | 596 | 1.00 | -11.26 | -60.73 |
| z1_anchor_macro0_train | 4260 | 12 | 0.00 | -111.10 | -111.10 |
| z1_anchor_macro100_train | 4284 | 12 | 1.00 | -6.43 | -60.31 |
| s3_impulse_map_right_macro100_decay0p5 | 4784 | 592 | 1.00 | -7.99 | -60.89 |
| z2_anchor_macro0_train | 5388 | 12 | 0.00 | -112.45 | -112.45 |
| z2_anchor_macro100_train | 5412 | 12 | 1.00 | -8.12 | -58.21 |
| z3_anchor_macro0_train | 6356 | 12 | 0.00 | -109.79 | -109.79 |
| z3_anchor_macro100_train | 6380 | 12 | 1.00 | -6.30 | -60.04 |
| s5_impulse_map_left_macro100_decay0p5 | 6404 | 592 | 1.00 | -7.90 | -60.40 |
| s6_sweep_left_up_macro100_decay0p5 | 7008 | 132 | 1.00 | -10.03 | -64.99 |
| s6_sweep_right_down_macro100_decay0p5 | 7152 | 132 | 1.00 | -9.79 | -64.90 |
| z4_anchor_macro0_train | 7428 | 12 | 0.00 | -117.81 | -117.81 |
| z4_anchor_macro100_train | 7452 | 12 | 1.00 | -6.32 | -61.93 |
| s7_tone_pair_120s_macro100_decay0p5 | 7476 | 128 | 1.00 | -10.69 | -66.55 |
| z5_anchor_macro0_train | 8224 | 12 | 0.00 | -96.86 | -96.86 |
| z5_anchor_macro100_train | 8248 | 12 | 1.00 | -62.02 | -62.02 |

**Session T90 (90 BPM, play head 0.25 s ahead, playing)**

| Recording | Processed time s | Length s | Macro | base | host |
|---|---|---|---|---|---|
| t01_macro0_decay0p5_train | 130 | 24 | 0.00 | -110.94 | -110.94 |
| t02_macro100_decay0p5_train | 164 | 24 | 1.00 | -4.44 | -60.25 |
| t03_tone_macro100_decay0p5_00 | 198 | 16 | 1.00 |  | -66.65 |
| t03_tone_macro100_decay0p5_01 | 214 | 16 | 1.00 |  | -66.28 |
| t03_tone_macro100_decay0p5_02 | 230 | 16 | 1.00 |  | -66.18 |
| t03_tone_macro100_decay0p5_03 | 246 | 16 | 1.00 |  | -66.17 |
| t03_tone_macro100_decay0p5_04 | 262 | 16 | 1.00 |  | -66.12 |
| t03_tone_macro100_decay0p5_05 | 278 | 16 | 1.00 |  | -66.27 |
| t03_tone_macro100_decay0p5_06 | 294 | 16 | 1.00 |  | -66.35 |
| t03_tone_macro100_decay0p5_07 | 310 | 16 | 1.00 |  | -66.06 |
| t03_tone_macro100_decay0p5_tail | 326 | 6 | 1.00 |  | -62.27 |
| t04_scan_macro100_decay0p5 | 342 | 486 | 1.00 |  | -59.47 |
| t05_end_probe | 838 | 20 | 1.00 | -3.28 | -66.64 |

**Session S90 (90 BPM, transport stopped)**

| Recording | Processed time s | Length s | Macro | base | host |
|---|---|---|---|---|---|
| t01_macro0_decay0p5_train | 100 | 24 | 0.00 | -105.45 | -105.45 |
| t02_macro100_decay0p5_train | 134 | 24 | 1.00 | -4.80 | -61.88 |
| t03_tone_macro100_decay0p5_00 | 168 | 16 | 1.00 |  | -66.52 |
| t03_tone_macro100_decay0p5_01 | 184 | 16 | 1.00 |  | -66.15 |
| t03_tone_macro100_decay0p5_02 | 200 | 16 | 1.00 |  | -66.11 |
| t03_tone_macro100_decay0p5_03 | 216 | 16 | 1.00 |  | -66.08 |
| t03_tone_macro100_decay0p5_04 | 232 | 16 | 1.00 |  | -66.12 |
| t03_tone_macro100_decay0p5_05 | 248 | 16 | 1.00 |  | -65.97 |
| t03_tone_macro100_decay0p5_06 | 264 | 16 | 1.00 |  | -66.18 |
| t03_tone_macro100_decay0p5_07 | 280 | 16 | 1.00 |  | -66.07 |
| t03_tone_macro100_decay0p5_tail | 296 | 6 | 1.00 |  | -62.68 |
| t04_scan_macro100_decay0p5 | 312 | 486 | 1.00 |  | -31.10 |
| t05_end_probe | 808 | 20 | 1.00 | -2.92 | -65.58 |

Reading of the table:

- **E**: the host clock and the law of `analysis2` give the same numbers to 0.01 dB on all ten recordings.
- **D**: the host clock gives the numbers of the late-chunk table to 0.01 dB on all sixteen, with nothing but the
  origin, the tempo and the block layout.
- **F** (another instance, the same jobs from frame 1 440 000 on, then 5300 s more): 99 probes -66.9 to -67.8 dB;
  the batch -56.8 to -78.0 dB; maps, sweeps and tones up to 7600 s -58.2 to -66.6 dB. On these jobs the old law
  fails where the step of the position is 2^-17 to 2^-15 quarter note (40 to 256 s), 2^-13 (512 to 1024 s),
  2^-11 and 2^-10 (2048 to 8192 s), and holds where it is 2^-14 (256 to 512 s), 2^-12 (1024 to 2048 s) and 2^-9
  (behind 8192 s). Which ranges fail depends on the block layout; only the last one is safe for every layout.
- Over all 159 recordings with Macro above 0 outside S90 the host clock scores -56.3 to -78.0 dB. The floor is
  the one of `analysis2`.
- `z5_anchor_macro0_train` (-96.9 dB) is the base alone at 8224 s; not a matter of this clock.
- **S90**: the scan's voice gains by least squares are 0.9998 / 1.0022 / 0.9998; what holds it at -31 dB is single
  chunks, not a voice.

## 8. What remains unexplained

1. **The drift of the stopped clock** (section 5): 5e-5 sample per quarter note with a sign that changes at 512 and
   1024 quarter notes. It holds S90's long scan at -31 dB. A stopped session of 20 minutes with the impulse scan
   would give the curve over three more octaves.
2. **Floor of -56 to -68 dB at full Macro**, unchanged from `analysis2` (single-precision read position of the
   reversed readers). Tempo does not move it: T90 sits at the same depth.
3. **Edges of the law that no recording reached**: A between 39.8 and 44.1 (the catch window), A exactly 0 in the
   same-pitch and octave-down readers (it happens at tempi where the chunk is a whole number of milliseconds,
   e.g. 100 BPM), A < 0 in the octave-up reader, two 44-sample blocks inside one tick.
4. **Other host rates and block sizes.** The tick is taken as the converter block (48 frames at 48 kHz). At 44.1 kHz
   that block is 44 frames (0.998 ms) and the stamps fall elsewhere; nothing was recorded. Host blocks other than
   512 are covered by the arithmetic and untested, apart from the eight shifted grids of T90 t03.
5. **One tempo.** Every "scales" is a two-point statement (90 and 120 BPM). A tempo that changes inside a
   recording, and a tempo whose chunks are not rational in milliseconds, were not recorded.
6. **Transport events**: start, stop, jump, loop, and the Macro law at another tempo.
7. **Beyond 32 768 quarter notes** (4.6 h at 120 BPM) the step of the position exceeds a tick; the arithmetic
   says what happens (boundaries on a grid of 2 ms and coarser), no recording does.

## 9. For the C++ engine

What the plug-in must read from the host, once at the start of every `processBlock`:

- the position in quarter notes (`ppqPosition`), the tempo (`bpm`), and `isPlaying`.

What it does with them (reference behaviour):

1. **Playing.** Convert the position to `float`. For every converter block of 48 host frames (the engine's
   existing 44.1 kHz conversion block; they run through across host blocks) remember the host block in which its
   first frame arrived and that frame's offset d, and stamp it with
   `float(p0 + float(d * bpm / (60 * hostRate)))`. Each reversed reader compares the stamp with multiples of its
   note length (`float` 4/3, 2, 8/3 quarter notes; use `fmod` or exact products, not an accumulated phase) and
   starts its next chunk as section 3.1 says. Chunk length in samples, windows and the octave-up recirculation
   delay are `note * 60 / bpm * 44100`; recompute them when the tempo changes. Grain readers and gains do not
   depend on the tempo.
2. **Stopped.** Do not use the host position. Count internal samples from zero at the moment the Character is
   selected (the same moment that restarts the 32 oscillators and the three phasors) and use
   `samples * bpm / (60 * 44100)` quarter notes, per 44-sample block, in double precision.
3. **No tempo from the host.** Not measured (the session host always reports one). Use 120 BPM: it gives the
   2/3, 1 and 4/3 s of every recording before today. No position from the host: treat as stopped.
4. **Exact or pleasant.** The single-precision stamps are what a null test against the reference needs: they move
   single chunks by up to one step of the position (0.06 ms at 10 minutes, 0.5 ms at 90 minutes, 2 ms behind
   4.6 hours at 120 BPM) and are inaudible until late in a long session, where they are a defect. A product may
   compute the stamp in double precision and keep the single-precision path for the test harness. Either way the
   tests need three hooks: the oscillator origin, the host's block layout (block size and where each run of
   blocks started), and the reported position.
5. **Do not port** the tick of 1/1024 s, the late-chunk table, or chunk lengths in seconds. The Python reference is
   `tempo/model/hostclock.py`; the port can be checked against it chunk by chunk (`chunk_schedule` returns k, the
   tick, A, I, s and X).

## 10. Files

| File | Content |
|---|---|
| `tempo/model/abyss_model.py`, `hostclock.py`, `base.py`, `abyss_core.c` | the extended model |
| `tempo/score_tempo.py`, `scores.json`, `logs/score_*.log` | the score table |
| `tempo/t00_origin.py` | oscillator origins |
| `tempo/measure.py`, `t01_map.py`, `cache/map_*.json` | mirror points by nulling |
| `tempo/t02_law.py`, `t03_resid.py`, `t04_implied.py`, `t05_ms.py`, `t06_dev.py` | the readings that failed or were stages (tick fixed, tick scaled, milliseconds) |
| `tempo/t07_stamp.py`, `t08_sched.py`, `t19_variants.py`, `t10_cmp.py` | the stamp arithmetic against the copies and against the old law |
| `tempo/t09_quick.py` | bit identity of the default path; probes with the host clock |
| `tempo/t11_t90.py`, `t12_voices.py`, `t13_up.py`, `t14_uplag.py`, `t15_upx.py` | what follows the tempo; the octave-up reader |
| `tempo/t16_s90.py`, `t17_s90null.py`, `t18_s90up.py`, `t20_s90drift.py` | stopped transport |
