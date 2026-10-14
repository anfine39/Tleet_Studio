# VOID — GUI and audio test plan

DSP tests: `tests/VoidTests.cpp`; GUI tests: `tests/VoidGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to
`test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (Hall playing, Plate Dark PRE filter, Void Ruin with DROWN and ∞, frozen) | VOID GUI: editor opens at every scale | `VOID_editor_*.png`, `VOID_state_*.png` |
| 15 knobs (with their readout windows), 5 algorithm lamps, 6 buttons, FREEZE, the display, EARLY / LATE, meters, filter graph and the five parts of the preset bar at mockup positions; click areas of different controls never overlap | VOID GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and dots are real values at their real angles; readouts print the parameter text | VOID GUI: scale numbers and dots | label and dot ≤ 1° from their ray; pointer at the value ≤ 0.05°; texts checked |
| Automation and keys move every knob; lamps, buttons, the COLOR and ALGORITHM knobs, FREEZE and the preset bar set the real parameters / presets | VOID GUI: every control drives its real parameter | parameter values, lit lamps, preset index |
| Meters read the real peak on the printed scale; EARLY / LATE follow the processor's levels; FREEZE lit = frozen; the lit lamp is the selected one; the DROWN ring is lit to the value; the filter curve is the processor's response; the spectrum peaks at the played frequency | VOID GUI: lamps, displays and meters | dB values, screenshot brightness, curve vs response (px) |
| Every visible control changes the rendered audio | VOID GUI: no dead controls | change > −40 dB per control |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/VOID/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.

## Manual QA in Studio Pro 8

1. ALGORITHM through all five with a drum loop: build-up, density and tail differ; no click on switching.
2. DECAY to ∞, then FREEZE on and off: the tail holds, stays clean, no level creep.
3. DROWN from 0 to 100 while playing: the space grows into a wash; the other knobs keep their values.
4. FILTER MODE PRE / LOOP / POST with HPF and LPF: in LOOP the tail gets darker / thinner as it decays.
5. COLOR CLEAN / DARK / RUIN; DUCK with a vocal; WIDTH 0 (mono) to 200.
6. MIX 0 is the dry signal; OUTPUT ±12 dB; the preset bar steps the presets.
