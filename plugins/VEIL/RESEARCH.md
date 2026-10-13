# VEIL — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
with its source and how strong that source is. Where sources disagree, the
conflict and the decision are written down here and are not repeated in the
model spec.

VEIL is informed by the **Manley Laboratories Stereo Variable Mu®
Limiter/Compressor** (1994 onward; standard, Mastering and T-Bar variants,
the current units with the HP SC switch). It is not a measured 1:1 clone of
one unit: no unit was measured for this project, no schematic was used, and
every constant Manley does not publish is an `APPROXIMATION` (MODEL_SPEC
section 11 lists what to measure).

All sources were accessed on 2026-10-13. The current Manley manual PDF
(<https://www.manley.com/s/VARIABLE-MU-MANUAL-WEB-REV-521.pdf>, the master
spec's reference) could not be downloaded from this build environment (the
host refused the connection); its content was taken from the Manley product
page (S2), the older owner's manual (S1, a public mirror of Manley's own
manual) and the Manley-endorsed UA manual (S3). Re-checking S1's figures
against rev 5.21 is open task R1.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — manufacturer | Manley manuals and product pages | `SOURCE_CONFIRMED` |
| A' — licensed | the manual of the Manley-licensed software model (Universal Audio, developed with Manley) | `SOURCE_CONFIRMED` for control ranges, context for behaviour |
| B — engineering | peer-reviewed / textbook valve and compressor theory, standards | `SCHEMATIC_OR_MEASURED` / basis of `APPROXIMATION`s |
| D — editorial / dealer | dealer articles, forums | context only, never the sole basis for a law |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | Manley Laboratories, *Owner's Manual, Stereo "Variable-Mu" Limiter Compressor* (older revision, public mirror) — <https://www.barryrudolph.com/recall/manuals/manleyvarimu.pdf> | A | ATTACK continuously variable, **Fast 25 ms, Med (12 o'clock) 50 ms, Slow 70 ms**; Mastering version 11 steps, **≈15 ms .. 90 ms**. RECOVERY **0.2 s, 0.4 s, 0.6 s, 4 s, 8 s** (very fast .. very slow). COMPRESS **≈1.5:1, soft knee**; LIMIT **4:1**, "At greater than 12dB of limiting the ratio increases (up to 20:1 maximum)", the knee "softens as more limiting is used". Onset of LIMIT −20..+10 dBm, of COMPRESS −10..+10 dBm. 5670 "variable-mu double triode operating in a fully balanced or symmetrical circuit" (the 6386 original "now too rare"); the side-chain is "technically called a feedback circuit"; "Vacuum tube audio rectification for side chain" (12AL5). LINK: "We combine the 'DC' control signals not the audio", both channels get the same GR, meters stay independent; linking a LIMIT channel with a COMPRESS channel "will not work". 0 VU = **1.228 V** (+4 dBu). Max output **+30 dB (26 V RMS), 26 dB headroom**; max input +36 dB for 1 % THD. Frequency response ±1 dB 20 Hz..20 kHz. THD **0.1 % at unity, 100 Hz..3 kHz typical**, 0.2 % 20 Hz..10 kHz. "entirely differential which tends to cancel distortion and hum"; handles signals "without introducing harmonic distortion — unless the operator / engineer wishes to do so"; "Distortion can be creatively used by turning up the INPUT and turning down the OUTPUTs". INPUT attenuator before the input transformer; OUTPUT a T-pad after the output transformer. |
| S2 | Manley Laboratories, *Variable Mu® Stereo Limiter Compressor* product page and spec sheet — <https://www.manley.com/products/pro-audio/dynamics/variable-mu> | A | "HP SC: High Pass Side Chain Filter **−3 dB @ 100 Hz**" (side-chain only). ATTACK **25–70 ms**; RECOVERY **0.2, 0.4, 0.6, 4, 8 s**. COMPRESS **1.5:1 soft knee**; LIMIT **4:1 to 20:1**, sharper knee. Tubes: 5670 ×2 (or 6BA6 ×4, T-Bar mod) variable gain, 5751/12AX7 amplifier, 12BH7 (earlier 7044/5687) output, **12AL5 ×2 side-chain rectifiers**. Max output +30 dBu, 26 dB headroom, **THD < 0.1 % @ 1 kHz**, 20 Hz–25 kHz ±0.1 dB, gain up to 35 dB, noise −85 dB typ. GR is obtained by re-biasing the gain tube; "the curve gets gentler" (softer) deeper into compression (wording of the page, see F2). |
| S3 | Universal Audio, *Manley Variable Mu Manual* (plug-in developed with Manley, models a 6BA6 T-Bar unit) — <https://help.uaudio.com/hc/en-us/articles/18737967080340-Manley-Variable-Mu-Manual> | A' | ATTACK continuous **70 ms (slow) .. 25 ms (fast)**; RECOVERY **8 s, 4 s, 0.6 s, 0.4 s (default), 0.2 s**. LIMIT "starts at 4:1 and rises to 20:1 once limiting exceeds 12 dB", knee sharper but softening with more limiting; COMPRESS "1.5:1 with a soft knee". HP SC **−3 dB at 100 Hz, 6 dB/octave**, side-chain only, against bass pumping. Sidechain LINK "forces both channels to compress by equal amounts". Dual-triode, remote-cutoff gain reduction "constantly re-biased by the sidechain control voltages". INPUT: "higher settings add more colour, lower settings sound more transparent". States the unit "can never achieve greater than −10 dB of gain reduction" (see F2, conflict). Dry/Wet mix and Headroom are plug-in-only features. |
| S4 | Access Analog, *The Manley Variable Mu Explained* — <https://accessanalog.com/manley-variable-mu-vari-mu-mastering/> | D | Restates S1–S3 (feedback detector, ratio rising with GR, 1.5:1 / 4:1→20:1, HP SC 6 dB/oct −3 dB @ 100 Hz, 12AL5 rectifiers re-biasing a remote-cutoff tube); v4 (T-Bar 6BA6) "less squashing" with more limiting; push-pull cancels much even-order content (attributed to H. Robjohns). Context only. |
| S5 | Historyofrecording.com, *Fairchild 660/670* specifications — <https://historyofrecording.com/Fairchild_660.html> | D | The vari-mu ancestor: 6386 remote-cutoff tubes in a "single push-pull stage … with an extremely high control voltage", ratio range 1:2 .. 1:30 set by the threshold controls, distortion < 1 % "at limiting with +12 dBm output". Supports F1/F3 qualitatively. |
| S6 | Wikipedia, *Cut-off (electronics)* (remote cut-off / variable-mu / super-control valves) — <https://en.wikipedia.org/wiki/Cut-off_(electronics)> | D | A remote-cutoff grid is a helix of **variable pitch**, so the valve "can operate with more negative grid voltages, with reduced amplification, before it is completely cut off"; used for AGC. |
| S7 | S. Ballantine, H. A. Snow, *Reduction of distortion and cross-talk in radio receivers by means of variable-mu tetrodes*, Proc. IRE 18 (12), Dec. 1930 (bibliographic reference confirmed via IEEE Cincinnati, <https://ieeecincinnati.org/2006/09/01/charles-stewart-ballantine/>) | B | The origin of the variable-mu valve. The textbook result attributed to it (F.E. Terman, *Radio Engineers' Handbook*, 1943, AVC section): for least cross-modulation the transconductance should vary **exponentially** with grid bias, i.e. the stage gain in dB falls about **linearly** with the control voltage, steepening near cutoff. The original paper was not re-read for this project (the IRE archive is not reachable here); used as the physical basis of the APPROXIMATION in F3. |
| S8 | D. Giannoulis, M. Massberg, J. D. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, J. Audio Eng. Soc. 60 (6), 2012 | B | Feed-forward vs feedback detection; branching (diode-steered) attack/release smoothing in the log domain; quadratic soft knee. |
| S9 | EBU R 68, *Alignment level in digital audio production equipment* | B | **0 dBu ≙ −18 dBFS**. Threshold and VU calibration (F7). |
| S10 | AES17, *Measurement of digital audio equipment* | B | dBFS of a sine = its peak (RMS + 3.01 dB). The detector level convention. |
| S11 | IEC 60268-17, *Standard volume indicators* (VU) | B | VU ballistics (already implemented in `shared/dsp/VUBallistics.h`). |

## Findings and decisions

### F1 Topology: feedback detector, rectified control voltage, re-biased gain valve

All manufacturer sources agree (S1, S2, S3): the side-chain is a **feedback**
circuit (it reads the output of the gain stage); a valve rectifier (12AL5)
turns the audio into a DC control voltage through the attack/recovery timing
network; that voltage re-biases a balanced pair of remote-cutoff triodes
(5670, or 6BA6 in the T-Bar units). There is no ratio control: the ratio is
a property of the valve curve and of the side-chain gain.

Decision: VEIL's gain law is written in the feedback form (the GR is a
function of the *output* level, `x = y + G(e(y))`, MODEL_SPEC 2), solved
exactly. [SOURCE_CONFIRMED topology]

