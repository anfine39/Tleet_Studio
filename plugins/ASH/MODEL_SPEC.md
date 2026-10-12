# ASH — model specification

Optical levelling amplifier in the LA-2A family. This file is the top of the
priority order for ASH: where it conflicts with the master spec or the
mockup, this file wins (see `UI_SPEC_CONFLICTS.md` for the conflicts found).

Provenance tags:

- **[SOURCE_CONFIRMED]** published by the original manufacturer or in its
  service documentation.
- **[COMMUNITY]** widely reported measurements, not from the manufacturer.
- **[APPROXIMATION]** our modelling choice, tuned by ear and by test. These are
  the values to refine with null/reference measurements
  (`OPEN_MEASUREMENT_TASKS.md`).

## 1. Signal flow

```
in ─► input stage ─► optical attenuator ─┬─► makeup ─► output stage ─► mix ─► output trim ─► out
         (tube/transformer)              │
                                         └─► detector (feedback, after the cell)
                                               ─► Peak Reduction gain ─► EL panel ─► photoresistor ─┘
```

- The detector is **feedback**: it listens after the attenuator, like the
  hardware T4 cell driven from the output side. [SOURCE_CONFIRMED topology]
- Stereo uses **one shared cell** (detector = max of both channels), so the
  image never shifts. There is no stereo-link control because the hardware
  has none.
- Everything between input and output stage runs at **2× oversampling**
  (FIR equiripple, linear phase). Latency is an integer **49 samples** at the
  host rate, reported to the host and matched by the dry path.

## 2. Optical cell

| Block | Model | Value | Tag |
|---|---|---|---|
| EL panel rise | one pole | 0.5 ms | APPROXIMATION |
| EL panel afterglow | one pole | 8 ms | APPROXIMATION |
| Photoresistor fast part, attack | one pole | 30 ms τ → ~6.5 ms to 63 % of the final GR | SOURCE_CONFIRMED target "about 10 ms average attack" |
| Photoresistor fast part, release | one pole | 50 ms τ → ~60 ms to 50 % recovery | SOURCE_CONFIRMED target "about 60 ms for 50 % release" |
| Photoresistor slow part, charge | one pole | 0.8 s | APPROXIMATION |
| Photoresistor slow part, release | one pole, time set by history | 0.5 s … 5 s | SOURCE_CONFIRMED range "0.5 to 5 s full release" |
| History (exposure memory) | one pole | 6 s | APPROXIMATION |
| Fast/slow split | weight | 0.55 / 0.45 | APPROXIMATION |
| Cell gain law | `g = 1 / (1 + L²)` | | APPROXIMATION |

**Program dependency** is real, not a lookup: long loud passages charge the
history integrator, which stretches the slow release. Measured by test: 1.29 s
recovery after a 0.3 s burst, 6.1 s after a 6 s burst.

## 3. Compress / Limit

The hardware switch changes how hard the panel is driven. We model it as two
light laws, crossfaded over 30 ms so switching never clicks:

- Compress: `L = EL` → static ratio measured **2.57:1** (target "about 3:1").
- Limit: `L = EL⁴` → static ratio measured **7.79:1** (target "high ratio, near
  limiting").

Both [APPROXIMATION]; the hardware ratio is program dependent and not fixed.

## 4. Controls

| Parameter ID | Name | Range | Default | Law | Tag |
|---|---|---|---|---|---|
| `peakReduction` | Peak Reduction | 0–100 | 40 | sidechain gain = −10 + 0.6·PR dB | APPROXIMATION |
| `gain` | Gain | 0–100 | 50 | makeup = (knob − 50) · 0.8 dB, ±40 dB | APPROXIMATION |
| `mode` | Mode | Compress / Limit | Compress | discrete, 2 states | SOURCE_CONFIRMED |
| `mix` | Mix | 0–100 % | 100 | linear crossfade with latency-aligned dry | TLEET addition (master spec) |
| `character` | Character | 0–100 % | 50 | scales tube stages (see 5) | TLEET addition (master spec) |
| `output` | Output | −20…+20 dB | 0 | trim after mix | TLEET addition (master spec) |
| `bypass` | Bypass | off/on | off | bit-exact delayed dry | host bypass |

Parameter IDs carry version 1 and are frozen.

## 5. Analogue stages (Character)

Input and output stages are asymmetric tanh curves normalised to unity
small-signal gain, which gives mainly 2nd harmonic with some 3rd. Character
`c` scales them with a squared taper:

- input: drive 0.04 + 0.90·c², bias 0.10 + 0.12·c
- output: drive 0.04 + 0.60·c², bias 0.06 + 0.06·c

Measured THD at −6 dBFS, 1 kHz, no compression: **0.009 %** at Character 0,
**0.42 %** at 50 (default), **5.8 %** at 100. Character never changes timing.
[APPROXIMATION]

DC blockers (4 Hz) after each stage remove the offset that asymmetry creates.

## 6. Telemetry

- **GR** = −20·log10(applied cell gain), the minimum gain since the last GUI
  read, so short peaks are never lost between frames. The test compares it with
  the attenuation measured on the audio: 11.6506 dB telemetry vs 11.6514 dB
  measured.
- **VU** (GUI only, see `UI_MAP.md`): GR mode shows 0 VU minus GR; +4 mode
  shows output RMS with 0 VU = −18 dBFS; +10 mode 0 VU = −12 dBFS. Needle
  ballistics are VU-standard (99 % in ~300 ms, ~1 % overshoot).

## 7. Not modelled (intentionally)

- Sidechain HF emphasis (the hardware's R37 "flat/emphasis" trim). It is an
  internal service control, not on the panel. Listed in `UI_SPEC_CONFLICTS.md`.
- Mains hum and tube noise. A noise-free modern build is the target.
- Cell ageing and temperature drift.
