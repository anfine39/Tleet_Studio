# STEEL — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Reference: `ui/UI_REFERENCE.png`. Faceplate and sprites:
`assets/processed/STEEL/plate.png`, the lit buttons (`*.png`) and where they
go (`sprites.json`), made by `tools/skinprep/prep_steel.py` (`--debug` writes
`ui/UI_OVERLAY_DEBUG.png`).

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor. Angles: 0° = 12 o'clock, clockwise.

| control_id | parameter_id | type | centre / rect (ref px) | travel | range / default | test id |
|---|---|---|---|---|---|---|
| input | input | rotary (large) | 140.9, 342.6 ; readout 80, 435, 119, 41 | ±136.7° | −12…+24 dB / 0 | steel.knob.input |
| ceiling | ceiling | rotary (large) | 1398.3, 342.7 ; readout 1340, 435, 119, 40 | ±136.7° | −12…0 dBTP / −1.0 | steel.knob.ceiling |
| release | release | rotary | 802.7, 584.0 ; readout 758, 630, 90, 27 | ±135.3° | 20…1000 ms, 250 at the centre / 180 | steel.knob.release |
| lookahead | lookahead | rotary | 946.7, 583.9 ; readout 902, 630, 90, 27 | ±135.3° | 0…10 ms / 3.0 | steel.knob.lookahead |
| stereoLink | stereoLink | rotary | 1428.9, 584.4 ; readout 1385, 631, 87, 27 | ±135.3° | 0…100 % / 100 | steel.knob.stereolink |
| softClipAmount | softClipAmount | rotary | 201.8, 743.3 ; readout 156, 795, 91, 27 | ±135.3° | 0…100 % / 30 | steel.knob.softclipamount |
| output | output | rotary | 1429.1, 749.8 ; readout 1386, 799, 86, 27 | ±135.3° | −12…+12 dB / 0 | steel.knob.output |
| mode | mode | 4 lit buttons | CLEAN 62, 573, 87, 49; PUNCH 171; DENSE 281; STEEL 391 | — | Clean / Punch / Dense / Steel, Steel | steel.buttons.mode |
| releaseMode | releaseMode | 2 lit buttons | AUTO 549, 572, 91, 50; MANUAL 649, 572, 91, 50 | — | Auto / Manual, Auto | steel.buttons.releasemode |
| oversampling | oversampling | 4 lit buttons | 1x 1152, 568, 42, 53; 2x 1199; 4x 1247; 8x 1296 | — | 1x / 2x / 4x / 8x, 4x | steel.buttons.oversampling |
| subFilter | subFilter | 4 lit buttons | OFF 298, 731, 37, 54; 20 341; 25 384; 30 426 | — | Off / 20 / 25 / 30 Hz, Off | steel.buttons.subfilter |
| truePeak | truePeak | lit push button | 1050, 576, 47, 45 | — | on | steel.button.truepeak |
| softClip | softClip | lit push button | 68, 734, 57, 45 | — | off | steel.button.softclip |
| delta | delta | lit push button | 1149, 735, 67, 53 | — | off | steel.button.delta |
| levelMatch | levelMatch | lit push button | 1258, 735, 72, 53 | — | off | steel.button.levelmatch |
| history | — (history slices) | output level, GR trace, ceiling, warnings | plot 400, 251, 720, 201 (0…−48 dB); GR line y 220; time labels y 461.5 | — | last 10 s | steel.display.history |
| meters | — (input / output peaks) | 2 × 4 bars: sample peak L R, true peak L R | IN x 291, 299, 315, 323; OUT x 1153, 1161, 1177, 1185; y 249…457 | — | 0…−48 dB at y 251…452 | steel.meters |
| loudness | — (BS.1770 meter) | 5 values | LUFS-I 490, 706; LUFS-S 611; LRA 728; TRUE PEAK 844; GR 967 (48 px high) | — | click = reset | steel.display.loudness |
| spectrum | — (output spectrum tap) | analyser | screen 251, 871, 615, 83; labels 240, 961, 640, 17 | — | 20 Hz…20 kHz log | steel.display.spectrum |

Knob scale numbers and dots are drawn live (13 dots on INPUT and CEILING, 11
on the small knobs, evenly over the travel; the numbers at the angle where
the knob really has that value). Every readout window prints the parameter's
own text. Every knob also takes vertical drag (Shift = fine), the mouse
wheel, the arrow keys, double-click to type a value and right-click for
reset / copy / paste. Presets live in the toolbar (the skin has no preset
bar).
