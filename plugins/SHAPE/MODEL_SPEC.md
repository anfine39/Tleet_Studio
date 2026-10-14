# SHAPE — model specification

Passive program equaliser with Pultec EQP-1A DNA. This file is the top of the
priority order for SHAPE's DSP: where it conflicts with the master spec or the
mockup, this file wins. The evidence for every law is in `RESEARCH.md`
(sources S1..S12, findings F1..F10, open tasks R1..R5).

Provenance tags:

- **[SOURCE_CONFIRMED]** published by the manufacturer (S1), or in a licensed
  or manufacturer-derived manual (S2, S3).
- **[SCHEMATIC_OR_MEASURED]** from the schematic-derived network model (S4,
  S5), the circuit itself, or published measurements (S7, S9).
- **[APPROXIMATION]** a modelling choice where no source gives the value.
  Section 14 lists what to measure.
- **[TLEET_EXTENSION]** deliberately not on the hardware.

SHAPE is hardware-informed. It is not a measured 1:1 clone of one unit.

## 1. Signal flow

```
host rate                         internal rate (host × 4 / 2 / 1, ≥ 176.4 kHz)                                         host rate
in ─► sanitise ─┬─► ↑N ─► × D ─► input transformer ─► passive network ─► make-up amplifier ─► output transformer ─► × 1/D ─► ↓N ─┐
                │                                     (EQ IN: crossfade with                                                      │
                │                                      the flat network)                                                          │
                └─► latency-aligned dry ──────────────────────────────────────────────────────────────── MIX ◄───────────────────┘
                                                                                                           │
                                                                                         OUTPUT ─► host Bypass (dry) ─► out
D = DRIVE gain × CHARACTER drive offset
```

- The network and every nonlinear stage run at the **internal rate**. The
  rate is 4× the host rate below 80 kHz, 2× below 160 kHz, and 1× at
  176.4/192 kHz: always ≥ 176.4 kHz (`internalRateFor`). The up/down pair is
  `juce::dsp::Oversampling`, `filterHalfBandFIREquiripple`, max quality,
  integer latency. [APPROXIMATION: numerical method]
- **Latency**: 61 samples at 44.1/48 kHz (1.38 / 1.27 ms), 49 at 88.2/96 kHz
  (0.56 / 0.51 ms), 0 at 176.4/192 kHz. It is reported to the host, and the
  impulse test measures the same values (section 11).
- MIX, OUTPUT and host Bypass run at the host rate on the latency-aligned
  plugin input (`dsp::LatencyAlignedDry`).
- Everything in the model runs in double precision. The interface between
  stages is float (the host buffers).
- There is no spectrum tap.

## 2. The passive network

### 2.1 Topology [SCHEMATIC_OR_MEASURED, S4/S5/S8]

One series loop, driven by the input transformer's secondary (an ideal
source; the source and load resistances are folded into the values as in S5):

```
vin ─► S ─► node ─┬─ A ───────────────────┐
                  └─ Lb = (Rla ‖ C1) + R2 + R3 ─┤   (vout taken across R3 and J)
                                                ├─ J = Rlb ‖ C2 ─► return
S  = (1 − b)·Rh + ( b·Rh ‖ (Rq + L + C) )     HF boost pot, series LC resonator, BANDWIDTH rheostat Rq
A  = (1 − a)·Ra + ( a·Ra ‖ (Rstop + Ca) )     HF atten pot, ATTEN SEL capacitor Ca
Lb = (Rla ‖ C1) + R2 + R3                     LF atten rheostat across C1 = C2 / 30, load
J  = Rlb ‖ C2                                 LF boost rheostat across C2 (ground leg)
vout = (v(J) + v(R3)) · makeup
```

b, a, la and lb are the pot positions after the taper (2.4). With every
boost/atten at 0 the network is purely resistive. Its loss, −23.80 dB, is
constant and made up exactly by `makeup` = 15.50 (S5: 15.4882; S7: "20 dB or
so"). Flat settings are therefore exactly 0 dB at every frequency (measured
< 1e-9 dB).

The sections interact as on the hardware (S8). The LF boost shelf, the LF
atten shelf, the HF resonance and the HF shelf are not separate filters; each
one loads the others.

### 2.2 Component values

