# WEIGHT — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png`. Rule: the specification defines behaviour, the
mockup defines placement and style. Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | GR meters printed as a VU scale: −20 −10 −7 −5 −3 −1 0 +1 +3 | A gain reduction is never positive, and the artwork lights the bar from the left as if it showed a level | Read as on the API 2500's own GR meter: g dB of reduction lights the bar from the 0 mark leftwards to the printed −g. 1 dB lights to −1, 3 dB to −3, 20 dB to −20. The segments right of 0 (+1, +3) never light and stay drawn dark | Approved by Andrew 2026-10-13 |
| 2 | One lit bar per meter (LEFT, RIGHT) | Addendum 10: WEIGHT must not show one linked value when L and R are reduced differently | Each meter shows its own channel's applied gain (`grLeft`, `grRight`); with the link below 100 % they differ | No |
| 3 | GR MODE PEAK / RMS / AVERAGE | Not in spec 13.5 | Display only, as the owner's brief asked: peak, 300 ms RMS or 300 ms average of the applied gain reduction; never changes the audio (tested) | No |
| 4 | THRESHOLD scale "−30, 0, −20, +10" | Garbled (−30 where −10 belongs) | Printed numbers removed; −20 −10 0 +10 drawn live at their real positions | No |
| 5 | ATTACK scale ".1 .3 .1 3 10 30" and an unlabelled first dot; RELEASE scale ".1 .3 1 3 10 30 s" (copied from ATTACK); RATIO "1.5, 1:1, 2, 4, 10, ∞" | Do not match the API 2500's switch positions | Printed numbers and dots removed. One live dot and number per real position: attack .03 .1 .3 1 3 10 30 ms, release .05 .1 .2 .5 1 2 s, ratio 1.5 2 3 4 6 10 ∞ | No |
| 6 | RELEASE has no VARIABLE position | The 2500 has one | Not modelled (no control for it on the skin), MODEL_SPEC.md | No |
| 7 | No knee switch | The 2500 has HARD / MED / SOFT | One fixed knee (MODEL_SPEC.md); a knee control would need your approval | No |
| 8 | DETECTOR: FEED FORWARD is a lamp in a wide housing, FEED BACK a plain lamp button | Two different button styles | Kept as drawn; each lamp lights only while its topology is active | No |
| 9 | Small marks under the middle lamp of THRUST, TONE and GR MODE | No function in the spec; they could be read as a selection marker | Static artwork (they mark the middle position, the selection is shown only by the lit lamp) | No |
| 10 | Lit lamps (THRUST NEUTRAL, DETECTOR FF, TONE NEUTRAL, GR MODE RMS, EXTERNAL SC) | Baked states | All lamps dark on the plate; a lamp is drawn lit only for the real value | No |
| 11 | EXTERNAL SC lamp | Lit on the artwork | Lit only while the external key is really used; switched on with no key connected it glows faintly (same rule as GLUE) | No |
| 12 | Amber slots at the left and right edges | No function in the spec | Static artwork | No |
