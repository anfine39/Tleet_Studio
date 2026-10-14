# VOID — research notes

Evidence behind `MODEL_SPEC.md`. VOID is an **original algorithmic stereo
reverb** (master spec 13.9: "This is not a hardware clone"). No hardware unit
or commercial plugin was measured or copied. The five algorithms combine
structures that are well documented in the open literature (Schroeder,
Moorer, Jot, Dattorro, Gardner, Allen & Berkley), with TLEET's own
constants, decay calibration and controls. This file lists every published
idea the model uses, what each source establishes, and where VOID departs
from it.

The sources are classic, widely cited papers and book chapters. They are
cited from the literature; this build environment did not re-fetch their
full texts (2026-10-14), so no value below depends on a figure that could not
be checked in the papers' well-known published form. Where VOID uses a
number taken from a paper (Dattorro's plate tank), it is marked; every other
number is a TLEET choice, tagged `APPROXIMATION` or `TLEET_EXTENSION` in
`MODEL_SPEC.md`.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| B — engineering literature | peer-reviewed papers, theses, book chapters on artificial reverberation | `SCHEMATIC_OR_MEASURED` structure; the basis of every `APPROXIMATION` |
| B' — measurement standards | ISO 3382-1, Schroeder integration | how the tests measure, not what the DSP does |
| C — numerical methods | interpolation, antialiasing, filter structures | `APPROXIMATION: numerical method` |

There is no class A (manufacturer) source: VOID imitates no product.

## Sources

| ID | Source | Class | What it establishes for VOID |
|---|---|---|---|
| S1 | M. R. Schroeder, "Natural sounding artificial reverberation", *J. Audio Eng. Soc.* 10(3), 1962, pp. 219–223 | B | Recirculating delays (combs) for the decay, series allpass filters for echo density without colouring the long-term spectrum. VOID's input diffusers, Void's bloom and the in-loop allpasses are Schroeder allpasses. |
| S2 | J. A. Moorer, "About this reverberation business", *Computer Music Journal* 3(2), 1979, pp. 13–28 | B | Early reflections as a tapped delay line ahead of the late reverberator; lowpass in each recirculating loop for frequency-dependent decay; mutually prime delays. ROOM/CHAMBER/HALL use a tapped early-reflection line feeding the late network (MODEL_SPEC 4.2). |
| S3 | J.-M. Jot, A. Chaigne, "Digital delay networks for designing artificial reverberators", AES 90th Convention, Paris, 1991, preprint 3030 | B | The feedback delay network (FDN) with a lossless (unitary) feedback matrix; decay set by an **absorption filter per delay line** whose gain is γ^m (m = line length), so that every mode decays at the same rate: **the RT60 is a property of γ, independent of the lengths**; a **tone-correction filter** at the output that removes the spectral colouring the absorption filters impose on the initial level. VOID's whole decay calibration is this (MODEL_SPEC 5). |
| S4 | J.-M. Jot, "An analysis/synthesis approach to real-time artificial reverberation", *Proc. IEEE ICASSP* 1992, vol. 2, pp. 221–224; and Jot's thesis (ENST Paris, 1992) | B | Frequency-dependent RT60(ω) via per-line absorption filters h_i with 20·log10 |h_i(ω)| = −60 · m_i / (f_s · RT60(ω)); tone correction with |t(ω)|² ∝ 1 / RT60(ω) so the late field's energy spectrum stays flat. The substitution **z⁻¹ → γ z⁻¹** applied to every delay of a network scales the impulse response by γⁿ exactly. VOID uses two-band (shelving) absorption and applies γ to *every* late-path delay, including those inside allpasses (F1). |
| S5 | J. Dattorro, "Effect design, part 1: reverberator and other filters", *J. Audio Eng. Soc.* 45(9), 1997, pp. 660–684 | B | The figure-of-eight plate-class tank (after Griesinger's Lexicon designs): mono input through four input diffusers (lattice allpasses, coefficients 0.75, 0.75, 0.625, 0.625), two tank halves of modulated allpass (decay diffusion 1 = 0.70) → delay → damping → decay → allpass (decay diffusion 2 = 0.50) → delay, each half feeding the other; output by summing seven taps per channel from both halves (Table 2); figures given in samples at **29 761 Hz**. **PLATE uses this topology and these figures** (scaled to the host rate and by SIZE), with Jot absorption instead of Dattorro's one-pole damping + single decay gain. |
| S6 | W. G. Gardner, "A realtime multichannel room simulator", *J. Acoust. Soc. Am.* 92(4), 1992 (abstract, 124th ASA meeting); W. G. Gardner, "Reverberation algorithms", in M. Kahrs, K. Brandenburg (eds.), *Applications of Digital Signal Processing to Audio and Acoustics*, Kluwer, 1998, ch. 3 | B | **Nested allpass** filters (an allpass whose delay contains further allpasses) and Gardner's small/medium/large room reverberators: cascades of nested allpasses and delays closed by a single feedback loop with a lowpass, outputs tapped from inside the cascade. **CHAMBER** is built from this idea (two nested-allpass loops, cross-coupled by a rotation). Gardner (1998) also surveys FDNs and the role of modulation. |
| S7 | V. Välimäki, J. D. Parker, L. Savioja, J. O. Smith, J. S. Abel, "Fifty years of artificial reverberation", *IEEE Trans. Audio, Speech, Lang. Process.* 20(5), 2012, pp. 1421–1448 | B | The survey VOID follows for its taxonomy: delay-network (Schroeder/Moorer, FDN, waveguide), convolution and physical room models; **the late field needs echo density ~ 1000 echoes/s or more and modal density high enough to avoid ringing**; FDN feedback matrices (Householder, Hadamard) and their cost; time-varying (modulated) delays to break up metallic resonances; velvet-noise based reverberation. |
| S8 | J. B. Allen, D. A. Berkley, "Image method for efficiently simulating small-room acoustics", *J. Acoust. Soc. Am.* 65(4), 1979, pp. 943–950 | B | The image-source method for a rectangular room: each reflection order adds mirrored sources; delay = distance / c, amplitude ∝ β^(reflections) / distance. VOID computes its early-reflection tables this way (shoebox rooms of its own choosing) once at start-up (MODEL_SPEC 4.2). |
| S9 | M. Karjalainen, H. Järveläinen, "Reverberation modeling using velvet noise", AES 30th Int. Conference (Intelligent Audio Environments), Saariselkä, 2007; V. Välimäki, B. Holm-Rasmussen, B. Alary, H.-M. Lehtonen, "Late reverberation synthesis using filtered velvet noise", *Applied Sciences* 7(5):483, 2017 | B | Sparse ±1 pulse sequences (velvet noise) perceived as smooth noise at ~2000 pulses/s; usable as decorrelating diffusers. **Considered and not used** (F6). |
| S10 | T. I. Laakso, V. Välimäki, M. Karjalainen, U. K. Laine, "Splitting the unit delay", *IEEE Signal Processing Magazine* 13(1), 1996, pp. 30–60 | C | Fractional delay: Lagrange interpolation (FIR, magnitude error, exact at integers) and the **first-order allpass interpolator** η = (1 − d)/(1 + d) (unity magnitude, phase approximation best for 0.5 ≤ d ≤ 1.5). VOID reads modulated loop delays with the allpass interpolator (lossless, so the decay calibration holds) and static/pre-delay taps with cubic Lagrange. |
| S11 | V. Välimäki, T. I. Laakso, "Suppression of transients in variable recursive digital filters with a novel and efficient cancellation method", *IEEE Trans. Signal Processing* 46(12), 1998, pp. 3408–3414 | C | Changing the coefficients/structure of a recursive filter produces a transient because the state belongs to the old filter; it can be cancelled by **updating the state** as if the new filter had been running. VOID's allpass interpolator re-derives its state from the delay line's history whenever the integer part of a moving delay changes (MODEL_SPEC 6.6). |
| S12 | D. Rocchesso, J. O. Smith III, "Circulant and elliptic feedback delay networks for artificial reverberation", *IEEE Trans. Speech Audio Process.* 5(1), 1997, pp. 51–63; S. J. Schlecht, E. A. P. Habets, "On lossless feedback delay networks", *IEEE Trans. Signal Processing* 65(6), 2017, pp. 1554–1564 | B | Conditions for a lossless FDN (unitary/orthogonal feedback matrix suffices); efficient orthogonal matrices (Householder N−1 adds, Hadamard via the fast Walsh–Hadamard transform N·log2 N). VOID's ∞ DECAY and FREEZE rely on exactly this losslessness (F3). |
| S13 | S. J. Schlecht, E. A. P. Habets, "Time-varying feedback matrices in feedback delay networks and their application in artificial reverberation", *J. Acoust. Soc. Am.* 138(3), 2015, pp. 1389–1398; D. Griesinger, "Practical processors and programs for digital reverberation", AES 7th Int. Conference, Toronto, 1989 | B | Modulation of delay lengths (Griesinger/Lexicon "randomisation") or of the matrix smooths the modal response and removes ringing; modulation must stay slow and small to avoid audible pitch artefacts. VOID modulates delay lengths (sine + band-limited random), with the pitch deviation capped (MODEL_SPEC 6.6). |
| S14 | J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx-16, Brno, 2016; S. Bilbao, F. Esqueda, J. D. Parker, V. Välimäki, "Antiderivative antialiasing for memoryless nonlinearities", *IEEE Signal Processing Letters* 24(7), 2017, pp. 1049–1053 | C | First-order antiderivative antialiasing (ADAA): y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1]); half a sample of delay, a gentle lowpass, strong reduction of aliasing for smooth nonlinearities. Used for RUIN's input saturator (MODEL_SPEC 8.3). |
| S15 | V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, rev. 2.1.2, 2020 (earlier eds. 2012–2018) | C | Topology-preserving transform (TPT) state-variable filter and one-pole: bilinear-exact responses, well behaved while coefficients move. All of VOID's linear filters (HPF/LPF, shelves, DARK band limit) use it. |
| S16 | J. S. Abel, P. Huang, "A simple, robust measure of reverberation echo density", AES 121st Convention, San Francisco, 2006 | B' | The normalised echo density (NED) profile: fraction of samples outside one standard deviation in a sliding window, / erfc(1/√2); 1 = Gaussian (fully mixed). The tests use it to show the algorithms differ (MODEL_SPEC 12). |
| S17 | M. R. Schroeder, "New method of measuring reverberation time", *J. Acoust. Soc. Am.* 37, 1965, pp. 409–412; ISO 3382-1:2009 | B' | Backward-integrated energy decay curve; T30 = least-squares slope from −5 to −35 dB, × 2. The decay-time tests measure this way in the 1 kHz octave. |
| S18 | L. Dahl, J.-M. Jot, "A reverberator based on absorbent all-pass filters", DAFx-00, Verona, 2000 | B | Allpass filters with absorption inside the recursion as reverberator building blocks; supports applying the decay inside VOID's allpasses (F1). |

