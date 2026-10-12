#pragma once

#include <cmath>
#include <algorithm>

namespace tleet::dsp
{
    // Mechanical VU needle model: a second-order system driven by the
    // rectified signal. IEC 60268-17 asks for ~300 ms to reach 99 % of a step
    // with 1-1.5 % overshoot; zeta = 0.81, wn = 13.5 rad/s meets that
    // [APPROXIMATION of the standard's ballistics, not of a specific meter].
    // Runs in the UI thread on values supplied by MeterSource.
    class VUBallistics
    {
    public:
        void reset (float position = 0.0f) noexcept { x = position; v = 0.0f; }

        // Advances the needle towards `target` (meter units) by dt seconds.
        float process (float target, float dtSeconds) noexcept
        {
            const int steps = std::max (1, (int) std::ceil (dtSeconds / 0.002f));
            const float h = dtSeconds / (float) steps;
            for (int i = 0; i < steps; ++i)
            {
                const float a = wn * wn * (target - x) - 2.0f * zeta * wn * v;
                v += a * h;
                x += v * h;
            }
            return x;
        }

        float position() const noexcept { return x; }

    private:
        float x = 0.0f, v = 0.0f;
        static constexpr float zeta = 0.81f, wn = 13.5f;
    };
}
