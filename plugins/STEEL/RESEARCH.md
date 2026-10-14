# STEEL — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
with its source and how strong that source is. Where sources disagree, the
conflict and the decision are written down here and not repeated in the
model spec.

`TLEET // STEEL` is an **original** true-peak mastering limiter (master spec
13.11). It has no hardware reference and copies no product. Its numbers fall
into two groups:

- **Measurement definitions.** These are standardised: BS.1770 loudness and
  true peak, the EBU Mode meters and Loudness Range. STEEL follows them
  exactly, and its tests use the standards' own test cases.
- **Limiter design.** This is TLEET's own. The sources below give the
  established building blocks: the lookahead peak limiter, the gain computer
  with a soft knee, the sliding-window minimum, the half-band oversampler and
  the two-stage release. The constants that combine them are TLEET design
  values, and the tests measure what each one does.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — standard | ITU / EBU recommendations and technical documents | `SOURCE_CONFIRMED` |
| B — peer-reviewed | journal and convention papers, textbooks | `SCHEMATIC_OR_MEASURED` (structure) |
| C — reference implementation | open-source code that implements a standard | cross-check of an A source, never the only basis |
| D — context | product manuals and articles | context only |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | ITU-R BS.1770-4 (10/2015), *Algorithms to measure audio programme loudness and true-peak audio level* — <https://www.itu.int/rec/R-REC-BS.1770-4-201510-I/en> | A | **Loudness.** K-weighting is a high-frequency shelf (+4 dB) followed by a high-pass (RLB). The coefficients are given for 48 kHz. Per channel, the mean square is taken over a 400 ms gating block; blocks overlap by 75 %. L = −0.691 + 10 log10 Σ G_i z_i, with G = 1.0 for L, R and C and 1.41 for the surrounds. Integrated loudness uses two gates: an absolute gate at −70 LKFS, and a relative gate at −10 LU below the loudness of the blocks that pass the absolute gate. **True peak (Annex 2).** Oversample 4x at 48 kHz (higher ratios at lower rates, lower ratios at higher rates). An example 48-tap interpolating filter is given. Take the absolute value; dBTP = 20 log10. The Annex notes that the estimate can under-read the true value, the more so for content close to fs/2 and at low oversampling ratios. |
| S2 | ITU-R BS.1770-5 (11/2023) — <https://www.itu.int/rec/R-REC-BS.1770-5-202311-I/en> | A | Revision 5 adds channel weightings for advanced sound systems. The K-filter, gating and the true-peak method used here are the same as in revision 4. STEEL is mono or stereo only, so the two revisions give identical results for it. |
| S3 | EBU R 128 (v5, 2023), *Loudness normalisation and permitted maximum level of audio signals* — <https://tech.ebu.ch/publications/r128> | A | Programme target −23 LUFS. **Maximum permitted true peak −1 dBTP.** Loudness is measured with BS.1770 and the EBU Mode meter (Tech 3341). |
| S4 | EBU Tech 3341 (v4), *Loudness Metering: 'EBU Mode' metering to supplement EBU R 128* — <https://tech.ebu.ch/publications/tech3341> | A | **Momentary** loudness uses a 400 ms sliding rectangular window. **Short-term** uses 3 s. **Integrated** is gated as in BS.1770. Meter tolerance is ±0.1 LU. Published test cases: (1) a stereo 1 kHz sine at −23 dBFS reads −23 LUFS for M, S and I; (2) at −33 dBFS it reads −33; (3)–(5) gated sequences that give I = −23; (9) a burst pattern of 1.34 s at −20 dBFS and 1.66 s at −30 dBFS, for which S is constant at −23; (12) a burst pattern of 0.18 s at −20 dBFS and 0.22 s at −30 dBFS, for which M is constant at −23. Further cases cover true-peak vectors and multichannel signals. |
| S5 | EBU Tech 3342 (v4), *Loudness Range: A measure to supplement EBU R 128 loudness normalisation* — <https://tech.ebu.ch/publications/tech3342> | A | LRA is computed from the short-term (3 s) loudness values. Those values are gated at an absolute −70 LUFS and a **relative −20 LU** below their power mean. **LRA = L95 − L10** (the 95th minus the 10th percentile). The update rate should be at least 10 Hz. Tolerance is ±1 LU. Test cases: (1) −20 / −30 dBFS → 10 LU; (2) −20 / −15 → 5 LU; (3) −40 / −20 → 20 LU; (4) −50 / −35 / −20 / −35 / −50 → 15 LU. |
| S6 | libebur128 (J. Holzmann, MIT licence) — <https://github.com/jiixyj/libebur128> | C | A widely used implementation of S1–S5. It **derives the K-filter for any sample rate** from analogue prototype values: shelf f0 = 1681.974450955533 Hz, G = 3.999843853973347 dB, Q = 0.7071752369554196; high-pass f0 = 38.13547087602444 Hz, Q = 0.5003270373238773. At 48 kHz these values reproduce the BS.1770 table to about 1e-8. Its LRA takes the percentiles at rank round(p (n − 1)) of the sorted gated values. Its histogram mode stores block energies in 0.01 LU bins from −70 to +10 LUFS. |
| S7 | D. Giannoulis, M. Massberg, J. D. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, J. Audio Eng. Soc. 60 (6), 2012, pp. 399–408 | B | Feed-forward gain computer with a **soft knee**: GR = (x − T + W/2)² / (2W) inside the knee (for ratio ∞, the slope factor 1 − 1/R is 1). It compares level detectors and smoothing filters. A smooth attack/release branch works on the gain signal, not on the audio, so it does not distort. A decoupled peak detector gives clean release behaviour. |
| S8 | U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley 2011, ch. 4 "Nonlinear processing" (dynamics processing, limiter) | B | Structure of a **lookahead peak limiter**: delay the signal by the lookahead, compute the gain from the undelayed peak, and smooth the gain so that it reaches the required value when the peak arrives. Attack and release smoothing act on the control signal. Oversampling is needed for nonlinear stages, because their harmonics alias otherwise. |
| S9 | D. Lemire, *Streaming Maximum-Minimum Filter Using No More than Three Comparisons per Element*, Nordic Journal of Computing 13 (4), 2006 — <https://arxiv.org/abs/cs/0610046> | B | The **sliding-window minimum** (monotone wedge / deque) in amortised O(1) per sample and bounded memory. This is the "hold" of the lookahead limiter. |
| S10 | P. P. Vaidyanathan, *Multirate Systems and Filter Banks*, Prentice Hall 1993, ch. 4 | B | **Half-band filters.** Every second tap is zero and the centre tap is 0.5. In the polyphase form one branch is a pure delay, so an interpolator passes the original samples unchanged. Cascades of 2x stages give 4x and 8x. A linear-phase FIR has the integer group delay (N − 1)/2. |
| S11 | J. F. Kaiser, *Nonrecursive digital filter design using the I0-sinh window function*, Proc. IEEE ISCAS 1974; A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing*, 3rd ed., 2010, sec. 7.6 | B | Kaiser window and Kaiser's formula β = 0.1102 (A − 8.7) for A > 50 dB (and the 21–50 dB form). This is how the half-band stages' stopbands are set. |
| S12 | S. Nielsen, T. Lund, *0dBFS+ Levels in Digital Mastering*, AES 109th Convention, 2000, preprint 5251; T. Lund, *Stop Counting Samples*, AES 121st Convention, 2006, paper 6972 | B | **Inter-sample peaks.** Reconstructed signals exceed the largest sample, by +3 dB for an fs/4 tone sampled at 45°. "Sample-counting" peak meters and limiters let these overs through to DACs and codecs. This is the argument for true-peak limiting and for the −1 dBTP limit in S3. |
| S13 | V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.2, 2020, ch. 4 — <https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.2.pdf> | B | The topology-preserving-transform (TPT) state-variable filter. It is bilinear-exact and stays well behaved under cut-off modulation. Used for SUB FILTER. |
| S14 | S. Butterworth, *On the Theory of Filter Amplifiers*, Experimental Wireless 7, 1930; standard tables | B | Fourth-order Butterworth: two sections with Q = 0.5412 and 1.3066, |H|² = 1 / (1 + (fc/f)^8), −3.01 dB at fc, 24 dB/oct. |
| S15 | R. Bristow-Johnson, *Cookbook formulae for audio EQ biquad filter coefficients* — <https://www.w3.org/TR/audio-eq-cookbook/> | B | Bilinear biquad shelf and high-pass forms. The S6 K-filter is parameterised in this form. The tests' click detector uses its high-pass. |

