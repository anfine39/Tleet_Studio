#pragma once

#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>

namespace tleet::weight
{
    // Per-channel gain-reduction telemetry (MODEL_SPEC.md section 7).
    //
    // The audio thread pushes, for every sample, the gain reduction actually
    // applied by that channel's VCA (grDb = -20 log10 g, g the linear VCA
    // gain, before makeup) and publishes once per processed chunk. The UI
    // reads lock-free (atomics only). Three readings, never clipped to a
    // meter range:
    //
    //   Peak    : the deepest grDb since the previous takePeakDb() / take().
    //             If no audio was processed since then, the latest grDb.
    //   RMS     : -10 log10( (1/W) sum g^2 ) over the last W samples: the
    //             attenuation of the energy of a steady signal (the RMS of the
    //             linear gain, in dB).
    //   Average : (1/W) sum grDb over the last W samples (mean of GR in dB).
    //
    // W = round(0.3 s x sample rate) (300 ms rectangular window). Before W
    // samples have been processed, the missing samples count as no gain
    // reduction (g = 1, 0 dB), exactly as if the plugin had idled.
    class GrTelemetry
    {
    public:
        struct Readings { float peakDb, rmsDb, averageDb; };

        static constexpr double windowSeconds = 0.3;

        static int windowLength (double sampleRate) noexcept
        {
            return std::max (1, (int) std::lround (windowSeconds * sampleRate));
        }

        // ---- message thread (prepareToPlay): allocates the window
        void prepare (double sampleRate)
        {
            window = windowLength (sampleRate);
            ringDb.assign ((size_t) window, 0.0f);
            ringG2.assign ((size_t) window, 1.0f);
            reset();
        }

        void reset() noexcept
        {
            std::fill (ringDb.begin(), ringDb.end(), 0.0f);
            std::fill (ringG2.begin(), ringG2.end(), 1.0f);
            pos = 0;
            sinceExact = 0;
            sumDb = 0.0;
            sumG2 = (double) window;
            blockPeak = -1.0f;
            latest = 0.0f;
            peakHold.store (-1.0f);
            rms.store (0.0f);
            average.store (0.0f);
            current.store (0.0f);
        }

        int getWindowLength() const noexcept { return window; }

        // ---- audio thread
        void push (float grDb, float gain) noexcept
        {
            if (window <= 0)
                return;
            const float g2 = gain * gain;
            auto& oldDb = ringDb[(size_t) pos];
            auto& oldG2 = ringG2[(size_t) pos];
            sumDb += (double) grDb - (double) oldDb;
            sumG2 += (double) g2 - (double) oldG2;
            oldDb = grDb;
            oldG2 = g2;
            if (++pos >= window)
                pos = 0;
            if (++sinceExact >= window)
                resum(); // no drift: exact sums once per window
            blockPeak = std::max (blockPeak, grDb);
            latest = grDb;
        }

        void publish() noexcept
        {
            if (window <= 0)
                return;
            if (blockPeak >= 0.0f)
            {
                float cur = peakHold.load (std::memory_order_relaxed);
                while (blockPeak > cur && ! peakHold.compare_exchange_weak (cur, blockPeak, std::memory_order_acq_rel)) {}
                blockPeak = -1.0f;
            }
            rms.store (rmsDbOfWindow(), std::memory_order_relaxed);
            average.store ((float) (sumDb / window), std::memory_order_relaxed);
            current.store (latest, std::memory_order_relaxed);
        }

        // ---- UI thread (lock-free)
        float takePeakDb() noexcept
        {
            const float p = peakHold.exchange (-1.0f, std::memory_order_acq_rel);
            return p >= 0.0f ? p : current.load (std::memory_order_relaxed);
        }

        float getRmsDb() const noexcept      { return rms.load (std::memory_order_relaxed); }
        float getAverageDb() const noexcept  { return average.load (std::memory_order_relaxed); }
        float getCurrentDb() const noexcept  { return current.load (std::memory_order_relaxed); }

        // All three at once (resets the peak hold).
        Readings take() noexcept { return { takePeakDb(), getRmsDb(), getAverageDb() }; }

    private:
        float rmsDbOfWindow() const noexcept
        {
            const double meanG2 = std::max (1.0e-30, sumG2 / window);
            return (float) std::max (0.0, -10.0 * std::log10 (meanG2));
        }

        void resum() noexcept
        {
            double d = 0.0, g = 0.0;
            for (int i = 0; i < window; ++i)
            {
                d += ringDb[(size_t) i];
                g += ringG2[(size_t) i];
            }
            sumDb = d;
            sumG2 = g;
            sinceExact = 0;
        }

        std::vector<float> ringDb, ringG2;
        int window = 0, pos = 0, sinceExact = 0;
        double sumDb = 0.0, sumG2 = 0.0;
        float blockPeak = -1.0f, latest = 0.0f;

        std::atomic<float> peakHold { -1.0f }, rms { 0.0f }, average { 0.0f }, current { 0.0f };
    };
}
