# WEIGHT — GUI and audio test plan

DSP tests: `tests/WeightTests.cpp`; GUI tests: `tests/WeightGuiTests.cpp`. All run
on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (bus glue, unlinked left, smashed) | WEIGHT GUI: editor opens at every scale | `WEIGHT_editor_*.png`, `WEIGHT_state_*.png` |
| 9 knobs, 11 lamps and the EXT SC switch at mockup positions; click areas never overlap; the meters take no clicks | WEIGHT GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and step dots are real values at their real angles | WEIGHT GUI: scale numbers and step dots | label ≤ 1° from its ray; pointer at the label's value ≤ 0.05°; one dot per step at the pointer's angle; texts equal the parameter's step texts |
| Automation and keys move every knob; lamp and caption clicks select; only the real position is lit on screen; GR MODE never changes the audio; EXT SC lit only with a real key | WEIGHT GUI: knobs and lamps | parameter values, lit/unlit brightness ≥ 2.5×, bit-identical outputs per GR mode, lamp brightness |
| Each GR meter = its channel's measured level drop; link 0 with a left-only signal leaves the right meter at 0; link 100 reduces and shows both; GR MODE switches the reading | WEIGHT GUI: GR meters | shown vs measured ≤ 0.3 dB; segment edges on printed ticks; lit ≥ 2× unlit; silence clears |
| DSP: detector topologies, THRUST, HPF, steps, link, telemetry, sidechain matrix, TONE, latency, bypass, NaN, state | `tests/WeightTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/WEIGHT/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests (freeze one channel's telemetry, make a lamp visual-only).
- GUI-open vs GUI-closed bit-identical render; offline render; CPU p95/p99.

## Manual QA in Studio Pro 8

1. WEIGHT on the mix bus, Threshold down: both bars grow from 0 to the left to the dB of real reduction.
2. Stereo Link to 0 with a hard-panned source: only that side's meter moves.
3. THRUST Clean → Hard: less pumping from the kick; the lamp follows.
4. DETECTOR Feed Forward / Feed Back, TONE Old / Neutral / New, GR MODE: a click on a lamp or its caption.
5. Route a key to the sidechain input, EXTERNAL SC on: the lamp lights; remove the key: faint glow, internal detector.
6. Save, reopen: values and editor size are restored.
