# VEIL — model specification

Stereo variable-mu compressor with Manley Variable Mu DNA. This file is the
top of the priority order for VEIL's DSP: where it conflicts with the master
spec or the mockup, this file wins. The evidence for every law is in
`RESEARCH.md` (sources S1..S11, findings F1..F10, open tasks R1..R4).

Provenance tags:

- **[SOURCE_CONFIRMED]** published by Manley, or in a Manley-licensed manual
  (S1–S3).
- **[SCHEMATIC_OR_MEASURED]** read from schematics, valve data or measurements
  of a real unit. No schematic or measurement of the Variable Mu was available
  (RESEARCH.md). The tag is used only for the remote-cutoff principle (S7).
- **[APPROXIMATION]** a modelling choice where no source gives the value.
  Section 14 lists what to measure.
- **[TLEET_EXTENSION]** deliberately not on the hardware.

VEIL is hardware-informed. It is not a measured 1:1 clone of one unit.

## 1. Signal flow

```
host rate                                    2x rate (FIR half-band, linear phase)                         host rate
in ─► INPUT ─► HPF ─► coupling HP 2 Hz ─► ↑2 ─► input tube stage (a) ─┬─► gain element b = g·(a + colour) ─┐
                                                                      │                                    ├─ m = a + MIX (b − a) ─► output stage ─► ↓2 ─►
                                                                      └──────────────────────────────────── ┘
           LF weight (CHARACTER) ─► OUTPUT ─► DRY/WET (with the latency-aligned plugin input) ─► Bypass ─► out ─► spectrum tap

detector, per channel, 2x rate:
   internal: a (the input-stage output)      external: key ─► ↑2 (same filters) ─► coupling HP 2 Hz
          └──────────────┬──────────────────────────────┘   (20 ms crossfade between the two sources)
                         ▼
   SC HPF (12 dB/oct) ─► |·| ─► one-pole 2.5 ms ─► one-pole 2.5 ms ─► L = 20 log10(π/2 · avg)  (dBFS, sine peak)
   ─► vari-mu law: steady GR target T(L − threshold, COMPRESS)
   ─► ballistics: c += (T > c ? k_attack : k_recovery)(T − c)        (GR in dB)
   ─► stereo link: cv_ch = c_ch + link·(max(c_L, c_R) − c_ch)       ─► g_ch = 10^(−cv_ch/20)
```

- **One oversampled region.** Every nonlinear block (the input stage, the gain
  element, the output stage) runs at 2x inside a single up/down pair
  (`juce::dsp::Oversampling`, `filterHalfBandFIREquiripple`, max quality,
  integer latency). The detector also runs at 2x on the same samples. The
  external key is upsampled by a second oversampler with identical filters,
  so it stays time-aligned with the audio. [APPROXIMATION: numerical method]
- **Latency 49 samples at every host rate** (1.11 ms at 44.1 kHz, 1.02 ms at
  48 kHz), reported, and measured equal by impulse (section 12). DRY/WET and
  Bypass use the latency-aligned plugin input (`dsp::LatencyAlignedDry`).
- INPUT, HPF, coupling, LF weight, OUTPUT, DRY/WET and Bypass are linear or
  time-varying gains and run at the host rate.
- Everything in the model runs in double precision, except the output stage's
  `powf` (section 6.3).

## 2. Detector and the vari-mu law

### 2.1 Detector feed

The hardware's side-chain reads the **output** of the gain stage (feedback,
S1–S3). VEIL keeps the feedback **static curve** (2.3) exactly, but computes it
from the level **before** the gain element: the input-stage output `a`
(internal key) or the key input (external key). Reading the input and solving
the feedback relation for the output gives the same steady GR without a
closed loop (2.4). [SOURCE_CONFIRMED topology (F1); APPROXIMATION
realisation]

- Internal key: after INPUT, the audio HPF, the coupling high-pass and the
  input tube stage. So INPUT, TUBE DRIVE (only through the stage's soft limit,
  see 6.1) and the audio HPF move the GR; HARMONICS, SATURATION, MIX, OUTPUT
  and DRY/WET never do (section 12: GR change 3e-5 dB, 0 dB, 0 dB).
- External key: after its own 2 Hz coupling high-pass. It does **not** see
  INPUT or the audio HPF.
- Both keys go through the SC HPF (4.1).
- Stereo key with stereo main: key L drives L, key R drives R. A mono key
  drives both channels. With a mono main and a stereo key, the key is the mean
  of the two **rectified** key channels.

### 2.2 Rectifier and smoothing

Full-wave rectifier (the 12AL5 is a double diode, S2), then **two identical
one-pole low-passes with τ = 2.5 ms each** at the 2x rate (the rectifier's
reservoir and the grid filter). The level is

```
L = 20 log10(π/2 · avg)     dBFS, sine-peak calibrated (AES17, S10)
```

so a steady sine peaking at P dBFS reads exactly P. [APPROXIMATION: the
time constant and the second pole]

