# PUNCH — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Compression scale "1, 4, 4, 8, 20" | Garbled (4 twice) and ends at 20, while the spec asks for the documented continuous ratio up to ∞ | Printed numbers removed; 1 2 4 8 ∞ drawn live at their real positions; the knob reaches ∞:1 at the right end | Approved by Andrew (∞ instead of 20) |
| 2 | Other printed scale numbers (Threshold, Mix, Output) | Values must be drawn from the parameter | Removed and drawn live at their real angles; the printed dots stay | No |
| 3 | OverEasy lamp lit on the artwork | A baked state | Plate lamp darkened; lit only while OverEasy is on | No |
| 4 | Lit GR bar (8 dB) | Baked value | Removed; drawn live from the applied VCA gain | No |
| 5 | Meter: 22 equal segments under a scale that is expanded below 4 dB | Equal segments cannot follow that scale | Segments follow the printed scale: 0.5 dB each to 4 dB, 1 dB each to 20 dB (24 segments) | No |
| 6 | No attack / release controls | dbx 160A has none: program-dependent timing (spec 13.4) | Nothing added | No |
| 7 | Amber slots beside the meter, bar at the window's right edge | No function in the spec | Static artwork | No |
| 8 | Knob travel ±112…±127° | Not the 270–300° of the general spec | Followed the printed end dots | No |
