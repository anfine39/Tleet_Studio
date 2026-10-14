# GLUE — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
with its source and how strong that source is. Where sources disagree, the
conflict and the decision are written down here and then not repeated in the
model spec.

GLUE is informed by the **SSL G-Series stereo bus compressor** (the centre-section
compressor of the SL 4000 G console, the 82E26/82E27 cards, and its outboard and
500-series descendants). It is not a measured 1:1 clone of one unit: no unit
was measured for this project (see "Open measurement tasks" in MODEL_SPEC).

All sources were accessed on 2026-10-12.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — manufacturer | SSL user guides and SSL-licensed manuals | `SOURCE_CONFIRMED` |
| B — measurement | peer-reviewed or preprint measurements of a real unit | `SCHEMATIC_OR_MEASURED` |
| C — schematic / clone | circuit readings of the SSL cards and documented clone designs | `SCHEMATIC_OR_MEASURED` |
| D — review / forum | reviews and forum measurements | context only, never the sole basis for a law |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | SSL, *500 Series G Comp Module User Guide* — <https://www.solidstatelogic.com/assets/uploads/downloads/SSL_500_Series_G_Comp_Module_User_Guide.pdf> | A | L and R are "independently rectified using a true peak full wave detector circuit". Classic "dominant" side-chain: the louder channel controls the GR of the stereo pair. The knee point changes with RATIO, and a lower ratio lowers the effective threshold. The main VCA is permanently in circuit. Threshold and make-up are continuous; attack, ratio and release are switches; there is an HPF in the side-chain. |
| S2 | SSL, *THE BUS+ User Guide* rev. 1.1 (April 2022) — <https://r2.gear4music.com/media/82/826466/download_826466.pdf> | A | The side-chain is fed from a **feed-forward position** by default, which is the "traditional 'grab' of the Bus Compressor". F/B takes the feed after the gain-reduction VCA ("more relaxed"). Auto release is two-stage, 100 ms short and 12 s long. Side-chain HPF is 12 dB/oct (2nd order), OUT..300 Hz. Threshold −20..+20 dB; make-up −10..+20 dB. "Classic Stereo" is individually rectified, unlike summed S/C stereo. THD+N with the compressor in is 0.004 % at 1 kHz, 0 dBu. |
| S3 | Universal Audio, *SSL 4000 G Bus Compressor Manual* (developed with SSL from SSL schematics) — <https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual> | A (licensed) | Threshold ±15 dB; make-up 0..+15 dB. Attack 0.1/0.3/1/3/10/30 ms. Release 0.1/0.3/0.6/1.2 s + Auto ("program-dependent, multi-stage"). Ratio 2/4/10. A "variable-knee" design with a ratio-dependent effective threshold. "Dominant" side-chain. SC HPF 12 dB/oct, 20..500 Hz, sidechain only. |
| S4 | H. Robjohns, *SSL XLogic G-Series Compressor*, Sound On Sound, Feb 2006 — <https://www.soundonsound.com/reviews/ssl-xlogic-g-series-compressor> | D | The same attack, release and ratio steps. Threshold −15..+15 dBu; make-up −5..+15 dB. External key input. GR meter to 20 dB. "10:1 is essentially peak limiting". |
| S5 | B. R. Thompson, M. C. Heilemann, *Evaluating Dynamic Range Compressor Models Using Control-Voltage Measurements: an Approach and Dataset*, arXiv:2606.18573 (17 June 2026) — <https://arxiv.org/abs/2606.18573> | B | A serviced 1991 SSL G384 was measured at the control voltage. Detector: max of the instantaneous amplitude across channels, with one shared gain signal. The threshold offset is applied on the **side-chain VCA**. The ratio switch sets an offset at the slope stage, so the effective threshold depends on ratio. Attack is "greatly accelerated" by the control loop's gain and is not exponential, because the loop gain is not constant. The static curve is soft at low ratios and close to hard-knee at 10:1. AUTO is a **two-pole** network with program-dependent release. Make-up is a DC offset on the **audio** VCA control only. Audio VCAs are THAT 202-series; side-chain VCAs are dbx 2151. |
| S6 | J. Gyraf, *The SSL Clone* (GSSL) — <https://gyraf.dk/gy_pd/ssl/ssl.htm> | C | The side-chain VCA acts as a tracking "dummy" VCA that parallels the main VCAs' GR, so the design acts "mostly as a feed-back compressor". A full-wave rectifier follows the side-chain VCA. A diode between the rectifier and the attack/release timing sets the onset. Attack: a resistor (switch) charges a capacitor; release: a capacitor/resistor per position. Auto is "two release time constants (91k+6u8 and 750k+u47)". The ratio switch sets an offset and gain at the rectifier and shifts the threshold slightly. Threshold −20..+20 dBm, with the threshold DC summed into the side-chain VCA control. Make-up 0..+20 dB, summed into the main VCA control. GR meter about 50 µA/dB, 20 dB full scale. The GSSL **sums** L+R into one side-chain, unlike the original (see S7). |
| S7 | *Why A GSSL and SSL Compressor Aren't Exactly the Same*, ProAudioDesignForum — <https://proaudiodesignforum.com/forum/php/viewtopic.php?p=61> | C | On the original 82E26 cards each channel has its own detector. The detectors are diode-OR'd onto the 82E27, so "the greater of two peaks" controls the bus. Combining after detection removes polarity and correlation effects. |
| S8 | DIYRE, *G Bus* assembly guide and manual — <https://manuals.diy.re/gbus>, <https://cdn.shopify.com/s/files/1/0698/2265/files/G_Bus_Manual_1.0.pdf?v=1679084320> | C | Release network: five 0.47 µF capacitors plus one 6.8 µF, matching S6's Auto values. Threshold works by "controlling the gain of the SC VCA". A diode sets the onset. The side-chain VCA mirrors the GR (feedback behaviour). The original L/R side-chains are independent up to the timing section. |
| S9 | GroupDIY, *SSL clone compressor ratio measurements* — <https://groupdiy.com/threads/ssl-clone-compressor-ratio-measurements.944/page-10> | D | Clone ratios are measured as the output rise for +10 dB of input from about 10 dB of GR (for example 5.3 / 2.3 / 0.9 dB for 2/4/10). The side-chain resistor shifts all ratios together. "Measuring too close to the knee" changes the result. |
| S10 | TapeOp, *SSL THE BUS+* review — <https://tapeop.com/reviews/gear/152/the-bus-bus-compressor> | D | Context for S2. Ratios 1.5:1 and 1.3:1 were added on later SSL units. |
| S11 | GroupDIY, *SSL mod: switch between feedforward and feedback* — <https://groupdiy.com/threads/ssl-mod-switch-between-feedforward-and-feedback-compression.1479/> | D | "the sidechain gets its signal from the inputs (as it is in the ssl comp)". |

