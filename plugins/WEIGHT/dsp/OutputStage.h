#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <algorithm>
#include "DetectorFilters.h"

// TONE: output-stage voicing (MODEL_SPEC.md section 6). A TLEET_EXTENSION:
// the API 2500's own OLD/NEW switch is the detector topology (DETECTOR on
// the skin); TONE never touches the detector, the timing or the gain law.
//
//   y = x + HP5(d(u)),  u = x / sqrt(1 + x^2/16),  d(u) = h2 u^2 - h3 u^3
//   then a low shelf (one pole, 60 Hz) and a high shelf (one pole, 10 kHz).
//
// Neutral is the identity (bit-exact pass-through once settled). Old leans
// on the 2nd harmonic with a slightly fuller low end and softer top
// (transformer-coupled discrete amp voicing); New leans on the 3rd with a
// slightly tighter low end and a little more air. All harmonics stay below
// 1 % THD at 0 dBFS.
namespace tleet::weight
{
    struct ToneLaw
    {
        float h2;         // 2nd-order term (0 dBFS: H2 = h2 / 2)
        float h3;         // 3rd-order term (0 dBFS: H3 = h3 / 4)
        float lowShelfDb; // gain below lowShelfHz
        float highShelfDb;// gain above highShelfHz
    };

    namespace tone
    {
        inline constexpr int numTones = 3;
        inline constexpr double lowShelfHz = 60.0;
        inline constexpr double highShelfHz = 10000.0;
        inline constexpr double dcBlockHz = 5.0;

        inline const ToneLaw& law (int index) noexcept
        {
            //                                    h2      h3      low dB  high dB
            static const ToneLaw laws[numTones] = { { 0.010f, 0.004f,  0.5f, -0.5f },   // Old
                                                    { 0.0f,   0.0f,    0.0f,  0.0f },   // Neutral: identity
                                                    { 0.0015f, 0.010f, -0.2f, 0.4f } }; // New
            return laws[std::clamp (index, 0, numTones - 1)];
        }

        inline double shelfGain (float db) noexcept { return std::pow (10.0, db / 20.0); }
    }

    // Small-signal magnitude (dB) of the TONE stage as implemented (the two
    // shelves; the nonlinearity adds no linear gain at low level).
    inline double toneResponseDb (double hz, int toneIndex, double sampleRate)
    {
        using namespace detector;
        const auto& l = tone::law (toneIndex);
        const auto zi = zInv (hz, sampleRate);
        const double gl = tone::shelfGain (l.lowShelfDb), gh = tone::shelfGain (l.highShelfDb);
        const auto lpL = onePoleLowpass (integratorGain (tone::lowShelfHz, sampleRate), zi);
        const auto lpH = onePoleLowpass (integratorGain (tone::highShelfHz, sampleRate), zi);
        const auto h = (1.0 + (gl - 1.0) * lpL) * (1.0 + (gh - 1.0) * (1.0 - lpH));
        return toDb (h);
    }

    // One channel of the output stage. Coefficients glide (per sample) toward
    // the selected law; once settled on Neutral the stage is skipped.
    class OutputStage
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            using namespace detector;
            const double gL = integratorGain (tone::lowShelfHz, sampleRate);
            const double gH = integratorGain (tone::highShelfHz, sampleRate);
            const double gD = integratorGain (tone::dcBlockHz, sampleRate);
            cL = (float) (gL / (1.0 + gL));
            cH = (float) (gH / (1.0 + gH));
            cD = (float) (gD / (1.0 + gD));
            glideCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
            reset();
        }

        void setTone (int index) noexcept { target = tone::law (index); }

        void reset() noexcept
        {
            cur = target;
            sL = sH = sD = 0.0f;
            first = true;
            advance();
        }

        bool isNeutral() const noexcept
        {
            return isZero (cur) && isZero (target);
        }

        // Call once per sample before process() (the glide is shared by the
        // channels: call it on one stage and copy, or on each; both glide the same).
        void advance() noexcept
        {
            glide (cur.h2, target.h2);
            glide (cur.h3, target.h3);
            const bool shelvesMoved = glide (cur.lowShelfDb, target.lowShelfDb) | glide (cur.highShelfDb, target.highShelfDb);
            if (shelvesMoved || first)
            {
                gl = (float) tone::shelfGain (cur.lowShelfDb) - 1.0f;
                gh = (float) tone::shelfGain (cur.highShelfDb) - 1.0f;
                first = false;
            }
        }

        float process (float x) noexcept
        {
            if (isNeutral())
            {
                sL = sH = sD = 0.0f;
                return x;
            }

            // harmonic generator, DC removed
            const float u = x / std::sqrt (1.0f + x * x * (1.0f / 16.0f));
            const float d = cur.h2 * u * u - cur.h3 * u * u * u;
            const float vD = (d - sD) * cD;
            const float lpD = vD + sD;
            sD = lpD + vD;
            float y = x + (d - lpD);

            // low shelf: y + (GL - 1) LP(y)
            const float vL = (y - sL) * cL;
            const float lpL = vL + sL;
            sL = lpL + vL;
            y += gl * lpL;

            // high shelf: y + (GH - 1) HP(y)
            const float vH = (y - sH) * cH;
            const float lpH = vH + sH;
            sH = lpH + vH;
            y += gh * (y - lpH);
            return y;
        }

    private:
        static bool isZero (const ToneLaw& l) noexcept
        {
            return detector::same (l.h2, 0.0) && detector::same (l.h3, 0.0)
                && detector::same (l.lowShelfDb, 0.0) && detector::same (l.highShelfDb, 0.0);
        }

        bool glide (float& v, float t) noexcept
        {
            if (detector::same (v, t))
                return false;
            const float d = t - v;
            v = std::abs (d) <= 1.0e-6f ? t : v + d * glideCoeff;
            return true;
        }

        ToneLaw target { 0, 0, 0, 0 }, cur { 0, 0, 0, 0 };
        float cL = 0, cH = 0, cD = 0, glideCoeff = 0.001f;
        float gl = 0, gh = 0;
        float sL = 0, sH = 0, sD = 0;
        bool first = true;
    };
}
