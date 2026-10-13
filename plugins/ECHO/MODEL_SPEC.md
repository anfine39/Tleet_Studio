# ECHO — model specification

Multi-head tape echo with spring reverb, Roland RE-201 Space Echo DNA. This
file is the top of the priority order for ECHO's DSP: where it conflicts with
the master spec or the mockup, this file wins. The evidence is in
`RESEARCH.md` (sources S1..S12, findings F1..F12, open tasks R1..R6).

Provenance tags:

- **[SOURCE_CONFIRMED]** published by Roland (S4) or in the Roland-licensed
  model's manual (S2, S3), or a specification sheet count (S9, S10).
- **[SCHEMATIC_OR_MEASURED]** from the engineering literature (S5, S6) or the
  tape mechanism itself (the physics of one tape and fixed heads).
- **[APPROXIMATION]** a modelling choice where no source gives the value.
  Section 15 lists what to measure.
- **[TLEET_EXTENSION]** deliberately not on the hardware.
- **[OWNER-DELEGATED]** a value the owner asked us to choose "closest to the
  real RE-201" (coordinator note, 2026-10-13). All of them are listed in
  section 14.

ECHO is hardware-informed. It is not a measured 1:1 clone of one unit.

## 1. Signal flow

```
in L/R ─► sanitise ─┬─► mono ─► [PRE filter] ─┐                    (record stage: 2x below 80 kHz)
                    │                         ├─► ↑2 ─► preamp (DRIVE, CHARACTER) ─► (+) ─► tape saturation ─► ↓2 ─► + hiss ─► × dropouts ─► TAPE
                    │      feedback ──────────┘                                       ▲
                    │          ▲                                                      └── ↑2 ◄── feedback
                    │          │
                    │      TAPE ─► heads 1/2/3 (MODE, one tape speed) ─► playback amp: LF roll-off, head loss, BASS, TREBLE
                    │                 │                                       │
                    │                 │                                       ├─► [LOOP filter] ─► × INTENSITY ─► feedback
                    │                 │                                       └─► [POST filter] ─► × ECHO VOLUME ─► echo M/S ─┐
                    │                 │                                                                                         ├─► + spring × REVERB VOLUME ─► STEREO width ─► × DUCK ─► wet L/R
                    │      preamp out + echo ─► springs A/B ─────────────────────────────────────────────────────────────────────┘
                    │                                                                                                            │
                    └─► dry (the input itself; zero latency) ──────────────────────────────────────────────────────────── MIX ◄─┘
                                                                                                                                │
                                                                                                   OUTPUT ─► host Bypass (dry) ─► out
```

- The engine runs **sample by sample** (the feedback delay can be shorter
  than a host block). Controls, the tape speed and filter coefficients update
  every **K = 32 samples** (a control block); every gain, the BASS/TREBLE
  shelves, the record-stage voicing, the tape saturation level and the chain
  compensation are interpolated per sample inside the block.
- Double precision inside; the tape stores float (the audio).
- The tape is **mono** (the RE-201 has one record path): the record input is
  (L + R)/2. The stereo image comes from head panning, the two springs and
  STEREO (3.3).
- **Latency: 0** at every rate. The record stage's oversampler delay is taken
  off the tape delay (4.4); the dry path is the plugin input itself.

## 2. MIX, OUTPUT, Bypass, buses

- **MIX** [TLEET_EXTENSION]: linear crossfade, out = dry + (wet − dry) × MIX.
  The wet is a separate return (echo + spring), not a level-matched copy of
  the dry, so a linear law gives MIX 100 % = pure return and keeps
  MIX 0 % bit-exact (owner-delegated: as SHAPE). 30 ms ramp with 2 ms rounded
  corners.
- **OUTPUT** ±12 dB after the blend, 30 ms dB-linear ramp.
- **Host Bypass** (`bypass`): 20 ms crossfade to the input; once settled the
  output **is** the input, bit for bit (measured: bit-exact 20 ms after
  engaging; bit-exact at 44.1–192 kHz, every layout).
- **MIX 0 %, OUTPUT 0 dB**: bit-exact input (tested at every rate and layout).
- **Buses**: mono → mono, stereo → stereo, mono → stereo. A mono output
  carries the mid of the wet image ((L + R)/2), so mono = mid of stereo
  (measured 4.5·10⁻⁸). Mono → stereo equals stereo with a copied input
  (measured identical).
- **Sanitising**: NaN/Inf at the input become 0 before anything (metering
  included); the tape write is guarded again; denormals are flushed
  (`ScopedNoDenormals`).

## 3. MODE and the heads

### 3.1 Mapping [SOURCE_CONFIRMED heads, RESEARCH F11 for the 8 positions]

| MODE | heads | notes |
|---|---|---|
| Reverb | none | spring only (REVERB VOLUME); ECHO VOLUME has no effect |
| 1 / 2 / 3 | one head | |
| 1+2, 1+3, 2+3, 1+2+3 | several | |

The hardware's 12 positions = these heads with REVERB VOLUME 0 (echo only)
or > 0 (echo + reverb). REVERB VOLUME adds the spring in every MODE
[OWNER-DELEGATED mapping, F11]. Heads switch with a 30 ms two-stage gain
glide (no click; tested).

### 3.2 Head levels [APPROXIMATION]

