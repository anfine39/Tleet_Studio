# BITE — model specification

FET limiting amplifier in the 1176LN family. This file is the top of the
priority order for BITE: where it conflicts with the master spec or the
mockup, this file wins.

Provenance tags:

- **[SOURCE_CONFIRMED]** published by the original manufacturer or in its
  service documentation.
- **[COMMUNITY]** widely reported measurements and schematic readings, not
  from the manufacturer.
- **[APPROXIMATION]** our modelling choice, tuned by ear and by test. These are
  the values to refine with null/reference measurements.

BITE is hardware-informed, not a measured 1:1 clone of a specific unit.

## 1. Signal flow

```
in ─► Input ─► input xfmr (HP) ─► FET shunt attenuator ─► line amp ─► output xfmr ─► Output ─► mix ─► out
                                        ▲          │
                                        │          └─► detector (feedback: FET output)
                                        │                ─► ratio network ─► rectifier ─► attack/release cap ─┘
```

- **Input drives the compression**; there is no threshold control. Input is a
  clean gain ahead of the FET. [SOURCE_CONFIRMED control concept]
- The FET is the shunt leg of a voltage divider at the amplifier input; its
  channel resistance is set by the gate voltage. [COMMUNITY, from the schematic]
- The detector is **feedback**: it listens to the FET output, the same point
  the hardware side-chain is fed from (the output side, through the ratio
  network). [COMMUNITY, from the schematic]
- **Output** is the makeup gain on the wet path, placed after the colour
  stages so it is a pure trim and never changes the compression law. On the
  hardware the output pot sits before the line amp. [APPROXIMATION]
- **Mix** blends the wet path with the latency-aligned dry input (linear
  crossfade; equal-power would bump correlated signals by up to +3 dB at 50 %).
  Mix 0 and Bypass are bit-exact delayed dry. [TLEET extension]
- Stereo uses **one shared detector** (max of both channels), like a linked
  pair, so the image never shifts. There is no link control. [APPROXIMATION /
  TLEET choice]
- Everything between Input and Output runs at **2× oversampling** (JUCE FIR
  half-band equiripple, linear phase, max quality). Latency is an integer
  **49 samples** at the host rate (measured at 44.1 and 96 kHz), reported to the
  host and matched by the dry path.

## 2. Detector and gain law

The rectified FET output charges a capacitor through the attack resistor and
discharges it through the release resistor; the capacitor voltage is the FET
gate drive. We express the capacitor state directly as gain reduction `c` (dB)
and give the side-chain the static feedback law

```
target(c) = (R - 1) · knee(out_dB - T),   out_dB = in_dB - c
c ← c + k · (target(c) - c)        k = attack or release coefficient
```

which settles to the textbook feedback result `out = T + (in - T) / R` above
the knee. The detector input is the instantaneous rectified peak of the
loudest channel (full-wave, like the hardware GR amplifier).

**Zero-delay feedback.** `out` depends on the gain being computed, so the
equation is solved implicitly each sample (Newton, ≤ 8 iterations, converges
in 1–3 on the quadratic knee). A one-sample-delayed loop is unstable for 20:1
at 20 µs (linearised loop gain `a·R` > 2); the implicit solve is stable for all
settings. [APPROXIMATION of the analogue loop]

**Attack coefficient.** The closed loop above the knee speeds the capacitor up
by R. The capacitor coefficient is therefore chosen so the closed loop has
exactly the knob's time constant: `k = (1 − p) / (1 + p·(R − 1))`, with
`p = exp(−1 / (t_attack · fs))`. Result: the measured attack equals the knob at
every ratio (250 µs measures 250 µs at 8, 12 and 20:1). [APPROXIMATION]

**Release.** Open loop below the knee: `k = 1 / (t_release · fs)` (τ ≫ 1/fs).

**Program-dependent release.** A memory integrator (τ = 0.6 s) follows the
*target* gain reduction (what the programme demands), saturating at 20 dB.
It stretches the release by `1 + d · memory` (d = 0.5, ALL 1.5): transients
release at the knob time, sustained reduction releases slower. Because it
follows the target, it fades during the release itself. [COMMUNITY behaviour,
APPROXIMATION model]

### Ratio network

