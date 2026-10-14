# BITE — GUI and audio test plan

DSP tests: `tests/BiteTests.cpp`; GUI tests: `tests/BiteGuiTests.cpp`. All run
on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Every control at its mockup position, rotary centre ±1 px, 12 hitboxes | BITE GUI: every mapped control sits at its mockup position | hit-tests centre and 4 inner corners of every hitbox; the gap between ratio buttons hits nothing |
| Hitboxes never overlap | same | pairwise intersection check |
| Attack/Release numbers are the real times at the dots, slowest left | BITE GUI: Attack/Release scale labels … | label = parameter value with the knob at that dot; ends = 800/20 µs, 1100/50 ms |
| Drag (clockwise = faster), keys and wheel step the Character knob one position, ratio buttons select their ratio | BITE GUI: drag, keys, wheel and button clicks | parameter values |
| Only the selected ratio lamp is lit, on screen | BITE GUI: the lit ratio button follows the parameter | pixel brightness of all 5 lamps in a snapshot (lit ≥ 2× the brightest other) |
| Host automation moves every control; VU modes change the measured quantity | BITE GUI: host automation … | knob positions, selected button, readings for GR / +4 / +8 |
| VU needle on the printed marks | same | angle at every printed mark ±0.001° |
| Screenshots at 75/100/150/200 % | BITE GUI: editor opens at every scale | `BITE_editor_*.png` |
| DSP: static curves, attack/release ranges, ALL mode, FET THD, Character, GR telemetry, latency, bypass, NaN, state | `tests/BiteTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/BITE/` (references need Andrew's approval first).
- Golden audio renders; mutation tests (disconnect Attack, freeze GR telemetry, make ratio buttons visual-only).
- GUI-open vs GUI-closed bit-identical render; offline render; CPU p95/p99 per instance count.
- `TEST_REPORT.html` generated from `report.json`.

## Manual QA in Studio Pro 8

1. Insert BITE on a drum bus; turn Attack and Release: the pointer moves, the labels stay, the sound gets faster clockwise.
2. Click each ratio button: only that lamp lights; automation lane for Ratio moves the lit button.
3. Click the VU: GR → +4 → +8; in GR the needle falls left with compression, in +4/+8 it follows the output level.
4. Character: three clicks of the stepped knob, Clean / TLEET / Burn.
5. Save, reopen the project: values, meter mode and editor size are restored.