The **dynamics** are not simulated as a closed loop: the steady GR the loop
would settle at is computed for the current detector level and the
attack/recovery network then moves the control voltage (GR in dB) toward it
with exactly the ATTACK / RECOVERY time constants. Reasons: (a) the skin
shows ATTACK and RECOVERY as times; with a literal loop the GR would move
up to the local ratio times faster than the knob reads (20× at full
COMPRESS), (b) a two-pole loop (rectifier smoothing + timing capacitor) with
a loop gain of up to 20 rings, which the hardware's 25 ms minimum attack
avoids but VEIL's 10 ms minimum would not. The static curve is the
hardware's feedback curve; only the loop acceleration is not modelled.
[APPROXIMATION, deliberate; open task R3]

### F2 Ratio grows with gain reduction

S1, S2, S3 agree: COMPRESS ≈ 1.5:1 soft knee; LIMIT 4:1 rising to **20:1
past ~12 dB of limiting**; the knee softens with more limiting (S1, S3).
S2 says the curve "gets gentler" deeper into compression; read in context
(and against S1/S3, which are explicit about 4:1 → 20:1), this describes the
knee becoming rounder, not the ratio falling. Decision: the ratio rises
monotonically with GR depth in every COMPRESS setting (master spec 13.6
requires it) and the knee is wide. [SOURCE_CONFIRMED trend; APPROXIMATION
shape]

