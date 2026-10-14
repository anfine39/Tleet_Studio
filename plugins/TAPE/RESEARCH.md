# TLEET // TAPE: research notes and sources

The model behind TAPE (MODEL_SPEC.md) is built from published machine and tape data,
magnetic-recording textbooks and one peer-reviewed real-time tape model. This file lists
those sources (R1-R14), what each one supports, and how reliable it is. MODEL_SPEC.md and
`dsp/TapeModel.h` cite them by number. Each constant carries a provenance tag:
[HW] published hardware data, [LIT] literature, [APX] a value chosen inside the
literature's range, [EXT] a TLEET decision.

Studer schematics and alignment data for the A800 are not openly published. The machine's
published *performance* (R1) is therefore the target. The *mechanisms* come from the
literature (R3, R4, R7, R8, R11). Wherever a value is fitted or chosen, MODEL_SPEC says so.

## Sources

**R1. Studer A800 technical data.** As reproduced at
<https://www.vintagedigital.com.au/studer-a800/>; the same table appears in Studer's A800
brochures and operating instructions. [HW]

| Item | Value |
|---|---|
| Speeds | 7.5 / 15 / 30 ips, crystal controlled, ±0.2 % |
| Equalisation (CCIR) | 70 µs at 7.5 ips, 35 µs at 15 ips, 17.5 µs at 30 ips |
| Equalisation (NAB) | 50 / 3180 µs at 7.5 and 15 ips; 17.5 µs at 30 ips |
| Response, ±2 dB | 7.5 ips: 30 Hz-15 kHz; 15 ips: 30 Hz-20 kHz; 30 ips: 50 Hz-20 kHz |
| Response, ±1 dB | 60 Hz-12 / 18 / 20 kHz |
| S/N, unweighted, record-repro | 66 dB at 7.5 and 15 ips, 70 dB at 30 ips |
| Wow and flutter, weighted | 0.06 % (7.5 ips), 0.04 % (15 ips), 0.04 % (30 ips) |
| Distortion | 3rd harmonic at 1 kHz ≤ 1 % |
| Crosstalk, 15 ips, repro | ≥ 40 dB, 100 Hz-12 kHz |
| Bias / erase frequency | 240 kHz / 80 kHz |
| Line level | maximum input and output +24 dBm |

The S/N reference is not stated there; see R10.

**R2. ATR Magnetics, "ATR Master Tape technical data sheet".**
<https://www.duplication.com/atr_magnetics/ATR-Master-Tape-Technical-Data-Sheet-Standard.pdf>. [HW]
A current high-output mastering tape.

- Reference level 320 nWb/m. MOL (1 kHz, 3 % THD) +12 dB re that level, which is +16.8 dB re 185 nWb/m.
- Modulation noise ratio −68 dB.
- Overbias that sets the recommended bias (10 kHz output falling from its peak), by repro gap and speed (30 / 15 / 7.5 ips):

  | Gap | 30 ips | 15 ips | 7.5 ips |
  |---|---|---|---|
  | 0.25 mil | 1.5 dB | 3.0 dB | 5.0 dB |
  | 0.3 mil | 1.2 dB | 2.5 dB | 4.5 dB |
  | 0.5 mil | 1.0 dB | 2.0 dB | 4.0 dB |

  The near-doubling per halving of speed is the wavelength law of the recording zone (R3).

**R3. H. N. Bertram, *Theory of Magnetic Recording*, Cambridge University Press, 1994.** [LIT]
The playback wavelength losses: gap loss sin(kg/2)/(kg/2), spacing loss exp(−kd) and
thickness loss, with k = 2π/λ. Also the recording zone and its depth against bias, and the
long-wavelength "contour" (head bump) response of a finite repro head.

**R4. C. D. Mee and E. D. Daniel (eds.), *Magnetic Recording Technology*, 2nd ed., McGraw-Hill, 1996.** [LIT]

- AC-bias recording makes the remanent magnetisation follow the anhysteretic curve, which linearises the transfer.
- Under-bias leaves the hysteretic (irreversible) behaviour exposed, with more distortion and more treble.
- Over-bias erases the short wavelengths in the coating.
- A magnetised head adds a DC field: even harmonics and extra noise.
- Modulation noise.

**R5. Reference fluxivities in studio practice.**
185 nWb/m ("Ampex operating level", 0 VU on a standard alignment); "+3" ≈ 260 nWb/m and
"+6" = 370 nWb/m (elevated alignments of high-output tape); 320 nWb/m (DIN / IEC).
These are the level conventions of MRL and IEC alignment tapes. The A800 is aligned this
way: 0 VU recorded at one of these fluxivities. [LIT / common practice]

