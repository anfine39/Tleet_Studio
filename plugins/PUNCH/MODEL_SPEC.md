# PUNCH — model specification

PUNCH is a feed-forward VCA compressor in the dbx 160A family. For PUNCH, this
file is the top of the priority order: where it conflicts with the master spec
or the mockup, this file wins. The evidence for every law is in `RESEARCH.md`
(sources S1..S8, findings F1..F10).

Provenance tags:

- **[SOURCE_CONFIRMED]**: published by dbx in the 160A manual or data sheet (S1, S2), or the 160X data (S3) where it agrees with them.
- **[SCHEMATIC_OR_MEASURED]**: documented circuit principle of the dbx/THAT detector, VCA and feed-forward compressor (S5–S8).
- **[APPROXIMATION]**: our modelling choice where no source gives the value. These are the values to refine with measurements (section 11).
- **[TLEET_EXTENSION]**: deliberately not on the hardware.

PUNCH is hardware-informed. It is not a measured 1:1 clone of one unit.

## 1. Signal flow

```
audio L/R ─► input coupling (0.5 Hz) ─► VCA (one gain for both channels) ─► GRIT (VCA colour, output stage) ─► DC block ─► Mix ─► Output ─► out
                │                             ▲
                └─► true-RMS detector: one pole on (L² + R²)/2, τ = 34.74 ms ─► level (dB) ─► gain computer (threshold, slope, hard/OverEasy) ─┘
                                                                                                         (all of this at 2x)
```

- **Feed-forward.** The detector sees the input, never the gain-reduced signal. [SOURCE_CONFIRMED: S3; SCHEMATIC_OR_MEASURED: S7, S8; RESEARCH F1] Two consequences follow:
  - the static curve of the whole unit is the gain computer itself (section 2.5);
  - attack and release are those of the RMS detector alone (section 3).