| Element | Value | Source |
|---|---|---|
| Rh, HF boost pot | 12 kΩ | S5 |
| √(L/C), HF resonator | 3464.1 Ω (L/C = 12·10⁶), resonance at the selected 3/4/5/8/10/12/16 kHz | S5 |
| Rq, BANDWIDTH rheostat | 754.0 Ω (Sharp) .. 2136.0 Ω (Broad), linear | **calibrated** (S5: 390 Ω + 0..3.9 kΩ) |
| Ra, HF atten pot | 1 kΩ | S5 |
| Ca, ATTEN SEL | 270 / 135 / 68 nF for 5 / 10 / 20 kHz | S5 |
| Rstop, in series with Ca | 94.53 Ω | **calibrated** (S5 declares a 91 Ω R1 there but does not place it) |
| C2, LF boost | 2.2 µF / 1.1 µF / 560 nF / 330 nF for 20 / 30 / 60 / 100 Hz | S5 |
| C1, LF atten | C2 / 30 | S5 |
| Rlb, LF boost rheostat | 9446 Ω | **calibrated** (S5: 8.2 kΩ; S10: 10 kΩ log) |
| Rla, LF atten rheostat | 95.51 kΩ | **calibrated** (S5: 110 kΩ) |
| R2, R3 | 1 kΩ, 10 kΩ | S5 |

### 2.3 Calibration [SOURCE_CONFIRMED targets (S1), APPROXIMATION values]

The five calibrated values are solved so that the analog prototype meets S1
exactly (RESEARCH F2):

| Target | Setting | Result |
|---|---|---|
| LF boost 13.5 dB | 60 Hz position, BOOST 10, at 60 Hz | 13.500 dB |
| LF atten 17.5 dB | 60 Hz position, ATTEN 10, at 60 Hz | −17.500 dB |
| HF boost 18 dB | BOOST 10, BANDWIDTH 0 (Sharp), at the selected frequency, every position | 18.000 dB |
| HF boost at Broad 13.5 dB (S3: "13 or 14") | BOOST 10, BANDWIDTH 10 | 13.500 dB |
| HF atten 16 dB | 10 kHz position, ATTEN 10, at 10 kHz | −16.000 dB |

Where the circuit puts the other positions (analog, at the set frequency):

| Position | LF boost 10 | LF atten 10 |
|---|---|---|
| 20 Hz | 12.09 dB (S9 measured 12) | −16.64 dB (S9 17) |
| 30 Hz | 13.58 | −17.55 |
| 60 Hz | 13.50 | −17.50 |
| 100 Hz | 13.58 | −17.55 |

HF atten 10 at the position: −16.00 dB (5 kHz), −16.00 dB (10 kHz),
−16.04 dB (20 kHz). The HF boost is 18.00 dB at Sharp, 15.35 dB at BANDWIDTH
5 and 13.50 dB at Broad, at every position.

### 2.4 Pot tapers [APPROXIMATION, RESEARCH F5]

Each boost/atten pot is r(x) = (B^x − 1)/(B − 1), with x = knob/10. B is
chosen so that **knob 5 gives 40 % of the control's full range in dB** at its
reference position. The values are B = 106.43 (LF boost), 29.33 (LF atten),
0.2975 (HF boost) and 0.2485 (HF atten). B < 1 means a reverse-log pot: the
HF sections' dB-vs-resistance curve is already strongly bent, and the taper
straightens it. BANDWIDTH is a linear rheostat.

Knob law of the analog prototype, in dB (magnitudes) at the reference
positions (60 Hz position; HF boost at 10 kHz with BANDWIDTH 5; ATTEN SEL
10 kHz). CSV: `SHAPE_knob_law.csv`.

| knob | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| LF boost, 60 Hz | 0.50 | 1.24 | 2.28 | 3.67 | 5.40 | 7.38 | 9.40 | 11.21 | 12.60 | 13.50 |
| LF atten, 60 Hz | 0.93 | 2.10 | 3.50 | 5.14 | 7.00 | 9.05 | 11.22 | 13.44 | 15.58 | 17.50 |
| HF boost, 10 kHz (BW 5) | 0.79 | 1.99 | 3.29 | 4.68 | 6.14 | 7.69 | 9.34 | 11.14 | 13.12 | 15.35 |
| HF atten, 10 kHz | 0.72 | 1.96 | 3.33 | 4.81 | 6.40 | 8.10 | 9.94 | 11.90 | 13.96 | 16.00 |

### 2.5 LF boost + atten: the non-cancelling shape [SCHEMATIC_OR_MEASURED, RESEARCH F3]

The two controls share the CPS position but not their corner. The LF atten
uses a capacitor 30× smaller in a branch with a rheostat 10× larger, so its
turnover sits about 2.2× higher than the boost's. Together they give boosted
lows and a dip above. As run (processor-measured at 48 kHz; the evaluator
gives the same values):

