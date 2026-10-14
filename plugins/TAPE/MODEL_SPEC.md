# TLEET // TAPE: model specification

TAPE is a multitrack tape machine with Studer A800 DNA, the 12th TLEET plugin. This document
specifies its DSP: what each stage computes, where each number comes from and how it is
verified. Sources R1-R14 are listed in RESEARCH.md. Every constant lives in `dsp/TapeModel.h`
with a provenance tag:

| Tag | Meaning |
|---|---|
| **[HW]** | HARDWARE_DATA: published Studer A800 / tape data |
| **[LIT]** | LITERATURE: textbook, paper or standard |
| **[APX]** | APPROXIMATION: a value chosen inside the literature's range, or fitted to a [HW] target |
| **[EXT]** | TLEET_EXTENSION: a TLEET design decision |

The hard rules come from the brief:

- Spec beats artwork, and no invented functions.
- Every control measurably works.
- The DSP parameter is the single source of truth, and the displays show real state.
- Host bypass is a bit-exact, latency-aligned dry signal, and MIX 0 is a latency-aligned null.
- No allocation on the audio thread, and no clicks.
- All verification is automated (tests/TapeTests.cpp, "TAPE: ..." cases, CSVs in test-output/).

---

## 1. Files

| File | Role |
|---|---|
| `Parameters.h` | Parameter registry: IDs, ranges, defaults, text |
| `PluginProcessor.{h,cpp}` | Buses, dry path, MIX, OUTPUT, host bypass, telemetry |
| `dsp/TapeModel.h` | Every model constant, with provenance |
| `dsp/TapeEngine.{h,cpp}` | The wet path (the machine), control smoothing, response evaluator |
| `dsp/Hysteresis.h` | Jiles-Atherton solver and its gain tables |
| `dsp/Filters.h` | Emphasis, TPT SVF, one-pole, each with an exact response function |
| `dsp/HeadLoss.h` | Wavelength losses as a linear-phase FIR |
| `dsp/Transport.h` | Wow and flutter generator, RNG, fractional delay line |
| `dsp/Oversampler.h` | Half-band oversampler to the 176.4 / 192 kHz top rate |
| `presets/*.json` | 14 factory presets |

## 2. Signal flow

```
plugin in ─► sanitise (non-finite → 0) ─┬─► TapeEngine ───────────────────────────────► repro ─► [REPRO VU]
                                        │     host rate: INPUT ─► [RECORD VU] ─► record pre-emphasis E(f; speed)
                                        │     top rate:  record amp limit ─► × drive (CAL, SATURATION, type)
                                        │                + head DC field (AGE) ─► Jiles-Atherton (bias → c)
                                        │                ─► × make-up ─► transport delay (wow, flutter, AGE, azimuth)
                                        │     host rate: de-emphasis 1/E ─► + hiss, + modulation noise
                                        │                ─► wavelength-loss FIR (gap, AGE spacing, bias zone)
                                        │                ─► head roll-off, dip, bump ─► LF / HF trims
                                        │                ─► wear variation (AGE) ─► L/R mismatch (AGE) ─► crosstalk
                                        │                ─► × AUTO GAIN (delayed by the latency) ─► wet
                                        └─► dry, delayed by the latency ─────────────────────────────────► MIX (linear)
                                                                                ─► × OUTPUT ─► host bypass ─► out
```

Rates:

- The magnetic stage and the transport always run at the **top rate**: 4× at 44.1 / 48 kHz, 2× at 88.2 / 96 kHz, 1× at 176.4 / 192 kHz.
- Everything else runs at the host rate.

Controls and timing:

- Controls update every **16 host samples**, aligned to a global sample counter so the output does not depend on the block size.
- Gains and the transport delay are interpolated linearly per sample between control points.
- The filters glide with their smoothed parameters.

## 3. Record and repro electronics

### 3.1 Levels, INPUT, OUTPUT

- **0 VU** is a sine at −18 dBFS, the suite convention. [EXT]
  - The RECORD VU and REPRO VU are aligned to it at every CALIBRATION, as on the A800: the meters show the operating level.
- **INPUT** (−12…+24 dB, 0.1 dB steps) is the record line-amp gain.
  - It moves the level *and* the drive. AUTO GAIN (§8) can take the level change back out.
- **OUTPUT** (±24 dB, 0 dB at the knob centre, 0.1 dB steps) is applied after MIX.
  - It uses a dB ramp of 30 ms plus a 3 ms corner one-pole, so a full-scale tone shows no click.

### 3.2 Record amplifier

- The record amplifier soft-limits at 20 dB above 0 VU: `y = x / (1 + |x/Vc|^8)^(1/8)`, Vc = +20 VU.
  - This matches the A800 line: +24 dBm maximum for a +4 dBm operating level (R1). [HW level, APX shape]
- It is clean (< 0.05 % H3) 5 dB below its clip point.
  - It only matters when INPUT pushes the record level past +15 VU, for example type C at CAL −3. There the electronics reach their limit before the tape does (§14: type C, CAL −3, MOL +17.5 VU instead of +18).

### 3.3 Record pre-emphasis and repro de-emphasis

