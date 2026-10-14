# WEIGHT — model specification

Stereo VCA bus compressor with API 2500 DNA. This file is the top of the
priority order for WEIGHT's DSP: where it conflicts with the master spec or
the mockup, this file wins. The evidence for every law is in `RESEARCH.md`
(sources S1..S12, findings F1..F10).

Provenance tags:

- **[SOURCE_CONFIRMED]** published by API, or in an API-licensed manual (S1–S6).
- **[SCHEMATIC_OR_MEASURED]** read from schematics or measured on a real unit.
  None is available for the 2500 (RESEARCH.md); the tag is not used.
- **[APPROXIMATION]** our modelling choice where no source gives the value
  (section 13 lists what to measure).
- **[TLEET_EXTENSION]** deliberately not on the hardware.

WEIGHT is hardware-informed. It is not a measured 1:1 clone of one unit.

## 1. Signal flow

```
audio L/R ──► VCA (g_L, g_R) ──► makeup ──► output stage (TONE) ──► wet ─► Mix (with dry) ─► Output ─► Bypass ─► out
                 ▲   │
                 │   └── (OLD: x·g, the VCA output before makeup)
                 │
detector feed per channel: NEW x · OLD x·g · external key (forces NEW)
   ─► SC HPF (12 dB/oct) ─► THRUST (+10 dB/dec weighting) ─► square ─► RMS (5 ms)
   ─► level (AES17 dBFS RMS) ─► gain computer (threshold, MED knee, ratio)
   ─► ballistics (attack / release, dB domain) ─► c_ch
stereo link: cv_L = c_L + (k/2)(c_R − c_L), cv_R = c_R + (k/2)(c_L − c_R);  g_ch = 10^(−cv_ch/20)
```

- Everything runs at the host rate. **Latency 0 samples**, reported (no
  oversampling: the only nonlinearity is the subtle TONE stage, whose
  aliasing is measured at −53 dBc worst case, section 11). [APPROXIMATION]
- Feed-forward (API NEW) and feedback (API OLD) share one detector and one
  gain computer; only the detector input changes. [SOURCE_CONFIRMED topology: S1–S4; APPROXIMATION shared ratio circuit: F1]
- The feedback detector reads the VCA output of the previous sample (a
  one-sample loop delay; 21 µs at 48 kHz). The loop is stable for every
  setting: its gain is at most S ≤ 1 and both smoothers are passive.
  [APPROXIMATION: numerical method]
- **Makeup** is applied after the detector tap in both topologies, so it never
  changes the GR (tested to 1e-4 dB). [APPROXIMATION, deliberate: F1]
- **Mix** is a linear crossfade with the (zero-latency) dry signal; **Output** is
  a trim after Mix; Mix 0 % and Bypass are bit-exact dry. [TLEET_EXTENSION]

## 2. Detector and gain law

### 2.1 Detector feed

| DETECTOR | feed of channel ch | provenance |
|---|---|---|
| Feed Forward (API NEW) | x_ch (plugin input) | [SOURCE_CONFIRMED: S1, S2, S3] |
| Feed Back (API OLD) | x_ch · g_ch (VCA output, before makeup) | [SOURCE_CONFIRMED: S1, S2, S4] |
| External SC on (bus live) | key channel (mapping, section 8) | [SOURCE_CONFIRMED: S6, S8] |

A DETECTOR switch crossfades the two feeds' powers over 10 ms; a feed that
comes back in starts from the other feed's filter states scaled by the
applied gain (the filters are linear), so the switch is click-free.
Only the feeds in use are computed. [TLEET_EXTENSION]

### 2.2 RMS detector

```
ms[n] = ms[n−1] + k_rms (p[n] − ms[n−1]),   k_rms = 1 − exp(−1 / (0.005 s · fs))
p[n]  = (filtered feed)²
L     = 10 log10(ms) + 3.0103   (dBFS RMS, AES17: a sine peaking at P dBFS reads P)
```

- True RMS with one time constant (S12). The **5 ms** averaging time is not
  published. [SOURCE_CONFIRMED RMS; APPROXIMATION 5 ms]