| Ratio | R − 1 | Knee centre T (dBFS peak at FET out, Input 0 dB) | Knee width (input axis) | Output-referred knee | Tag |
|---|---|---|---|---|---|
| 4 | 3 | −15 | 12 dB | 4.80 dB | ratio SOURCE_CONFIRMED, T/knee APPROXIMATION |
| 8 | 7 | −14 | 10 dB | 2.22 dB | as above |
| 12 | 11 | −13 | 8 dB | 1.23 dB | as above |
| 20 | 19 | −12 | 6 dB | 0.57 dB | as above |
| ALL | 39 | −15, falling with drive (see 3) | 4 dB | 0.20 dB | APPROXIMATION |

- Knee widths are chosen on the input axis and converted to the
  output-referred overshoot the feedback detector sees: `w_out = 2·w_in / (R+1)`.
  The knee hardens as the ratio rises.
- The threshold rises by 1 dB per ratio step. [COMMUNITY: "higher ratio
  buttons also raise the threshold"; amount APPROXIMATION]
- **Calibration:** at Input 0 dB a −18 dBFS RMS sine (−15 dBFS peak) sits at
  the centre of the 4:1 knee: **0.80 dB GR** measured; −33 dBFS RMS gives 0 dB.

Measured static ratios (1 kHz sine, Clean, slope between −2 and +10 dBFS peak):

| Ratio | Measured |
|---|---|
| 4 | **4.00 : 1** |
| 8 | **7.98 : 1** |
| 12 | **11.95 : 1** |
| 20 | **19.81 : 1** |
| ALL | **negative: dOut/dIn = −0.178** (output falls 2.1 dB as input rises 12 dB) |

Static curve excerpt (output dBFS peak / GR dB), full table in
`test-output/BITE_static_curves.csv`:

| In (dBFS peak) | 4 | 8 | 12 | 20 | ALL |
|---|---|---|---|---|---|
| −15 | −15.8 / 0.8 | −15.0 / 0.0 | −15.0 / 0.0 | −15.0 / 0.0 | −15.1 / 0.1 |
| −10 | −13.9 / 3.9 | −13.6 / 3.6 | −12.8 / 2.8 | −11.9 / 1.9 | −15.0 / 5.0 |
| −5 | −12.5 / 7.5 | −12.9 / 7.9 | −12.3 / 7.3 | −11.6 / 6.6 | −16.3 / 11.3 |
| 0 | −11.2 / 11.2 | −12.2 / 12.2 | −11.9 / 11.9 | −11.4 / 11.4 | −17.8 / 17.8 |
| +5 | −10.0 / 15.0 | −11.6 / 16.6 | −11.5 / 16.5 | −11.1 / 16.1 | −18.8 / 23.8 |
| +10 | −8.7 / 18.7 | −11.0 / 21.0 | −11.1 / 21.1 | −10.9 / 20.9 | −19.4 / 29.4 |

## 3. ALL ("all buttons in")

Pressing all ratio buttons parallels the ratio network and shifts the GR
amplifier's bias; users report heavy over-compression (louder in, quieter
out), a changed attack/release feel and much more distortion. [COMMUNITY]
ALL is therefore a **distinct law**, not ratio ∞:

| Aspect | ALL | 20:1 | Tag |
|---|---|---|---|
| Feedback ratio | 40 | 20 | APPROXIMATION |
| Bias shift: threshold drop | `6 dB · tanh(0.4 · knee(in − (−9 dBFS), 6 dB) / 6 dB)` | none | APPROXIMATION |
| Attack | 0.5 × knob | 1 × knob | APPROXIMATION |
| Release | 0.7 × knob, program dependence 1.5 | 1 × knob, 0.5 | APPROXIMATION |
| FET Vds terms (even / odd) | 0.045 / 0.012 | 0.012 / 0.001 | APPROXIMATION |

The bias shift lowers the feedback threshold as the drive into the unit rises,
which produces the negative-ratio region; it saturates at 6 dB, after which the
curve flattens at 40:1.

Measured (processor, 48 kHz, 1 kHz at −6 dBFS, Input +6 dB, attack 800 µs,
release 1100 ms, TLEET):

