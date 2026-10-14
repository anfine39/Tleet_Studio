# STEEL — model specification

`TLEET // STEEL` is an **original true-peak mastering limiter** (master spec
13.11). It is not a clone of any product. This file turns the research in
`RESEARCH.md` into the DSP that runs. Each value or behaviour carries one of
four provenance tags:

| Tag | Meaning in STEEL |
|---|---|
| `SOURCE_CONFIRMED` | Taken from a standard cited in RESEARCH.md (S1–S6): BS.1770, R 128, Tech 3341, Tech 3342. |
| `SCHEMATIC_OR_MEASURED` | A published structure (S7–S15), or a value measured by STEEL's own tests. |
| `APPROXIMATION` | A numerical method or design choice that stands in for an ideal. |
| `TLEET_EXTENSION` | A TLEET design decision. The owner decisions are listed in section 17. |

Code:

- `plugins/STEEL/dsp/`: `Halfband.h`, `TruePeak.h`, `Primitives.h`,
  `Filters.h`, `Modes.h`, `Loudness.h`, `Telemetry.h`, `SteelEngine.h/.cpp`
- `plugins/STEEL/Parameters.h`
- `plugins/STEEL/PluginProcessor.h/.cpp`

Tests: `tests/SteelTests.cpp` (29 tests, all named `STEEL: …`).

---

## 1. Scope

| Item | Value | Tag |
|---|---|---|
| Buses | mono→mono, stereo→stereo. Everything else is rejected. | `TLEET_EXTENSION` |
| Latency | OVERSAMPLING + LOOKAHEAD + ceiling stage, exact, reported through `setLatencySamples` (section 12) | `SCHEMATIC_OR_MEASURED` |
| Precision | Audio in float. Release envelopes, loudness sums and the box-filter sums are in double. | `APPROXIMATION` |
| Sample rates | Any. Tested at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. | — |
| Tail | 0 s | — |
| Oversampling | User selectable: 1x / 2x / 4x / 8x (master 6.4) | `TLEET_EXTENSION` |

## 2. Signal flow

```text
in ─► sanitise (NaN/Inf → 0) ─► x ───────────────────────────────────────────── dry (delayed by the latency) ─┐
         │                                                                                                    │
         ▼                                                                                                    │
       INPUT (±, 20 ms C1 ramp) ─► u0 ─┬─► IN meters, input true peak                                         │
                                       ▼                                                                      │
                         SUB FILTER (Off / 20 / 25 / 30 Hz) ─► u                                              │
                                       ▼                                                                      │
   ┌───────────────────── LimiterSlot (A or B, crossfaded on MODE / OVERSAMPLING / LOOKAHEAD) ─────────────┐   │
   │ up ×L (half-band) ─► pad ─► SOFT CLIP ─► required gain (knee) ─► sliding min ─► release (MANUAL/AUTO)  │   │
   │   ─► attack ramp (box cascade) ─► STEREO LINK ─► × delayed signal ─► down ×L                           │   │
   └────────────────────────────────────────────────────────────────────────────────────────────────────────┘   │
                                       ▼                                                                      │
             CeilingStage (host rate): true-peak or sample-peak detector ─► hold ─► 15 ms release              │
               ─► 0.25 ms ramp ─► STEREO LINK ─► × delayed signal ─► y   (≤ CEILING)                          │
                                       ▼                                                                      │
                     × OUTPUT ─► × LEVEL MATCH ─► DELTA crossfade with (u − y) / INPUT ─► Bypass crossfade ◄────┘
                                       ▼
                                      out ─► OUT meters, output true peak, loudness, history, warnings, spectrum
```

This is master spec 13.11's topology: input → pre-gain → optional soft clip
→ lookahead detector → limiting engine → true-peak stage → output ceiling →
metering. STEEL adds SUB FILTER after the pre-gain, and OUTPUT and LEVEL
MATCH after the ceiling (decisions D3 and D4).

## 3. Input stage

### 3.1 INPUT (`TLEET_EXTENSION`)

- u0 = clamp(x, ±1e4) × G_in. The clamp only keeps absurd host values finite
  through the gain; it is +80 dBFS.
- G_in moves along a dB-linear ramp of 20 ms. A 3 ms one-pole then rounds
  the ramp's corners (`SmoothGain`). Even a 24 dB jump at full scale
  therefore has no slope discontinuity (section 11).

### 3.2 SUB FILTER (`SCHEMATIC_OR_MEASURED`, S13 / S14)

- 4th-order Butterworth high-pass: two TPT SVF sections, Q = 0.5412 and 1.3066.
- Measured response (`STEEL_subfilter.csv`): −3.010 dB at 20 / 25 / 30 Hz;
  −24.099 dB an octave below; −0.017 dB an octave above; 0.000 dB at 100 Hz.
  This equals the analogue Butterworth response to 0.001 dB.
- **20 ↔ 25 ↔ 30:** the cut-off glides in the log domain (15 ms).
- **Off ↔ on:** a 50 ms crossfade between u0 and the filtered signal.
- **Off:** exact identity. The filter keeps running so that it is warm when
  switched on.
- It sits **before** the limiter, so sub-sonic energy does not cost gain
  reduction.
- The IN meters measure **before** it (decision D2).

## 4. Limiter (stage A, the slot)

Every slot runs at L = 1, 2, 4 or 8 times the host rate (section 5). Each
channel runs the following per oversampled sample:

### 4.1 Required gain and soft knee (`SCHEMATIC_OR_MEASURED`, S7)

- r = 1 below the knee.
- r = T / |v| above the knee.
- Inside a knee of width W dB centred on the ceiling T:
  GR = (over + W/2)² / (2W), the S7 soft knee with ratio ∞.
- Measured: DENSE (W = 3 dB) on a sine 1 dB below the ceiling gives
  −0.0417 dB, which is the formula (0.25 / 6). The other modes give exactly 0.