- The ripple of the mean square at 2f makes low tones read slightly above
  their RMS (measured ≤ 0.09 dB from 60 Hz to 8 kHz with attack 30 ms /
  release 50 ms). [consequence of the above]

### 2.3 Gain computer, ratio and knee

```
over = L − thr                      (thr in dBFS RMS, section 2.5)
T    = S · softKnee(over, W),       S = 1 − 1/ratio (1 for ∞), W = 6 dB
softKnee(x, W) = 0 (x ≤ −W/2);  x (x ≥ W/2);  (x + W/2)² / (2W) otherwise
```

- Ratios 1.5 / 2 / 3 / 4 / 6 / 10 / ∞. [SOURCE_CONFIRMED: S1, S2, S5]
- The knee is fixed at the 2500's **MED** position, a 6 dB quadratic soft knee:
  GR = 0 at threshold − 3 dB, S·0.75 dB at threshold, S·3 dB at threshold + 3 dB,
  continuous slope. [SOURCE_CONFIRMED positions; APPROXIMATION width; no knee
  control on the skin — adding one needs the owner's approval]
- A ratio switch glides S over 10 ms (no click). [APPROXIMATION]

### 2.4 NEW (feed-forward) vs OLD (feedback): documented difference

Static: in OLD the detector level is the input level minus the GR, so the GR
`c` solves `c = T(L_in − c)` (exact bisection in `staticGainReductionDb()`).
Above the knee:

| ratio label | 1.5 | 2 | 3 | 4 | 6 | 10 | ∞ |
|---|---|---|---|---|---|---|---|
| NEW effective ratio | 1.5 | 2 | 3 | 4 | 6 | 10 | ∞ |
| OLD effective ratio = 1 + S = 2 − 1/ratio | 1.333 | 1.5 | 1.667 | 1.75 | 1.833 | 1.9 | 2 |

The knee is correspondingly softer in OLD (4:1 at threshold: 0.42 dB vs
0.56 dB). This is the "very smooth" OLD of S1/S4 and the "harder", "more
squashed" NEW of S1/S8. [APPROXIMATION: F1, S9]

Timing: near equilibrium (u = RMS level in dB, c = GR) the linearised loop is

```
u' = (L_in − c − u) / τ_rms        c' = (S (u − thr) − c) / τ_a
```

whose slow eigenvalue is the tail of the attack. NEW has no coupling, so its
tail is exactly τ_a; OLD's is faster (2:1, 30 ms: 17.7 ms; ∞, 30 ms: 10 ms;
measured 17.64 / 10.01 ms). With fast attacks and slow releases the OLD loop
can overshoot slightly and recover at the release speed, as the reference ODE
does (section 11). [APPROXIMATION, analysed]

### 2.5 Threshold calibration

```
thr_dBFS(RMS, AES17) = knob_dB − 18          knob −20..+10 → −38..−8 dBFS RMS
```

The knob is the hardware's dBu scale (+10..−20 dBu, S1/S3/S5/S6) at the EBU
R68 alignment 0 dBu = −18 dBFS (S10). A 1 kHz sine peaking at −18 dBFS sits
exactly at threshold with the knob at 0. [SOURCE_CONFIRMED range;
APPROXIMATION alignment: F6]

## 3. Ballistics (attack, release)

```
c[n] = c[n−1] + k (T[n] − c[n−1]),  k = 1 − exp(−1/(τ fs)),  τ = τ_attack if T[n] > c[n−1] else τ_release
```

- One capacitor steered by a diode: exact exponential approach with the label
  as time constant (S9 branching smoother). [SOURCE_CONFIRMED values;
  APPROXIMATION exponential law]
- Attack τ: 30 µs, 100 µs, 300 µs, 1 ms, 3 ms, 10 ms, 30 ms.
- Release τ: 50 ms, 100 ms, 200 ms, 500 ms, 1 s, 2 s. The VARIABLE position
  has no control on the skin and is not modelled.
- Attack and release switch the coefficient on the next sample (no hidden
  envelope). The coefficients are refreshed every 32 samples.
- Effective 63 % GR rise from silence to +15 dB (constant envelope), NEW, all ratios:
  1.68 / 1.75 / 1.99 / 2.97 / 5.42 / 12.7 / 32.5 ms for the seven attacks
  (the RMS integration dominates below ~1 ms; large overshoots are faster:
  0.74 ms at +30 dB). OLD: 0.9..21 ms (faster, loop gain).
