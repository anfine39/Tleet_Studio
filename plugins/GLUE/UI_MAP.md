# GLUE — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.
Faceplate: `assets/processed/GLUE/plate.png`, made by `tools/skinprep/prep_glue.py`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| threshold | threshold | — | rotary, ±136° | 172, 601.5 | 97, 526, 150, 150 | −20…+10 / 0 / dB (0 at top; 0 dB = −10 dBFS peak at the detector) | yes | yes | yes | glue.knob.threshold |
| attack | attack | — | stepped rotary, ±125°, 6 steps | 453, 602.5 | 378, 528, 150, 150 | 0.1 / 0.3 / 1 / 3 / 10 / 30 ms, default 10 ms | no | yes | yes | glue.knob.attack |
| release | release | — | stepped rotary, ±120°, 5 steps | 723.5, 603 | 648, 528, 150, 150 | 0.1 / 0.3 / 0.6 / 1.2 s / Auto, default Auto | no | yes | yes | glue.knob.release |
| ratio | ratio | — | stepped rotary, ±120°, 5 steps | 1003.5, 604.5 | 928, 529, 150, 150 | 1.5 / 2 / 4 / 10 / ∞, default 4 | no | yes | yes | glue.knob.ratio |
| makeup | makeup | — | rotary, ±136° | 1279, 601.5 | 1204, 526, 150, 150 | −10…+20 / 0 / dB (0 at top) | yes | yes | yes | glue.knob.makeup |
| scHpf | scHpf | — | rotary, ±140° | 156, 888.5 | 96, 828, 120, 120 | 20…300 Hz log / 20 Hz | yes | yes | yes | glue.knob.schpf |
| mix | mix | — | rotary, ±139° | 408, 887.5 | 348, 827, 120, 120 | 0–100 / 100 / % | yes | yes | yes | glue.knob.mix |
| character | character | — | stepped rotary, ±74°, arc lamp | 691, 897 | 631, 837, 120, 120 | Clean / TLEET / Burn, default TLEET | no | yes | yes | glue.knob.character |
| dark | dark | — | rotary, ±143° | 918.5, 886 | 858, 826, 120, 120 | 0–100 / 0 / % | yes | yes | yes | glue.knob.dark |
| crush | crush | — | rotary, ±143° | 1085, 888.5 | 1025, 828, 120, 120 | 0–100 / 0 / % | yes | yes | yes | glue.knob.crush |
| output | output | — | rotary, ±139° | 1288.5, 888.5 | 1228, 828, 120, 120 | −20…+20 / 0 / dB | yes | yes | yes | glue.knob.output |
| grMeter | — | grMeter (gain the VCA really applies) | 20-segment LED bar | row y 349–362 | 393, 250, 670, 170 (no clicks) | 0–20 dB | segments of 1 dB | — | — | glue.meter.gr |
| scExternal | scExternal | isUsingExternalSidechain() | toolbar switch "EXT SC" with lamp | toolbar | 78 px left of BYPASS | off / on, default off | no | yes | yes | GLUE GUI: sidechain lamp |

## Live elements (drawn at runtime, never baked)

- Knob pointers: near-white core in an amber glow between `pointer.inner` and
  `pointer.outer`, turning around the cap centre (the knobs are in slight
  perspective; the printed dots sit on a circle around `scaleCentre`).
- **Scale numbers**: every number printed on the artwork was removed. Each
  number is drawn live at the angle where the parameter really has that value
  (`convertTo0to1`), e.g. SC HPF 20 · 50 · 100 · 300 Hz on a log scale.
  Threshold −20 −10 0 +10, Makeup −10 0 +10 +20, Output −20 0 +20, Mix 0 50 100,
  Dark and Crush 0 100.
- **Stepped knobs** (Attack, Release, Ratio): the printed dots were removed;
  one dot per real switch position is drawn, with its value next to it
  (Release's last step reads "AUTO").
- **Character**: the amber lamp on the arc is drawn at the selected position
  (Clean −74°, TLEET 0°, Burn +74°); the lamp baked into the art was removed.
- **GR meter**: the baked lit bar was removed. 20 segments of 1 dB, each
  spanning the printed scale between its two dB marks (marks measured on the
  artwork: 0 → x 443 … 20 → x 996, piecewise linear). Lit segments =
  round(GR), instant rise, 120 ms fall-back (UI-only smoothing).

Decorative (no hitbox): screws, title block, "PRESSURE / COHESION / CHARACTER"
plate, the amber slots left and right of the meter, the tall bar at the meter's
right end, the star under the meter.
