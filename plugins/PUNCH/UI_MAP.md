# PUNCH — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.
Faceplate and lamp: `assets/processed/PUNCH/plate.png`, `lamp_lit.png`, made by
`tools/skinprep/prep_punch.py`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| threshold | threshold | — | rotary, −127…+124° | 175.6, 783.5 | 91, 699, 170, 170 | −20…+10 / 0 / dB (0 at top) | yes | yes | yes | punch.knob.threshold |
| ratio | ratio | — | rotary, ±125° | 462.2, 783.3 | 377, 698, 170, 170 | 1:1 … ∞:1 / 4:1 (plain value = slope 1 − 1/ratio, linear on the knob) | yes | yes | yes | punch.knob.ratio |
| overEasy | overEasy | — | rocker with lamp | 724, 682 | 648, 632, 152, 100 (rocker and its OFF / ON captions) | off / on, default off | no | yes | yes | punch.switch.overeasy |
| grit | grit | — | rotary, −117…+118° | 723.5, 881 | 673, 831, 100, 100 | 0–100 / see MODEL_SPEC / % (Clean → Punch) | yes | yes | yes | punch.knob.grit |
| mix | mix | — | rotary, −112…+113° | 986.3, 786.4 | 901, 701, 170, 170 | 0–100 / 100 / % | yes | yes | yes | punch.knob.mix |
| output | output | — | rotary, −125…+127° | 1268.6, 785.6 | 1183, 700, 170, 170 | −20…+20 / 0 / dB | yes | yes | yes | punch.knob.output |
| grMeter | — | grMeter (gain the VCA really applies) | 24-segment LED bar | row y 440–455 | 345, 320, 760, 200 (no clicks) | 0–20 dB | 0.5 dB segments to 4 dB, 1 dB to 20 dB | — | — | punch.meter.gr |

## Live elements (drawn at runtime, never baked)

- Knob pointers: near-white core in an amber glow between `pointer.inner` and
  `pointer.outer`, turning around the cap centre (the knobs are drawn tilted;
  the printed dots sit on a circle around `scaleCentre`). Travel ends are the
  printed end dots seen from the cap centre.
- **Scale numbers**: every number printed on a knob scale was removed and is
  drawn at the angle where the parameter really has that value: Threshold
  −20 −10 0 +10, Compression 1 2 4 8 ∞, Mix 0 50 100, Output −20 −10 0 +10 +20.
  The printed dots stay as scale marks.
- **OverEasy lamp**: the plate shows the lamp dark; the lit lamp (cut from the
  mockup) is drawn only while OverEasy is on.
- **GR meter**: the baked lit bar and its glow were removed. The printed scale
  is expanded below 4 dB (ticks at 0 1 2 3 4, then 6 8 … 20); the segments
  follow it, 0.5 dB each up to 4 dB and 1 dB each up to 20 dB, so the lit bar
  ends at the printed value of the actual reduction. A segment lights once the
  reduction reaches its middle; instant rise, 120 ms fall-back (UI-only).

Decorative (no hitbox): screws, title block, "PRESSURE / COHESION / CHARACTER"
plate, CLEAN / PUNCH captions (names of the GRIT travel ends), "RATIO : 1",
the amber slots beside the meter, the bar at the window's right edge, the star.
