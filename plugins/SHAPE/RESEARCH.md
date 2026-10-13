# SHAPE — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
here with its source and how strong that source is. Where sources disagree,
the conflict and the decision are written down here and are not repeated in
the model spec.

SHAPE is informed by the **Pultec EQP-1A program equaliser** (Pulse
Techniques, 1950s onward, reissued by Pulse Techniques LLC). It is not a
measured 1:1 clone of one unit. No unit was measured for this project. The
network topology and its reactive values come from a published,
schematic-derived model (S4, S5). The five values that set the control ranges
are calibrated to the manufacturer's published ranges (S1), and every constant
nobody publishes is an `APPROXIMATION` (MODEL_SPEC section 14 lists what to
measure).

All sources were accessed on 2026-10-13. Some sources could not be read from
this build environment; they are listed under "Blocked sources" at the end.
Their content was not guessed.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — manufacturer | Pulse Techniques (the Pultec name holder) product data | `SOURCE_CONFIRMED` |
| A' — licensed / derived manufacturer | the manual of the licensed software model (Universal Audio); the manual of a manufacturer's own EQP-1A derivative (Manley) | `SOURCE_CONFIRMED` for behaviour the original shares; context for ranges |
| B — engineering | peer-reviewed circuit modelling, public schematic-derived models, standards | `SCHEMATIC_OR_MEASURED` / basis of `APPROXIMATION`s |
| C — measurement review | magazine reviews with measured plots | `SCHEMATIC_OR_MEASURED` trend, never an exact constant |
| D — forum / DIY | builder forums, DIY clone pages | context and cross-checks only, never the sole basis for a law |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | Pulse Techniques, *EQP-1A* product page and specifications — <https://pulsetechniques.com/products/eqp-1a> | A | Low shelf boost **20, 30, 60, 100 CPS, 0 to 13.5 dB**; low shelf attenuate **20, 30, 60, 100 CPS, 0 to 17.5 dB**; peak boost **3, 4, 5, 8, 10, 12, 16 KCS, 0 to 18 dB**; high shelf attenuate **5, 10, 20 KCS, 0 to 16 dB**. Distortion **≤ 0.15 % at +10 dBm into 600 Ω**. Amplifier response flat 20 CPS – 20 KCS ± 0.5 dB. Noise 92 dB below +10 dBm. Input ≈ 600 Ω, output ≈ 50 Ω. Tubes 12AU7, 12AX7, 6X4 (rectifier). No insertion-loss, gain or bandwidth figure. |
| S2 | Universal Audio, *Pultec Passive EQ Collection Manual* — <https://help.uaudio.com/hc/en-us/articles/7183176266260-Pultec-Passive-EQ-Collection-Manual> | A' | The same positions as S1 (CPS/KCS). BANDWIDTH sets how wide the HF boost is; "lower values yield a narrower band". LF boost and atten together: the original manual advised against it "because in theory, they would cancel each other out", but "the Boost control has slightly higher gain than the Attenuation has cut, and the frequencies they affect are slightly different too". The result is described as an extra feature. With EQ switched out "the signal is still colored" by the I/O circuitry. "The original EQP-1A hardware unit has an inherent level boost of approximately 1.13 dB when it is in the signal path." The tube amplifier makes up the passive insertion loss. Amplifier overload effects are audible. |
| S3 | Manley Laboratories, *Enhanced Pultec EQP1A* owner's manual (public mirror) — <https://www.barryrudolph.com/recall/manuals/pulteceqp1-a.pdf> | A' | Manley's own 1U derivative of the Western Electric / Pultec passive network, not the original. BANDWIDTH "affects the width of the HIGH FREQUENCY BOOST effect": counter-clockwise is narrow (high Q), clockwise is wide (low Q). Maximum HF boost **20 dB at narrow, 13 or 14 dB at wider settings**. Maximum EQ boost 16 dB lows, 11–20 dB highs; maximum cut 20 dB lows, 11–21 dB highs. IN/BYPASS: in BYPASS the EQ section is bypassed while the amplifier stays in circuit. LF boost and cut together: "The typical effect is boosted lows with a dip in the low mids or mids"; "most engineers do use both knobs at the same time". THD+N 0.015 % at +4 dBm (Manley's amplifier, not the original's). |
| S4 | A. Barrera, X. Lizarraga-Seijas, F. Font, *Modeling the Pultec EQP-1A with Wave Digital Filters*, SMC 2024, Porto — <https://smcnetwork.org/smc2024/papers/SMC2024_paper_id132.pdf> | B | Passive equaliser stage followed by a 12AX7 + 12AU7 make-up stage that restores the insertion loss. Input and output transformers exist but are out of the paper's scope. Four filters: LF boost (shelf), LF cut (shelf), HF boost (resonant RLC with selectable centre and bandwidth), HF cut (shelf). The schematics are compiled from the EQP-1R schematic, the Gyraf G-Pultec (S10), the EQP-1A manual curves and SoS measurements of the 2019 reissue, because "the original schematics for the circuit of the EQP-1A are not publicly available". The authors simplify the input/output resistors and adjust the other values to keep the response. Stated maxima: LF boost up to 16 dB, LF cut up to 20 dB, HF cut up to 20 dB. No component values in the text. |
| S5 | A. Barrera, *EQP1A-WDF* (the S4 plug-in, GPL-3.0) — <https://github.com/ABSounds/EQP1A-WDF> (`Source/EQP1A.h`, `Source/EQP1A.cpp`, `README.md`, read as raw files) | B | The network as one loop driven by the source: series arm = HF boost pot (12 kΩ) with the series LC resonator plus a Q resistor (`RQ` 0..3.9 kΩ + 390 Ω); the shunt = HF cut pot (1 kΩ) with the cut capacitor ‖ (LF cut pot (110 kΩ) ‖ C1, then R2 = 1 kΩ, R3 = 10 kΩ); ground leg = LF boost pot (8.2 kΩ) ‖ C2. Output = v(R3) − v(J) (sign by orientation), with a make-up gain of **15.4882** (≈ 23.8 dB). **C2 = 2.2 µF, 1.1 µF, 560 nF, 330 nF** for 20/30/60/100 Hz; **C1 = C2 / 30**. HF resonator **L/C = 12·10⁶** (√(L/C) = 3464 Ω), tuned to the selected frequency. HF cut capacitor **270 nF, 135 nF, 68 nF** for 5/10/20 kHz. All pots linear in that code. The README says the values were adjusted in LTspice so the response approximates the original unit as measured by Sound On Sound, and quotes MusicTech for the ranges (13.5 / 17.5 / 18 / 16 dB, the S1 figures). |
| S6 | A. Barrera Herrero, *Pultec EQP-1A Modeling with Wave Digital Filters*, master's thesis, Universitat Pompeu Fabra, 2023, DOI 10.5281/zenodo.8382764 — <https://zenodo.org/records/8382764> | B | Record metadata only (CC-BY 4.0): WDF model of the passive section, prototype checked against LTspice. **The PDF could not be read** (see Blocked sources). |
| S7 | H. Robjohns, *Cartec EQP1A (Mono Valve Equaliser)*, Sound On Sound, October 2010 — <https://www.soundonsound.com/node/4907989> | C | A faithful EQP-1A recreation measured: LF boost shelf up to **about 14 dB**; LF cut down to **about −18 dB** with "something approaching a modest 3 dB/octave slope"; the attenuation slopes extend **higher in frequency** than the boost slopes; boost + attenuate together give a "resonant shelf" with a **mid dip up to 8 dB** deep. HF boost up to **18 dB**, Sharp and Broad shown; narrow settings get "biting and quite aggressive-sounding". HF attenuate is a shelf, **up to 16 dB** at 5/10/20 kHz; with the boost it makes "dip-before-peak" responses. Boost and cut get more effective as they increase, in a "logarithmic way". Distortion: "quiet and clean", "a mild tendency towards **third-harmonic** distortion"; becomes "crunchy and dynamically compressed" only at very high inputs (**above +24 dBu** peaks). The original has three transformers (input, inter-stage, output) and a multi-tapped HF inductor. The amplifier gives "a modest 20 dB or so of gain" to restore unity when flat; 12AX7 input, 12AU7 driving the output transformer, push-pull, with negative feedback from an extra output-transformer winding. |
| S8 | D. P. Berners, J. S. Abel, *Ask the Doctors!*, Universal Audio Webzine, June 2003 — <https://uaudio.com/webzine/2003/june/text/content2.html> | B | The EQP-1A network is passive (R, C, L only); the sections interact, so the whole circuit must be modelled at once (a closed-form transfer function from the schematic). A 3 dB boost with a 3 dB cut on the low shelf does not cancel. They fitted every component, parasitics included, to measurements of a "golden" unit "to within an absolute impedance factor", because tolerances vary and the original values are disputed (potted parts). The **pot tapers had to be measured**. Transformers and make-up amplification add nonlinearity and EQ of their own. |
| S9 | ruffrecords ("Ian"), *Pultec EQP-1A turnover frequencies*, GroupDIY forum — <https://groupdiy.com/threads/pultec-eqp-1a-turnover-frequencies.43095/> | D | Measured turnover / maximum per position. LF boost: 20 → 100 Hz / 12 dB, 30 → 200 / 13, 60 → 300 / 13, 100 → 700 / 13 ("about 6 times the set frequency"). LF cut: 20 → 250 / 17, 30 → 400 / 18, 60 → 800 / 18, 100 → 1500 / 17 ("13.5 times"). HF cut: 5 k → 600 / 16, 10 k → 1500 / 16, 20 k → 2500 / 16. |
| S10 | Gyraf Audio, *G-Pultec* DIY project page — <https://www.gyraf.dk/gy_pd/pultec/pultec.htm> | D | A reverse-engineered clone, a source of S4's schematic. The low-boost pot is **10 kΩ log** (the PCB print "10K Lin" is a known error). The real filter schematic came from Chris Jenrick. Component values are only in the schematic images, which could not be read (Blocked sources). |
| S11 | EBU R 68, *Alignment level in digital audio production equipment* | B | **0 dBu ≙ −18 dBFS**. Level alignment of the analog stages (F7). |
| S12 | AES17, *Measurement of digital audio equipment* | B | dBFS of a sine is its peak. THD and THD+N conventions used by the tests. |

