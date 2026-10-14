# ECHO — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
here with its source and how strong that source is. Where sources disagree,
the conflict and the decision are written down here.

ECHO is informed by the **Roland RE-201 Space Echo** (Roland, 1974 onward): a
tape echo with one record head, three playback heads on an endless tape loop,
a variable-speed capstan motor (REPEAT RATE), feedback (INTENSITY), a
two-band tone control on the echo, and a spring reverb, selected by a
12-position MODE selector. ECHO is not a measured 1:1 clone of one unit. No
unit was measured for this project. Head times come from the manufacturer's
licensed model; everything nobody publishes is an `APPROXIMATION`, and the
open tasks at the end say what to measure.

All sources were accessed on 2026-10-13. Sources that could not be read from
this build environment are listed under "Blocked sources". Their content was
not guessed.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — manufacturer | Roland product documentation | `SOURCE_CONFIRMED` |
| A' — licensed model | the manual of Roland's licensed software RE-201 (Universal Audio), and UA's own tape-echo model built from the same hardware | `SOURCE_CONFIRMED` for behaviour and ranges the hardware shares |
| B — engineering | peer-reviewed papers on spring reverberation and tape modelling | `SCHEMATIC_OR_MEASURED` structure; basis of `APPROXIMATION`s |
| C — retail / restorer specification sheets | published specification lists | `SOURCE_CONFIRMED` for counts (springs, selector positions), never for a curve |
| D — articles / forums | magazine articles, forums | context only, never the sole basis for a law |

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | Roland, *RE-201 Owner's Manual* — <https://cdn.roland.com/assets/media/pdf/RE-201_OM.pdf> | A | **Not readable here** (the PDF is a scan with no text layer). Nothing is taken from it. |
| S2 | Universal Audio, *Roland RE-201 Space Echo Manual* (the Roland-licensed model) — <https://help.uaudio.com/hc/en-us/articles/33538271334164-Roland-RE-201-Space-Echo-Manual> | A' | Delay ranges **head 1: 69–177 ms, head 2: 131–337 ms, head 3: 189–489 ms**. REPEAT RATE "varies the tape playback speed in realtime by manipulating the tape capstan motor"; clockwise = shorter. The virtual heads "always maintain their proportional relationship to each other". With tempo sync the control is quantised to note values "at the leading head" (head 1). **12 MODE positions: 4 echo only, 7 echo + reverb, 1 reverb only**; positions 1–4: H1, H2, H3, H2+H3; 5–11 (with reverb): H1, H2, H3, H1+H2, H2+H3, H1+H3, H1+H2+H3; 12: reverb only (the table renders poorly; verify against S1). REVERB VOLUME has no effect in positions 1–4; ECHO VOLUME none in Reverb Only. INTENSITY: "Higher values will cause self-oscillation; the exact position is program and Mode dependent"; it can oscillate with no input. BASS and TREBLE: cut/boost on the tape echo only (not the dry, not the reverb), flat at 12 o'clock; **no frequencies or dB ranges given**. The spring model "has no noise". Input: driving the preamp past unity adds clipping that colours echo and reverb. |
| S3 | Universal Audio, *Galaxy Tape Echo Manual* — <https://help.uaudio.com/hc/en-us/articles/4419513003668-Galaxy-Tape-Echo-Manual> | A' | The same head ranges (69–177 / 131–337 / 189–489 ms), heads "spaced at a fixed distance" with a fixed proportional relationship. Echo Rate changes the tape speed in real time with a "musical ramp-up and ramp-down". A Tape Age switch (new / used / old): worn tape "loses fidelity and gains wow and flutter". Single-head modes oscillate more simply than multi-head modes. Peak lamp at about −2 to −1.5 dB after the input control. No wow/flutter rates, no tone frequencies. |
| S4 | Roland/BOSS, *RE-202 Reference Manual* — <https://static.roland.com/assets/media/pdf/RE-202_reference_eng01_W.pdf> | A | Roland's own digital Space Echo: "The delay times for playback heads 2, 3 and 4 are **2x, 3x and 4x** the delay time of playback head 1" (head 4 "was not on the RE-201"). Tap time sets head 1; tap maximum **1 s** (Normal) / **2 s** (Long range). REPEAT RATE "adjusts the tape speed … The tone also changes along with the speed." WOW & FLUTTER: wavering "due to the friction resistance of the tape mechanism". SATURATION: "preamp distortion and the magnetic saturation of the tape". TAPE: new vs. "somewhat used" tape changes tone and wavering. INTENSITY fully clockwise oscillates. |
| S5 | V. Välimäki, J. Parker, J. S. Abel, *Parametric spring reverberation effect*, J. Audio Eng. Soc. 58(7/8), 2010, pp. 547–562 | B | The parametric spring structure: two recirculating chains, C_lf (low chirps) from cascaded *stretched* first-order allpasses followed by a lowpass at the transition frequency f_C, and C_hf (high chirps). Read through S6's summary; the JAES text itself was not available here. |
| S6 | J. Parker, *Efficient dispersion generation structures for spring reverb emulation*, EURASIP J. Adv. Signal Process. 2011:646134 — <https://acris.aalto.fi/ws/portalfiles/portal/13004391/art_10.1155_2011_646134.pdf> | B | Stretched allpass H(z) = (a + z^-k)/(1 + a z^-k); cascade of M sections; group delay **D = kM(1 − a²)/(1 + 2a cos(ωk) + a²)** samples; low chirps only below f_C, the group delay peaks at f_C; high chirps across the band "at much lower amplitude". S5's chain lengths M_low = 100, M_high = 200; C_lf lowpass as an 8th-order crossover; random delay modulation, cross-coupling and EQ are optional refinements. f_C above 5 kHz "rarely seen". |
| S7 | J. S. Abel, D. P. Berners, S. Costello, J. O. Smith III, *Spring reverb emulation using dispersive allpass filters in a waveguide structure*, AES 121st Convention, 2006 | B | The dispersive-allpass waveguide idea behind S5 (cited by S5/S6; not read in full here). |
| S8 | S. Bilbao, J. Parker, *A virtual model of spring reverberation*, IEEE Trans. Audio, Speech, Lang. Process. 18(4), 2010, pp. 799–808 | B | Physical (finite-difference) helical-spring model; the alternative to S5 at a much higher cost (cited, not used). |
| S9 | Sweetwater, *Roland RE-201 Technical Specifications* — <https://www.sweetwater.com/sweetcare/articles/roland-re-201-technical-specifications/> | C | **Rotary 12-way mode selector**; **reverb unit "3 springs system"**; controls: Mic Volume ×2, Instrument Volume, Treble, Bass, Reverb (volume), (Echo) Volume, Repeat Rate, Intensity. |
| S10 | Vintage Digital, *The Legendary Roland RE-201 Space Echo* — <https://www.vintagedigital.com.au/roland-re-201-space-echo/> | C | Record head, three playback heads, variable-speed motor; "direct drive type brushless electronic servo motor"; endless tape; 3-spring reverb; the 12-position selector chooses heads and reverb. No delay, wow/flutter or tone figures. |
| S11 | Roland Articles, *Tips and Tricks: Roland Cloud RE-201 Space Echo* — <https://articles.roland.com/tips-and-tricks-roland-cloud-re-201-space-echo/> | D | The head's distance from the record head plus REPEAT RATE sets the delay; mode 1 uses the closest head; the preamps "impart saturation"; a hot input gives a dirtier sound. No numbers. |
| S12 | Fractal Audio forum thread on the RE-201's wow/flutter — <https://forum.fractalaudio.com/threads/volantes-re-201-space-echo-sound-quality-wow-flutter-mechanics-please-help-to-mimic.212719/> | D | No measured wow/flutter data (checked; nothing used). |

