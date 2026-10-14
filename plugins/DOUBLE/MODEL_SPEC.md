# DOUBLE — model specification

Stereo doubler / micro-pitch / ADT (master spec 13.10). This file is the top of
the priority order for DOUBLE's DSP: where it conflicts with the master spec
or the mockup, this file wins. DOUBLE is an original TLEET processor; there is
no hardware unit to clone, so nothing here is SOURCE_CONFIRMED.

Provenance tags:

- **[SPEC]** required by master spec 13.10, addendum 16 / 26 or the product
  brief (control set, ranges, behaviour).
- **[DSP_STANDARD]** a textbook technique, used as published (TPT state-variable
  filter, Doppler delay-line pitch shift, WSOLA-style correlation splicing,
  Schroeder all-pass, constant-power pan).
- **[TLEET_EXTENSION]** our design choice where the spec leaves freedom.
- **[TUNED]** a value set by ear and by the tests; the values to revisit in
  listening sessions.

All numbers marked *measured* come from `tests/DoubleTests.cpp` ("DOUBLE: …"),
48 kHz unless stated; CSVs are written to `test-output/DOUBLE_*.csv`.

## 1. Signal flow

```
            ┌──────────────────────────── dry (latency 0) ─────────────────────────────┐
in (L,R) ──►┤                                                                           ├─► Mix ─► Output ─► Bypass ─► out
            └─► mono feed ─► wet HPF ─► wet LPF ─► GHOST shelf ─► delay ring             │
                 (L+R)/2                                            │                   │
                     ┌──────────────────────────────────────────────┘                   │
                     ├─► voice 0: read tap (delay + micro-pitch + drift) ─► diffusion ─► gain ─► pan ─┐
                     ├─► voice 1 …                                                                  ├─► Σ ─► Mono Safe side HPF ─► wet
                     ├─► voice 2 …  (VOICES 4)                                                      │
                     └─► voice 3 …  (VOICES 4)                                                      ┘
```

- The voices are moving read taps on one mono delay ring. A tap whose delay
  changes at a constant rate plays the signal at a constant pitch ratio
  (Doppler), so delay and micro-pitch are one operation. [DSP_STANDARD]
- **The wet filters sit on the voice feed, ahead of the voices**, not after
  them as drawn in 13.10. For delays and gains the order is irrelevant (all
  linear, time-invariant); a detuned voice sees the filter at its input
  frequency f instead of f·r. Measured on a +25-cent voice: the audio follows
  |H(f_in)| within 0.016 dB (85 Hz–15 kHz) and differs from the drawn curve
  at f_out by at most the curve's own change over 25 cents (0.41 vs 0.40 dB).
  One filter instead of four. [TLEET_EXTENSION]
- **Stereo input is summed to mono for the voices**; the input's own image is
  carried by the dry side of Mix. The wet is a new stereo spread built from
  that mono feed. [TLEET_EXTENSION]