- The record equaliser is E(s) = (1 + sτ) / (1 + sτ/4), with τ = 35 µs · 15 / ips:
  - 70 µs at 7.5 ips and 35 µs at 15 ips (IEC/CCIR, the A800 default), and 17.5 µs at 30 ips (AES) (R1, R6). [HW τ, APX ceiling of +12 dB]
- The repro de-emphasis after the tape is the **exact digital inverse**: both are plain bilinear transforms of the analogue pair.
  - Small-signal, the pair is perfectly flat.
  - Large-signal, the treble enters the magnetic layer up to 12 dB hotter than the mids, so the treble saturates first and earlier at low speed.
  - This is the speed-dependent HF headroom of a real machine. §14 measures it: the 10 kHz 1 dB compression point is +4 / +7 / +10.5 VU at 7.5 / 15 / 30 ips, while 1 kHz stays at +14 VU.

### 3.4 Head bump, dip and roll-off

The head bump is the contour effect of the repro head (R3): a resonance at a fixed
*wavelength*, so its frequency follows the speed.

| Stage | Value | Tag |
|---|---|---|
| bump | bell at f_b = v / 7 mm, Q 1 → 27 / 54 / 109 Hz at 7.5 / 15 / 30 ips | [LIT mechanism, APX λ] |
| height | HEAD BUMP × 5 dB × (ips/15)^0.32 → at 100 %: 4.0 / 5.0 / 6.2 dB peak gain | [APX] |
| dip | bell at 0.55 f_b, Q 1.2, −0.5 × the bump gain | [APX] |
| roll-off | 2nd-order high-pass at 0.27 f_b, Q 0.7: the head's long-wavelength loss | [APX] |

Measured at the default HEAD BUMP of 36 % (re 1 kHz), the bump is +1.21 / +1.51 / +1.87 dB at
29 / 58 / 113 Hz. At HEAD BUMP 0 the response is flat within 0.3 dB from 100 Hz to 5 kHz.

### 3.5 Wavelength losses

The wavelength losses are a linear-phase FIR designed by frequency sampling, as in R8 eq. 13.
The losses are real functions of k = 2πf/v (R3):

| Loss | Law | Value | Tag |
|---|---|---|---|
| repro gap | sin(kg/2)/(kg/2) | g = 4 µm | [LIT law, APX g] |
| AGE spacing | exp(−k d), 54.6 d/λ dB (R11) | d = 2 µm × AGE (right head +25 %) | [LIT law, APX d] |
| bias zone | exp(−k d_rec) | d_rec = 6.3 µm × (smax(I, 0.7) − smax(1, 0.7)); I = bias current / optimum | [HW data R2, APX law] |

FIR design and updates:

- Taps: N = 33 / 65 / 129 at 44.1-48 / 88.2-96 / 176.4-192 kHz. The delay (N − 1)/2 is part of the reported latency.
- Design: 4(N − 1) frequency bins, Hann window, unity at DC.
- Redesign: only when speed, AGE or BIAS changes, at most every 64 samples (at 48 kHz), with a crossfade of the same length. The designer does not allocate.
- The aligned machine's own spacing and thickness losses are equalised by its alignment, so only *changes* from the aligned state appear.

The bias-zone law reproduces the published overbias of a mastering tape (R2):

| | 30 ips | 15 ips | 7.5 ips |
|---|---|---|---|
| ATR, 0.25-0.3 mil gap (R2) | 1.2-1.5 dB | 2.5-3.0 dB | 4.5-5.0 dB |
| Model, at 10 kHz for +30 % bias current | 1.25 dB | 2.5 dB | 5.0 dB |

Under-bias gives negative d_rec down to I = 0.7 (the 10 kHz peak), which brightens the treble.

### 3.6 LF and HF repro trims

These are the A800's repro LF / HF adjustments. Each step is 0.06 dB, so ±100 = ±6 dB. [EXT range]

- **LF:** low shelf at 100 Hz.
- **HF:** high shelf at the EQ turnover 1/(2πτ) = 2.27 / 4.55 / 9.09 kHz.

Measured at ±100: +5.97 / −5.97 dB at 25 Hz (LF) and +5.99 / −5.99 dB at 16 kHz (HF). Each
shelf moves less than 0.1 dB outside its own band.

## 4. Magnetic layer

### 4.1 Jiles-Atherton hysteresis

The magnetisation follows the Jiles-Atherton equations (R7) in Chowdhury's tape form (R8),
normalised to m = M/Ms and h = H/a:

```
Q    = h + α' m                       effective field
L(Q) = coth Q − 1/Q                   anhysteretic (Langevin)
d    = L(Q) − m
dm/dh = [ (1−c) δ_M d / ((1−c) δ k' − α' d) + c L'(Q) ] / (1 − c α' L'(Q))
```

- δ = sign(dh/dt). δ_M = 1 when the irreversible part moves towards the anhysteretic curve.
- The α' feedback of the reversible term is solved exactly. That is why c → 1 stays on the anhysteretic curve, which the tests verify.

The solver:

- Classical RK4 on every sample of the top rate, with h linear between samples.
- The irreversible denominator is clamped to ≥ ¼ (1−c) k'. Non-finite states reset to 0 and m is clamped to ±1.
- Accuracy against a 16× finer RK4 reference is −124…−127 dB.
- RK2 reaches only −71 dB at hot fields, so RK4 is kept. (§14)
- The Langevin function uses a series below |Q| = 0.2 and one `exp` above.

