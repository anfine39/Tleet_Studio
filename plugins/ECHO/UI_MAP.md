# ECHO — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Reference: `ui/UI_REFERENCE.png`. Faceplate and sprites:
`assets/processed/ECHO/plate.png`, the lit buttons, switch positions and lamps
(`*.png`) and where they go (`sprites.json`), made by `tools/skinprep/prep_echo.py`.

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | type | centre / rect (ref px) | travel | range / default / unit | test id |
|---|---|---|---|---|---|---|
| mode | mode | 8-position selector | 195.0, 399.6 | −90…+120° (30° steps) | Reverb, 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3 / 1+2 | echo.knob.mode |
| repeatRate | repeatRate | rotary, log | 456.4, 385.0 | ±128.3° | 20…2000 ms / 200 (centre) | echo.knob.repeatrate |
| intensity | intensity | rotary | 650.9, 386.4 | ±138.5° | 0…10 / 4 | echo.knob.intensity |
| echoVolume | echoVolume | rotary | 826.2, 386.6 | ±138.5° | 0…10 / 7 | echo.knob.echovolume |
| reverbVolume | reverbVolume | rotary | 1000.8, 386.7 | ±138.5° | 0…10 / 0 | echo.knob.reverbvolume |
| bass | bass | rotary | 1217.5, 388.3 | ±132.4° | −10…+10 dB / 0 | echo.knob.bass |
| treble | treble | rotary | 1393.1, 388.3 | ±132.4° | −10…+10 dB / 0 | echo.knob.treble |
| timeMode | timeMode | slide switch | housing 112, 559, 76, 42 | — | Free / Sync, Free | echo.switch.timemode |
| bpmSource | bpmSource | slide switch | housing 319, 559, 50, 42 | — | Host / Manual, Host | echo.switch.bpmsource |
| bpmDisplay | manualBpm (arrows) | LED display + arrows | glass 472, 555, 99, 45 | — | 20…300 BPM / 120 | echo.display.bpm |
| noteDivision | noteDivision | 6 lamp buttons | 670 + 60k, 563, 50, 35 | — | 1/1 … 1/32, 1/8 | echo.buttons.notedivision |
| feel | feel | 3 lamp buttons | 1078…1351, 563 | — | Straight / Dotted / Triplet | echo.buttons.feel |
| tap | — (processor tap) | button + lamp | body 1401, 549, 64, 56 | — | — | echo.button.tap |
| drive | drive | rotary | 106.6, 733.0 | ±136.8° | 0…10 / 3 | echo.knob.drive |
| age | age | rotary | 250.5, 733.2 | ±136.8° | 0…10 / 2 | echo.knob.age |
| wow | wow | rotary | 397.4, 733.3 | ±136.8° | 0…100 % / 20 | echo.knob.wow |
| flutter | flutter | rotary | 545.8, 733.4 | ±136.8° | 0…100 % / 20 | echo.knob.flutter |
| hpf | hpf | rotary, log | 715.0, 752.8 | ±117.3° | 20…300 Hz / 20 | echo.knob.hpf |
| lpf | lpf | rotary, log | 868.4, 752.7 | ±117.3° | 1…20 kHz / 20 k | echo.knob.lpf |
| filterMode | filterMode | 3-cell switch | housing 982, 697, 34, 95 | — | Pre / Loop / Post, Loop | echo.switch.filtermode |
| stereo | stereo | rotary | 1156.8, 734.9 | ±139.4° | 0…100 % / 30 | echo.knob.stereo |
| duck | duck | rotary | 1290.6, 735.2 | ±139.4° | 0…100 % / 0 | echo.knob.duck |
| mix | mix | rotary | 1426.7, 735.7 | ±137.9° | 0…100 % / 35 | echo.knob.mix |
| vuIn / vuOut | — (meters) | VU needles | faces 55, 860, 218, 100 / 297, 860, 220, 100 | measured scale tables | 0 VU = −18 dBFS sine | echo.vu.in / echo.vu.out |
| tapeCondition | tapeCondition | segment bar | bar 578, 926, 294, 9 | marks at 601.7 / 722.7 / 825 | Clean / Worn / Magnetic dust | echo.bar.tapecondition |
| noise | noise | rotary | 967.9, 926.1 | ±129.7° | 0…100 % / 10 | echo.knob.noise |
| character | character | 3 lamp buttons | 1068…1302, 894 | — | Clean / Original / Burn | echo.buttons.character |
| output | output | rotary | 1414.8, 921.5 | ±129.5° | −12…+12 dB / 0 | echo.knob.output |

## Live elements (drawn at runtime, never baked)

- One pointer per knob; every scale number and dot at the angle where the
  parameter really has that value; the MODE captions from the parameter's
  choices.
- MODE lamp of the selected position; its brightness follows the peak of the
  heads that position plays (`takeHeadActivity`), base glow when quiet.
- The lit button of NOTE DIVISION, FEEL and CHARACTER; the switch positions.
- BPM digits = `getEffectiveBpm()`; dimmed in FREE; blinking while
  `isSyncTimeClamped()`.
- TAP lamp: a 70 ms flash at the start of each head-1 period (`getHeadDelayMs (0)`).
- TAPE CONDITION: lit segments up to the condition's mark.
- VU needles from `inputMeters` / `outputMeters` (RMS of both channels),
  ballistics in the UI only.

## Static (printed)

Titles, units (ms, dB, %, Hz), the section captions, the tape window, the
FREE / SYNC, HOST / MANUAL, PRE / LOOP / POST, CLEAN / WORN / MAGNETIC DUST
captions, the VU faces and scales.