- **Zero latency.** Voices only read the past (see the delay floor, 4.3), so
  the plugin reports 0 samples and the dry needs no delay. [SPEC "ideally zero
  latency"]
- Mono and stereo buses (input layout = output layout). On a mono bus the
  voices sum at their centre gain; Width and Mono Safe do nothing.

## 2. Controls

| Parameter ID | Name | Range | Default | Law | Tag |
|---|---|---|---|---|---|
| `voices` | Voices | 1 / 2 / 4 (added voices) | 2 | slots 0 / 0–1 / 0–3, 50 ms fades | SPEC |
| `mode` | Mode | Tight / Wide / Ghost | Tight | template set (section 3), glides | SPEC |
| `detune` | Detune | −25…+25 cents | 8 | × mode × slot share; + = left voices sharp | SPEC range, sign TLEET_EXTENSION |
| `delay` | Delay | 0–40 ms | 15 | × mode × slot share, floor in 4.3 | SPEC |
| `humanize` | Humanize | 0–100 % | 35 | depth of the slow drifts (section 7) | SPEC |
| `width` | Width | 0–200 % | 100 | placement × Width; 0 = all voices centred | SPEC |
| `hpf` | HPF | 20–1000 Hz (log) | 20 | 12 dB/oct Butterworth on the wet | SPEC, slope TLEET_EXTENSION |
| `lpf` | LPF | 1–20 kHz (log) | 20 k | 12 dB/oct Butterworth on the wet | SPEC, slope TLEET_EXTENSION |
| `monoSafe` | Mono Safe | off / on | on | section 6 | SPEC |
| `autoLevel` | Auto Level | off / on | on | section 8 | SPEC |
| `mix` | Mix | 0–100 % | 50 | linear crossfade with the latency-aligned dry | SPEC (as on every TLEET plugin) |
| `output` | Output | −12…+12 dB | 0 | trim after Mix (like ASH) | TLEET_EXTENSION placement |
| `bypass` | Bypass | off / on | off | bit-exact dry, 20 ms ramp | host bypass |

Parameter IDs, ranges and defaults are owned by `Parameters.h` (version 1);
DOUBLE's DSP did not change any of them.

## 3. Voice and mode templates

Offsets are never fixed numbers: every voice takes a **share** of the
mode-scaled knob values (`dsp/VoiceTemplates.h`, the single source for the
engine and the tests). [SPEC "derive from Detune/Delay controls and mode templates"]

```
detune(slot) = Detune · mode.detune · slot.detune                       (cents)
delay(slot)  = Delay  · mode.delay  · slot.delay                        (ms, before the floor 4.3)
pan(slot)    = clamp(Width · 0.7 · mode.spread · slot.pan
                     · (VOICES 1 ? 0.5 : 1) · (Mono Safe ? 0.5 : 1), −1, +1)
```

| Slot | Role | detune share | delay share | pan share | level | GHOST all-passes | Tag |
|---|---|---|---|---|---|---|---|
| 0 | outer left | +1.00 | 1.00 | −1.00 | 0 dB | 4.3 / 7.1 ms | TLEET_EXTENSION |
| 1 | outer right | −1.00 | 0.78 | +1.00 | 0 dB | 5.1 / 6.3 ms | TLEET_EXTENSION |
| 2 | inner left | +0.45 | 0.70 | −0.45 | −1 dB | 3.7 / 8.3 ms | TLEET_EXTENSION |
| 3 | inner right | −0.55 | 0.86 | +0.40 | −1 dB | 4.7 / 6.9 ms | TLEET_EXTENSION |

- The outer pair is symmetric in pitch and place but **not identical in
  time** (1.00 / 0.78): equal delays would fuse into a phantom centre. The
  inner pair is smaller, earlier, unequal and 1 dB lower, so the outer pair
  defines the width. [SPEC "symmetric but not identical", values TUNED]
- Positive Detune puts the sharp voices on the left (measured: Detune +10,
  Width 200 → left +10.0000 c, right −10.0000 c; Detune −10 → mirrored).
- A slot keeps its shares in every layout: changing VOICES only fades slots
  in or out (plus slot 0's pan glide between 1 and 2 voices). VOICES 1 is slot
  0 at half its placement so Width still moves it.

| Mode | detune × | delay × | spread × | humanize depth × | humanize rate × | wet shelf | diffusion g | Intent |
|---|---|---|---|---|---|---|---|---|
| TIGHT | 0.60 | 0.75 | 0.55 | 0.60 | 1.0 | — | — | small, natural double |
| WIDE | 1.00 | 1.00 | 1.00 | 1.00 | 1.0 | — | — | the knob values, wide |
| GHOST | 1.25 | 1.40 | 0.85 | 1.70 | 1.5 | −8 dB above 2.5 kHz | 0.55 | darker, smeared, later, unstable |

All mode values [TUNED]; the intents are [SPEC]. Measured at 4 voices,
Detune 10, Delay 18, Width 100 (white noise unless stated):

| | TIGHT | WIDE | GHOST |
|---|---|---|---|
| outer-left pan / detune / delay | −0.385 / 6.0 c / 13.5 ms | −0.70 / 10.0 c / 18.0 ms | −0.595 / 12.5 c / 25.2 ms |
| wet L/R correlation | 0.887 | 0.650 | 0.742 |
| wet spectral centroid | 3203 Hz | 2791 Hz | **1273 Hz** |
| peak xcorr wet vs dry (Detune 0) | 0.498 | 0.543 | **0.293** |
| voice impulse, 90 % energy spread | 0.04 ms | 0.04 ms | **25.6 ms** |
| pitch drift s.d. at Humanize 50 | 0.74 c | 1.24 c | **2.52 c** |

## 4. Micro-pitch engine

### 4.1 Pitch by delay ramp

A voice's read delay (samples) is `d(n) = c + δ(n) + u(n)`: centre delay `c`,
humanize drift `δ`, and the pitch ramp `u`, which moves by `1 − r` per
sample with `r = 2^(cents/1200)` (`cents` = template detune + pitch drift).
The tap then plays the input at exactly ratio `r`. [DSP_STANDARD] The ratio
uses a 4th-order series of 2^x, error < 2·10⁻⁸ cent over ±60 cents.

### 4.2 Splices (keeping the ramp in its window)

`u` must stay within a window `W` around the centre. When it leaves, a second
tap B is started and A is crossfaded into it:

- **Window** `W = 12 ms · min(1, R / 1 cent)`, `R` = the largest possible pitch
  offset (|detune| + humanize pitch depth). Below 1 cent the window shrinks
  with the offset, so the interval between splices stays 10–21 s; at the
  largest offset (GHOST, 31 cents) it is 0.3–0.7 s. [TUNED]
- **Phase match** (WSOLA-style [DSP_STANDARD]): B starts on the other half of
  the window at the lag that maximises the normalised cross-correlation
  between the signal under A and under B over an 8 ms window. Coarse search
  on a ~12 kHz grid, the three best separated coarse peaks refined at full
  rate (±1 coarse step), parabolic sub-sample peak. (Keeping three peaks fixed
  a tie between a true peak and an edge point that cost up to 0.05 cent.)
- **Crossfade** 24 ms raised cosine with correlation-normalised gains:
  `y = (a·A + b·B) / sqrt(a² + b² + 2ρab)` — amplitude-complementary when the
  taps are in phase (ρ = 1), equal-power when uncorrelated (ρ = 0). [DSP_STANDARD]
- The lag range on the far half of the window spans 6–12 ms, which holds a
  whole multiple of every period **≤ 12 ms (f ≥ 83 Hz)**. Partials below
  83 Hz are not phase-matched and may step in phase at a splice (once per
  splice interval). Measured: a 60 Hz tone through a +25-cent voice deviates
  3.8 dB from a single-sine fit (phase steps), 75 Hz 0.12 dB, ≥ 85 Hz within
  0.016 dB. The wet HPF is the remedy; the bass preset uses HPF 320 Hz.

Measured on one voice at ±25 cents (WIDE, Delay 20), 6 s per case:

| Tone | splices | level ripple | max phase jump (10 ms windows) | residual vs ideal shifted tone |
|---|---|---|---|---|
| 110 Hz | 9 / 9 | 0.00006 dB | 0.0007° | −87 dB |
| 220 Hz | 9 / 9 | 0.0002 dB | 0.002° | −77 dB |
| 1 kHz | 11 / 7 | 0.0002 dB | 0.0015° | −68 dB |
| 3.15 kHz | 11 / 13 | 0.0004 dB | 0.04° | −59 dB |
| 196 Hz, 3 partials | 8 | 0.088 dB (20 ms RMS) | | |

**Pitch accuracy** (1 kHz, 3.5 s per voice, zero-crossing frequency over the
last 3 s, splices included — 126 splices in the measured runs):
max error **9.4·10⁻⁵ cent** over 72 voices (3 modes × Detune −25, −12.5, −3,
+0.5, +8, +25 × 4 slots); telemetry detune equals the template to < 10⁻⁴ cent.
`test-output/DOUBLE_pitch_accuracy.csv`.

### 4.3 Delay floor and delay changes

- **Floor.** A modulated tap must never read the future:
  `c = max(requested, W/2 + δmax + overshoot + guard)`, with
  `overshoot = (24 ms + 32 samples)·(r_max − 1)` (the ramp keeps moving during
  a crossfade and until the next control update) and `guard` = 6 samples
  (half the interpolator) scaled down when the modulation itself is smaller.
  With Detune 0 and Humanize 0 the floor is 0, so Delay 0 really is 0.
  Measured: Delay 0, Detune 8 → centre delays 6.18–6.24 ms (W/2 = 6 ms).
  [TLEET_EXTENSION; required for zero latency]
- **Delay accuracy** (Detune 0, Humanize 0, phase slope of the voice's
  response over the wet filter's own response, 200 Hz–4 kHz): max error
  **2.8·10⁻⁵ ms (0.0013 samples)** over 40 voice/setting pairs (TIGHT and WIDE ×
  Delay 0, 2.5, 13.3, 27.7, 40 × 4 slots); telemetry equals the template.
  GHOST's diffused voices: telemetry exact, response onset within the
  interpolator's half length. `test-output/DOUBLE_delay_accuracy.csv`.
- **Read point.** Measured over 6 s at ±25 and +4 cents: the tap never leaves
  `W/2` + crossfade overshoot around the centre; the delay measured from the
  audio (cross-correlation, 128-sample windows) matches the telemetry tap
  delay within **0.29 samples**.
- **Delay changes.** Centre changes under 2 ms glide (one-pole, 50 ms):
  moving the Delay knob bends the pitch like a tape delay's time change.
  Larger steps splice straight to the new centre, phase-matched within
  ±2 ms. Detune changes glide 20 ms. [TLEET_EXTENSION]

### 4.4 Interpolator

12-tap Kaiser-windowed sinc (β = 5), 512 tabulated phases with linear
interpolation between phases, normalised to unity DC gain, exactly a unit
impulse at integer delays (an unmodulated integer delay is bit-transparent).
Worst fractional position (computed): within 0.03 dB to 16 kHz at 44.1 kHz,
0.04 dB to 17 kHz at 48 kHz; at 20 kHz −1.4 dB (48 kHz) / −5 dB (44.1 kHz) at
half-sample positions, so a detuned voice's top octave is softened slightly.
Measured through the plugin with a fractional static delay: 0.016 dB to
16 kHz. Below 5 samples of delay (only at Delay ≤ 0.2 ms with no detune and
no humanize, or sub-0.02-cent detunes) the read falls back to linear
interpolation, which keeps the zero latency. The ring is mirrored so every
kernel and correlation window is contiguous memory. [DSP_STANDARD]

## 5. Wet filters

- **HPF** and **LPF**: 2nd-order Butterworth (k = √2), TPT state-variable
  filters, bilinear with pre-warping (`g = tan(π·fc/fs)`, fc clamped to
  0.49·fs), HPF then LPF, double precision, on the mono voice feed.
  [DSP_STANDARD]
- Knob changes glide in log frequency (one-pole, 20 ms, coefficients updated
  every 32 samples) — no zipper, and the coefficients are bit-identical to
  the target once settled.
- **GHOST shelf**: first-order high shelf, −8 dB above 2.5 kHz
  (`y = LP + G·(x − LP)`, LP = one-pole TPT at 2.5 kHz), gain ramped linearly
  over 40 ms on mode changes. [TUNED]
- **Response function** (`dsp/WetFilter.h`):
  `wetFilterResponseDb(hz, hpfHz, lpfHz, sampleRate)` = HPF·LPF, and an
  overload with `mode` that adds GHOST's shelf. Both evaluate the exact
  z-domain transfer functions of these structures with the same `g, k`, so the
  curve the editor draws is the filter that runs. `getWetSampleRate()` is the
  host rate (no oversampling).

Measured (one voice, Detune 0, sine sweep in 1/6-octave steps plus the
corner frequencies, steady-state sine fit), max |measured − function|:

| Case | HPF | LPF | Mode | fs | max error |
|---|---|---|---|---|---|
| knobs min | 20 Hz | 1 kHz | TIGHT | 48 k | 1.2·10⁻⁶ dB |
| knobs mid | 141 Hz | 4.47 kHz | TIGHT | 48 k | 4.0·10⁻⁶ dB |
| knobs max | 1 kHz | 20 kHz | TIGHT | 48 k | 3.3·10⁻⁶ dB |
| open | 20 Hz | 20 kHz | WIDE | 48 k | 7.6·10⁻⁷ dB |
| narrow | 1 kHz | 1 kHz | WIDE | 48 k | 7.5·10⁻⁶ dB |
| vocal | 120 Hz | 9 kHz | TIGHT | 48 k | 4.3·10⁻⁷ dB |
| guitar | 250 Hz | 6 kHz | WIDE | 44.1 k | 2.2·10⁻⁶ dB |
| hi-res | 80 Hz | 16 kHz | TIGHT | 96 k | 9.3·10⁻⁷ dB |
| GHOST (5-arg function) | 200 Hz | 8 kHz | GHOST | 48 k | 1.4·10⁻⁵ dB |
| fractional delay 13.3 ms | 100 Hz | 12 kHz | TIGHT | 48 k | 0.016 dB (≤ 16 kHz, interpolator) |

`test-output/DOUBLE_wet_filter_<case>.csv` (expected, measured, error per
frequency) and `DOUBLE_wet_filter_summary.csv`.

## 6. Placement: Width and Mono Safe

- **Pan law**: constant power, `gL = cos θ`, `gR = cos(π/2 − θ)`,
  `θ = (pan + 1)·π/4`, so gL and gR are bit-identical at the centre. Pans glide
  30 ms (double precision, landing exactly on the target). [DSP_STANDARD]
- **Width** scales every voice's placement (section 3). Width 0: every
  voice centred — measured **L == R bit for bit** in all modes, Mono Safe on
  and off, Humanize 80, after automating Width from 120 to 0; telemetry pans
  0. Width 200: the WIDE pair is hard-panned (left channel: sharp voice 0.500,
  flat voice 4·10⁻⁶).
- **Mono Safe** (stereo only, 30 ms crossfade) [SPEC intent, values TUNED]:
  1. placement × 0.5 (before the ±1 clamp);
  2. GHOST diffusion all-pass coefficient × 0.5 (less decorrelation);
  3. the wet **side** signal is high-passed at 200 Hz (2nd-order Butterworth
     TPT); the wet mid — the mono fold-down — is untouched.

  Voices, detunes and delays are unchanged (telemetry identical), so the
  double is kept. Measured (Width 200, Detune 10, Delay 18, Humanize 35, pink
  noise; Mono Safe off → on):

  | Mode, voices, Mix | L/R correlation | mono fold-down loss | side below 150 Hz re mid |
  |---|---|---|---|
  | TIGHT 2, 100 % | 0.42 → 0.89 | −1.49 → −0.26 dB | −12.0 → −29.9 dB |
  | TIGHT 4, 100 % | 0.62 → 0.93 | −0.90 → −0.15 dB | −13.6 → −32.0 dB |
  | WIDE 2, 100 % | 0.09 → 0.62 | −2.65 → −0.92 dB | −9.1 → −24.1 dB |
  | WIDE 4, 100 % | 0.41 → 0.79 | −1.51 → −0.48 dB | −12.7 → −27.3 dB |
  | GHOST 2, 100 % | 0.08 → 0.77 | −2.69 → −0.54 dB | −7.0 → −24.5 dB |
  | GHOST 4, 100 % | 0.46 → 0.87 | −1.37 → −0.28 dB | −11.3 → −27.4 dB |
  | WIDE 2, 50 % | 0.62 → 0.84 | −0.92 → −0.35 dB | −14.6 → −28.5 dB |

  In every case (3 modes × 2/4 voices × Mix 100/50) correlation rises by
  > 0.1, the fold-down loss is at least halved, the LF side drops > 6 dB, the
  side stays above −20 dB re mid, and nothing goes anti-phase.
  `test-output/DOUBLE_mono_safe.csv`.

## 7. Humanize

Three slow drifts per voice — pitch, delay (timing) and level — each a
sample-and-hold random target in [−1, 1] held for a random time, smoothed by
two cascaded one-poles (`dsp/Humanizer.h`). The output is a convex
combination of past targets, so it is **bounded** by construction, C1
(no steps, no zipper), falls at 40 dB/decade above the smoothing corner, and
has **no period** (random hold times: never an LFO chorus). Deterministic
seeds per slot. [SPEC behaviour, design TLEET_EXTENSION]

| Drift | Depth at 100 % (WIDE) | Hold | Smoothing | Tag |
|---|---|---|---|---|
| pitch | ±5 cents | 0.35–0.90 s | 0.22 s | TUNED |
| delay | ±1.2 ms | 0.60–1.50 s | 0.40 s | TUNED |
| level | ±0.5 dB | 0.30–0.80 s | 0.20 s | TUNED |

Mode scales the depths (TIGHT 0.6, GHOST 1.7) and GHOST runs 1.5× faster.
The knob's depth glides 50 ms. Measured (1 voice, WIDE, Detune 8, 30 s,
telemetry at 1 kHz): Humanize 0 → pitch, delay and gain exactly static;
Humanize 100 → pitch s.d. 2.6 c (range 11.4 c incl. the Doppler of the delay
drift), delay s.d. 0.42 ms (range 1.93 ms, bound ±1.2 ms), level range
0.85 dB (bound ±0.5 dB); energy above 3 Hz: 0.17 % of the pitch drift,
0.016 % of the delay drift; highest autocorrelation of the pitch drift at
1–10 s lags 0.21 (no periodicity); audio pitch vs telemetry ≤ 0.015 cent per
100 ms.

## 8. Auto Level

Holds the **summed voice power** constant, on the assumption that the voices
are mutually decorrelated (different delays and detunes):

```
norm = √2 · ((1 − a) + a / sqrt(Σ fade_shape_i² · t_i²))      t_i = slot level, a = Auto Level (0/1, 20 ms ramp)
```

Stereo: a centred voice at `√2 · cos(π/4)` = 1 sits at the dry's level. The
norm is computed from the fade states (which are linear ramps, so their
future values are known) exactly at the end of every ≤ 32-sample run and
interpolated inside it, so it tracks the 50 ms voice fades with no lag.
[SPEC intent, law TLEET_EXTENSION]

Measured (Detune 8, Delay 15, Humanize 35, Width 100, 6 s, level re the wet
feed):

| Content | 1 voice | 1 / 2 / 4-voice spread (worst mode) |
|---|---|---|
| white noise | −0.06…−0.09 dB | ≤ 0.05 dB |
| pink noise above 150 Hz | −0.02…−0.07 dB | ≤ 0.09 dB |
| full-band pink noise | −0.03…−0.08 dB | ≤ 0.85 dB (+0.20…0.30 dB at 2, +0.49…0.55 dB more at 4) |
| Auto Level off, white noise | | +3.01…3.04 dB (1→2; 3.01 expected), +2.55…2.57 dB (2→4; 2.54 expected) |

The full-band pink excess is real, not an error of the law: below ~150 Hz the
voices' delays differ by a fraction of a period and same-side voices detune
in the same direction, so their low partials sum partly coherently (a 147 Hz
harmonic tone shows +0.25…0.45 dB from 2 to 4 voices). Switching 1 → 2 → 4 →
1 while running, white noise: 50 ms levels stay within 0.76 dB (window noise)
with Auto Level on, 6.3 dB off. `test-output/DOUBLE_auto_level.csv`.

## 9. GHOST diffusion

Each voice passes two Schroeder all-passes (`g` = 0.55, 0.275 with Mono Safe;
lengths per slot in section 3, different per slot so the voices decorrelate):
flat magnitude, smeared transients (measured 90 % energy spread of a voice's
impulse 25.6 ms vs 0.04 ms in WIDE). They run in every mode, so their state is
always current, and are crossfaded in and out over 40 ms on mode changes.
Running them only in GHOST left stale buffers that clicked on entering GHOST
(measured 0.047 |2nd difference| vs 0.0027 baseline). [DSP_STANDARD structure,
values TUNED]

## 10. Mix, Output, Bypass, latency

- `y = (dry + (wet − dry)·mix) · output`, mix and output smoothed 30 ms. Mix
  0 with Output 0 is bit-exact dry (tested with four detuned GHOST voices
  running). The linear law loses up to 3 dB at Mix 50 on a decorrelated wet;
  the factory presets compensate with Output (section 14).
- **Bypass**: 20 ms ramp, then bit-exact dry (tested with Mix 100, Output +9).
- **Latency 0** reported; measured impulse onset 0 samples at 44.1 and
  96 kHz; with Delay 0 / Detune 0 / one centred voice the wet equals the wet
  filter alone within 1.5·10⁻⁸.
- Tail: 0.2 s (longest voice plus the GHOST all-pass ring-out).

## 11. Voice count and mode changes

Voices enter and leave with 50 ms raised-cosine fades; a voice entering from
silence starts settled on its targets (no glide, no splice). Mode changes
glide every template value: detunes 20 ms, centre delays 50 ms (or a
phase-matched splice above 2 ms), pans 30 ms, humanize depth 50 ms, shelf and
diffusion 40 ms. Measured (110 Hz sine, Mix 50, Mono Safe, switching VOICES
and MODE every 37 × 64 samples): max |2nd difference| **0.00258** vs 0.00269 for
the worst fixed setting. Rapid random automation of every parameter every 64
samples: output stays finite, max |y| 0.26, max sample step 0.0068 (200 Hz
sine at 0.3).

## 12. Telemetry (`DoubleTelemetry`)

Published at the end of every block for all four slots (inactive slots too):

- `active` — the slot is in the current layout (VOICES 2 never shows 4);
- `pan` — the real placement after Width and Mono Safe, as it glides (0 on a
  mono bus);
- `delayMs` — centre delay including the humanize drift;
- `detuneCents` — the tap's instantaneous playback ratio in cents:
  template + pitch drift + the Doppler of delay drift and centre glides;
- `gain` — linear, relative to the dry level of a centred voice: slot level ×
  Auto Level × fade × humanize level (wet filters excluded).

Per-voice peak of the voice output (after gain, before pan) is pushed every
4 ms slice, aligned to the absolute sample count (inactive slots push 0).
Measured: pans within 2·10⁻⁴ and gains within 0.01 dB of the values measured
on the audio (3 modes × 4 widths × 4 slots); slice peaks within 6·10⁻⁸ of the
audio; telemetry pitch within 0.015 cent of the audio under Humanize 100.

## 13. Robustness and cost

- Host input sanitised (NaN/Inf → 0) before the dry copy and the voices;
  `ScopedNoDenormals`; silence in → exact silence out; DC never reaches the
  wet (HPF).
- No allocation after `prepareToPlay`: tested at 44.1/48/88.2/96/192 kHz ×
  blocks 1…2048 × mono/stereo with the busiest settings (4 voices, GHOST,
  ±25 cents, Delay 40, Humanize 100, Width 200, Mono Safe) and a splice inside
  every counted run. Blocks longer than the prepared size run in chunks.
- Control-rate work (templates, windows, splice triggers) every 32 samples,
  aligned to the absolute sample count, so results do not depend on the host
  block size.
- CPU (this build machine, RelWithDebInfo, block 128, Detune 25, Humanize
  100, Mono Safe on): 0.8 % of one core at 48 kHz for 1 voice, 1.9–2.0 % for 4
  voices.

## 14. Factory presets

11 presets (`presets/*.json`): Tight Vocal Double, Wide Lead Vocal, Backing
Stack, Guitar Wall, Mono Safe Widener, Subtle Thickener, Slap Double, Synth
Spread, Ghost Layer, Haunted Whisper, Bass Upper Double. Each is
level-matched: Output = −10·log10((1 − m)² + m²) rounded to 0.5 dB, the loss
of the linear Mix law on a decorrelated wet. Measured on pink noise above
150 Hz: output within −0.86…+0.01 dB of the input. [TUNED]

## 15. Test sensitivity

Each of these deliberate faults was built and made the named test fail
(then reverted):

| Fault | Failing test |
|---|---|
| splice lag = middle of the range, no correlation search | splices seamless (phase jumps, residual) |
| pitch ratio 1 % off (≈ 0.08 cent) | pitch accuracy |
| wet filter coefficients frozen at 20 Hz / 20 kHz | wet filter sweep |
| DSP HPF without pre-warping (response function unchanged) | wet filter sweep (knobs max, narrow) |
| telemetry detune without the humanize drift | Humanize (audio vs telemetry) |
| Mono Safe placement share off / side HPF off | Mono Safe |
| Auto Level ignored / norm lagging the fades (one-pole) | Auto Level (spread / switching) |
| instant voice fades / diffusers stopped outside GHOST | click-free automation |
| Bypass as a crossfade instead of the exact dry | latency / Mix / Bypass |
| every delay + 1 sample | delay accuracy |
| VOICES 2 running a third voice | VOICES 1 / 2 / 4 |
| input sanitising removed | NaN/Inf |
| right pan gain 1 ppm high / pan glide in float | Width 0 (L == R) |
| slice peaks scaled by 0.9 | telemetry equals the audio |

## 16. Not modelled / open items

- Splices are phase-matched for partials above 83 Hz only (4.2); a longer
  search would raise the delay floor. Revisit if a bass use case needs it.
- Auto Level assumes decorrelated voices; LF-heavy material gains up to
  ~0.9 dB from 1 to 4 voices (section 8). A coherence-aware law would need a
  signal-dependent estimate.
- Stereo input is doubled from its mono sum (section 1).
- The editor's filter curve uses the four-argument response function, which
  omits GHOST's −8 dB shelf; the five-argument overload draws the full wet
  tone.
- Listening sessions: template shares, mode scalings, humanize depths and
  rates, GHOST shelf and diffusion are all [TUNED] by test, not yet by a
  panel.
