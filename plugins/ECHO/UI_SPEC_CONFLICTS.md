# ECHO — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the `Echo.png` skin). Rule: the specification
defines behaviour, the mockup defines placement and style. Nothing below has
an invented function. Where a question was open, the most hardware-faithful
answer was taken (Andrew, 2026-10-13).

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | MODE ring with nine lamps: ECHO+REVERB, REVERB, 1, "1·2", 3, "1·2", 1+3, 2+3, 1+2+3 | Art errors ("1·2" twice, no 2 and no 1+2); four lamps lit at once; ECHO+REVERB has no mode behind it (REVERB VOLUME adds the spring in every mode, as the RE-201's reverb level does) | Eight real positions (REVERB, 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3) on the eight printed lamps from REVERB clockwise, 30° apart; the ECHO+REVERB lamp, its caption and the two dots beyond it are removed. All captions drawn live from the parameter. Every lamp dark on the plate; the selected one is lit, brighter while its heads play signal | No |
| 2 | Baked pointers on all eighteen knobs (MODE pointing between two positions) | Values in the art | Erased (pointer, glow, notch on the skirt); one live pointer per knob | No |
| 3 | REPEAT RATE printed 20 50 200 200 500 1K 2K | "200" twice; the scale is logarithmic with 200 ms at the centre | Printed numbers erased; 20 50 100 200 500 1K 2K drawn live at their real angles. 13 dots, one per sixth of a decade | No |
| 4 | Every other scale number (0 10, WOW and FLUTTER 0 10 under a "%" caption, −10 0 +10, HPF 20 20 50 100 300, LPF 1K 1K 3K 10K 20K, 0 100, −12 0 +12) | HPF and LPF print 20 and 1K twice; WOW and FLUTTER are 0…100 % but print 0 10; printed dots unevenly spaced | All printed numbers and dots erased (WOW and FLUTTER read 0 100); numbers drawn live at their real angles, 11 dots evenly over the travel (13 on OUTPUT: one per 2 dB) | No |
| 5 | REPEAT RATE in SYNC | The note value sets head 1's time in SYNC | REPEAT RATE stays turnable but is dimmed (pointer, numbers and dots at 40 %) while SYNC is on | No |
| 6 | Lit buttons: NOTE DIVISION 1/8, FEEL STRAIGHT, CHARACTER ORIGINAL | Baked state | Every button dark on the plate; the lit image of the real choice is drawn live (made from the mockup's lit button and each button's own caption) | No |
| 7 | TIME MODE and HOST BPM slide switches shown at SYNC / HOST | Baked state | A whole sprite per position (the second one is the mockup's switch mirrored); a click on the switch toggles, a click on a caption selects that side | No |
| 8 | FILTER MODE switch lit at LOOP | Baked state | Three dark cells on the plate; the lit cell drawn at the real position; a click on a cell or a caption selects it | No |
| 9 | BPM display printed "120" | Value in the art | Digits erased; the tempo SYNC really uses (host or MANUAL BPM) is drawn live as italic LED digits, one decimal when the host's tempo has one. Dimmed in FREE; blinks while SYNC clamps the note to the tape's range. The arrows step MANUAL BPM and switch the source to MANUAL | No |
| 10 | TAP lamp lit | Baked state | Dark on the plate; flashes once per real head-1 period (from the processor's head-delay telemetry) | No |
| 11 | TAPE CONDITION bar lit to between CLEAN and WORN | Baked state; the parameter has three conditions | The bar is unlit on the plate; it lights up to the mark of the condition in use (CLEAN, WORN, MAGNETIC DUST). A click picks the nearest mark | No |
| 12 | OUT meter scale printed "−2" where the IN meter has "−3" | Art error (a VU scale reads −20 −10 −7 −5 −3 −1 0 +1 +2 +3) | The IN meter's "−3" is set in its place; both needles follow the measured printed marks | No |
| 13 | Baked VU needles | Values in the art | Erased; IN shows the plugin input, OUT the plugin output, 0 VU = a sine at −18 dBFS | No |
| 14 | Tape window with reels and heads at the top | No function in the spec | Static picture, no hitbox, no animation (GUI rule: no decorative animation) | No |
| 15 | "FILTER MODE ." | Stray dot printed after the caption | Kept as printed (part of the artwork) | No |