### 4.2 Lookahead hold and attack ramp (`SCHEMATIC_OR_MEASURED`, S8 / S9; shape `TLEET_EXTENSION`)

- D = round(LOOKAHEAD × fs) host samples, × L.
- The signal is delayed by D + pad.
- The required gain r is delayed by D + pad − a. Here a = round(attackFraction × D)
  is the attack window.
- h = **sliding minimum** of r over a + 1 samples (Lemire wedge, S9).
- The applied envelope passes through a **cascade of 1–3 box filters**. Their
  combined support is a + 1, giving a linear, quadratic-S or cubic-S ramp.
- A box average over a window that contains the peak sample is never above
  the sliding minimum at that peak. So the gain is at or below the required
  gain **on the peak sample itself**: the limiter's guarantee holds by
  construction.
- LOOKAHEAD 0 → a = 0: an instant gain change on the peak sample. The
  ceiling stage (section 6.3) still smooths it.
- Measured onsets at LOOKAHEAD 5 ms, 48 kHz (`STEEL_modes.csv`): the
  0.26 dB threshold is crossed 177 / 62 / 193 / 133 samples before the peak
  for CLEAN / PUNCH / DENSE / STEEL. The attack fraction × LOOKAHEAD is
  240 / 72 / 240 / 168.

### 4.3 Release (MANUAL and AUTO)

**Fast envelope.** e_f is the gain that results from h:

- An instant attack: e_f = h when h < e_f.
- A one-pole release in linear gain: e_f += α (h − e_f).
- e_f is in **double** precision. In float, the step α(1 − e_f) falls below
  the resolution of e_f near 1. At 8x with a 1 s release, the gain would then
  stall about 0.2 dB short of unity. A test found this.

**MANUAL** (`TLEET_EXTENSION`):

- α comes from the RELEASE knob, as a time constant (63 %).
- Measured time constant, against the knob (`STEEL_manual_release.csv`):

  | Knob | Measured |
  |---|---|
  | 20 ms | 20.9 ms |
  | 50 ms | 50.8 ms |
  | 180 ms | 180.5 ms |
  | 500 ms | 500.3 ms |
  | 1000 ms | 1000.0 ms |

  The release is the same at 1x, 4x and 8x. The extra ~0.9 ms is the attack
  ramp's own smoothing.

**AUTO** (`TLEET_EXTENSION`; RESEARCH F5). Two branches; the applied
envelope is min(e_f, e_s).

- The fast branch uses the mode's `autoFastMs`.
- The slow branch runs once per host sample, in dB:
  - it follows `autoSlowDepth` × the fast branch's deepest GR in that sample;
  - with its own attack (`autoSlowAttackMs`) and release (`autoSlowReleaseMs`).
- **The RELEASE knob is not used in AUTO.** The test checks that AUTO with
  RELEASE 20 ms and with 1000 ms is bit-identical.
- Switching AUTO ↔ MANUAL is continuous:
  - AUTO → MANUAL: e_f = min(e_f, e_s);
  - MANUAL → AUTO: the slow branch starts at the current GR.
- Measured AUTO recovery after an isolated peak (time constant):

  | Mode | Measured | Mode value |
  |---|---|---|
  | CLEAN | 121.5 ms | 120 ms |
  | PUNCH | 30.7 ms | 30 ms |
  | DENSE | 52.0 ms | 50 ms |
  | STEEL | 71.3 ms | 70 ms |

- **No pumping** (`STEEL_auto_release.csv`). The test signal is a 60 Hz bed
  6 dB over the ceiling, plus a 4 ms transient every 0.5 s. "Pump" is the
  extra GR 20–300 ms after each transient. "Ripple" is the GR's standard
  deviation on the bed.

  | | Pump | Ripple |
  |---|---|---|
  | AUTO | 0.02–0.62 dB | 0.10 / 0.30 / 0.15 / 0.10 dB (C / P / D / S) |
  | MANUAL 1000 ms | 1.5–2.0 dB | — |
  | MANUAL 20 ms | — | 0.20 / 0.42 / 0.19 / 0.27 dB |

### 4.4 MODE (`TLEET_EXTENSION`; values in `Modes.h`)

| | attackFraction | Ramp | Knee | AUTO fast | Slow attack / release | Slow depth | Clip scale |
|---|---|---|---|---|---|---|---|
| CLEAN | 1.00 | cubic S (3 boxes) | 0 dB | 120 ms | 300 / 1500 ms | 0.90 | 0.5 |
| PUNCH | 0.30 | linear (1 box) | 0 dB | 30 ms | 600 / 400 ms | 0.50 | 0.8 |
| DENSE | 1.00 | quadratic S (2) | 3 dB | 50 ms | 200 / 600 ms | 0.60 | 1.5 |
| STEEL | 0.70 | quadratic S (2) | 1 dB | 70 ms | 300 / 900 ms | 0.75 | 1.0 |

What each mode is for:

- **CLEAN:** the longest and smoothest ramp, slow recovery, conservative clipping.
- **PUNCH:** the gain dips late and steeply, so the transient's onset passes
  before the dip. Recovery is fast.
- **DENSE:** the knee gives continuous control below the ceiling, recovery is
  fast, and the clipper takes 1.5× the load.
- **STEEL:** the balanced default.

The modes are measurably distinct (`STEEL_modes.csv`). Each row below is the
same programme at INPUT +8 dB, or a 50 Hz tone 6 dB over the ceiling for THD:

| | LUFS-I | Crest | THD 50 Hz (6 dB over) |
|---|---|---|---|
| CLEAN | −10.29 | 9.73 dB | 0.40 % |
| PUNCH | −7.96 | 7.43 dB | 3.41 % |
| DENSE | −8.83 | 8.28 dB | 0.81 % |
| STEEL | −9.17 | 8.62 dB | 1.06 % |