| Position | boost = atten = 10: plateau / dip at | boost alone −3 dB turnover | atten alone −3 dB turnover |
|---|---|---|---|
| 20 Hz | +15.8 dB / −6.5 dB at 142 Hz | 92 Hz (S9: 100) | 202 Hz (S9: 250) |
| 30 Hz | +16.2 / −6.5 at 282 Hz | 182 (200) | 402 (400) |
| 60 Hz | +16.3 / −6.5 at 560 Hz | 362 (300) | 781 (800) |
| 100 Hz | +16.4 / −6.5 at 942 Hz | 609 (700) | 1341 (1500) |

At 5/5 the plateau is +2.6 dB and the dip −2.5 dB. At the selected frequency
the pair always still boosts (> +6 dB at 10/10). S7 measured a dip of up to
8 dB, which is deeper than SHAPE's 6.5 dB (open task R3).

### 2.6 HF boost and BANDWIDTH [SCHEMATIC_OR_MEASURED mechanism, APPROXIMATION taper]

Rq in series with the resonator sets its Q, and the peak height follows, as
on the hardware (S3): Sharp is narrow at 18 dB, Broad is wide at 13.5 dB.
Q falls monotonically from Sharp to Broad at every position and the peak
stays at the selected frequency (within 1 %). The same Q holds at all seven
positions, measured on the analog prototype at BOOST 10:

| BANDWIDTH | peak | Q from the −3 dB width | Q from the half-peak-dB width |
|---|---|---|---|
| 0 (Sharp) | 18.0 dB | 2.13 | 0.77 |
| 10 (Broad) | 13.5 dB | 1.11 | 0.53 |

The digital response at 96 kHz host has the same Q within 3 % (test).

### 2.7 HF atten

A shelf whose corner is set by Ca and the pot: −16 dB at full, at the
position. S9 measured turnovers of 600 / 1500 / 2500 Hz for the 5 / 10 /
20 kHz positions, which shows how far down the shelf reaches. SHAPE at the
5 kHz position, ATTEN 10, is −12.9 dB at 3 kHz.

## 3. Controls that select (frequency positions)

Only the hardware's positions exist (S1): LF 20/30/60/100 Hz, HF boost
3/4/5/8/10/12/16 kHz and ATTEN SEL 5/10/20 kHz. These are choice
parameters; no in-between frequencies are invented. A position change glides
the **capacitor value** (LF, ATTEN SEL) or the **resonance frequency** (HF
boost) in the log domain (section 4.5), so a switch never clicks.
[SOURCE_CONFIRMED positions; TLEET_EXTENSION glide]

## 4. Discretisation and the response evaluator

### 4.1 Companion models

Every reactive element becomes its trapezoidal (bilinear) companion model
v = R·i + e. A capacitor has R = 1/(K·C) and history e' = v + R·i. An
inductor has R = K·L and e' = −(v + R·i). The loop then holds only
resistors and sources and is solved in closed form per sample: one loop
current, then the two shunt-branch currents and the history updates
(`net::Channel::process`, 19 precomputed coefficients). The network stays
passive with positive companion resistances, so the filter is stable for any
values, including while they change. [APPROXIMATION: numerical method]

### 4.2 Element-wise pre-warping

Each element has its own bilinear constant K:

- LF capacitors: K = 2·fs (the internal rate is ≥ 176.4 kHz, so the LF
  warp is negligible).
- HF resonator: K is pre-warped at the resonance f0, so the peak sits
  **exactly** at the selected frequency. In addition √(L/C) is scaled by 1/γ,
  γ = ω0T / sin(ω0T), which keeps the digital bandwidth equal to the analog
  one at the peak. At 16 kHz and a 176.4 kHz internal rate, γ = 1.056.
- HF atten capacitor: K is pre-warped at 0.7 × the selected frequency. This
  value is the best compromise for the shelf's middle (APPROXIMATION).

### 4.3 The evaluator: the curve that is drawn is the filter that runs

On the unit circle the bilinear map gives s → K·ψ with ψ = j·tan(ωT/2).
The digital transfer function is therefore the analog formula with C →
R_C/ψ and L → R_L·ψ, where R_C and R_L are the companion resistances
actually used. `net::digitalResponse` evaluates exactly that, and
`eqResponseDb (hz, EqSettings, hostRate)` wraps it at the internal rate for
the host rate (0 dB when EQ IN is Out). `ShapeProcessor::getEqResponseDb (hz)`
returns it for the current parameter values. Both use the same
`elementsFor()` as the audio path, so there is one model.

- Network as run vs the evaluator: max 3.1e-7 dB (sine sweep of the bare
  model, 101 settings × 4 rates).
- Full processor vs the evaluator: max 0.0043 dB at 44.1, 48 and 96 kHz.
  This is a multitone at −40 dBFS through the whole chain (Clean, DRIVE 0),
  as a ratio to the same chain with EQ IN Out, for all 101 settings.

