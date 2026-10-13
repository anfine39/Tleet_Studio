# PUNCH — research notes

This file lists the evidence behind `MODEL_SPEC.md`. Each claim the model
relies on is given with its source and the strength of that source. Where
sources disagree, the conflict and the decision are recorded here and not
repeated in the model spec.

PUNCH is informed by the **dbx 160 family** of feed-forward VCA compressors:
the 160 (1975), the 165/165A (1980/82, which introduced OverEasy), the 160X
(1982), and in particular the **dbx 160A** (1995), whose manual and data
sheet are the primary references. PUNCH is not a measured 1:1 clone of one
unit, because no unit was measured for this project (see "Open measurement
tasks" in MODEL_SPEC).

All sources were accessed on 2026-10-12.

## Source classes

| Class | Meaning | Tag it supports |
|---|---|---|
| A — manufacturer | dbx manuals and data sheets | `SOURCE_CONFIRMED` |
| B — component maker | THAT Corporation data sheets, design briefs and AES papers. THAT was formed from dbx's VCA/RMS detector IC business and documents the same detector and VCA principles | `SCHEMATIC_OR_MEASURED` |
| C — transcribed documents | spec transcriptions of original brochures and manuals | `SCHEMATIC_OR_MEASURED` when they agree with class A |
| D — history / review | dealer pages, histories, forums | context only, never the only basis for a law |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | dbx, *160A Compressor/Limiter Owner's Manual* 18-2040V-E (printed 10/01/20) — <https://dbxpro.com/en-US/product_documents/dbx160a-18-2040v-cpdf>, which redirects to <https://adn.harmanpro.com/product_documents/documents/721_1602283220/dbx160A_Manual_18-2040V-E_original.pdf> | A | See the S1 list below the table. |
| S2 | dbx, *160A* cut sheet and A&E specification — <https://c3.zzounds.com/media/160ACutSheet-160ae9a0b6f5c925275ab71466c59d56.pdf>, <https://images.static-thomann.de/pics/atg/atgdata/document/specs/106584.pdf> | A | The same timing, ratio, threshold and THD figures as S1. "Stereo Coupling: True RMS Power Summing™". "Strappable with another 160A for true RMS stereo summing operation", "with true RMS summing of input levels". "The compressor attack and release times shall be program dependent." |
| S3 | Vintage Digital, *dbx 160X Compressor* (transcribes the 160X brochure and manual) — <https://www.vintagedigital.com.au/dbx-160x-compressor/> | C | 1982. Gain element is a "wide-range Blackmer voltage-controlled amplifier", the 146732 SIP (dbx 2152 / THAT 2159 equivalent). RMS detector is the 146742 SIP. True RMS detection feeding **feed-forward** circuitry. Ratio is continuous 1:1 → ∞:1 → −1:1 in both hard knee and OverEasy, and in OverEasy "the ratio control sets the maximum ratio". Attack: 15 ms for 10 dB, 5 ms for 20 dB, 3 ms for 30 dB. Release: program dependent, "0 to 500 ms". Maximum compression 60 dB. THD below threshold: 2nd 0.07 %, 3rd 0.07 %. THD above threshold: 2nd 0.07 %, 3rd 0.02 %. Output gain −20..+20 dB. Stereo coupling via a TRS strap cable. |
| S4 | Vintage Digital, *dbx 160 series* — <https://www.vintagedigital.com.au/series/dbx-160/> and *dbx 160A* — <https://www.vintagedigital.com.au/?p=154847> | D | Lineage: 160 in 1975 (true RMS, feed-forward VCA), 165 in 1980 (Over Easy), 160X in 1982 (adds Over Easy to the 160), 160A in 1995. Repeats the S1/S2 160A figures. |
| S5 | THAT Corporation, *THAT 2252 RMS-Level Detector* data sheet — <https://thatcorp.com/datashts/THAT_2252_Datasheet.pdf> | B | Log-domain RMS detector with output "proportional to the log of the true rms value". A single pole is set by the timing capacitor and current. Release is a "straight-line decay" on the log (dB) scale. Attack follows 1 − e^(−t/τ) in the mean square, and "the log transformation steepens the apparent attack shape". "Separate attack and release adjustments are not possible within the constraint of rms response." |
| S6 | THAT Corporation, *THAT 4301 Analog Engine* data sheet — <https://thatcorp.com/datashts/THAT_4301_Datasheet.pdf> | B | RMS time constant ≈ 0.026·C_T/I_T, "~35 ms in the circuit shown". "the attack and release time constants are fixed in their relationship to each other". The VCA control is −6.5 mV/dB, and the detector output is likewise in mV per dB. Compression ratio is set by scaling the detector's dB-linear output into the VCA control, and infinite compression holds the output constant above threshold. VCA THD is 0.003 % at 0 dB gain; at +10 dBV it is 0.03..0.035 % across ±15 dB of gain. |
| S7 | THAT Corporation, *Design Brief DB203: Basic Compressor/Limiter Design* (THAT 4305) — <https://thatcorp.com/datashts/DB203%20Basic%20Comp-Limiter%20Design%20THAT4305.pdf> | B | A "feedforward approach to gain control". "RATIO = 1/(1−R)", where R is the fractional setting of the COMPRESSION pot: the pot passes a fraction of the dB-linear control voltage. Soft knee: the threshold rectifier is replaced by an open-loop diode, a "'sloppy' half-wave rectification of the RMS detector's output", spanning about 300 mV of diode voltage. RMS integration time is "approximately 32 ms". Time constants are "entirely determined by the RMS detector". |
| S8 | F. Floru (THAT Corp.), *Attack and Release Time Constants in RMS-Based Feedback Compressors*, AES 104th Convention, 1998, preprint 4703 — <https://thatcorp.com/datashts/AES4703_Attack_and_Release_Time_Constants_I.pdf> | B | In a feed-forward RMS compressor the transient response is the detector's own exponential, with a 35 ms example. A feedback topology would be about R times faster. This separates feed-forward (dbx 160) behaviour from feedback behaviour. |

S1 establishes the following (verbatim where quoted):

- "True RMS Level Detection".
- Threshold range "−40dBu to +20dBu".
- Compression ratio "Variable 1:1 to ∞:1 thru to −1:1; >60dB Maximum Compression". The control "covers the entire range from 1:1 to ∞:1 and, in addition, goes to Infinity+ (negative) ratios".
- Attack: "Program-Dependent; Typically 15ms for 10dB, 5ms for 20dB, 3ms for 30dB".
- Release: "Program-Dependent; Typically 8ms for 1dB, 80ms for 10dB, 400ms for 50dB; 125dB/Sec Rate".
- OverEasy: signals "begin to gradually activate the 160A's gain change circuitry as they approach the THRESHOLD reference level". There is "no distinct point at which processing begins", and the threshold lies midway between the onset and full ratio. An amber LED lights in the OverEasy region.
- Hard knee is the button-out state.
- Stereo: "The Host 160A will now respond to the combined inputs of the 160As" and "the Linked 160A will track accurately", with "True Power Summing".
- THD+N "<0.2%, Typical, Any Amount of Compression up to 40dB @ 1kHz".
- Frequency response "20Hz–20kHz, +0, −0.5dB; +0, −3dB 0.5Hz and 90kHz".
- Output gain "Variable from −20dB to +20dB".
- Gain-reduction display with 12 LEDs up to 40 dB, while the unit can deliver 60 dB.
- Meter "0" = +4 dBu.
- The manual does not describe the internal circuit or name the VCA.

## Findings and decisions

### F1. Topology: feed-forward, true RMS, dB-linear control

- S3 states feed-forward explicitly, and S7 and S8 describe the dbx/THAT feed-forward compressor in detail.
- The detector produces a voltage linear in dB, and the VCA's gain is linear in dB of control voltage (S6). Gain reduction is therefore `slope × (detector dB − threshold dB)` above threshold.
- Because the detector never sees the gain-reduced signal, the static curve of the whole unit is the gain computer itself. The attack and release are those of the detector alone (S7: "entirely determined by the RMS detector").

**Decision.** Feed-forward gain computer in dB on a true-RMS detector. `SOURCE_CONFIRMED` for the topology (S1, S3) and `SCHEMATIC_OR_MEASURED` for the dB-linear law (S6, S7).

### F2. Detector time constant: one pole, 34.7 ms, from the 125 dB/s release

- The release figures in S1/S2 are exactly a constant rate: 1 dB / 125 dB/s = 8 ms, 10 dB → 80 ms, 50 dB → 400 ms.
- A log-domain RMS detector releases in a straight line in dB (S5). If its mean square decays as e^(−t/τ), the level falls at 10·log10(e)/τ dB/s. So **125 dB/s ⇔ τ = 34.74 ms**.
- This matches THAT's own ~35 ms (S6, S8) and ~32 ms (S7) examples. The 160X's "0 to 500 ms" (S3) is the same law: 60 dB of maximum compression at 125 dB/s is 480 ms.
- Attack uses the same pole (S5, S6: attack and release are locked). For a step from the threshold to D dB above it, the time to 63 % of the final gain reduction is

  `t = −τ ln(1 − (10^(−0.0368 D) − 10^(−D/10)) / (1 − 10^(−D/10)))`

  This gives 15.7, 6.6 and 2.8 ms for 10, 20 and 30 dB, against dbx's "typically 15, 5, 3 ms". The 10 and 30 dB figures agree within 5 %.
- The 20 dB figure is 33 % slower than the data sheet. A single time constant cannot match both the 125 dB/s release and 5 ms at 20 dB, because 5 ms at 20 dB would need τ = 26 ms, which would release at 168 dB/s. dbx gives no measurement definition for "attack time", and the 63 % definition is our reading of it.

**Decision.** One pole on the mean square of the power sum, τ = 10·log10(e)/125 s = 34.74 ms.

- The 125 dB/s rate is `SOURCE_CONFIRMED`.
- The single-pole detector is `SCHEMATIC_OR_MEASURED` (S5–S8).
- The attack interpretation is `APPROXIMATION`, with the 20 dB deviation documented and tested at a stated tolerance.

### F3. Ratio law and range

- 1:1 → ∞:1 → negative ratios (S1, S2, S3). The 160A's negative region ("Infinity+") goes to −1:1.
- The dbx/THAT feed-forward ratio pot passes a fraction R of the control voltage: `RATIO = 1/(1−R)` (S7). The natural continuous variable is therefore the **slope** s = 1 − 1/ratio: s = 0 is 1:1, s = 1 is ∞:1, and s = 2 would be −1:1.
- S7 notes that loading puts 2:1 "slightly more than the halfway point". The 160A's own pot taper is not published.

**Decision.**

- Parameter plain value = s ∈ [0, 1], linear on the knob. 2:1 sits at mid-travel, 4:1 at 75 % and ∞ at the end. (`SCHEMATIC_OR_MEASURED` law; `APPROXIMATION` taper.)
- Negative ratios (s > 1) are **not in v1**: the skin's scale ends at ∞.

### F4. OverEasy knee

- S1 confirms three things: (a) the threshold is the **midpoint** of the transition, (b) there is "no distinct point at which processing begins", and (c) the ratio control sets the ratio reached after the transition (S3).
- No source gives a width in dB.
- The circuit principle (S7) is a diode's exponential soft rectification of a dB-linear voltage. The resulting curve is a softplus in dB: zero slope far below, the full slope far above, and the two asymptotes crossing at the threshold. Unlike a quadratic knee it has no hard onset point, which matches (b).

**Decision.** `GR = s · w · ln(1 + e^((L − T)/w))` with w = 2.5 dB.

- The local slope goes from 10 % to 90 % over ±5.5 dB around the threshold.
- 0.1 dB of GR at ∞:1 appears about 8 dB below the threshold.
- GR at the threshold is 1.73 dB × s.
- The shape is `SCHEMATIC_OR_MEASURED` and the width is `APPROXIMATION`. This is open measurement task M1.

### F5. Hard knee

- Button out (S1): no gain change below the threshold and the full ratio above it.

**Decision.** `GR = s · max(0, L − T)`, `SOURCE_CONFIRMED`. The RMS detector makes the corner smooth in time but not in level.

### F6. Threshold range and calibration (knob → dB RMS)

- Hardware: −40..+20 dBu RMS, with meter 0 = +4 dBu (S1).
- At the common alignment 0 dBFS = +22 dBu (EBU R68-style −18 dBFS = +4 dBu), the hardware's useful centre (−10..0 dBu) lands at −32..−22 dBFS RMS. In a DAW, hot drum and bus tracks run at about −14..−8 dB RMS over the 35 ms detector window. At hardware calibration they would therefore always be far over the knob centre.
- The PUNCH skin prints −20..+10 with 0 at the top.

**Decision (`APPROXIMATION`, reported to the coordinator).**

- Knob 0 dB = **−15 dB RMS**, which is a sine peaking at −12 dBFS, or +7 dBu at 0 dBFS = +22 dBu.
- The knob therefore covers **−35..−5 dB RMS**: −13..+17 dBu at that alignment, inside the hardware's −40..+20 dBu.
- At the knob top, a hot snare or kick track at −12..−10 dB RMS sits 3..5 dB over threshold, which is a few dB of "punch" at 4:1. At +10 a full-scale sine (−3 dB RMS) is only 2 dB over, which is close to "off". At −20 the deepest setting squashes anything above −35 dB RMS.

GLUE's knob 0 = −10 dBFS **peak**. For a sine, PUNCH's −15 dB RMS is −12 dBFS peak, so the two units sit within a few dB of each other at the knob top.

### F7. VCA character and distortion

- The 160X uses a Blackmer VCA (S3): a class-AB log/antilog cell whose small distortion is mostly 2nd and 3rd harmonic.
- The data sheet bounds are 0.07 % 2nd / 0.07 % 3rd below threshold and 0.07 % / 0.02 % above (S3), and THD+N < 0.2 % with up to 40 dB of compression (S1). THAT VCAs of the same family are at 0.003..0.035 % (S6).
- In a feed-forward RMS compressor, part of the distortion at low frequencies is **gain modulation by the detector's ripple**: the mean square of a sine ripples at 2f. With a 35 ms pole this is ±4.5 % at 50 Hz and ±0.23 % at 1 kHz, so the GR carries a small 2f ripple. This is real behaviour of the hardware. PUNCH keeps it in every GRIT setting, including Clean.

**Decision.**

- **Clean** is a numerically linear VCA. Below threshold THD is < −140 dB; with GR only the detector-ripple modulation remains, < 0.2 % at 1 kHz as in the data sheet.
- **GRIT** is the `TLEET_EXTENSION` macro required by the master spec: "additional level-dependent VCA/output coloration". It adds 2nd and 3rd order terms to the VCA transfer, whose depth grows with GR and whose size grows with the VCA's input level, plus an asymmetric output-stage drive.
- GRIT never alters the gain computer, and the GR telemetry is unchanged by it.

### F8. Stereo strapping

- Two 160As are strapped and the Host "will now respond to the combined inputs". The coupling is "True RMS Power Summing" (S1, S2): the RMS energies of the two inputs add in the detector. One control drives both units; the Linked unit's controls are disabled and it "will track accurately".

**Decision.**

- Detector input = mean of the channel powers, (L² + R²)/2, followed by one pole. One gain drives both channels.
- Power summing is `SOURCE_CONFIRMED`.
- The ½ normalisation is `APPROXIMATION`/`TLEET_EXTENSION`. It makes a centred mono source (L = R) read the same in a stereo instance as in a mono instance. On strapped hardware the summed power reads +3 dB, which the operator would compensate with the threshold.
- Consequences: (A, A) = (A, −A) = mono A; (A, 0) reads 3.01 dB lower; polarity and correlation do not matter. This differs from GLUE's dominant (max) peak detector.

### F9. Frequency response, levels and range

- Frequency response is 20 Hz–20 kHz +0/−0.5 dB, −3 dB at 0.5 Hz and 90 kHz (S1). PUNCH uses 0.5 Hz input coupling (`SOURCE_CONFIRMED`). The 90 kHz roll-off is above the audio band and is not modelled.
- Maximum compression is >60 dB (S1, S3), so PUNCH clamps GR at 60 dB.
- Output gain is −20..+20 dB (S1 spec table, S3), matching the skin. S1's front-panel text says "up to +20 dB"; the spec table is used.
- The hardware GR display reaches 40 dB (S1). The skin's LED meter reaches 20 dB. The telemetry is not clipped, so the editor decides what to show.

### F10. External detector input

The 160A/160X have a rear-panel detector input (S1, S3). The PUNCH skin has no control for it, and master spec §12 says not to wire one without a UI control. It is not implemented in v1.

## Conflicts between sources

| Topic | Conflict | Decision |
|---|---|---|
| Release | The 160X says "0 to 500 ms"; the 160A says 125 dB/s with 8/80/400 ms | Consistent: 60 dB at 125 dB/s is 480 ms. Use 125 dB/s. |
| Attack at 20 dB | The data sheet says 5 ms; a single pole fitted to 125 dB/s gives 6.6 ms | Keep one pole (S5/S6: attack and release are locked). Document a +33 % deviation, tested within 40 %. |
| Output gain | S1 text: "up to +20 dB"; S1 table and S3: −20..+20 dB | −20..+20 dB |
| Input impedance | S1 text >40 kΩ vs table 100 kΩ | Irrelevant to the model |

## Not established by any source (APPROXIMATION in MODEL_SPEC)

- OverEasy knee width (softplus scale 2.5 dB).
- The 160A's ratio pot taper (PUNCH uses linear in slope).
- The definition behind dbx's "attack time" figures (PUNCH uses 63 % of the final GR from a step that starts at threshold).
- Threshold calibration to dBFS (F6).
- The ½ normalisation of the stereo power sum (F8).
- GRIT coefficients (a `TLEET_EXTENSION`, bounded by the F7 figures at its default).