### 4.5 STEREO LINK (`TLEET_EXTENSION`)

- Per oversampled sample, each channel moves towards the deeper of the two
  gains: g_c += k (min(g_L, g_R) − g_c), where k = LINK / 100.
- The ceiling stage links the same way.
- A channel's gain never rises above its own requirement, so each channel
  keeps its guarantee at any link.
- Measured (L 6 dB over, R 20 dB under, at 1x and 4x):

  | LINK | GR on R |
  |---|---|
  | 0 % | exactly 0 dB, level unchanged |
  | 50 % | −2.49 dB (expected −2.50) |
  | 100 % | the same as L, −6.00 dB |

### 4.6 SOFT CLIP (`TLEET_EXTENSION`; curve `APPROXIMATION`)

- Runs inside the oversampled section, before the limiter. It is crossfaded
  on/off over 20 ms.
- The effective amount is a = AMOUNT × the mode's clip scale, clamped to 0..1.
- Curve:
  - saturation level S = CEILING + 3 (1 − a) dB;
  - knee start K = S − (1 + 5a) dB;
  - identity below K;
  - a quadratic C1 knee that reaches S with zero slope at K + 2 (S − K);
  - flat above that;
  - odd symmetric.
- The limiter after it catches whatever the clipper leaves above the ceiling.
- Measured (`STEEL_softclip_aliasing.csv`):
  - Mean limiter GR on programme at INPUT +8: −2.24 dB with the clipper off;
    −1.21 / −0.88 / −0.08 dB at AMOUNT 0 / 30 / 100 %.
  - Mode scale, as the THD of a sine 1 dB below the ceiling at AMOUNT 60 %:
    CLEAN 0.000002 % (not clipped), PUNCH 0.18 %, STEEL 0.72 %, DENSE 2.56 %.
  - Aliasing of a 7 kHz tone at AMOUNT 100 %, relative to the fundamental:
    −29.0 dB at 1x, −51.4 dB at 2x, −94.1 dB at 4x, −94.6 dB at 8x.

## 5. Oversampling

### 5.1 Filters (`SCHEMATIC_OR_MEASURED`, S10 / S11)

Linear-phase half-band FIR stages, Kaiser-windowed, N = 4k + 3. The centre
tap is 0.5 and every other tap is 0.

| Stage | Rates | N | Stopband | Measured |
|---|---|---|---|---|
| 1 | fs ↔ 2 fs | 127 | 90 dB | flat < 0.001 dB to 20 kHz at 44.1 kHz; < −85 dB from 24.1 kHz |
| 2 | 2 fs ↔ 4 fs | 35 | 100 dB | |
| 3 | 4 fs ↔ 8 fs | 23 | 100 dB | |

- The upsampler's odd phase is a pure delay, so original samples pass bit-exactly.
- The filters are evaluated as forward dot products with eight partial sums,
  which the compiler vectorises without fast-math.

### 5.2 Latency (exact)

The round trip in top-rate samples is padded up to whole host samples:

| OVERSAMPLING | 1x | 2x | 4x | 8x |
|---|---|---|---|---|
| Host samples | 0 | 63 | 72 | 75 |

## 6. True peak

### 6.1 Estimator (`SOURCE_CONFIRMED` method, S1 Annex 2; filters `APPROXIMATION`)

- Upsample by P = 8 below 60 kHz, 4 below 120 kHz, 2 above, so that P × fs ≥ 352.8 kHz.
- The first stage is a half-band FIR with N = 511 and 60 dB. It is flat to
  0.493 fs, so the reconstruction includes content up to fs/2. Images at
  −60 dB move an estimate by at most 0.009 dB.
- Stages 2 and 3 are the oversampler's.
- Every local maximum of |v| is refined with a parabola through it and its
  neighbours.
- Output: the peak of interval [j, j + 1), A = 1 + λ/P host samples late. A
  is 134 / 132 / 128 at P = 8 / 4 / 2.
- **One class** serves four users:
  - the ceiling stage;
  - the output true-peak meter;
  - the input true-peak meter;
  - the loudness meter's max TP.

### 6.2 Accuracy (`SCHEMATIC_OR_MEASURED`)

- On sines at 997 Hz – 0.45 fs, and the fs/4 at 45° and fs/6 at 60°
  vectors, at 44.1–192 kHz: worst error 0.0011 dB.
- On isolated band-limited pulses at fractional sample positions: worst
  error 0.0005 dB. Without the parabolic refinement it is 0.036 dB.
  - The design target is 0.02 dB; BS.1770 asks for 0.2 dB.
- On programme, against the independent reference: ≤ 0.025 dB.
- The fs/4 at 45° vector reads 0.00 dBTP, against a sample peak of −3.01 dBFS.

### 6.3 Ceiling stage (stage B, host rate) and the tolerance policy

- **Detector.** The estimator of 6.1 is crossfaded (TRUE PEAK on/off over
  20 ms) with |x| delayed by A. So TRUE PEAK off is a sample-peak ceiling
  with the same latency: toggling TRUE PEAK never changes the latency
  (decision D7).
- **Gain.** r = CEILING / p above the ceiling. Then:
  - a sliding minimum over W_B + 2K_h, where K_h = max(8, round(fs/3000)) is
    the hold radius covering the reconstruction kernel's main lobe;
  - an instant attack and a 15 ms release;
  - two box filters with W_B = max(4, round(0.25 ms × fs)).
- **Latency:** A + W_B − 1 + K_h host samples.

  | fs (kHz) | 44.1 | 48 | 88.2 | 96 | 176.4 | 192 |
  |---|---|---|---|---|---|---|
  | Ceiling stage (samples) | 159 | 161 | 182 | 187 | 230 | 239 |

  (`TLEET_EXTENSION` constants.)
- Stage A has already done the musical limiting. Stage B only removes
  residual overs: those from the downsampling filters, from LOOKAHEAD 0, and
  inter-sample peaks at 1x.
