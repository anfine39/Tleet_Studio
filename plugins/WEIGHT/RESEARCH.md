# WEIGHT — research notes

Evidence behind `MODEL_SPEC.md`. Every claim the model relies on is listed
with its source and how strong that source is. Where sources disagree, the
conflict and the decision are written down here and are not repeated in the
model spec.

WEIGHT is informed by the **API 2500 stereo bus compressor** (rack unit, and
its 2500+ revision; the 500-series 527/527A share its detector ideas). It is
not a measured 1:1 clone of one unit: no unit was measured for this project
(see "Open measurement tasks" in MODEL_SPEC).

All sources were accessed on 2026-10-13.

## Source classes

| Class | Meaning | Used as |
|---|---|---|
| A — manufacturer | API manuals and product pages | `SOURCE_CONFIRMED` |
| A' — licensed | manuals of API-licensed software models (UA, Waves), written with API | `SOURCE_CONFIRMED` for control ranges, context for behaviour |
| B — engineering | peer-reviewed design literature and standards | `SCHEMATIC_OR_MEASURED` / basis of `APPROXIMATION`s |
| D — review / forum | reviews and editorial Q&A | context only, never the sole basis for a law |

No schematic or independent measurement of a 2500 was found in public. Every
timing constant, knee width and filter corner that the manufacturer does not
publish is therefore an `APPROXIMATION` (MODEL_SPEC section 10).

## Sources