## Findings and decisions

### F1 Head spacing — sources disagree

S2 and S3 (the licensed RE-201 model) give the three heads' delay ranges.
Their ratios are the same at both ends of the speed range:

| | fastest | slowest | ratio to head 1 |
|---|---|---|---|
| head 1 | 69 ms | 177 ms | 1 |
| head 2 | 131 ms | 337 ms | 1.899 / 1.904 |
| head 3 | 189 ms | 489 ms | 2.739 / 2.763 |

S4 (Roland's own digital RE-202) uses an idealised **1 : 2 : 3 (: 4)**. The
master spec asks for researched ratios and allows 1:2:3 only when documented.
**Decision: 1 : 1.90 : 2.75** (S2/S3 rounded to two decimals). The RE-202's
1:2:3 is a digital simplification, not the RE-201's head block.
[SOURCE_CONFIRMED ranges; owner-delegated choice of the RE-201 values]

### F2 Tape speed range

S2/S3: head 1 runs 69–177 ms, a speed range of 2.57 : 1 on the hardware.
REPEAT RATE in the registry is 20–2000 ms, and SYNC needs up to a whole note
at 40 BPM (6 s). The model keeps the physical mechanism (one tape speed, the
heads at fixed distances) over a wider range, and the head-loss law
(F4) holds the hardware's tone outside 69–177 ms. [TLEET_EXTENSION range]

### F3 Speed changes ramp; the pitch bends

S2: REPEAT RATE drives the capstan motor; S3: "musical ramp-up and ramp-down";
S4: "the tone also changes along with the speed". A tape echo has no read
pointer that can jump: the material on the tape is replayed faster or slower
(pitch bend). The model integrates the tape position from the speed history
(MODEL_SPEC 4). The motor's time constant is not published: 2 × 60 ms in the
log-delay domain (≈ 0.5 s to settle within 1 %) is an `APPROXIMATION`.

### F4 Tone: head loss, BASS, TREBLE

No source gives the playback response, the tone-control frequencies or their
dB ranges (S2, S3: "cut/boost", flat at the centre; S4: "low/high frequency
range"). The registry fixes BASS and TREBLE at ±10 dB. Choices
(`APPROXIMATION`, owner-delegated): first-order shelves with half-gain
frequencies **150 Hz** and **4 kHz** (gentle slopes, as a two-knob
cut/boost tone control of that era; the RE-201's actual circuit is task R1);
a 2nd-order playback LF roll-off
at 40 Hz; a head-loss lowpass (2nd order, Butterworth) at **6.5 kHz at the
slowest speed (177 ms)**, scaled with √speed (10.4 kHz at 69 ms) and held
outside the hardware range (S4: tone follows speed). AGE and TAPE CONDITION
lower the corner.

### F5 Saturation: preamp and tape

S2/S3/S4/S11 agree that the colour comes from the **preamp** (clipping when
the input is driven) and the **tape's magnetic saturation** (S4). No source
gives a curve, THD figure or headroom. The model has two memoryless
saturators (preamp, tape with pre/de-emphasis) whose constants are calibrated
by THD targets (MODEL_SPEC 5). `APPROXIMATION`.

### F6 Self-oscillation

S2/S3/S4: INTENSITY fully up oscillates; the onset depends on program and
mode; it oscillates without input; single-head modes oscillate more simply.
The model's loop gain reaches 1.25 at INTENSITY 10 and the tape saturation
bounds the level (onset ≈ 8.25–8.5, MODEL_SPEC 6). The onset position is an
`APPROXIMATION`.

### F7 Spring reverb

S9/S10: a 3-spring tank. S2: noise-free model. S5/S6: the parametric
structure (F8). The tank's transition frequency, chirp spacing and decay are
not published for the RE-201's tank: values in MODEL_SPEC 8 are
`APPROXIMATION`s inside S5's structure (f_C ≈ 4.4 kHz, T60 ≈ 2.3 s). Two
decorrelated springs give the stereo image (the hardware is mono).

### F8 The S5/S6 structure as used

Group delay of the C_lf cascade: D(ω) = kM(1 − a²)/(1 + 2a cos(ωk) + a²),
rising from kM(1 − a)/(1 + a) at DC to kM(1 + a)/(1 − a) at ωk = π, i.e. at
f_C = fs/(2k). Low frequencies arrive first: the "drip" chirp. ECHO uses
M = 60/56, a = 0.62/0.58 (two springs), a 4th-order lowpass at 0.92 f_C
instead of S6's 8th-order crossover, and no delay-line modulation (S6:
optional). The test measures the arrival-time law against this formula.

### F9 Wow and flutter

S2/S3/S4 name wow/flutter and that worn tape has more (S3), but give no rates
or depths. S10: the RE-201's brushless direct-drive servo keeps them low. The
model: wow at 0.55 Hz (capstan/reel class), flutter at 6.5 Hz + 11.3 Hz
(pinch roller / bearing class), plus band-limited random drift; WOW 100 % =
0.6 % peak, FLUTTER 100 % = 0.2 % peak. All `APPROXIMATION`
(owner-delegated); the defaults (20 %) give 0.12 % / 0.04 %, in line with a
serviced machine.

### F10 Tape condition / age

S3: new / used / old tape: worn tape "loses fidelity and gains wow and
flutter". S4: new vs. "somewhat used" tape changes tone and wavering. The
master spec adds noise, irregular modulation and feedback degeneration for
MAGNETIC DUST. The offsets (MODEL_SPEC 7.4) are `APPROXIMATION`s.

### F11 MODE positions: 12 on the hardware, 8 in the registry

The hardware has 12 positions (S2, S9): 4 echo-only, 7 echo + reverb,
1 reverb-only; the reverb is switched by the selector. The registry (fixed)
has 8 choices: Reverb, 1, 2, 3, 1+2, 1+3, 2+3, 1+2+3, and a separate REVERB
VOLUME. **Decision**: MODE selects the heads; REVERB VOLUME adds the spring
in every mode (as the hardware's echo + reverb positions do); "Reverb" = no
head. The hardware's echo-only positions are REVERB VOLUME 0. The editor's
conflict table (UI_SPEC_CONFLICTS.md #1) records the same mapping.

### F12 Tap tempo

S4 (RE-202): the tapped time becomes head 1's time; maximum 1 s / 2 s. ECHO:
FREE: REPEAT RATE = the tapped interval (20–2000 ms). SYNC: the tapped
interval is a quarter note: MANUAL BPM = 60 / interval (20–300 BPM) and BPM
SOURCE switches to Manual. [TLEET_EXTENSION, behaviour after S4]

## Blocked sources (this build environment)

| Source | Why | What it might add |
|---|---|---|
| S1 Roland RE-201 owner's manual | image-only PDF, no text layer | MODE table (to confirm S2's), level specifications |
| RE-201 service notes / schematic | not found in a readable public copy | preamp topology and clipping level, tone-stack values, the record/playback EQ, the motor servo's time constant |
| S5 (JAES 2010) full text | behind the AES library | the original parameter table (read through S6 instead) |

## Open research tasks

| # | Task | Affects |
|---|---|---|
| R1 | Read S1 (OCR) and confirm the MODE table and the tone-control frequencies | MODE mapping, F4 |
| R2 | Measure a serviced RE-201: head delays at both REPEAT RATE extremes (confirm 1 : 1.90 : 2.75), the playback response at slow and fast speed, BASS/TREBLE curves | F1, F4 |
| R3 | Measure wow/flutter spectrum (DIN/IEC weighted) on a serviced unit and on worn tape | F9, F10 |
| R4 | Measure THD vs input level (MIC and INST inputs) and the self-oscillation onset per mode | F5, F6 |
| R5 | Measure the spring tank's impulse response (f_C, chirp period, T60) | F7 |
| R6 | Motor ramp time for a REPEAT RATE step | F3 |