`analogResponseDb` evaluates the continuous-time prototype (no pre-warping).
It is used only to document the digital deviation (4.4).

### 4.4 Digital vs analog prototype

Max |digital − analog| over the 101-setting matrix:

| Host rate (internal) | ≤ 1 kHz | ≤ 10 kHz | ≤ 20 kHz | worst case |
|---|---|---|---|---|
| 44.1 kHz (176.4) | 0.024 dB | 0.142 dB | 0.220 dB | ≤ 10 kHz: 16 kHz boost 10 Sharp, at 6.8 kHz; ≤ 20 kHz: 8 kHz boost 10 Sharp, at 19.8 kHz |
| 48 / 96 / 192 kHz (192) | 0.021 dB | 0.120 dB | 0.188 dB | the same settings (6.8 kHz / 20 kHz) |

The peaks match exactly (pre-warped). The residual sits on the resonator's
skirts: the bilinear map pre-warped at f0 is exact only there, and γ keeps
the peak's height and width at the cost of a few hundredths to 0.14 dB on the
skirts. The ATTEN SEL 20 kHz shelf adds about 0.1 dB
around 10 kHz. Below 1 kHz the deviation is below 0.025 dB everywhere. Tests
hold it under 0.03 / 0.2 / 0.3 dB. [APPROXIMATION, documented]

### 4.5 Time-varying controls

The model keeps a target, a mid and a current `Physical` state (pot
positions, capacitor values, resonance). Every 16 internal samples (83 µs at
192 kHz) it glides them as follows:

| What | Glide |
|---|---|
| BOOST/ATTEN knobs, BANDWIDTH | critically damped, two one-poles of 10 ms each, linear domain |
| frequency selectors (C2, Ca, f0, the Ca pre-warp reference) | critically damped, 2 × 20 ms, log domain |
| CHARACTER voicing constants | one-pole 20 ms (Vc in the log domain) |
| DRIVE | critically damped, 2 × 10 ms, **per sample** |

After each control tick the 19 network coefficients are recomputed. Between
ticks they are interpolated linearly per sample and land exactly on the
control point. A critically damped glide has no slope step at start or end,
so no control produces a kink in the envelope (click test, section 11).

## 5. Analog stages

### 5.1 Level alignment [SOURCE_CONFIRMED S11/S12, APPROXIMATION]

0 dBu = −18 dBFS (EBU R68). S1's +10 dBm is −8 dBFS. DRIVE 0 % puts the
stages at this alignment.

### 5.2 Transformers (input; output, which also stands in for the inter-stage transformer)

The source drives the magnetising inductance. The winding voltage loses the
drop of the magnetising current, which is a function of the core flux:

```
v = x − a·h(φ)               φ = ω_ref ∫ v dt  (normalised: a sine of amplitude A at f has |φ| = A·f_ref/f), f_ref = 20 Hz
h(φ) = φ + k·Φs·f(φ/Φs)      f(u) = u³/(3 + u²)
a = fc/f_ref (pre-warped)    fc = 2 Hz: the linear part is a first-order high-pass at 2 Hz
```

f is odd. It behaves as u³/3 at small flux, so H3 ∝ A²/f³: it rises with
level and with falling frequency, as iron does. At large flux it behaves as
u: the inductance collapses by (1 + k), the LF corner rises, and the low end
thins and compresses. The integral is trapezoidal and solved implicitly per
sample with one Newton step from the exact linear solution. dh/dφ ≥ 1, so
the solution is unique and the stage is passive. Saturation levels:
Φs = 1.0 for the input (a 0 dBFS sine at 20 Hz) and 0.7 for the output.
DC is blocked on the wet path (measured 0 output DC after 4 s of 0.25 DC).
[APPROXIMATION; RESEARCH F6]

### 5.3 Make-up amplifier

```
z = a / Vc,   s = z / √(1 + z²)               odd soft limit: push-pull with negative feedback (S7)
out = Vc · (s + e2 · HP5(s²))                 even term: the single-ended 12AX7 stage, high-passed at 5 Hz (no DC)
```

The small-signal gain is exactly 1, so every CHARACTER preserves the EQ curves
(measured within 0.001 dB at −40/−50 dBFS). [APPROXIMATION]

### 5.4 DRIVE and CHARACTER [TLEET_EXTENSION]

DRIVE 0..100 % = 0..+24 dB into the stages (×D before the input transformer,
×1/D after the output transformer). The level the stages see changes, but
the output level does not. CHARACTER selects one of three voicings. It is
glided, so a switch never clicks.

