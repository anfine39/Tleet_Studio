# ASH — open measurement tasks

Values tagged [APPROXIMATION] in `MODEL_SPEC.md` should be refined against a
reference. Plan:

1. Render the TLEET test signal pack (sines at −30…0 dBFS, 0.3 s / 2 s / 6 s
   bursts, log sweep) through a reference LA-2A plugin in Studio Pro at fixed
   settings (Peak Reduction 30/50/70, Gain 50, Compress and Limit).
2. Measure from the renders: static curve, attack to 63 %, recovery to 50 % and
   90 % after each burst length, THD at 1 kHz vs level.
3. Fit: Peak Reduction law, fast/slow split, slow charge time, history time,
   Limit exponent, Character default.
4. Store the reference measurements (numbers only, not third-party audio) in
   `tests/golden/ASH/reference_measurements.json` and add tolerance tests.
