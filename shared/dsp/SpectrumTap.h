#pragma once

#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace tleet::dsp
{
    // Lock-free hand-off of audio samples from the audio thread to a spectrum
    // analyser in the UI (addendum 13: the analyser shows real audio). The
    // audio thread writes a mono sum into a ring buffer, only while an editor
    // has switched the tap on, so a closed GUI costs one atomic load per block.
    // The UI reads whatever arrived since its last read; when it falls more
    // than the ring behind it simply skips to the newest samples.
    class SpectrumTap
    {
    public:
        static constexpr int capacity = 1 << 15; // power of two, > 0.5 s at 48 kHz

        SpectrumTap() : ring ((size_t) capacity, 0.0f) {}

        void setSampleRate (double sr) noexcept { sampleRate.store (sr, std::memory_order_relaxed); }
        double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }

        // UI thread: the editor turns the tap on while an analyser is visible.
        void setActive (bool on) noexcept { active.store (on, std::memory_order_release); }
        bool isActive() const noexcept { return active.load (std::memory_order_acquire); }

        // Audio thread. `right` may be null for mono; stereo is summed to (L + R) / 2.
        void push (const float* left, const float* right, int numSamples) noexcept
        {
            if (! isActive() || left == nullptr || numSamples <= 0)
                return;
            auto w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
                ring[(size_t) (w & mask)] = x;
                ++w;
            }
            writePos.store (w, std::memory_order_release);
        }

        // UI thread: copies up to `maxSamples` of the newest unread samples into
        // `dest` (oldest first) and returns how many were copied.
        int pull (float* dest, int maxSamples) noexcept
        {
            const auto w = writePos.load (std::memory_order_acquire);
            auto available = (int64_t) (w - readPos);
            if (available <= 0 || maxSamples <= 0)
                return 0;
            // keep a margin so the writer cannot overtake what is being copied
            const auto limit = (int64_t) std::min (maxSamples, capacity / 2);
            if (available > limit)
            {
                readPos = w - (uint64_t) limit;
                available = limit;
            }
            for (int64_t i = 0; i < available; ++i)
                dest[i] = ring[(size_t) ((readPos + (uint64_t) i) & mask)];
            readPos += (uint64_t) available;
            return (int) available;
        }

    private:
        static constexpr uint64_t mask = (uint64_t) capacity - 1;
        std::vector<float> ring;
        std::atomic<uint64_t> writePos { 0 };
        uint64_t readPos = 0; // UI thread only
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<bool> active { false };
    };
}
