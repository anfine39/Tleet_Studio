# TAPE — GUI and audio test plan

DSP tests: `tests/TapeTests.cpp`; GUI tests: `tests/TapeGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to
`test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (defaults running, Hot driven, 7.5 IPS worn with noise, 30 IPS with EQ and MIX) | TAPE GUI: editor opens at every scale | `TAPE_editor_*.png`, `TAPE_state_*.png` |
| 13 knobs (with their readout windows), 12 buttons, AUTO GAIN, both VUs, the SATURATION bar and TAPE CONDITION at mockup positions; click areas of different controls never overlap; the reels sit on their flange centres and never take the mouse | TAPE GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and dots are real values at their real angles; MIX reads 0…100; OUTPUT 0 dB at the top; readouts print the parameter text | TAPE GUI: scale numbers and dots | label and dot ≤ 1° from their ray; pointer at the value ≤ 0.05°; texts checked |
| Automation and keys move every knob; every button group has one button per real choice and sets it; AUTO GAIN toggles its parameter | TAPE GUI: every control drives its real parameter | parameter values, lit buttons |
| RECORD needles read 0 VU for a −18 dBFS sine and follow INPUT; L and R needles are independent; needles point at the printed marks; SATURATION is dark in silence, lights more with more drive, = round(24 × activity), falls back; the TAPE CONDITION line equals the processor's response at every point and follows TAPE SPEED / HEAD BUMP / HF; the spectrum reads a −12 dBFS sine at −12 dB; the reels stand still without signal and turn by exactly IPS / (2π × 3.5 in) turns per second at every speed | TAPE GUI: VU needles, saturation bar, response, spectrum and reels | VU values, segment counts, dB, radians |
| The reel picture turns, never shows above the window, and repeats every 120° (three windows) under a fixed light | TAPE GUI: the reels are drawn turned | screenshot pixel differences |
| Every visible control changes the rendered audio | TAPE GUI: no dead controls | change > −60 dB per control |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/TAPE/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.

## Manual QA in Studio Pro 8

1. Drums and a mix bus: INPUT up; the SATURATION bar lights further, the REPRO needles stay near the RECORD ones with AUTO GAIN on, and the sound thickens without getting louder.
2. TAPE SPEED 7.5 / 15 / 30: the TAPE CONDITION line moves its head bump and HF roll-off; the reels turn at half / double speed; no click on switching.
3. TAPE TYPE and CALIBRATION at the same INPUT: earlier or later saturation.
4. AGE, WOW, FLUTTER at extremes; NOISE ON with NOISE amount: hiss audible in silence, still on the meters.
5. Play / stop in the host: the reels turn only while the transport plays.
6. MIX 0 is the dry signal, latency-aligned (null against the dry track).
