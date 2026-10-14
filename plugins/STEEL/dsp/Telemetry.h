#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <algorithm>

namespace tleet::steel
{
    // One 10 ms slice of the large history display (MODEL_SPEC 9.3).
    struct HistorySlice
    {
        float outPeakDb; // sample peak of the plugin output in the slice, both channels (dBFS, floor -120)
        float outRmsDb;  // RMS of the plugin output in the slice, mean of the channels' mean squares (dBFS, floor -120)
        float grDb;      // deepest gain reduction applied in the slice (dB, <= 0), the deeper channel
    };

    // Single-producer (audio thread) / single-consumer (editor timer) FIFO of
    // history slices. The writer never blocks and never waits: if the reader
    // falls more than half the ring behind (editor closed), it skips to the
    // newest half, so a reopened editor shows recent history, never stale.
    class HistoryFifo
    {
    public:
        static constexpr int capacity = 4096; // 41 s of 10 ms slices

        void push (const HistorySlice& s) noexcept
        {
            const auto w = writePos.load (std::memory_order_relaxed);
            auto& slot = ring[(size_t) (w & mask)];
            slot[0].store (s.outPeakDb, std::memory_order_relaxed);
            slot[1].store (s.outRmsDb, std::memory_order_relaxed);
            slot[2].store (s.grDb, std::memory_order_relaxed);
            writePos.store (w + 1, std::memory_order_release);
        }

        int pop (HistorySlice* dest, int maxSlices) noexcept
        {
            const auto w = writePos.load (std::memory_order_acquire);
            if (w - readPos > (uint64_t) (capacity / 2))
                readPos = w - (uint64_t) (capacity / 2);
            int n = 0;
            while (readPos < w && n < maxSlices)
            {
                const auto& slot = ring[(size_t) (readPos & mask)];
                dest[n++] = { slot[0].load (std::memory_order_relaxed), slot[1].load (std::memory_order_relaxed),
                              slot[2].load (std::memory_order_relaxed) };
                ++readPos;
            }
            return n;
        }

        uint64_t written() const noexcept { return writePos.load (std::memory_order_acquire); }

    private:
        static constexpr uint64_t mask = (uint64_t) capacity - 1;
        std::array<std::array<std::atomic<float>, 3>, capacity> ring {};
        std::atomic<uint64_t> writePos { 0 };
        uint64_t readPos = 0; // reader only
    };

    // Lock-free "max since last take" of a float (true peaks, GR peaks).
    struct PeakHold
    {
        std::atomic<float> v { 0.0f };
        void push (float x) noexcept
        {
            float cur = v.load (std::memory_order_relaxed);
            while (x > cur && ! v.compare_exchange_weak (cur, x, std::memory_order_relaxed)) {}
        }
        float take() noexcept { return v.exchange (0.0f, std::memory_order_acq_rel); }
    };
}
