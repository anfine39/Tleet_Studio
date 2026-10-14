# VOID — model specification

`TLEET // VOID` is an **original algorithmic stereo reverb**. It is not a
hardware clone (master spec 13.9). This file turns the research in
`RESEARCH.md` into the DSP that runs. Each value or behaviour carries one of
four provenance tags:

| Tag | Meaning in VOID |
|---|---|
| `SOURCE_CONFIRMED` | Taken from a published source cited in RESEARCH.md (S1–S18). The only numeric example is Dattorro's plate tank. |
| `SCHEMATIC_OR_MEASURED` | A published structure (a topology, not numbers), or a value measured by VOID's own tests. |
| `APPROXIMATION` | A numerical method or design choice that stands in for an ideal. |
| `TLEET_EXTENSION` | A TLEET design decision. The owner decisions are listed in section 16. |

Code: `plugins/VOID/dsp/` (`Primitives.h`, `Filters.h`, `Networks.h/.cpp`,
`VoidEngine.h/.cpp`), `plugins/VOID/Parameters.h` and
`plugins/VOID/PluginProcessor.h/.cpp`. Tests: `tests/VoidTests.cpp`
(24 tests, all named `VOID: …`).

---

## 1. Scope

| Item | Value | Tag |
|---|---|---|
| Buses | mono→mono, stereo→stereo, mono→stereo (stereo→mono and surround are rejected) | `TLEET_EXTENSION` (master spec 0.9) |
| Latency | 0 samples. The dry path is the plugin input itself. | `TLEET_EXTENSION` |
| Precision | Networks run in double precision. Delay memory is stored as float. | `APPROXIMATION` |
| Sample rates | Any. Tested at 44.1, 48, 96 and 192 kHz. Every delay is defined in ms. | — |
| Tail length reported to the host | 62 s | `TLEET_EXTENSION` |
| Oversampling | None (section 8.3) | `TLEET_EXTENSION` |

## 2. Signal flow

```text
in ─► sanitise (NaN/Inf/denormal → 0) ─┬──────────────────────────────── dry ─────────────────────┐
                                       │                                                          │
                                       ▼                                                          │
  [PRE HPF+LPF] ─► DARK in (4th-order LP 8 kHz) ─► RUIN drive (ADAA soft sat) ─► DC block 5 Hz      │
       ─► PRE-DELAY (2-tap crossfade) ─► × FREEZE input gate                                      │
       ─► algorithm slot A / B (crossfaded on ALGORITHM)                                          │
            early reflections / diffusion ─► late network                                         │
            (Jot absorption, LOOP HPF+LPF, modulation, RUIN smear + in-loop saturation, bound)    │
       ─► early + late ─► [POST HPF+LPF] ─► DARK out (2nd-order LP 10 kHz)                         │
       ─► WIDTH (M/S side gain) ─► × DUCK gain ─► wet ─► wetSpectrum tap                          │
                                                   │                                              │
                                                   └──► MIX (linear) ◄────────────────────────────┘
                                                          ─► OUTPUT ─► host Bypass (20 ms crossfade to dry) ─► out
DUCK detector: the peak of the sanitised dry input.
```

This is the order that master spec 13.9 asks for: pre-delay → early
reflections → diffusion → late network → loop filtering → modulation →
colour → width. COLOR acts at three points: input (DARK band limit, RUIN
drive), loop (HF decay, RUIN saturation, smear and modulation instability)
and output (DARK band limit).

## 3. Real-time structure

| Item | Value | Tag |
|---|---|---|
| Control block | K = 32 samples. Controls, glides and coefficients update once per block and move linearly inside it. | `APPROXIMATION` |
| SIZE glide | two-stage exponential, 0.1 s per stage | `TLEET_EXTENSION` |
| DECAY glide (loss rate λ = 1/RT60) | two-stage, 40 ms | `TLEET_EXTENSION` |
| DIFFUSION, MOD DEPTH, WIDTH, COLOR weights, filter routing weights | two-stage, 20 ms | `TLEET_EXTENSION` |
| HPF, LPF, MOD RATE | one-stage glide in the log domain, 15 ms. The filter coefficients themselves move per sample (6.1). | `TLEET_EXTENSION` |
| FREEZE gate, slot input gates | two-stage, 15 ms | `TLEET_EXTENSION` |
| OUTPUT | 30 ms dB-linear ramp plus a 2 ms rounding one-pole | `TLEET_EXTENSION` |
| MIX | 30 ms linear ramp plus a 2 ms rounding one-pole | `TLEET_EXTENSION` |
| Bypass | 20 ms crossfade; fully bypassed output is bit-exact dry | `TLEET_EXTENSION` |
| Memory | Two slots, each with one arena sized for the largest algorithm at SIZE 100 % with full modulation excursion. Allocated in `prepareToPlay`; the audio thread only carves from it. 1.89 MB at 44.1 kHz, 2.05 MB at 48 kHz, 4.11 MB at 96 kHz, 8.21 MB at 192 kHz. | `SCHEMATIC_OR_MEASURED` |
| Allocation on the audio thread | none, including during ALGORITHM switches (test "all sample rates…") | — |

## 4. The five networks

Each ALGORITHM is its own topology. They are not parameter sets on one
network (13.9). Every loop element is lossless except the Jot absorption
filter of each line, half or loop (section 5). All five share the same
building blocks:

- Schroeder allpasses (S1);
- the allpass-interpolated moving read (S10, S11);
- two-band absorption (S3, S4);
- tone correction (S3);
- the loop bound and FREEZE hold (6.4, 6.5).

### 4.1 SIZE law (`TLEET_EXTENSION`)

The network scale is s = s0 · 2^(k·(SIZE − 50)/50). It multiplies every loop
delay, the early-reflection times and, for Void only, the bloom diffusers.