Rectifier ripple: on low tones the residual ripple of the two poles is still
present, and the diode-steered attack (3) follows it toward its peaks. A 50 Hz
sine reads **+1.0 dB** of GR above the static law, a 120 Hz sine **+0.24 dB**,
1 kHz and up **< 0.003 dB** (section 12). This is the low-frequency
"grab" of a real vari-mu. It is documented and tested, not corrected.
[APPROXIMATION]

### 2.3 The vari-mu law

Levels in dB relative to the threshold:

```
x = L − threshold                (input over threshold)
y                                 (output over threshold),   v = 10^(y/20)
e = knee(v − 1, w)                rectified excess above the delay bias, in threshold-amplitude units
GR = G(e) = (K/λ)(exp(λ e) − 1)   (λ → 0: K e)                    remote-cutoff valve
x = y + G(e(y))                   the feedback relation, solved exactly for y
```

- **knee**: quadratic in amplitude. With `v_lo = 1 − w/2`:
  `e = 0` for `v ≤ v_lo`; `e = (v − v_lo)² / (2w)` for `v_lo < v < 1 + w/2`;
  `e = v − 1` above. The knee starts at `20 log10(1 − w/2)` dB below the
  threshold.
- **valve law**: the gain in dB falls linearly with the control voltage near
  the operating point and faster toward cutoff (S6, S7, F3). G is capped at
  **60 dB**, the valve's cutoff. [SCHEMATIC_OR_MEASURED principle;
  APPROXIMATION constants]
- **local ratio** `dx/dy = 1 + G'(e) e'(v) v ln10/20`. Above the knee this is
  `1 + 0.1151 (1 + e)(K + λ GR)`. It **rises with the GR at every COMPRESS**,
  because the rectifier is linear in amplitude while the valve is linear in
  dB (F2, F3). [SOURCE_CONFIRMED trend: S1–S3; APPROXIMATION shape]

**COMPRESS** (0..10, continuous) is a macro over the three constants, with
`t = COMPRESS / 10`: [TLEET_EXTENSION]

| constant | law | COMPRESS 0 | 5 | 10 |
|---|---|---|---|---|
| K (side-chain gain, dB per unit excess) | `K0 (K1/K0)^t`, `K0 = 0.25/0.1151`, `K1 = 3/0.1151` | 2.17 (onset 1.25:1) | 7.52 | 26.06 (onset 4:1) |
| λ (cutoff curvature) | `9.5 t²` | 0 | 2.375 | 9.5 |
| w (knee width, amplitude) | `1.0 − 0.6 t` | 1.0 (knee from −6.0 dB) | 0.7 (−3.7 dB) | 0.4 (−1.9 dB) |

Measured ratios of the pure law (`law::localRatio`, section 12):

| COMPRESS | onset | at 3 dB GR | at 10 dB GR | at 12 dB GR |
|---|---|---|---|---|
| 0 | 1.25 | 1.60 | 2.40 | |
| 2.5 | | 2.08 | 3.90 | |
| 3 | 1.53 | | | |
| 5 | 1.87 | 2.93 | 6.76 | |
| 7.5 | | 3.79 | 11.05 | |
| 10 | 4.00 | 5.11 | 15.53 | 18.85 |

Calibration (F2): COMPRESS 10 is the hardware's LIMIT (onset 4:1, rising to
about 20:1 past 12 dB, S1–S3). COMPRESS 3 sits on the hardware's COMPRESS
(about 1.5:1, soft knee). COMPRESS 0 is gentler than the hardware. GR is not
capped at 10 dB; RESEARCH.md F2 explains the conflict with S3.

### 2.4 Dynamics: steady target plus exact ballistics

The loop is **not** simulated as a closed feedback loop. For every 2x sample,
the model solves `x = y + G(e(y))` for the current detector level (the steady
GR the loop would settle at) and the ballistics (3) move the GR toward it.
This keeps the attack and recovery knobs exact time constants and avoids the
ringing of a two-pole loop with a loop gain of up to 20 at a 10 ms attack.
[APPROXIMATION, deliberate: F1; open task R3]

Solver: safeguarded Newton on `F(y) = y + G(e(y)) − x` (`F' ≥ 1`), bracketed
by [knee start, x]. It starts from a first-order prediction (previous root +
Δx / previous slope) and accepts a step below 1e-5 dB, so a single
evaluation is normally enough. Measured against the pure bisection law
(`solveOutputDbBisection`, 1e-13 dB): **max error 5.0e-10 dB** over 150
level / COMPRESS / threshold points (section 12).

### 2.5 Pure functions (what the editor draws)

`dsp/VeilLaw.h`, namespace `tleet::veil::law`:

- `staticGainReductionDb(levelDbfs, thresholdDbfs, compress)`: steady GR, dB ≥ 0.
- `staticOutputDbfs(levelDbfs, thresholdDbfs, compress)`: steady output of the gain element.
- `localRatio(levelDbfs, thresholdDbfs, compress)`: dx/dy at that level.
- `constantsFor(compress)`, `kneeStartDb(constants)`.