- **Guarantees, measured** (`STEEL_ceiling_48k.csv`, `STEEL_ceiling_rates.csv`;
  140 configurations at 48 kHz, plus every rate, block sizes 1–2048, mono,
  and CEILING −12 to 0):
  - **Sample peak** (TRUE PEAK off or on): always ≤ CEILING. Worst
    −2.9e-7 dB, which is float rounding.
  - **True peak, by the independent reference** (windowed sinc, −6 dB at
    fs/2; RESEARCH F1):
    - programme and inter-sample test vectors: ≤ CEILING + 0.05 dB (worst
      measured +0.029 dB);
    - full-band white noise and naive squares, which have energy up to fs/2
      where the reconstruction itself is definition-dependent: ≤ CEILING +
      0.1 dB (worst measured +0.071 dB, 1x).
  - **The plugin's own TP meter** agrees with the reference within 0.032 dB.

## 7. Output stage

### 7.1 DELTA (`TLEET_EXTENSION`; master 13.11 "mathematically meaningful")

- d = (u_aligned − y) / G_in,aligned. This is exactly what the soft clipper,
  the limiter and the ceiling stage removed, referred back to the plugin
  input level.
- It does not pass through OUTPUT or LEVEL MATCH.
- It is crossfaded over 20 ms.
- Measured:
  - |d − (u − y)/G| ≤ 6e-8 at 1x;
  - digital silence when nothing is limited (1x);
  - −40 dB of band-limiting residue at 4x;
  - at INPUT +24 dB its peak stays at 0.95 × the input peak (no explosion).

### 7.2 OUTPUT (`TLEET_EXTENSION`, decision D3)

- A post-limiter trim, −12 to +12 dB, 0 at the knob centre, with the same
  ramp as INPUT.
- At OUTPUT ≤ 0 dB the ceiling holds at the output.
- At OUTPUT > 0 dB the output exceeds CEILING by the OUTPUT gain. TP OVERS
  then reports it, because it compares against CEILING.

### 7.3 LEVEL MATCH (`TLEET_EXTENSION`, decision D4)

- Compares K-weighted 3 s loudness (30 hops of 100 ms) of the plugin input x
  and of the processed signal after OUTPUT.
- target = clamp(L_in − L_out, −30, 0) dB, smoothed per hop with a 1 s time
  constant, applied with a 100 ms C1 ramp.
- It holds while either side is below −70 LUFS.
- It **never boosts**, so it can only lower the exported peak.
- Measured, at INPUT +6 / +12:

  | INPUT | Output re input, off | Output re input, on |
  |---|---|---|
  | +6 | +5.89 LU | +0.02 LU |
  | +12 | +9.96 LU | −0.01 LU |

  Quieter than the input: the output is bit-identical to LEVEL MATCH off.

### 7.4 Host Bypass (`TLEET_EXTENSION`)

- Bypass is a parameter (`getBypassParameter`).
- It crossfades over 20 ms (linear) to the sanitised input, **delayed by the
  reported latency**.
- Fully bypassed, the output is that delayed input bit for bit. This is
  tested for every OVERSAMPLING, LOOKAHEAD, mono and stereo.
- The latency stays reported while bypassed, so the timeline does not jump.

## 8. Loudness (`SOURCE_CONFIRMED`, S1–S6)

- **K-weighting:** S6's parameterisation for any rate, checked to 2e-7
  against BS.1770's 48 kHz table.
- **Channel weights:** 1 for L and R. A mono bus is **one** channel with
  G = 1 (decision D9).
- **Hops:** 100 ms (round(0.1 fs)).
  - **M:** the last 4 hops (400 ms).
  - **S:** the last 30 hops (3 s).
  - Both update every 100 ms.
- **I:** one 400 ms block per hop (75 % overlap), absolute gate −70 LUFS,
  relative gate −10 LU.
- **LRA:** one S value per hop once 30 hops exist. Absolute gate −70 LUFS,
  relative gate −20 LU below their power mean; L95 − L10 at rank round(p (n − 1)).
- **Memory:** 0.01 LU histogram bins from −70 to +10 LUFS, with counts and
  exact energy sums. Memory is fixed however long the measurement runs.
- **Max TP:** the 6.1 estimator. **Max SP:** |sample|.
- **What is measured:** the final plugin output, after OUTPUT, LEVEL MATCH,
  DELTA and Bypass (decision D5).
- **Reset:** `resetLoudness()` from any thread. The audio thread performs it
  at its next block. Readings read as "none" from the moment of the request.
- Verified at ±0.1 LU:

  | Check | Rates | Worst result |
  |---|---|---|
  | Tech 3341 cases 1 and 2 (M, S, I) | 44.1, 48, 96, 192 kHz | within ±0.1 LU |
  | Tech 3341 cases 3–5 (I) | 48 kHz | within ±0.1 LU |
  | Tech 3341 case 9: S constant at −23 | 44.1, 48, 96 kHz | 0.016 LU |
  | Tech 3341 case 12: M constant at −23 | 44.1, 48, 96 kHz | 0.042 LU |
  | Tech 3342 cases 1–4 | 48 kHz | 10.000 / 5.000 / 20.000 / 15.000 LU |
  | Continuous LRA (level ramp) | 48 kHz | an analytic reference |

## 9. Telemetry (exactly as implemented, `PluginProcessor.h`)

All telemetry is lock-free. Index 0 = L, 1 = R. A mono bus feeds both
indices with the single channel.

### 9.1 Meters

`std::array<dsp::MeterSource, 2> inputMeters, outputMeters`

- **IN** measures **after INPUT, before SUB FILTER**: the signal that drives
  the limiter.
- **OUT** measures the final plugin output, after OUTPUT, LEVEL MATCH, DELTA
  and Bypass.
