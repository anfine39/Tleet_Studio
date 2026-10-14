# WEIGHT — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.
Faceplate and lamps: `assets/processed/WEIGHT/plate.png`, `lit_*.png`,
`lamp_ext.png`, made by `tools/skinprep/prep_weight.py`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| threshold | threshold | — | rotary, −114…+115° | 173.3, 584.4 | 93, 504, 160, 160 | −20…+10 / 0 / dB (0 at top) | yes (0.1 dB) | yes | yes | weight.knob.threshold |
| attack | attack | — | 7-step rotary, −118…+116° | 455.8, 584.1 | 376, 504, 160, 160 | .03 .1 .3 1 3 10 30 ms / 10 ms | no | yes | yes | weight.knob.attack |
| release | release | — | 6-step rotary, −118…+117° | 724.3, 583.9 | 644, 504, 160, 160 | .05 .1 .2 .5 1 2 s / 0.2 s | no | yes | yes | weight.knob.release |
| ratio | ratio | — | 7-step rotary, −117…+116° | 990.0, 584.4 | 910, 504, 160, 160 | 1.5 2 3 4 6 10 ∞ / 2 | no | yes | yes | weight.knob.ratio |
| makeup | makeup | — | rotary, ±115° | 1275.7, 584.5 | 1196, 505, 160, 160 | −12…+24 / 0 / dB (0 at top) | yes (0.1 dB) | yes | yes | weight.knob.makeup |
| thrust | thrust | — | 3 lamps | 130, 207, 280 (x), 801 | 92 / 168 / 244, 748, 76, 80 | Clean / Neutral / Hard, Clean | no | yes | yes | weight.lamps.thrust |
| detector | detector | — | 2 lamps | 480, 588 (x), 802 | 398, 748, 140, 80; 538, 748, 120, 80 | Feed Forward / Feed Back, FF | no | yes | yes | weight.lamps.detector |
| tone | tone | — | 3 lamps | 782, 860, 941 (x), 801 | 742 / 822 / 900, 748, 78–80, 80 | Old / Neutral / New, Neutral | no | yes | yes | weight.lamps.tone |
| scHpf | scHpf | — | rotary, ±116° | 1131.0, 795.8 | 1081, 746, 100, 100 | 20…240 / 20 / Hz (log) | yes | yes | yes | weight.knob.schpf |
| scExternal | scExternal | isUsingExternalSidechain | lamp switch | 1309, 777 | 1278, 748, 62, 82 | off / on, off | no | yes | yes | weight.switch.scexternal |
| mix | mix | — | rotary, ±118° | 263.6, 972.2 | 208, 917, 112, 112 | 0–100 / 100 / % | yes | yes | yes | weight.knob.mix |
| output | output | — | rotary, ±113° | 537.6, 973.4 | 482, 918, 112, 112 | −12…+12 / 0 / dB | yes | yes | yes | weight.knob.output |
| link | link | — | rotary, ±117° | 830.9, 972.5 | 775, 917, 112, 112 | 0–100 / 100 / % | yes | yes | yes | weight.knob.link |
| grMode | grMode | — | 3 lamps | 1105, 1173, 1243 (x), 992 | 1070 / 1140 / 1206, 942, 66–76, 72 | Peak / RMS / Average, RMS (display only) | no | yes | yes | weight.lamps.grmode |
| grMeterL | — | grLeft (left VCA's applied gain) | arc LED bar, 13 + 6 segments | arc centre 428.5, 3049.1 | 140, 215, 572, 195 (no clicks) | 0–20 dB | 0.5 dB to 1 dB, then 1, 1.5, 3, 4 dB | — | — | weight.meter.gr.left |
| grMeterR | — | grRight (right VCA's applied gain) | arc LED bar, 13 + 6 segments | arc centre 1026.7, 2965.2 | 737, 215, 572, 195 (no clicks) | 0–20 dB | as left | — | — | weight.meter.gr.right |

## Live elements (drawn at runtime, never baked)

- Knob pointers: near-white core in an amber glow, turning around the centre
  of the printed dot circle (the artwork's pointers pass through it). Travel
  ends are the printed end dots.
- **Scale numbers**: every number printed on a knob scale was removed and is
  drawn at the angle where the parameter really has that value: Threshold
  −20 −10 0 +10; Makeup −12 −6 0 +6 +12 +24; HPF 20 60 120 240; Mix and Stereo
  Link 0 50 100; Output −12 −6 0 +6 +12. The printed dots of these knobs stay.
- **Stepped knobs** (Attack, Release, Ratio): the printed dots were not at the
  real step angles (up to 3° off) and are removed; one dot and one number per
  real position are drawn live: .03 .1 .3 1 3 10 30 (ms), .05 .1 .2 .5 1 2 (s),
  1.5 2 3 4 6 10 ∞ (:1). The units under the knobs stay printed.
- **Lamps**: every lamp is dark on the plate; the lit lamp (cut from the
  mockup, glow included) is drawn only on the parameter's real position. A click
  on a lamp or its caption selects that position.
- **EXTERNAL SC**: lit only while the external key is really used; faint glow
  when switched on without a key connected.
- **GR meters**: the baked bars and glow were removed. The bar sits on an arc
  (centre far below the window); segments are annular sectors whose edges lie
  on the printed scale. Gain reduction g lights from the 0 mark leftwards to the
  printed −g (API style): segments 0–0.5, 0.5–1, 1–2, 2–3, 3–4, 4–5, 5–6, 6–7,
  7–8.5, 8.5–10, 10–13, 13–16, 16–20 dB. A segment lights once the reduction
  reaches its middle; instant rise, 120 ms fall-back (UI-only). The six
  segments right of 0 never light. GR MODE picks the reading both meters show.

Decorative (no hitbox): screws, title block, "PRESSURE / COHESION / CHARACTER"
plate, section titles and captions, units under the knobs, the stars, the marks
under the middle lamps, the amber side slots, "GAIN REDUCTION (dB)".
