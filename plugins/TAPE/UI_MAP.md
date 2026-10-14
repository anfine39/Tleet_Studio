# TAPE — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Reference: `ui/UI_REFERENCE.png`. Faceplate and sprites:
`assets/processed/TAPE/plate.png`, the lit buttons, LED segments, VU caps and
reels (`*.png`) and where they go (`sprites.json`), made by
`tools/skinprep/prep_tape.py` (`--debug` writes `ui/UI_OVERLAY_DEBUG.png`).

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor. Angles: 0° = 12 o'clock, clockwise.

| control_id | parameter_id | type | centre / rect (ref px) | travel | range / default | test id |
|---|---|---|---|---|---|---|
| input | input | rotary (large) | 108.7, 319.6 ; readout 53, 416, 110, 37 | ±145.4° | −12…+24 dB / 0 | tape.knob.input |
| output | output | rotary (large) | 1428.8, 319.0 ; readout 1373, 416, 110, 37 | ±145.4° | −24…+24 dB, 0 at the centre / 0 | tape.knob.output |
| bias | bias | rotary | 778.6, 573.8 ; readout below | ±142.2° | −100…+100 % / 0 | tape.knob.bias |
| saturation | saturation | rotary | 927.7, 573.3 | ±142.2° | 0…100 % / 42 | tape.knob.saturation |
| headBump | headBump | rotary | 1074.1, 573.4 | ±142.2° | 0…100 % / 36 | tape.knob.headbump |
| lf | lf | rotary | 1233.0, 573.7 | ±142.2° | −100…+100 / 0 | tape.knob.lf |
| hf | hf | rotary | 1411.3, 573.3 | ±142.2° | −100…+100 / 0 | tape.knob.hf |
| age | age | rotary | 97.5, 762.6 | ±142.2° | 0…100 % / 18 | tape.knob.age |
| wow | wow | rotary | 249.9, 763.1 | ±142.2° | 0…100 % / 6 | tape.knob.wow |
| flutter | flutter | rotary | 400.2, 763.3 | ±142.2° | 0…100 % / 4 | tape.knob.flutter |
| noiseAmount | noiseAmount | rotary | 640.2, 764.6 | ±142.2° | 0…100 % / 12 | tape.knob.noiseamount |
| crosstalk | crosstalk | rotary | 793.7, 763.2 | ±142.2° | 0…100 % / 8 | tape.knob.crosstalk |
| mix | mix | rotary | 939.8, 763.5 | ±142.2° | 0…100 % / 100 | tape.knob.mix |
| tapeType | tapeType | 3 lit buttons | A 52, 541, 64, 56; B 120; C 182 | — | A / B / C, A | tape.buttons.tapetype |
| speed | speed | 3 lit buttons | 7.5 273, 541, 59, 56; 15 335; 30 387 | — | 7.5 / 15 / 30 IPS, 15 | tape.buttons.speed |
| calibration | calibration | 4 lit buttons | −3 476, 541, 50, 56; 0 531; +3 582; +6 633 | — | −3 / 0 / +3 / +6 dB, 0 | tape.buttons.calibration |
| noise | noise | 2 lit buttons | OFF 511, 725, 66, 45; ON 511, 778, 66, 44 | — | off | tape.buttons.noise |
| autoGain | autoGain | lit push button | 1035, 740, 76, 57 | — | on | tape.button.autogain |
| vuRecord | — (RECORD VU, L and R) | 2 needles | face 945, 242, 178, 115; pivot 1030.2, 422.4 | −20…+3 VU at the printed marks | 0 VU = −18 dBFS | tape.vu.record |
| vuRepro | — (REPRO VU, L and R) | 2 needles | face 1140, 242, 176, 115; pivot 1223.3, 423.3 | same | same | tape.vu.repro |
| saturationMeter | — (saturation activity) | 24 LED segments | x 1022 + 9.217 i, y 413, 6×20; red from the 16th | — | round(24 × activity) | tape.meter.saturation |
| tapeCondition | — (response + output spectrum) | line + analyser | screen 1164, 719, 329, 99; labels 1158, 827, 344, 14 | — | 20 Hz…20 kHz log; line ±12 dB | tape.display.condition |
| reelLeft / reelRight | — (transport state, IPS) | turning reels | flange centres 387.4, 246.5 (R 154) and 759.1, 247.7 (R 147.6); visible below y 186.5 | — | IPS / (2π × 3.5 in) turns/s | tape.reel.left / .right |

Knob scale numbers and dots are drawn live (11 dots on every knob, evenly
over the travel; the numbers at the angle where the knob really has that
value). Every readout window prints the parameter's own text. Every knob
also takes vertical drag (Shift = fine), the mouse wheel, the arrow keys,
double-click to type a value and right-click for reset / copy / paste.
Presets live in the toolbar (the skin has no preset bar).