| Algorithm | s0 | k | Scale range (SIZE 0 → 100) |
|---|---|---|---|
| Room | 1.0 | 1.0 | 0.5 – 2.0 |
| Plate | 0.7 | 0.8 | 0.40 – 1.22 (of Dattorro's figures) |
| Chamber | 1.0 | 0.7 | 0.62 – 1.62 |
| Hall | 1.0 | 1.0 | 0.5 – 2.0 |
| Void | 1.0 | 1.1 | 0.47 – 2.14 |

### 4.2 Early reflections (image source, S8): Room, Chamber, Hall

The early reflections come from a shoebox image-source model (Allen & Berkley).
Images up to the given order are generated; the strongest N are kept and
sorted by time. Each tap's delay is the extra path re the direct sound / c
(c = 343 m/s, minimum 0.2 ms). Its gain is β^order · d0/d. It is panned by the
lateral direction cosine (equal power). The summed power per channel is
normalised to 1. The tables are computed once. Taps are read with cubic
Lagrange interpolation at t·scale.

| | Room | Chamber | Hall |
|---|---|---|---|
| Room (m) | 6.2 × 4.6 × 3.0 | 4.4 × 3.6 × 2.9 | 30 × 19 × 12 |
| Source / receiver (m) | (2.0, 1.4, 1.5) / (4.3, 3.1, 1.6) | (1.1, 1.0, 1.3) / (3.2, 2.5, 1.5) | (7.5, 10.5, 1.8) / (19, 8, 1.7) |
| β, max order, taps | 0.82, 2, 24 | 0.90, 3, 32 | 0.80, 2, 16 |
| ER gain | 1.00 | 0.70 | 0.50 |
| Late network fed from the ER line at | 5 ms·scale | 3 ms·scale | 15 ms·scale |
| Smear | — | 3 allpasses per channel, 1.9/3.7/6.1 ms (L) and 2.3/4.3/6.9 ms (R), g = 0.85·DIFFUSION | — |

All values are `TLEET_EXTENSION`; the method is `SCHEMATIC_OR_MEASURED` (S8).

### 4.3 ROOM: early-reflection dominant (`TLEET_EXTENSION` constants, S3 structure)

- 24-tap ER bank (4.2).
- Late network: 2 input diffusers per channel (3.1/4.3 ms L, 3.4/4.7 ms R; g = 0.6·DIFFUSION), feeding an 8-line FDN.
- Feedback matrix: Householder.
- Line lengths: 21.1, 24.7, 28.3, 32.9, 36.1, 40.3, 44.9 and 49.7 ms (× scale).
- All lines modulated; excursion 0.12 ms at MOD DEPTH 100 %.
- Late gain 0.62.
- Decay shape: RT60(LF)/RT60(1 kHz) = 1.00 below 250 Hz; RT60(HF)/RT60(1 kHz) = 0.42 above 4.5 kHz.
- Measured: energy peak at 8.5 ms, centre time 28 ms, NED 0.23 at 50 ms.

### 4.4 PLATE: Dattorro figure-eight tank (S5)

- Topology and figures: `SOURCE_CONFIRMED` (Dattorro 1997, samples at 29 761 Hz, converted to ms and × scale).
- Input: mono (L+R)/2 through 4 diffusers, 142/107/379/277 samples, g = 0.75/0.75/0.625/0.625 × DIFFUSION.
- Tank halves:
  - modulated allpass, 672 / 908 samples, g = −0.70·DIFFUSION;
  - delay, 4453 / 4217;
  - Jot absorption, then the LOOP filter, then RUIN saturation, then the bound gain;
  - allpass, 1800 / 2656, g = 0.50·DIFFUSION;
  - delay, 3720 / 3163.
  - Each half feeds the other.
- Output: the 7 taps per side of Dattorro's Table 2, scaled with the tank.
- Early output: the diffused input after stages 3 and 4, × 0.35. Late gain 0.6.
- Differences from Dattorro (`TLEET_EXTENSION`):
  - Jot absorption replaces the one-pole damping plus decay gain, so DECAY is calibrated (section 5);
  - the modulation is sine plus random wander (6.6) instead of a pure LFO;
  - SIZE scales the tank.
- Decay shape: LF 1.00 below 200 Hz; HF 0.65 above 5 kHz (bright).
- Measured: energy peak at 97 ms, centre time 129 ms.

### 4.5 CHAMBER: Gardner nested-allpass loops (S6 structure, `TLEET_EXTENSION` constants)

- Two loops (L, R). Each is a cascade:
  - nested allpass A (outer 19.7 ms, g 0.45; inner 5.3 ms, g 0.6);
  - delay T1 (7.1 ms);
  - nested allpass B (outer 29.3 ms, g 0.4; inners 7.7 and 3.1 ms, g 0.5 and 0.6);
  - delay T2 (31.1 ms, modulated, excursion 0.35 ms);
  - nested allpass C (outer 11.9 ms, g 0.45; inner 4.1 ms, g 0.5);
  - delay T3 (23.3 ms).
  - Those are the left loop's values. The right loop: 21.3, 5.9, 6.3, 27.7, 8.3, 3.4, 34.3, 13.1, 4.6 and 21.1 ms.
  - All g are × DIFFUSION.
- The loop end passes the Jot absorption, the LOOP filter and RUIN saturation. The two loop ends are then cross-coupled by a rotation of 0.6 rad (orthogonal, so lossless).
- Output: yL = 0.5·T1 + 0.6·T2 − 0.45·T3 of the other loop (likewise for R), × 0.5.
- Fed by the 32-tap, third-order ER bank through its allpass smear (4.2).
- Decay shape: LF 1.10 below 300 Hz; HF 0.50 above 4.5 kHz.
- Measured: energy peak at 18 ms, centre time 52 ms, NED 0.59 at 50 ms (the densest early field), mixing time 11 ms.

### 4.6 HALL: 16-line Hadamard FDN (S3, S12)

- Sparse 16-tap ER bank (4.2), then 4 input diffusers per channel (Dattorro's input figures at 48 kHz scale: 4.771/3.595/12.735/9.307 ms L, +8 % R; g = 0.75/0.75/0.625/0.625 × DIFFUSION).
- 16-line FDN with a Hadamard matrix (fast Walsh–Hadamard transform).
- Line lengths: 43.1 … 139.3 ms (16 values, mutually non-commensurate).
- Odd lines modulated; excursion 0.45 ms.
- Input: L feeds the even lines and R the odd lines, with Hadamard sign rows. The outputs are two orthogonal Hadamard rows.
- Late gain 1.85.
- Decay shape: LF 1.25 below 300 Hz; HF 0.50 above 3.5 kHz (longer bass, darker top).
- Measured: energy peak at 34 ms, centre time 100 ms.

### 4.7 VOID: bloom + allpass-FDN (`TLEET_EXTENSION`)

- "Bloom": two 6-stage allpass chains (8.9 … 97.7 ms L, 9.6 … 104.6 ms R; g = 0.72·DIFFUSION). They scale with SIZE, so the build-up is slow.
- The bloom output × 0.5 is the early component.
- Late network: 16-line FDN with a Householder matrix.
  - Every line is modulated; excursion 1.6 ms.
  - Every line contains its own Schroeder allpass (4.1–16.1 ms, g = 0.6·DIFFUSION).
  - Line lengths: 61.3 … 203.9 ms.
- Late gain 4.5.
- Decay shape: LF 1.15 below 200 Hz; HF 0.72 above 3 kHz.
- Measured: energy peak at 310 ms, centre time 343 ms, mixing time 568 ms. That is the "huge, slowly building" signature.

## 5. Decay calibration

DECAY is the RT60 at 1 kHz. Its loss rate is λ = 1/DECAY (λ = 0 for ∞ and
FREEZE). The gain for D samples spent in the late path is
γ^D = 10^(−3·λ·D/fs).

### 5.1 Jot substitution in every allpass (`SCHEMATIC_OR_MEASURED`, S4 / S18; RESEARCH F1)

γ^D is applied to every delay of the late path:

- the loop delays (through the absorption gain);
- inside every allpass: the allpass delay output is × γ^d;
- the Plate's and Void's diffusers;
- the Chamber's nested allpasses.

The late response is then the lossless network's response × γⁿ, whatever the
allpasses do. The decay rate at 1 kHz equals DECAY for any topology and SIZE.

### 5.2 Two-band absorption (`APPROXIMATION` of S4)

Per element, H(z) = gain · LS(z) · HS(z):

- LS is a one-pole low shelf at loHz with DC gain A.
- HS is a high shelf at hiHz with Nyquist gain B.
- A and B are chosen so that RT60(DC)/RT60(1 kHz) = loRatio and RT60(Nyquist)/RT60(1 kHz) = hiRatio, over the element's whole loop length.
- `gain` makes |H(1 kHz)| exactly γ^Lmid. Lmid is the element's pure delay; its allpasses carry their own γ (5.1).
- Passive clamp: the peak gain is never above 1.
- DECAY, COLOR and SIZE changes ramp `gain` and both shelf amounts linearly over the control block, so no filter steps.
- The cutoffs are fixed per algorithm. DARK multiplies hiRatio by (1 − 0.55·dark) and hiHz by (1 − 0.45·dark). RUIN multiplies them by (1 − 0.3·ruin) and (1 − 0.2·ruin).

### 5.3 Tone correction (`SCHEMATIC_OR_MEASURED`, S3)

There is a fixed shelf pair per output channel with |T|² ∝ RT60(1 kHz)/RT60(f),
using the algorithm's base decay shape. The late field's initial spectrum is
flat, so a longer-decaying band is not also louder.

### 5.4 Short decays (`TLEET_EXTENSION`, owner decision D2; RESEARCH F2)

Below about 0.8 s the onset is a large part of the response, and T30 measures
the onset as much as the decay. Acceptance is therefore:

- T30 within 5 % for DECAY 0.8–12 s;
- below that, the energy decay curve (EDC) reaches −60 dB within 1.6·DECAY + 0.35 s, and a shorter DECAY gives a shorter response.

Measured EDC −60 dB times at DECAY 0.1 s: Room 0.080 s, Plate 0.170 s, Chamber
0.142 s, Hall 0.096 s, Void 0.131 s.

### 5.5 Measured accuracy (`SCHEMATIC_OR_MEASURED`, `VOID_rt60.csv`)

- T30 in the 1 kHz octave, Schroeder EDC (S17).
- Grid: 5 algorithms × DECAY 0.8/1.5/4/12 s × 44.1/48/96/192 kHz.
- **Worst error 4.04 %**; limit 5 %.

Band RT60 ratios at 48 kHz, DECAY 4 s:

| | 250 Hz / 1 kHz | 4 kHz / 1 kHz |
|---|---|---|
| Room | 1.06 | 0.68 |
| Plate | 1.02 | 0.86 |
| Chamber | 1.10 | 0.75 |
| Hall | 1.21 | 0.69 |
| Void | 1.08 | 0.83 |

## 6. Shared stages

### 6.1 HPF / LPF (`APPROXIMATION`: Zavalishin TPT SVF, S15; owner decision D5)

**Filter design.**

- 12 dB/oct Butterworth TPT state-variable filters: HPF 20–1000 Hz, LPF 1–20 kHz (log).
- `filterResponseDb` evaluates the exact discrete transfer function of the same structure from the same coefficients.

**Open end stops (RESEARCH F4).**

- HPF at 20 Hz has g = 0: exactly the identity.
- LPF at 20 kHz has g × 1000: below 1e-5 dB everywhere.
- Over the last third of an octave before each stop (25 → 20 Hz, 16 → 20 kHz), g moves continuously, by smoothstep in log frequency. There is no step on the knob.
- Without the open stops, a "fully open" LOOP filter is lossy every pass. ∞ DECAY would then decay, and every tail would darken by about 8 dB/s in the top octave.

**Gliding.**

- The cutoffs glide in the log domain (15 ms).
- The integrator gain g moves linearly per sample inside each control block; `a = 1/(1+g(g+k))` is recomputed exactly per sample.
- This keeps a 20 kHz → 1 kHz LPF jump click-free. A linear `a` was unstable during the glide.

**Measured.** The worst error between measured sweeps and `filterResponseDb` over min/mid/max/musical settings, PRE and POST, is **0.0018 dB** (`VOID_filter_responses.csv`).

### 6.2 FILTER MODE (`TLEET_EXTENSION`, 13.9 "analogous to ECHO")

- **PRE** filters the network input once.
- **LOOP** (default) puts the HPF+LPF inside every line, half or loop, after the absorption. Out-of-band content then decays faster every pass. Measured, in the tail band re open: HPF 400 Hz gives −63 dB at 125 Hz; LPF 2 kHz gives −56 dB at 8 kHz.
- **POST** filters the wet output once. PRE and POST give the same response for a linear setting: 9.4e-8 re peak difference.
- The modes crossfade with 20 ms weights. Each stage restarts from zero state while its weight is 0, so there is no click.

### 6.3 DECAY at its maximum = ∞ (`TLEET_EXTENSION`, owner decision D3)

- DECAY 60 s (the top of the knob, text "∞") sets λ = 0. The loop is lossless (S12), and input keeps entering.
- A finite DECAY reaches at most 59.9 s, including when raised by DROWN.
- Measured without input: level change from 4 s to 22 s is ≤ 0.048 dB for all algorithms.

### 6.4 Loop energy bound (`TLEET_EXTENSION`, owner decision D3)

This is one slow gain on the whole loop, set from the RMS of the network's
**late output** per channel.

**Ceiling.**

- 1.0 (0 dBFS, a safety only) up to RT60 10 s.
- 0.25 (−12 dBFS) from RT60 30 s and for ∞/FREEZE.
- Log-interpolated in RT60 between.

**Dynamics.**

- Detector: 50 ms. Attack: 20 ms. Release: 0.4 s.
- Below the ceiling the gain is exactly 1: transparent, so the decay calibration is untouched.

**Safety guard.** Every loop write passes `loopGuard`: identity up to |x| = 2,
then a soft knee that never exceeds 4, and non-finite becomes 0. It is never
reached in the tests.

**Measured** (`VOID_infinite.csv`): ∞ DECAY with continuous −20 dBFS noise for
2 minutes.

- Level 50–120 s: −10.7 / −10.6 / −11.2 / −10.9 / −8.7 dBFS RMS.
- Bound gain: 0.96 / 0.86 / 0.94 / 0.90 / 0.69.
- Level change from 60 s to 120 s: ≤ 0.053 dB.
- Output peak: ≤ 1.92.

Maximum settings (`VOID_max_settings.csv`): every algorithm and colour, DECAY
59.9 s and ∞, 0 dBFS noise, SIZE / DIFFUSION / MOD 100 %. The output is finite
and bounded. The highest last-3-s peak is +0.98 dBFS (Void Clean ∞).

### 6.5 FREEZE (`TLEET_EXTENSION`, owner decision D4; 13.9)

**What FREEZE does.**

- The input gate closes in two 15 ms stages, so new input stops entering.
- λ → 0: the loop is lossless.
- RUIN's in-loop saturation is faded out (it would drain energy).
- The LOOP filters leave the loop and shape the frozen output as POST filters instead. A filter inside a lossless loop would keep removing energy every pass.
- The dry signal and the new input pass over the tail through MIX. With the gate shut, the frozen wet is bit-identical with or without new input.
- Release: the tail decays again at DECAY. Measured −24.2 dB/s at the 1 kHz octave for DECAY 2.5 s; expected −24.

**Level hold.** A lossless loop whose delays are modulated is not exactly
energy-preserving: its level random-walks (RESEARCH F3). After FREEZE has been
fully engaged for 5 s, the hold:

- measures the frozen level with a 1 s detector;
- keeps it with a proportional servo: rate = clamp(errorDb / 2 s, ±0.5 dB/s), applied per pass over the network's mean loop time.

**Measured** (`VOID_freeze.csv`), 2 minutes, 107 s of frozen windows:

- **Worst drift 0.065 dB** (Hall Dark MOD 100). Without the hold the worst is 0.17 dB (Plate), and it grows with time.
- No NaN.
- DC below 0.6 % of the RMS.

### 6.6 Modulation (`SCHEMATIC_OR_MEASURED` S13, `APPROXIMATION` S10/S11)

**Shape.**

- The modulated delays move by excursion × (sin(2π·rate·mul·t + phase) + wander).
- Rate multipliers are spread by the golden ratio (0.7–1.3); phases likewise.
- "Wander" is band-limited noise at 1.5 × rate with unit RMS.
- Amplitudes: sine 1.0 and wander 0.25 (CLEAN/DARK); sine 0.7 and wander 1.0 under RUIN ("instability").

**Pitch cap.** Excursion = excursion_max × MOD DEPTH, capped so that the pitch
deviation 2π·rate·E ≤ 3 %.

**Interpolation.**

- Reads use the first-order allpass interpolator with fraction in [0.5, 1.5). It is lossless, so the decay calibration holds while delays move.
- When the integer part changes, the state is re-derived by running the new filter over the 5 previous samples (S11 transient elimination).

**Measured.**

- The 8 kHz+ residue of a 220 Hz tone at MOD 100 %, 5 Hz is at worst −63.6 dB re wet RMS (Plate Ruin).
- At 0.5 Hz it is −80 to −96 dB.
- The limit is 2× the static setting + 1e-3.

### 6.7 PRE-DELAY (`TLEET_EXTENSION`)

- Range 0–250 ms. The knob centre is 60 ms (skewed).
- Target in samples = ms·fs/1000, exact for whole ms at whole sample rates.
- Read: below 1 sample, linear interpolation; otherwise cubic Lagrange.
- Changes crossfade between two taps with a 20 ms raised cosine. There is no glide, so no pitch sweep. A change during a fade starts the next fade when this one ends.
- Measured: integer delays shift the wet response sample for sample; the worst fractional error is 5.7e-6 samples (`VOID_predelay.csv`).

### 6.8 WIDTH, DUCK, MIX, OUTPUT (`TLEET_EXTENSION`)

- **WIDTH:** M/S on the wet signal, side × WIDTH/100 (0–200 %). 0 % is mono wet; 100 % is the network's native image.
  - Mono sum re stereo at 100 %: Room −1.2, Plate −3.1, Chamber −2.8, Hall −2.1, Void −3.0 dB.
  - At 200 %: −3.6 to −7.2 dB (`VOID_width.csv`).
  - A mono output is the mid of the wet signal.
- **DUCK:**
  - Detector: peak of the dry input, instant attack, 50 ms release.
  - Attenuation = DUCK% × 24 dB × clamp((env − (−45 dBFS)) / 25 dB, 0, 1), with 10 ms attack and 250 ms release.
  - Measured: −24.0 dB at 100 % and −12.0 dB at 50 %; −22 dB 20–30 ms after onset; recovery to −0.13 dB 1.4 s after the input stops.
- **MIX:** a linear crossfade. MIX 100 % is exactly the wet signal; MIX 0 % is bit-exact dry.
- **OUTPUT:** ±12 dB after MIX, centre-detented 0 dB, 0.1 dB steps.

### 6.9 Robustness

- The input is sanitised (NaN, Inf and denormals become 0) before the meters and the engine. The outputs are checked finite.
- Silence in gives exact silence out.
- A first-order 5 Hz DC blocker sits on the network input. DC at the wet output is below 1 % of the RMS over 107 s.

## 7. Changes without clicks

### 7.1 ALGORITHM (`TLEET_EXTENSION`, owner decision D6)

There are two slots. On a change:

- the new network is configured in the idle slot from silence, and its input gate opens over 15 ms;
- the old slot's input gate closes, and it rings out with a 1 s raised-cosine fade;
- the old slot's memory is then cleared incrementally (8192 floats per control block) and the slot returns to idle;
- a change that arrives while the other slot is still ringing out fades that tail in 30 ms first.

There is never a discontinuity, and memory is never allocated.

### 7.2 Every other control

| Control | Click-free mechanism |
|---|---|
| COLOR | Weight glides plus shelf-amount ramps |
| SIZE | Delay lengths glide (two stages of 0.1 s) |
| DECAY | λ glide plus ramped absorption gain and shelves |
| FREEZE | Gates |
| FILTER MODE | Crossfaded weights |
| HPF, LPF | Per-sample coefficient glide |

Measured with an 8 kHz high-pass click detector on a 220 Hz tone (−12 dBFS):

- Each control is toggled between extremes, twice. PRE-DELAY is toggled both 0 ↔ 250 ms and 0 ↔ 101.3 ms: 250 ms is a whole number of 220 Hz cycles.
- Limit: 2× the worse fixed setting + 2e-4 (−74 dBFS).
- **Worst: SIZE 0 ↔ 100 at 1.5e-4 (−76.5 dBFS)**, the Doppler of a full-range sweep. Every other control is below 1e-4 (`VOID_switching.csv`).
- Rapid random automation of every control stays finite.

## 8. COLOR

### 8.1 CLEAN

No colour stages run. Measured −3 dB bandwidth is at least 19.5 kHz.

### 8.2 DARK (`TLEET_EXTENSION`, "vintage-digital band limit")

- Input: 4th-order Butterworth LP at 8 kHz.
- Output: 2nd-order Butterworth LP at 10 kHz.
- In the loop: a darker HF decay (5.2).
- Weight glided, 20 ms.
- Measured −3 dB bandwidth 3.4–8.5 kHz, depending on the algorithm.

### 8.3 RUIN (`TLEET_EXTENSION`, `APPROXIMATION` for ADAA, S14)

**What RUIN does.**

- **Input saturation.** +12 dB drive into f(x) = x/√(1+x²), then back down 12 dB. The curve is odd, so only odd harmonics appear. It uses first-order ADAA in its exact closed form (x + x₁)/(√(1+x²) + √(1+x₁²)). The stage runs at all times, with its state kept current, and is mixed in by the RUIN weight.
- **In-loop saturation.** x/√(1 + (x/0.7)²) on every line or loop end, faded out while frozen.
- **Smear.** DIFFUSION + 0.2.
- **Modulation instability.** See 6.6.
- **Darker HF decay.** See 5.2.

**Measured** (`VOID_ruin_thd.csv`, `VOID_color.csv`).

- THD vs level, Hall: −40 dBFS 0.014 %, −30 dBFS 0.14 %, −20 dBFS 1.29 %, −12 dBFS 6.1 %, −6 dBFS 15.1 %, 0 dBFS 27.7 %. It rises monotonically.
- THD+N is within 0.07 % of THD.
- Even harmonics are below 1 % of odd harmonics at ≥ −12 dBFS (symmetric).
- The early component alone (Room, soloed, −6 dBFS): THD 69 % under RUIN against 1e-6 % under CLEAN. This proves that the input stage, not only the loop, saturates.
- Aliasing: a 4.44 kHz tone at −6 dBFS gives inharmonic energy of **−45.6 dB** re the fundamental. Without ADAA it is −37 dB.
- Small-signal levels of the three colours are matched within 1.5 dB.

**No oversampling.** This is a deliberately degraded colour, and −45.6 dB is
adequate. A ×2 stage would cost a half-band filter pair per channel.

## 9. DROWN macro (`TLEET_EXTENSION`, owner decision D7; 13.9)

The curve is c = smoothstep(DROWN/100) = 3d² − 2d³. The effective values are:

| Target | Effective value | At DROWN 100 |
|---|---|---|
| SIZE | base + (100 − base) · 0.6 · c | 60 % of the headroom |
| DECAY | base · 2^(2c), capped at 59.9 s; ∞ stays ∞ | × 4 |
| DIFFUSION | base + (100 − base) · 0.8 · c | 80 % of the headroom |
| MOD DEPTH | base + (100 − base) · 0.5 · c | 50 % of the headroom |
| WIDTH | base + (200 − base) · 0.25 · c | 25 % of the headroom |
| LPF | base · 2^(−0.75c), floored at 1 kHz | −0.75 octave |

- The parameters (base values) are never written, including during processing.
- DROWN 0 renders bit-identically to an instance that never had the macro.
- `getEffectiveSettings()` and the DSP use the same function, `applyDrown`.
- Measured: DROWN 50 on DECAY 1.5 s gives an effective 3 s, and the measured RT60 is within 5 % of it.
- `VOID_drown.csv` records the curve.

## 10. Telemetry (exactly as implemented)

All members are public on `tleet::voidrev::VoidProcessor`:

```cpp
std::array<dsp::MeterSource, 2> inputMeters, outputMeters; // [0] L, [1] R; mono bus: both carry the channel
float takeEarlyLevel() noexcept;   // RMS (linear) of the early component since the last call
float takeLateLevel() noexcept;    // RMS (linear) of the late component since the last call
dsp::SpectrumTap wetSpectrum;      // wet (after WIDTH and DUCK, before MIX/OUTPUT), (L+R)/2; setActive(true) to fill
bool isFrozen() const noexcept;    // FREEZE engaged (input gate closing/closed, loop lossless)
double filterResponseDb (double hz) const noexcept; // HPF+LPF magnitude as they run now (glided cutoffs, DROWN's LPF)
struct EffectiveValues { float size, decay, diffusion, modDepth, width, lpf; bool decayInfinite; };
EffectiveValues getEffectiveSettings() const noexcept; // after DROWN, in %, s, %, %, %, Hz
```

**Early and late levels.**

- These are the RMS over both channels of the sum of the two slots, at the network output.
- They are taken before the POST filter, DARK out and WIDTH, and are multiplied by the DUCK gain.
- Formula: sqrt(Σ 0.5·(xL² + xR²)·duck² / n).
- When no audio was processed since the last call, the previous value is returned.

**Effective settings.** In `getEffectiveSettings().decay`, +∞ means DECAY is at
its maximum (`decayInfinite` is then true). A finite effective decay is capped
at 59.9 s.

**Measured.**

- Early and late readings against the RMS of the soloed component: relative error ≤ 1.6e-7.
- Silence reads 0.
- The spectrum tap equals the wet signal.
- `filterResponseDb` follows the running filters.
- Base-class meters: `inputMeter` and `outputMeter` (`TleetProcessorBase`).

## 11. Parameters

IDs are version 1 and immutable. The text column shows `getText` output,
verified by the registry test.

| ID | Name | Range / law | Default | getText examples |
|---|---|---|---|---|
| `algorithm` | Algorithm | choice Room / Plate / Chamber / Hall / Void | Hall | `Void` |
| `color` | Color | choice Clean / Dark / Ruin | Clean | `Ruin` |
| `preDelay` | Pre-Delay | 0–250 ms, skew with 60 ms at the centre | 20 ms | `42 ms`, `4.5 ms`, `0 ms`, `250 ms` |
| `size` | Size | 0–100 %, step 0.1 | 50 % | `72 %` |
| `decay` | Decay | 0.1–60 s log (centre 2.45 s); 60 = ∞ | 2.5 s | `2.50 s`, `0.10 s`, `12.5 s`, `59.0 s`, `∞` |
| `diffusion` | Diffusion | 0–100 % | 70 % | `70 %` |
| `drown` | Drown | 0–100 % | 0 % | `0 %` |
| `modRate` | Mod Rate | 0.01–5 Hz log (centre 0.22 Hz) | 0.5 Hz | `0.42 Hz`, `5.00 Hz` |
| `modDepth` | Mod Depth | 0–100 % | 30 % | `30 %` |
| `width` | Width | 0–200 % | 100 % | `150 %` |
| `hpf` | HPF | 20–1000 Hz log (centre 141 Hz); 20 = open | 20 Hz | `120 Hz`, `1.0 kHz` |
| `lpf` | LPF | 1–20 kHz log (centre 4.47 kHz); 20 k = open | 20 kHz | `8.5 kHz`, `20.0 kHz` |
| `filterMode` | Filter Mode | choice Pre / Loop / Post | Loop | `Pre` |
| `duck` | Duck | 0–100 % | 0 % | `0 %` |
| `mix` | Mix | 0–100 % | 35 % | `72.5 %` |
| `freeze` | Freeze | bool | off | — |
| `output` | Output | ±12 dB, 0 dB at the centre, 0.1 dB steps | 0 dB | `+3.0 dB`, `-4.5 dB`, `0.0 dB` |
| `bypass` | Bypass | bool (host bypass parameter) | off | — |

**Text input.** The parsers accept:

- `inf` and `∞` for DECAY;
- `800 ms` and `3.2 s` for DECAY;
- `0.1 s` and `35` for PRE-DELAY;
- `8.5k`, `4.5 kHz` and `85 Hz` for the filters;
- `-6 dB` for OUTPUT.

**Helpers.** These are in `tleet::voidrev::params` and are used by the editor:

- `percentText`, `msText`, `decayText`, `rateText`, `hzText` and `dbText`;
- `isInfiniteDecay`.

## 12. Verification

| 13.9 / addendum item | Test (`VOID: …`) | Result |
|---|---|---|
| Decay-time validation across sample rates | decay-time validation | worst 4.04 % (5 % limit), 44.1–192 kHz; short decays monotonic |
| No unstable feedback at max settings | no unstable feedback at maximum settings | finite, bounded, no hard clip, every algorithm and colour |
| Freeze for minutes without NaN or drift | FREEZE holds the tail for 2 minutes | drift ≤ 0.065 dB / 107 s (limit 0.12), DC < 0.6 %, new input passes dry |
| ∞ decay | DECAY at its maximum is infinite | lossless (≤ 0.048 dB / 18 s), bounded 2 min |
| Modulation does not click | modulation does not click | worst −63.6 dB HF residue at 5 Hz |
| Mono compatibility | WIDTH… mono compatibility | WIDTH 0 exactly mono; mono and mono→stereo layouts |
| Algorithms measurably distinct | algorithms are measurably distinct | early fraction, centre time, energy peak and NED differ pairwise (`VOID_algorithms.csv`) |
| Addendum 26 filters | filter responses; FILTER MODE | 0.0018 dB worst; LOOP vs PRE/POST |
| Addendum 27 nonlinear | COLOR | THD/THD+N vs level, symmetry, aliasing, early-only THD |
| Every control works | every control audibly changes the output | each in its specified direction |
| No clicks on any change | switching every control | worst −76.5 dBFS (8 kHz+) |
| DROWN preserves base values | DROWN is a macro | exact |
| DUCK | DUCK | −24 / −12 dB, 10 ms attack |
| Telemetry is real | telemetry is real | ≤ 1.6e-7 relative |
| Realtime | all sample rates and block sizes | 0 allocations (switches included), 0 latency |
| Robustness | NaN/Inf; silence / DC / denormal / tails | pass |
| Bypass bit-exact | host Bypass and MIX 0 | bit-exact, every rate and layout |
| State | state round trip | identical renders; foreign or corrupt state ignored |
| Presets | factory presets | 16 load, cover every algorithm and colour |
| CPU | CPU cost | see below |

**CPU** (RelWithDebInfo, one core of the build machine; stereo, RUIN + LOOP
filters, 128-sample blocks; median-block %):

| | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Room | 3.1 | 3.4 | 6.9 | 13.8 |
| Plate | 1.3 | 1.5 | 2.9 | 6.0 |
| Chamber | 4.4 | 3.6 | 7.3 | 14.9 |
| Hall | 4.0 | 4.5 | 9.3 | 19.4 |
| Void | 3.7 | 4.1 | 14.6 | 20.9 |

**CSV files.** These are written to `build-void/tests/test-output/`:

- `VOID_rt60.csv`, `VOID_max_settings.csv`, `VOID_freeze.csv`, `VOID_infinite.csv`
- `VOID_predelay.csv`, `VOID_drown.csv`
- `VOID_filter_responses.csv`, `VOID_filter_modes.csv`
- `VOID_color.csv`, `VOID_ruin_thd.csv`
- `VOID_duck.csv`, `VOID_width.csv`, `VOID_algorithms.csv`, `VOID_switching.csv`

## 13. Mutation checks

Each mutation was applied to the source, rebuilt, and checked against the named
test; then the source was restored. All 20 are caught.

| # | Mutation | Caught by |
|---|---|---|
| M1 | Decay ignores the time spent in allpasses | decay-time validation |
| M2 | FREEZE leaves the loop lossy | FREEZE holds |
| M3 | DROWN writes the effective SIZE into the parameter during processing | DROWN (base values checked after a render) |
| M4 | Loop energy bound removed | DECAY at its maximum |
| M5 | Pre-delay off by one sample | PRE-DELAY |
| M6 | WIDTH scales the mid as well | WIDTH |
| M7 | LOOP filter not in the FDN loop | FILTER MODE |
| M8 | `filterResponseDb` evaluates the wrong Q | filter responses |
| M9 | RUIN input saturation removed | COLOR (early-only THD) |
| M10 | Hard ALGORITHM switch | switching |
| M11 | An allocation in processBlock | all sample rates |
| M12 | Early telemetry measures the late component | telemetry |
| M13 | DARK ignored | COLOR |
| M14 | MOD RATE ignored | every control |
| M15 | FREEZE level hold removed | FREEZE holds (0.12 dB limit) |
| M16 | No open end stops on the filters | filter responses |
| M17 | Allpass transient elimination removed | modulation |
| M18 | RUIN without ADAA | COLOR (aliasing) |
| M19 | PRE-DELAY change jumps instead of crossfading | switching (0 ↔ 101.3 ms) |
| M20 | DUCK attenuation halved | DUCK |

## 14. Factory presets

There are 16 presets. All are newly authored, and every one sets all 17
parameters except Bypass (FREEZE is off).

| Preset | Algo | Color | Pre | Size | Decay | Diff | Drown | Mod | Width | HPF / LPF, mode | Duck | Mix | Out |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Small Room | Room | Clean | 5 ms | 25 | 0.6 s | 60 | 0 | 0.4 Hz / 15 | 90 | 60 / 14000 Loop | 0 | 22 | 0 |
| Tight Ambience | Room | Dark | 0 ms | 10 | 0.35 s | 50 | 0 | 0.5 Hz / 10 | 80 | 80 / 12000 Loop | 0 | 18 | 0 |
| Drum Room | Room | Ruin | 0 ms | 35 | 0.9 s | 55 | 0 | 0.5 Hz / 10 | 120 | 100 / 9000 Post | 30 | 25 | −1 |
| Eternal Room | Room | Clean | 10 ms | 45 | ∞ | 80 | 0 | 0.3 Hz / 20 | 100 | 90 / 11000 Loop | 0 | 35 | −2 |
| Bright Plate | Plate | Clean | 10 ms | 50 | 1.8 s | 80 | 0 | 0.8 Hz / 25 | 120 | 150 / 18000 Post | 0 | 25 | 0 |
| Vocal Plate Duck | Plate | Clean | 60 ms | 45 | 2.2 s | 75 | 0 | 0.6 Hz / 20 | 110 | 200 / 12000 Pre | 45 | 30 | 0 |
| Rusted Plate | Plate | Ruin | 20 ms | 60 | 3 s | 80 | 0 | 1.2 Hz / 50 | 130 | 120 / 7000 Loop | 0 | 35 | −1 |
| Stone Chamber | Chamber | Clean | 8 ms | 50 | 1.4 s | 70 | 0 | 0.5 Hz / 15 | 100 | 50 / 15000 Loop | 0 | 25 | 0 |
| Vintage Chamber | Chamber | Dark | 20 ms | 60 | 2 s | 65 | 0 | 0.4 Hz / 25 | 90 | 80 / 8000 Loop | 0 | 28 | 0 |
| Ruined Chamber | Chamber | Ruin | 12 ms | 55 | 1.8 s | 75 | 15 | 0.9 Hz / 40 | 120 | 100 / 9000 Post | 15 | 30 | −1 |
| Concert Hall | Hall | Clean | 25 ms | 65 | 2.6 s | 70 | 0 | 0.5 Hz / 25 | 100 | 30 / 16000 Loop | 0 | 30 | 0 |
| Dark Hall | Hall | Dark | 30 ms | 70 | 3.8 s | 75 | 10 | 0.3 Hz / 30 | 110 | 40 / 9000 Loop | 0 | 30 | 0 |
| Cathedral Ghost | Hall | Clean | 50 ms | 95 | 7.5 s | 85 | 20 | 0.2 Hz / 35 | 140 | 50 / 11000 Loop | 0 | 35 | 0 |
| Huge Wash | Void | Clean | 40 ms | 90 | 12 s | 90 | 40 | 0.25 Hz / 45 | 150 | 80 / 12000 Loop | 0 | 45 | −2 |
| Shoegaze Drown | Void | Ruin | 15 ms | 75 | 8 s | 85 | 80 | 0.6 Hz / 60 | 160 | 120 / 10000 Loop | 20 | 50 | −3 |
| Abyss | Void | Dark | 80 ms | 100 | 25 s | 95 | 30 | 0.12 Hz / 50 | 180 | 60 / 6000 Loop | 0 | 50 | −3 |

## 15. Not modelled / limitations

- **No real room or hardware is reproduced.** The constants are TLEET's own (RESEARCH R1).
- **Late field is not physically modelled.** No air absorption beyond the two-band shelves, no source or listener directivity, and the late field has no geometry.
- **No oversampling.** RUIN's aliasing is −45.6 dB (8.3).
- **Large SIZE sweeps leave a small HF residue.** A full-range SIZE sweep is a Doppler pitch glide, which is inherent to a delay network that is resized live. Its 8 kHz+ residue is −76.5 dBFS on a −12 dBFS tone.
- **Hold correction is not bit-transparent.** The FREEZE level hold corrects a few hundredths of a dB per second.
- **The ∞ bound engages only on loud sustained input.** A sustained loud input into ∞ DECAY settles at about −11 dBFS late RMS (−8.7 dBFS Void), by design.

## 16. Owner decisions

These are the open questions decided as a reverb designer would. Each can be
revisited without changing the topology.

- **D1 — DECAY reference.**
  - RT60 at 1 kHz, 0.1–60 s on a log knob.
  - The top of the knob is ∞ (text "∞"), so ∞ is reachable by automation and the knob alone.
  - A finite DECAY reaches at most 59.9 s, including when DROWN raises it.
- **D2 — Short decays.** T30 accuracy is required from 0.8 s up. Below that, the response must reach −60 dB within 1.6·DECAY + 0.35 s and get shorter monotonically (5.4).
- **D3 — ∞ DECAY keeps accepting input.**
  - The late output is limited by a slow loop gain that is transparent below the ceiling.
  - Ceiling: 0 dBFS (safety only) up to RT60 10 s; −12 dBFS late RMS per channel from RT60 30 s and at ∞/FREEZE; log-interpolated between (6.4).
  - Rationale: a lossless loop with input is an integrator. −12 dBFS leaves headroom for the dry signal and the early reflections, and it engages only on loud sustained input.
- **D4 — FREEZE.**
  - The input gate closes (two 15 ms stages) and the loop becomes lossless.
  - RUIN's in-loop saturation and the LOOP filters leave the loop; LOOP becomes POST while frozen.
  - A level-hold servo keeps the frozen level.
  - New input is heard dry through MIX and never enters the tail.
  - FREEZE is saved with the state like every parameter. Factory presets have it off.
- **D5 — The filter end stops are truly open.** HPF 20 Hz = identity; LPF 20 kHz ≈ transparent. There is a continuous transition over the last third of an octave (6.1).
- **D6 — ALGORITHM change.** Dual-slot crossfade: the new network starts silent, and the old one rings out over 1 s (7.1). There is no hard cut and no allocation.
- **D7 — DROWN.** The smoothstep curve and the shares in section 9 (SIZE 60 %, DECAY ×4, DIFFUSION 80 %, MOD DEPTH 50 %, WIDTH 25 % of the headroom, LPF −0.75 octave). ∞ stays ∞.
- **D8 — COLOR definitions.** As in sections 8.2 and 8.3. RUIN combines input saturation (+12 dB drive, ADAA), in-loop saturation, smear, modulation instability and darker HF decay. DARK band-limits at 8 kHz in, 10 kHz out, with a darker HF decay.
- **D9 — DUCK law.**
  - Dry peak detector.
  - Range: −45 dBFS for none to −20 dBFS for full.
  - Maximum 24 dB × DUCK.
  - 10 ms attack, 250 ms release.
- **D10 — WIDTH, MIX and OUTPUT.**
  - WIDTH is M/S side gain on the wet only, 0–200 %.
  - MIX is a linear crossfade; MIX 100 % = exactly wet. Default 35 %.
  - OUTPUT acts after MIX.
  - Mono out = the mid of the wet signal.
- **D11 — PRE-DELAY changes.** A 20 ms tap crossfade, without a glide.
- **D12 — Modulation.**
  - Sine plus band-limited random wander per line.
  - RUIN shifts the balance to random.
  - Pitch deviation is capped at 3 %.
  - MOD RATE 0.01–5 Hz, log.
- **D13 — Filter slopes.** 12 dB/oct Butterworth for both HPF and LPF. FILTER MODE defaults to LOOP, the reverb-architecture choice that 13.9 points to.
- **D14 — Zero latency.** The tail length reported to the host is 62 s.

## 17. Open items

- **O1** Listening review of the algorithm voicings and the presets (RESEARCH R1).
- **O2** Perceptual check of the MOD 100 % / 5 Hz residue on program material (RESEARCH R2).
- **O3** The owner may prefer a different ∞/FREEZE ceiling (D3) or DROWN shares (D7). Each is one constant: `design::ceilingLong` and the `drown::` constants.
- **O4** Optional ×2 oversampling for RUIN if −45.6 dB of aliasing proves audible.
- **O5** Velvet-noise early diffusion as a CPU optimisation (RESEARCH R3).
