# BITE — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.

Coordinates are **reference pixels** of the 1448×1086 mockup. The editor at
100 % shows the panel at 0.75 scale (1086×815) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| input | input | — | rotary, ±123° | 219.7, 444.5 | 112, 327, 230, 230 | −24…+24 / 0 / dB | yes | yes | yes | BITE GUI: positions; mouse; automation |
| output | output | — | rotary, ±124° | 1225.3, 441.7 | 1101, 325, 230, 230 | −24…+24 / 0 / dB | yes | yes | yes | same |
| attack | attack | — | rotary, −114°…+87°, clockwise = faster | 222, 728 | 157, 663, 135, 135 | 800…20 / 250 / µs (log) | yes | yes | yes | same + BITE GUI: scale labels |
| release | release | — | rotary, −114°…+86°, clockwise = faster | 1224, 729.75 | 1152, 662, 135, 135 | 1100…50 / 300 / ms (log) | yes | yes | yes | same |
| mix | mix | — | rotary, ±106° | 220.25, 958.5 | 160, 898, 122, 122 | 0–100 / 100 / % | yes | yes | yes | same |
| character | character | — | stepped rotary, Clean −65° / TLEET 0° / Burn +65° | 722, 972 | 660, 910, 124, 124 | Clean / TLEET / Burn, default TLEET | no (3 states) | yes | yes | same |
| ratio | ratio | — | 5 interlocked lamp buttons | buttons x 485…885 step 100, y 682 | 476+100·i, 645, 92, 118 | 4 / 8 / 12 / 20 / ALL, default 4 | no (5 states) | yes | yes | BITE GUI: mouse; lit button follows the parameter |
| vu | — (UI property `meterMode`) | GR, output RMS | lamp-lit VU needle | pivot 727, 720 | 455, 255, 530, 245 | GR / +4 / +8 | no (3 states) | no (GUI setting) | yes (with the project) | BITE GUI: automation + VU modes |

## Live elements (drawn at runtime, never baked)

- Knob pointers: amber line between the `pointer.inner` and `pointer.outer` radii.
- **Attack / Release scale numbers**: the printed .1–10 ms / s numbers did not
  match the hardware times and were removed. At each printed dot the editor
  writes the parameter's real value there, computed from the parameter's own
  range (`convertFrom0to1` at the dot's angle): Attack 800 · 234 · 97 · 41 · 20 µs,
  Release 1100 · 385 · 185 · 90 · 50 ms. Units "µs" / "ms" below the knob.
- Ratio: the selected button is drawn pressed with its lamp lit
  (`ratio_lit.png`, cut from the lit ALL button, moved by `cellPitch`); the
  others are released with dark lamps (faceplate).
- VU needle: near-white core in an amber halo (as on the artwork), angle from
  the measured printed scale; mode caption "GR" / "+4" / "+8" at the bottom
  right of the face.

VU scale angles (degrees from vertical), measured on the artwork:
−20: −27.45, −10: −21.44, −7: −17.3, −5: −11.54, −3: −4.95, −2: −0.86,
−1: 3.62, 0: 10.47, +1: 16.93, +2: 22.35, +3: 27.6.

Decorative (no hitbox): screws, title, side handles, star badge and the
"FAST CIRCUITS / BIGGER TONE" plate.
