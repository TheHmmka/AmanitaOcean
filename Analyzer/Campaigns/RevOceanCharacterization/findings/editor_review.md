# Editor review: the final editor (Fathom, deep blue, recomposed centre, block centred on the whole Evolution control)

Reviewer's report, 7 October 2026. Read-only for the source tree. Everything below was rendered, run and
measured by the reviewer; the engineers' pictures and numbers were used only for comparison.

- Build: `build-ebb-review-editor` (Release, arm64), target `AmanitaOceanStateTests`, exit 0, 0 warnings.
  State tests in that build: exit 0, 45 metric lines. Source hashes taken at the build and again at the end
  of the review are identical (`source_hashes_after_build.txt`).
- Scratch and evidence: `Analyzer/Results/RevOceanCharacterization/work/review_editor/final/` (called `final/`
  below). Older files one level up belong to an earlier review of an earlier editor.
- "dp" are design pixels of the 960 x 640 canvas, "pt" points of the editor, "px" pixels of a picture.

## Verdict

The editor does what the owner decided. No blocker. One major finding, and it is older than this wave
(a keyboard trap in the value fields of the knobs). The rest is minor.

**Does the centre read as one balanced group? Yes.** The block and the knob hang under the drop-down as a
pair: the same inner gutter on both sides (44.00 and 44.25 dp), one common middle line (0.06 dp apart), the
same air over the ring and under the value (54.3 and 52.9 dp), and the pair as a whole centred between the
foot of the drop-down and the footer rule (middle 318.5 against 318).

**What still looks wrong**, in order of weight:
1. Over the GPU field the dark pool round the block is far larger than the block. In bright passages it
   takes 44 to 50 % of the whole centre down by more than half and reads as a dark oval about 560 x 400 dp.
   There is no box, no straight edge and no corner. The left half of the knob stands in it (finding 8).
2. The EVOLUTION label of the same group loses its ground when a bright passage passes: in 15 % of frames
   at factory settings (finding 9).
3. The mirror line on the text side is carried by the hairline alone. The paragraph's widest line ends
   2.8 dp (Veil) to 14.1 dp (Fathom) short of it, so the gutter looks wider on the left for some
   Characters. This comes with ragged text and is no defect.
4. The name starts 108.6 dp under the drop-down, the ring 54.3 dp: the left column begins lower. That is
   the consequence of centring the block on the whole control, as decided.

## 1. Layout, measured