## Findings and decisions

### F1 Topology and component values

S4 and S8 agree that the EQP-1A EQ is one passive R-L-C network with
interacting sections, followed by a tube make-up amplifier. S5 publishes the
network as compiled from the public schematics (S4's sources include S10)
and tuned in LTspice to SoS measurements. Decisions:

- SHAPE uses S5's **topology** (one series loop: HF boost arm, the two shunt
  branches, the LF boost leg) and its **reactive values**: C2 per LF
  position, C1 = C2/30, √(L/C) = 3464 Ω, the three HF cut capacitors, the
  12 kΩ HF boost pot, the 1 kΩ HF atten pot and the R2/R3 load.
  [SCHEMATIC_OR_MEASURED, via S4/S5]
- S5 re-tuned its values to one measured unit and S8 says the original values
  are disputed. So the five values that set the control ranges are
  **calibrated to S1's published maxima** instead of copied: the LF boost
  rheostat (S5 8.2 kΩ → 9.446 kΩ), the LF atten rheostat (110 kΩ →
  95.51 kΩ), the Q resistor (S5 390 Ω + 0..3.9 kΩ → 754 Ω + 0..1382 Ω), and a
  94.5 Ω stop resistor in series with the HF atten capacitor (S5 declares a
  91 Ω `R1` beside the HF cut network but does not place it in its tree).
  [APPROXIMATION, calibrated; MODEL_SPEC 2.3]