## Findings and decisions

### F1. Topology: input-fed side-chain with a tracking side-chain VCA

- S2 and S11 say the side-chain is **fed from the input** (feed-forward position).
- S5, S6 and S8 say the side-chain passes through a **side-chain VCA that follows the GR**. The detector therefore sees the input multiplied by the applied gain, so the loop behaves like a feedback compressor. S5's "loop gain" that accelerates the attack only exists with this loop.
- These are not in conflict. The feed is taken at the input; the side-chain VCA closes the loop. S2 calls that "a feed-back topology … from a feed-forward position".

**Decision.** Detector input = side-chain feed × threshold gain × applied gain, solved implicitly every sample. The external key replaces the side-chain feed only; the loop through the applied gain stays. That is what a key into the hardware's side-chain does.

### F2. Detector: per-channel full-wave peak, dominant

S1, S3 and S7 agree; S5 confirms the max-across-channels detector on a real unit. The GSSL's summed side-chain (S6) is a clone deviation and is **not** used.

**Decision.** Each channel has its own SC HPF/DARK and its own full-wave rectifier, and the larger one wins. Stereo GR is one control voltage for both VCAs (fully linked; there is no link control).

### F3. Threshold, ratio and knee

- The ratio and its knee come from the **slope stage plus a diode onset** (S5, S6, S8). The static law is therefore curved in the dB domain: soft at 2:1 and nearly hard at 10:1 (S5).
- A lower ratio lowers the effective threshold (S1, S3, S5).
- No source gives the knee width in dB or the threshold offsets per ratio. Both are `APPROXIMATION`.
- Ratio labels on the hardware are nominal. Clones calibrate them as the output change for +10 dB from about 10 dB of GR (S9). GLUE uses the same idea: **the label equals the secant ratio over the first 10 dB above the ratio's threshold**.

