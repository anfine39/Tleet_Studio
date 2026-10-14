# STEEL — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the `Steel.png` skin). Rule: the specification
defines behaviour, the mockup defines placement and style. Nothing below has
an invented function. Where a question was open, the most faithful answer was
taken (Andrew, 2026-10-13).

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Baked pointers on all seven knobs; STEEL, AUTO, 4x, SUB 20, TRUE PEAK and SOFT CLIP lit | Values in the art | Pointers and lit buttons erased; each is drawn live from its parameter. DELTA and LEVEL MATCH (dark in the art) get lit images made from the lit TRUE PEAK button | No |
| 2 | Readout windows printed "+6.4 dB", "-1.0 dBTP", "180 ms", "28 %" … | Values in the art | Printed text erased; each window prints the parameter's live text | No |
| 3 | Scale numbers (20 250 1K, 0 10, 0 100, -12 +12) and dots | Printed, not tied to the parameter; the small knobs' printed dots are unevenly spaced | Erased and drawn live: the numbers at the angle where the knob really has that value, 13 / 11 dots evenly over the travel. RELEASE: 250 is printed at the top, so the parameter is skewed to have 250 ms at the centre | No |
| 4 | History display: "-60s … 0" time axis | The addendum (§14) asks for a GR history of roughly 5–10 s | 10 s, labels −10s −7.5s −5s −2.5s 0 at the printed positions | No |
| 5 | History display: waveform, red GR line, dB scale "0 -6 -12 -28 -34 -36 -42 -48" | Baked trace; the printed scale is not monotonic in 6 dB steps | Erased. Drawn live from the processor's history slices: output peak and RMS envelope (dBFS, from the bottom), the applied gain reduction (red, falling from the GR line at the top, same dB per pixel), the ceiling (dashed). Scale 0…−48 in 6 dB steps | No |
| 6 | Right scale "0 -6 -12 -18 -24 -36 -42 -48" (no −30) | Misprint | Drawn live in 6 dB steps (0…−48) on the same scale as the bars | No |
| 7 | IN and OUT each have two pairs of bars, lit | Baked levels; the spec lists sample peak and true peak | Unlit on the plate. Left pair = sample peak L / R, right pair = true peak L / R (BS.1770 estimator), IN after INPUT, OUT at the plugin output | No |
| 8 | Loudness panel "-9.8 / -8.6 / 6.4 / -1.0 / -3.2" | Values in the art | Erased; LUFS-I, LUFS-S, LRA, highest true peak since reset and the gain reduction (deepest applied GR of the last 0.3 s) drawn live. A click on the panel resets the measurement (the spec's loudness reset; the skin has no reset button). TRUE PEAK turns red while the output exceeds the ceiling | No |
| 9 | Spectrum at the bottom with a curve and frequency labels | Baked curve | Interior cleared; the live spectrum of the output (1/6 octave), grid and labels on the analyser's own log axis | No |
| 10 | Warnings (TP OVERS, HEAVY LIMITING) | Required by the addendum, not on the mockup | Drawn in the top-right corner of the history display only while the processor raises them | No |
| 11 | No preset bar on the skin | — | Presets in the toolbar, as in every TLEET plugin | No |
| 12 | Logo, ruins picture, "MORE LOUDNESS / SAME EMOTIONS", "MASTER THE FEELING", "LIMIT PRESERVE BREATHE RELEASE", star emblems, 鋼 | Decoration | Static, no hitbox, no animation | No |