| | 20:1 | ALL |
|---|---|---|
| GR | 11.3 dB | 17.7 dB |
| THD | 0.73 % | **1.46 %** |
| H2 | 0.72 % | **1.45 %** |
| H3 | 0.071 % | **0.128 %** |
| Attack, 400 µs knob | 401 µs | **203 µs** |
| Release, 300 ms knob, after a 50 ms burst | 307 ms | **231 ms** |
| Release, 300 ms knob, after a 3 s burst | 388 ms | **429 ms** |
| GR at +10 dBFS peak in | 20.9 dB | 29.4 dB (+8.5 dB) |

## 4. Timing

| Control | Range | Law | Tag |
|---|---|---|---|
| Attack | 20 µs … 800 µs | log taper, **clockwise = faster** | range and direction SOURCE_CONFIRMED |
| Release | 50 ms … 1100 ms | log taper, **clockwise = faster** | range and direction SOURCE_CONFIRMED |

Times are time constants: measured as the time for GR to reach 63 % of its
final value after a step (attack) or to fall by 63 % after the signal stops
(release). [APPROXIMATION of the published definition]

Measured on the bare model (attack at 192 kHz with a constant-envelope step
to 20 dB over the 4:1 knee; release at 96 kHz after a 50 ms burst):

| Knob (0 = anticlockwise) | Attack set | Attack measured | Release set | Release measured |
|---|---|---|---|---|
| 0.00 | 800 µs | 802 µs | 1100 ms | 1115 ms |
| 0.25 | 318 µs | 323 µs | 508 ms | 518 ms |
| 0.50 | 126 µs | 130 µs | 235 ms | 240 ms |
| 0.75 | 50 µs | 52 µs | 108 ms | 111 ms |
| 1.00 | 20 µs | 21 µs | 50 ms | 51 ms |

Through the processor and the GR telemetry (whole chain incl. oversampling):
attack 766 µs / 21 µs (DC step, 192 kHz, block size 1), recovery to 37 %
1123 ms / 53 ms (1 kHz burst, 48 kHz). Program dependence: release 300 ms
measures 307 ms after a 50 ms burst and 382 ms after a 3 s burst.

Attack and release act on the next sample (no hidden smoothing of the timing
controls, master spec 6.2).

## 5. FET and analogue stages (Character)

**FET attenuator.** Divider gain `y = x / (1 + K · G)`, with `K = 1/g − 1`
set by the gate (`g` = small-signal gain, the applied gain) and channel
conductance `G = 1 + e·v + o·v²`. `v` is the voltage across the channel,
normalised to the 4:1 knee level (−15 dBFS) and soft-limited to |v| < 4. The
hardware feeds half of the drain voltage back to the gate to cancel the `e`
term; `e` is the residual. Because `K = 0` at unity gain, **the FET only
distorts when it is reducing gain, and more so the deeper it reduces.**
[COMMUNITY mechanism, APPROXIMATION values: e = 0.012, o = 0.001; ALL 0.045 / 0.012]

**Line amplifier.** Class A stage: asymmetric tanh normalised to unity
small-signal gain (mostly 2nd harmonic).

**Output transformer.** The low band (one pole at 80 Hz) passes through a
tanh, the rest linearly: transformer-style LF saturation (3rd harmonic on bass).

**Input transformer.** 2 Hz high-pass (no DC through the unit). A 4 Hz DC
blocker after the asymmetric stages removes their offset.

| Character | FET drive | Amp drive / bias | Transformer drive | Intent |
|---|---|---|---|---|
| Clean | 0.35 | 0.05 / 0.05 | 0 | keeps the compressor law and the core FET behaviour, no extra colour |
| TLEET | 1.00 | 0.25 / 0.12 | 0.8 | default voicing |
| Burn | 2.20 | 1.20 / 0.20 | 2.0 | more internal drive into the FET and amplifiers (inside the model, ahead of Output; not a post waveshaper) |

All [APPROXIMATION]. Character never changes the gain law or the timing.

Measured THD (processor, 48 kHz, 1 kHz, attack 800 µs, release 1100 ms):

| Condition | THD |
|---|---|
| 4:1, −6 dBFS + Input 6 dB (11.2 dB GR): Clean / TLEET / Burn | **0.24 % / 0.73 % / 4.8 %** at identical GR (11.18 dB each) |
| TLEET 4:1, Input +6, in −24 / −12 / −6 / 0 dBFS peak | 0.048 % / 0.49 % / 0.73 % / 0.96 % (GR 0 / 6.7 / 11.2 / 15.7 dB) |
| Clean, no GR (Input −18) | 0.0004 % |
| Clean, 15.7 dB GR (Input +12) | 0.32 % |

