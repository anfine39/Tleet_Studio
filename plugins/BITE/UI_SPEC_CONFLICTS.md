# BITE — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Attack scale ".1 .3 1 3 10 ms", Release ".1 .3 1 3 10 s", fast end on the left | The spec (13.2) gives 20–800 µs and 50–1100 ms, clockwise = faster, as on the hardware. The printed numbers were neither the real times nor the right direction | Printed numbers removed by `prep_bite.py`; the real time at each dot is drawn live from the parameter range; units "µs" / "ms" | Approved by Andrew ("Реальное время") |
| 2 | All five ratio lamps lit, ALL pressed | A lamp must show the selected ratio only | Faceplate gets dark lamps and a released ALL button; the selected button is drawn pressed and lit | No |
| 3 | No meter-mode switch on the art | The spec requires real GR / +4 / +8 switching | **Click on the VU** cycles the mode; the current mode is drawn live on the face | Yes: confirm the click-on-meter approach (same as ASH) |
| 4 | Caption "TYPE 76" in the header | Reads as a reference to a trademarked product | Kept as static art | Yes: suggest another caption in the next skin revision |
| 5 | Knob travel (Attack −114…+87°, Release −114…+86°, Mix ±106°, Character ±65°) | Not the 270–300° of the general spec | Followed the **printed dots** so the pointer hits them | No, unless the scales are redrawn |
| 6 | Side handles, screws, star badge, "FAST CIRCUITS / BIGGER TONE / TLEET AUDIO MMXXIV" plate | Static decoration | Static, no interaction, no animation | No |
| 7 | Baked pointers and needle in the PNG | Values must never be baked in | Removed by `tools/skinprep/prep_bite.py`; drawn at runtime | No |
| 8 | Hardware "all buttons in" mode | On the hardware it is several buttons pressed together; here it is the ALL button | ALL is a fifth ratio state (spec 13.2) | No |
