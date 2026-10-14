# ASH — GUI and audio test plan

All tests live in `tests/AshTests.cpp` and run on every CI build (Linux under
Xvfb, Windows, macOS). Artefacts go to `test-output/`.

## Automated now

| Requirement | Test | Evidence |
|---|---|---|
| Every control at its mockup position, rotary centre ±1 px | ASH GUI: every mapped control sits at its mockup position | hit-tests centre and 4 inner corners of every hitbox |
| Hitboxes never overlap | same | pairwise intersection check |
| Drag, Shift fine drag, wheel, Alt-click reset, lever clicks change the parameter **and** the audio | ASH GUI: mouse drag, fine drag, wheel, reset and lever clicks | parameter value + measured GR / output level |
| Host automation moves knob pointer and lever; VU mode switch changes the measured quantity | ASH GUI: host automation … | knob angle, lever state, needle reading for a known signal |
| VU calibration | ASH: VU meter calibration | 0 VU = −18 dBFS (+4), −12 dBFS (+10) |
| GR = applied gain | ASH: GR telemetry equals the attenuation measured on the audio | tolerance 0.05 dB (measured error 0.001 dB) |
| Screenshots at 75/100/150/200 % | ASH: editor opens at every scale | `ASH_editor_*.png` |
| State restore incl. A/B, preset, UI settings, identical render | ASH: state round trip | bit-identical render |
| 5 sample rates × 9 block sizes × mono/stereo, zero allocations | ASH: all sample rates and block sizes | allocation counter |
| Latency reported = measured; bypass and Mix 0 bit-exact | ASH: reported latency … | impulse |
| Attack/release, program dependency | ASH: attack is ~10 ms and release is history dependent | measured times |
| Static curves | ASH: Compress and Limit are different static laws | `ASH_static_curves.csv` |
| THD vs Character | ASH: colouration exists without compression … | THD values |
| NaN/Inf, silence, DC | dedicated tests | |
| Automation stress, click-free | ASH: rapid automation … | max sample step |

## Still to add (tracked, release-blocking)

- Screenshot **regression**: pixel + perceptual diff against
  `tests/golden/ASH/` with saved diff images (references need Andrew's approval
  first; they are never auto-regenerated).
- Golden audio renders and reference measurements in `tests/golden/ASH/`.
- Mutation tests: disconnect Peak Reduction, freeze GR telemetry, make the lever
  visual-only; each must make a test fail.
- GUI-open vs GUI-closed bit-identical render.
- Offline (non-realtime) render pass.
- CPU: p95/p99/max per block for 1/8/16/32 instances.
- Numeric entry through the knob's text box (implemented; test pending).
- `TEST_REPORT.html` generated from `report.json`.

## Manual QA in Studio Pro 8 (until host tests are automated)

1. Insert ASH on a vocal track, play, turn Peak Reduction to 60: the needle in
   GR mode drops by roughly 5–10 dB on loud phrases and recovers slowly.
2. Click the VU: caption changes GR → +4 → +10; with a −18 dBFS RMS sine the
   needle sits at 0 VU in +4 and at −6 VU in +10.
3. Automate Peak Reduction and Mode: knob and lever follow during playback.
4. Save the song, close, reopen: every control, A/B slot and meter mode restored.
5. Bypass: the null against the dry track is silent.