| CHARACTER | extra drive | output level | amplifier Vc | even share e2 | k input | k output |
|---|---|---|---|---|---|---|
| Clean | 0 dB | 0 dB | 11.22 (+21 dBFS) | 0.002 | 0.6 | 0.9 |
| Original | 0 dB | +1.13 dB | 4.467 (+13 dBFS) | 0.012 | 7.0 | 12.0 |
| Burn | +6 dB | +1.13 dB | 3.162 (+10 dBFS) | 0.04 | 15.0 | 22.5 |

The output level is the hardware's inherent +1.13 dB (S2, F9; owner decision
O4): it applies with EQ IN in or out, after the output transformer, and glides
with a CHARACTER change. Host Bypass and MIX 0 stay at unity.

Original at DRIVE 0 is the hardware reference (5.5). Clean reduces the colour
by about 7× (−17 dB THD) at the reference level. Burn adds 6 dB of drive, a
harder amplifier and more iron.

### 5.5 Measured harmonic tables (48 kHz host, EQ flat, DRIVE 0)

THD at 1 kHz; the 50 Hz THD is in the last column of each row.

| CHARACTER | level | THD 1 kHz | THD+N 1 kHz | H2 | H3 | THD 50 Hz |
|---|---|---|---|---|---|---|
| Clean | −30 dBFS | 0.0003 % | 0.0006 % | −111 dBc | −120 dBc | 0.0004 % |
| Clean | −18 dBFS | 0.0019 % | 0.0020 % | −99 | −96 | 0.0028 % |
| Clean | −8 dBFS | 0.016 % | 0.016 % | −89 | −76 | 0.025 % |
| Clean | 0 dBFS | 0.099 % | 0.099 % | −81 | −60 | 0.144 % |
| Clean | +6 dBFS | 0.389 % | 0.389 % | −75 | −48 | 0.488 % |
| Original | −30 dBFS | 0.0043 % | 0.0043 % | −87 | −104 | 0.0045 % |
| Original | −18 dBFS | 0.020 % | 0.020 % | −75 | −80 | 0.032 % |
| **Original** | **−8 dBFS (+10 dBu)** | **0.112 %** | 0.112 % | −65 | **−60** | **0.272 %** |
| Original | 0 dBFS | 0.623 % | 0.623 % | −58 | −44 | 1.47 % |
| Original | +6 dBFS (+24 dBu) | 2.26 % | 2.26 % | −52 | −33 | 3.92 % |
| Burn | −30 dBFS | 0.040 % | 0.040 % | −68 | −86 | 0.042 % |
| Burn | −18 dBFS | 0.177 % | 0.177 % | −56 | −62 | 0.261 % |
| Burn | −8 dBFS | 0.902 % | 0.902 % | −46 | −42 | 1.89 % |
| Burn | 0 dBFS | 4.22 % | 4.22 % | −40 | −28 | 6.41 % |
| Burn | +6 dBFS | 11.2 % | 11.2 % | −37 | −19 | 9.36 % |

Calibration anchors (tests):

- Original at +10 dBu is ≤ 0.15 % (S1) and H3 > H2 (S7's third-harmonic
  tendency).
- 50 Hz THD is ≥ 2× the 1 kHz THD at the reference level (iron; measured
  2.4×).
- Original at +24 dBu is > 2 % ("crunchy", S7).
- Clean < Original < Burn at every level and both frequencies, and THD rises
  monotonically with level.

DRIVE (Original, 1 kHz, −18 dBFS): 0 % 0.020 %, 25 % 0.052 %, 50 % 0.170 %,
75 % 0.623 %, 100 % 2.26 %. The small-signal gain stays within 0.001 dB.
DRIVE +12 dB at −18 dBFS equals DRIVE 0 at −6 dBFS within 0.05 dB of THD.

Level dependence through the EQ: an LF boost sits before the amplifier and
the output transformer. A 40 Hz tone at −24 dBFS with LF BOOST 10 (60 Hz,
+15 dB) has 1.12 % THD, against 0.054 % flat.

### 5.6 EQ IN [SOURCE_CONFIRMED behaviour (S2, S3), RESEARCH F8]

EQ IN Out crossfades from the network output to its flat equivalent (unity
after make-up) at the network's place in the chain. The crossfade is 20 ms
linear with 2 ms rounded corners, snapped at 0 and 1. The transformers and
the amplifier stay in, so the colour stays and the level does not jump.

- EQ Out with heavy settings deviates **0.000 dB** from flat.
- THD is unchanged: 0.623 % at 1 kHz −6 dBFS, the same with EQ Out as with a
  flat EQ In.
- 40 Hz still distorts more (2.5 %).
- EQ Out differs from the delayed dry by up to 0.137 in sample value (not
  bypass).
