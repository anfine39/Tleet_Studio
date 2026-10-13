# SHAPE — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the `Shape.png` skin, spec 13.7). Rule: the
specification defines behaviour, the mockup defines placement and style.
Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | HIGH FREQUENCY selector printed 3 4 5 10 13 12 16 | Art error: spec 13.7 lists 3 / 4 / 5 / 8 / 10 / 12 / 16 kHz | Printed numbers erased; 3 4 5 8 10 12 16 drawn live at the real positions (addendum §4: only real values) | No |
| 2 | BANDWIDTH scale printed 1 2 3 4 6 4 7 | Art error | Printed numbers and dots erased; 1…9 and one dot per step of 1 drawn live at their real angles. SHARP / BROAD captions kept | No |
| 3 | Dots of the three frequency selectors (LOW 20/30/60/100, HIGH 3…16, ATTEN SEL 5/10/20) | Off-centre and unevenly spaced: they did not sit where the switch stops | Erased; the editor draws one dot per real switch position, on the knob's own centre | No |
| 4 | Baked pointers on all twelve knobs | Values in the art | Erased (pointer, glow, notch on the skirt); one live pointer per knob | No |
| 5 | Lit EQ IN lamp | Baked state | Dark on the plate; the lit image (cut from the mockup) is drawn only while EQ IN is on. A click on the bezel toggles | No |
| 6 | EQ IN vs host bypass | Two "off" states | Spec 13.7: EQ IN off removes the passive EQ curves but keeps the transformers and the tube amplifier (the hardware's own bypass); the host's bypass is fully dry. Tested as two different outputs | No |
| 7 | CHARACTER knob with CLEAN / ORIGINAL / BURN printed on an arc | — | Three-position switch; the live pointer stops exactly on the printed marks (tested ≤ 1.5°). The small printed triangle at ORIGINAL is part of the art and stays static | No |
| 8 | Amber slot to the left of DRIVE | Spec names no function for it | Kept as printed decoration, no hitbox. Can become a level or drive indicator only with Andrew's approval | Yes, if wanted |
| 9 | No EQ curve display and no readout windows on the mockup | Addendum §12 asks for real transfer functions where curves are shown | Nothing is drawn that the mockup does not have; the transfer functions are checked in `tests/ShapeTests.cpp` (LF boost/atten families, the combined boost+atten shape, HF bandwidth sweep). A curve display would need a new place on the panel and approval | Yes, if wanted |
| 10 | DRIVE, MIX, OUTPUT scales print 0 50 100 / −20 0 +20 | — | Printed numbers erased and drawn live at the real angles; the printed dots stay (they sit at the real values) | No |