- The make-up gain is the exact inverse of the flat (all-zero) network's
  resistive loss: −23.80 dB, ×15.50. S5 uses 15.4882 and S7 says "20 dB or
  so". [SCHEMATIC_OR_MEASURED]
- No code from S5 is used. SHAPE's solver is its own (trapezoidal companion
  models in one loop equation, not a WDF tree), and only component values
  and topology are taken from S5. Those are facts about a circuit.

### F2 Control ranges — sources disagree

| Section | S1 (Pulse) | S5 / MusicTech | S7 (SoS, Cartec) | S9 (forum, measured) | S4 (paper) | S3 (Manley derivative) |
|---|---|---|---|---|---|---|
| LF boost | 13.5 dB | 13.5 | ≈ 14 | 12–13 | up to 16 | 16 |
| LF atten | 17.5 dB | 17.5 | ≈ 18 | 17–18 | up to 20 | 20 |
| HF boost | 18 dB | 18 | 18 | — | — | 20 narrow, 13–14 wide |
| HF atten | 16 dB | 16 | 16 | 16 | up to 20 | 11–21 |

Decision: the manufacturer (S1) wins, and S5, S7 and S9 support it within
about 1 dB. S4's "up to" figures are rounded upper bounds, and S3 describes a
different product. Calibration points (MODEL_SPEC 2.3):