- Toggling EQ IN gives a double sine-predictor residual of 3.8e-6 (no click).

`eqResponseDb` returns 0 dB when EQ IN is Out.

### 5.7 Oversampling and aliasing

The worst alias (dBc) of 5/7/9 kHz tones at −1 dBFS, at 44.1 and 48 kHz
host, against the same model run at 1×:

| Setting | oversampled (4×) | same model at 1× |
|---|---|---|
| defaults (Original, DRIVE 25 %) | −128 dBc | −35 dBc |
| Original, DRIVE 100 % (+24 dB into the stages) | −66 dBc | −10..−35 dBc |
| Burn, DRIVE 100 % (+30 dB into the stages, heavily clipped) | −37 dBc | −10..−20 dBc |

Every case is at least 10 dB better than 1×. The extreme corner is limited
by 4× oversampling with a hard-clipping input. Higher factors would cost CPU
(section 14, owner decision O1).

## 6. Internal rate, oversampling, latency

See section 1. `getHostSampleRate()`, `getInternalSampleRate()` and
`getOversamplingFactor()` expose the rates; the model also exposes
`getHostRate()` / `getInternalRate()`.

## 7. MIX, OUTPUT, EQ IN, Bypass

```
y = (dry + (wet − dry) · MIX) · OUTPUT          dry = latency-aligned plugin input
y = Bypass ramp ≥ 1 ? dry : y + (dry − y) · ramp
```

- **MIX** 0..100 %: 30 ms linear ramp with 2 ms rounded corners. MIX 0 with
  OUTPUT 0 dB is the delayed input, **bit-exact**. MIX blends with the
  latency-aligned dry, so it never comb-filters: with HF BOOST 10, MIX 50 %
  gives +10.7 dB at 10 kHz. [TLEET_EXTENSION]
- **OUTPUT** −20..+20 dB, 0 dB at the knob centre, after MIX. 30 ms
  exponential (dB-linear) ramp with 2 ms rounded corners. [TLEET_EXTENSION]
- **Bypass** (host): 20 ms linear crossfade to the latency-aligned input. Fully
  bypassed is bit-exact with the delayed input, whatever the other settings.
- The input is sanitised (NaN/Inf → 0) before anything else, so NaN never
  reaches the state.

## 8. Parameters (namespace `tleet::shape::params`, version 1, IDs frozen)

| ID | Name | Range / choices (exact strings) | Default | getText examples |
|---|---|---|---|---|
| `lfFreq` | LF Frequency | "20 Hz", "30 Hz", "60 Hz", "100 Hz" | "60 Hz" | "60 Hz" |
| `lfBoost` | LF Boost | 0..10, continuous | 0 | "0.0", "6.5", "10.0" |
| `lfAtten` | LF Atten | 0..10 | 0 | "3.0" |
| `hfFreq` | HF Frequency | "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" | "10 kHz" | "16 kHz" |
| `hfBoost` | HF Boost | 0..10 | 0 | "4.2" |
| `hfBandwidth` | HF Bandwidth | 0 (Sharp) .. 10 (Broad) | 5 | "5.0"; text input also takes "Sharp" / "Broad" |
| `hfAttenFreq` | HF Atten Frequency | "5 kHz", "10 kHz", "20 kHz" | "20 kHz" | "10 kHz" |
| `hfAtten` | HF Atten | 0..10 | 0 | "2.0" |
| `drive` | Drive | 0..100 %, step 0.1 | 25 % | "25 %", "72.5 %" |
| `character` | Character | "Clean", "Original", "Burn" | "Original" | "Burn" |
| `mix` | Mix | 0..100 %, step 0.1 | 100 % | "100 %", "18.5 %" |
| `output` | Output | −20..+20 dB, 0 dB at the knob centre (piecewise linear), 0.1 dB grid | 0 dB | "+3.4 dB", "0.0 dB", "-4.6 dB" |
| `eqIn` | EQ In | bool | true | "In" / "Out" |
| `bypass` | Bypass | bool | false | |

- No parameter labels are used. The 0..10 knobs print a bare number, like the
  hardware's unitless dials.
- OUTPUT uses `centredDbRange` (exact inverse, every 0.1 dB value round-trips).
- Defaults are the hardware's flat setting at the usual reference positions,
  with Original voicing and a moderate DRIVE.

## 9. Presets (`presets/*.json`, 12)

