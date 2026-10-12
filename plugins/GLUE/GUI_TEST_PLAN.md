# GLUE — GUI and audio test plan

DSP tests: `tests/GlueTests.cpp`; GUI tests: `tests/GlueGuiTests.cpp`. All run
on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (compressing, heavy Burn, gentle) | GLUE GUI: editor opens at every scale | `GLUE_editor_*.png`, `GLUE_state_*.png` |
| 11 knobs at mockup centres, hitboxes never overlap; meter and arc lamp take no clicks; EXT SC sits in the toolbar | GLUE GUI: hitboxes and centres | hit tests, pairwise intersection |
| Scale numbers are real values at their real angles; one dot per step | GLUE GUI: scale labels | label ≤ 1° from its ray; pointer at the label's value within 0.05°; expected texts |
| Host automation and keys move every knob | GLUE GUI: knobs follow automation | pointer angles |
| GR LED meter = measured level drop | GLUE GUI: GR meter | lit segments vs measured attenuation (±0.3 dB → ±1 segment) at four thresholds; lit ≥ 2× unlit brightness; silence clears the bar |
| Character lamp follows the parameter; EXT SC lamp lit only with a real key | GLUE GUI: character lamp and sidechain switch | lamp angle and brightness; lamp off without a sidechain bus; click sets the parameter |
| DSP: linked detector, dominant channel, stepped timing, ratios, Auto release, SC HPF pumping, Dark, Crush, Character, external key, GR telemetry, latency, bypass, NaN, state | `tests/GlueTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/GLUE/` (references need Andrew's approval first).
- Golden audio renders; mutation tests (disconnect SC HPF, freeze GR telemetry).
- GUI-open vs GUI-closed bit-identical render; offline render; CPU p95/p99.

## Manual QA in Studio Pro 8

1. GLUE on the mix bus, Threshold down: the LED bar lights to the dB of real reduction.
2. Attack, Release, Ratio click through their steps; the dots and numbers stay put.
3. Character: Clean / TLEET / Burn, the arc lamp moves.
4. Route a kick to the sidechain input, press EXT SC: the lamp lights and the bus ducks to the kick. Remove the route: the lamp shows only an outline.
5. Save, reopen: values, EXT SC and editor size are restored.