Why it grows (F3): the rectifier makes a control voltage proportional to the
output *amplitude* above the threshold bias, while the valve's gain in dB
falls about linearly with that voltage. dB of GR per volt × volts per dB of
output rises exponentially with the output level, so each extra dB of output
costs more GR: the local ratio is `1 + 0.1151 (1 + e)(K + λ·GR)` above the
knee (MODEL_SPEC 2.3), linear in the GR for a pure exponential valve (λ = 0)
and steeper toward cutoff (λ > 0).

Conflict: S3 says the plug-in "can never achieve greater than −10 dB of gain
reduction, as with the hardware", while S1 and S3 itself describe the ratio
change "past 12 dB of limiting". The 10 dB figure is most likely the GR meter
scale / the practical range at the reference levels. Decision: VEIL does not
cap the GR at 10 dB (the law's floor is 60 dB, a valve near cutoff); with the
hardware-like COMPRESS 10 law the GR grows only slowly beyond ~12 dB anyway.
[APPROXIMATION; open task R2]

Calibration of the COMPRESS macro (a TLEET_EXTENSION, master spec 13.6):
COMPRESS 10 reproduces the LIMIT figures (onset 4:1, ~19:1 at 12 dB GR,
measured in MODEL_SPEC 2.4); the hardware's COMPRESS (≈1.5:1 at light GR)
sits near COMPRESS 3; COMPRESS 0 is gentler than the hardware (onset
1.25:1). Intermediate settings interpolate the side-chain gain (log) and the
cutoff curvature (quadratic), so the variable-mu shape is kept everywhere.

### F3 The remote-cutoff valve law

S6 (variable-pitch grid) and S7 (Ballantine & Snow; the exponential
transconductance characteristic) give the classic remote-cutoff behaviour:
gain in dB roughly proportional to the bias over most of the range, falling
faster near cutoff. VEIL's tube law is `GR = (K/λ)(exp(λe) − 1)` of the
rectified excess `e` (linear in e for λ → 0, faster than linear for λ > 0).
No published curve of the 5670 / 6BA6 in this circuit was found.
[SCHEMATIC_OR_MEASURED principle (S7); APPROXIMATION constants]

### F4 Attack and recovery

Hardware: attack continuous **25–70 ms** (standard), **15–90 ms** (Mastering);
recovery stepped **0.2, 0.4, 0.6, 4, 8 s** (S1, S2, S3). VEIL's skin has
continuous knobs with wider ranges, ATTACK **10..1000 ms** (centre 100 ms)
and RECOVERY **50..2000 ms** (centre 400 ms) (owner decision, TLEET
extension of the ranges). The hardware's 0.2, 0.4 and 0.6 s positions are
inside VEIL's range; 4 s and 8 s are not (the skin stops at 2 s).

Defaults: ATTACK **50 ms** (the hardware's 12 o'clock "Med", S1);
RECOVERY **400 ms** (UA's default, S3, and the knob centre).

How the hardware times are defined is not published (RC constants of the
timing network are the usual meaning). VEIL defines both as **exact
exponential time constants of the control voltage, which is the GR in dB**
(MODEL_SPEC 3) — for an exponential valve (F3) the bias voltage and the GR
in dB are proportional, so this is the timing network's own constant.
[SOURCE_CONFIRMED ranges/positions; APPROXIMATION definition]

### F5 Stereo link

S1: "We combine the 'DC' control signals not the audio"; both channels get
the same GR in dB; meters stay independent. S3: linking forces equal
compression. With two diode rectifiers charging tied control lines, the
larger control voltage wins (the other diode is reverse biased). Decision:
**linked = both channels take the larger of the two control voltages**
(each channel keeps its own detector and timing network); **DUAL MONO =
independent channels**. The shared-capacitor timing interaction of a real
tied network (a single side charging both capacitors more slowly) is not
modelled. [SOURCE_CONFIRMED link of the DC control; APPROXIMATION max and
timing]

### F6 Tube stages and how they colour with level

- Input stage (5751/12AX7 amplifier): S1 "turning up the INPUT … distortion
  can be creatively used"; S3 "higher [input] settings add more colour".
  Colour therefore depends on the level into the valves (TUBE DRIVE).
- Gain element: balanced/differential ("tends to cancel distortion", S1),
  so even orders largely cancel: odd-order dominant, rising with the bias
  (deeper GR moves the operating point toward the curved region of the
  remote-cutoff characteristic, S6/S7; the Fairchild's distortion is quoted
  "at limiting", S5).
- Output stage (12BH7 / 7044 push-pull, transformer): symmetric, odd order;
  +30 dBu maximum output (26 dB above +4 dBu) (S1, S2).
- Calibration anchors: **THD < 0.1 % at 1 kHz** at the nominal level (S2),
  0.2 % 20 Hz..10 kHz typical (S1), **1 % at +36 dBu in** (S1).

No harmonic spectrum of the unit is published. Low-order (2nd/3rd) colour
with level is the common description (S3, S4) and what a triode stage and
a balanced remote-cutoff pair produce. All stage constants are
APPROXIMATIONs (MODEL_SPEC 6), measured by the tests (THD tables in
MODEL_SPEC 6.5).

### F7 Levels: threshold and VU calibration

0 VU = 1.228 V = +4 dBu (S1). The digital alignment of the analogue
reference is a converter choice: EBU R68 puts 0 dBu at −18 dBFS (S9), the
usual plug-in convention puts 0 VU at −18 dBFS. Decision: **0 VU = a sine at
−18 dBFS** (AES17, S10) and the THRESHOLD knob's 0 dB is the same
−18 dBFS, so the −20..+20 dB travel covers −38..+2 dBFS. The hardware's
LIMIT onset range (−20..+10 dBm around +4 dBu) maps to −42..−12 dBFS at that
alignment; VEIL's range is shifted up by 10 dB to give mastering headroom
and add the INPUT knob for the rest. [SOURCE_CONFIRMED 0 VU; APPROXIMATION
digital alignment]

### F8 Side-chain high-pass

S2/S3: HP SC is −3 dB at **100 Hz**, **6 dB/oct**, side-chain only. VEIL's SC
HPF is continuous, 20 (Off) .. 300 Hz with 80 Hz at the knob top (owner
decision, skin). Slope: **12 dB/oct Butterworth**, steeper than the
hardware's switch so the useful range up to 300 Hz really removes kick
energy from the detector (a first-order filter at 80 Hz only removes 4 dB
at 50 Hz). [TLEET_EXTENSION, documented departure from S3's 6 dB/oct]

### F9 Audio-path HPF (skin knob "HPF")

Not on the hardware. Owner decision (2026-10-13): an audio-path high-pass at
the plugin input, 20 (Off) .. 300 Hz, 100 Hz at the knob top, default Off,
12 dB/oct Butterworth; before the input tube stage and the internal detector
tap; not on the DRY/WET dry path. [TLEET_EXTENSION, owner-approved]

### F10 Coupling and frequency response

The audio path is transformer coupled (S1, S2: Manley IRON input and output
transformers), flat 20 Hz–25 kHz ±0.1 dB (S2). VEIL models the input
coupling as a first-order high-pass at **2 Hz** (−0.04 dB at 20 Hz), which
also keeps DC out of the detector, and the output transformer's low-end
"weight" as a CHARACTER voicing (MODEL_SPEC 6.4). [APPROXIMATION]

## Open research tasks

- R1 Re-read Manley's current manual (rev 5.21, the master spec's link) and
  check every figure taken from S1 against it.
- R2 Resolve the "never more than 10 dB of GR" statement of S3 against S1.
- R3 Measure a unit's attack and recovery curves at several GR depths to
  decide whether the loop acceleration of the feedback side-chain should be
  modelled (F1).
- R4 Measure the static curve (COMPRESS and LIMIT, several thresholds) and
  the harmonic spectrum vs level of a unit to replace the constants of
  MODEL_SPEC 2.3 and 6.