## Findings

**F1 — Decay calibration with allpasses in the loop.** Jot's absorption
gain γ^m makes the RT60 exact when the loop consists of pure delays (S3,
S4). VOID's loops also contain allpasses (Plate tank, Chamber nested
allpasses, Void's in-line allpasses, the input diffusers ahead of the loop),
and an allpass delays energy by much more than its delay length on average
(its impulse response rings). Applying γ only to the pure delays made
measured RT60 15–21 % too long for PLATE/VOID at short decays. Applying
Jot's substitution z⁻¹ → γz⁻¹ inside every allpass (S4, S18) — the allpass
delay line's output is multiplied by γ^D — makes the late response exactly
the lossless network's response times γⁿ, so the 1 kHz RT60 equals DECAY
for any topology. Measured after the change: worst 4.0 % over 5 algorithms
× DECAY 0.8–12 s × 44.1/48/96/192 kHz (T30). Decision: MODEL_SPEC 5.1.

**F2 — Short decays and T30.** Below ~0.8 s a reverb's onset (early
reflections, input diffusion, the plate's ~0.25 s tank transit, Void's
bloom) is a large part of the whole decay, and with few loop passes the EDC
is a staircase; ISO T30 of the full response then measures the onset as
much as the decay (measured T30/DECAY at 0.5 s: Room 0.94, Plate 1.09,
Chamber 1.06, Hall 0.97, Void 1.21). The late decay *rate* is still exact by
construction (F1). Decision: the decay-time acceptance (13.9 "decay-time
validation") is T30 within 5 % for DECAY ≥ 0.8 s; below that the tests check
that the response falls 60 dB within 1.6·DECAY + 0.35 s and that shorter
DECAY is shorter (MODEL_SPEC 5.4).

**F3 — "∞" decay and FREEZE need a lossless network, and a lossless network
needs a bound.** An orthogonal feedback matrix with unit-gain delays and
allpasses is lossless (S12). With input still entering, a lossless loop
integrates energy without limit; with modulated delays the stored energy
also random-walks (each pass through a lengthening or shortening delay
stretches or compresses the waveform it holds, S13). Hence: a slow,
transparent-below-threshold loop gain (the bound) for ∞, and for FREEZE a
level-hold servo of a few hundredths of a dB per second (MODEL_SPEC 6.4,
6.5). The bound is referenced to the late output (−12 dBFS RMS per channel
for ∞ and FREEZE, transparent below). Measured: frozen level drift ≤ 0.065 dB
over 107 s for every algorithm (0.17 dB for PLATE without the servo, growing
with time as a random walk); ∞ decay without input: ≤ 0.05 dB over 18 s.

**F4 — Filters inside a lossless loop act every pass.** A 20 kHz
second-order lowpass is −0.26 dB at 10 kHz per pass; at 30 passes/s that is
−8 dB/s of top-octave loss, so a "default" LOOP filter would make ∞ decay
lossy and darken every tail. Decision: the filters' end stops are truly open
(MODEL_SPEC 6.1, owner decision D5).

**F5 — Modulation artefacts.** The allpass interpolator is lossless (S10),
which keeps the decay calibration exact while delays move, but each time a
moving delay crosses an integer the interpolator's state is stale and a
small transient results (S10, S11). With the state update of S11 the 8 kHz+
residue of a 220 Hz tone at MOD 100 % / 5 Hz dropped by 4–8 dB (worst case
−58.9 dB re the wet RMS, Void RUIN 5 Hz; −86 to −100 dB at the default rate).

**F6 — Velvet-noise diffusion (S9) not used.** Velvet-noise decorrelators
would give a smoother onset at lower cost, but a sparse FIR's response is
fixed in time: it cannot be scaled smoothly by SIZE, and it is not lossless
as a loop element. VOID's diffusers must scale with SIZE (Void's bloom) and
sit inside lossless loops (∞, FREEZE), so Schroeder allpasses are used
throughout. Recorded as a possible future CPU optimisation for the
non-recirculating early part.

