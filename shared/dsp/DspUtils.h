#pragma once

#include <cmath>
#include <algorithm>

namespace tleet::dsp
{
    inline constexpr double pi = 3.14159265358979323846;

    inline float dbToGain (float db) noexcept      { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g, float floorDb = -120.0f) noexcept
    {
        return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
    }

    // Replaces NaN/Inf with silence so a bad host buffer cannot poison filter state.
    inline float sanitize (float x) noexcept { return std::isfinite (x) ? x : 0.0f; }

    // Coefficient of a one-pole smoother reaching 1 - 1/e of a step in `timeSeconds`.
    inline float onePoleCoeff (double timeSeconds, double sampleRate) noexcept
    {
        if (timeSeconds <= 0.0 || sampleRate <= 0.0)
            return 1.0f;
        return (float) (1.0 - std::exp (-1.0 / (timeSeconds * sampleRate)));
    }

    // First-order DC blocker (high-pass around a few Hz). Asymmetric saturation
    // stages generate DC; this removes it without touching the audio band.
    struct DCBlocker
    {
        void prepare (double sampleRate, float cutoffHz = 5.0f) noexcept
        {
            r = (float) std::exp (-2.0 * pi * cutoffHz / sampleRate);
            reset();
        }
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = y;
            return y;
        }
        float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
    };

    // Linear ramp used for click-free switching (bypass, mode changes).
    struct Ramp
    {
        void prepare (double sampleRate, double seconds) noexcept
        {
            step = (float) (1.0 / std::max (1.0, seconds * sampleRate));
        }
        void setTarget (float t) noexcept { target = t; }
        void snap (float v) noexcept      { value = target = v; }
        float next() noexcept
        {
            if (value < target)      value = std::min (target, value + step);
            else if (value > target) value = std::max (target, value - step);
            return value;
        }
        bool isSmoothing() const noexcept { return value < target || value > target; }
        float value = 0.0f, target = 0.0f, step = 0.001f;
    };
}
