# STEEL — GUI and audio test plan

DSP tests: `tests/SteelTests.cpp`; GUI tests: `tests/SteelGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to
`test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (Steel loud, Clean manual with soft clip, Punch delta, Dense level match with TP off) | STEEL GUI: editor opens at every scale | `STEEL_editor_*.png`, `STEEL_state_*.png` |
| 7 knobs (with their readout windows), 14 buttons, 4 push buttons, history, IN / OUT meters, loudness panel and spectrum at mockup positions; click areas of different controls never overlap | STEEL GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and dots are real values at their real angles; RELEASE 250 ms sits at the top; readouts print the parameter text | STEEL GUI: scale numbers and dots | label and dot ≤ 1° from their ray; pointer at the value ≤ 0.05°; texts checked |
| Automation and keys move every knob; every button group has one button per real choice and sets it; the push buttons toggle their parameters | STEEL GUI: every control drives its real parameter | parameter values, lit buttons |
| IN / OUT bars read the real sample and true peak on the printed scale; history slices arrive at the processor's rate and show the applied GR and an output under the ceiling; the GR readout is the history's; warnings shown = the processor's flags; LUFS-I / LUFS-S / TRUE PEAK of a −23 dBFS 1 kHz sine read −23.0; a click resets; DELTA lit on screen; the spectrum peaks at the played frequency | STEEL GUI: meters, history, loudness and spectrum | dB values, texts, screenshot brightness |
| Every visible control changes the rendered audio | STEEL GUI: no dead controls | change > −60 dB per control |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/STEEL/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.

## Manual QA in Studio Pro 8

1. A finished mix: INPUT up until 3–6 dB of GR; the history shows the red GR trace, the output stays under the dashed ceiling, OUT true peak never above the ceiling.
2. MODE CLEAN / PUNCH / DENSE / STEEL on drums: transients and density differ; no click on switching.
3. AUTO vs MANUAL release; LOOKAHEAD 0…10 ms and OVERSAMPLING 1x…8x: the host's latency compensation keeps the track aligned.
4. DELTA: only what the limiter removes; LEVEL MATCH: the loudness of the input.
5. Loudness panel against Studio Pro's own loudness meter on the same material; click resets.
6. SUB FILTER 20 / 25 / 30 on a bass-heavy mix; STEREO LINK 0 vs 100 on a wide mix.
