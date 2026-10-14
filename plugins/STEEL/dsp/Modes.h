#pragma once

#include <cmath>
#include <algorithm>

// The four MODEs and the soft clipper (MODEL_SPEC 4.4, 4.6). Every number
// here is a TLEET_EXTENSION design value; the tests measure what each one
// does (attack onset, crest factor, release, distortion, clip share).
namespace tleet::steel
{
    enum class Mode { clean = 0, punch, dense, steel };
    inline constexpr int numModes = 4;

    struct ModeSpec
    {
        // Lookahead gain computer
        float attackFraction;  // share of LOOKAHEAD over which the gain ramps down (the rest is pure delay)
        int boxes;             // attack-ramp shape: 1 = linear, 2 = quadratic S, 3 = cubic S
        float kneeDb;          // soft knee width at the ceiling (0 = hard; GR starts kneeDb/2 below it)
        // AUTO release (two branches, MODEL_SPEC 4.3)
        float autoFastMs;      // fast branch: recovery after isolated peaks
        float autoSlowAttackMs;// slow branch: how long limiting must last before it is held
        float autoSlowReleaseMs;
        float autoSlowDepth;   // share of the fast branch's GR (dB) that the slow branch holds
        // Soft clip
        float clipScale;       // AMOUNT multiplier: how much of the peak load the clipper takes
    };

    inline const ModeSpec& modeSpec (int mode) noexcept
    {
        static const ModeSpec specs[numModes] = {
            // CLEAN: transparency first. Full-length smooth S attack, no knee, slow
            // program-dependent recovery that holds 90 % of sustained GR, conservative clip.
            { 1.00f, 3, 0.0f, 120.0f, 300.0f, 1500.0f, 0.90f, 0.5f },
            // PUNCH: transients first. The gain ramps only over the last 30 % of the lookahead
            // (later, steeper dip), fast recovery, the slow branch holds only half.
            { 0.30f, 1, 0.0f, 30.0f, 600.0f, 400.0f, 0.50f, 0.8f },
            // DENSE: loudness first. 3 dB knee (continuous control below the ceiling), fast
            // recovery, the clipper takes 1.5x the load when SOFT CLIP is on.
            { 1.00f, 2, 3.0f, 50.0f, 200.0f, 600.0f, 0.60f, 1.5f },
            // STEEL: the balanced signature. 70 % attack window, 1 dB knee, medium recovery.
            { 0.70f, 2, 1.0f, 70.0f, 300.0f, 900.0f, 0.75f, 1.0f },
        };
        return specs[std::clamp (mode, 0, numModes - 1)];
    }

    // Soft clipper: identity below the knee start K, a quadratic knee (C1)
    // that reaches the saturation level S with zero slope at input K + 2 (S - K),
    // flat at S above. Odd symmetric (no DC, no even harmonics).
    //   AMOUNT a in [0, 1] (times the mode's clipScale, clamped) sets
    //     S = ceiling + 3 (1 - a) dB          (0 %: 3 dB above the ceiling, 100 %: at it)
    //     knee width = 1 + 5 a dB, K = S - width
    struct SoftClipCurve
    {
        float knee = 1.0f, sat = 1.0f, invTwoW = 1.0f;

        void set (float ceilingLin, float amount) noexcept
        {
            const float a = std::clamp (amount, 0.0f, 1.0f);
            sat = ceilingLin * std::pow (10.0f, 3.0f * (1.0f - a) / 20.0f);
            knee = sat * std::pow (10.0f, -(1.0f + 5.0f * a) / 20.0f);
            invTwoW = 1.0f / (4.0f * (sat - knee)); // 1 / (2 w), w = 2 (S - K)
        }

        float process (float x) const noexcept
        {
            const float ax = std::abs (x);
            if (ax <= knee)
                return x;
            const float u = ax - knee;
            const float w = 2.0f * (sat - knee);   // input width of the knee
            const float y = u < w ? knee + u - u * u * invTwoW : sat;
            return x < 0.0f ? -y : y;
        }
    };
}
