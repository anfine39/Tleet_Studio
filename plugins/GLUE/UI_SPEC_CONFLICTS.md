# GLUE — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Printed scale numbers (Attack ".1 .1 3 10 30", Release ".1 .3 1 3 10 s", SC HPF "20 160 100 300", Ratio without 2) | Garbled by the image generator; none match the real switch positions or the log HPF range | All printed numbers and step dots removed by `prep_glue.py`; real values drawn live at their real angles (as BITE, "Реальное время") | No (same rule as BITE) |
| 2 | Release has no Auto position | The hardware has Auto; the spec says "Auto release if included by selected hardware revision" (MODEL_SPEC.md) | Auto is the 5th release step, label "AUTO" | No |
| 3 | Ratio shows 1.5 and ∞ | The G-bus switch has 2 / 4 / 10 only | 1.5 and ∞ kept as TLEET extensions from the skin, documented in MODEL_SPEC.md | Yes: confirm 1.5 and ∞ |
| 4 | No external sidechain switch on the art | Spec §12: external SC required from v1 | "EXT SC" switch in the toolbar, lamp lit only while the detector really listens to the sidechain input; outlined lamp = switched on but the host sends no key | Approved by Andrew |
| 5 | Lit GR bar (8 dB) and the lit TLEET lamp baked into the art | Values must never be baked in | Removed; drawn live from the processor | No |
| 6 | Amber slots beside the meter, tall bar at the meter's right end | No function in the spec | Static artwork, never animated | No, unless you want them removed |
| 7 | Knob travel ±120…±143°, Character ±74° | Not the 270–300° of the general spec | Followed the printed dots so pointers hit them | No, unless the scales are redrawn |
| 8 | Header "PRESSURE / COHESION / CHARACTER", "TLEET AUDIO MMXXIV" | Decoration | Static | No |