Each active head is mixed at **1/√N** (N = active heads), so the echo's
power stays the same when heads are added. The feedback sums the heads at
**1/N** (the loop scale is another 1/√N): the loop gain at a given INTENSITY
is the same in every MODE. Measured: MODE 1+2, head-1 echo of a 0.5 burst =
0.330 (1/√2 × 0.5 × the playback chain at 1 kHz).

### 3.3 Pans and STEREO [TLEET_EXTENSION]

Active heads are spread evenly over [−1, +1] in head order (N = 1: centre;
2: −1, +1; 3: −1, 0, +1). The echo is built as mid M (the sum) and side S
(the pan-weighted sum); the springs add M = (A + B)/2, S = (A − B)/2. STEREO
sets the width w: **L = M + w·S, R = M − w·S**, so (L + R)/2 = M at any
width (mono-compatible by construction). STEREO 0 % = mono wet.

## 4. The tape and the time system

### 4.1 One tape, three heads [SCHEMATIC_OR_MEASURED mechanism, SOURCE_CONFIRMED ratios]

`TapeTransport`: the record head writes one sample per host sample into a
ring. The tape speed v (head-1 distances per sample) is constant over each
control block, and the tape position at each block start is stored in double
precision, so the position P(t) is exact and piecewise linear. Head h (at
distance r_h from the record head) plays, at sample n, the sample recorded
when the tape was at P(n) − r_h:

```
find t_w with P(t_w) = P(n) − r_h + comp·v     (block search with a cursor; binary search fallback)
head h = x(t_w)                                 (band-limited fractional read, 4.6)
```

- **r = 1 : 1.90 : 2.75** (S2/S3 delay ranges; RESEARCH F1)
  [SOURCE_CONFIRMED ranges, OWNER-DELEGATED rounding].
- The delay of every head follows the whole speed history: a speed change
  bends the pitch of what is already on the tape; the read position never
  jumps; head times keep their ratio at any speed (tested at 69, 177, 500 ms,
  FREE and SYNC; telemetry = measured within 0.01 ms).
- Wow and flutter modulate the one shared speed (7.1), so all heads move
  together, as on a tape.

### 4.2 FREE, SYNC, TAP [SOURCE_CONFIRMED formula (master spec 13.8), TLEET_EXTENSION sync]