**F7 — Echo density and the "algorithms must materially differ" rule.**
13.9 requires different topology/constants/diffusion structure, not one
network with different settings. VOID uses four different structures (FDN
with ER bank, Dattorro tank, nested-allpass loops with ER bank, FDN with
in-line allpasses and a scaled bloom), and the tests verify the difference
with S16's NED, the early energy fraction, centre time and energy peak
(MODEL_SPEC 12, `VOID_algorithms.csv`).

**F8 — Saturation aliasing.** RUIN's input saturator x/√(1+x²) at +12 dB
drive gives −37 dB of inharmonic (aliased) energy for a 4.4 kHz tone at
−6 dBFS without antialiasing. First-order ADAA (S14), which for this curve
has the closed form (x₁ + x₀)/(√(1+x₁²) + √(1+x₀²)) (no ill-conditioned
difference quotient), brings it to −45.6 dB at no measurable CPU cost.
Oversampling the stage ×2 would add a half-band filter pair per channel;
not needed at this level for a deliberately degraded colour (MODEL_SPEC 8.3).

## Open research tasks

- **R1** Listening comparison of the five algorithms against published
  descriptions of room/plate/chamber/hall character (S6, S7); the constants
  are TLEET's own and may be revoiced without changing the topology.
- **R2** Perceptual threshold of the modulation residue at MOD 100 % / 5 Hz
  (F5) on program material.
- **R3** Velvet-noise early diffusion (F6) as a CPU optimisation.
