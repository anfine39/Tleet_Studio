# VEIL — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.
Faceplate and lit buttons: `assets/processed/VEIL/plate.png` and the sprites
listed under `sprites` in the map, made by `tools/skinprep/prep_veil.py`.

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | test id |
|---|---|---|---|---|---|---|---|
| input | input | — | rotary, ±127° | 131.3, 550.6 | 65, 481, 132, 172 | −20…+20 / 0 / dB | veil.knob.input |
| threshold | threshold | — | rotary, ±128° | 328.0, 550.4 | 262, 480, 132, 172 | −20…+20 / 0 / dB (0 = −18 dBFS) | veil.knob.threshold |
| attack | attack | — | rotary, ±112° | 567.9, 551.3 | 502, 481, 132, 172 | 10…1000 / 50 / ms (log) | veil.knob.attack |
| recovery | recovery | — | rotary, ±113° | 809.8, 550.2 | 744, 480, 132, 172 | 50…2000 / 400 / ms (400 at top) | veil.knob.recovery |
| compress | compress | — | rotary, ±126° | 1042.9, 551.5 | 977, 482, 132, 172 | 0…10 / 5 | veil.knob.compress |
| mix | mix | — | rotary, ±129° | 1231.2, 550.1 | 1165, 480, 132, 172 | 0…100 / 100 / % | veil.knob.mix |
| output | output | — | rotary, ±130° | 1417.0, 549.8 | 1351, 480, 132, 172 | −20…+20 / 0 / dB | veil.knob.output |
| tubeDrive | tubeDrive | — | rotary, ±124° | 128.2, 778.3 | 76, 722, 104, 136 | 0…100 / 28 / % | veil.knob.tubedrive |
| harmonics | harmonics | — | rotary, ±124° | 297.4, 778.2 | 245, 722, 104, 136 | 0…100 / 42 / % | veil.knob.harmonics |
| hpf | hpf | — | rotary, ±124° | 1050.7, 777.5 | 999, 722, 104, 136 | Off (20)…300 / Off / Hz (100 at top) | veil.knob.hpf |
| saturation | saturation | — | rotary, ±124° | 1237.8, 778.2 | 1186, 722, 104, 136 | 0…100 / 18 / % | veil.knob.saturation |
| dryWet | dryWet | — | rotary, ±125° | 1413.6, 778.2 | 1362, 722, 104, 136 | 0…100 / 100 / % | veil.knob.drywet |
| scHpf | scHpf | — | rotary, ±128° | 1405.9, 314.0 | 1348, 256, 116, 116 | Off (20)…300 / 60 / Hz (80 at top) | veil.knob.schpf |
| character | character | — | 3 lit buttons | CLEAN / VEIL / THICK | see map | Clean / Veil / Thick, Veil | veil.lamps.character |
| scExternal | scExternal | isUsingExternalSidechain | 2 lit buttons | INT / EXT | see map | Int / Ext, Int | veil.lamps.sidechain |
| meterMode | meterMode | — | 3 lit buttons | GR / VU / IN/OUT | see map | GR / VU / In/Out, GR (display only) | veil.lamps.meter |
| stereoLink | stereoLink | — | lamp + DUAL MONO button | 640.7, 271.7 | 600, 232, 82, 80; 579, 320, 123, 56 | Linked / Dual mono, Linked | veil.switch.link |
| vuL | — | grMeters[0], inputMeters[0], outputMeters[0] | VU needle | pivot 301.6, 620.5 | 120, 222, 364, 154 | −20…+3 VU | veil.meter.left |
| vuR | — | grMeters[1], inputMeters[1], outputMeters[1] | VU needle | pivot 952.4, 556.0 | 786, 222, 334, 154 | −20…+3 VU | veil.meter.right |
| spectrum | — | spectrum tap (final output) | analyser | screen 235, 890, 724, 82 | 235, 890, 724, 82 | 20 Hz…20 kHz, 0…−84 dB | veil.display.spectrum |

## Live elements (drawn at runtime, never baked)

- Knob pointers: white core in a red glow, turning around the centre of the
  printed dot circle. Travel ends are the printed end dots.
- **Scale numbers**: every printed number was removed and is drawn at the
  angle where the parameter really has that value: INPUT, THRESHOLD, OUTPUT
  −20 0 +20; ATTACK 10 30 100 300 1K; RECOVERY 50 150 400 1S 2S; COMPRESS 0 5
  10; MIX, TUBE DRIVE, HARMONICS, SATURATION, DRY/WET 0 50 100; HPF OFF 100
  300; SC HPF OFF 80 300. The printed dots stay as scale marks.
- **Readouts** under the knobs: emptied on the plate, the live value is drawn.
- **Buttons**: dark on the plate; the lit image is drawn only for the real
  value. A click anywhere on a button selects it.
- **VU needles**: VU ballistics (UI only) on the reading METER selects; at
  rest (no audio) 0 in GR mode, the left pin in level modes.
- **Spectrum**: real FFT (Hann) of the final output, level per 1/6-octave
  band (at least 4 bins); a sine of peak A reads 20·log10(A) dBFS. Quality
  Off / Eco / Normal / High (toolbar "FFT" button) changes only FFT size,
  refresh rate and averaging; Off or a closed editor stops the audio-thread
  tap.

Decorative (no hitbox): screws, title block and the photo, the text blocks
("WARM SMOOTH…", "MANLEY INSPIRED…", "TLEET AUDIO MMXXIV…", "SAME SOUL…"),
section titles and captions, units under the knobs, the stars.