- **One control voltage** drives both channels: fully linked, with a single GR value. [SOURCE_CONFIRMED: S1 strapping, S2 "True RMS Power Summing"]
- **Mix** is a linear crossfade with the latency-aligned dry signal. **Output** is a trim after Mix, applied to both dry and wet. Mix 0 % and Bypass are bit-exact delayed dry. [TLEET_EXTENSION, except Output, which is the 160A's ±20 dB output gain: SOURCE_CONFIRMED range]
- **Oversampling.** The wet path runs at 2x through JUCE's half-band FIR equiripple filters (max quality, integer latency). Only GRIT is nonlinear, but the gain modulation and the GRIT stages are simplest and alias-free at one rate.
  - Latency is **49 samples** at every host rate (1.02 ms at 48 kHz) and is reported to the host.
  - The dry path is delayed by the same 49 samples, and a re-prepare clears the dry history. [APPROXIMATION]
- **No external sidechain.** The skin has no control for one (master spec §12). The 160A's rear-panel detector input is not modelled in v1. [RESEARCH F10]

## 2. Detector and gain law

### 2.1 Input coupling

The wet path has a first-order high-pass at 0.5 Hz (double precision) before
both the VCA and the detector, so DC never reaches the detector.
- 160A: "−3 dB 0.5 Hz". [SOURCE_CONFIRMED: S1]
- A second 0.5 Hz high-pass after the GRIT stages removes the offset of the asymmetric output stage. [APPROXIMATION]
- Measured wet response with no GR: −0.0066 dB at 20 Hz and −0.0006 dB at 20 kHz (160A: +0/−0.5 dB). The 160A's −3 dB at 90 kHz is above the band and not modelled.

### 2.2 True-RMS detector and stereo power sum

```
e[n]  = (Σ_ch x_ch[n]²) / numChannels                 (power sum, normalised: L = R reads like mono)
ms[n] = ms[n−1] + a (e[n] − ms[n−1]),   a = 1 − exp(−1 / (τ fs))
L[n]  = 10 log10(ms[n])                               (dB RMS re full scale; a sine peaking at P dBFS reads P − 3.01)
τ     = 10 log10(e) / 125 dB/s = 34.74 ms
```

- **True RMS** (mean square, not peak). [SOURCE_CONFIRMED: S1 "True RMS Level Detection"]
- **One pole on the mean square.** Release is a straight line in dB and attack is 1 − e^(−t/τ) in the mean square. [SCHEMATIC_OR_MEASURED: S5, S6]
- **τ from the 125 dB/s release rate:** a mean square decaying as e^(−t/τ) falls 4.343/τ dB/s. [SOURCE_CONFIRMED rate (S1, S2); RESEARCH F2]
- The discretisation is exact for a free decay: a factor of exp(−1/(τ fs)) per sample.
- Silence settles to exactly 0 (below 1e-30 the mean square is set to 0), so GR returns to exactly 0 dB.
- **Stereo: true RMS power summing.** The powers of the channels add in one detector. [SOURCE_CONFIRMED: S1, S2]
  - The ÷2 normalisation makes a centred mono source read the same in a stereo instance as in a mono instance. [APPROXIMATION; RESEARCH F8]
  - Measured (∞:1, threshold −30 dB RMS, constant envelope at −10 dB RMS):

| Input | GR |
|---|---|
| (A, A) | 20.0001 dB |
| mono A | 20.0001 dB |
| (A, −A) | 20.0001 dB |
| (A, 0) | 16.9898 dB (−3.0103 dB) |
| (0, A) | 16.9898 dB |
| (A, A/2) | 17.9589 dB (= 20 + 10 log10(1.25/2)) |

### 2.3 Gain computer: hard knee and OverEasy

The detector level relative to the threshold is x = L − T, and s = 1 − 1/ratio is the slope.

```
hard knee:  GR = s · max(0, x)
OverEasy:   GR = s · w · ln(1 + e^(x / w)),   w = 2.5 dB
GR is clamped at 60 dB.
gain = 10^(−GR/20)
```

**Ratio law.**
- Range 1:1 → ∞:1. [SOURCE_CONFIRMED: S1–S3]
- s is the fraction of the dB-linear detector voltage that reaches the VCA (`RATIO = 1/(1−R)`). [SCHEMATIC_OR_MEASURED: S6, S7]
- The measured ratio above threshold equals the setting: max error 2e-4·r² over a quarter-octave sweep from 1:1 to 64:1 (`PUNCH_ratio_sweep.csv`).
- ∞:1: the output rises 1e-6 dB for +15 dB of input.

**Negative ratios** (the 160A's "Infinity+" region, s up to 2, i.e. −1:1) are **not implemented in v1**. The skin's scale ends at ∞. The law would extend unchanged to s ∈ (1, 2]. [SOURCE_CONFIRMED behaviour, deliberately out of scope]

**Hard knee.** Nothing below threshold; the full slope above. [SOURCE_CONFIRMED: S1]

**OverEasy.**
- Softplus in dB. The threshold is where the asymptotes meet: the midpoint, with no distinct onset. [SOURCE_CONFIRMED properties (S1); SCHEMATIC_OR_MEASURED shape (S7 diode soft rectification); APPROXIMATION width w]
- Anchors (measured on the model):

| Point | Value |
|---|---|
| GR at threshold | s · 2.5 ln 2: 0.866 dB at 2:1, 1.300 dB at 4:1, 1.733 dB at ∞:1 |
| Local slope at threshold | s/2 (midpoint of the transition) |
| Local slope reaches 10 % / 90 % of s | 5.5 dB below / above threshold |
| 0.1 dB of GR at ∞:1 | from 7.9 dB below threshold |
| Within 0.01 dB of the hard knee | from 15 dB above threshold |

- Plot data for OverEasy vs hard knee at 2:1, 4:1 and ∞:1: `PUNCH_overeasy_vs_hard.csv`.

**Pure function for the editor:** `PunchModel::gainComputerDb (detectorDb, thresholdDb, slope, overEasy)`.
- It is the exact code the DSP runs every sample. `staticOutputDb` gives the input→output curve.
- `PunchProcessor::getDetectorLevelDb()` gives the live x position.

### 2.4 Threshold calibration

- The knob is printed −20..+10 dB with 0 at the top.
- `params::thresholdToDetectorDb(knob) = knob − 15 dB`: **knob 0 dB = −15 dB RMS** (a sine peaking at −12 dBFS).
- The travel covers −35..−5 dB RMS. At 0 dBFS = +22 dBu that is −13..+17 dBu, inside the 160A's −40..+20 dBu.
- The knob top gives a few dB of compression on hot drum tracks; +10 is nearly off; −20 squashes. [APPROXIMATION, reported to the coordinator; RESEARCH F6]

### 2.5 Static law

Feed-forward, so for a constant detector level the steady GR is exactly the gain computer at L.

| Check | Max error |
|---|---|
| Model vs `gainComputerDb(detector level)`: all 8 ratios (1, 1.5, 2, 3, 4, 8, 20, ∞), both knees, thresholds −35/−20/−5, levels −20..+30 dB around threshold | **1.1e-6 dB** |
| Model vs the MODEL_SPEC formulas at the *input* RMS level (independent code in the test) | **1.4e-4 dB**. The residual is the input coupling's +0.00014 dB at the Nyquist frequency of the constant-envelope test signal. |
| Processor (2x, 1 kHz sine, THRESHOLD knob −20/0/+10, 6 ratios, both knees, 4 levels) GR telemetry vs law at the sine's RMS level | **0.010 dB**. This is the detector ripple at 2 kHz, because the telemetry reports the block's deepest GR. |

CSV: `PUNCH_static_curves.csv` and `PUNCH_static_curves_processor.csv`.

## 3. Timing: program-dependent attack and release

### 3.1 One time constant

There are no attack or release controls: the hardware has none and neither
does the skin. Attack and release both come from the single detector pole
(section 2.2), so they are locked together and depend on the program.
[SOURCE_CONFIRMED: S1, S2 "program dependent"; SCHEMATIC_OR_MEASURED: S5, S6]

### 3.2 Attack

**Definition (our reading of dbx's "15 ms for 10 dB").** The time to 63 % of
the final GR for a level step from the threshold to D dB above it. In a
feed-forward hard knee this time does not depend on the ratio:

```
t(D) = −τ ln(1 − (10^(−0.0368 D) − 10^(−D/10)) / (1 − 10^(−D/10)))
```

| Step | dbx typical (S1) | Model, hard knee (any ratio) | Analytic | vs dbx | Processor telemetry (48 kHz, 16-sample blocks) | OverEasy |
|---|---|---|---|---|---|---|
| 10 dB | 15 ms | 15.78 ms | 15.66 ms | +5 % | 15.99 ms | 19.18 ms |
| 20 dB | 5 ms | 6.70 ms | 6.63 ms | **+34 %** | 6.87 ms | 7.93 ms |
| 30 dB | 3 ms | 2.81 ms | 2.77 ms | −6 % | 2.98 ms | 3.29 ms |

- A single pole that releases at the documented 125 dB/s cannot give 5 ms at 20 dB, which would need τ = 26 ms and 168 dB/s.
- PUNCH keeps the source-confirmed release rate and accepts the 20 dB deviation. Tests hold 10/30 dB within ±10 % and 20 dB within ±40 % of dbx, and the model within 0.5 % of the analytic pole. [APPROXIMATION definition; RESEARCH F2]
- From silence the step is slower: 19.4 / 7.0 / 2.8 ms for 10 / 20 / 30 dB over threshold.
- Program dependence: bigger overshoots attack faster.
- CSV: `PUNCH_attack_steps.csv`. WAV input and output of the 20 dB processor step: `PUNCH_attack_20dB_*.wav`.

### 3.3 Release

- When the signal stops, the detector falls in a straight line at **125 dB/s**. The GR returns to 0 when the detector crosses the threshold, after (L − T)/125 s = GR / (125·s) s.
- At ∞:1 these are the data-sheet figures exactly. [SOURCE_CONFIRMED: S1]

| GR | dbx (S1) | Model ∞:1 | Model 4:1 | Model 2:1 |
|---|---|---|---|---|
| 1 dB | 8 ms | 8.00 ms | 10.67 ms | 16.0 ms |
| 10 dB | 80 ms | 80.00 ms | 106.7 ms | 160 ms |
| 20 dB | — | 160.0 ms | 213.3 ms | 320 ms |
| 50 dB | 400 ms | 400.0 ms | — | — |

- GR at half the recovery time is half the GR: a straight line in dB.
- Processor telemetry (48 kHz, 64-sample blocks, ∞:1): 81.3 ms from 10 dB and 241.3 ms from 30 dB (expected 80 and 240 ms).
- **Program dependence.** A drop to a level still above the threshold releases exponentially in the mean square, so it slows in dB as it nears the new level. Going from 20 to 10 dB over threshold at ∞:1, the first 5 dB take 49.5 ms and the next 4 dB take 73.7 ms. Both match the analytic pole within 0.1 ms.
- CSV: `PUNCH_release_steps.csv`.

### 3.4 Discretisation

The model runs at 2x the host rate (88.2..384 kHz). Its pole coefficient is
exact for every rate, so timing does not depend on the sample rate.

## 4. VCA and GR telemetry

- The VCA multiplies both channels by `gain = 10^(−GR/20)`. A Blackmer-type VCA is linear in dB of control. [SOURCE_CONFIRMED: S3 "Blackmer VCA"; S6]
- **Telemetry:** `grMeter` receives −20·log10 of the lowest gain the VCA *actually applied* in each block. It is not clipped at the LED meter's 20 dB.
- Measured:

| Check | Result |
|---|---|
| \|telemetry − applied gain\| over every block of a 3 s level-stepped program (2:1, 4:1, 20:1, ∞; hard and OverEasy) | max **2.9e-6 dB**. Trace: `PUNCH_gr_telemetry_error_trace.csv` |
| Attenuation measured on the audio in steady windows vs telemetry (77..85 windows per setting) | max **0.011 dB** (detector ripple) |
| 31 dB over threshold at ∞ | telemetry 31.08 dB |

## 5. GRIT (Clean → Punch) [TLEET_EXTENSION within the documented VCA character]

GRIT is a macro for level-dependent VCA and output colouring. It is not a
distortion added after compression: its main term lives in the VCA and grows
with the VCA's gain reduction. At amount g (0..1):

```
depth = g · (0.1 + 0.9 · min(1, GR / 20 dB))
v     = clamp(x / 0.5, −4, 4)                        (x = VCA input, 0.5 = −6 dBFS)
VCA   : y = gain · x · (1 + depth · (0.030 v + 0.015 v²))      (2nd + 3rd harmonics; pure polynomial up to +6 dBFS)
output: y ← (c(d(y + b)) − c(d b)) / (d (1 − (d b)²)),  c(u) = u − u³/3 on |u| ≤ 1, held beyond
        d = 0.25 g, b = −0.15                        (asymmetric cubic soft clip, unity small-signal gain)
```

- The VCA term's harmonics relative to the fundamental do not depend on the gain, only on the input level and the depth. Gain reduction therefore *adds* colour, as on the hardware (S3 gives the 3rd harmonic changing with threshold).
- The output stage depends on level only.
- GRIT never touches the gain computer or the telemetry: GR is identical across GRIT 0..100 % (11.9998 dB in the test).
- Default **20 %**.

THD at 1 kHz, 48 kHz host, a −6 dBFS sine (`PUNCH_grit_thd.csv`):

| GRIT | No GR (1:1) | 12 dB GR (4:1, threshold −25 dB RMS) |
|---|---|---|
| 0 % (Clean) | 0.0000016 % (−156 dB) | 0.043 % |
| 20 % (default) | 0.040 % | 0.20 % |
| 50 % | 0.13 % | 0.51 % |
| 100 % (Punch) | 0.40 % | 1.04 % |

**Clean bound (tested).**
- THD < 1e-7 (−140 dB) without GR.
- THD < 0.2 % with GR (the 160A's THD+N spec, S1). The 0.043 % that remains is the hardware's own RMS-detector ripple modulating the VCA (RESEARCH F7). It is part of the model, not GRIT.

**Level dependence (Punch, no GR).** THD is 0.024 / 0.097 / 0.19 / 0.40 / 0.86 % at −30 / −18 / −12 / −6 / 0 dBFS.

**GR dependence (Punch, −6 dBFS input).** 2nd harmonic is 0.38 / 0.57 / 1.01 / 1.51 % at 0 / 4 / 12 / 20 dB of GR.

**Aliasing (Punch, 15 kHz at −1 dBFS, 12 dB GR, 48 kHz host).**
- Worst fold of harmonics 2..6 is −110 dBc (tested < −100 dBc).
- Both stages are 3rd-order polynomials below +6 dBFS, and their harmonics up to 45 kHz stay inside the 2x band and are removed by the decimator.

The GRIT coefficients are [APPROXIMATION]. At the default they stay inside the 160X's published 0.07 % per harmonic plus GR colour.

## 6. Mix, Output, Bypass

- **Mix:** 0..100 %, linear, smoothed over 30 ms, with the latency-aligned dry signal. Null test: \|out − (dry + m(wet − dry))\| ≤ 8.2e-8.
- **Output:** −20..+20 dB, multiplicative smoothing over 30 ms, after Mix.
- **Bypass:** a 20 ms ramp to dry. Fully bypassed, the output is the bit-exact delayed dry signal, and the first 49 samples after prepare are silent.
- Wet/dry are time-aligned: at Mix 50 % with no GR, the level changes by less than 0.007 dB from 20 Hz to 20 kHz.

## 7. Control smoothing

- Threshold, slope (ratio), the OverEasy blend and GRIT glide through **two one-poles in series**, 8 ms per stage (about 27 ms to 99 %). The gain's slope starts and ends at zero.
- Jumping the ratio 1:1 ↔ ∞ under 16 dB of overshoot while toggling OverEasy and GRIT 0 ↔ 100 % gives a maximum second difference of 5.5e-5. That is 1.06× the steady baseline of 5.2e-5, equivalent to an instant step of 0.0016 dB.
- While the OverEasy switch glides, the gain computer blends the hard and OverEasy curves.
- The detector itself is never smoothed or reset by a control change. Master spec 6.2: no hidden envelope.

[APPROXIMATION]

## 8. Parameters (`Parameters.h`, namespace `tleet::punch::params`, version 1)

| ID | Name | Plain range | Normalisation / step | Default | Text |
|---|---|---|---|---|---|
| `threshold` | Threshold | −20..+10 dB | piecewise linear, 0 dB at 0.5 (`centredDbRange`), 0.1 dB grid | 0 dB (= −15 dB RMS) | "+2.5 dB" |
| `ratio` | Compression | slope s = 1 − 1/ratio, 0..1 | linear, continuous | 0.75 (4:1) | "4.0:1", "20:1", "∞:1" |
| `overEasy` | OverEasy | bool | — | off (hard knee) | — |
| `grit` | Grit | 0..100 % | 0.1 | 20 % | "20.0" |
| `mix` | Mix | 0..100 % | 0.1 | 100 % | "100.0" |
| `output` | Output | −20..+20 dB | linear, 0.1 | 0 dB | "+0.0 dB" |
| `bypass` | Bypass | bool | — | off | — |

**Helpers for the editor:**
- `ratioFromPlain(s)` returns 1/(1−s), and ∞ at s = 1.
- `plainFromRatio(r)` returns 1 − 1/r: 0 for r ≤ 1 and 1 for ∞.
- `ratioText` and `ratioPlainFromText` accept "4:1", "4", "inf" and "∞".
- Label positions on the knob: 1 → 0, 2 → 0.5, 4 → 0.75, 8 → 0.875, 20 → 0.95, ∞ → 1.
- `thresholdToDetectorDb(knob)` converts the knob to dB RMS.

**Buses:** stereo in == stereo out, or mono == mono.

## 9. Not modelled in v1

- Negative ratios (Infinity+).
- The external detector input.
- The 90 kHz output roll-off.
- Noise floor (< −90 dBu on the 160A).
- Meter ballistics (left to the editor; the telemetry is the true value).
- Output clipping at +24 dBu.
- The 160A's front-panel threshold LEDs. The editor can derive them from `getDetectorLevelDb()` against the threshold.

## 10. Verification (`tests/PunchTests.cpp`, 20 tests, all "PUNCH: …")

**Coverage:**
- Registry and helpers.
- 5 rates × 9 block sizes × mono/stereo: finite output and **0 allocations** after prepare.
- Audio matrix (impulse, sine, stepped sine, log sweep, multitone, white/pink noise, bursts, asymmetric stereo): bounded output, telemetry equal to applied gain, realtime bit-identical to offline.
- NaN/Inf, silence, DC and denormals.
- Latency (49 measured = 49 reported at 44.1/48/96/192 kHz).
- Bit-exact Mix 0 and Bypass.
- Dry/wet alignment, the Mix null test, static curves (CSV) and the ratio sweep (CSV).
- Attack and release (CSV, WAV), linked stereo and power sum, GR telemetry (error trace).
- GRIT THD, level and GR dependence, and aliasing (CSV).
- Click-free switching and random automation.
- The editor does not alter audio.
- The detector hand-off, state round trip, presets, and CPU.

**CPU, measured on a 2.8 GHz Xeon, stereo, OverEasy, GRIT 50 %:**

| Host rate | Load, block 64 / 512 (% of one core) | p99 block time, block 64 (% of the block period) |
|---|---|---|
| 44.1 kHz | 1.9 / 2.0 % | 3.9 % |
| 48 kHz | 2.1 / 2.2 % | 4.3 % |
| 96 kHz | 4.7 / 4.9 % | 11.4 % |
| 192 kHz | 8.9 / 8.9 % | 21.6 % |

At 48 kHz the 2x model costs about 1.2 % and the FIR oversampler about 0.8..1.0 %. GLUE is at a similar 2.6 %.

**Mutation checks.** Each fault was injected and the suite rebuilt, then restored. Each one makes the named test fail:

| Fault | Failing test |
|---|---|
| Threshold disconnected | static curves (processor) |
| Telemetry frozen | GR telemetry |
| Release rate 100 dB/s | attack + release |
| Detector τ 5 ms with the constant unchanged | attack + release |
| OverEasy disconnected | static curves |
| Ratio ignored | static curves |
| Detector reads only L | linked stereo |
| GRIT no-op | GRIT |
| Mix squared | Mix null test |
| Bypass returns wet | latency/bypass |

## 11. Open measurement tasks

- **M1. OverEasy knee width.** Measure a 160A's static curve at ∞:1 and 4:1 from −20 to +20 dB around threshold, then fit w (currently 2.5 dB).
- **M2. Attack definition.** Capture step responses at 10/20/30 dB on a 160A and check the 63 % reading and the 20 dB deviation. A second, faster detector stage, if one exists, would show here.
- **M3. Ratio pot taper** of the 160A (S7 notes a loaded pot puts 2:1 slightly past mid-travel).
- **M4. VCA harmonic profile** vs GR and level on a serviced unit, to anchor the GRIT 0..20 % range.
- **M5. Strapped-pair calibration:** whether two strapped 160As read +3 dB for an identical L = R signal (the unnormalised power sum).