Kick Weight, Bass Deep, Sub Push Dip (20 Hz boost 8 / atten 5), Snare Crack,
Burnt Drums (Burn, MIX 70 %), Parallel Glow (Burn DRIVE 70 %, MIX 40 %),
Guitar Polish, Vocal Air, Master Sheen (Clean), Clean Air Lift (Clean, 16 kHz
Broad), Warm Roll-Off (ATTEN SEL 5 kHz) and Amp Colour Only (EQ IN Out, DRIVE
45 %). Every preset sets all 13 automatable parameters; presets of other
plugins are rejected (test).

## 10. Provenance summary

| Block | Tag |
|---|---|
| Passive network topology, reactive values | SCHEMATIC_OR_MEASURED (S4, S5) |
| Rlb, Rla, Rq range, Rstop | APPROXIMATION, calibrated to S1 |
| Control ranges 13.5 / 17.5 / 18 / 16 dB | SOURCE_CONFIRMED (S1) |
| HF boost 13.5 dB at Broad | SOURCE_CONFIRMED trend (S3), APPROXIMATION value |
| Frequency positions | SOURCE_CONFIRMED (S1, S2) |
| LF boost/atten interaction | SCHEMATIC_OR_MEASURED (falls out of the network; S7, S8, S9 agree) |
| Pot tapers (40 % at knob 5), linear BANDWIDTH | APPROXIMATION (F5) |
| Make-up = exact inverse of the flat loss | SCHEMATIC_OR_MEASURED |
| Companion models, element-wise pre-warp, γ | APPROXIMATION (numerical method) |
| Input/output transformer model, constants | APPROXIMATION (F6) |
| Push-pull amplifier with feedback, even term | SOURCE_CONFIRMED stage (S7), APPROXIMATION law |
| THD calibration (0.112 % at +10 dBu) | SOURCE_CONFIRMED target (S1) |
| 0 dBu = −18 dBFS | APPROXIMATION (S11) |
| EQ IN keeps the colour | SOURCE_CONFIRMED (S2, S3) |
| DRIVE, CHARACTER, MIX, OUTPUT ±20 dB, host Bypass | TLEET_EXTENSION |
| Glides of selectors and knobs | TLEET_EXTENSION (hardware switches click) |
| Internal rate ≥ 176.4 kHz | APPROXIMATION (numerical) |

## 11. Verification (tests/ShapeTests.cpp, all `SHAPE:`)

22 tests, all green. CSVs are written to
`<build>/tests/test-output/SHAPE_*.csv`: network vs evaluator, curves, curve
errors, ranges, knob law, LF boost/atten, HF bandwidth, digital vs analog,
THD and aliasing. Key numbers (this container, RelWithDebInfo):

- Registry: IDs, ranges, defaults, texts, choice strings and buses.
- No allocation in processBlock at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz ×
  blocks 1..2048 × mono / stereo. The output is always finite.
- NaN/Inf in: the output stays finite and the state recovers. Silence gives
  silence; DC is blocked; denormal input stays clean.
- Latency is reported = measured (61 / 49 / 0 samples). Host Bypass and MIX 0
  are bit-exact latency-aligned dry at every rate.
- Network vs evaluator: 3.1e-7 dB. Processor vs evaluator: 0.0043 dB (101
  settings, 44.1/48/96 kHz).
- Ranges as in 2.3; flat network < 1e-9 dB.
- LF interaction as in 2.5 (processor ≤ 0.01 dB from the evaluator at every
  position, 5/5 and 10/10).