| ID | Source | Class | What it establishes |
|---|---|---|---|
| S1 | API, *2500 Stereo Bus Compressor Operator's Manual* (rev. 18-06-18) — <https://www.apiaudio.com/docs/manuals/2500_user_18-06-18.pdf> | A | THRESHOLD continuously variable **+10 to −20 dBu**. ATTACK 7 positions: **30 µs, 100 µs, 300 µs, 1 ms, 3 ms, 10 ms, 30 ms**. RATIO **1.5, 2, 3, 4, 6, 10, ∞ :1**. RELEASE **50 ms, 100 ms, 200 ms, 500 ms, 1 s, 2 s, VARIABLE** (pot 50 ms..3 s, active only in the last position). KNEE HARD / MED / SOFT ("HARD … sharp transition", "MED … slight fade-in", "SOFT … gradual fade-in … similar to an over-easy type knee"). THRUST: a filter "in front of the RMS detector, with a slope of 10 dB per decade", the inverse of the pink-noise energy curve; NORM = no filter; MED = "slight attenuation of the low frequencies and a slight boost of the high frequencies" with a flat midrange; LOUD = "a gradual, linear filter, down 15 dB at 20 Hz and up 15 dB at 20 kHz". TYPE: NEW = **feed-forward** ("the RMS detector sends a signal to the VCA"), OLD = **feedback** ("the RMS detector gets the signal from the output of the VCA", "how the older API 525 … worked"); "The NEW mode is much harder and the OLD mode is very smooth". L/R LINK: **IND, 50, 60, 70, 80, 90, 100 %**; each channel keeps its own RMS detector; the link mixes the detector control voltages. SHAPE: HP / LP / BP filters **in the link path only** (no frequencies given). Make-up: auto (from THRESH and RATIO) or manual GAIN 0..+24 dB. Signal path "ALL DISCRETE, using the API 2510 and the API 2520", three VCAs per channel "to minimize noise and distortion". GR meters 0..20 dB; "can compress up to 30 dB". |
| S2 | API, *2500+ Stereo Bus Compressor Operator's Manual* (rev. 23-01-09) — <https://apiaudio.com/docs/manuals/2500+_user_23-01-09.pdf> | A | Same attack (.03 .. 30 ms), release (.05 .. 2 s + variable 50 ms..3 s) and ratio steps. THRESHOLD **+20 .. −20 dBu**; 0 VU = +4 dBu. THRUST LOUD: "10 dB/decade (−3 dB/8va)" filter on the signal feeding the RMS detector. TYPE NEW (F FWD): "The RMS detector receives the signal from before the VCA"; OLD (F BACK): "… from after the VCA". KNEE, THRUST and TYPE are grouped under a **"TONE"** heading on the 2500+ (this is where the WEIGHT skin's TONE label comes from; see F8). LINK: "When the 2500+ is linked, the control voltages are summed together" while each channel still detects its own level; 50..100 % steps + IND. The 2500+ SHAPE is **FAST (peak-acting) / SLOW (RMS-acting) / BOTH** on the link control voltage (different from S1). Adds BLEND / MIX (crossfade or parallel). GR meters 0..20 dB. |
| S3 | API, *2500 Stereo Compressor* product page — <https://apiaudio.com/product/2500-stereo-compressor/> | A | "Variable threshold between −20 dBu and +10 dBu". "Patented THRUST® selectable high-pass filter before RMS detector". "Old 'feed back' or new 'feed forward' compression"; the new style is "fed directly from the unit input". "Soft, medium or hard knee". "Output clips at +28 dBu". "API 2520 Op Amp". |
| S4 | API, *527A Compressor/Limiter Operator's Manual* (rev. 20-07-13) — <https://apiaudio.com/docs/manuals/527A_user_20-07-13.pdf> | A | Same OLD/NEW definitions; feedback gives "a smoother, softer, more transparent sound", feed-forward "more aggressive compression and a harder, more affected sound". THRUST "down 15 dB at 20 Hz and up 15 dB at 20 kHz", "10 dB per decade (−3 dB/8va)". Specs (527A): frequency response ±0.5 dB 20 Hz..40 kHz, S/N 110 dB, max output +30 dBu, threshold +10..−20 dBu. |
| S5 | Universal Audio, *API 2500 Bus Compressor Manual* (UA with API; two units analysed from API schematics) — <https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual> | A' | Threshold +10..−20 dB; the same attack, ratio and release steps. NEW: "Feed-forward sidechain routing … harder and more transparent"; OLD: modelled on the 525, "smoother with more character". Link IND/50..100 %; SHAPE = HP and LP on the link signal (both = band-pass), no frequencies. The **IN** switch defeats only the compression: "the amp and transformer coloration remains". UA note: in OLD "the gain stage sits inside the sidechain", so make-up can change the amount of compression. |
| S6 | Waves, *API 2500 Compressor plugin manual* (licensed by API) — <https://manuals.plus/waves/api-2500-compressor-plugin-manual> | A' | Threshold **+10..−20 dBu, shown as −12..−42 dBFS** (i.e. 0 dBu ≙ −22 dBFS in that product). L/R link **default 100 %**, each channel with its own RMS detector. "Sidechain works only in Feed Forward; selecting an external sidechain in Feed Back switches the unit to Feed Forward." THRUST "inserts a High Pass Filter at the RMS detector input"; LOUD −15 dB at 20 Hz / +15 dB at 20 kHz. |
| S7 | H. Robjohns, *Q. What does an API 2500 compressor's 'Thrust' control do?*, Sound On Sound, April 2015 — <https://www.soundonsound.com/sound-advice/q-what-does-api-2500-compressors-thrust-control-do> | D | LOUD: a linear slope of **+10 dB per decade (+3 dB/octave)** from below 20 Hz to above 20 kHz, the inverse of pink noise. MED: the midrange, **roughly 200 Hz to 3 kHz, is flat**, with the same +3 dB/oct slopes outside. NORM: flat. |
| S8 | G. Walker, *Review: API 2500 Bus Compressor*, AudioTechnology, 30 July 2006 — <https://www.audiotechnology.com/reviews/api-2500-bus-compressor> | D | "the side-chain process is only available in 'new' compression mode". Link: stepped "dynamic crosstalk between left and right channels from 100 % to 50 %", fully counter-clockwise = separate. The IN/OUT switch keeps the signal "passing through all the unit's internals (2520s, transformers, VCAs etc.)". OLD → NEW gives "a more up-front and conspicuously squashed form of compression". |
| S9 | D. Giannoulis, M. Massberg, J. D. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, J. Audio Eng. Soc. 60(6), 2012 | B | Feed-forward vs feedback topologies: with the same gain computer, a feedback compressor cannot reach an infinite ratio and has a softer static curve; branching (diode-steered) attack/release smoothing in the log domain; the quadratic soft knee `(x + W/2)² / 2W`. |
| S10 | EBU R 68, *Alignment level in digital audio production equipment* | B | Alignment: **0 dBu ≙ −18 dBFS** (sine). Used for the threshold calibration (F6). |
| S11 | AES17, *Measurement of digital audio equipment* | B | dBFS for RMS levels: a full-scale **sine** reads 0 dBFS (RMS + 3.01 dB). The detector level convention (F6). |
| S12 | *Blackmer RMS detector* (THAT/dbx log-domain true-RMS detector) — <https://en.wikipedia.org/wiki/Blackmer_RMS_detector> | D | A true-RMS detector integrates the squared signal with one time constant and outputs its logarithm (dB). Context for the RMS block (F3); no 2500-specific value. |

## Findings and decisions

### F1 Topology: NEW = feed-forward, OLD = feedback (one gain computer)