- **FREE**: head 1 = REPEAT RATE (20–2000 ms, log, 200 ms at the centre).
- **SYNC**: head 1 = the note value: quarter = 60000/BPM; divisions 1/1 … 1/32
  = 4 … 1/8 quarters; Dotted × 1.5; Triplet × 2/3. BPM = the host's tempo when
  BPM SOURCE is Host and the host reports one, otherwise MANUAL BPM (also as
  the fallback when Host is selected and no tempo arrives;
  `isHostTempoAvailable()` reports which). Heads 2 and 3 follow the tape
  (1.90 and 2.75 × head 1), as S2 does ("quantised … at the leading head");
  they are not forced to note values (master spec: "BPM sync controls the
  virtual tape speed").
- **TAP** (`tap (timeSeconds)`, message thread): taps < 2 s apart form a
  sequence; the interval is the mean of the last ≤ 3 intervals. FREE:
  REPEAT RATE = interval (clamped 20–2000 ms). SYNC: the interval is a quarter
  note: MANUAL BPM = round(60 / interval) (20–300) and BPM SOURCE = Manual.
  Parameters change through the APVTS with begin/end gestures (automation
  records them) [TLEET_EXTENSION, after S4].

### 4.3 Limits [TLEET_EXTENSION]

Head 1 is limited to **10 ms … 6 s** (6 s = a whole note at 40 BPM; head 3
then sits 16.5 s back on the tape). A SYNC note value outside is clamped and
reported (`isSyncTimeClamped()`, `getTargetHead1Ms()`). Measured: 1/1 at
40 BPM lands at exactly 288000 samples at 48 kHz. Tape memory: 4.5 MB at
44.1/48 kHz, 9 MB at 96 kHz, 18 MB at 192 kHz (power-of-two ring, allocated
in `prepareToPlay`).

### 4.4 Timing reference and compensation

The echo time is defined as the **group delay at 1 kHz** from the plugin
input to the echo output (a broadband pulse's peak would sit at a higher
effective frequency, where the head-loss lowpass delays less). The tape delay
is shortened by

```
comp = record-stage oversampler latency (23 host samples below 80 kHz, else 0)
     + group delay of the playback chain at 1 kHz (LF roll-off, head loss, BASS, TREBLE)
```

recomputed every control block from the filters that run, and glided per
sample (a moving chain delay is a smooth tape-delay change, never a jump).
Measured with a Gaussian 1 kHz burst (envelope peak of the complex
correlation): **worst error 0.015 samples** over every division × feel at
120 BPM (quarter = 500 ms = sample 24000 at 48 kHz), Host and Manual, 44.1 /
48 / 96 kHz; 0.007–0.03 samples at 44.1–192 kHz, blocks 1–2048. Tolerance in
the tests: 0.25 sample.

### 4.5 The motor: speed changes glide [APPROXIMATION, RESEARCH F3]

ln(head-1 delay) follows its target through two cascaded one-pole stages of
**60 ms** each, per control block (critically damped, no overshoot). A tempo
change 120 → 140 BPM settles within 1 % in 523 ms with at most 0.40 ms delay
change per 128-sample block; REPEAT RATE 250 → 180 ms: 493 ms, 0.88 ms. The
echo bends in pitch while the motor moves; there is no click (8 kHz
high-pass detector: 9.6·10⁻⁶ during the change vs 2.2·10⁻⁶ steady).

### 4.6 Fractional read [APPROXIMATION: numerical method]

48-tap Kaiser-windowed sinc (β 7.5, cutoff fs/2), 512 tabulated phases,
linear between phases, each phase normalised to unity DC gain. Flat within
0.01 dB to 0.42 fs and zero phase at every fraction. (A 4-point Hermite read
loses up to 5 dB at 0.375 fs depending on the fraction; mutation M12 shows
the frequency-response test catches that.)

## 5. Record stage: preamp and tape saturation

### 5.1 Preamp (DRIVE) [APPROXIMATION law, RESEARCH F5]

```
z = x · D / Vc,   s = z / √(1 + z²),   out = (Vc / D) · (s + e2 · HP5(s²))
D = 10^((24 dB · DRIVE/10 + driveOffset) / 20)
```

Small-signal gain exactly 1: DRIVE changes the colour, not the level
(measured spread over CHARACTER × DRIVE at −60 dBFS: 0.009 dB). The even
term (single-ended transistor stage) is high-passed at 5 Hz, so it adds no
DC.

### 5.2 Tape [APPROXIMATION law, RESEARCH F5]

Pre-emphasis high shelf +E dB at 3 kHz → **Lt · u/√(1 + u²)**, u = x/Lt →
the exact inverse de-emphasis shelf. Odd and level dependent; HF (emphasised)
saturates first: at −6 dBFS (Original, DRIVE 0) 3 kHz comes back 0.89 dB
lower than 400 Hz. The tape saturator sits on the record sum (input +
feedback) and bounds the loop (6.4). Lt = voicing Lt × condition × AGE
(7.4, 7.5), interpolated per sample.

### 5.3 CHARACTER [TLEET_EXTENSION voicings, level-matched]

| CHARACTER | drive offset | preamp Vc | even e2 | tape Lt | emphasis E |
|---|---|---|---|---|---|
| Clean | 0 dB | 4.0 | 0.002 | 1.4 | 3 dB |
| Original | 0 dB | 1.0 | 0.04 | 0.7 | 6 dB |
| Burn | +6 dB | 0.6 | 0.08 | 0.45 | 9 dB |

The voicing glides (two stages × 15 ms) per sample, so switching CHARACTER
is click-free (tested).

### 5.4 Oversampling and aliasing [APPROXIMATION: numerical method]

The record stage runs at **2× below 80 kHz** (88.2/96 kHz) and 1× above
(the record rate is ≥ 88.2 kHz everywhere). Per-sample polyphase half-band
FIR, 47 taps, Kaiser β 8 (−80 dB stopband), linear phase; up 23 + down 23
samples at 2× = exactly 23 host samples, taken off the tape delay (4.4).
Measured largest non-harmonic component in the first echo (dBc):

| case | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Original DRIVE 3, 5 kHz −6 dBFS | −87.5 | −100.2 | −99.9 |
| Original DRIVE 10, 1.1 kHz −12 dBFS | −125 | −157 | −161 |
| Burn DRIVE 10, 1.1 kHz −12 dBFS | −75.9 | −81.1 | −81.3 |
| Burn DRIVE 10, 7 kHz −3 dBFS (worst case) | −22.7 | −23.0 | −22.9 |

The worst case is a near-square preamp (≈ +36 dB into the clipper) on a
7 kHz tone: its 11th/13th harmonics fold back from above the record Nyquist.
4× or ADAA would lower it at extra cost (open owner decision, section 15).
SMPTE IMD (60 Hz + 7 kHz 4:1, Original DRIVE 6): 78 % — the preamp
compresses strongly at that drive, by design.

### 5.5 Calibration: THD of the first echo (400 Hz, 48 kHz, MODE 1, no feedback)

THD (%) by input level (dBFS); 0 VU = −18 dBFS:

| CHARACTER / DRIVE | −40 | −30 | −18 | −12 | −6 | 0 |
|---|---|---|---|---|---|---|
| Clean 0 | 0.0007 | 0.007 | 0.11 | 0.43 | 1.6 | 5.3 |
| Clean 3 | 0.0012 | 0.010 | 0.16 | 0.63 | 2.3 | 7.2 |
| Clean 10 | 0.021 | 0.20 | 2.8 | 8.4 | 18 | 28 |
| Original 0 | 0.020 | 0.073 | 0.62 | 2.1 | 6.6 | 15 |
| **Original 3 (default)** | 0.047 | 0.17 | **1.44** | 4.6 | 12 | 22 |
| Original 10 | 0.44 | 2.9 | 18 | 28 | 36 | 40 |
| Burn 0 | 0.13 | 0.46 | 3.1 | 8.4 | 18 | 27 |
| Burn 3 | 0.31 | 1.2 | 8.6 | 18 | 28 | 35 |
| Burn 10 | 3.5 | 15 | 34 | 39 | 42 | 42 |

Original DRIVE 3 at −18 dBFS: H2 −45.3 dBc, H3 −37.5 dBc. Targets
(APPROXIMATION, open task R4): Clean ≪ Original (< ¼), Burn > 2 × Original at
0 VU; THD rises with level and DRIVE. Full table: `test-output/ECHO_thd.csv`.

## 6. Feedback loop and self-oscillation

### 6.1 The spec's order and the model's [deviation, documented]

Master spec: head → HPF → LPF → tape saturation → wow/flutter → feedback gain
→ record stage. The model:

```
head → playback amp (LF roll-off, head loss, BASS, TREBLE) → [LOOP HPF → LPF] → × INTENSITY
     → (+ preamp output) → tape saturation (record) → TAPE (wow/flutter = transport speed)
```

- HPF → LPF in the loop: as specified (LOOP mode).
- **Tape saturation after the gain**, at the record head, shared with the
  input: on a tape machine the feedback is re-recorded together with the new
  input, and the tape saturates on the sum. Placing it after the gain is what
  makes INTENSITY drive the tape into saturation and bounds self-oscillation
  (6.4).
- **Wow/flutter is the transport**, not a block in the loop: the shared tape
  speed modulates every head and every repeat (each repeat accumulates the
  modulation, as on tape).
- The preamp (DRIVE) carries the new input only; the feedback joins at the
  record sum before the tape saturation (a model choice: DRIVE colours the
  input, INTENSITY drives the tape; the RE-201's actual record-amplifier
  topology is open task R1).

### 6.2 FILTER MODE [master spec definitions]

All three filter instances run continuously; MODE crossfades their weights
(30 ms), so switching is click-free:

- **PRE**: on the input before the record stage (every repeat inherits it once).
- **LOOP** (default): on the recirculation only; the first pass of each head
  is unfiltered, every further repeat is filtered again.
- **POST**: on the echo output only (not the loop, not the spring send).

Measured cumulative decay (5 kHz minus 1 kHz, per repeat, LPF 3 kHz):
**Pre −0.34 dB, Loop −16.6 dB, Post −0.34 dB** (Pre/Post only show the
chain's own per-repeat HF loss; the evaluator agrees within 1 dB).
HPF 20–300 Hz and LPF 1–20 kHz are 12 dB/oct Butterworth TPT SVFs, log glides.

### 6.3 INTENSITY [APPROXIMATION law]

Loop gain = INTENSITY/10 × **1.25** × (1/N head sum) × AGE/condition
degeneration (−0.15 dB per AGE step; Worn −0.6 dB, Magnetic dust −1.8 dB per
repeat) × the chain's response. Measured per-repeat decay at INTENSITY 6
(1 kHz, AGE 0): Clean −2.5 dB, Worn −3.1 dB, Magnetic dust −4.3 dB.

### 6.4 Self-oscillation [SOURCE_CONFIRMED behaviour (S2–S4), APPROXIMATION levels]

Onset (sustained without input): **INTENSITY 8.25** (MODE 1), **8.5** (1+2,
1+2+3). At INTENSITY 10 the oscillation settles where the tape saturation's
describing-function gain cancels the excess loop gain: peaks at 0.93–1.31 ×
Lt, no flat tops, stable: in a 30 s run the level changes by ≤ 0.04 dB
between the window 8–10 s before the end and the last 2 s (test limit 3 dB). Output RMS (echo volume 7): MODE 1 Clean −4.6, Original
−10.7, Burn −14.5 dBFS; 1+2+3 Clean +0.2, Original −5.9, Burn −9.8 dBFS.
Removing the tape saturation (mutation M5) runs away and is caught.

## 7. Playback tone, modulation, noise, condition

### 7.1 Wow and flutter [APPROXIMATION, OWNER-DELEGATED, RESEARCH F9]

Speed deviation m(t) per control block:

```
m = wowA · (sin 2π·0.55 Hz·t + 0.35 · drift)
  + flutterA · (sin 2π·6.5 Hz·t + 0.35 · sin 2π·11.3 Hz·t + 0.30 · random)
  + irregular · scrape (band-limited noise 3–25 Hz)
wowA = WOW% · 0.6 % + condition, flutterA = FLUTTER% · 0.2 % + condition; |m| ≤ 5 %
```

Measured on the head-1 delay trace (1 kHz tone, head 1 at 100 ms, 22 s; the
delay integrates the speed over the tape between the heads, so a line at f
has depth D·A·|sinc(f·D)|): WOW 100 % gives
the 0.55 Hz line at 0.584 ms (expected 0.597 ms);
FLUTTER 100 % gives the 6.5 Hz line at 0.084 ms (expected 0.087 ms); WOW 0 /
FLUTTER 0 / Clean: 7·10⁻¹⁰ ms (none). Cross-talk ≤ 6 % (random drift tails).
The rates are fixed (not tied to tape speed) — owner decision 14.9.

### 7.2 Head loss and tone [APPROXIMATION, OWNER-DELEGATED, RESEARCH F4]

- Head-loss lowpass (2nd order Butterworth): **6.5 kHz × √(177 ms / D1)**,
  D1 clamped to the hardware's 69–177 ms (10.4 kHz at the fastest speed),
  lowered by AGE (0.15 oct per step) and TAPE CONDITION (Worn 0.5 oct,
  Magnetic dust 1.2 oct). Measured: 6500 Hz at 177 ms, 10411 Hz at 69 ms,
  2298 Hz at 177 ms + AGE 10.
- Playback LF roll-off: 2nd order Butterworth high-pass at 40 Hz.
- **BASS** ±10 dB: first-order low shelf, half-gain frequency **150 Hz**.
  **TREBLE** ±10 dB: first-order high shelf, half-gain frequency **4 kHz**.
  On the echo only (S2): the dry and the preamp's spring send are not
  affected. Shelves ramp their
  gain and pole per sample (click-free; mutation M11).
- Every filter has an evaluator built from the running coefficients
  (`toneResponseDb`, `filt::hpLpResponseDb`, `EchoEngine::playbackResponseDb`).
  Measured = evaluator within **0.029 dB** for all HPF/LPF settings (min, mid,
  max, musical) and BASS/TREBLE −10/−5/+5/+10 at 44.1/48/96 kHz (responses
  above −40 dB; below, the record stage's intermodulation of the test
  multitone sets a 0.3 dB floor).

### 7.3 NOISE [APPROXIMATION]

Tape hiss (white, +≈4 dB tilt above 2 kHz) added at the record head, so the
heads, head loss and loop shape it like the signal. NOISE 0 % = none;
0 < NOISE ≤ 100 % = **−80 … −45 dBFS RMS** at the record head (log). TAPE
CONDITION adds its own floor (Worn −72, Magnetic dust −62 dBFS). Default
(NOISE 10 %, Clean): −90.9 dBFS RMS at the output with silence in. NOISE 0 +
Clean: the output is exactly 0.

### 7.4 TAPE CONDITION macro [APPROXIMATION, master spec 13.8]

Offsets added to the user's controls (user controls stay additive on top),
glided over 150 ms:

| | wow (peak) | flutter | irregular (RMS) | hiss | head loss | loop loss | dropouts | tape Lt |
|---|---|---|---|---|---|---|---|---|
| Clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | × 1 |
| Worn | +0.12 % | +0.04 % | 0.03 % | −72 dBFS | −0.5 oct | −0.6 dB | 1 dB | × 0.85 |
| Magnetic dust | +0.25 % | +0.08 % | 0.12 % | −62 dBFS | −1.2 oct | −1.8 dB | 4 dB | × 0.7 |

Dropouts: short level dips where a slow random process (6 Hz) crosses a
threshold. Measured (48 kHz, MODE 1, user WOW/FLUTTER/NOISE/AGE 0):

| | 6.3 kHz re 500 Hz | noise floor | head-1 delay modulation RMS | decay / repeat at INTENSITY 6 |
|---|---|---|---|---|
| Clean | −0.35 dB | none | 0 | −2.5 dB |
| Worn | −6.6 dB | −78.8 dBFS | 0.133 ms | −3.1 dB |
| Magnetic dust | −20.9 dB | −71.7 dBFS | 0.263 ms | −4.3 dB |

"Clean: low wow/flutter/noise/HF loss" (master spec) is met by adding
nothing: the user's WOW/FLUTTER/NOISE/AGE set the clean machine.

### 7.5 AGE [APPROXIMATION]

Per step (0–10): head-loss corner −0.15 oct, loop loss −0.15 dB, tape Lt
−0.3 dB. AGE glides (two stages × 15 ms) so the head-loss corner and the
chain compensation never jump (mutation M10).

### 7.6 DUCK [TLEET_EXTENSION, master spec 13.8]

Detector: peak of the **dry input** (instant attack, 50 ms release). Gain
computer: attenuation = DUCK % × **24 dB** × clamp((env dB + 45)/25, 0, 1)
(none below −45 dBFS, full from −20 dBFS); smoothed with **10 ms attack,
250 ms release**; applied to the whole wet return (echo + spring).
Measured: −24.0 dB during the input at DUCK 100 %; attack to 63 % in 10 ms;
release to 37 % in 370 ms (detector + smoother); −0.29 dB after 1.2 s.
`getDuckAttenuationDb()` reports the current value.

## 8. Spring reverb

### 8.1 Structure [SCHEMATIC_OR_MEASURED structure (S5/S6), APPROXIMATION values]

Per spring, two parallel recirculating chains (RESEARCH F8):

- **C_lf** (low chirps): x + g·fb → M stretched allpasses (a + z^-k)/(1 + a z^-k)
  → 4th-order Butterworth lowpass at 0.92 f_C → delay DL → fb. k =
  round(fs / (2 · 4.4 kHz)): f_C = 4.41 kHz at 44.1, 4.80 kHz at 48, 4.36 kHz
  at 96 kHz.
- **C_hf** (high chirps): x + g·fb → M first-order allpasses (k = 1, a < 0)
  → delay → fb; level 0.12 of C_lf.
- Input high-pass 90 Hz (the tank's driver passes little bass).

| | M_lf | a_lf | DL_lf | g_lf | M_hf | a_hf | DL_hf | g_hf |
|---|---|---|---|---|---|---|---|---|
| spring A | 60 | 0.62 | 41 ms | 0.88 | 30 | −0.62 | 23 ms | 0.86 |
| spring B | 56 | 0.58 | 46.5 ms | 0.88 | 28 | −0.58 | 26.3 ms | 0.86 |

Measured: low frequencies arrive first; the arrival time of each band follows
D = kM(1 − a²)/(1 + 2a cos ωk + a²) within 0.4 ms (300 Hz … 0.85 f_C,
44.1/48/96 kHz). Arrival spread 300 Hz → 0.85 f_C: A 11.7 ms, B 10.7 ms. T60
≈ 2.3 s (26.4 dB/s).

### 8.2 Two springs, stereo [APPROXIMATION]

The RE-201 tank has three springs into one mono pickup (S9). ECHO runs two
decorrelated springs; their sum is the mid, their difference the side
(STEREO, 3.3).

### 8.3 Send, level, gating [APPROXIMATION]

Spring input = preamp output (the dry, as on the hardware) + the echo (after
ECHO VOLUME). Output gain 0.42, then × REVERB VOLUME (2·(v/10)², the same law
as ECHO VOLUME). The springs run only while REVERB VOLUME > 0 and restart
silent (REVERB VOLUME ramps up from 0, so no click). Noise-free (as S2).

## 9. Telemetry and meters (`EchoProcessor`, any thread unless noted)

Exactly as implemented (`PluginProcessor.h`):

```cpp
std::array<dsp::MeterSource, 2> inputMeters, outputMeters;   // [0] = L, [1] = R (mono: both the one channel)
                                                             // input = plugin input; output = after MIX, OUTPUT, Bypass
bool   isHeadActive (int head) const noexcept;               // head 0..2; from the MODE parameter
float  takeHeadActivity (int head) noexcept;                 // peak |head playback| since the last call; 0 when inactive
double getHeadDelayMs (int head) const noexcept;             // actual input→echo delay incl. glides, wow/flutter (per 32 samples)
double getEffectiveBpm() const noexcept;                     // BPM SYNC uses: host (Host + tempo present) or MANUAL BPM
bool   isHostTempoAvailable() const noexcept;                // the host reported a tempo in the last block
bool   isSyncTimeClamped() const noexcept;                   // SYNC note value outside 10 ms .. 6 s
double getTargetHead1Ms() const noexcept;                    // head-1 delay the motor drives to (after clamping)
static double getHeadRatio (int head) noexcept;              // 1, 1.90, 2.75
void   tap (double timeSeconds);                             // message thread; monotonic time stamp
EchoEngine& getEngineForTesting() noexcept;                  // test hook
```

`EchoEngine` (test/editor hooks): `getSampleRate()`, `getRecordOversampling()`,
`getCompensationSamples()`, `getHeadLossHz()`, `getCurrentHead1Samples()`,
`getCurrentSpeedDeviation()`, `getSpring(i)` (`lowChirpDelaySamples`,
`getTransitionHz`, `getStretch`, `getLowLoopDelay`), `getTapeMemoryBytes()`,
`getDuckAttenuationDb()`, `playbackResponseDb(hz)`; free function
`toneResponseDb(hz, bassDb, trebleDb, fs)`. VU ballistics run in the UI
(`dsp::VUBallistics`); 0 VU = a sine at −18 dBFS (`params::alignmentDbfs`).
`getTailLengthSeconds()` = 6 s × 2.75 + 3 s = 19.5 s.

## 10. Parameters (namespace `tleet::echo::params`, version 1, IDs frozen)

| ID | Range / choices | Default | Law | getText examples |
|---|---|---|---|---|
| `mode` | Reverb, 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3 | 1+2 | choice | "1+2", "Reverb" |
| `repeatRate` | 20–2000 ms | 200 ms | exact log, 200 ms at centre | "250 ms", "20 ms", "1.20 s", "2.00 s" |
| `intensity` | 0–10 | 4 | linear | "6.5" |
| `echoVolume` | 0–10 | 7 | linear (gain 2·(v/10)²) | "10.0" |
| `reverbVolume` | 0–10 | 0 | linear (gain 2·(v/10)²) | "0.0" |
| `bass` | ±10 dB | 0 dB | centred, 0.1 dB grid | "+3.0 dB" |
| `treble` | ±10 dB | 0 dB | centred, 0.1 dB grid | "-4.5 dB" |
| `timeMode` | Free, Sync | Free | choice | "Sync" |
| `bpmSource` | Host, Manual | Host | choice | "Manual" |
| `manualBpm` | 20–300 BPM, step 1 | 120 | linear | "128 BPM" |
| `noteDivision` | 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 | 1/8 | choice | "1/8" |
| `feel` | Straight, Dotted, Triplet | Straight | choice | "Dotted" |
| `drive` | 0–10 | 3 | linear (0–24 dB) | "3.0" |
| `age` | 0–10 | 2 | linear | "2.0" |
| `wow` | 0–100 % | 20 % | linear | "35 %" |
| `flutter` | 0–100 % | 20 % | linear | "20 %" |
| `hpf` | 20–300 Hz | 20 Hz | exact log | "20 Hz", "150 Hz" |
| `lpf` | 1–20 kHz | 20 kHz | exact log | "4.5 kHz", "20.0 kHz" |
| `filterMode` | Pre, Loop, Post | Loop | choice | "Post" |
| `stereo` | 0–100 % | 30 % | linear | "30 %" |
| `duck` | 0–100 % | 0 % | linear | "0 %" |
| `mix` | 0–100 % | 35 % | linear | "72.5 %" |
| `tapeCondition` | Clean, Worn, Magnetic dust | Clean | choice | "Magnetic dust" |
| `noise` | 0–100 % | 10 % | linear | "10 %" |
| `character` | Clean, Original, Burn | Original | choice | "Original" |
| `output` | ±12 dB | 0 dB | centred, 0.1 dB grid | "0.0 dB" |
| `bypass` | bool | off | host bypass | "Off"/"On" |

Text parsing: `repeatRate` accepts "250", "250 ms", "1.2 s"; `hpf`/`lpf`
accept "4.5k", "4.5 kHz"; dB values round-trip exactly on the 0.1 dB grid.

## 11. Presets (`presets/*.json`, 14, all 26 parameters each)

Slap Back, Dotted Eighth Vocal, Dub Feedback, Worn Space, Spring Only,
Runaway Siren (oscillates: MODE 1, INTENSITY 8.6, Burn), Tape Wobble,
Quarter Note Throw, Triplet Bounce, Magnetic Dust Haze, Clean Doubler,
Burnt Repeats, Wide Ping, Ducked Vocal Echo. Together they use every
CHARACTER, TAPE CONDITION, FILTER MODE, FEEL and TIME MODE, and the Reverb
MODE. Original names (master spec 9.4).

## 12. Verification (`tests/EchoTests.cpp`, all `ECHO:`; 26 tests)

| Area | Test | Key result |
|---|---|---|
| Registry | IDs, ranges, defaults, texts, buses | 27 parameters, version 1 |
| §15 timing | every division × feel at 120 BPM, Host/Manual, 44.1/48/96 kHz | worst 0.015 samples (tol. 0.25) |
| Head ratios | 1 : 1.90 : 2.75 at 69/177/500 ms, FREE and SYNC | ≤ 0.25 sample; telemetry ≤ 0.01 ms |
| MODE | heads per position from the choice text; activity telemetry | |
| Tempo change | host 120 → 140 BPM, REPEAT RATE 250 → 180 ms while playing | no click; glide 0.5 s |
| SYNC sources / clamp | host, manual, fallback, 6 s clamp | 288000 samples |
| TAP | FREE and SYNC, timeout, averaging | |
| Self-oscillation | INTENSITY 10, 30 s, 3 MODEs × 3 CHARACTERs; onset | bounded by Lt, stable |
| Cumulative spectral decay | Pre / Loop / Post | −0.34 / −16.6 / −0.34 dB per repeat |
| Wow/flutter spectrum | lines at the spec's rates and depths | 0.584 / 0.084 ms |
| DUCK | depth, attack, release | −24 dB, 10 ms, 370 ms |
| Spring dispersion | arrival law, decay | ≤ 0.4 ms, T60 2.3 s |
| Filters (§26) | HPF/LPF min/mid/max/musical, BASS/TREBLE, head loss | ≤ 0.029 dB |
| THD (§27) | level × DRIVE × CHARACTER | table 5.5 |
| TAPE CONDITION | HF loss, noise, modulation, degeneration, additivity | table 7.4 |
| Every control | all 27 parameters change the output, with directions | |
| Switching | 28 control toggles incl. time controls and selectors; random automation | no click; finite |
| Rates/blocks | 44.1–192 kHz × blocks 1–2048 × 3 layouts | finite, 0 allocations, latency 0 |
| Robustness | NaN/Inf, silence, DC, denormals, 20 s tail | silence = 0; tail −132 dBFS |
| Bypass / MIX 0 | every rate and layout | bit-exact |
| Mono/stereo | mono = mid; mono→stereo = stereo | 4.5·10⁻⁸ |
| State | round trip, corrupt and foreign state | identical render |
| Presets | 14, all parameters, coverage, rejects | |
| Aliasing / IMD | table 5.4 | |
| CPU | 44.1–192 kHz, blocks 64/128, MODE 1+2+3 + spring | median 5.0–5.6 % of one core at 44.1/48 kHz; 9.6 % at 96; 19.5 % at 192 |

CSV output (`test-output/`): `ECHO_timing.csv`, `ECHO_head_timing.csv`,
`ECHO_self_oscillation.csv`, `ECHO_feedback_csd.csv`,
`ECHO_wow_flutter_spectrum.csv`, `ECHO_duck.csv`,
`ECHO_spring_dispersion.csv`, `ECHO_filter_responses.csv`, `ECHO_thd.csv`,
`ECHO_switching.csv`, `ECHO_aliasing.csv`.

### 12.1 Mutation checks

Each defect was introduced, the named test failed, and the code was restored
(script: introduce → build → run → restore).

| # | Defect | Caught by |
|---|---|---|
| M1 | head ratios 1 : 2 : 3 | head timing (head 3 off by 200 samples) |
| M2 | no record/chain compensation | §15 timing (+25 samples) |
| M3 | naive digital delay: the read point jumps with REPEAT RATE/tempo | tempo-change click test |
| M4 | LOOP filter weight ignored | cumulative spectral decay |
| M5 | tape saturation removed | self-oscillation (not bounded) |
| M6 | DUCK disconnected | DUCK (0 dB instead of −24 dB) |
| M7 | CHARACTER ignored | THD (Clean < Original < Burn) |
| M8 | spring not dispersive (a = 0) | spring dispersion law |
| M9 | wow at 1 Hz instead of 0.55 Hz | wow/flutter spectrum |
| M10 | AGE not glided (corner and chain delay jump) | switching (AGE click) |
| M11 | BASS/TREBLE shelves stepped per block | switching (BASS click) |
| M12 | 4-point Hermite tape read | filter responses (HF error up to 5 dB) |
| M13 | an allocation in processBlock | rates/blocks (allocation counter) |
| M14 | MODE 1+3 wired to heads 2+3 | MODE |

## 13. Not modelled (intentionally)

- The hardware's input section (two MIC inputs, INST input, input volumes,
  peak LED) and its output level switch: one input, DRIVE stands in for the
  preamp gain.
- Spring delay-line modulation and cross-coupling (S6: optional), the
  tank's driver/pickup transducers beyond the 90 Hz input high-pass.
- Tape splice, tape stretch over hours, capstan motor hum, mechanical noise.
- Speed-dependent wow/flutter rates (they are fixed in Hz; 14.9).
- The hardware's MODE positions as 12 detents (8 + REVERB VOLUME, F11).

## 14. Owner-delegated decisions (closest to the real RE-201 within the spec and the registry)

1. **Head spacing 1 : 1.90 : 2.75** from the licensed RE-201 delay ranges
   (S2/S3), not the RE-202's 1 : 2 : 3 (F1).
2. **Tape speed range**: the hardware's head-1 range is 69–177 ms; the model
   keeps one tape speed over 10 ms – 6 s (registry REPEAT RATE 20–2000 ms and
   SYNC); the head-loss tone is held at the hardware's ends outside
   69–177 ms (F2, 7.2).
3. **Motor**: 2 × 60 ms log-domain glide; echoes bend in pitch (F3, 4.5).
4. **Tone**: BASS 150 Hz / TREBLE 4 kHz first-order shelves, ±10 dB (registry);
   playback LF roll-off 40 Hz; head loss 6.5 kHz at 177 ms ∝ √speed (F4).
5. **Saturation**: preamp soft clip + tape saturation with 3 kHz emphasis;
   Original = the RE-201-like voicing (1.44 % THD at 0 VU, DRIVE 3);
   Clean and Burn level-matched variants (5.3, 5.5).
6. **Self-oscillation onset** ≈ INTENSITY 8.25–8.5 (the hardware oscillates
   "fully clockwise" and earlier in multi-head modes; F6).
7. **Spring**: two S5-structure springs, f_C ≈ 4.4 kHz, T60 ≈ 2.3 s,
   input 90 Hz high-pass, fed by the preamp output + echo (F7, 8).
8. **Head mixing 1/√N** per head, loop 1/N (3.2).
9. **Wow/flutter**: 0.55 Hz wow, 6.5 + 11.3 Hz flutter, fixed rates; WOW 100 %
   = 0.6 %, FLUTTER 100 % = 0.2 % peak; defaults 20 % (0.12 % / 0.04 %, a
   serviced machine with a servo motor, S10) (F9).
10. **REVERB VOLUME active in every MODE** (the hardware's modes 1–4 have no
    reverb; here that is REVERB VOLUME 0, the default) (F11).
11. **Defaults** (registry): MODE 1+2, REPEAT RATE 200 ms (just beyond the
    hardware's 177 ms slowest head-1 time: the registry's knob centre),
    INTENSITY 4, ECHO VOLUME 7, REVERB 0, DRIVE 3, AGE 2, WOW/FLUTTER 20 %,
    NOISE 10 %, CHARACTER Original, TAPE CONDITION Clean, FILTER MODE Loop.
12. **TAPE CONDITION Clean adds nothing** (the user's controls describe the
    clean machine); Worn/Magnetic dust per 7.4.
13. **MIX law linear** (bit-exact at 0 %, pure return at 100 %) (2).
14. **TAP in SYNC** treats the tapped interval as a quarter note and switches
    BPM SOURCE to Manual (F12).

## 15. Open measurement tasks and owner decisions

- **R1–R6** (RESEARCH.md): MODE table and tone frequencies from S1; head
  delays, playback response and tone curves, wow/flutter spectrum, THD and
  oscillation onset, spring impulse response and motor ramp from a serviced
  RE-201.
- **Aliasing at extreme settings** (5.4): Burn DRIVE 10 on a hot 7 kHz tone
  gives −23 dBc fold-back. Options: 4× record stage (more CPU; not
  measured), or first-order ADAA in the two saturators (half-sample delay to compensate).
  Musical program (≤ −12 dBFS, ≤ 1–5 kHz) is at −76 dBc or better.
- **Self-oscillation level of Clean with three heads** (+0.2 dBFS RMS at
  ECHO VOLUME 7, INTENSITY 10): accepted as "the loudest voicing"; lower the
  Clean tape Lt (1.4) if the owner wants it below 0 dBFS.
- **CPU at 192 kHz** (19.5 % of one core): the springs and the tape read run
  at the host rate; running the springs at a decimated rate would halve it.
- **12 vs 8 MODE positions** (F11): the registry is fixed at 8; confirm that
  REVERB VOLUME stands in for the echo/echo + reverb distinction.
- **BASS/TREBLE frequencies** and the **±10 dB** range: confirm or replace
  once S1 or a schematic is readable.