**Switching.** Ratio and Character are discrete. Every constant of the ratio
law and of the voicing glides with a one-pole of 15 ms (≈ 45 ms to 95 %), as do
Input and Output. The capacitor state is continuous, so a switch moves the gain
smoothly. Measured (100 Hz sine, Input +12, fastest attack/release, switching
every 64 × 50 samples): max |2nd difference| 0.0015 vs 0.0010 for the worst
fixed setting; with the glide disabled the same test measures 0.13.

## 6. Controls

| Parameter ID | Name | Range | Default | Law | Tag |
|---|---|---|---|---|---|
| `input` | Input | −24…+24 dB | 0 | gain into the FET, drives compression | SOURCE_CONFIRMED concept, range TLEET |
| `output` | Output | −24…+24 dB | 0 | makeup on the wet path, before Mix | SOURCE_CONFIRMED concept, range TLEET |
| `attack` | Attack | 20–800 µs | 250 µs | reversed log (clockwise faster) | SOURCE_CONFIRMED |
| `release` | Release | 50–1100 ms | 300 ms | reversed log (clockwise faster) | SOURCE_CONFIRMED |
| `ratio` | Ratio | 4 / 8 / 12 / 20 / ALL | 4 | discrete, glides (see 5) | SOURCE_CONFIRMED |
| `mix` | Mix | 0–100 % | 100 | linear crossfade with latency-aligned dry | TLEET extension |
| `character` | Character | Clean / TLEET / Burn | TLEET | discrete, glides (see 5) | TLEET extension |
| `bypass` | Bypass | off/on | off | bit-exact delayed dry, 20 ms ramp | host bypass |

Parameter IDs carry version 1 and are frozen (`Parameters.h`).

## 7. Telemetry

- **GR** = −20·log10(applied FET small-signal gain), the minimum gain since the
  last GUI read (`BiteModel::takeMinGain()` → `grMeter`), so short peaks are
  never lost between frames. Test (−6 dBFS, 1 kHz, attack 800 µs, release
  1100 ms), telemetry vs attenuation measured on the audio:

  | Setting | Telemetry | Measured | Error |
  |---|---|---|---|
  | 4:1 Clean | 6.6962 | 6.6948 | 0.001 dB |
  | 4:1 TLEET | 6.6962 | 6.7058 | 0.010 dB |
  | 20:1 Clean | 5.6355 | 5.6341 | 0.001 dB |
  | 20:1 TLEET | 5.6355 | 5.6476 | 0.012 dB |
  | ALL TLEET | 9.8505 | 9.8857 | 0.035 dB |
  | 4:1 Burn | 6.6962 | 6.8316 | 0.135 dB (amplifier saturation lowers the fundamental further; bound 0.25 dB) |

- **Input/Output meters** (`inputMeter`, `outputMeter` from the base) carry
  sample peak and RMS of the plugin input and output. VU calibration and
  ballistics live in the editor.

## 8. Robustness

- Host input is sanitised (NaN/Inf → 0) before the dry copy and the wet path;
  a non-finite detector state resets to 0 dB GR.
- `ScopedNoDenormals` in `processBlock`; the capacitor snaps to 0 below 1e-7 dB.
- No allocation after `prepareToPlay` (tested at 44.1/48/88.2/96/192 kHz ×
  block 1…2048 × mono/stereo, ALL + Burn + Input +18, fastest timing).
- Blocks longer than the prepared size are processed in chunks.

## 9. Not modelled (intentionally)

- Noise, hum and the hardware's input/output impedance interaction.
- Unit-to-unit spread (revision differences between 1176 versions).
- The "meter pinning" of ALL mode on the hardware VU (the meter shows the
  real GR here).
- External side-chain: the hardware has none on the panel; if one is added
  later it must be a real bus (master spec 12).

## 10. Open measurement tasks

- Null against a reference unit: static curves per ratio, ALL curve shape and
  depth, THD vs level, attack/release definitions.
- CPU per instance at all rates (benchmark executable, master spec 7).
