# TAPE — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the `Tape.png` skin). Rule: the specification
defines behaviour, the mockup defines placement and style. Nothing below has
an invented function. Where a question was open, the most hardware-faithful
answer was taken (Andrew's standing rule, 2026-10-13).

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Baked pointers on all thirteen knobs; A, 15, 0, NOISE OFF and AUTO GAIN lit | Values in the art | Pointers and lit buttons erased; each is drawn live from its parameter | No |
| 2 | Readout windows "+6.4 dB", "0 %", "42 %", "-12", "+18" … | Values in the art; BIAS printed "0 %" | Printed text erased; each window prints the parameter's live text (BIAS in % from the tape's optimum, LF / HF as signed trims) | No |
| 3 | Scale numbers and dots | Printed, not tied to the parameter; dots unevenly spaced (small knobs: gaps of 24…34°) | Erased and drawn live: the numbers at the angle where the knob really has that value, 11 dots evenly over the travel | No |
| 4 | MIX scale printed "10 … 100" | Misprint: MIX starts at 0 % | Drawn "0 … 100" | No |
| 5 | Each VU shows two needles (a dark one and a faint one) | Baked; one needle per meter or two? | Two live needles per meter: dark = L, light = R (the machine has two tracks). RECORD = after INPUT, before the record amplifier; REPRO = off the tape after the repro EQ, before AUTO GAIN; 0 VU = −18 dBFS sine (MODEL_SPEC). The lit wedge the art painted between its needles was smoothed out of the face | No |
| 6 | VU scale without a +2 mark | Printed scale | Needle angles follow the printed marks; +2 interpolated between +1 and +3 | No |
| 7 | SATURATION bar, 15 white + 3 red segments lit | Baked level | All 24 segments dark on the plate; lit live: round(24 × the deepest saturation of the magnetic layer since the last frame), white, red from segment 16 (as printed) | No |
| 8 | TAPE CONDITION: smooth curve and frequency labels 20 / 100 / 1K / 10K / 20K placed off a log axis | Baked curve; labels not on a real axis | Cleared. Line = the machine's real small-signal frequency response as set now (TapeProcessor::tapeResponseDb: speed, head bump, LF, HF, bias, AGE), ±12 dB around a dashed 0 dB line; shaded area = the real spectrum of the output (1/6 octave, toolbar FFT quality). Labels at their real log positions | No |
| 9 | Reels | The spec asks for reel animation; the art's reels are clipped by the window top and unevenly spaced (windows 74°/99°/187° apart), so they cannot simply turn | Rebuilt per reel as a three-window NAB reel from the reel's own picture (one window's half-sector mirrored and repeated every 120°), the hub, hub ring, label (left) and TLEET print (right) kept from the art, the light on the reels held fixed while they turn. They turn counter-clockwise (tape from the bottom of the supply reel to the bottom of the take-up reel) at IPS / (2π × 3.5 in) turns per second, only while the host plays (or, without playhead information, while there is input above −60 dBFS). UI only; nothing runs with the editor closed | No |
| 10 | Head block lamp, rollers, guides | Decoration | Static, no hitbox, no animation | No |
| 11 | No preset bar on the skin | — | Presets in the toolbar, as in every TLEET plugin | No |
| 12 | Logo, ruins pictures, "PAST THROUGH SOUND LIVES", 録音 block, "REAL MACHINES …", "imperfect by design", "MAGNETIC FIELDS …", star emblems | Decoration | Static, no hitbox, no animation | No |