### 4.2 Bias: anhysteretic recording instead of a 240 kHz signal

The A800 records with a 240 kHz bias (R1). Simulating it as a signal (R8's approach) needs a
solver rate of ≥ 1 MHz, about 6× the top rate. That is far outside the CPU budget, and the
result would depend on the oversampling. TAPE uses instead the physical result that AC bias
produces (R4): the recorded magnetisation follows the **anhysteretic** curve, and the more
completely the higher the bias. In J-A terms, the bias current I sets the reversibility:

```
c(I) = cMax − (cMax − cMin) e^(−κ I),  cMin 0.15, cMax 0.95, κ such that c(1) = cOpt(type)
```

[LIT mechanism, APX law]

Combined with the bias-zone loss (§3.5), BIAS couples linearity, distortion, sensitivity and
HF the way a real machine does. The bias sweep at 15 ips (§14) shows:

| BIAS | Sensitivity at 1 kHz | THD at +6 VU | 10 kHz |
|---|---|---|---|
| −60 % (under-bias) | −3.1 dB | 3.4 % | brighter |
| 0 (optimum) | 0 dB | 1.6 % | reference |
| +60 % (over-bias) | ≈ 0 dB | 1.3 % | darker |

Neither curve is a filter knob: the treble change comes from the recording zone, the
distortion from the hysteresis.

### 4.3 Bias sensitivity and level normalisation

- `HysteresisTables` measures the solver's fundamental gain once per process (about 0.15 s, first prepareToPlay).
  - Grid: 3 types × 14 values of c × 41 amplitudes.
  - Method: a 1 kHz sine, 192 steps per cycle, after 2 warm-up cycles.
- The make-up after the tape divides by the small-signal gain at max(c, cOpt).
  - Over-bias therefore keeps the level.
  - Under-bias loses sensitivity, because the irreversible layer responds less. That is the A800 alignment practice of biasing past the sensitivity peak.

### 4.4 Tape types

The types are TLEET profiles, as the spec asks; no trademarks.

| Type | k' | α' | cOpt | MOL (H3 = 3 %, 1 kHz, 15 ips) | hiss re B | hScale | Character |
|---|---|---|---|---|---|---|---|
| **A** CLASSIC | 0.90 | 0.0200 | 0.82 | **+9 dB** re 185 nWb/m | +2 dB | 0.52791 | earlier, softer saturation |
| **B** MODERN | 1.227 | 0.02545 | 0.88 | **+12 dB** | 0 dB | 0.39546 | Chowdhury's k, α (R8) |
| **C** HOT | 1.50 | 0.0300 | 0.91 | **+15 dB** | −1.5 dB | 0.28170 | later saturation |

The MOLs sit inside the R9 ranges. [LIT ranges, APX values]

hScale is the field per unit of flux:

- It was found by bisection with the solver at 192 kHz, then trimmed so that the MOL lands at the target *measured through the whole machine*.
- The repro de-emphasis lowers H3 by 1.3 dB at 15 ips, which is why the bare-solver value had to be trimmed.
- Measured MOLs at CAL 0: +9.00 / +11.99 / +14.95 VU (§14).

Type changes blend every type-dependent quantity over 50 ms; a change made during a blend waits for it to finish.

### 4.5 CALIBRATION and SATURATION

**CALIBRATION** −3 / 0 / +3 / +6 sets how much flux 0 VU records: 131 / 185 / 261 / 370 nWb/m (R5).

- The drive into the tape rises by the CAL dB, and the repro gain falls by the same dB: the operating level stays at 0 VU.
- Headroom above 0 VU falls 1:1 and distortion at a given VU rises. Measured MOL = type MOL − CAL, within 0.05 dB for 11 of 12 combinations. Type C at CAL −3 lands at +17.5 VU because the record amplifier limits first (§3.2).
- Hiss relative to 0 VU falls by the CAL dB. More flux means more signal over the same tape noise, which is why studios aligned hot.

**SATURATION** 0…100 % sets the drive into the magnetic layer: 0.25 dB per %, 0 dB at 42 % (the default). [EXT]

- The make-up is exactly inverse, so the operating level holds within 0.25 dB at −30 VU.
- Distortion moves strongly: THD at 0 VU is 0.23 % at 10 % and 5.2 % at 90 %.
- Hiss is unchanged: it is a drive control, not a re-alignment.

### 4.6 Head magnetisation (AGE)

AGE adds a DC field of 0.06 a × AGE at the record head. A magnetised head is the textbook
source of even harmonics and extra noise (R4). [LIT mechanism, APX value]

- H2 at +6 VU: −52 dBc at AGE 50 %, −46 dBc at 100 %.
- At AGE 0 the model is exactly odd-symmetric: an inverted input gives an inverted output, and there are no even harmonics.

## 5. Oversampling and latency

**The rate rule is fixed** (owner decision D2). The top rate is always 176.4 / 192 kHz:

| Host rate | Oversampling |
|---|---|
| 44.1 / 48 kHz | 4× |
| 88.2 / 96 kHz | 2× |
| 176.4 / 192 kHz | 1× |

There is **no quality switch**, so the sound and the CPU cost do not depend on a hidden setting.

The oversampler (`dsp/Oversampler.h`):

- Linear-phase Kaiser half-band FIRs (R14), the same design as STEEL's.
- Stage 1: 95 taps, 85 dB. Stage 2: 27 taps, 90 dB.
- An integer pad at the top rate makes the round trip a whole number of host samples: 54 / 47 / 0.

Aliasing of a 6.1 kHz tone at +9 VU, SATURATION 70 %, 48 kHz host:

| Processing | Alias energy |
|---|---|
| 1× | −22.8 dBc |
| 2× | −50.6 dBc |
| 4× (the rule) | **−81.7 dBc** |

Reported latency = oversampler + transport centre (2.5 ms) + loss-FIR centre:

| Host rate | Latency |
|---|---|
| 44.1 kHz | 54 + 110 + 16 = 180 samples |
| 48 kHz | 54 + 120 + 16 = **190 samples (3.96 ms)** |
| 88.2 kHz | 47 + 221 + 32 = 300 samples |
| 96 kHz | 47 + 240 + 32 = 319 samples |
| 176.4 kHz | 0 + 441 + 64 = 505 samples |
| 192 kHz | 0 + 480 + 64 = 544 samples |

At every rate, mono and stereo, the reported latency equals the measured impulse delay to the sample.

## 6. Transport: wow and flutter

### 6.1 Rotating parts

The rates follow the tape speed: one revolution of each part. [LIT mechanism, APX sizes]

| Part | Size | Rate at 7.5 / 15 / 30 ips |
|---|---|---|
| supply reel | r = 110 mm (10.5" reel, mid pack) | 0.28 / 0.55 / 1.10 Hz |
| take-up reel | r = 75 mm | 0.40 / 0.81 / 1.62 Hz |
| capstan | D = 28 mm | 2.17 / 4.33 / 8.66 Hz |
| guide roller | D = 15 mm | 4.04 / 8.08 / 16.2 Hz |

### 6.2 Depths

Peak speed deviation at 100 %. [EXT ranges]

**WOW:**

- supply reel 0.25 %
- take-up reel 0.10 %
- random drift 0.05 % rms (band noise around 1.2 Hz)

**FLUTTER:**

- capstan 0.30 %
- roller 0.10 %
- random 0.06 % rms (around 12 Hz)
- × 1.5 at 7.5 ips (A800: 0.06 % vs 0.04 %, R1)

**AGE:**

- wow +0.04 % rms (around 0.9 Hz)
- flutter +0.08 % rms (around 9 Hz)
- capstan +0.04 % peak

The defaults (WOW 6 %, FLUTTER 4 %, AGE 18 %) measure **0.044 / 0.033 / 0.033 %** weighted
(2σ) at 7.5 / 15 / 30 ips, inside the A800 specification of 0.06 / 0.04 / 0.04 % (R1).
WOW = FLUTTER = AGE = 0 gives exactly no deviation.

### 6.3 Time warp

A periodic speed deviation A sin(2πft) plays back as a delay deviation −A/(2πf) · cos(2πft).
Random parts are band-limited speed noise integrated with a 0.25 Hz leak. The total is
soft-limited (tanh) to ±2.3 ms around a 2.5 ms centre. The limit is never reached by the
defaults; it is touched only by WOW > 70 % at 7.5 ips.

- The deviation is computed at the control rate, interpolated linearly per top-rate sample, and read with a 4-point Lagrange interpolator at the top rate. An integer delay reads exactly.
- One transport serves both channels, so L and R share the same deviation.
- The random generator is a deterministic xorshift64* with a fixed seed, so renders are repeatable.

### 6.4 Azimuth (AGE)

AGE moves the right channel by +20 µs relative to the left at 100 % (±10 µs). [EXT]
Measured: 19.7 µs.

## 7. Noise (NOISE on)

- **Hiss level:** set by the A800's S/N, 66 dB at 7.5 and 15 ips and 70 dB at 30 ips, unweighted (R1), re 510 nWb/m (R10).
  - Re 185 nWb/m that is 57.2 / 57.2 / 61.2 dB below 0 VU at CAL 0.
  - Type offsets: A −2 dB, C +1.5 dB.
  - CAL lowers the hiss re 0 VU by its dB.
- **Hiss spectrum:**
  - The repro EQ lifts the tape's noise above the turnover: shelf +4 dB at 1/(2πτ). [APX]
  - Low-passed at 20 kHz, then normalised to its RMS in 20 Hz-20 kHz.
  - It then passes through the head losses and the repro EQ like the signal.
- **NOISE AMOUNT:** 12 % is the real machine; the hiss amplitude goes with (amount/12)^1.5, +9 dB per doubling. 100 % is −27.8 dB re 0 VU. 0 % is silence. [EXT]
- **AGE 100 %:** +6 dB.
- **Modulation noise:** −68 dB re the signal (R2): the signal times band noise around 400 Hz.
- **NOISE off:** both sources are exactly zero. A silent input gives exact zeros after the tails.

Measured (type B, CAL 0, unweighted):

| Speed | Noise re 0 VU | S/N re 510 nWb/m | A-weighted re 0 VU |
|---|---|---|---|
| 7.5 ips | −58.0 dB | 66.8 dB | −59.8 dB |
| 15 ips | −57.4 dB | 66.2 dB | −59.8 dB |
| 30 ips | −61.2 dB | 70.0 dB | −63.8 dB |

All 27 tested type × speed × CAL combinations are within 1 dB of target.

## 8. AUTO GAIN

Definition: **the gain that brings a 0 VU, 1 kHz sine back to its input level** at the present
INPUT, CAL, SATURATION, type, speed and bias.

```
AG = −INPUT − 20 log10( g_amp · G(type, c, h_ref) / g_ss(type, max(c, cOpt)) )
```

| Term | Meaning |
|---|---|
| g_amp | record amplifier fundamental gain at the reference (§3.2), tabulated |
| G | solver fundamental gain at the reference field h_ref, from the J-A tables |
| g_ss | small-signal gain at max(c, cOpt) (§4.3) |

How it is applied:

- It is a pure gain on the wet signal, before MIX. Its on/off weight is ramped over 30 ms.
- It is computed from the smoothed controls and **delayed by the plugin latency**, so the gain matches the audio it belongs to. An INPUT move under a steady tone leaves the output within 0.03 dB.

Result:

- 0 VU in comes out at 0 VU across 256 settings (INPUT −12…+24, every CAL, SATURATION 0/42/100, every type, speed, BIAS ±50).
- The error is within ±0.3 dB, and at most 0.32 dB where INPUT drives the record amplifier into its limit.
- Off, the machine's level follows INPUT.

## 9. Telemetry (exactly as implemented, `PluginProcessor.h`)

All telemetry is lock-free; the editor polls it. Channel 0 = L, 1 = R; a mono bus feeds both.

**RECORD and REPRO VU**

```cpp
dsp::MeterSource recordVu, reproVu;                                   // needle = power mean of L and R
std::array<dsp::MeterSource, 2> recordVuChannel, reproVuChannel;      // per channel
float takeRecordVuDb() noexcept;      float takeReproVuDb() noexcept;
float takeRecordVuDb (int ch) noexcept; float takeReproVuDb (int ch) noexcept;
static float vuDbFromRms (float rms) noexcept;   // dB re 0 VU, floor −80, NaN if no audio since the last call
```

- **RECORD VU** is read after INPUT and before the record pre-emphasis. It is independent of CAL.
  - A 0 VU sine reads 0.00 dB, and INPUT +6 reads +6.
- **REPRO VU** is read at the repro amplifier output, after the tape, losses, EQ, AGE, noise and crosstalk, and before AUTO GAIN, MIX and OUTPUT.
  - It equals the measured plugin output minus getAutoGainDb() within 0.05 dB.
- The MeterSources hold squares normalised to 0 VU (takeRms() = 1 is 0 VU), and peaks in VU units.
- Ballistics run in the UI (dsp::VUBallistics).

**SATURATION bar**

```cpp
float getSaturationActivity() noexcept;   // read-and-reset, 0..1
```

- It reads the deepest instantaneous knee of the magnetic layer since the last call, both channels:
  - knee = 1 − L(|Q|)/(|Q|/3), the shortfall of the magnetisation against a linear medium at the effective field Q.
  - activity = clamp(1 + 20 log10(knee)/36, 0, 1).
- So 1.5 dB of knee depth = one segment of the **24-segment** bar; lit = round(24 × activity).
- Type A, CAL 0, SATURATION 42 %, 1 kHz sine:

  | Level | Segments lit |
  |---|---|
  | up to −1 VU | 0 |
  | 0 VU | 1 |
  | +6 VU | 8 |
  | +9 VU (MOL) | about 12 |
  | +12 VU | 15 |
  | +18 VU | 19 |
  | INPUT +24, SATURATION 100 % | more than 20 |

**TAPE CONDITION display**

```cpp
dsp::SpectrumTap outputSpectrum;          // (L + R)/2 of the final output; inactive until the editor activates it
double tapeResponseDb (double hz, int channel = -1) const;
void   tapeResponseDb (const double* hz, double* outDb, int count, int channel = -1) const;
```

- **outputSpectrum** carries the final output.
- **tapeResponseDb** is the machine's small-signal magnitude as it runs. It covers everything from the plugin input to the REPRO point except INPUT:
  - wavelength losses (gap, AGE spacing, bias zone), bump / dip / roll-off, LF / HF, bias sensitivity, AGE L/R mismatch.
  - It is computed from the same coefficients as the audio, using its own FIR designer under a mutex, so any thread may call it.
  - channel −1 = power mean of L and R.
- Measured sweeps match it within 0.06 dB (7 configurations, 25 Hz-18 kHz).

**Transport and gain state**

```cpp
bool   isTransportRunning() const noexcept;  // host playhead if it reports one; else input > −60 dBFS within 0.5 s
double getTapeSpeedIps() const noexcept;     // speed in use; glides (log) over 100 ms after a change
float  getAutoGainDb() const noexcept;       // AUTO GAIN in force, dB (0 when off)
```

Test hooks: `getEngineForTesting()`, `setOversamplingOverrideForTesting (log2)` (−1 = the rule).

## 10. Parameters

Defaults are the registry's. getText examples are the strings `getText` returns, which the
skin prints verbatim.

| ID | Name | Range / choices | Default | Law | getText examples |
|---|---|---|---|---|---|
| `input` | Input | −12…+24 dB, 0.1 dB | 0 | record gain, drive (§3.1) | "+6.4 dB", "0.0 dB" |
| `output` | Output | ±24 dB, 0 at centre, 0.1 dB | 0 | after MIX | "-4.5 dB", "0.0 dB" |
| `tapeType` | Tape Type | A / B / C | A | §4.4 | "A", "C" |
| `speed` | Tape Speed | 7.5 / 15 / 30 | 15 | §3.3-3.5, §6 | "15 IPS", "7.5 IPS" |
| `calibration` | Calibration | −3 / 0 / +3 / +6 | 0 | §4.5 | "+3 dB", "0 dB", "-3 dB" |
| `bias` | Bias | −100…+100 % of optimum current | 0 | I = 1 + x/100 (§4.2) | "+25 %", "-40 %", "0 %" |
| `saturation` | Saturation | 0…100 % | 42 | 0.25 dB/% re 42 % | "42 %" |
| `headBump` | Head Bump | 0…100 % | 36 | §3.4 | "36 %" |
| `lf` | LF | −100…+100 | 0 | 0.06 dB/step, 100 Hz shelf | "-12", "0", "+18" |
| `hf` | HF | −100…+100 | 0 | 0.06 dB/step, turnover shelf | "+18" |
| `age` | Age | 0…100 % | 18 | §11 | "18 %" |
| `wow` | Wow | 0…100 % | 6 | §6.2 | "6 %" |
| `flutter` | Flutter | 0…100 % | 4 | §6.2 | "4 %" |
| `noise` | Noise | Off / On | Off | §7 | "Off", "On" |
| `noiseAmount` | Noise Amount | 0…100 % | 12 | (x/12)^1.5 | "12 %" |
| `crosstalk` | Crosstalk | 0…100 % | 8 | §12 | "8 %" |
| `mix` | Mix | 0…100 % | 100 | linear, latency-aligned dry | "100 %" |
| `autoGain` | Auto Gain | Off / On | On | §8 | "On" |
| `bypass` | Bypass | host bypass | Off | bit-exact delayed dry, 20 ms crossfade | — |

- Text entry accepts "30 IPS", "7.5", "+6 dB", "-3", "b" and plain numbers.
- Parameter version 1. The IDs are permanent.

## 11. AGE macro (0 = a factory-new, serviced machine)

| Effect | At 100 % | Measured |
|---|---|---|
| HF loss (wear and debris spacing) | L +2 µm, R +2.5 µm | 10 kHz re 1 kHz at 15 ips: L −2.47 dB, R −3.11 dB (law: −2.58 / −3.22) |
| L/R level mismatch | L +0.25 dB, R −0.25 dB, plus R's extra spacing | 0.53 dB at 1 kHz |
| L/R timing (azimuth) | R +20 µs | 19.7 µs |
| wear variation (contact) | HF band (> 3 kHz) 0.12 rms, whole signal 0.015 rms, 0.5-8 Hz | 10 kHz envelope 8.8 % rms, 300 Hz 1.25 % |
| wow and flutter | §6.2 | 0.12 % weighted with WOW = FLUTTER = 0 |
| noise | +6 dB | +6.1 dB (0.5-2 kHz) |
| head magnetisation | DC field 0.06 a | H2 −46 dBc at +6 VU |

The response evaluator shows the HF loss and the mismatch per channel.

## 12. Crosstalk

Crosstalk is fringing between adjacent tracks, a long-wavelength effect.

- The bleed is a first-order low-pass whose corner follows the speed: 125 / 250 / 500 Hz. Its plateau is the same at every speed.
- Level: 0.1 × (x/100)^1.5 at 1 kHz, 15 ips.
  - 8 % → −52.9 dB at 1 kHz and a −40.6 dB plateau.
  - 100 % → −20 dB at 1 kHz.
- It is symmetric between L and R, exactly zero at 0, and absent on a mono bus. [HW bound, APX shape]
- At the default 8 %, the worst crosstalk between 100 Hz and 12 kHz is −42.8 / −41.2 / −40.8 dB at 7.5 / 15 / 30 ips. That meets the A800's ≥ 40 dB (R1) at every speed.

## 13. Smoothing, switching, dry paths, robustness

**Smoothing**

- Linear ramps:
  - INPUT, SATURATION, BIAS, HEAD BUMP, LF, HF, NOISE AMOUNT, CROSSTALK, AUTO GAIN on/off: 30 ms.
  - CAL, AGE, WOW, FLUTTER: 50 ms.
  - NOISE on/off: 20 ms.
  - Speed: a 100 ms glide in log-speed.
  - Type: a 50 ms blend.
- Filters glide with their parameters. The loss FIR crossfades after each redesign.
- **Every control, both directions, was switched under a tone with no click.** Energy above 8 kHz stayed within 2× the steady state of either setting, or below −80 dBFS (36 transitions, TAPE_switching.csv).
- Random automation of every parameter every 64 samples stays finite and bounded, with zero allocations.

**Dry paths**

- **MIX** is linear wet/dry with the dry delayed by the latency. At MIX 0 the output is the latency-aligned input, bit-exact.
- **Host bypass** crossfades over 20 ms to the latency-aligned dry, and is bit-exact once it arrives. A session that starts bypassed is bit-exact from the first sample.
- Both crossfades are click-free.

**Robustness**

- NaN and ±Inf input samples are taken as silence; nothing else changes (bit-exact to the same input with zeros).
- Finite huge inputs are bounded by the record amplifier and the tape.
- Denormals are flushed (ScopedNoDenormals); a decayed tail is exact zeros.
- **No allocation in processBlock** at any rate (44.1-192 kHz), block size (1-2048), or channel count (mono / stereo).
- Host blocks larger than announced are processed in chunks.
- The output is **bit-identical across block sizes**.
- `reset()` clears the tape path and the dry delay.

## 14. Verification (tests/TapeTests.cpp; numbers from the last run)

**CPU** (stereo, block 128, NOISE on, AGE 60, best-of-3 median; this machine is shared and noisy)

| Host rate | Oversampling | CPU |
|---|---|---|
| 44.1 kHz | 4× | 5.3-5.6 % of one core |
| 48 kHz | 4× | 5.3-6.6 % |
| 96 kHz | 2× | 6.4-7.1 % |
| 192 kHz | 1× | 7.8-8.1 % |

- The test fails above 10 % at ≤ 48 kHz and above 20 % at higher rates.
- Under four parallel compile jobs, one run measured 10.3 % at 48 kHz.
- The hysteresis (4 `exp` per RK4 step per channel at 192 kHz) is about half of the cost.

**Response windows** (§3.4)

| Speed | Worst deviation in the A800 ±2 dB window | Status |
|---|---|---|
| 7.5 ips | 1.31 dB | met |
| 15 ips | 1.50 dB | met |
| 30 ips | 1.86 dB | met |

The A800 ±1 dB window (60 Hz-12/18/20 kHz) is met at 7.5 ips (0.80 dB). At 15 / 30 ips the
default HEAD BUMP of 36 % peaks at +1.5 / +1.9 dB; see D8. At HEAD BUMP 18 % the window is met
at every speed (worst 0.80 / 0.74 / 0.92 dB), and the test checks it.

**J-A physics**

- At c = 0.99999 the solver stays within 9.3·10⁻⁶ (relative) of the anhysteretic curve m = L(h + α'm).
- Type B loops at h = ±3:

  | c | Remanence | Coercive field | Loop area |
  |---|---|---|---|
  | 0.6 | 0.105 | 0.376 | 0.75 |
  | 0.88 (optimum bias) | 0.029 | 0.104 | 0.23 |

- The steady loop is odd-symmetric to 10⁻⁶.

**Distortion**

- THD at 0 VU, CAL 0: A 0.56 %, B 0.20 %, C 0.09 %. Nearly all of it is H3, so the A800's "H3 ≤ 1 % at 1 kHz" (R1) is met by every type at its nominal alignment.
- THD+N equals THD with NOISE off.

**Other measured numbers**

- Bias, headroom, aliasing, solver, noise, wow and flutter, crosstalk, AGE, AUTO GAIN and telemetry: as quoted in §3-§12.
- Presets: 14. They cover every type, speed and CAL, and render within ±6 dB of the input level.

**Outputs** written to `test-output/`:

- `TAPE_*.csv`: latency, frequency response, trims, bias sweep, THD vs level, MOL, harmonics, aliasing, solver accuracy, HF headroom, W&F lines and spectrum, noise, crosstalk, AUTO GAIN, SAT activity, switching, rates / blocks, presets, response evaluator, CPU.
- `TAPE_impulse_responses_7.5_15_30ips.wav`
- `report.json`, with every MEASURE value.

### 14.1 Wow and flutter measurement

1. A 3150 Hz carrier is quadrature-demodulated: a 4th-order 250 Hz low-pass, then the phase derivative decimated to 1 kHz.
2. The deviation is weighted by a filter fitted exactly to the AES6 / IEC 60386 weighting points (R13): 0.0296 at 0.2 Hz, 0.5 at 0.8 Hz, 1 at 4 Hz (the peak) and 0.508 at 20 Hz.
   - The filter: a 2nd-order high-pass at 0.4345 Hz, Q 0.751; a 1st-order high-pass at 1.666 Hz; a 1st-order low-pass at 9.673 Hz.
3. The peak value is the 2σ value: the level the weighted deviation exceeds 5 % of the time.
4. The lines are also checked individually against their set depths:

   | Line | Set | Measured |
   |---|---|---|
   | capstan | 0.30 % × FLUTTER (× 1.5 at 7.5 ips) | within 10 % |
   | roller | 0.10 % | within 15 % |
   | supply reel | WOW 50 % → 0.125 % | 0.122-0.129 % |

## 15. Mutation checks

The checks were scripted: each defect was put into the source, the build was rerun with the
matching test, and the source was restored. All 18 defects are **CAUGHT**.

| # | Defect introduced | Caught by | Failing check |
|---|---|---|---|
| 1 | repro de-emphasis removed | frequency response | HEAD BUMP 0 not flat; ±2 dB window |
| 2 | head bump at 14 mm instead of 7 mm | frequency response | bump at 29 Hz instead of 54 Hz |
| 3 | bias-zone sign flipped | BIAS sweep | overbias −4.9 dB instead of +5; treble order |
| 4 | α' feedback dropped from the reversible term | J-A physics | anhysteretic-limit distance > 1e-4 |
| 5 | CAL not applied to the drive | THD vs level | MOL stays at +9 VU for every CAL |
| 6 | head magnetisation removed | harmonic spectrum | no H2 at AGE 50 / 100 % |
| 7 | dry path one sample late | MIX 0 / bypass | not bit-exact delayed dry |
| 8 | AUTO GAIN reference ignores CAL | AUTO GAIN | 0 VU in → out off by > 0.3 dB |
| 9 | AUTO GAIN not delayed by the latency | AUTO GAIN | output level moves under an INPUT ramp |
| 10 | INPUT gain stepped per control block | switching | click on INPUT 0 → +18 |
| 11 | azimuth removed | AGE | R−L delay 0 instead of 20 µs |
| 12 | hiss ignores CAL | NOISE | noise off target by the CAL dB |
| 13 | crosstalk plateau follows speed | CROSSTALK | 7.5 ips worse than 40 dB |
| 14 | wow and flutter rates fixed at 15 ips | wow and flutter | capstan and roller lines missing at 7.5 / 30 ips |
| 15 | RECORD VU off by 0.1 dB | VU telemetry | 0 VU reads +0.10 dB |
| 16 | latency under-reported by one sample | latency | measured 180 vs reported 179 |
| 17 | response evaluator forgets the bias sensitivity | response evaluator | 0.25 dB off at BIAS −50 |
| 18 | SAT telemetry halved | SAT telemetry | 0 VU / MOL segment targets |

Three defects were first written so that they changed nothing observable, and all three were missed:

- **#4:** the self-consistent gain tables absorbed it.
- **#7:** it first touched only the MIX crossfade formula, which is exact at m = 0 anyway.
- **#8:** it first touched only the blend-from type, which has weight 0 in steady state.

They were redefined as real defects and are caught. #4 needed a new test, "J-A physics": the anhysteretic limit plus the loop shape.

## 16. Owner decisions

These were made as a tape modeller would; each can be revisited.

- **D1. Bias as a reversibility, not a 240 kHz signal (§4.2).** A real bias signal needs a ≥ 1 MHz solver: about 6× the CPU, and a sound that depends on the oversampling. The anhysteretic-recording result (R4), plus the recording-zone loss (R2, R3), gives the same couplings: linearity, distortion, sensitivity and HF. The bias-induced noise floor is folded into NOISE.
- **D2. Fixed top-rate rule, no quality switch (§5).** 176.4 / 192 kHz at every host rate. At 48 kHz this is 4×, with aliases at −82 dBc for a hot 6 kHz tone; 2× would give −51 dBc. CPU is 6-7 % of one core at 48 kHz.
- **D3. CCIR/IEC equalisation (70 / 35 / 17.5 µs), the A800 default (R1).** NAB (50/3180 µs) is not offered: the spec has no EQ selector, and inventing one would add a function.
- **D4. Latency 3.96 ms at 48 kHz.** The transport needs a centre delay (2.5 ms) so that wow can both advance and retard the signal. With the oversampler and the loss FIR, the reported and compensated total is 190 samples.
- **D5. 0 VU = −18 dBFS sine.** This is the suite convention. CAL 0 records 185 nWb/m.
- **D6. Tape types are TLEET profiles** with MOL +9 / +12 / +15 dB re 185 nWb/m. Type B uses Chowdhury's published k and α. The types carry no formulation names.
- **D7. S/N reference 510 nWb/m (R10).** The A800 data gives 66 / 70 dB unweighted without a reference. If the reference were 320 nWb/m, hiss would be 4 dB lower, the same as NOISE AMOUNT 8.5 % instead of 12 %. AMOUNT 12 % = the real machine.
- **D8. HEAD BUMP default 36 % (the registry's).** This gives the familiar +1.5 dB bump at 15 ips and +1.9 dB at 30 ips. That is slightly outside the A800's ±1 dB window, though inside ±2 dB. HEAD BUMP ≈ 18 % reproduces the published ±1 dB machine.
- **D9. SATURATION is a drive control with exact make-up.** Hiss and operating level do not move. CALIBRATION is the re-alignment control: hiss re 0 VU moves with it.
- **D10. AGE is one macro (§11).** At 0 the machine is exactly symmetric, matched L/R, with no azimuth or wear: a freshly serviced A800 that still has its specified wow, flutter and noise. The default 18 % is a well-used studio machine inside spec.
- **D11. SAT bar on a dB scale of knee depth (§9).** On a linear scale it would stay dark until +10 VU. The dB scale starts lighting at 0 VU, reaches half the bar at the tape's MOL, and fills only in hard saturation.
- **D12. Crosstalk plateau fixed across speeds.** The fringing corner moves with the wavelength, and the A800's ≥ 40 dB holds at the default for all speeds.

## 17. Open items for the owner and the parent

- A measured A800 (or a Studer service note) would let the [APX] values in §3.4, §4.4 and §6.2 become [HW]:
  - head bump and dip shapes, record-amp knee, tape-type hysteresis parameters, capstan and roller depths.
- The S/N reference (D7) rests on a forum quote. The Studer A800 operating manual is the source to confirm it.
- The TAPE CONDITION display: `tapeResponseDb` (the machine's response as it runs, per channel) is the recommended trace. `outputSpectrum` can be overlaid as live programme.
