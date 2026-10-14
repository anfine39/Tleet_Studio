# VEIL — GUI and audio test plan

DSP tests: `tests/VeilTests.cpp`; GUI tests: `tests/VeilGuiTests.cpp`. All run
on every build (Linux under Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Editor opens at 75/100/150/200 %, plus states (gentle GR, dual mono VU, heavy EXT IN/OUT) | VEIL GUI: editor opens at every scale | `VEIL_editor_*.png`, `VEIL_state_*.png` |
| 13 knobs, 8 lit buttons, the link lamp and DUAL MONO, the spectrum at mockup positions; click areas never overlap (VU faces included); the FFT button is in the toolbar | VEIL GUI: click areas | hit tests, pairwise intersection |
| Scale numbers are real values at their real angles | VEIL GUI: scale numbers | label ≤ 1° from its ray; pointer at the label's value ≤ 0.05°; texts checked |
| Automation and keys move every knob; button clicks select; only the real value is lit; link lamp and DUAL MONO are one switch; SIDECHAIN lamps show the key really in use (faint EXT with no key) | VEIL GUI: knobs, lamps and the link switch | parameter values; lit/unlit brightness ≥ 1.6× and red glow ≥ 3× |
| GR needles = measured level drop per channel; dual mono with a left-only signal leaves the right needle at 0, linked moves both; VU = output level, IN/OUT = input level; rest positions; METER never changes the audio | VEIL GUI: needles | shown vs measured ≤ 0.5 dB (GR), ≤ 0.3 VU (levels); bit-identical outputs per mode |
| Spectrum reads the real output level of a sine at 100 Hz, 1 kHz, 5 kHz in every quality; the peak is narrow; Off and a hidden or closed editor stop the tap; the quality is restored with the editor; Off / Eco / High give bit-identical audio | VEIL GUI: spectrum | shown vs output ≤ 1 dB; ≥ 30 dB down two octaves away |
| DSP: law, ratio growth, timing, link, telemetry, both HPFs, sidechain, harmonics, aliasing, MIX vs DRY/WET, latency, bypass, NaN, state, presets | `tests/VeilTests.cpp` | see `MODEL_SPEC.md` |

## Still to add (tracked, release-blocking)

- Screenshot regression against `tests/golden/VEIL/` (references need Andrew's approval first).
- Golden audio renders; GUI-level mutation tests.
- Offline render; CPU p95/p99 with the analyser at High.

## Manual QA in Studio Pro 8

1. VEIL on a bus, THRESHOLD down: both needles fall from 0 by the dB of real reduction; deeper reduction gets firmer.
2. DUAL MONO with a hard-panned source: only that side's needle moves; the link lamp goes dark.
3. METER VU / IN/OUT: output and input levels; the sound never changes.
4. HPF up: the lows thin out in the spectrum; SC HPF up: less pumping from the kick, the lows stay.
5. Route a key to the sidechain input, EXT: EXT lights; remove the key: EXT glows faintly, INT lights.
6. FFT button: Off stops the curve; Eco / Normal / High differ only in detail and smoothness.
7. Save, reopen: values, editor size and FFT quality are restored.
