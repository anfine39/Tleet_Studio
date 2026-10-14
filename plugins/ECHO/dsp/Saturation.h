#pragma once

#include "Filters.h"
#include <algorithm>
#include <cmath>

// The two nonlinear stages of the record path (MODEL_SPEC 5). Both run at the
// record-stage rate (176.4/192 kHz: 4x, 2x or 1x) inside Oversampler2x.
//
//   preamp (the RE-201's transistor input/mixing stage, DRIVE):
//       z = x * D / Vc,  s = z / sqrt(1 + z^2)
//       out = (Vc / D) * (s + e2 * HP5(s^2))
//     small-signal gain exactly 1 (DRIVE changes the colour, not the level);
//     the even term (single-ended transistor stage) is high-passed at 5 Hz, no DC.
//   tape (record head + oxide, in the loop):
//       pre-emphasis high shelf +E dB at 3 kHz -> Lt * u / sqrt(1 + u^2), u = x / Lt
//       -> exact inverse de-emphasis shelf.
//     Odd, level-dependent; high frequencies (emphasised) saturate first: the
//     tape's HF compression. Small-signal gain exactly 1 at every frequency.
//     It bounds the loop: at INTENSITY above the oscillation point the
//     recirculating signal settles where the saturator's describing-function
//     gain cancels the excess loop gain (MODEL_SPEC 6).
namespace tleet::echo
{
    struct Voicing
    {
        double driveOffsetDb; // extra preamp drive (Burn)
        double preVc;         // preamp soft-limit level (linear, 1 = 0 dBFS)
        double preEven;       // e2: even-order share of the preamp
        double tapeLt;        // tape saturation level Lt (linear)
        double emphDb;        // record pre-emphasis E (dB)
    };

    namespace sat
    {
        inline constexpr int numCharacters = 3;
        inline constexpr double driveMaxDb = 24.0;   // DRIVE 0..10 = 0..+24 dB into the preamp
        inline constexpr double emphHz = 3000.0;     // pre/de-emphasis half-gain frequency
        inline constexpr double evenDcHz = 5.0;

        // [APPROXIMATION constants, TLEET_EXTENSION voicings; calibration in MODEL_SPEC 5.5]
        inline const Voicing& voicing (int index) noexcept
        {
            //                                           drive  Vc     e2      Lt     E
            static const Voicing v[numCharacters] = { { 0.0,   4.0,   0.002,  1.4,   3.0 },   // Clean
                                                      { 0.0,   1.0,   0.04,   0.7,   6.0 },   // Original
                                                      { 6.0,   0.6,   0.08,   0.45,  9.0 } }; // Burn
            return v[std::clamp (index, 0, numCharacters - 1)];
        }

        inline double driveGain (double drive0to10, double offsetDb) noexcept
        {
            return std::pow (10.0, (driveMaxDb * std::clamp (drive0to10, 0.0, 10.0) / 10.0 + offsetDb) / 20.0);
        }

        inline double softClip (double z) noexcept { return z / std::sqrt (1.0 + z * z); }
    }

    struct Preamp
    {
        void prepare (double fs) noexcept
        {
            const double g = std::tan (filt::pi * sat::evenDcHz / fs);
            evenG = g / (1.0 + g);
            reset();
        }
        void reset() noexcept { evenState = 0.0; }

        // inGain = D / Vc, outGain = Vc / D
        double process (double x, double inGain, double outGain, double e2) noexcept
        {
            const double s = sat::softClip (x * inGain);
            const double sq = s * s;
            const double v = (sq - evenState) * evenG; // HP5(s^2) = s^2 - LP(s^2)
            const double lp = v + evenState;
            evenState = lp + v;
            return outGain * (s + e2 * (sq - lp));
        }

        double evenG = 0.0, evenState = 0.0;
    };

    struct TapeSaturation
    {
        void reset() noexcept { emph.reset(); deemph.reset(); }

        void set (double emphDb, double fs) noexcept
        {
            const double g = std::pow (10.0, emphDb / 20.0);
            emph.set (sat::emphHz, fs, g, true);
            deemph.set (sat::emphHz, fs, 1.0 / g, true);
        }

        double process (double x, double lt) noexcept
        {
            const double e = emph.process (x);
            return deemph.process (lt * sat::softClip (e / lt));
        }

        filt::Shelf emph, deemph;
    };
}
