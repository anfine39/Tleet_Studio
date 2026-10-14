# DOUBLE — UI map

Machine-readable source: `UI_MAP.json` (embedded into the plugin; the editor
reads all geometry from it, so the map and the code cannot drift apart).
Debug overlay: `ui/UI_OVERLAY_DEBUG.png`. Reference: `ui/UI_REFERENCE.png`.

Coordinates are **reference pixels** of the 1536×1024 mockup. The editor at
100 % shows the panel at 0.75 scale (1152×768) under a 40 px toolbar; 75–200 %
scale the whole editor.

| control_id | parameter_id | telemetry_id | type | centre (ref px) | hitbox x, y, w, h | range / default / unit | continuous? | automation | persisted | test id |
|---|---|---|---|---|---|---|---|---|---|---|
| detune | detune | — | rotary ±117.5° + readout | 287.3, 550.6 | 220, 482, 135, 169 | −25…+25 / +8 / cents | yes | yes | yes | DOUBLE GUI: positions; readouts |
| delay | delay | — | rotary ±117.5° + readout | 607.4, 551.4 | 540, 482, 135, 169 | 0–40 / 15 / ms | yes | yes | yes | same |
| humanize | humanize | — | rotary ±117.5° + readout | 927.5, 551.1 | 860, 482, 135, 169 | 0–100 / 35 / % | yes | yes | yes | same |
| width | width | — | rotary ±117.5° + readout | 1249.5, 550.7 | 1182, 482, 135, 169 | 0–200 / 100 / % | yes | yes | yes | same |
| hpf | hpf | — | rotary ±118° + readout | 133.2, 764.2 | 75, 704, 117, 154 | 20–1000 Hz (log) / 20 | yes | yes | yes | same + filter display |
| lpf | lpf | — | rotary ±118° + readout | 325.5, 764.2 | 267, 704, 117, 154 | 1–20 kHz (log) / 20 kHz | yes | yes | yes | same + filter display |
| mix | mix | — | rotary ±118° + readout | 1169.9, 764.1 | 1111, 704, 117, 154 | 0–100 / 50 / % | yes | yes | yes | same |
| output | output | — | rotary ±118° + readout | 1393.4, 764.0 | 1335, 704, 117, 154 | −12…+12 / 0 / dB | yes | yes | yes | same |
| voices | voices | — | 3 interlocked lamp buttons | x 127, y 271 / 329 / 386 | 64, 244+58·i, 127, 57 | 1 / 2 / 4 added voices, default 2 | no | yes | yes | DOUBLE GUI: buttons light up |
| mode | mode | — | 3 interlocked lamp buttons | x 1309, y 272 / 330 / 387 | 1250, 244+58·i, 120, 57 | Tight / Wide / Ghost, default Tight | no | yes | yes | same |
| monoSafe | monoSafe | — | lamp toggle | 966, 737 | 892, 704, 148, 66 | on / off, default on | no | yes | yes | same |
| autoLevel | autoLevel | — | lamp toggle | 966, 805 | 892, 775, 148, 63 | on / off, default on | no | yes | yes | same |
| voiceDisplay | — | DoubleTelemetry: active voices, pan, delay, detune, envelope | display | screen 238, 190, 973, 229 | (no interaction) | — | — | — | — | DOUBLE GUI: voice display |
| filterDisplay | — | wetFilterResponseDb (live HPF/LPF) | display | plot 434, 690, 409, 134 | (no interaction) | 20 Hz–20 kHz log, −30…+6 dB | — | — | — | DOUBLE GUI: filter curve |

## Live elements (drawn at runtime, never baked)

- Knob pointers: white-hot core in a rose glow, between `pointer.inner` and `pointer.outer`.
- Readout windows under every knob: the parameter's own text ("12 cents", "1.2 kHz", "0.0 dB"); double-click to type a value.
- VOICES / MODE / MONO SAFE / AUTO LEVEL: released on the faceplate; the
  selected / switched-on button is drawn lit from its own sprite (the lit look
  of the artwork with that button's label).
- Voice display: per voice that actually runs, an envelope cloud of its own
  output at its real pan position, a dot on the pan scale, and its live
  detune and delay; colour = sharp (rose) / flat (lilac). No audio = the
  clouds fade. The L / R marks and the centre star (the dry source) are static.
- Filter display: the exact magnitude response of the wet HPF/LPF as the DSP
  computes it, on a true log axis 20 Hz–20 kHz with 6 dB lines; the
  frequency labels are drawn at their real positions.

Decorative (no hitbox): screws, title, photographs, taglines, stars.
