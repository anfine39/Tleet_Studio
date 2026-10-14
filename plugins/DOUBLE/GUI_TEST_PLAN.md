# DOUBLE — GUI and audio test plan

DSP tests: `tests/DoubleTests.cpp`; GUI tests: `tests/DoubleGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Every control at its mockup position, knob centre and readout window ±1 px, 16 hitboxes | DOUBLE GUI: every mapped control sits at its mockup position | hit-tests centre and 4 inner corners of every hitbox |
| Clickable areas never overlap | same | pairwise check of all hitboxes |
| Readouts show the real value and follow host automation | DOUBLE GUI: readouts print the real parameter value | readout text = parameter text, knob position = parameter |
| VOICES / MODE / toggles change the parameter and light the right button on screen | DOUBLE GUI: VOICES, MODE and the lamp toggles | parameter value + pixel brightness of every button face in a snapshot |
| Voice display shows exactly the voices the DSP runs (Voices 2 never shows 4), at their real pans; Width 0 collapses | DOUBLE GUI: the voice display shows exactly the voices the DSP runs | display state vs telemetry, voice count, pans |
| Filter curve = exact response of the live wet filters on a true log axis | DOUBLE GUI: the filter curve is the exact response | drawn path vs `wetFilterResponseDb` at 6 frequencies × 3 settings (±0.6 px) |
| Screenshots: 75/100/150/200 %, default and 4 representative states | DOUBLE GUI: editor opens at every scale | `DOUBLE_editor_*.png`, `DOUBLE_state_*.png` |
| DSP: pitch and delay accuracy, voice count, width, mono fold-down, auto level, humanize, modes, filters vs sweep, click-free automation, latency, bypass, state | `tests/DoubleTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/DOUBLE/` (references need Andrew's approval first).
- Mutation tests: freeze telemetry, make a button visual-only, draw a fixed filter curve; each must make a test fail.
- GUI-open vs GUI-closed bit-identical render; CPU per instance count; `TEST_REPORT.html`.

## Manual QA in Studio Pro 8

1. Vocal on a mono track, DOUBLE inserted: switch VOICES 1 / 2 / 4, the display shows 1 / 2 / 4 clouds, the level stays even with AUTO LEVEL on.
2. Turn WIDTH to 0: the clouds gather at the centre; back up: they spread.
3. Turn HPF and LPF: the curve moves, the readouts show the frequency.
4. Sum the master to mono with MONO SAFE on and off and compare.
5. Save, reopen the project: values, buttons and editor size are restored.
