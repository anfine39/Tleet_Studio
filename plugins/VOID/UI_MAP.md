# VOID — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Reference: `ui/UI_REFERENCE.png`. Faceplate and sprites:
`assets/processed/VOID/plate.png`, the lit lamps, buttons, DROWN ring and
FREEZE face (`*.png`) and where they go (`sprites.json`), made by
`tools/skinprep/prep_void.py` (`--debug` writes `ui/UI_OVERLAY_DEBUG.png`).

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor. Angles: 0° = 12 o'clock, clockwise.

| control_id | parameter_id | type | centre / rect (ref px) | travel | range / default | test id |
|---|---|---|---|---|---|---|
| algorithm | algorithm | 5-position selector | 137.8, 317.6 | ±120° | Room, Plate, Chamber, Hall, Void / Hall | void.knob.algorithm |
| colorKnob | color | 3-position selector | 1422.6, 317.5 | ±60° | Clean, Dark, Ruin / Clean | void.knob.color |
| preDelay | preDelay | rotary | 117.6, 564.4 ; readout 73, 636, 82, 27 | ±144° | 0…250 ms (60 at centre) / 20 | void.knob.predelay |
| size | size | rotary | 267.8, 564.3 ; readout 230, 636, 77, 27 | ±144° | 0…100 % / 50 | void.knob.size |
| decay | decay | rotary | 421.1, 564.0 ; readout 383, 636, 75, 27 | ±144° | 0.1…60 s log, max = ∞ / 2.5 | void.knob.decay |
| diffusion | diffusion | rotary | 578.9, 564.2 ; readout 540, 636, 75, 27 | ±144° | 0…100 % / 70 | void.knob.diffusion |
| modRate | modRate | rotary | 1018.9, 564.3 ; readout 980, 636, 85, 27 | ±144° | 0.01…5 Hz log / 0.5 | void.knob.modrate |
| modDepth | modDepth | rotary | 1207.7, 564.2 ; readout 1170, 636, 75, 27 | ±144° | 0…100 % / 30 | void.knob.moddepth |
| width | width | rotary | 1398.0, 564.4 ; readout 1360, 636, 76, 27 | ±144° | 0…200 % / 100 | void.knob.width |
| hpf | hpf | rotary | 109.2, 789.2 ; readout 72, 848, 78, 25 | ±140.5° | 20…1000 Hz log / 20 | void.knob.hpf |
| lpf | lpf | rotary | 248.9, 789.5 ; readout 210, 848, 78, 25 | ±140.5° | 1…20 kHz log / 20000 | void.knob.lpf |
| duck | duck | rotary | 992.0, 790.6 ; readout 957, 850, 71, 26 | ±140.5° | 0…100 % / 0 | void.knob.duck |
| mix | mix | rotary | 1149.1, 790.4 ; readout 1115, 850, 70, 26 | ±140.5° | 0…100 % / 35 | void.knob.mix |
| output | output | rotary | 1428.0, 790.4 ; readout 1390, 850, 78, 26 | ±140.5° | −12…+12 dB / 0 | void.knob.output |
| algorithmLamps | algorithm | 5 lamps + captions | lamps x 232.0, rows 254.8, 290.5, 326.25, 361.75, 397.5 | — | as ALGORITHM | void.lamps.algorithm |
| color | color | 3 lamp buttons | CLEAN 1254, 244, 82, 45; DARK 1255, 301, 82, 45; RUIN 1255, 359, 82, 46 | — | Clean / Dark / Ruin | void.buttons.color |
| filterMode | filterMode | 3 lamp buttons | PRE 340, 741, 70, 31; LOOP 340, 778, 70, 31; POST 340, 815, 70, 31 | — | Pre / Loop / Post, Loop | void.buttons.filtermode |
| drown | drown | knob with lit ring (no pointer) | 781.7, 620.0; ring r 106.5…115.5 | ±130.5° | 0…100 % / 0 | void.knob.drown |
| freeze | freeze | lit push button | face 1268, 762, 54, 60 | — | off / on, off | void.button.freeze |
| spectrum | — (wet spectrum tap) | analyser over the picture | screen 386, 218, 661, 174; labels 386, 397, 661, 20 | — | 20 Hz…20 kHz log, 0…−84 dBFS | void.display.spectrum |
| earlyLate | — (early / late levels) | 2 level bars | EARLY 455, 206, 76, 6; LATE 919, 206, 76, 6 | — | −48…0 dBFS | void.display.earlylate |
| meters | — (input / output peaks) | 4 bars IN L R, OUT L R | x 1078, 1087, 1136, 1145; y 230…402 | — | printed scale 0 −6 −12 −24 −48 dBFS | void.meters |
| filterGraph | — (HPF + LPF response) | curve | plot 439, 728, 202, 109 | — | 20 Hz…20 kHz log, −24…+6 dB | void.display.filter |
| presetBar | — (presets) | prev, name, next, save, delete | frame 553, 72, 435, 41 | — | — | void.presets |

Knob scale numbers and dots are drawn live (end values at the ends of the
travel, 11 dots evenly over it; the selectors get one dot per real position).
Every readout window prints the parameter's own text ("42 ms", "8.40 s", "∞",
"8.5 kHz", "+3.0 dB"). Every knob also takes vertical drag (Shift = fine),
the mouse wheel, the arrow keys, double-click to type a value and right-click
for reset / copy / paste.