- Release: once the detector level drops below the knee the GR decays with
  exactly the label's time constant (fitted τ = label within 0.1 % in both
  topologies). Recovery to 37 % = label + 5..9 ms (the RMS detector's own decay).

## 4. Detector filters (detector path only)

Both are TPT structures in double precision at the host rate.
`scHpfResponseDb()`, `thrustResponseDb()` and `detectorResponseDb()` in
`dsp/DetectorFilters.h` evaluate the discrete transfer functions of the
running filters from the same coefficients (sweep error < 3e-9 dB).

### 4.1 SC HPF

12 dB/oct Butterworth high-pass (TPT SVF, bilinear, pre-warped), 20..240 Hz
log; 20 Hz (the default) only removes subsonics (−0.2 dB at 40 Hz). The
cutoff glides in the log domain over ~20 ms. Not on the 2500 (it has THRUST
and the link SHAPE filters instead). [TLEET_EXTENSION, from the skin]

### 4.2 THRUST

A half-order (+10 dB/decade) slope approximated by first-order zero/pole
pairs, log-spaced so the average slope is exact:

```
pair k of n over [fa, fb], D = log10(fb/fa):  zero = fa·10^(D(k+¼)/n),  pole = fa·10^(D(k+¾)/n)
Hard    (API LOUD): 10 Hz..40 kHz, 4 pairs; 0 dB at 632.5 Hz (√(20·20000))
Neutral (API MED):  10..200 Hz, 2 pairs  +  3..40 kHz, 2 pairs; 0 dB at 774.6 Hz (√(200·3000))
Clean   (API NORM): no filter
```

Digital section: `y = x + (k − 1)·HP1(x)` with k = the analog pair's gain at
Nyquist and the one-pole's g = k·tan(π f_zero / fs), so the zero lands
exactly and nothing over-boosts near Nyquist.

| | 20 Hz | 200 Hz | 1 kHz | 2 kHz | 20 kHz | max deviation from the documented curve, 20 Hz..20 kHz |
|---|---|---|---|---|---|---|
| Hard (96 kHz) | −14.80 | −5.04 | | +5.03 | +14.88 | 0.24 (192 k) .. 0.57 dB (44.1 k) |
| Neutral (96 kHz) | −9.63 | | +0.12 | | +7.74 | 1.30 dB (at the 200 Hz / 3 kHz corners, which a half-order slope necessarily rounds) |

[SOURCE_CONFIRMED LOUD curve (S1, S2, S4, S6); MED shape from S7 (class D);
APPROXIMATION realisation; TLEET_EXTENSION labels Clean/Neutral/Hard (F5)]

Switching THRUST glides each section's (log g, log k) and the pivot gain over
~20 ms (a section with k = 1 is the identity, so Clean is k → 1).

## 5. Stereo link

```
cv_L = c_L + (k/2)(c_R − c_L)        cv_R = c_R + (k/2)(c_L − c_R)        k = STEREO LINK / 100
```

- k = 0: independent channels (API IND). k = 1: both channels get the mean of
  the two control voltages (API 100 %). Identical channels compress exactly as
  one channel at any k. [SOURCE_CONFIRMED structure "control voltages summed,
  each channel keeps its RMS detector" (S1, S2, S6); APPROXIMATION
  normalisation; TLEET_EXTENSION continuous 0..100 % with default 100 %]
- With a signal only in L (R silent): GR_R = (k/2)·c_L, GR_L = (1 − k/2)·c_L.
  Measured (NEW, 4:1, 14 dB over): R = 0 / 1.31 / 2.63 / 3.94 / 5.25 dB for
  k = 0 / 25 / 50 / 75 / 100 %, L = 10.51 → 5.25 dB. In OLD c_L itself depends on
  k (L's detector reads its linked output), documented and tested.
- The link glides over ~20 ms. The hardware's link SHAPE filters are not modelled.
- Mono: one channel, the link does nothing; both telemetry sinks report it.

## 6. VCA, makeup, output stage (TONE), Mix, Output, Bypass

- VCA: `y = x · g`, g = 10^(−cv/20), ideal (no VCA distortion modelled).
  [APPROXIMATION]
- Makeup: −12..+24 dB (0 at the knob top), exact gain, glides over 20 ms.
  The 2500's auto make-up has no control on the skin and is not modelled.
- **TONE** (output-stage voicing) — [TLEET_EXTENSION, F8]:

```
u = x / √(1 + x²/16),   d = h2·u² − h3·u³,   y = x + HP5Hz(d)
then a one-pole low shelf at 60 Hz and a one-pole high shelf at 10 kHz
```

| TONE | h2 | h3 | low shelf | high shelf | THD 1 kHz at −20 / −10 / 0 dBFS | character |
|---|---|---|---|---|---|---|
| Old | 0.010 | 0.004 | +0.5 dB | −0.5 dB | 0.050 / 0.157 / 0.48 % | 2nd-dominant, fuller lows, softer top (transformer-coupled discrete amp) |
| Neutral | 0 | 0 | 0 | 0 | 0 (bit-exact identity) | clean reference |
| New | 0.0015 | 0.010 | −0.2 dB | +0.4 dB | 0.008 / 0.034 / 0.24 % | 3rd-dominant, tighter lows, a little air |

  Neutral bound: with no GR the plugin output is **bit-identical** to the input.
  TONE never reaches the detector: the GR trace is bit-identical for all three
  (also in OLD, where the detector reads the VCA output, which is before TONE).
  `toneResponseDb()` gives the small-signal response (sweep error < 1e-6 dB).
  Aliasing at 1x (15 kHz, 0 dBFS, 48 kHz, 3rd harmonic folded to 3 kHz):
  Old −60.6 dBc, New −53.3 dBc. Switching TONE glides all coefficients over 20 ms.
- Mix: 0..100 %, linear, 30 ms smoothing. Output: −12..+12 dB after Mix, 30 ms
  smoothing. Bypass: 20 ms ramp; fully bypassed = bit-exact dry.
- DC passes the VCA path (it is DC-coupled in the model; the hardware's
  transformers are not modelled as a high-pass) but never reaches the
  detector (the SC HPF is always at least 20 Hz).

## 7. GR telemetry (per channel, actual applied gain)

`WeightProcessor::grLeft` and `grRight` (`dsp/GrTelemetry.h`). For every
sample the model pushes the channel's applied VCA gain g (before makeup) and
its GR in dB (= −20·log10 g). Three readings, lock-free (atomics), never
clipped to the meter's range:

| GR MODE | reading | formula |
|---|---|---|
| Peak | `takePeakDb()` | the deepest GR (dB) since the previous peak read; with no audio since, the latest GR |
| RMS | `getRmsDb()` | −10·log10( (1/W) Σ g² ) over the last W samples: the attenuation of the energy of a steady signal |
| Average | `getAverageDb()` | (1/W) Σ GR_dB over the last W samples (mean GR in dB) |

W = round(0.3 s · fs) (300 ms rectangular window); before W samples have
passed, the missing samples count as 0 dB. The sums are recomputed exactly
once per window (no drift). Example: half the window at 0 dB and half at
12 dB reads Peak 12, RMS 2.74, Average 6.00 dB.

`WeightProcessor::takeGrDb (channel, mode)` returns the selected reading (Peak
resets that channel's hold; RMS/Average reset nothing); `getGrMode()` reads
the GR MODE parameter. GR MODE never reaches the DSP (audio bit-identical in
all three modes, tested). With link < 100 % L and R differ and both meters
show their own channel (addendum 10).

Measured: max |telemetry − formula on the applied gains| = 1.3e-6 dB (Peak),
9.5e-7 dB (RMS), 9.6e-7 dB (Average) over both topologies, links 0/50/100 %;
the attenuation of the audio equals the telemetry within 0.05 dB in steady
windows; 37 dB of GR is reported as 37.15 dB.

## 8. External side-chain

- Optional "Sidechain" input bus (mono or stereo, **disabled by default**). The
  key feeds the detectors only when External SC is on and the bus has
  channels; otherwise the internal detector runs (safe fallback) and
  `isUsingExternalSidechain()` says which.
- Channel mapping: stereo key L → L detector, R → R detector (then the link
  as set); mono key → both detectors; mono main + stereo key → the mean power
  of the two key channels.
- With a key the detector is feed-forward whatever DETECTOR says (the key
  replaces the feed; the hardware's side-chain only works in NEW: S6, S8).
- SC HPF and THRUST filter the key. The key never reaches the audio
  (key-frequency component −299 dBFS; silent main + loud key = silence).
- Toggling External SC crossfades the feeds over 10 ms. Factory presets
  store `scExternal = 0`.

## 9. Parameters (namespace `tleet::weight::params`, version 1, IDs frozen)

| ID | name | range / choices (exact strings) | step | default | notes |
|---|---|---|---|---|---|
| `threshold` | Threshold | −20..+10 dB, 0 at the knob centre (piecewise) | 0.1 dB | 0 dB | = dBu; −18 dBFS RMS at 0 (2.5) |
| `attack` | Attack | "0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" | choice | "10 ms" | 3 |
| `release` | Release | "0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s" | choice | "0.2 s" | 3 |
| `ratio` | Ratio | "1.5", "2", "3", "4", "6", "10", "∞" | choice | "2" | 2.3 |
| `makeup` | Makeup | −12..+24 dB, 0 at the knob centre (piecewise) | 0.1 dB | 0 dB | 6 |
| `thrust` | Thrust | "Clean", "Neutral", "Hard" (= API NORM / MED / LOUD) | choice | "Clean" | 4.2 |
| `detector` | Detector | "Feed Forward", "Feed Back" (= API NEW / OLD) | choice | "Feed Forward" | 2.4 |
| `tone` | Tone | "Old", "Neutral", "New" | choice | "Neutral" | 6 |
| `scHpf` | SC HPF | 20..240 Hz, log | continuous | 20 Hz | 4.1 |
| `scExternal` | External SC | bool | — | off | 8 |
| `mix` | Mix | 0..100 %, linear | 0.1 | 100 % | 6 |
| `output` | Output | −12..+12 dB, linear | 0.1 dB | 0 dB | after Mix |
| `link` | Stereo Link | 0..100 %, linear | 0.1 | 100 % | 5 |
| `grMode` | GR Mode | "Peak", "RMS", "Average" | choice | "RMS" | display only (7) |
| `bypass` | Bypass | bool | — | off | bit-exact dry |

- Default DETECTOR = Feed Forward: API's "NEW", the modern default and the only
  position where the hardware's side-chain works. Default link 100 % (Waves'
  licensed default; keeps the stereo image of a bus).
- Threshold and makeup use GLUE's `centredDbRange`: exact inverse, 0.1 dB grid,
  every grid value round-trips through range, parameter and text.

## 10. Provenance summary

| block | tag |
|---|---|
| Feed-forward / feedback detector positions | SOURCE_CONFIRMED (S1–S4) |
| Shared gain computer in both positions (OLD = 2 − 1/ratio) | APPROXIMATION (F1, S9) |
| One-sample delay in the feedback loop | APPROXIMATION (numerical) |
| RMS detector | SOURCE_CONFIRMED (S1–S4) |
| RMS time constant 5 ms | APPROXIMATION |
| Ratio, attack and release steps | SOURCE_CONFIRMED (S1, S2, S5) |
| Exponential dB-domain ballistics | APPROXIMATION (S9) |
| Release VARIABLE position | not modelled (no control) |
| Knee fixed at MED, 6 dB quadratic | APPROXIMATION (positions SOURCE_CONFIRMED) |
| Threshold range −20..+10 dBu | SOURCE_CONFIRMED (S1, S3) |
| Threshold alignment 0 dBu = −18 dBFS | APPROXIMATION (EBU R68, S10) |
| THRUST LOUD curve | SOURCE_CONFIRMED (S1, S2, S4, S6) |
| THRUST MED shape | APPROXIMATION from S7 (class D) |
| THRUST realisation (pole/zero pairs) | APPROXIMATION |
| THRUST labels Clean/Neutral/Hard | TLEET_EXTENSION (skin) |
| SC HPF | TLEET_EXTENSION (skin) |
| Link: summed control voltages, own RMS detectors | SOURCE_CONFIRMED (S1, S2, S6) |
| Link normalisation, continuous 0..100 % | APPROXIMATION / TLEET_EXTENSION |
| Link SHAPE filters | not modelled (no control) |
| Ideal VCA | APPROXIMATION |
| Makeup after the detector tap (also in OLD) | APPROXIMATION (deliberate; S5 differs) |
| Auto make-up | not modelled (no control) |
| TONE output-stage voicing | TLEET_EXTENSION |
| Mix, Output, crossfades, GR MODE | TLEET_EXTENSION |
| External key forces feed-forward | SOURCE_CONFIRMED (S6, S8) |

## 11. Verification (tests/WeightTests.cpp, all `WEIGHT:`)

29 tests; CSVs in `<build>/tests/test-output/WEIGHT_*.csv` (static curves,
attack/release steps, OLD vs NEW ratios, THRUST sweeps, link sweep, GR
telemetry error trace, TONE THD). Key numbers:

- Static curves (7 ratios × 4 thresholds × 2 topologies, −10..+30 dB):
  max |model − law| = 1.7e-6 dB; ratio labels measured exactly (NEW) and
  2 − 1/ratio (OLD) to 0.1 %; knee anchors to 1e-4 dB. Processor (1 kHz sine,
  300 ms average telemetry): max error 0.011 dB.
- Attack vs the documented continuous-time ODE (RK4, 384 kHz): within 3 % for
  every step, ratio and topology; NEW tail τ = label within 0.5 % (10 / 30 ms).
- Release: fitted τ = label within 0.1 %, both topologies; recovery to 37 % =
  label + 5..9 ms, matching the ODE within 1 %.
- OLD vs NEW: ratio, knee and tail timing as section 2.4; on a programme with
  level steps (6:1) mean GR 14.9 dB (NEW) vs 7.7 dB (OLD).
- THRUST and SC HPF sweeps vs the response functions: < 3e-9 dB at 44.1..192 kHz;
  processor GR = static law at level + response within 0.09 dB; a 50 Hz bass
  under a 2 kHz tone loses GR from Clean to Neutral to Hard. SC HPF 240 Hz cuts
  the kick-driven pumping of a 1 kHz tone 41-fold (10.1 → 0.25 dB swing).
- Stereo-link sweep and GR telemetry: section 5 and 7 numbers.
- Sidechain matrix (internal / external / silent / sine / transient / stereo
  mapping / mono / fallback / no leakage / filters on the key).
- TONE: section 6 numbers. Mix null 3.7e-8; Mix 0 and Bypass bit-exact;
  latency 0 reported and measured; editor, GR MODE and state do not alter audio.
- No allocation in processBlock at 44.1/48/88.2/96/192 kHz × blocks 1..2048 ×
  mono/stereo × key.
- CPU (this container, block 128, stereo, THRUST Hard, TONE Old): 1.5 % of one
  core at 44.1 kHz, 2.0 % at 48 kHz, 3.5 % at 96 kHz, 7.3 % at 192 kHz
  (+0.2 % with a key).

## 12. Not modelled (intentionally)

- KNEE switch (fixed MED), release VARIABLE, auto make-up, link SHAPE filters,
  the L/R tilt trim, IN (soft bypass) vs BYP, VU metering of input/output.
- VCA distortion, noise, the 2520/transformer frequency response (the TONE
  extension is the only colour).
- The 2500+ BLEND modes (WEIGHT's Mix is the crossfade mode).

## 13. Open measurement tasks

1. RMS detector time constant (5 ms assumed) and whether the attack/release
   switch also changes it, from a 2500 schematic or a step measurement.
2. KNEE widths of HARD / MED / SOFT (MED assumed 6 dB quadratic).
3. THRUST MED corners and slope, measured at the detector (S7 is the only
   source); the LOUD curve's behaviour beyond 20 Hz / 20 kHz.
4. OLD-mode static curve on a real unit: is the ratio circuit shared (effective
   2 − 1/ratio, as modelled) or recalibrated for feedback?
5. Whether make-up is inside the OLD loop (UA's note, S5).
6. The exact link law (normalised sum assumed) and the SHAPE filter corners.
7. Threshold alignment against a hardware unit at +4 dBu = 0 VU.
