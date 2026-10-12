# GLUE — model specification

Stereo VCA bus compressor in the SSL G-Series family. This file is the top of
the priority order for GLUE: where it conflicts with the master spec or the
mockup, this file wins. The evidence for every law is in `RESEARCH.md`
(sources S1..S11). Conflicts with the skin are listed in `UI_SPEC_CONFLICTS.md`.

Provenance tags:

- **[SOURCE_CONFIRMED]** published by the manufacturer, or in a manual licensed by
  the manufacturer (S1, S2, S3).
- **[SCHEMATIC_OR_MEASURED]** read from schematics or clone designs of the circuit,
  or measured on a real unit (S5, S6, S7, S8).
- **[APPROXIMATION]** our modelling choice where no source gives the value.
  These are the values to refine with measurements (section 11).
- **[TLEET_EXTENSION]** deliberately not on the hardware.

GLUE is hardware-informed. It is not a measured 1:1 clone of one specific unit.

## 1. Signal flow

```
audio L/R ─► input coupling (1 Hz) ─► main VCAs (one control voltage) ─► makeup ─► output stage ─► DC block ─► Mix ─► Output ─► out
                                            ▲
side-chain feed L/R (inputs, or the external key)                     (all of this at 2x)
   ─► SC HPF ─► DARK ─► side-chain VCA (threshold gain × applied gain)
   ─► full-wave peak rectifier per channel ─► dominant (max) ─► slope stage (ratio, diode knee)
   ─► attack resistor ─► release network (one RC, or the Auto two-capacitor network) ─► control voltage
```

- **Side-chain fed from the inputs, through a side-chain VCA that tracks the
  applied gain.** The detector sees the input times the gain, so the circuit
  is a loop and behaves like a feedback compressor. The loop is what speeds up
  the attack (section 3.1). [SCHEMATIC_OR_MEASURED: S2, S5, S6, S8, S11; RESEARCH F1]
- The loop is **solved implicitly every sample**, with no unit delay, so 10:1 at
  0.1 ms stays stable and the loop's acceleration is exact. [APPROXIMATION: numerical method]
- **Stereo:** one control voltage drives both VCAs, so the link is full and
  there is no link control. [SOURCE_CONFIRMED: S1, S3]
- **Makeup** is applied to the audio VCA's gain only, after the detector tap. It
  never changes the compression. [SCHEMATIC_OR_MEASURED: S5, S6]
- **Mix** is a linear crossfade with the latency-aligned dry signal. **Output**
  is a trim after Mix, on both dry and wet. Mix 0 % and Bypass are bit-exact delayed dry.
  [TLEET_EXTENSION]
- **Oversampling:** the wet path runs at 2x through JUCE's half-band FIR
  equiripple filters (max quality, integer latency). The latency is **49 samples**
  at every host rate and is reported to the host. The external key goes through
  an identical second oversampler, so key and audio stay sample-aligned. [APPROXIMATION]

## 2. Detector and gain law

### 2.1 Side-chain feed

The detector input is either the main inputs (the default) or the external key
(section 9). The key replaces only the feed: the loop through the applied gain
stays, as on hardware with a key input. DC is removed at the input (1 Hz).
[SCHEMATIC_OR_MEASURED feed point; APPROXIMATION coupling corner]

### 2.2 Rectifier and dominant link

Each channel has its own SC HPF/DARK filter and its own full-wave peak rectifier.
The larger rectified value is used: `rect = max_ch |filtered_ch|`.
- Polarity and inter-channel correlation do not matter.
- (A, A), (A, 0), (0, A), (A, −A) and (A, A/2) all give identical GR. Measured: 9.846 dB for each.
- A summing detector would give 14.66 dB for (A, A).

[SOURCE_CONFIRMED: S1 "independently rectified … true peak full wave", "dominant"; S5 max across channels; S7]

### 2.3 Slope stage (ratio) and knee

The detector level normalised to the ratio's own threshold is

```
u = rect · scGain / θ_R · 10^(−c/20)       (c = applied GR in dB; scGain = 10^(−threshold_dBFS/20))
T = K_R · softKnee(u − 1, w_R)             (target GR in dB; linear in rectified amplitude)
softKnee(x, w) = 0 (x ≤ −w/2);  x (x ≥ w/2);  (x + w/2)² / (2w) otherwise
```