All manufacturer sources agree (S1, S2, S3, S4): NEW taps the detector before
the VCA, OLD after it, "much harder" vs "very smooth". The ratio switch is
one circuit for both positions (no source describes a separate OLD ratio
law). With the same gain computer, the feedback loop has a lower effective
ratio, 1 + S with S = 1 − 1/ratio (S9): 1.33 at 1.5:1 up to 2:1 at ∞. That
matches every description of OLD (smooth, "you can hardly hear the
compression" with SOFT/LOUD/OLD, S1) and makes OLD vs NEW a measurable,
documented difference (ratio, knee and timing; MODEL_SPEC 2.4).

Decision: one gain computer, the detector input switched (and crossfaded).
[SOURCE_CONFIRMED topology; APPROXIMATION that the ratio circuit is shared]

UA's remark that make-up sits inside the OLD loop (S5) is not followed: in
WEIGHT makeup is after the VCA tap in both positions, so MAKEUP never changes
the GR (a deliberate choice; listed as open task 5).

### F2 Attack and release steps

S1, S2 and S5 agree on 30 µs .. 30 ms (7 steps) and 50 ms .. 2 s (6 steps)
plus VARIABLE. The skin has no control for VARIABLE, so WEIGHT has the six
fixed releases only. [SOURCE_CONFIRMED values; the meaning as exponential
time constants of the ballistics is an APPROXIMATION]

### F3 Detector: true RMS

Every API source says "RMS detector" (S1–S4). No time constant is published.
WEIGHT uses a true-RMS detector (one-pole on the mean square, S12) with a
**5 ms** averaging time, followed by the attack/release ballistics in dB.
Consequence, kept on purpose: the RMS integration limits how fast the GR can
rise for moderate overshoots (63 % rise ≈ 1.7 ms at 15 dB over with 30 µs
attack), while large transients still clamp within a fraction of a
millisecond (the dB level of a mean square rises almost instantly when the
overshoot is large). This is the "punch" usually attributed to RMS-detecting
VCA bus compressors. [APPROXIMATION: open task 1]

### F4 Ratio and knee

Ratios 1.5/2/3/4/6/10/∞ (S1, S2, S5). The KNEE switch (HARD/MED/SOFT) has no
control on the WEIGHT skin, so one knee is fixed: the **MED** position,
modelled as a 6 dB-wide quadratic soft knee (S9 form). HARD would be 0 dB and
SOFT an over-easy-like wide knee; none has published widths. A knee control
would need the owner's approval (UI change). [SOURCE_CONFIRMED positions;
APPROXIMATION width]

### F5 THRUST

The manufacturer gives the LOUD curve (S1, S2, S4, S6): 10 dB/decade,
−15 dB at 20 Hz, +15 dB at 20 kHz (0 dB at 632 Hz, the geometric centre).
MED has no published numbers in API documents; S7 (SOS technical editor)
describes it as flat between ~200 Hz and ~3 kHz with the same slope outside.

Conflicts:
- S1's text says ±15 dB while its front-panel diagram labels ±20 dB. The text
  (repeated in S2, S4 and S6) wins.
- S3/S6 call THRUST a "high-pass filter", but every curve description is a
  tilt that also boosts the highs. The tilt wins (it is what "10 dB/decade"
  and "+15 dB at 20 kHz" mean).

Mapping to the skin: THRUST CLEAN / NEUTRAL / HARD = API NORM / MED / LOUD.
The TLEET labels come from the skin; the mapping keeps the order of
increasing low-frequency desensitisation. [TLEET label remapping]

### F6 Threshold calibration

The knob reads the hardware's dBu scale: −20..+10 with 0 at the top (S1, S3,
S5, S6). Digital alignment: EBU R68, 0 dBu = −18 dBFS (S10), so knob 0 dB =
−18 dBFS RMS (AES17 sine-referenced, S11), range −38..−8 dBFS. Waves' licensed
model aligns 4 dB lower (0 dBu = −22 dBFS, S6); R68 was chosen because it
keeps the top of the range usable on hot modern mix buses. The 2500+ extends
the threshold to +20 dBu (S2); the skin keeps the 2500's range.

### F7 Stereo link

The link mixes the two channels' control voltages, each channel keeping its
own RMS detector (S1, S2, S6); IND = independent, 100 % = one control for
both. "Summed together" (S2) is modelled as a **normalised** sum so that two
identical channels compress exactly as one channel at any link setting:
`cv_L = c_L + (k/2)(c_R − c_L)`, `cv_R = c_R + (k/2)(c_L − c_R)`, k = link.
The hardware steps (IND, 50..100 %) become a continuous 0..100 % knob (the
skin's STEREO LINK). Default 100 % (Waves' default, S6; a bus compressor keeps
the image). The link SHAPE filters (S1/S5: HP/LP/BP; S2: FAST/SLOW/BOTH) have
no control on the skin and are not modelled. [SOURCE_CONFIRMED structure;
APPROXIMATION normalisation; TLEET_EXTENSION continuous range]

### F8 TONE on the skin is not the API OLD/NEW switch

On the 2500+ the heading "TONE" groups KNEE, THRUST and TYPE (S2). The WEIGHT
skin has a separate DETECTOR FEED FORWARD / FEED BACK selector, which is the
API TYPE switch (NEW/OLD). Following the master spec's ambiguity rule, the
skin's TONE OLD / NEUTRAL / NEW is a **TLEET extension** on the output stage
only: it never changes the detector, the timing or the gain law (tested).
Its voicing is inspired by the documented discrete 2520 / transformer path
(S1, S5, S8), not measured.

### F9 External side-chain

The hardware has side-chain inputs that work only in NEW (S8), and Waves
switches to feed-forward when an external key is used in Feed Back (S6). In
WEIGHT the key replaces the detector feed, so the detector is feed-forward
with a key regardless of DETECTOR. SC HPF and THRUST still filter the key.

### F10 Output stage and headroom

The 2500 output clips at +28 dBu (S3) = +10 dBFS at the R68 alignment: the
output stage cannot clip inside a float host's 0 dBFS range, so WEIGHT has
no clipper; the TONE extension supplies the (subtle) colour. The 527A's
±0.5 dB 20 Hz..40 kHz response (S4) bounds the TONE shelves (±0.5 dB).
