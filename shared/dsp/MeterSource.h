#pragma once

#include <atomic>
#include <cmath>

namespace tleet::dsp
{
    // Lock-free hand-off of meter data from the audio thread to the UI timer.
    // The audio thread accumulates; the UI reads and resets with exchange().
    // No buffers are shared, so a slow UI can never block or corrupt audio.
    class MeterSource
    {
    public:
        // Called once per block from the audio thread.
        void pushLevel (float sumOfSquares, int numSamples, float peak) noexcept
        {
            addFloat (sumSquares, sumOfSquares);
            count.fetch_add (numSamples, std::memory_order_relaxed);
            maxFloat (peakHold, peak);
        }

        // Gain reduction in dB (positive number = attenuation), taken directly
        // from the DSP gain-control signal.
        void pushGainReduction (float grDb) noexcept { maxFloat (grHold, grDb); }

        // UI thread: RMS since the last call (linear), or -1 if no new audio.
        float takeRms() noexcept
        {
            const int n = count.exchange (0, std::memory_order_acq_rel);
            const float s = sumSquares.exchange (0.0f, std::memory_order_acq_rel);
            return n > 0 ? std::sqrt (s / (float) n) : -1.0f;
        }

        float takePeak() noexcept { return peakHold.exchange (0.0f, std::memory_order_acq_rel); }

        // UI thread: deepest gain reduction since the last call, or -1 if none was pushed.
        float takeGainReduction() noexcept { return grHold.exchange (-1.0f, std::memory_order_acq_rel); }

    private:
        static void addFloat (std::atomic<float>& a, float v) noexcept
        {
            float cur = a.load (std::memory_order_relaxed);
            while (! a.compare_exchange_weak (cur, cur + v, std::memory_order_relaxed)) {}
        }

        static void maxFloat (std::atomic<float>& a, float v) noexcept
        {
            float cur = a.load (std::memory_order_relaxed);
            while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }

        std::atomic<float> sumSquares { 0.0f }, peakHold { 0.0f }, grHold { -1.0f };
        std::atomic<int> count { 0 };
    };
}
