# ECHO — GUI and audio test plan

DSP tests: `tests/EchoTests.cpp`; GUI tests: `tests/EchoGuiTests.cpp`. All
run on every build (Linux under Xvfb, Windows, macOS). Artefacts go to
`test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (SYNC dotted, dub Burn, reverb only) | ECHO GUI: editor opens at every scale | `ECHO_editor_*.png`, `ECHO_state_*.png` |
| 18 knobs, 12 buttons, both slide switches with their captions, FILTER MODE with its captions, TAP, the BPM arrows, the tape bar and both VU faces at mockup positions; click areas of different controls never overlap | ECHO GUI: click areas | hit tests, pairwise intersection |
| Scale numbers and dots are real values at their real angles; MODE's pointer stops on the printed lamps | ECHO GUI: scale numbers and dots | label and dot ≤ 1° from their ray; pointer at the value ≤ 0.05°; texts checked; MODE ≤ 2° from the lamps |
| Automation and keys move every knob; clicks on buttons, switch bodies and captions, FILTER MODE cells and captions, tape-bar marks, BPM arrows and TAP set the real parameters | ECHO GUI: every control drives its real parameter | parameter values, lit lamps |
| MODE lamp at the selected position, brighter with head signal; REPEAT RATE dimmed only in SYNC; BPM digits = the tempo in use; TAP flash per head-1 period; tape bar lit to the condition's mark; VU 0 at a −18 dBFS sine on the printed 0 | ECHO GUI: lamps, displays and meters | glow values, screenshot brightness, display text, needle VU |
| Every visible control changes the rendered audio | ECHO GUI: no dead controls | change > −40 dB per control |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/ECHO/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.
- Offline render; CPU p95/p99.

## Manual QA in Studio Pro 8

1. MODE through all eight positions with INTENSITY 4: the heads' rhythm changes; REVERB is the spring alone.
2. REPEAT RATE turned while audio plays: the echoes bend in pitch (tape speed), no clicks.
3. TIME MODE SYNC: echoes on the host grid for every NOTE DIVISION and FEEL; HOST BPM follows the project tempo; MANUAL with the arrows and TAP.
4. INTENSITY up to self-oscillation, then back down.
5. FILTER MODE PRE / LOOP / POST with HPF and LPF: in LOOP every repeat gets darker / thinner.
6. TAPE CONDITION, AGE, WOW, FLUTTER, DRIVE, CHARACTER: the tape's colour and wear.
7. MIX 0 is the dry signal; OUTPUT ±12 dB; IN and OUT meters read 0 VU at −18 dBFS.
