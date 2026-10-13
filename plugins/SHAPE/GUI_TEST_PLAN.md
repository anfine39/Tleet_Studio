# SHAPE — GUI and audio test plan

DSP tests: `tests/ShapeTests.cpp`; GUI tests: `tests/ShapeGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to
`test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (low trick, air sharp, Burn with EQ out) | SHAPE GUI: editor opens at every scale | `SHAPE_editor_*.png`, `SHAPE_state_*.png` |
| 12 knobs and the EQ IN bezel at mockup positions; click areas never overlap | SHAPE GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and dots are real values at their real angles; only the real selector positions are printed (addendum §4); CHARACTER stops on the printed marks | SHAPE GUI: scale numbers and dots | label and dot ≤ 1° from their ray; pointer at the value ≤ 0.05°; texts checked; CHARACTER ≤ 1.5° from the marks |
| Automation and keys move every knob; a click on EQ IN toggles it and only the real state is lit; every visible control changes the rendered audio | SHAPE GUI: knobs and EQ IN | parameter values; lit/dark brightness ≥ 2×; change > −40 dB per control |
| DSP: passive network vs its evaluator, curve families, LF boost+atten shape, HF bandwidth sweep, ranges, EQ IN vs host bypass, THD per CHARACTER and DRIVE, aliasing, clicks, latency, NaN, state, presets | `tests/ShapeTests.cpp` (22 tests) | see `MODEL_SPEC.md` and the CSVs |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/SHAPE/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.
- Offline render; CPU p95/p99.

## Manual QA in Studio Pro 8

1. LOW FREQUENCY 60, BOOST and ATTEN both up: more weight below, a dip above it; the two never cancel.
2. HIGH FREQUENCY sweep 3…16 kHz with BOOST up; BANDWIDTH from SHARP to BROAD widens the lift.
3. HIGH ATTEN at 5 / 10 / 20 kHz: the top rolls off from each.
4. EQ IN off: the curves go, the transformer and tube colour stays; host Bypass is the dry signal.
5. CHARACTER Clean / Original / Burn and DRIVE: colour rises in that order, the EQ stays the same.
6. MIX 0 is the dry signal, OUTPUT ±20 dB.
7. Save, reopen: values and editor size are restored.
