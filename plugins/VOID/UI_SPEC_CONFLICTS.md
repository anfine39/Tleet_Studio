# VOID — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the `Void.png` skin). Rule: the specification
defines behaviour, the mockup defines placement and style. Nothing below has
an invented function. Where a question was open, the most faithful answer was
taken (Andrew, 2026-10-13).

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Baked pointers on all fourteen knobs, ROOM lamp lit, CLEAN and LOOP lit, FREEZE lit, DROWN ring lit to about 50 % | Values in the art | Pointers, lit lamps, lit buttons and the lit ring erased; each is drawn live from its parameter (pointer per knob, lit lamp / button of the real choice, ring lit from the start of the travel to the value, FREEZE lit while the parameter is on) | No |
| 2 | Readout windows printed "42 ms", "58 %", "8.4 s", "120 Hz", "0.0 dB" … | Values in the art | Printed text erased; each window prints the parameter's live text | No |
| 3 | End numbers under each knob (0 250, 0.1 ∞, 20 1K …) and the scale dots | Printed, not tied to the parameter | Erased and drawn live at the angle where the knob really has that value; 11 dots evenly over the travel | No |
| 4 | DECAY's scale ends in "∞" | The spec gives 0.1…60 s | Kept: the end of the travel is infinite decay (the loop holds its energy, input keeps entering; MODEL_SPEC). The readout prints "∞" there | No |
| 5 | ALGORITHM knob with a list of five lamps beside it | The spec has one ALGORITHM selector | One parameter, two views: the knob turns in five 60° steps (±120°), the lamps show the algorithm in use and a click on a lamp or its caption selects it | No |
| 6 | COLOR knob next to the CLEAN / DARK / RUIN buttons | Two controls for one setting in the spec | Same as 5: the knob turns in three 60° steps (±60°), the buttons light up for the setting in use and select it | No |
| 7 | Frequency labels under the display (20 100 1K 10K 20K) and under the filter graph | Printed off a log axis (up to 12 px) | Erased; drawn live on the analyser's and the graph's own log axis | No |
| 8 | Spectrum display over a ruins picture | The spec asks for real wet energy / spectrum | The picture stays as a static background; the live spectrum of the reverb signal (1/6 octave) is drawn over it. No animation of the picture | No |
| 9 | EARLY and LATE bars at the top of the display | Baked levels | Unlit on the plate; each lights up to the real level of the early reflections / the tail (−48…0 dBFS) | No |
| 10 | IN / OUT meters lit to about −9 | Baked levels | Unlit on the plate; four peak bars (IN L R, OUT L R) on the printed 0 −6 −12 −24 −48 scale | No |
| 11 | Filter graph with a curve and grid | Baked curve | Interior cleared; the exact response of HPF + LPF as they run is drawn live on a log axis (−24…+6 dB) | No |
| 12 | Preset bar printed "Eternal Room" with arrows, save and delete icons | Not in the spec; the toolbar already has presets | The printed bar works: arrows step the presets, a click on the name opens the preset list, save opens the save menu, delete removes the current user preset after a confirmation (dimmed for factory presets). The name is drawn live; "Eternal Room" is a factory preset | No |
| 13 | DROWN "MORE SPACE / LESS REALITY" caption, star emblem, VOID logo, ruins picture | Decoration | Static, no hitbox, no animation | No |
