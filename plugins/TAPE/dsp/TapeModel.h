#pragma once

#include <algorithm>
#include <cmath>

// Every constant of the tape model in one place, with its provenance
// (MODEL_SPEC.md gives the derivations; RESEARCH.md the sources R1..R14).
//   [HW]  HARDWARE_DATA   published Studer A800 data
//   [LIT] LITERATURE      textbook / paper / standard
//   [APX] APPROXIMATION   a value chosen within the literature's range
//   [EXT] TLEET_EXTENSION a TLEET design decision
namespace tleet::tape
{
    // Exact comparison for the smoothing end-points and the solver's "no change" test
    // (bit-exact settling is intended; written this way to keep -Wfloat-equal quiet).
    // NaN compares as equal, which every caller treats as "nothing to do".
    template <typename T>
    constexpr bool exactly (T a, T b) noexcept { return ! (a < b) && ! (b < a); }
}

namespace tleet::tape::model
{
    inline constexpr double pi = 3.14159265358979323846;
    inline constexpr double ipsToMetres = 0.0254;

    // ------------------------------------------------------------ levels
    // 0 VU = a sine at -18 dBFS (suite convention, ECHO / VEIL). [EXT]
    inline constexpr double zeroVuDbfs = -18.0;
    inline const double zeroVuPeak = std::pow (10.0, zeroVuDbfs / 20.0);   // 0.12589
    inline const double zeroVuRms = zeroVuPeak / std::sqrt (2.0);
    // CALIBRATION 0 = 0 VU records the reference fluxivity 185 nWb/m; +3 / +6 = 260 / 370 nWb/m. [LIT, R5]
    inline constexpr double referenceFluxNwbPerM = 185.0;

    // Record amplifier output stage: soft limit 20 dB above 0 VU (+24 dBm max with +4 dBm
    // operating level, A800 data R1). y = x / (1 + |x/Vc|^8)^(1/8). [HW level, APX shape]
    inline const double recordAmpClip = zeroVuPeak * 10.0;

    // ------------------------------------------------------------ speeds
    inline constexpr double speedIps[3] = { 7.5, 15.0, 30.0 };
    inline double speedMetres (double ips) noexcept { return ips * ipsToMetres; }

    // Equalisation time constant, IEC / CCIR (the Studer default): 70 us at 7.5 ips,
    // 35 us at 15 ips, AES 17.5 us at 30 ips -- exactly 35 us * (15 ips / v). [HW, R1, R6]
    inline double eqTau (double ips) noexcept { return 35.0e-6 * 15.0 / ips; }
    // Record pre-emphasis: (1 + s tau) / (1 + s tau / G), G = 4 (+12 dB ceiling). It boosts
    // the treble into the magnetic layer (what the record EQ does to overcome the record
    // losses) and is undone exactly after it, so treble saturates first. [APX]
    inline constexpr double preEmphasisCeiling = 4.0;

    // ------------------------------------------------------------ head losses (wavelength laws, R3 / R4)
    // gap loss sinc(pi g f / v) with the effective repro gap g. [LIT law, APX value]
    inline constexpr double reproGapMetres = 4.0e-6;
    // Spacing loss exp(-2 pi d f / v) for the spacing in excess of the aligned machine
    // (54.6 d / lambda dB, Wallace). AGE 100 % adds 2.0 um of wear / debris spacing. [LIT law, APX value]
    inline constexpr double ageSpacingMetres = 2.0e-6;
    inline constexpr double ageRightSpacingExtra = 0.25; // right head +25 % of that: L/R mismatch [EXT]
    // Record-zone (bias) loss: overbias moves the recording zone into the coating: an
    // exponential loss with d_rec = D (smax(I, 0.7) - smax(1, 0.7)), I = bias current / optimum.
    // D = 6.3 um per unit current reproduces the published "overbias" of a mastering tape:
    // 10 kHz falls 1.25 / 2.5 / 5 dB at 30 / 15 / 7.5 ips for +30 % current, measured through
    // the whole machine at -20 VU (the zone alone would give 54.6 D 0.3 / lambda = 2.7 dB at
    // 15 ips; the rising reversibility gives 0.2 dB of it back). [HW R2 data, APX law]
    inline constexpr double biasZoneMetresPerUnit = 6.3e-6;
    inline constexpr double biasPeakCurrent = 0.7, biasPeakWidth = 0.05;