- BANDWIDTH as in 2.6; digital vs analog as in 4.4.
- EQ IN vs Bypass as in 5.6; THD tables as in 5.5; aliasing as in 5.7.
- Every visible control changes the audio:
  - LF BOOST 10: +13.49 dB at 60 Hz; LF ATTEN 10: −17.51 dB.
  - LF 20 Hz: +5.2 dB at 60 Hz.
  - HF BOOST 10 (BW 5): +15.35 dB at 10 kHz; HF 3 kHz: +4.0 dB at 10 kHz.
  - Sharp/Broad: 18.0 / 13.5 dB; at 4 kHz relative to the peak, −12.1 / −7.3 dB.
  - ATTEN SEL 5 kHz: −12.9 dB at 3 kHz; 20 kHz: −3.8 dB.
  - EQ Out: −0.01 dB (the iron's 2 Hz corners).
  - OUTPUT +6: +6.00 dB; MIX 50 %: +10.7 dB.
  - DRIVE 100 vs 0 and Burn vs Clean: > 10× THD.
- EQ automation during playback lands on the evaluator's curve. After
  moving all eight EQ controls (selectors included) while audio runs, the
  multitone response is ≤ 0.0013 dB from `eqResponseDb`.
- Click-free switching of all 13 controls (selectors included). Worst double
  sine-predictor residual: 5.3e-5 (CHARACTER); every other control ≤ 6e-6
  (limit 1e-4 or 2× the fixed-setting floor). Rapid random automation of
  everything stays finite.
- Mono equals stereo-left (0 difference). The state round trip renders
  identically. 12 presets load.
- CPU (one instance, stereo, block 128, this container): 4.9 % of one core
  at 44.1 kHz, 4.7 % at 48 kHz, 4.5 % at 96 kHz, 3.0 % at 192 kHz (median
  block). The 4× oversampled network + stages dominate.

## 12. Mutation checks

Each defect was introduced alone, the SHAPE tests were run, and the source
was restored.

| Mutation | How it was introduced | Caught by (failing SHAPE tests) |
|---|---|---|
| Coefficients not updated after prepare | `controlUpdate` recomputes the network only on a snap | "EQ changes during playback (automation) land on the evaluator's curve" (max error far above 0.01 dB). This test was added because the first run of this mutation survived: every other curve test sets the parameters before `prepareToPlay`. |
| LF atten mirrored as the boost shelf | C1 = C2 · Rlb/Rla (equal RC corners) | control ranges; LF boost + atten do not cancel (atten turnover no longer above the boost's); every control changes the audio (LF ATTEN −17.5 dB) |
| BANDWIDTH ignored | Rq fixed at mid-range | control ranges (18 / 13.5 dB at Sharp / Broad); HF BANDWIDTH Q sweep; every control changes the audio |
| EQ IN acting like host bypass | EQ Out crossfades the whole chain to its input | EQ IN Out keeps the colour (THD with EQ Out 0.000009× the EQ In value; 40 Hz colour gone; EQ Out ≠ flat network); every curve family and LF interaction (their reference is EQ Out, which lost the transformers' 2 Hz corners: 0.086 dB at 20 Hz) |
| CHARACTER ignored | `setCharacter` does nothing | THD tables (Clean < Original < Burn at every level, Clean ≤ 0.2× Original); symmetry (Burn H2 > 2× Original); every control (Burn > 10× Clean THD); curve families, LF interaction and automation (the Clean reference no longer applies) |
| Dry not latency-aligned | `dry.prepare (…, 0)` | latency / Bypass / MIX 0 bit-exact (every rate with latency); EQ IN vs host Bypass |

All six were restored afterwards. The final run has every test green.

## 13. Not modelled (intentionally)

- The inter-stage transformer as a separate stage: its colour is folded into
  the output transformer.
- Tube noise and hum; the 6X4 power supply sag.
- Component tolerances and the parasitics S8 fitted.
- The switches' own clicks (positions glide instead).

## 14. Open measurement tasks and owner decisions

Measurement tasks (RESEARCH R1–R5):

1. Network values against S6 / S10 schematics (R1).
2. Pot tapers and the BANDWIDTH rheostat law (R2).
3. LF boost+atten dip depth (S7: 8 dB vs 6.5 dB) and the 20/100 Hz turnovers
   (R3).
4. THD vs level/frequency and harmonic balance of a real unit (R4).
5. The 1.13 dB level and what EQ OUT puts in place of the network (R5).

Owner decisions:

- **O1 CPU / eco mode.** Four-times oversampling at 44.1/48 kHz costs about
  5 % of a core per stereo instance. An "eco" 2× option would roughly halve
  the cost of the network and stages (not measured), at the cost of more
  aliasing in the Burn/DRIVE-high corner and a larger HF warp (176.4 → 88.2
  kHz internal). It is not implemented, because the spec defines no such
  control.
- **O2 Tapers.** Knob 5 = 40 % of the range in dB is a guess (F5). Change it
  once a real unit is measured.
- **O3 γ trade-off.** The resonator keeps an exact peak frequency, height and
  width at the cost of ≤ 0.14 dB on its skirts below 10 kHz (4.4). Without γ
  the skirts would follow the plain bilinear map, but the bandwidth at 16 kHz
  would be about 5 % narrower (1/γ) than the analog prototype's (γ = 1.056 at a
  176.4 kHz internal rate).
- **O4 Level. Decided by Andrew 2026-10-13:** the hardware's +1.13 dB level
  (S2, F9) is part of the voicing: Original and Burn are +1.13 dB at flat
  settings, with EQ IN in or out; Clean stays at unity. Host Bypass and MIX 0
  are unity. A CHARACTER change glides the level (no click). Tested in
  `tests/ShapeTests.cpp` (THD test and EQ IN test). [SOURCE_CONFIRMED level,
  TLEET_EXTENSION per voicing]
- **O5 Defaults.** DRIVE 25 % (+6 dB into the stages) and BANDWIDTH 5 make the
  default slightly coloured and mid-Q. DRIVE 0 would be the hardware level.
