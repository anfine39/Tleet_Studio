# ASH — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Two amber lamps left and right of the bottom panel | Look like status LEDs, but no state is defined for them | Kept as **static faceplate art**, no interaction, no animation | Yes: proposal to make them real IN/OUT signal-present lamps (lit when the input/output RMS > −40 dBFS) |
| 2 | Caption "TYPE 2A" in the header | Reads as a reference to a trademarked product | Kept as static art | Yes: suggest replacing with "TYPE A" or "MODEL 2" in the next skin revision |
| 3 | Small knob travel (Mix ±109°, Character ±92°, Output ±108°) | Master spec asks for 270–300° travel; the printed scales only span 184–218° | Followed the **printed scale** so the pointer hits the printed marks | No, unless the scales are redrawn |
| 4 | No meter-mode switch on the art | The spec requires real GR / +4 / +10 switching | Implemented as **click on the VU**; the current mode is drawn live at the bottom right of the meter face ("GR", "+4", "+10") | Yes: confirm the click-on-meter approach, or add a small switch to the skin |
| 5 | "GR VU" caption printed on the face | "GR" would be wrong in +4/+10 modes | "VU" stays printed; the mode text is drawn live next to it | No |
| 6 | Baked pointers, lever and needle in the PNG | Values must never be baked in | Removed by `tools/skinprep/prep_ash.py`; drawn at runtime from the parameters | No |
| 7 | Peak Reduction / Gain scales 0–100 | Matches hardware; values are unitless | Kept | No |
| 8 | Hardware's internal HF emphasis trim | Not on the panel | Not modelled | No |
| 9 | Screws, title plate, "OPTO LEVELING AMPLIFIER" | Static decoration | Static | No |