- The slope amplifier works on the rectified voltage and a diode sets the onset,
  so the law is linear in amplitude and curved in dB: softer at low ratios,
  nearly hard at 10:1. [SCHEMATIC_OR_MEASURED: S5, S6, S8]
- **Ratio calibration:** `K_R = (10 − 10/R) / (10^((10/R)/20) − 1)`. The hard-knee
  law then passes through (θ, θ) and (θ + 10, θ + 10/R): the label is the secant
  ratio over the first 10 dB above the ratio's threshold. This is how clone ratios
  are trimmed (S9). [APPROXIMATION]
- **Knee:** a fixed diode span of 3.24 dB of control voltage, `w_R = 3.24 / K_R`,
  so the knee narrows as the slope rises. [APPROXIMATION]
- **Threshold offset per ratio** (a lower ratio starts lower): [SOURCE_CONFIRMED direction (S1, S3, S5); APPROXIMATION values]

| Ratio | K_R (dB per unit u) | w_R (on u) | offset θ_R | GR reaches 0.1 dB at (threshold −20 dBFS) | Provenance |
|---|---|---|---|---|---|
| 1.5 | 2.887 | 1.122 | −2.5 dB | −25.25 dBFS | TLEET_EXTENSION |
| 2 | 6.424 | 0.504 | −2.0 dB | −23.00 dBFS | SOURCE_CONFIRMED step |
| 4 | 22.49 | 0.144 | −1.0 dB | −21.00 dBFS | SOURCE_CONFIRMED step |
| 10 | 73.76 | 0.044 | 0 dB | −19.75 dBFS | SOURCE_CONFIRMED step |
| ∞ | — (section 2.6) | 2 dB (input) | 0 dB | — | TLEET_EXTENSION |

Measured secant ratios over the first 10 dB, model in its equilibrium:

| Label | Measured |
|---|---|
| 1.5 | 1.49999 |
| 2 | 2.00000 |
| 4 | 4.00014 |
| 10 | 10.0019 |

The local ratio keeps rising above the calibration span, which is the curvature S5 describes.
- 2:1 is 1.51x steeper at +20 dB than at +5 dB.
- 10:1 is 1.16x steeper at +20 dB than at +5 dB.

### 2.4 Threshold calibration

The knob is printed −20..+10 dB. Knob 0 dB = **−10 dBFS peak** at the detector,
so the travel covers −30..0 dBFS peak (`params::thresholdToDbfs`).
- At the hardware's dBu calibration, 0 dBu would sit near −19 dBFS. A hot DAW
  bus would then always be deep in compression at the knob centre (RESEARCH F4).
- The threshold is the 10:1 knee centre. The other ratios start earlier by θ_R.

[APPROXIMATION, reported to the coordinator]

### 2.5 Static law (equilibrium)

For a constant-envelope detector level, the capacitor settles where charge
through the attack resistor balances discharge through the release resistor:

```
c = ρ · T(level − c),     ρ = τ_r / (τ_a + τ_r)
```

ρ ranges from 0.77 (30 ms attack, 0.1 s release) to 0.99992 (0.1 ms, 1.2 s). `GlueModel::staticGainReductionDb()` solves
this by bisection, and the tests compare it with the running model (section 10).
- Max |model − law| over all ratios, thresholds and levels: **0.00028 dB**.
- Through the processor with a 1 kHz sine: **0.028 dB**. That includes sampled-peak
  ripple at 2x.

### 2.6 Infinity (∞) — limiter law [TLEET_EXTENSION]

The skin has an ∞ position. It is a true limiting law with the same detector,
threshold, attack and release network:

```
required GR = softKnee(level_dB − threshold_dB, 2 dB)     (overshoot in dB, 2 dB soft knee)
```

- The capacitor charges toward the required GR with exactly τ_a (no loop
  acceleration and no leak while charging, so the attack equals the label).
- The diode holds the capacitor at the target.
- It releases through the same network as the other ratios.

Measured:
- Output at threshold +10 dB = threshold −0.0003 dB.
- Output rise from +10 to +30 dB over threshold = **0.000 dB**.
- 29 dB of overshoot → 28.86 dB of GR.

Switching to or from ∞ crossfades the loop solution and the limiter solution
over 10 ms (`limiterWeight`).

## 3. Timing

### 3.1 Attack [SOURCE_CONFIRMED steps; SCHEMATIC_OR_MEASURED circuit]