**R6. IEC 60094 (IEC 94) and the AES 30 ips equalisation.**
The record/repro equalisation time constants 70 / 35 µs (IEC/CCIR) and 17.5 µs (AES, 30 ips).
These are the CCIR values the A800 lists (R1). [LIT]

**R7. D. C. Jiles and D. L. Atherton, "Theory of ferromagnetic hysteresis", *J. Magn. Magn. Mater.* 61 (1986) 48-60.** [LIT]
The hysteresis model: a Langevin anhysteretic curve, pinning k, inter-domain coupling α and reversibility c.

**R8. J. Chowdhury, "Real-time physical modelling for analog tape machines", Proc. DAFx-19, Birmingham, 2019.**
<https://dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf>, with the open-source
implementation at <https://github.com/jatinchowdhury18/AnalogTapeModel>. [LIT]

- The J-A differential equation as a tape model (eq. 6, 9, 10), solved with RK2/RK4 at an oversampled rate.
- The playback losses as a linear-phase FIR (eq. 13).
- Published parameters: Ms = 3.5·10⁵, a = 2.2·10⁴, α = 1.6·10⁻³, k = 2.7·10⁴, c = 1.7·10⁻¹. TAPE's type B is exactly that k and α in normalised form (k' = k/a = 1.227, α' = α Ms / a = 0.02545).

TAPE differs from R8 in how bias enters the model (MODEL_SPEC 4.2): R8 adds the bias as a
high-frequency signal, while TAPE uses the anhysteretic-recording result of R4 instead.

**R9. Tape formulation data sheets (MOL ranges).**

- RMGI SM911 data sheet: <https://www.fullcompass.com/common/files/6763-RMGISM911.pdf>
- ATR (R2)

Classic "standard output" tapes reach MOL ≈ +8…+10 dB re 185 nWb/m, the 456 class
≈ +12, and high-output mastering tapes +14…+17. TAPE's types sit at +9 / +12 / +15 (A / B / C).
[LIT ranges, APX values]

**R10. GroupDIY forum, "A800 noise floor testing".**
<https://groupdiy.com/threads/a800-noise-floor-testing.88724>. Secondary source: a forum.
Posters quoting the Studer manual give the 66 dB record-repro S/N of R1 as unweighted, re
510 nWb/m. TAPE takes that reference: 66 dB re 510 nWb/m is 57.2 dB re 185 nWb/m.
**Reliability: moderate.** Owner decision D7 in MODEL_SPEC.

**R11. R. L. Wallace Jr., "The reproduction of magnetically recorded signals", *Bell System Technical Journal* 30 (1951) 1145-1173.** [LIT]
The spacing loss, 54.6 d/λ dB.

**R12. Filters.**

- V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.2, Native Instruments.
- A. Simper, "Linear trapezoidal integrated state variable filter", Cytomic technical papers.

TPT/SVF filters for the head bump, dip, roll-off, shelves and noise shaping. They stay
stable and zipper-free under modulation. [LIT]

**R13. Wow and flutter measurement.**

- IEC 60386 (1972), DIN 45507 and AES6-2008: 3150 Hz test tone, frequency demodulation, weighting that peaks at 4 Hz, and the "2-sigma" peak value (the deviation exceeded 5 % of the time).
- Virtins, "Wow and flutter measurement using Multi-Instrument": <https://www.virtins.com/doc/Wow-and-Flutter-Measurement-using-Multi-Instrument.pdf>. It gives the weighting factors 0.0296 at 0.2 Hz, 0.5 at 0.8 Hz, 1 at 4 Hz and 0.508 at 20 Hz.

The tests use a weighting fitted exactly to those points (MODEL_SPEC 14.1). [LIT]

**R14. J. F. Kaiser, "Nonrecursive digital filter design using the I0-sinh window function", Proc. IEEE ISCAS 1974.** [LIT]
The Kaiser-window half-band filters of the oversampler; the same design as STEEL's.

## Notes on reliability

- **R1 is the target for every performance figure.** The tests check frequency response, S/N, wow and flutter, 3rd-harmonic distortion and crosstalk against it.
- **R10 (S/N reference) is the weakest link.** If the 66 dB were re 320 nWb/m, the machine's hiss would be 4 dB lower. That is the NOISE AMOUNT difference between 12 % and 8.5 %.
- **The tape types are TLEET profiles (spec: "TLEET profiles unless mapped to specific formulations").** They have generic names, no trademarks, and MOLs inside the R9 ranges.
- **No schematic or measured A800 transfer data was available.** The record and repro amplifier stages are modelled from their published level limits (R1) only.