## Findings and decisions

### F1. What "true peak" means here, and what the ceiling is held against

- S1 defines true peak operationally: oversample (4x at 48 kHz), then take the
  peak. It warns that this under-reads near fs/2. The value therefore depends
  on the interpolator. An ideal band-limited reconstruction and a practical
  DAC disagree for content close to fs/2, which the DAC's reconstruction
  filter attenuates.
- S12 shows that the overs that matter in practice come from programme
  content well below fs/2: clipped or limited masters and codec input.

**Decision.** STEEL's estimator is stricter than the S1 example. It uses 8x
(4x at 88.2/96 kHz, 2x above), a 511-tap first stage that is flat to 0.493 fs,
and a parabolic refinement of every local maximum (MODEL_SPEC 6.1). One class
feeds the limiter's ceiling stage, the output and input true-peak meters and
the loudness meter's max true peak. Every displayed true peak therefore uses
the limiter's own definition (addendum 11).

The tests check the ceiling against an **independent** reference that is
written in the test file and shares no code with the plugin. It is a 16-phase
Kaiser-windowed sinc (β = 10, ±128 taps), flat within 0.01 dB to 0.48 fs and
−6 dB at fs/2, like a high-quality DAC. The reference is validated against
analytic sine peaks (error under 0.0001 dB) and against an ideal FFT
reconstruction on limited programme (0.02 dB). The tolerance policy is in
MODEL_SPEC 6.3.

### F2. Loudness implementation: the K-filter at every sample rate

- S1 gives coefficients for 48 kHz only. It says other rates need equivalent
  filters but gives no formula.
