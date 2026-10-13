#pragma once

#include "Filters.h"
#include <algorithm>
#include <cmath>

// The tube colour stages of VEIL (MODEL_SPEC.md section 6). They run inside
// the 2x oversampled wet path. Every constant below is an APPROXIMATION
// calibrated against the published Manley figures (RESEARCH.md F6) and the
// THD tables measured by tests/VeilTests.cpp; the controls are TLEET_EXTENSIONs.
//
//   input tube stage (TUBE DRIVE, HARMONICS, CHARACTER)
//       z = a D / Vc,  u = z / sqrt(1 + z^2)               (soft grid/plate limit, odd)
//       y = (u + h (b2 HP5(u^2) - b3 u^3)) Vc / D           (low-order harmonic generator)
//     D = 10^(24 dB x drive / 20): TUBE DRIVE is the level into the stage and
//     the exact inverse 1/D after it is the loudness compensation (small-signal
//     gain exactly 1 at every drive). h = HARMONICS: the strength of the
//     2nd/3rd-order terms; it never moves the level or the limit.
//   gain element (vari-mu tube, distortion grows with the bias = the GR)
//       b = g (a + rho h E Ve (e2 HP5(q^2) - e3 q^3)),  q = a / Ve,  rho = GR / (GR + 10 dB)
//     balanced (push-pull) stage: mostly odd order, a little even from tube mismatch.
//   output stage (SATURATION, CHARACTER)
//       y = m / (1 + |m / C|^p)^(1/p),  C = 10^((24 - 24 s) / 20)  (+24 dBFS .. 0 dBFS)
//     SATURATION s is the depth (how far the ceiling comes down); CHARACTER
//     sets the knee softness p. Symmetric: odd harmonics only. s = 0 bypasses
//     the stage (linear).
//   LF weight (output transformer, CHARACTER): first-order low shelf at 60 Hz.
namespace tleet::veil
{
    struct Voicing
    {
        double b2;        // input stage 2nd-order term at HARMONICS 100 % (bias asymmetry)
        double b3;        // input stage 3rd-order term at HARMONICS 100 %
        double headroom;  // input stage limit Vc multiplier
        double element;   // gain-element distortion multiplier
        double softness;  // output stage knee exponent p (lower = softer, more low-level harmonics)
        double weightDb;  // LF weight shelf (dB below 60 Hz)
    };

    namespace tube
    {
        inline constexpr int numCharacters = 3;
        inline constexpr double driveMaxDb = 24.0;
        inline constexpr double inputLimit = 4.0;     // Vc at Veil: +12 dBFS
        inline constexpr double elementScale = 4.0;   // Ve
        inline constexpr double elementE2 = 0.05, elementE3 = 1.0;
        inline constexpr double elementKneeDb = 10.0; // rho = GR / (GR + 10)
        inline constexpr double satCeilingMaxDb = 24.0, satCeilingRangeDb = 24.0;
        inline constexpr double dcBlockHz = 5.0;

        inline const Voicing& voicing (int index) noexcept
        {
            //                                             b2     b3     headroom  element  p     weight
            static const Voicing v[numCharacters] = { { 0.020, 0.010, 1.41254,  0.25,   3.0,  0.0 },   // Clean
                                                      { 0.120, 0.040, 1.0,      1.0,    2.0,  0.5 },   // Veil
                                                      { 0.300, 0.100, 0.79433,  2.0,    1.6,  1.2 } }; // Thick
            return v[std::clamp (index, 0, numCharacters - 1)];
        }

        inline double driveGain (double drive01) noexcept { return std::pow (10.0, driveMaxDb * std::clamp (drive01, 0.0, 1.0) / 20.0); }
        inline double saturationCeiling (double sat01) noexcept
        {
            return std::pow (10.0, (satCeilingMaxDb - satCeilingRangeDb * std::clamp (sat01, 0.0, 1.0)) / 20.0);
        }
        inline double elementWeight (double grDb) noexcept { return grDb > 0.0 ? grDb / (grDb + elementKneeDb) : 0.0; }
    }

    // Control-rate coefficients shared by the channels.
    struct TubeCoeffs
    {
        double inScale = 1.0 / tube::inputLimit;  // D / Vc
        double outScale = tube::inputLimit;       // Vc / D
        double h2 = 0.0, h3 = 0.0;                // h b2, h b3
        double hElement = 0.0;                    // h E
        double satDepth = 0.0;                    // s (0 = stage bypassed)
        double invCeiling = 1.0 / 16.0;
        double p = 2.0, invP = 0.5;
        double dcCoeff = 0.0;
    };

    // Per-channel state of the three stages.
    struct TubeChannel
    {
        void reset() noexcept
        {
            inEven.reset();
            elEven.reset();
        }

        double inputStage (double a, const TubeCoeffs& c) noexcept
        {
            const double z = a * c.inScale;
            const double u = z / std::sqrt (1.0 + z * z);
            const double even = inEven.highpass (u * u, c.dcCoeff);
            return (u + c.h2 * even - c.h3 * u * u * u) * c.outScale;
        }

        double element (double a, double g, double grDb, const TubeCoeffs& c) noexcept
        {
            const double q = a * (1.0 / tube::elementScale);
            const double even = elEven.highpass (q * q, c.dcCoeff);
            const double w = tube::elementWeight (grDb) * c.hElement;
            return g * (a + w * tube::elementScale * (tube::elementE2 * even - tube::elementE3 * q * q * q));
        }

        static double outputStage (double m, const TubeCoeffs& c) noexcept
        {
            if (c.satDepth <= 0.0)
                return m;
            const double t = std::abs (m) * c.invCeiling;
            return m / std::pow (1.0 + std::pow (t, c.p), c.invP);
        }

        OnePole inEven, elEven;
    };
}
