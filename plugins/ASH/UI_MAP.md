# ASH — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| peakReduction | peakReduction | — | rotary, ±148° | 237.5, 514.5 | 107, 384, 262, 262 | 0–100 / 40 / — | yes | yes | yes | ASH GUI: positions; mouse; automation |
| gain | gain | — | rotary, ±148° | 1211.5, 516.5 | 1081, 386, 262, 262 | 0–100 / 50 / — (±40 dB) | yes | yes | yes | same |
| mix | mix | — | rotary, ±109° | 273.5, 922.5 | 213, 862, 120, 120 | 0–100 / 100 / % | yes | yes | yes | same |
| character | character | — | rotary, ±92° | 721.5, 924.5 | 661, 864, 120, 120 | 0–100 / 50 / % | yes | yes | yes | same |
| output | output | — | rotary, ±108° | 1157.5, 925.5 | 1097, 865, 120, 120 | −20…+20 / 0 / dB | yes | yes | yes | same |
| mode | mode | — | toggle lever, Compress 55° / Limit −55° | pivot 724.5, 656 | 530, 615, 370, 85 | Compress / Limit | no (2 states) | yes | yes | same |
| vu | — (UI property `meterMode`) | GR, output RMS | VU needle | pivot 731.6, 785 | 455, 265, 540, 285 | GR / +4 / +10 | no (3 states) | no (GUI setting) | yes (with the project) | ASH GUI: automation + VU modes; ASH: VU calibration |

Label anchors: printed on the faceplate (static). Numeric display: a value pill
appears above a knob while it is hovered or dragged, and the toolbar shows the
preset name; nothing numeric is baked into the art.

VU scale angles (degrees from vertical), measured on the artwork:
−20: −24.85, −10: −20.2, −7: −16.0, −5: −11.5, −3: −6.1, −2: −1.15, −1: 3.5,
0: 9.2, +1: 14.4, +2: 18.75, +3: 24.0.

Decorative (no hitbox): screws, title, lamps (see `UI_SPEC_CONFLICTS.md` #1).