    // Head bump (contour effect of the repro head, R3): a resonance at a fixed
    // wavelength, so its frequency follows the speed: f_b = v / lambda_b, plus a dip
    // at 0.55 f_b and the head's long-wavelength roll-off (12 dB/oct below 0.27 f_b).
    //   7.5 / 15 / 30 ips -> 27 / 54 / 109 Hz bump.  [LIT mechanism, APX values]
    inline constexpr double bumpWavelengthMetres = 7.0e-3;
    inline constexpr double bumpQ = 1.0, dipRatio = 0.55, dipQ = 1.2, dipShare = 0.5;
    inline constexpr double rolloffRatio = 0.27, rolloffQ = 0.7;
    // HEAD BUMP 100 % = +5 dB at 15 ips; x0.8 at 7.5 ips, x1.25 at 30 ips (the contour
    // ripples are larger where the head is short against the wavelength). [APX]
    inline constexpr double bumpMaxDb = 5.0;
    inline double bumpSpeedScale (double ips) noexcept { return std::pow (ips / 15.0, 0.32); }

    // ------------------------------------------------------------ repro EQ trims (A800 repro LF / HF adjust)
    // LF / HF -100..+100 = -6..+6 dB (0.06 dB per step). LF: low shelf at 100 Hz;
    // HF: high shelf at the EQ turnover 1 / (2 pi tau) = 2.27 / 4.55 / 9.1 kHz. [EXT range, LIT corner]
    inline constexpr double trimDbPerStep = 0.06;
    inline constexpr double lfTrimHz = 100.0;

    // ------------------------------------------------------------ tape types (Jiles-Atherton, normalised)
    // m = M / Ms, h = H / a:  k' = k / a, alpha' = alpha Ms / a, c = reversibility.
    // Type B is Chowdhury's published parameter set (R8): k' = 27/22, alpha' = 1.6e-3 * 3.5e5 / 2.2e4.
    // MOL = the 1 kHz level (dB re 185 nWb/m) at which H3 = 3 % at optimum bias, 15 ips:
    //   A +9 (a classic "+3" era tape), B +12 (456 class), C +15 (high-output "+9" class). [LIT R2 / R9 ranges]
    // hScale = field per unit flux: found by bisection with this solver at 192 kHz, then trimmed
    // so that the MOL lands there measured through the whole machine (the repro de-emphasis
    // lowers H3 by 1.3 dB at 15 ips) (MODEL_SPEC 4.4; verified by "TAPE: THD ...").
    struct TypeSpec
    {
        double kPrime, alphaPrime, cOpt, molDb, hScale;
        double snrOffsetDb;   // hiss re type B (classic tape is noisier)
        const char* label;
    };
    inline const TypeSpec& typeSpec (int t) noexcept
    {
        static const TypeSpec types[3] = {
            { 0.90,  0.02000, 0.82,  9.0, 0.52791, -2.0, "CLASSIC" },
            { 1.227, 0.02545, 0.88, 12.0, 0.39546,  0.0, "MODERN" },
            { 1.50,  0.03000, 0.91, 15.0, 0.28170, +1.5, "HOT" } };
        return types[std::clamp (t, 0, 2)];
    }

    // BIAS: current I = 1 + BIAS / 100 (0 .. 2 x optimum). AC bias makes the recorded
    // magnetisation follow the anhysteretic curve (R3, R4): in the J-A model that is the
    // reversible share c. c(I) = cMax - (cMax - cMin) exp(-kappa I), kappa set so that c(1) = cOpt.
    inline constexpr double cMin = 0.15, cMax = 0.95;
    inline double biasCurrent (double biasPercent) noexcept { return std::clamp (1.0 + biasPercent / 100.0, 0.0, 2.0); }
    inline double reversibility (double current, double cOpt) noexcept
    {
        const double kappa = -std::log ((cMax - cOpt) / (cMax - cMin));
        return cMax - (cMax - cMin) * std::exp (-kappa * std::max (0.0, current));
    }
    inline double softMax (double x, double floor, double w) noexcept
    {
        const double a = x / w, b = floor / w, m = std::max (a, b);
        return w * (m + std::log (std::exp (a - m) + std::exp (b - m)));
    }
    inline double biasZoneMetres (double current) noexcept
    {
        return biasZoneMetresPerUnit * (softMax (current, biasPeakCurrent, biasPeakWidth) - softMax (1.0, biasPeakCurrent, biasPeakWidth));
    }

    // SATURATION: drive into the magnetic layer, 0.25 dB per %, 0 dB at 42 % (the
    // type's nominal alignment), with the same make-up after the tape: it moves the
    // operating point along the curve without changing the small-signal level. [EXT]
    inline constexpr double saturationDbPerPercent = 0.25, saturationNominal = 42.0;
    inline double saturationDb (double percent) noexcept { return saturationDbPerPercent * (percent - saturationNominal); }

    // AGE: head magnetisation (a DC field at the record head) = the even-order source.
    // AGE 100 % = 0.06 a (H2 about -47 dBc at +6 VU). [LIT mechanism R4, APX value]
    inline constexpr double ageHeadFieldMax = 0.06;