- LF boost 13.5 dB and LF atten −17.5 dB at 60 Hz with the 60 Hz position.
- HF boost 18 dB at Sharp at the selected frequency (every position).
- HF atten −16 dB at 10 kHz with the 10 kHz position.

S1 gives one HF boost figure. Its reduction at Broad comes from S3 ("13 or
14 dB at wider settings"), so SHAPE sets **13.5 dB at Broad**. Because one
network serves every position, the other positions land where the circuit
puts them: LF boost 12.1 dB at the 20 Hz position (S9 measured 12 dB) and
13.6 dB at 30/100 Hz; LF atten −16.6 / −17.5 dB; HF atten −16.0 dB at 5 and
10 kHz and −16.04 dB at 20 kHz. [SOURCE_CONFIRMED ranges]

### F3 LF boost + atten interaction (the "low-end trick")

S2, S3, S7 and S8 agree: boost and atten on the same LF position do not
cancel. The boost acts lower and slightly stronger; the atten acts higher, so
the result is boosted lows with a dip above. S7 measured a dip of up to 8 dB.
S9's turnovers put the atten at about 13.5× and the boost at about 6× the
set frequency. In the network this falls out of the topology. The LF boost
is C2 ‖ rheostat in the ground leg. The LF atten is C1 = C2/30 ‖ a ten-times-
larger rheostat in the shunt branch, so its RC corner sits higher than the
boost's. [SCHEMATIC_OR_MEASURED]

SHAPE as run (60 Hz position, boost = atten = 10): +16.3 dB plateau, dip
**−6.5 dB at 560 Hz**. The −3 dB turnovers are 362 Hz for the boost and
781 Hz for the atten (6.0× and 13.0× the set frequency; S9 says 6× and
13.5×). At the other positions the turnovers are 92/202 Hz (20 Hz), 182/402
Hz (30 Hz) and 609/1341 Hz (100 Hz). S9 measured 100/250, 200/400 and
700/1500 Hz. The dip at full settings is 1.5 dB shallower than S7's "up to
8 dB" (open task R3).

### F4 HF boost bandwidth

S2, S3, S7: BANDWIDTH widens the boost. Sharp is narrow with the highest
boost; Broad is wide with less boost. S3 gives about 20 → 13–14 dB; SHAPE
uses 18 → 13.5 dB (F2). The circuit does this with the Q resistor in series
with the LC resonator. A larger resistance lowers the resonator's Q and the
peak height together, so one control changes both, as on the hardware. The
rheostat is linear. No source gives its taper. [SCHEMATIC_OR_MEASURED
mechanism (S5); APPROXIMATION taper]

### F5 Pot tapers

S5's code is linear in every pot. S10 says the LF boost pot is a 10 kΩ
**log** pot on the schematic. S7 hears boost and cut "becoming more
effective as they increase, in a logarithmic way". S8 had to measure the
tapers. Decision: every boost/atten pot uses an exponential taper r(x) =
(B^x − 1)/(B − 1). Each B is set so that **knob 5 gives 40 % of that
control's full range in dB** at its reference position: the dB change grows
toward the top of the dial (S7). The 40 % figure is not published.
[APPROXIMATION; open task R2]

### F6 Make-up amplifier and transformers: where the colour comes from

- S7: the original has input, inter-stage and output transformers, a 12AX7
  input stage and a 12AU7 push-pull output stage. The output stage has global
  negative feedback from a winding of the output transformer. The unit is
  clean, with a mild third-harmonic tendency, and gets "crunchy and
  dynamically compressed" only above about +24 dBu.
- S2: overload of the amplifier is audible. S2 and S3: with the EQ switched
  out the signal still passes the I/O circuitry and amplifier, so it stays
  coloured.
- S8: the transformers contribute nonlinearity and EQ of their own.

Decisions (MODEL_SPEC 5):

- Two **transformers** (input; output, which also stands in for the
  inter-stage transformer). Each has the magnetising-inductance high-pass
  (2 Hz corner) and a flux-dependent saturation. Their distortion grows as
  the level rises and as the frequency falls (flux ∝ level / frequency), as
  in any iron-core transformer.
- A **make-up amplifier** with an odd-order soft limit (push-pull with
  feedback, S7). A small even-order term, high-passed so it adds no DC, stands
  for the single-ended 12AX7 stage.
- The constants are calibrated to S1's THD figure (F7), S7's third-harmonic
  balance and +24 dBu overload, and the iron's low-frequency rise.
  [APPROXIMATION for every constant; SOURCE_CONFIRMED stage list]
- DRIVE and CHARACTER have no hardware counterpart. DRIVE is the level into
  the stages, compensated after them (so it is the hardware's level, not a
  volume control). CHARACTER selects three voicings of the same stages.
  [TLEET_EXTENSION]

### F7 Levels and the THD calibration point

S1: THD ≤ 0.15 % at +10 dBm into 600 Ω. Per S11, 0 dBu = −18 dBFS, so
+10 dBu is a **−8 dBFS** sine (peak, S12). (dBm into 600 Ω equals dBu.)
SHAPE's Original voicing at DRIVE 0 gives **0.112 % at 1 kHz, −8 dBFS**,
with H3 above H2. S7's +24 dBu (+6 dBFS) overload is "crunchy": 2.3 % THD at
1 kHz and 3.9 % at 50 Hz. [SOURCE_CONFIRMED calibration point; APPROXIMATION
alignment]

### F8 EQ IN and Bypass

S3: BYPASS removes the EQ section but keeps the amplifier. S2: EQ out is
still coloured by the I/O. Decision: **EQ IN Out replaces the network with
its flat equivalent** (a click-free crossfade). The transformers and the
amplifier stay in circuit, so the colour stays and the level does not jump:
the flat network is unity after make-up. The host **Bypass** is the plugin's
own true bypass: the latency-aligned input, bit-exact. [SOURCE_CONFIRMED EQ
IN behaviour; TLEET_EXTENSION host bypass]

### F9 The 1.13 dB inherent level boost (not modelled)

S2: the original "has an inherent level boost of approximately 1.13 dB when
it is in the signal path". No other source repeats it. SHAPE is unity gain
at flat settings; OUTPUT covers ±20 dB. Whether to add it is an owner
decision (MODEL_SPEC 14).

### F10 Amplifier bandwidth

S1: the amplifier is flat 20 Hz – 20 kHz ± 0.5 dB. SHAPE's analog stages are
flat in that band (the two 2 Hz transformer corners give −0.09 dB at 20 Hz
and −0.01 dB at 60 Hz)
and have no HF roll-off. The S8 plug-in's 18 kHz roll-off was a latency
artefact of that plug-in, not hardware behaviour, and is not copied.
[SOURCE_CONFIRMED; APPROXIMATION no HF roll-off]

## Blocked sources (this build environment)

| Source | What happened | Consequence |
|---|---|---|
| Gyraf schematic images (`pultech.gif`, `gy_pd_sch.gif`) and PCB PDF (S10) | the host refused the connection (proxy CONNECT 403) | no independent read of the original filter values; S5 is the only values source |
| Zenodo thesis PDF (S6) | the download came back as binary, and the preview is disallowed by robots | the thesis's component tables and LTspice comparison are unread |
| UPF repository record (`repositori.upf.edu`, S4's handle 10230/60656) | "Access Denied" | nothing beyond S4 |
| `ABSounds/EQP1A-WDF` Netlist directory and the GitHub API (S5) | listing disallowed by robots; the GitHub API repository access is not enabled for this session | only `Source/EQP1A.h`, `Source/EQP1A.cpp` and `README.md` were read |
| GroupDIY *understanding the PULTEC EQ circuit* thread | values were in images that did not come through | none |

## Open research tasks

- **R1** Read S6 (thesis) and S10's schematic images from a machine with
  access. Compare the network values and the S5 LTspice tuning with SHAPE's
  calibrated values (F1).
- **R2** Measure the pot tapers of a real unit or a faithful reissue (S8
  did). Replace the 40 %-at-knob-5 law (F5) and the linear BANDWIDTH
  rheostat (F4).
- **R3** Measure the LF boost+atten curves of a real unit at several knob
  pairs. In particular, check the deepest dip (S7: up to 8 dB; SHAPE 6.5 dB
  at 10/10) and the 20 Hz and 100 Hz turnovers against S9 (F3).
- **R4** Measure THD vs level and frequency (1 kHz and 50 Hz, −30..+6 dBFS
  equivalent) and the harmonic balance of a real unit. Replace the
  APPROXIMATION constants of the transformers and the amplifier (F6, F7).
- **R5** Confirm the 1.13 dB level boost (F9) and what the hardware's EQ
  IN/OUT switch puts in place of the network (F8).
