# SHAPE — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.
Faceplate and the lit EQ IN lamp: `assets/processed/SHAPE/plate.png` and
`eqin_lamp.png`, made by `tools/skinprep/prep_shape.py`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | test id |
|---|---|---|---|---|---|---|
| lfFreq | lfFreq | selector, ±60° | 272.3, 398.9 | 214, 341, 116, 116 | 20 / 30 / 60 / 100 Hz, 60 | shape.knob.lffreq |
| lfBoost | lfBoost | rotary, ±134.7° | 152.4, 560.1 | 67, 475, 170, 170 | 0…10 / 0 | shape.knob.lfboost |
| lfAtten | lfAtten | rotary, ±134.6° | 387.9, 560.2 | 303, 475, 170, 170 | 0…10 / 0 | shape.knob.lfatten |
| hfFreq | hfFreq | selector, ±90° | 805.6, 400.0 | 744, 338, 124, 124 | 3 / 4 / 5 / 8 / 10 / 12 / 16 kHz, 10 | shape.knob.hffreq |
| hfBoost | hfBoost | rotary, ±133° | 672.0, 560.7 | 587, 476, 170, 170 | 0…10 / 0 | shape.knob.hfboost |
| hfBandwidth | hfBandwidth | rotary, ±128.1° | 957.3, 562.7 | 872, 478, 170, 170 | 0 (Sharp)…10 (Broad) / 5 | shape.knob.hfbandwidth |
| hfAttenFreq | hfAttenFreq | selector, ±50° | 1258.6, 405.4 | 1204, 350, 110, 110 | 5 / 10 / 20 kHz, 20 | shape.knob.hfattenfreq |
| hfAtten | hfAtten | rotary, ±132.1° | 1259.9, 564.2 | 1175, 479, 170, 170 | 0…10 / 0 | shape.knob.hfatten |
| drive | drive | rotary, ±137.6° | 201.7, 917.4 | 132, 847, 140, 140 | 0…100 / 25 / % | shape.knob.drive |
| character | character | 3-position switch, ±66.8° | 540.5, 922.0 | 476, 857, 130, 130 | Clean / Original / Burn, Original | shape.knob.character |
| mix | mix | rotary, ±136.1° | 853.1, 919.5 | 783, 850, 140, 140 | 0…100 / 100 / % | shape.knob.mix |
| output | output | rotary, ±137.8° | 1102.9, 918.9 | 1033, 849, 140, 140 | −20…+20 / 0 / dB (0 at top) | shape.knob.output |
| eqIn | eqIn | lamp switch | 1325.5, 893.6 | 1288, 856, 76, 76 | In / Out, In | shape.switch.eqin |

## Live elements (drawn at runtime, never baked)

- **Knob pointers**: near-white core in an amber glow, as the artwork's lit
  pointers, turning around the centre of each knob's scale. Travel ends are
  the end positions of the printed scales.
- **Scale numbers**, all drawn at the angle where the parameter really has
  that value: LOW FREQUENCY 20 30 60 100; HIGH FREQUENCY 3 4 5 8 10 12 16;
  HIGH ATTEN selector 5 10 20; BOOST / ATTEN 0…10; BANDWIDTH 1…9 between the
  printed SHARP and BROAD; DRIVE and MIX 0 50 100; OUTPUT −20 0 +20.
- **Scale dots**, all live: one per real position on the selectors, 11
  evenly spaced (a tenth of the travel each) on every continuous knob.
- **EQ IN lamp**: dark on the plate; the lit lamp is drawn only while EQ IN
  is on. A click on the bezel toggles it.

Decorative (no hitbox): screws, the title block, TYPE P / TLEET AUDIO MMXXIV,
section titles, BOOST / ATTEN / BANDWIDTH captions, SHARP / BROAD, CLEAN /
ORIGINAL / BURN with their arc and marks, units (% and dB), the amber slot
left of DRIVE, the star under EQ IN.
