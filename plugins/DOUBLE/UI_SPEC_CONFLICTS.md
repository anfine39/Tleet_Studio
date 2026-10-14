# DOUBLE — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Readouts "12 cents", "18 ms", "42 %", "128 %", "20 Hz", "1.2 kHz", "78 %", "0.0 dB" | Baked values, and most did not match their pointers (Detune pointer at 0 reads "12 cents") | Readout windows emptied by `prep_double.py`; the live parameter text is drawn there | No |
| 2 | HPF knob with two pointers | Artwork glitch | Both removed; one live pointer | No |
| 3 | Filter display: curve, grid and labels "20 100 1K 10K 20 20K" | The labels did not sit at their frequencies on the plot (1K was at ~650 Hz), "20 20K" was garbled, and the curve did not match the 1.2 kHz LPF readout | Screen emptied; grid, exact live curve and labels 20 · 100 · 1K · 5K · 20K drawn at their real positions (10K and 20K overlap on a true log axis) | No |
| 4 | Waveform in the central display, bright dots on the dotted line | Baked picture of a signal | Removed; the display shows the real voices (see UI_MAP.md) | No |
| 5 | Dim dots beyond the end labels of HPF / LPF / MIX / OUTPUT (±139°) | Look like scale ends but the bright dots at ±118° match the top-row knobs and the labels | Travel ±118° (bright end dots); dim dots kept as static art | Yes, if the dim dots were meant as the ends |
| 6 | VOICES 1 / 2 / 4 | Meaning not given in the spec | Number of **added** voices next to the dry signal (spec review, item 9) | Already agreed |
| 7 | Lit "2", "WIDE", "MONO SAFE" | A lamp must show the real state | Plate shows all released; the lit look of each button is rebuilt from the lit button of its group with its own label | No |
| 8 | Photographs, "make it wider / make it real", taglines, stars, "KALININGRAD" | Static decoration | Static | No |
