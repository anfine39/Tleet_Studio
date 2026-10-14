#pragma once

#include <cstdint>
#include <cmath>

// Slow bounded random drift for Humanize (MODEL_SPEC.md section 7).
//
// Each drift is a sample-and-hold random target, uniform in [-1, 1], held for
// a random time, then smoothed by two cascaded one-pole low-passes. The output
// is a convex combination of past targets, so it can never leave [-1, 1]
// (bounded), it is C1-continuous (no steps, no zipper), and its spectrum
// falls at 40 dB/decade above the smoothing corner (well under 1 Hz), so
// there is no fast jitter. The hold times are random, so the drift has no
// period: nothing cycles like an LFO chorus. Deterministic seeds make renders
// reproducible.
namespace tleet::dbl
{
    class Drift
    {
    public:
        void prepare (double sampleRate, double minHoldSeconds, double maxHoldSeconds,
                      double smoothingSeconds, std::uint32_t seed) noexcept
        {
            fs = sampleRate;
            holdMin = minHoldSeconds;
            holdMax = maxHoldSeconds;
            smoothing = smoothingSeconds;
            initialSeed = seed ? seed : 0x9e3779b9u;
            setRate (1.0);
            reset();
        }

        // Speed factor (1 = base timing). Smoothing and hold times scale together.
        void setRate (double r) noexcept
        {
            rate = r > 0.0 ? r : 1.0;
            a = 1.0 - std::exp (-rate / (smoothing * fs));
        }

        void reset() noexcept
        {
            state = initialSeed;
            target = s1 = s2 = 0.0;
            countdown = 0;
        }

        double next() noexcept
        {
            if (--countdown <= 0)
            {
                target = 2.0 * uniform() - 1.0;
                countdown = (int) ((holdMin + (holdMax - holdMin) * uniform()) * fs / rate) + 1;
            }
            s1 += a * (target - s1);
            s2 += a * (s1 - s2);
            return s2;
        }

        double value() const noexcept { return s2; }

    private:
        double uniform() noexcept
        {
            // xorshift32
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (double) state * (1.0 / 4294967296.0);
        }

        double fs = 48000.0, holdMin = 0.4, holdMax = 1.0, smoothing = 0.25, rate = 1.0, a = 0.0;
        double target = 0.0, s1 = 0.0, s2 = 0.0;
        int countdown = 0;
        std::uint32_t state = 1, initialSeed = 1;
    };
}