Steps: 0.1, 0.3, 1, 3, 10, 30 ms. The attack resistor charges the timing
capacitor toward T while the release resistor discharges it:

```
dc/dt = max(0, T(c) − c) / τ_a − leak(c)
```

T falls as c rises (the loop), so the effective attack is shorter than the
label: "greatly accelerated" by the loop gain (S5). The tests check every
step against an RK4 reference of this ODE (`GLUE_attack_steps.csv`, effective
63 % time for a step 15 dB over the ratio's threshold at 96 kHz):

| Ratio | 0.1 ms | 0.3 ms | 1 ms | 3 ms | 10 ms | 30 ms |
|---|---|---|---|---|---|---|
| 2 | 0.032 ms | 0.093 | 0.309 | 0.923 | 3.07 | 9.13 |
| 4 | 0.016 | 0.044 | 0.143 | 0.426 | 1.41 | 4.22 |
| 10 | 0.0073 | 0.0185 | 0.058 | 0.169 | 0.557 | 1.66 |
| ∞ | 0.0999 | 0.2999 | 0.9996 | 2.999 | 9.997 | 29.99 |

Error against the reference:
- ≤ 0.4 % wherever the effective time is ≥ 8 model samples.
- Below that, the backward-Euler step slows a sub-sample attack. The worst case
  is 10:1 at 0.1 ms: 7.3 µs vs 5.6 µs, i.e. 0.16 of a model sample.
- ∞ matches the label within 0.006 %.

### 3.2 Fixed release [SOURCE_CONFIRMED steps]

Steps: 0.1, 0.3, 0.6, 1.2 s; one RC: `leak(c) = c / τ_r`.
- Measured 63 % recovery equals the label at every ratio, within 0.0002 %.
- Through the processor and the GR telemetry: exactly 100 / 300 / 600 / 1200 ms (64-sample resolution).

### 3.3 Auto release [SCHEMATIC_OR_MEASURED components; APPROXIMATION topology]

Two capacitors:
- C1 = 0.47 µF, discharged by R1 = 750 kΩ (τ1 = 0.3525 s).
- C2 = 6.8 µF, coupled to C1 through R2 = 91 kΩ (R2·C1 = 42.8 ms, R2·C2 = 0.619 s).

```
dC1/dt = charge − C1/τ1 − (C1 − C2)/τ2a
dC2/dt = (C1 − C2)/τ2b
```

The control voltage is C1. The eigen time constants are **36.1 ms and 6.04 s**.
- A short burst leaves C2 almost empty, so the release is fast.
- A sustained burst charges C2, which then holds C1 up, so the release is slow.
- This is the two-pole, program-dependent release of S5. A series arrangement
  would not be program dependent (RESEARCH F5).

Measured (model vs the analytic two-pole solution: max error 3e-5 of GR0):

| Burst | GR at end | C2 | Recovery to 37 % |
|---|---|---|---|
| 20 ms | 12.2 dB | 0.39 dB | 41.9 ms |
| 3 s | 12.2 dB | 12.1 dB | 5374 ms |
| processor, 30 ms burst | | | 44 ms |
| processor, 4 s burst | | | 5423 ms |

### 3.4 Discretisation [APPROXIMATION]

- Backward Euler with `k = expm1(h/τ)`, which makes a free decay exact:
  `c[n] = c[n−1]·e^(−h/τ)`.
- The attack equation is solved by Newton from the release-only solution. It
  converges monotonically and uses at most 16 iterations (typically 2–3).
- Attack and release switches change the network on the next sample, as the hardware switches do.

## 4. SC HPF and DARK (detector only)

- **SC HPF:** 2nd-order Butterworth high-pass (TPT state-variable filter),
  12 dB/oct, continuous log 20..300 Hz. [SOURCE_CONFIRMED order (S2, S3); TLEET_EXTENSION range]
  - There is **no Off position** (skin). 20 Hz is the gentlest setting: −0.26 dB at 40 Hz and −12.3 dB at 10 Hz. In practice it only removes subsonic content.
- **DARK:** 1st-order high shelf in the detector, pole 2 kHz, up to +12 dB at 100 %. It makes the
  compressor react more to the top end without changing the audio's tone.
  [TLEET_EXTENSION]
  - At 100 %: +11.85 dB at 10 kHz, +5.98 dB at 1 kHz, +0.15 dB at 100 Hz.
- Both filters run at the 2x rate, per channel, and glide in about 20 ms: the cutoff
  in the log domain, the shelf linearly.
- `glue::detectorResponseDb(hz, hpfHz, dark, detectorRate)` is the exact response
  of these filters. The editor draws from it.
  - Swept against the running filters at 88.2 / 96 / 192 / 384 kHz: max error **2.4e-9 dB**.
  - Through the whole processor: GR = the static law at (level + response) within 0.11 dB.
    The residual is the sampled peak of an 8 kHz tone at 96 kHz.
- **Pumping:** 50 Hz kick-like bursts under a sustained 1 kHz tone.
  - At 20 Hz the 1 kHz level swings by 10.8 dB. At SC HPF 200 Hz it swings by 1.58 dB.
  - That is **6.9x less pumping**.

## 5. CRUSH [TLEET_EXTENSION]

At 100 % (scaled linearly by the amount):
- detector sensitivity +6 dB (the threshold effectively drops by 6 dB)
- slope ×2 (harder ratio; 4:1 becomes a secant of 6.65 over 10 dB)
- release times ×0.5 (0.6 s → 300 ms measured)
- nonlinear drive ×4

GR at 4:1, 6 dB over threshold: 5.19 / 8.13 / 11.09 dB at CRUSH 0 / 50 / 100 %.

## 6. Character (VCA and output stage)

```
VCA:    y = G·x·(1 + depth·(even·v + odd·v²)),   v = (x/0.5)/sqrt(1 + (x/0.5)²/16)
        depth = floor + (1 − floor)·min(1, GR/20 dB)
output: y = (tanh(d·(y + b)) − tanh(d·b)) / (d·(1 − tanh²(d·b)))   (unity small-signal gain)
then a 2 Hz DC blocker
```

| Character | even | odd | floor | drive d | bias b | Provenance |
|---|---|---|---|---|---|---|
| Clean | 0 | 0 | 0 | 0 (bypassed) | 0 | APPROXIMATION (S2: 0.004 % THD+N) |
| TLEET (default) | 0.004 | 0.0015 | 0.15 | 0.12 | −0.08 | APPROXIMATION |
| Burn | 0.020 | 0.008 | 0.30 | 0.55 | −0.20 | TLEET_EXTENSION |

- The output-stage bias is negative. Its 2nd harmonic then has the same sign as the
  VCA's, so the two stages add and never cancel.
- Character never changes the gain law: GR is identical across characters to 1e-3 dB.

Measured THD at 1 kHz, −6 dBFS:

| Character | No GR | ~8 dB GR |
|---|---|---|
| Clean | 3.5e-6 % | 0.0097 % (control-voltage ripple only) |
| TLEET | 0.063 % | 0.107 % |
| Burn | 1.82 % | 1.16 % |

- Burn without GR (THD rises with level):

  | Level | −24 dBFS | −12 dBFS | −6 dBFS | 0 dBFS |
  |---|---|---|---|---|
  | THD | 0.23 % | 0.91 % | 1.82 % | 3.50 % |

- Aliasing, Burn + CRUSH 100 % at 15 kHz: the 3rd-harmonic alias at 3 kHz is −105.5 dBc.

## 7. Controls

| ID | Name | Type / range | Step | Default | Notes |
|---|---|---|---|---|---|
| `threshold` | Threshold | −20..+10 dB, **piecewise linear: −20..0 on [0, 0.5], 0..+10 on [0.5, 1]** | 0.1 dB | 0 dB (centre) | knob 0 = −10 dBFS peak (2.4) |
| `attack` | Attack | choice 0.1 / 0.3 / 1 / 3 / 10 / 30 ms | — | 10 ms | 3.1 |
| `release` | Release | choice 0.1 / 0.3 / 0.6 / 1.2 s / Auto | — | Auto | 3.2, 3.3 |
| `ratio` | Ratio | choice 1.5 / 2 / 4 / 10 / ∞ | — | 4 | 1.5 and ∞ are TLEET_EXTENSION |
| `makeup` | Makeup | −10..+20 dB, **piecewise linear: −10..0 on [0, 0.5], 0..+20 on [0.5, 1]** | 0.1 dB | 0 dB (centre) | 1 |
| `scHpf` | SC HPF | 20..300 Hz, log | continuous | 20 Hz | no Off; 20 Hz is the gentlest (4) |
| `mix` | Mix | 0..100 % | 0.1 | 100 % | linear blend |
| `character` | Character | choice Clean / TLEET / Burn | — | TLEET | 6 |
| `dark` | Dark | 0..100 % | 0.1 | 0 % | 4 |
| `crush` | Crush | 0..100 % | 0.1 | 0 % | 5 |
| `output` | Output | −20..+20 dB, linear | 0.1 dB | 0 dB | trim after Mix |
| `scExternal` | External SC | bool | — | off | 9 |
| `bypass` | Bypass | bool | — | off | 20 ms ramp; settled = bit-exact dry |

- Threshold and makeup are 0 dB at normalised 0.5, matching the skin, which prints 0 at the knob top.
  - `convertTo0to1` is the exact inverse of `convertFrom0to1`.
  - Values snap to a 0.1 dB grid. Every grid value round-trips exactly through the range,
    the parameter and its text.
- Parameter version 1. IDs are frozen.
- Threshold, makeup, CRUSH and character glide in about 20 ms; ratio-law changes in 10 ms;
  SC HPF/DARK in about 20 ms. Mix and Output use 30 ms smoothing.
- Worst switching curvature under compression is equivalent to an instant gain step of 0.009 dB.

## 8. Telemetry

- `grMeter` receives −20·log10 of the **lowest VCA gain actually applied** in
  each block (pre-makeup, from the running model; not an estimate). Stereo is
  linked, so one value is exact for both channels.
  - Max |telemetry − applied gain| over every block and ratio: **1.9e-6 dB**.
  - Telemetry vs the attenuation measured on the audio in steady windows: ≤ 0.015 dB.
  - Values above the 20 dB LED scale are reported unclipped (60 dB internal limit).
- `inputMeter` and `outputMeter` are filled per block like BITE's: RMS and peak of
  the sanitised input and the final output.
- `isUsingExternalSidechain()` reports the detector source (9).

## 9. External side-chain

- The optional "Sidechain" input bus is mono or stereo and **disabled by default**.
- The key feeds the detector only when **External SC is on and the bus is enabled with channels**.
  Otherwise the internal detector runs (safe fallback), and `isUsingExternalSidechain()` says which.
- The key passes through the same SC HPF/DARK and the same dominant rectifier.
  A mono key works the same as a stereo key: 8.272 dB for L=R, L only, R only, anti-phase and mono.
- It never reaches the output: the key-frequency component in the output is −178 dBFS,
  and a silent main with a loud key gives exactly 0.
- Toggling the source crossfades the two detector feeds over 10 ms.
  A fresh `prepareToPlay` starts on the selected source.
- An external silent key gives 0 dB GR even with the main input 14 dB over threshold.
- Factory presets store `scExternal = 0`. Loading a preset never routes a key.

## 10. Verification (tests/GlueTests.cpp, all `GLUE:`)

The suite has 32 tests and writes CSVs to `build/tests/test-output/GLUE_*.csv`.
It covers:
- the registry and ranges (piecewise threshold/makeup round trip)
- makeup as an exact post-detector gain
- allocation-free processing at 44.1/48/88.2/96/192 kHz × blocks × mono/stereo × key
- NaN/Inf, silence, DC and denormals
- latency, Mix 0 and Bypass bit-exact
- wet/dry alignment
- static curves
- attack and release per step
- Auto
- stereo link and the dominant detector
- the filter sweep
- pumping, DARK, CRUSH, Character/THD/aliasing
- GR telemetry
- the side-chain matrix
- switching and automation
- the editor and state round trip
- presets
- CPU: 2.6 % of one core at 48 kHz (3.4 % with a key) and 7.1 % at 96 kHz with a key, block 128

## 11. Not modelled (intentionally)

- Console noise, hum and VCA control feedthrough.
- The hardware's "IN" switch: the main VCA stays in circuit. GLUE's Bypass is a true bypass.
- THE BUS+ features: F/B switch, Dual Mono/M-S, negative ratios, Auto 2.
- Autofade.

## 12. Open measurement tasks

1. Knee width per ratio and the threshold offsets per ratio against a measured unit
   (S5's dataset publishes control-voltage sweeps for 2/4/10).
2. The topology of the Auto network and how much C2 couples, from an original 82E27 schematic.
3. The side-chain VCA's tracking gain. GLUE assumes exactly the applied gain.
4. THD per character against a measured 202-class VCA and output stage, at several GR depths.
5. Effective attack per step against S5's ballistics data (the loop acceleration magnitude).