    // ------------------------------------------------------------ noise (NOISE on)
    // A800 record/repro S/N, unweighted: 66 dB at 7.5 and 15 ips, 70 dB at 30 ips (R1),
    // re 510 nWb/m (R10): re 185 nWb/m (= 0 VU at CAL 0) that is 57.2 / 57.2 / 61.2 dB.
    // NOISE AMOUNT 12 % = that machine; the hiss amplitude goes with (amount / 12)^1.5
    // (+9 dB per doubling). AGE 100 % adds 6 dB. Modulation noise ratio -68 dB (R2).
    inline double snrDb (double ips) noexcept
    {
        const double ref510 = 20.0 * std::log10 (510.0 / 185.0); // 8.81 dB
        return (ips > 20.0 ? 70.0 : 66.0) - ref510;
    }
    inline constexpr double noiseAmountReal = 12.0, noiseAmountExponent = 1.5;
    inline constexpr double ageNoiseDb = 6.0, modulationNoiseDb = -68.0;
    inline constexpr double hissShelf = 1.6;   // +4 dB above the EQ turnover (repro EQ on the tape's noise) [APX]
    inline double noiseAmountGain (double percent) noexcept
    {
        return percent <= 0.0 ? 0.0 : std::pow (percent / noiseAmountReal, noiseAmountExponent);
    }

    // ------------------------------------------------------------ transport (wow and flutter)
    // Rates follow the speed (one revolution of each rotating part):
    //   reels   f = v / (2 pi r): supply r = 110 mm, take-up r = 75 mm (10.5" reel, mid pack)
    //   capstan f = v / (pi D):   D = 28 mm;   guide roller D = 15 mm.            [LIT mechanism, APX sizes]
    inline constexpr double supplyRadius = 0.110, takeupRadius = 0.075, capstanDiameter = 0.028, rollerDiameter = 0.015;
    // Peak speed deviation at 100 % (fractions of nominal speed). [EXT ranges; defaults meet the A800 spec]
    inline constexpr double wowSupplyMax = 0.0025, wowTakeupMax = 0.0010, wowDriftRmsMax = 0.0005;
    inline constexpr double flutterCapstanMax = 0.0030, flutterRollerMax = 0.0010, flutterRandomRmsMax = 0.0006;
    // AGE 100 %: worn bearings / pinch roller.
    inline constexpr double ageWowRms = 0.0004, ageFlutterRms = 0.0008, ageCapstanPeak = 0.0004;
    // A800 weighted W&F: 0.06 / 0.04 / 0.04 % at 7.5 / 15 / 30 ips (R1): flutter x1.5 at 7.5 ips.
    inline double flutterSpeedScale (double ips) noexcept { return ips < 10.0 ? 1.5 : 1.0; }
    // Centre delay of the transport line: the deviation never exceeds +-2.3 ms.
    inline constexpr double transportCentreSeconds = 0.0025, transportMaxDeviation = 0.0023;
    // AGE 100 %: azimuth error: right channel 20 us later than left (+-10 us). [EXT]
    inline constexpr double azimuthSecondsMax = 10.0e-6;

    // ------------------------------------------------------------ AGE: level mismatch and wear variation
    inline constexpr double ageGainMismatchDb = 0.25;   // L +, R - at 100 %
    // Wear variation: head-to-tape contact fluctuates (0.5..8 Hz): HF band (above 3 kHz)
    // level varies by up to 0.12 RMS, the whole signal by 0.015 RMS at AGE 100 %. [APX]
    inline constexpr double wearSplitHz = 3000.0, wearHfRms = 0.12, wearBroadRms = 0.015;

    // ------------------------------------------------------------ crosstalk (fringing between tracks)
    // |bleed| at 1 kHz, 15 ips = 0.1 (x / 100)^1.5 (8 % -> -53 dB, 100 % -> -20 dB), first-order
    // low-pass at 250 Hz at 15 ips: fringing is a long-wavelength effect, so the corner is
    // proportional to the speed (125 / 250 / 500 Hz) and the plateau below it is the same at
    // every speed (8 % -> -40.6 dB). A800 spec: crosstalk >= 40 dB at 100 Hz - 12 kHz (R1). [HW bound, APX shape]
    inline constexpr double crosstalkMax1k = 0.1, crosstalkExponent = 1.5, crosstalkCornerHz15 = 250.0;
    inline double crosstalkGain1k (double percent) noexcept
    {
        return percent <= 0.0 ? 0.0 : crosstalkMax1k * std::pow (std::clamp (percent, 0.0, 100.0) / 100.0, crosstalkExponent);
    }
}