- S6 derives them from analogue prototype values. At 48 kHz these reproduce
  S1's table to about 1e-8.

**Decision.** Use S6's parameterisation (`SOURCE_CONFIRMED` through S1's
48 kHz table, which the test checks to 2e-7). Responses at 44.1–192 kHz agree
with the 48 kHz curve within 0.04 dB from 10 Hz to 10 kHz (measured).

### F3. Gating and LRA details

- Integrated: absolute gate −70 LUFS, relative gate −10 LU (S1).
- LRA: short-term values every 100 ms, absolute gate −70 LUFS, relative
  gate −20 LU, then L95 − L10 (S5). S5 does not fix the percentile
  estimator. STEEL uses rank round(p (n − 1)), as S6 does.
- For fixed memory, the energies are kept in 0.01 LU histogram bins with
  exact energy sums per bin (as in S6's histogram mode). The only
  approximation is where a gate falls within one 0.01 LU bin.
- Mono: S1 sums the channels with their weights. A mono bus is **one channel
  with G = 1**, not dual mono. So a −23 dBFS mono tone reads −26 LUFS. This
  is S1 applied literally. Some meters assume dual mono and add 3 dB; STEEL
  does not (owner decision D9).

### F4. Lookahead limiter structure (S8, S9, S7)

A lookahead limiter needs four things:

1. The required gain per sample: ceiling / |x| above the ceiling, through
   the S7 soft knee.
2. A hold over the lookahead, so the gain is already down when the peak
   arrives. This is the sliding minimum of S9.
3. A smooth attack ramp that ends exactly on the peak.
4. A release.

STEEL implements the ramp as a cascade of moving averages (box filters)
over the attack window. A box filter preceded by a sliding minimum of the
same support can never exceed the required gain at the peak sample. The
guarantee is therefore exact by construction, not tuned. This is
`APPROXIMATION` only in the sense that the attack shape (linear,
quadratic-S or cubic-S) is a design choice.

### F5. Release: program-dependent "auto"

None of the sources gives one auto-release law. S7 discusses smooth
branching. Two-stage releases (a fast stage for transients and a slow stage
for sustained gain reduction) are the common practice in commercial
mastering limiters (D-class context; no specific product is copied).

**Decision.** AUTO = two branches:

- a fast linear-gain release that recovers isolated peaks;
- a slow branch, run in dB at the host rate, that follows a mode-specific
  share of the fast branch's GR with its own attack and release.

The applied gain is the lower of the two. Measured result: no pumping on
transients over a sustained bed (0.02–0.6 dB, against 1.5–2.0 dB with a
1 s manual release), and less LF ripple than a 20 ms release (MODEL_SPEC 4.3).

### F6. Oversampling filters (S10, S11)

- Linear-phase half-band FIR stages, Kaiser-windowed, N = 4k + 3:
  - stage 1: N = 127, 90 dB;
  - stages 2 and 3: N = 35 and N = 23, 100 dB.
- Linear phase makes the latency an exact integer.
- The polyphase pure-delay branch means a signal that is not limited passes
  the original samples through bit-exactly at the oversampled rate.
- The round trip is rounded up to whole host samples by a delay pad, so the
  reported latency is exact: 0 / 63 / 72 / 75 host samples for 1x / 2x / 4x / 8x.
- **Alternative rejected:** minimum-phase IIR half-bands are cheaper and have
  almost no latency. But they have non-constant group delay, which smears
  transients and makes "reported = actual" true only at DC. The spec asks for
  documented phase and latency (master 6.4), and STEEL already has lookahead
  latency, so linear phase costs little extra.

### F7. Soft clip before the limiter

Clipping produces harmonics, which alias without oversampling (S8). STEEL
runs the clipper **inside** the oversampled section, before the lookahead
limiter. The limiter then catches what the clipper leaves above the ceiling,
so the ceiling guarantee holds whatever the clipper does. Measured aliasing
of a 7 kHz tone, 6 dB over the ceiling at AMOUNT 100 %, relative to the
fundamental: −29 dB at 1x, −51 dB at 2x, −94 dB at 4x and 8x.

### F8. Sub filter

There is no standard; this is a mastering convenience. A 4th-order Butterworth
high-pass (S14) as a TPT SVF (S13) is −3.01 dB at the selected 20, 25 or
30 Hz. It glides between those positions without zipper noise.

## Not established by any source (APPROXIMATION / TLEET_EXTENSION in MODEL_SPEC)

- The four MODE parameter sets: attack fraction, ramp shape, knee, the AUTO
  branch constants and the clip scale (MODEL_SPEC 4.4).
- The ceiling stage's hold (fs/3000) and ramp (0.25 ms) (MODEL_SPEC 6.3).
- The soft-clip curve (quadratic C1 knee) and its AMOUNT law (MODEL_SPEC 4.6).
- LEVEL MATCH timing (3 s window, 1 s smoothing, 100 ms gain ramp) and its
  clamp to −30..0 dB (MODEL_SPEC 7.3).
- Warning thresholds: TP OVERS at ceiling + 0.1 dB held for 2 s; HEAVY
  LIMITING at a mean GR of 4 dB over 2 s (MODEL_SPEC 9.5).