### F4. Threshold calibration (knob → dBFS)

- The hardware markings differ: ±15 dB (S3, S5), ±15 dBu (S4), ±20 dBm (S6), ±20 dB (S2).
- At the console's +4 dBu nominal level, and with 0 dBFS ≈ +18..+22 dBu, a hardware threshold of 0 dBu would sit near −19 dBFS. A DAW mix bus that peaks at −6..0 dBFS would then always be deep in compression with the knob at its centre.
- **Decision (`APPROXIMATION`, reported to the coordinator).** Knob 0 dB = **−10 dBFS peak** at the detector. The skin's −20..+10 range therefore covers −30..0 dBFS peak, and the knob centre (0 dB) is a sensible start on a hot bus.

### F5. Timing

- The attack and release steps are SOURCE_CONFIRMED (S3, S4, S5, S6).
- Attack is a resistor charging the release capacitor (S6), so the capacitor also discharges through the release resistor while charging. The loop gain accelerates the effective attack (S5).
- Auto is a two-pole network (S5) with components 750 k + 0.47 µF and 91 k + 6.8 µF (S6, S8). S2's later Auto is 100 ms / 12 s: a different network, used only as a sanity check of the order of magnitude.
- How the two capacitors connect is not in our sources. Two candidates:
  - **Series** (C1 drains through R1 into C2): C2 only ever charges from C1, so a short burst and a long burst release from the same state shape. This is not program dependent, which contradicts S5 and every manual.
  - **Parallel** (C1 = 0.47 µF discharged by 750 k; C2 = 6.8 µF coupled to C1 through 91 k): a short burst barely charges C2, so the release is fast (≈40 ms). A sustained burst charges C2, which then holds C1 up, so the release is slow (seconds). This is program dependent as S5 describes.
  - **Decision.** Parallel network, `SCHEMATIC_OR_MEASURED` for the components and `APPROXIMATION` for the topology. Eigen time constants come out at about 36 ms and 6.0 s.

### F6. Side-chain HPF

- 12 dB/oct, side-chain only (S2, S3). The hardware range is OUT..300 Hz (S2) or 20..500 Hz (S3).
- The GLUE skin prints 20..300 Hz continuous with **no Off**. At 20 Hz the filter only removes subsonic content: −0.26 dB at 40 Hz and −12 dB at 10 Hz. In practice that is the "off" position. Documented as such (`TLEET_EXTENSION` range).

### F7. Make-up

- Make-up is a DC offset on the audio VCA control (S5, S6). It is applied **after** the detector tap and never changes the compression.
- Hardware ranges are 0..+15 (S3), −5..+15 (S4, S5), 0..+20 (S6) and −10..+20 (S2). The skin's −10..+20 matches S2.

### F8. Distortion

- The hardware is low distortion: 0.004 % THD+N with the compressor in (S2). The VCAs are THAT 202 / dbx 2151 class (S5).
- GLUE's **Clean** is therefore numerically clean. **TLEET** stays under about 0.1 % and grows with GR (the VCA term). **Burn** is a deliberate `TLEET_EXTENSION` drive of the VCA and output stage.

### F9. External key

An external key input exists on the outboard units (S4) and in licensed plug-ins (S3). It replaces the side-chain feed, as described in F1.

## Not established by any source (APPROXIMATION in MODEL_SPEC)

- Knee width per ratio (diode softness, 3.24 dB of control voltage).
- Threshold offset per ratio (−2.5 / −2 / −1 / 0 dB).
- The topology of the Auto network (F5).
- The side-chain VCA's exact tracking gain (GLUE uses exactly the applied gain).
- The output-stage and VCA nonlinearity coefficients.
- The threshold calibration (F4).