- Each is a sample peak (`takePeak()`) and RMS (`takeRms()`) since the last take.
- `inputMeter` / `outputMeter` (the base class's) carry the channel-summed
  values.

### 9.2 True peak

- `float takeOutputTruePeakDb (int channel)`
- `float takeInputTruePeakDb (int channel)`

Each returns the maximum true peak since the previous call, in dBTP, floor
−120. The estimator is 6.1. **Input** is after INPUT, before SUB FILTER;
**output** is the final output. Both are **always measured**, whether or not
the editor is open. The input estimator costs about 1/6 of the plugin's
true-peak work.

### 9.3 Gain reduction and history

GR is the gain actually applied by the limiter × the ceiling stage, per
channel. The soft clipper is a waveshaper and is excluded. Values are in dB
and ≤ 0.

- `float getCurrentGainReductionDb()`: the deepest GR in the most recent
  completed 10 ms slice, deeper channel.
- `float getCurrentGainReductionDb (int channel)`: the same for one channel.
- `float takeGainReductionDb()`: the deepest GR since the previous call
  (0 if none).

History:

- `struct HistorySlice { float outPeakDb; float outRmsDb; float grDb; }`,
  aliased as `SteelProcessor::HistorySlice`.
- `static constexpr double historySliceSeconds = 0.01`
- `int popHistory (HistorySlice* dest, int maxSlices)`

Each slice holds:

- the output sample peak (both channels, dBFS, floor −120);
- the output RMS (the mean of the channels' mean squares, dBFS, floor −120);
- the deepest applied GR (dB ≤ 0, deeper channel).

The FIFO holds 4096 slices. A reader that falls more than 2048 slices behind
(editor closed) skips to the newest 2048, so it never shows stale history.
The editor keeps its own 5–10 s display buffer (addendum 14).

Measured:

- At 1x, output / aligned input = reported gain, to a relative 1.5e-7.
- Slice peak, RMS and GR equal the output and the applied gain to under 3e-6 dB.
- Below the ceiling, GR is exactly 0.

### 9.4 Loudness

- `LoudnessReadings getLoudness() const`
  - `momentaryLufs`, `shortTermLufs`, `integratedLufs`: −inf = no reading.
  - `loudnessRangeLu`: 0 until two values pass the gates.
  - `maxTruePeakDbtp`, `maxSamplePeakDbfs`: −inf when there is none.
  - `measuredSeconds`
- `void resetLoudness()`

### 9.5 Warnings (decision D6)

Warnings never change the audio.

- `bool isTruePeakOver()`: true while an output true peak above CEILING
  (the ceiling in force at that sample) + `tpOverToleranceDb` = 0.1 dB
  occurred within the last `tpOverHoldSeconds` = 2.0 s of processed audio.
- `bool isHeavyLimiting()`: true while the mean applied GR over the last
  `heavyLimitingWindowSeconds` = 2.0 s (200 slices, the mean of per-sample
  dB) is ≥ `heavyLimitingDb` = 4.0 dB.

The test checks both exactly, after every slice of a rising 60 Hz tone:
0 mismatches.

### 9.6 Spectrum

`dsp::SpectrumTap outputSpectrum` taps the final output, (L + R) / 2. It is
inactive, at no cost, until the editor calls `setActive (true)` (addendum 12).

### 9.7 Effective release and LEVEL MATCH gain

`double getEffectiveReleaseMs()`:

- **MANUAL:** the knob.
- **AUTO:** fast^(1−φ) × slow^φ, using the mode's AUTO fast and slow release
  times. φ is the share of the GR held by the slow branch, weighted by the GR
  itself and averaged over ~0.5 s. It holds while nothing is limited.
- Sustained limiting gives φ → autoSlowDepth. For STEEL this is 70^0.25 ×
  900^0.75 = 477 ms, measured 477.1 ms. Isolated peaks read lower (266 ms
  measured).

`float getLevelMatchGainDb()` is the attenuation LEVEL MATCH applies now
(≤ 0 dB; 0 when off).

### 9.8 Test hooks

- `getEngineForTesting()`
- `setGainCaptureForTesting (std::vector<float>*)`: the caller reserves the
  vector, so nothing is allocated while it has room.
- `currentSlotConfig()`

## 10. Parameters

IDs are immutable. Every parameter is at version 1. Texts are what
`getText` returns; the skin prints them verbatim.

| ID | Range / law | Default | getText examples |
|---|---|---|---|
| `input` | −12 … +24 dB, linear, 0.1 dB steps | 0.0 | "+6.4 dB", "0.0 dB", "-3.5 dB" |
| `ceiling` | −12 … 0 dB, linear, 0.1 dB steps | −1.0 | "-1.0 dBTP" (TRUE PEAK on), "-1.0 dBFS" (off), "0.0 dBTP" |
| `mode` | Clean / Punch / Dense / Steel | Steel | "Steel" |
| `releaseMode` | Auto / Manual | Auto | "Auto" |
| `release` | 20 … 1000 ms, skewed so the knob centre is exactly 250 ms (`setSkewForCentre (250)`) | 180 | "180 ms", "20 ms" (accepts "0.5 s", "1k") |
| `lookahead` | 0 … 10 ms, 0.1 ms steps | 3.0 | "3.0 ms", "0.0 ms" |
| `truePeak` | bool | on | "On" / "Off" |
| `oversampling` | 1x / 2x / 4x / 8x | 4x | "4x" |
| `stereoLink` | 0 … 100 %, 0.1 steps | 100 | "100 %", "63 %" |
| `softClip` | bool | off | "Off" |
| `softClipAmount` | 0 … 100 %, 0.1 steps | 30 | "28 %" |
| `subFilter` | Off / 20 / 25 / 30 (Hz) | Off | "Off", "25" |
| `delta` | bool | off | "Off" |
| `levelMatch` | bool | off | "Off" |
| `output` | −12 … +12 dB, piecewise linear with 0 dB at the knob centre, 0.1 dB steps | 0.0 | "-4.5 dB", "+2.0 dB" |
| `bypass` | bool (host bypass parameter) | off | "Off" |

Every control acts on the audio, each in its specified direction. A test
checks each one.

## 11. Changes without clicks

| Control | Method |
|---|---|
| INPUT, OUTPUT, CEILING | dB-linear 20 ms ramp + 3 ms corner smoothing |
| LEVEL MATCH gain | dB-linear 100 ms ramp + 3 ms corner smoothing |
| STEREO LINK, SOFT CLIP AMOUNT | 20 ms linear |
| Bypass, DELTA, SOFT CLIP, TRUE PEAK | 20 ms linear crossfades |
| SUB FILTER | 50 ms on/off fade; 15 ms log-domain glide between 20 / 25 / 30 |
| RELEASE (both modes) | Acts on the next sample's coefficient. The envelope is continuous; nothing is reset. |
| MODE, OVERSAMPLING, LOOKAHEAD | A second slot is configured. It inherits the active slot's envelope, warms up (latency + 2 × OS latency + 64 samples), then crossfades over 30 ms (raised cosine). The dry, u, INPUT-gain and telemetry references are crossfaded with the same weights at their own delays. |

The new latency is reported (`setLatencySamples`) in the block in which the
change arrives. The host is notified once per change; this is tested with an
`AudioProcessorListener`.

Measured (`STEEL_switching.csv`): every control, including jumps of ±12 dB
OUTPUT, −12 ↔ +12 dB INPUT, 0 ↔ −12 dB CEILING and 1x ↔ 8x, stays within
2× the steady-state 8 kHz-band level + −74 dBFS.

Rapid automation of every parameter every block for 10 s: finite, sample
peak ≤ 0 dBFS.

## 12. Latency

Latency (host samples) = OS (section 5.2) + round(LOOKAHEAD × fs) + ceiling
stage (section 6.3). The test measures the impulse delay for every
OVERSAMPLING × LOOKAHEAD {0, 0.1, 1, 3, 5.5, 10 ms} at 44.1, 48, 96 and
192 kHz (`STEEL_latency.csv`). It equals the reported value in every case.

At 48 kHz:

| OVERSAMPLING | LOOKAHEAD 0 | LOOKAHEAD 3 ms (default) | LOOKAHEAD 10 ms |
|---|---|---|---|
| 1x | 161 | 305 | 641 |
| 2x | 224 | 368 | 704 |
| 4x | 233 | 377 (7.85 ms) | 713 |
| 8x | 236 | 380 | 716 |

## 13. Verification

| Test (`STEEL: …`) | Result |
|---|---|
| registry: IDs, ranges, defaults, texts, buses | pass |
| oversampling / TP filters, exact latencies | stage-1 ripple < 0.001 dB, stopband < −85 dB; 0 / 63 / 72 / 75 |
| reported latency = measured impulse delay (96 combinations) and follows changes | exact |
| CEILING at 48 kHz (140 configurations) | TP ≤ +0.029 dB (programme, vectors), ≤ +0.071 dB (noise, squares); SP ≤ −2.9e-7 dB |
| CEILING at every rate, blocks 1–2048, mono, CEILING −12 to 0 | within policy |
| reference validated | sines 1e-5 dB; FFT-ideal −0.017 dB |
| TP meters (input, output) on sines and isolated band-limited pulses, 44.1–192 kHz | sines ≤ 0.0011 dB, pulses ≤ 0.0005 dB; programme ≤ 0.025 dB |
| K-weighting | coefficients to 2e-7; other rates ≤ 0.040 dB |
| Tech 3341 | within ±0.1 LU (worst 0.042) |
| Tech 3342 and continuous LRA | 10.000 / 5.000 / 20.000 / 15.000 LU; level ramp 23.95 = reference 23.95 LU |
| GR telemetry and history | 1.5e-7 relative; slices < 3e-6 dB |
| MODE distinctness | section 4.4 |
| AUTO: no pumping, programme dependency | section 4.3 |
| MANUAL release | ≤ 4.4 % (20 ms: +0.9 ms) |
| STEREO LINK | section 4.5 |
| SOFT CLIP: amount, mode scale, aliasing | section 4.6 |
| SUB FILTER; Off bit-exact | section 3.2 |
| DELTA | 6e-8 |
| LEVEL MATCH | ≤ 0.02 LU after matching; never boosts |
| warnings | exact, 0 mismatches |
| click-free switching of every control, rapid automation | pass |
| NaN / Inf / ±3e38 / denormal input | NaN and Inf act exactly as zeros (bit-identical); finite; ceiling holds |
| all rates × blocks {1 … 2048} × mono / stereo with switches | 0 allocations, finite, latency as reported |
| Bypass | bit-exact delayed dry; 20 ms crossfade to 1.5e-7 |
| state round trip, foreign and corrupt states | pass |
| factory presets (16) | every MODE; TP presets ≤ CEILING + 0.002 dB |
| CPU per instance (median block, block 128, stereo, everything on) | table below |
| telemetry is real (meter points, spectrum, reset, latency notification) | pass |
| every control changes the output, each in its direction | pass |

**CPU**, as % of one core (`STEEL_cpu.csv`; RelWithDebInfo, a shared cloud
core, noisy):

| | 1x | 2x | 4x | 8x |
|---|---|---|---|---|
| 44.1 kHz | 5.4 | 5.9 | 6.8 | 8.6 |
| 48 kHz | 5.9 | 6.3 | 8.0 | 9.6 |
| 96 kHz | 8.2 | 8.6 | 10.4 | 14.3 |
| 192 kHz | 10.7 | 12.8 | 16.5 | 24.8 |

The six true-peak estimators (input, output and ceiling stage, × 2 channels)
are about half of the cost.

Outputs written to `test-output/`:

- CSV: `STEEL_latency.csv`, `STEEL_ceiling_48k.csv`,
  `STEEL_ceiling_rates.csv`, `STEEL_truepeak_meter.csv`,
  `STEEL_kweighting.csv`, `STEEL_tech3341.csv`, `STEEL_tech3342.csv`,
  `STEEL_modes.csv`, `STEEL_auto_release.csv`, `STEEL_manual_release.csv`,
  `STEEL_softclip_aliasing.csv`, `STEEL_subfilter.csv`,
  `STEEL_switching.csv`, `STEEL_presets.csv`, `STEEL_cpu.csv`.
- WAV: `STEEL_programme_*.wav`.
- The JSON report.

## 14. Mutation checks

Each defect was introduced into the code, the tests were rebuilt and run,
and the code was restored.

| # | Defect introduced | Caught by (`STEEL: …`) | Failure |
|---|---|---|---|
| M1 | Ceiling stage disabled (r = 1) | CEILING holds at 44.1–192 kHz … | 1x music −0.04 dBTP for a −1 dB ceiling |
| M2 | Reported latency +1 sample | reported latency equals the measured impulse delay … | reported 160, measured 159 |
| M3 | GR telemetry frozen at 0 dB | gain-reduction telemetry … | output / input ≠ reported gain |
| M4 | K-weighting shelf gain 3.9 dB instead of 3.9998 | K-weighting = the BS.1770-4 coefficients … | b0 1.5187 vs 1.5351 |
| M5 | Relative gate −20 LU instead of −10 | loudness meter passes EBU Tech 3341 … | case 3: I = −24.16 |
| M6 | LRA upper percentile 90 instead of 95 | loudness range … continuous level ramp | 22.55 vs 23.95 LU |
| M7 | DELTA sign inverted | DELTA outputs exactly what … was removed | error ≫ 1e-6 |
| M8 | SUB FILTER second section Q 0.54 instead of 1.31 | SUB FILTER … | −27.1 dB an octave below 20 Hz |
| M9 | MANUAL release ignores the knob (fixed 180 ms) | MANUAL release time constant … | 20 ms set: 180.6 ms measured |
| M10 | STEREO LINK ignored in the limiter | STEREO LINK … | GR right 0 dB at 100 % |
| M11 | Dry path misaligned by one sample | host Bypass is bit-exact delayed dry … | not bit-exact |
| M12 | True-peak parabolic refinement removed | true-peak meters … | isolated pulse under-read by 0.036 dB |
| M13 | Soft knee curve doubled | the four MODEs are measurably distinct … | DENSE knee −0.083 vs −0.042 dB |
| M14 | AUTO slow branch disabled | AUTO release does not pump … | AUTO ripple like a fast release |
| M15 | A heap allocation in processBlock | all sample rates and block sizes … | 13230 allocations |
| M16 | Input not sanitised | NaN / Inf / absurd input … | non-finite output |
| M17 | HEAVY LIMITING threshold 3 dB instead of 4 | warnings … | flag ≠ definition |
| M18 | LEVEL MATCH may boost | LEVEL MATCH … never boosts | output differs from LEVEL MATCH off |
| M19 | IN meters measured after SUB FILTER | telemetry is real … | IN peak 0.031 instead of 0.499 |
| M20 | Linear slot crossfade instead of raised cosine | switching … is click-free | LOOKAHEAD 3 ↔ 1.3: 2.09e-4 > 2.03e-4 |

All 20 were caught. Two earlier attempts at M6 and M12 were not: the
two-level Tech 3342 cases cannot tell a 90th percentile from a 95th, and a
long sine always has some cycle whose peak lands near an oversampled point.
The continuous-ramp LRA test and the isolated-pulse true-peak test were
added for those two, and they now catch both.

## 15. Factory presets

16 presets. Every MODE is used; both release modes; 1x, 2x, 4x and 8x.
LEVEL MATCH, DELTA and Bypass are off in all of them.

| Preset | MODE | INPUT | CEILING | RELEASE | LOOKAHEAD | OS | LINK | SOFT CLIP | SUB | OUTPUT |
|---|---|---|---|---|---|---|---|---|---|---|
| Bass Heavy Master | CLEAN | +5.0 | −1.0 dBTP | MANUAL 500 ms | 8.0 ms | 4x | 100 % | off | 25 | 0.0 |
| Broadcast -2 dBTP | CLEAN | +3.0 | −2.0 dBTP | AUTO | 5.0 ms | 4x | 100 % | off | 20 | 0.0 |
| Dense EDM | DENSE | +11.0 | −0.8 dBTP | AUTO | 2.5 ms | 8x | 100 % | 70 % | 30 | 0.0 |
| Dense Loud | DENSE | +9.0 | −1.0 dBTP | AUTO | 3.0 ms | 4x | 100 % | 50 % | 25 | 0.0 |
| Gentle Vocal Peak Catch | CLEAN | 0.0 | −3.0 dBFS | MANUAL 80 ms | 1.5 ms | 2x | 100 % | off | Off | 0.0 |
| Hot CD Master | DENSE | +8.0 | −0.3 dBTP | AUTO | 3.0 ms | 8x | 100 % | 35 % | 20 | 0.0 |
| Live Low Latency | STEEL | +3.0 | −1.0 dBFS | MANUAL 150 ms | 0.5 ms | 1x | 100 % | off | Off | 0.0 |
| Loud Club | DENSE | +10.0 | −0.3 dBTP | MANUAL 90 ms | 2.0 ms | 4x | 100 % | 60 % | 30 | 0.0 |
| Punch Master | PUNCH | +5.0 | −1.0 dBTP | AUTO | 2.0 ms | 4x | 100 % | off | Off | 0.0 |
| Punchy Drums Bus | PUNCH | +6.0 | −0.5 dBFS | MANUAL 60 ms | 1.0 ms | 2x | 80 % | 40 % | Off | 0.0 |
| Safe Streaming Ceiling | STEEL | +3.0 | −1.0 dBTP | AUTO | 3.0 ms | 4x | 100 % | off | Off | 0.0 |
| Steel Master | STEEL | +6.0 | −1.0 dBTP | AUTO | 3.0 ms | 4x | 100 % | 25 % | 20 | 0.0 |
| Streaming -14 Gentle | STEEL | +4.0 | −1.0 dBTP | AUTO | 4.0 ms | 8x | 100 % | off | 20 | 0.0 |
| Transparent Master | CLEAN | +2.0 | −1.0 dBTP | AUTO | 5.0 ms | 4x | 100 % | off | Off | 0.0 |
| Vinyl Pre-Master | CLEAN | +1.0 | −0.5 dBFS | MANUAL 400 ms | 6.0 ms | 4x | 100 % | off | 30 | 0.0 |
| Wide Mix Independent | STEEL | +4.0 | −1.0 dBTP | AUTO | 3.0 ms | 4x | 50 % | off | Off | 0.0 |

The dBFS presets are sample-peak ceilings by design (bus and live use, and
vinyl cutting, where inter-sample peaks do not matter). The presets test
renders every preset on programme and checks the ceiling of the TRUE PEAK
presets (`STEEL_presets.csv`).

## 16. Not modelled / limitations

- **True peak at fs/2.** Content at or near fs/2 (full-band noise, naive
  squares) can read up to 0.1 dB over the ceiling on a reference that is
  −6 dB at fs/2. An ideal reconstruction would read more for such content
  (RESEARCH F1). Real masters do not contain it.
- **OUTPUT above 0 dB.** The ceiling guarantee holds for OUTPUT ≤ 0 dB
  (decision D3).
- **LOOKAHEAD 0** is not zero latency: the ceiling stage needs 159–239 samples
  (3.3–3.6 ms at 44.1/48 kHz; decision D1).
- **Channels.** No multichannel (5.1) loudness; STEEL is mono or stereo.
- **Double-precision host processing** is not implemented (master 6.5: float is
  the baseline).

## 17. Owner decisions

These are open questions in the spec, decided as a mastering-tool designer would:

Status (2026-10-14): taken as decided under Andrew's standing instruction to settle open questions without asking (closest to hardware practice); he reviews the finished plugin by ear. The editor dims RELEASE while AUTO runs (D8).

- **D1 — LOOKAHEAD 0 still has latency.**
  - The true-peak stage needs its estimator's delay plus its hold and ramp:
    161 samples at 48 kHz.
  - A zero-latency true-peak limiter cannot guarantee a true-peak ceiling.
  - LOOKAHEAD 0 removes only the musical lookahead.
- **D2 — Where the meters measure.**
  - **IN** is after INPUT and before SUB FILTER: what the user drives into the
    limiter.
  - **OUT** is the final output, including OUTPUT, LEVEL MATCH, DELTA and
    Bypass: what leaves the plugin.
  - The input true peak is always measured. It is cheap enough, and a
    reopened editor then shows the truth at once.
- **D3 — OUTPUT is a post-ceiling trim.**
  - Common in mastering limiters: it trims the output for following gear or a
    format without re-limiting.
  - The ceiling holds for OUTPUT ≤ 0 dB.
  - With OUTPUT > 0 dB, TP OVERS reports the result honestly.
- **D4 — LEVEL MATCH affects the plugin output while it is on.** A plugin
  cannot tell monitoring from rendering (spec review, item 8).
  - It is saved with the state like every parameter ("save state", master
    13.11).
  - All factory presets have it off.
  - It only ever attenuates (−30 to 0 dB). So it cannot raise the exported
    level above the ceiling: "must not alter rendered/exported target ceiling
    unpredictably".
- **D5 — Loudness readings measure the final output.** With DELTA, LEVEL MATCH
  or Bypass on, the readings show what is heard. The loudness meter is reset
  by `resetLoudness()` (the editor's reset button).
- **D6 — Warning definitions** (master 13.11 leaves the numbers open).
  - TP OVERS: CEILING + 0.1 dB, held 2 s. 0.1 dB sits above STEEL's own worst
    case (+0.07 dB on pathological signals), so the warning means a real over
    (TRUE PEAK off, or OUTPUT > 0).
  - HEAVY LIMITING: a mean GR of 4 dB over 2 s, a common threshold beyond
    which mastering limiters audibly flatten transients.
- **D7 — TRUE PEAK off keeps the latency.** The sample-peak path is delayed by
  the estimator's latency, so toggling TRUE PEAK is a click-free crossfade
  with no latency change and no host re-sync.
- **D8 — RELEASE in AUTO.** The knob is not used in AUTO (bit-exact test).
  The editor should dim it there. `getEffectiveReleaseMs()` gives the AUTO
  value for a readout.
- **D9 — Mono loudness.** A mono bus is one channel with G = 1. This is
  BS.1770 applied literally: a −23 dBFS mono tone reads −26 LUFS.
- **D10 — The soft clipper is not part of GR.** GR shows the gain the
  limiter and the ceiling stage apply. The clipper reshapes waveforms and
  has no "gain".
- **D11 — Gain smoothing has rounded corners.** INPUT, OUTPUT, CEILING and
  LEVEL MATCH use a dB-linear ramp followed by a 3 ms one-pole. A plain linear
  ramp's corners produced −52 dBFS clicks at full scale; the test showed this.
- **D12 — Slot crossfade is a raised cosine** (30 ms). A linear crossfade's
  corners measured just above the click limit.

## 18. Open items

- Validate in hosts (master §23): latency compensation when LOOKAHEAD or
  OVERSAMPLING is automated during playback, and the bypass parameter
  mapping.
- A dedicated low-CPU mode is not provided. If it is ever needed, an
  editor-closed switch for the input true-peak estimator would save about
  1/6 of the true-peak cost (D2 keeps it always on).
