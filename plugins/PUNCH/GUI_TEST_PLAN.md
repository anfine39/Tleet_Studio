# PUNCH — GUI and audio test plan

DSP tests: `tests/PunchTests.cpp`; GUI tests: `tests/PunchGuiTests.cpp`. All run
on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (compressing, smashed, gentle) | PUNCH GUI: editor opens at every scale | `PUNCH_editor_*.png`, `PUNCH_state_*.png` |
| 5 knobs and the OverEasy switch at mockup positions; hitboxes never overlap; the meter takes no clicks | PUNCH GUI: hitboxes | hit tests, pairwise intersection |
| Scale numbers are real values at their real angles; ratio numbers equal the parameter's own ratio | PUNCH GUI: scale numbers | label ≤ 1° from its ray; pointer at the label's value ≤ 0.05°; texts |
| Automation and keys move every knob; clicks on the rocker or its caption toggle OverEasy; the lamp is lit on screen only when on | PUNCH GUI: knobs and the OverEasy switch | parameter values, lamp brightness ratio |
| GR LED meter = measured level drop; segments follow the printed scale | PUNCH GUI: GR meter | shown vs measured ≤ 0.3 dB at four thresholds; segment edges at printed ticks; lit ≥ 2× unlit; silence clears |
| DSP: RMS detector, program-dependent timing, continuous ratio to ∞, hard knee vs OverEasy, GRIT, GR telemetry, latency, bypass, NaN, state | `tests/PunchTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/PUNCH/` (references need Andrew's approval first).
- Golden audio renders; mutation tests at GUI level (freeze GR telemetry, make OverEasy visual-only).
- GUI-open vs GUI-closed bit-identical render; offline render; CPU p95/p99.

## Manual QA in Studio Pro 8

1. PUNCH on a snare or drum bus, Threshold down: the bar lights to the dB of real reduction; 0–4 dB in half-dB steps.
2. Compression from 1 to ∞: the sound goes from untouched to limiting; the numbers stay put.
3. OverEasy: click the rocker or the OFF / ON caption; the lamp lights; the knee softens.
4. GRIT from Clean to Punch: colour grows with the gain reduction.
5. Save, reopen: values and editor size are restored.