The level is the detector level (2.2). For the processor at high input
levels, the input stage's soft limit (6.1) comes first: the static-curve test
uses the input-stage output level (`inputStageSine` in the tests) and then
matches the processor's GR telemetry to **0.003 dB** and its output level to
**0.003 dB** from −38 to +6 dBFS (section 12).

### 2.6 Threshold calibration

- THRESHOLD knob −20..+20 dB; **knob 0 = −18 dBFS** (EBU R68 alignment
  level, S9; the hardware's 0 VU, +4 dBu, S1). The travel therefore covers
  **−38..+2 dBFS** (`params::thresholdToDbfs(knob) = knob − 18`).
- The threshold is the centre of the knee in the law above (v = 1): the
  output level, in sine-peak dBFS, where the rectified excess starts to grow
  linearly. GR begins at the knee start (2.3), 1.9..6.0 dB below.
- The hardware's LIMIT onset range (−20..+10 dBm) maps to about −42..−12 dBFS
  at this alignment. VEIL's range is shifted up by 10 dB for mastering
  headroom; INPUT covers the rest. [SOURCE_CONFIRMED 0 VU; APPROXIMATION
  digital alignment]

## 3. Ballistics (ATTACK, RECOVERY)

The control voltage `c` is the GR in dB (for an exponential valve the bias
voltage and the GR in dB are proportional, F4). Per 2x sample:

```
c ← c + k (T − c),   k = 1 − exp(−1 / (τ f_2x)),   τ = ATTACK if T > c, else RECOVERY
```

This is a diode-steered RC network: charge through the attack resistor,
discharge through the recovery resistor. Both knobs are **exact time
constants of the GR in dB**. [SOURCE_CONFIRMED ranges of the hardware;
APPROXIMATION definition; TLEET_EXTENSION ranges]

- ATTACK 10..1000 ms, log, centre 100 ms, default 50 ms (the hardware's
  12 o'clock, S1). RECOVERY 50..2000 ms, skew centre 400 ms, default 400 ms
  (UA's default, S3).
- Measured (section 12): the fitted tail τ equals the knob to **6e-5 %**
  (attack 10, 30, 100, 300, 1000 ms; recovery 50, 150, 400, 1000, 2000 ms).
  The full attack trace matches an RK4 reference of the documented system
  (two-pole detector + the law + the diode-steered capacitor) to
  **0.006 dB**. Through the whole processor: recovery 400 ms fits 400.0 ms,
  attack 100 ms fits 100.1 ms.
- The time to 63 % of the GR is longer than the knob by the detector's own
  smoothing: 15.4 ms at ATTACK 10 ms, 105.0 ms at 100 ms. The 1/e recovery
  time is the knob + 4.2 ms.

## 4. Filters

All are TPT structures from the bilinear transform with pre-warped cutoffs, in
double precision (`dsp/Filters.h`). `hpfResponseDb` and `shelfResponseDb`
evaluate the discrete-time transfer functions of the same structures with the
same coefficients, so a drawn curve is the filter that runs.

### 4.1 SC HPF (detector only)

12 dB/oct Butterworth SVF high-pass (`k = √2`), 20..300 Hz, skew centre
80 Hz (knob top), **20 = Off**, default 60 Hz. It filters the key (internal or
external) before the rectifier, at the 2x rate. It never touches the audio.
[TLEET_EXTENSION: the hardware's HP SC is a 6 dB/oct switch at 100 Hz (S2, S3,
F8)]

- Response: `hpfResponseDb(hz, scHpf, processor.getDetectorSampleRate())`.
- Measured against a sweep at 88.2, 96 and 384 kHz detector rates, cutoffs 25,
  60, 80, 150, 300 Hz: **max error 6e-9 dB**.
- In the processor, the GR for a tone with the SC HPF at f equals the GR with
  the SC HPF Off at (level + response) to 0.05 dB.
- A 50 Hz kick under a 1 kHz tone: the tone's level swing (pumping) falls from
  8.7 dB (Off) to 0.28 dB (300 Hz), a factor of 31.

### 4.2 HPF (audio path)

Owner decision 2026-10-13 (F9). [TLEET_EXTENSION]

- 12 dB/oct Butterworth, 20..300 Hz, skew centre 100 Hz (knob top), **20 = Off**,
  **default Off**.
- Position: plugin input, after INPUT, before the coupling high-pass, the
  input tube stage and the internal detector tap. The internal key sees the
  high-passed signal; the external key does not. It is **not** on the DRY/WET
  dry path.
- Runs at the host rate. Response: `hpfResponseDb(hz, hpf, processor.getHostSampleRate())`.
- Measured: model sweep **< 2e-9 dB** at 44.1, 48 and 192 kHz. Through the
  processor (with the coupling high-pass added to the expectation)
  **< 0.003 dB**.
- Off is bit-transparent: the processor with HPF Off renders bit-identically
  to the same chain without the filter.

### 4.3 Switching and gliding (both high-passes)

- The cutoff glides in the log domain, about 20 ms, updated every 16 samples.
- Off ↔ on is a 20 ms linear crossfade with 2 ms rounded corners. Once fully
  Off, the filter is not computed and its state is cleared, so Off is
  bit-transparent. Switching on restarts from rest at the chosen cutoff.

### 4.4 Coupling high-pass

First-order 2 Hz high-pass on the audio (host rate, after the HPF) and on the
external key (2x rate): the input transformer's coupling. It keeps DC out of
the tube stages and the detector (−0.04 dB at 20 Hz). DC on the input gives no
steady GR and no DC on the wet output; DRY/WET keeps the input's DC.
[APPROXIMATION, F10]

### 4.5 LF weight (output transformer)

First-order low shelf at 60 Hz, after the downsampler: **0 / +0.5 / +1.2 dB**
for Clean / Veil / Thick (CHARACTER). Measured at 30 Hz re 1 kHz, including
the coupling high-pass: −0.02 / +0.38 / +0.96 dB. The shelf gain glides per
sample (20 ms), so switching CHARACTER does not step. [APPROXIMATION]

## 5. Stereo link

The hardware combines the DC control voltages, not the audio, and both
channels get the same GR (S1, F5).

- **Linked** (default): each channel keeps its own detector and timing
  network; both apply the **larger** of the two control voltages, so the
  louder side wins, as with two diodes charging tied control lines.
  [SOURCE_CONFIRMED link of the DC control; APPROXIMATION max]
- **Dual Mono**: independent channels.
- Switching crossfades over 20 ms (rounded corners).
- Measured: signal only in L, dual mono → GR L 11.64 dB, GR R 0 dB; linked →
  11.64 / 11.64 dB (bit-equal gains, max |GR L − GR R| = 0).
- Not modelled: the shared-capacitor timing interaction of a real tied network.

## 6. Tube colour stages

All constants are [APPROXIMATION], calibrated against S1/S2's THD figures and
the tables measured in 6.5. The controls are [TLEET_EXTENSION]s (the hardware
has INPUT/OUTPUT only; S1, S3: input level sets the colour).

### 6.1 Input tube stage (TUBE DRIVE, HARMONICS, CHARACTER)

```
z = a D / Vc,   u = z / sqrt(1 + z²)                         (soft grid/plate limit, odd)
y = (u + h (b2 · HP5(u²) − b3 u³)) · Vc / D                   (low-order generator)
```

- D = 10^(24 dB · DRIVE / 20): TUBE DRIVE is the level into the stage, 0..+24 dB
  (default 28 % = +6.7 dB). The exact `1/D` after it is the loudness
  compensation, so the small-signal gain is exactly 1 at every drive
  (measured at −40 dBFS: within 0.004 dB from 0 to 100 %).
- Vc = 4 × headroom(CHARACTER): +15.0 / +12.0 / +10.0 dBFS (Clean / Veil / Thick).
- h = HARMONICS (0..1): the strength of the 2nd- and 3rd-order terms. It never
  moves the level or the limit. HP5 is a 5 Hz DC blocker on the even term.
- b2, b3 per CHARACTER (6.4).

### 6.2 Gain element (vari-mu valve pair)

```
b = g · (a + ρ h E Ve (e2 · HP5(q²) − e3 q³)),   q = a / Ve,   ρ = GR / (GR + 10 dB)
```

Ve = 4, e2 = 0.05, e3 = 1, E per CHARACTER. A balanced push-pull pair: mostly
odd order, a little even from mismatch (S1 "entirely differential", F6). The
distortion grows with the bias, i.e. with the GR (S5, S6). With no GR (ρ = 0),
or with HARMONICS 0, the element is the ideal gain `g`.

### 6.3 Output stage (SATURATION, CHARACTER)

```
y = m / (1 + |m / C|^p)^(1/p),   C = 10^((24 − 24 s) / 20)    (+24 dBFS at s = 0 .. 0 dBFS at s = 1)
```

- s = SATURATION (0..1) is the depth: how far the ceiling comes down. Default
  18 % puts C at +19.7 dBFS.
- Symmetric, so odd harmonics only. p (knee softness) per CHARACTER.
- s = 0 bypasses the stage. Below 5 % the stage is blended in linearly
  (`m + (s/5 %)(y − m)`), so the knob is continuous at 0.
- Single-precision `powf` (Veil's p = 2 uses `sqrt`): relative error ~1e-7.

### 6.4 CHARACTER voicings

| | b2 | b3 | headroom (Vc) | element E | output p | LF weight |
|---|---|---|---|---|---|---|
| **Clean** | 0.02 | 0.01 | 1.41254 (+15 dBFS) | 0.25 | 3.0 (hard, late) | 0 dB |
| **Veil** (default) | 0.12 | 0.04 | 1.0 (+12 dBFS) | 1.0 | 2.0 | +0.5 dB |
| **Thick** | 0.30 | 0.10 | 0.79433 (+10 dBFS) | 2.0 | 1.6 (soft, early) | +1.2 dB |

The voicing parameters glide at the control rate (20 ms), so switching
CHARACTER is click-free. Clean is the cleanest at every level (6.5).

### 6.5 Measured harmonic tables (1 kHz, 48 kHz host, defaults 28/42/18, no GR)

THD (%) by CHARACTER and level:

| level (dBFS) | Clean | Veil | Thick |
|---|---|---|---|
| −30 | 0.0054 | 0.043 | 0.136 |
| −18 | **0.036** | **0.18** | 0.55 |
| −12 | 0.12 | 0.42 | 1.14 |
| −6 | 0.46 | 1.15 | 2.56 |
| 0 | 1.73 | 3.55 | **6.23** |

Anchors: the hardware is < 0.1 % at 1 kHz at nominal, 0.2 % 20 Hz..10 kHz
typical (S1, S2). Veil at −18 dBFS (0 VU) = 0.18 %, Clean 0.036 %, Thick at
0 dBFS = 6.2 %, which is the "colour by driving the input" region (S1).

Each control does its own job (Veil, section 12):

| control | effect | measured |
|---|---|---|
| TUBE DRIVE 0 / 25 / 50 / 75 / 100 % | THD at −18 dBFS | 0.080 / 0.17 / 0.37 / 0.99 / 3.0 % |
| | the drive is the level: THD at −18 dBFS with +12 dB drive = THD at −6 dBFS with 0 drive | to 3e-5 dB |
| | H3/H2 rises with drive (the limit is odd) | −15.8 dB → +8.2 dB |
| | small-signal gain | within 0.004 dB |
| HARMONICS 0 / 21 / 42 / 84 % | H2 at −12 dBFS, proportional to the setting | −190 / −55.4 / −49.4 / −43.4 dBc |
| | fundamental | within 0.004 dB |
| SATURATION 0 / 25 / 50 / 75 / 100 % | H3 at −6 dBFS | −40.8 / −40.4 / −39.2 / −35.8 / −29.2 dBc |
| | H2 (must not move) | −43.7..−44.3 dBc |
| gain element, GR 0 / 5 / 10 / 15 / 21 dB | THD (Veil, HARMONICS 100 %) | 0.37 / 0.44 / 0.47 / 0.49 / 0.51 % |
| GR at −18 dBFS vs colour controls | DRIVE / HARMONICS / SATURATION / CHARACTER | −0.027 / −3e-5 / 0 / −0.007 dB |
| INPUT +6 dB (for comparison) | GR / THD | +5.1 dB / +7.3 dB |

### 6.6 Oversampling and aliasing

2x, linear-phase FIR half-band. Worst in-band alias of a high tone, 44.1 and
48 kHz, tones 5 / 7 / 9 kHz (section 12):

| setting | 2x (VEIL) | 1x (no oversampling) | model at 4x (information) |
|---|---|---|---|
| defaults, −1 dBFS | **−112 dBc** | −31 dBc | |
| hot: Thick, DRIVE/HARMONICS/SATURATION 60 %, −3 dBFS | **−61 dBc** | | about −102 dBc |
| extreme: Thick, all three 100 % (+24 dB drive), −1 dBFS | **−28 dBc** | −11 dBc | about −49 dBc |

At 2x the defaults and moderate colour are clean. The extreme corner (a
near-square wave from +23 dB into the input stage, 9 kHz at −1 dBFS) aliases
audibly. 4x would cost about twice the model's CPU (section 12). This is an
owner decision, open item 14.6. [APPROXIMATION]

## 7. MIX, OUTPUT, DRY/WET, Bypass

- **MIX** (0..100 %, default 100 %) is a parallel blend **inside** the
  oversampled region, before the output stage: `m = a + MIX (b − a)`, between
  the coloured-but-uncompressed signal `a` and the compressed signal `b`.
  MIX 0 still has the input stage, the output stage and the LF weight.
  [TLEET_EXTENSION]
- **OUTPUT** (−20..+20 dB) is a gain after the LF weight, before DRY/WET.
- **DRY/WET** (0..100 %, default 100 %) is a global linear crossfade with the
  latency-aligned plugin input (before INPUT): `y = dry + (wet·OUTPUT − dry)·dw`.
  **DRY/WET 0 is bit-exact delayed input.**
- **Bypass** is a 20 ms ramp to the latency-aligned input; fully bypassed is
  bit-exact delayed input.
- Smoothing: INPUT and OUTPUT multiplicative 30 ms; DRY/WET linear 30 ms with
  2 ms rounded corners; MIX, link and the key source linear 20 ms with rounded
  corners; THRESHOLD, COMPRESS, DRIVE, HARMONICS, SATURATION and the voicing
  glide exponentially (20 ms).
- Measured: MIX is linear between MIX 0 and 100 to 1.2e-7 with SATURATION 0,
  and is provably before the output stage (0.028 difference with SATURATION
  100). DRY/WET is linear to 3e-8. OUTPUT is an exact gain to 1e-7. Below
  threshold, every MIX × DRY/WET combination is within 0.0013 dB of unity.

## 8. Metering and telemetry

### 8.1 GR telemetry (per channel, actually applied)

`VeilProcessor` (audio thread writes once per processed chunk, UI reads
lock-free):

```cpp
std::array<dsp::MeterSource, 2> inputMeters, outputMeters, grMeters;
float takeGrDb (int ch);          // deepest applied GR of channel ch since the last call (dB >= 0);
                                  // the latest value if no audio was processed since
float getCurrentGrDb (int ch) const;   // GR of the last processed sample
params::MeterMode getMeterMode() const;
bool isUsingExternalSidechain() const;
double getHostSampleRate() const;      // for hpfResponseDb (hpf)
double getDetectorSampleRate() const;  // for hpfResponseDb (scHpf), = 2x host
dsp::SpectrumTap spectrum;             // final output, after DRY/WET, also in bypass
```

- Index 0 = left, 1 = right. With a mono main bus both carry the one channel.
- `inputMeters[ch]`: the plugin input before INPUT, `pushLevel(sum of squares, n, peak)`.
- `outputMeters[ch]`: the plugin output after DRY/WET and Bypass.
- `grMeters[ch]`: `pushGainReduction(deepest GR the channel's gain element applied in the chunk)`.
- The value is `cv_ch` (after the link), which is exactly `−20 log10(g_ch)`.
  Measured: |takeGrDb − deepest applied GR from the gain trace| ≤ **1.3e-6 dB**
  (dual mono and linked, block 64). In steady windows, |telemetry − measured
  attenuation of the audio| ≤ **0.001 dB** (the reference is MIX 0, so the
  input stage's own soft limit is not counted).
- Deep GR is not clipped by the telemetry (24.2 dB at COMPRESS 0, 58 dB over
  threshold).

### 8.2 VU calibration

**0 VU = a sine at −18 dBFS** (S1's 0 VU = +4 dBu at the EBU R68 alignment):
`params::vuDb(rms) = 20 log10(rms·√2) + 18`. A −18 dBFS sine reads 0.0 VU
(measured −1e-5 dB), −24 dBFS reads −6.0. VU ballistics (IEC 60268-17) run in
the editor (`dsp::VUBallistics`).

### 8.3 METER mode

GR / VU / In/Out choose what the editor draws. The parameter is deliberately
never passed to the DSP: the audio is bit-identical in all three modes (test).

### 8.4 Spectrum tap

`spectrum.setSampleRate()` is called in prepareToPlay; `spectrum.push(L, R or
null, n)` on the final output (after DRY/WET; also in bypass). Inactive, it
costs one atomic load per block (measured about 1 ns per push) and delivers
nothing. Active, the delivered samples are exactly the output, and enabling
it does not change the audio.

## 9. External side-chain

Exactly as WEIGHT/GLUE:

- Optional input bus "Sidechain", mono or stereo, **disabled by default**.
- SIDECHAIN Ext uses the key only when the host has enabled the bus with at
  least one channel. Otherwise VEIL falls back to the internal key
  (`isUsingExternalSidechain()` reports which is live).
- Int/Ext switching crossfades the detector source over 20 ms (rounded): max
  sample jump 0.008 while toggling under compression.
- The key is never audible: silent main + loud key → output exactly 0; the key
  frequency in the output −178 dBFS.
- Measured: sine key at −8 dBFS → GR = static law to 0.0003 dB, and the main
  signal's attenuation equals the GR to 0.002 dB. Transient key: decay after
  one recovery time is 0.376 (e^−1 = 0.368, plus the rectifier's own decay).
  The SC HPF acts on the key (60 Hz key: 15.6 dB GR Off, 0 dB at 300 Hz); the
  audio HPF does not.

## 10. Parameters (namespace `tleet::veil::params`, version 1, IDs frozen)

| ID | name | range / choices (exact strings) | skew / step | default | getText |
|---|---|---|---|---|---|
| `input` | Input | −20..+20 dB | 0 dB at the knob centre (piecewise linear), 0.1 dB | 0 dB | "+3.4 dB", "0.0 dB", "-4.6 dB" |
| `threshold` | Threshold | −20..+20 dB (= −38..+2 dBFS) | as input | 0 dB (−18 dBFS) | as input |
| `attack` | Attack | 10..1000 ms | log, centre 100 ms | 50 ms | "50 ms", "120 ms" |
| `recovery` | Recovery | 50..2000 ms | setSkewForCentre(400) | 400 ms | "400 ms", "1.2 s" |
| `compress` | Compress | 0..10 | linear, continuous | 5 | "5.0" |
| `mix` | Mix | 0..100 % | linear, 0.1 | 100 % | "100 %", "18.5 %" |
| `output` | Output | −20..+20 dB | as input | 0 dB | as input |
| `tubeDrive` | Tube Drive | 0..100 % | linear, 0.1 | 28 % | as mix |
| `harmonics` | Harmonics | 0..100 % | linear, 0.1 | 42 % | as mix |
| `saturation` | Saturation | 0..100 % | linear, 0.1 | 18 % | as mix |
| `dryWet` | Dry/Wet | 0..100 % | linear, 0.1 | 100 % | as mix |
| `hpf` | HPF | 20..300 Hz, 20 = Off | setSkewForCentre(100) | 20 (Off) | "Off", "100 Hz" |
| `scHpf` | SC HPF | 20..300 Hz, 20 = Off | setSkewForCentre(80) | 60 Hz | "Off", "60 Hz" |
| `character` | Character | "Clean", "Veil", "Thick" | choice | "Veil" | the choice string |
| `scExternal` | Sidechain | bool | | false | "Int" / "Ext" |
| `stereoLink` | Stereo Link | bool | | true | "Linked" / "Dual Mono" |
| `meterMode` | Meter | "GR", "VU", "In/Out" | choice | "GR" | the choice string |
| `bypass` | Bypass | bool | | false | |

- Units are inside getText; no parameter labels are used.
- dB knobs use the shared `centredDbRange`: exact inverse, 0.1 dB grid, every
  grid value round-trips through range, parameter and text.
- Time readouts: below one second "N ms" (integer); from one second "N.N s".
  Text input accepts "120", "120 ms", "1.2 s".
- Filter text input accepts "Off", "60", "60 Hz", "0.1 kHz".
- COMPRESS prints a bare number ("6.2"). If the skin's readout shows "6.2 dB",
  the editor adds the unit; the parameter is unitless.

## 11. Provenance summary

| block | tag |
|---|---|
| Feedback side-chain, rectified control voltage, re-biased remote-cutoff pair | SOURCE_CONFIRMED (S1–S3) |
| Static curve in feedback form, solved exactly | APPROXIMATION (F1) |
| No closed-loop dynamics (steady target + ballistics) | APPROXIMATION, deliberate (F1, R3) |
| Full-wave rectifier | SOURCE_CONFIRMED (12AL5 double diode, S2) |
| Two 2.5 ms smoothing poles | APPROXIMATION |
| Remote-cutoff law GR = (K/λ)(e^{λe} − 1) | SCHEMATIC_OR_MEASURED principle (S6, S7); APPROXIMATION constants |
| Ratio rising with GR | SOURCE_CONFIRMED trend (S1–S3) |
| COMPRESS macro 0..10 | TLEET_EXTENSION (COMPRESS 10 = LIMIT, S1–S3) |
| 60 dB GR floor (cutoff) | APPROXIMATION |
| Attack / recovery as exact τ of the GR in dB | APPROXIMATION (F4) |
| Attack / recovery ranges | TLEET_EXTENSION (hardware 25–70 ms; 0.2..8 s) |
| Link = max of the control voltages | SOURCE_CONFIRMED link of DC (S1); APPROXIMATION max |
| Threshold 0 = −18 dBFS; 0 VU = −18 dBFS sine | SOURCE_CONFIRMED 0 VU (S1); APPROXIMATION alignment (S9) |
| SC HPF 12 dB/oct, 20..300 Hz | TLEET_EXTENSION (hardware 6 dB/oct at 100 Hz, S2, S3) |
| Audio HPF | TLEET_EXTENSION (owner-approved 2026-10-13) |
| Coupling 2 Hz | APPROXIMATION (F10) |
| Input stage, gain-element colour, output stage, LF weight | APPROXIMATION (F6), calibrated to S1/S2 THD |
| TUBE DRIVE, HARMONICS, SATURATION, CHARACTER | TLEET_EXTENSION |
| MIX, DRY/WET, METER mode, spectrum tap | TLEET_EXTENSION |
| 2x oversampling | APPROXIMATION (numerical) |

## 12. Verification (tests/VeilTests.cpp, all `VEIL:`)

27 tests. CSVs are written to `<build>/tests/test-output/VEIL_*.csv`: static
curves, ratio growth, attack/recovery, link, GR telemetry error trace, SC HPF
sweep, HPF sweep, THD (character / level / element), THD controls, aliasing.
Key numbers (this container, RelWithDebInfo):

- Registry: IDs, ranges, skews, defaults, texts, choice strings and buses.
- No allocation in processBlock at 44.1 / 48 / 88.2 / 96 / 192 kHz × blocks
  1..2048 × mono / stereo × no key / mono key / stereo key. Output always finite.
- NaN/Inf in the input or the key: output finite, the state recovers (GR
  21 dB on the following clean signal). Silence → silence, 0 dB GR. DC 0.25 →
  no steady GR (8e-6 dB after 6 s; the onset step is a transient), 0 on the
  wet output. Denormal input stays
  clean.
- Latency 49 samples reported = measured by impulse at 44.1..192 kHz. Bypass
  and DRY/WET 0 are bit-exact delayed dry, also with a key and with HPF / SC
  HPF / colour settings.
- Static curves: model vs pure law **5e-10 dB**; processor GR telemetry vs
  law **0.003 dB**, output level **0.003 dB** (5 COMPRESS × 3 thresholds, −38..+6 dBFS).
- Ratio growth: the pure law's local ratio rises monotonically at every
  COMPRESS; the processor's finite-difference ratio matches it within
  **0.3 %** (GR > 1 dB); 2.3 tables.
- Ballistics: 3 numbers.
- Link and telemetry: 5 and 8.1 numbers.
- METER mode: bit-identical audio in GR, VU and In/Out; VU calibration (8.2).
- SC HPF / HPF / sidechain matrix: 4 and 9 numbers. HPF Off bit-transparent;
  40 Hz tone: internal GR 16.2 dB with HPF Off, 0 dB at 300 Hz; external key:
  16.2 dB either way.
- Harmonics: 6.5 tables; aliasing 6.6.
- MIX vs DRY/WET: section 7 numbers.
- Click-free switching (CHARACTER, link, HPF, SC HPF, COMPRESS, DRIVE,
  SATURATION, MIX, DRY/WET, one at a time and all at once every 67 ms, 100 Hz
  tone under compression): sine-predictor residual ≤ 3.4e-5 (one at a time)
  and 2.3e-5 (all), against a −80 dBFS step limit of 1e-4. Rapid random
  automation of every control: finite, max jump 0.008.
- The editor (open, resized, painted, meters and spectrum read) does not alter
  the audio. State round trip restores every parameter and renders
  identically. 12 factory presets load, set every parameter and reject other
  plugins' presets.
- CPU (this container, block 128, stereo, defaults; a shared machine, so the
  spread between runs is shown): **4.0–5.1 %** of one core at 44.1 / 48 kHz,
  **8.2–10.3 %** at 96 kHz, **15–21 %** at 192 kHz (with or without a key). The
  model alone at 48 kHz (2x, no oversampling filters) is 3.7 %; the rest is
  the two FIR oversamplers. The test checks the load computed from the
  median block time (robust on a shared machine): < 12 % up to 96 kHz,
  < 25 % at 192 kHz.
- Memcheck (valgrind) on the sidechain, NaN, latency, METER, MIX, state and
  spectrum tests: no invalid or uninitialised accesses in VEIL (only the test
  harness's own new/delete hooks are reported).

- SATURATION is continuous at 0: 0 % vs 0.1 % differ by at most 2.4e-5 at
  −6 dBFS (Thick).
- Mutation check (each break applied alone, the named test fails, then
  restored): threshold ignored (static curves); GR telemetry frozen
  (telemetry); link without the max (link); METER mode reaching the DSP
  (METER); SC HPF bypassed (SC HPF); HARMONICS fixed (controls); spectrum
  tap fed the dry signal (spectrum); dry path without latency (latency /
  bit-exact); attack using the recovery coefficient (ballistics); MIX after
  the output stage (MIX vs DRY/WET); HPF parameter ignored (HPF); stereo key
  mapped to L only (key mapping); SATURATION continuity removed (controls);
  LF weight stepped per block (switching). 14 of 14 caught.

## 13. Not modelled (intentionally)

- The closed-loop acceleration of the feedback side-chain (2.4), the
  shared-capacitor timing of a tied link (5).
- Valve noise, hum, microphonics, the transformers' HF response and phase,
  power-supply sag.
- The hardware's stepped RECOVERY (VEIL is continuous and stops at 2 s; the
  4 s and 8 s positions are not reachable), the Mastering version's stepped
  attack, the M/S option and the T-Bar 6BA6 voicing as a separate model.
- Linking a LIMIT channel with a COMPRESS channel (one COMPRESS for both).

## 14. Open measurement tasks

1. The static curve of a real unit (COMPRESS and LIMIT, several thresholds),
   to replace the macro's K / λ / w (R2).
2. The attack/recovery network (RC values) and how the hardware times are
   defined; whether the 2.5 ms rectifier poles are right (R3).
3. The closed-loop attack shape at 25 ms on a real unit (R3).
4. A harmonic spectrum of the unit at 0 VU and with INPUT driven, per stage
   (input amplifier, 5670 pair at several GR depths, output stage), to replace
   the colour constants (F6).
5. Re-read Manley's current manual (rev 5.21, the master spec's link): its
   download was blocked from this container (R1).
6. **Owner decision:** keep 2x (extreme-corner aliasing −28 dBc) or move to 4x
   (−49 dBc, about twice the model's CPU), or offer a quality switch.
7. The link's timing interaction on a real tied network.