Pictures: `final/cpu/` (18 pictures from `AmanitaOceanStateTests --render-ui`, six Characters at 804, 960
and 1440; pixel-identical to the reviewer's own renders, 0 differing pixels in all 18), `final/list/`,
`final/sheets/`. Method and every number: `final/measure.py`, `final/layout_measurements.txt` (parts painted
alone at 8 px per point, and the whole picture less the same editor without controls).

| Measure (dp) | 804 | 960 | 1440 | Decision |
|---|---|---|---|---|
| Drop-down centre less axis | 0.60 (0.5 pt, odd width) | 0.00 | 0.00 | centred |
| Drop-down size | 340.3 x 40.6 | 340.0 x 40.0 | 340.0 x 40.0 | 340 x 40 |
| Drop-down under the header rule | 32.2 | 32.0 | 32.0 | about 30 |
| Axis to block (end of hairline) | 44.18 | 44.00 | 44.00 | about 44 |
| Axis to ring | 44.33 | 44.25 | 44.58 | about 44 |
| Mirror error | 0.15 (0 px at 2x) | 0.25 (0 px) | 0.58 (2 px) | mirror |
| Ring across | 178.8 | 179.4 | 179.4 | about 180 |
| Drop-down foot to ring top | 53.9 | 54.25 | 53.9 | at least 48, about 54 |
| Control: ring top to value baseline | 206.7 to 430.9 | 206.25 to 430.75 | 205.9 to 430.4 | about 206.5 to 431 |
| Control middle | 318.81 | 318.50 | 318.17 | about 319 |
| Text: capitals' top to last baseline | 260.7 to 376.7 | 260.6 to 376.5 | 260.25 to 376.2 | |
| Text middle less control middle | -0.075 | +0.062 | +0.042 | on the middle |
| The same with the ink of "68 %" as foot | -0.22 | -0.125 | -0.125 | |
| Value ink to footer rule | 52.8 | 52.9 | 53.25 | at least 32 |
| Value field to footer rule | 47.0 | 47.0 | 47.3 | at least 32 |

All six Characters give the same figures at one size (four paragraph lines each, pitch 14.00, capitals of
the name 12.25 high). Name, subtitle and hairline begin at 0.00 of the block; the paragraph's ink begins
0.08 to 0.33 to the right (side bearing). The EVOLUTION label and the value are centred under the dial
(613.56 and 614.44 against 614). The dial's centre is at 614.00, 296.00 at the default size.

- Clipping and overlap: the controls paint 0 pixels in the centre outside the bounds of drop-down, block
  and knob (18 pictures). The block's ink keeps 16.9 pt or more to its own bounds.
- Header and bottom row against the committed editor (`final/compare_committed.txt`, Current at 960):
  no pixel differs by more than 7 of 255 (the moved background). Bright pixels: header 4786 and 4786, none
  displaced; bottom row 14906 and 14894, 22 displaced. A shift of one picture pixel would displace 1682 to
  8732.
- Five sizes in between (852, 900, 1026, 1200, 1338; `final/layout_measurements_intermediate_sizes.txt`):
  vertical centring -0.05 to 0.00 dp, air under the drop-down 53.9 to 54.6. The mirror reaches -1.2 dp at
  900 (block 44.8, ring 43.6; 1.1 pt) and +0.9 dp at 1200 (finding 11).

## 2. Legibility and colour

GPU field. The reviewer wrote his own offscreen run of the shaders (`final/offscreen.cpp`) and checked it
against pictures read back from the running editor's OpenGL context at three sizes
(`final/validate_offscreen.txt`): mean difference 0.36 to 0.40 of 255, against 2.5 to 4.6 with the calm
region off and 0.40 to 0.54 with the calm region where the block stood before the centring. So the editor
does hand the moved block to the shader.

Block over the GPU field, 3072 frames (128 moments over the 4096 s cycle, six Characters, four settings;
`final/contrast_gpu_calm_on.txt`), lowest contrast at any glyph pixel:

| | name | subtitle | paragraph |
|---|---|---|---|
| Calm region on | 14.01 | 5.48 | 9.49 (Bloom, Evolution 100 %, Focus 0 %) |
| Calm region off | 1.79 | 1.00 | 1.14 |

No frame has the paragraph under 7 : 1 or the subtitle under 5 : 1. The field is the same picture at every
editor size, so these hold at 804 and 1440.

CPU fallback, 1152 frames per size (`final/contrast_cpu.txt`): paragraph 9.26 / 9.19 / 9.04, name 13.48 /
13.45 / 13.47, subtitle 5.54 / 5.44 / 5.40 at 804 / 960 / 1440. With a contour line of the editor assumed
over the pixel: 8.18, 12.18 and 4.91 at worst.

Deep blue (`final/contrast_accent.txt`, `final/contrast_title.txt`):
- highlighted list item: white text 4.72 : 1 (the other five accents carry dark text at 6.1 to 7.9);
- focus border of the drop-down 3.35 : 1 against its field (state test metric of this build);
- title word OCEAN 4.47 : 1 on the plain header (18 pt bold; the other accents 6.7 to 8.9). Over the GPU
  field a glyph pixel falls under 3 : 1 in 5.5 % of frames at factory settings, the other accents 0 to 3.1 %;
- label of a toggle that is on (11 pt bold) 4.21 : 1, the other accents 6.3 to 8.4 (finding 10);
- mark of the selected list item 4.09 : 1.

At 804 on a display with one pixel per point the subtitle is 7.1 pt and the paragraph 8.8 pt; both stay
readable (`final/sheets/block_fathom_1x_at_804_960_1440_enlarged3.png`).

## 3. Behaviour

`final/probe.cpp` links the plug-in's code as the state tests do and drives the editor in a window of its
own (fully transparent). Logs: `final/behave.log`, `final/mods.log`, `final/morph.log`.

Works as intended:
- The drop-down has the keyboard when the window opens. Arrows step and wrap, one host gesture each;
  the block follows. Escape, Home, End, Page Down and letters are passed on.
- Return and Space open the list. Down and Up move the highlight, Return or Space choose (one gesture),
  Escape closes without a change, and the focus returns to the drop-down.
- Host to drop-down and block, and a state restored while the editor is open: both update at once
  (four states tried).
- Accent morph: look-and-feel, list highlight, its text, title word, focus border and ring move in step
  and settle about 0.8 s after the change; the highlighted text turns from dark to white on the way. Read
  back from OpenGL: after Default to Fathom and Fathom to Bloom no pixel of the old accent is left in
  title, ring or bottom row (`final/morph/`).
- Accessibility tree: combo box "Reverb character" with its description and the current name as value;
  the block as static text with the name as title and subtitle plus paragraph as description; the list as
  a menu of six named items.

Findings 1 to 5 below are from this part.

## 4. Code

- The description table holds the six approved texts character for character
  (`final/texts_check.txt`; 18 fields identical, no character outside ASCII).
- No string CHARACTER is drawn: no literal in `Source/` contains the word in capitals, the only
  upper-casing is of knob names and toggle texts, and no picture shows it. "Reverb character" exists only
  as the accessible title.
- The working name: no occurrence in `Source/`, `Tests/StateTests.cpp` or `CMakeLists.txt`. What is left
  elsewhere: three hexadecimal noise seeds that spell it (`Tests/DspTests.cpp:3527` and `:4390`,
  `Tools/FathomRender.cpp:383`) and the folder name `build-ebb` in `docs/FATHOM.md`.
- Nothing is left of the segment selector, the label or the index line (no `TextButton`, segment,
  caption or index code under `Source/ui`).
- Sizes in the look-and-feel are named design values scaled by the field's height; no size preset. The
  list follows the editor: 285 x 196 with items of 31 at 804, 340 x 228 with 36 at 960, 510 x 342 with 54
  at 1440, each under its field at the field's width.

Findings 4, 6, 11 and 12 below are from this part.

## 5. Tests

- 191 assertions in the committed file, 303 now. Eleven committed assertions are gone
  (`final/test_pins_diff.txt`): "exactly five choices" and ten about the segment buttons and the old
  geometry. No assertion with a kept message has a changed condition. The background test lost no line.
- The pins do test the final editor: bounds 310,112 / 188,239 / 510,199, mirror, ring, air, the centring
  on the whole control for six Characters at three sizes, two lines over the hairline (no index line),
  nothing painted between header rule and drop-down (no label), the list, the keys, the accent.
- Gaps: findings 4 and 7, and the trap of finding 1, which no test sees. The removed assertion "Character
  segment must not create a keyboard-focus trap" has no successor.

## Findings

Severity, file and line, scenario, evidence.

1. **Major, older than this wave. Forward Tab never gets past the Evolution value.**
   `Source/ui/ParameterKnob.cpp:85-96` (`onEditorHide` hands the focus to the dial one message later),
   with `:83` and `:173-177`. Tab from the drop-down goes to Mono Safe, Freeze, the Evolution dial, the
   Evolution value (its editor opens), then to the Pre-delay dial, and 120 ms later the focus is back on
   the Evolution dial. 34 Tab presses never reached the lower row (`final/behave.log`, "TAB ORDER").
   Shift-Tab works. The same deferred grab takes the keyboard from any control one moves to out of an
   open value editor, the drop-down included. These lines are unchanged from the committed file.
2. **Minor, older than this wave. Tab order is drop-down, Mono Safe, Freeze, then the knobs.**
   `Source/ui/PluginEditor.cpp:165`, `:181`, `:186-196`. The orders 5 to 14 are set on dials inside
   their knob components, and JUCE sorts each parent by itself, so the header toggles (15, 16) come
   second and third.
3. **Minor. With the list open, Left and Right change the Character behind it.**
   `Source/ui/CharacterSelector.cpp:192-211`. JUCE's menu forwards these keys to the drop-down, whose
   `keyPressed` steps without asking whether its list is open. Sequence: Bloom selected, Return, Right,
   Right, Left gives parameter 1, 2, 3, 2 with three host gestures while the list stays open with its
   mark and highlight on Bloom; Escape then leaves Drift (`final/behave.log`,
   `final/behave/list_after_right_right_left.png`).
4. **Minor. The help text that names the six Characters reaches nobody.**
   `Source/ui/CharacterSelector.cpp:159-170` and `:186`. A combo box reports its tooltip as help, not
   `getHelpText()`: the handler's help is empty (`final/behave.log`, "ACCESSIBILITY TREE"). The committed
   selector, a plain component, did expose its help. `Tests/StateTests.cpp:1661-1664` pins the unused
   string.
5. **Minor. The drop-down takes arrows with any modifier, and Space.**
   `Source/ui/CharacterSelector.cpp:194`, `:200`, `:207` (`isKeyCode` ignores modifiers). Cmd+Right,
   Alt+Down, Shift+Up and Ctrl+Left change the Character; Shift+Space opens the list (`final/mods.log`).
   Plain Space was asked for; note that the drop-down holds the keyboard as soon as the window has it, so
   in a host that offers keys to the plug-in first, Space opens the list where the committed selector let
   it through to the transport.
6. **Minor. The widow control is no longer used.**
   `Source/ui/CharacterSelector.cpp:64-67` and `:134-156`. At 248 wide no paragraph ends in a widow (last
   lines 35 to 98 % of the width); it was needed at 232 for Current and Fathom (`final/widow.log`). No
   test reaches it.
7. **Minor. No test holds the approved wording.**
   `Tests/StateTests.cpp:1240-1255` asks only for the name, a capitalised subtitle of 12 characters and a
   paragraph of 120 that ends in a full stop and contains "Evolution". A changed word passes. Nor is any
   colour of the block's text pinned.
8. **Minor, design. The calm pool is much wider than the block.**
   `Source/ui/AbyssalFlowShaders.h:183-189`: the calm fades over 0.30 of the height, 192 dp, beyond the
   block. In bright frames the ground is under half of its uncalmed luminance from x 14-33 to 586 and
   from y 103-134 to 479-552; 44 to 50 % of the centre zone is darkened by more than half; the dial's
   left half stands at 0.45 to 0.49 of its luminance, its right half at 0.96 to 0.98; the drop-down's
   ground at 0.58 to 0.70 (`final/calm_pool_extent.txt`, `final/hard/hard_bloom_t1249_e100_f0.png` beside
   `..._calm_off.png`). No box is visible.
9. **Minor. The EVOLUTION label loses its ground over bright passages.**
   Label colour `Source/ui/ParameterKnob.cpp:60`. A glyph pixel under 4.5 : 1 in 15.0 % of frames at
   Evolution 35 %, Focus 100 %, in 35.2 % at 100 % and 0 % (under 3 : 1 in 17.2 %), lowest 1.00. Its value:
   0.0 % and 4.4 %. The engineers' 5.8 % and 26.2 % are of the mean ground under the label's box, a
   milder measure. Known as an open decision; the labels of the bottom row and the product subtitle
   behave alike and were so before.
10. **Minor. Small text in the deep blue is under 4.5 : 1.**
    `Source/ui/OceanLookAndFeel.cpp:400`: FREEZE or MONO SAFE when on, 11 pt bold, 4.21 : 1 on the plain
    pill. Known as an open decision.
11. **Note. The mirror is set from design constants, the vertical middle from the knob as laid out.**
    `Source/ui/PluginEditor.cpp:41-42` and `:321-323`. Whole-pixel rounding leaves 0.15 / 0.25 / 0.58 dp
    at the three pinned sizes and up to 1.2 dp (1.1 pt) at 900 wide.
12. **Note. Small things in the code.** `PluginEditor.cpp:32` repeats the ring geometry of
    `OceanLookAndFeel::drawRotarySlider` as 103.6 / 224 (a test pins the pair,
    `Tests/StateTests.cpp:1426`). `ParameterKnob.cpp:71` still sets a 35 pt value font for the hero that
    `resized()` replaces with 18 (older than this wave). `OceanLookAndFeel.cpp:179` and `:575-578` serve
    menu section headers, which no menu of the editor has.
13. **Note. Documents.** `README.md:61-62` and `:480-491` still describe one selector whose inactive
    names fade to 15 %, a central Evolution and five accents. `docs/FATHOM.md:837` (item 10) refers to
    selector words that no longer exist. Neither file was the reviewer's to edit.

## Not verified

- No host: the plug-in was not installed or opened in one. Host scale factors, how a host hands over the
  keyboard, and the list inside a host window were not seen.
- Mouse: hover in the list, click to open, click outside to close. The list's shadow.
- VoiceOver itself. The tree above is what JUCE reports; how VoiceOver reads the static text is untested.
- Windows and Linux. The sanitized runs and the DSP tests were not repeated (not asked; no DSP file is
  in this review).
- Mutations of the sources: the read-only rule forbids them in the tree, and no copy was built. The test
  gaps above rest on reading the assertions.
