#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

// The virtual tape (MODEL_SPEC 4.1): one tape, one speed, three playback
// heads at fixed distances from the record head.
//
// The record head writes one sample per host sample into a time-indexed ring.
// The tape speed v (tape-distance units per sample; head 1 sits 1 unit from the
// record head) is held constant over control blocks of K = 32 samples, and the
// tape position at the start of each block is stored (double precision). The
// position is therefore an exact piecewise-linear function of time, and a head
// at distance r reads, at sample n, the sample that was recorded when the
// tape was at P(n) - r:
//
//     find t_w with P(t_w) = P(n) - r      (block search, then linear inside the block)
//     head output = x(t_w)                  (4-point cubic Hermite)
//
// So the delay of each head follows the whole speed history between record
// and playback exactly, as on a tape machine: a speed change bends the pitch of
// what is already on the tape and never makes the read position jump; wow and
// flutter affect every head through the same transport; head times stay in
// their fixed ratio at any speed.
namespace tleet::echo
{
    class TapeTransport
    {
    public:
        static constexpr int blockLog2 = 5;
        static constexpr int K = 1 << blockLog2;  // control block (samples)
        static constexpr int numHeads = 3;

        // maxDelaySamples: the longest delay any head can reach.
        void prepare (double maxDelaySamples, double initialSpeed)
        {
            const auto blocksNeeded = (std::int64_t) std::ceil (maxDelaySamples / K) + 8;
            numBlocks = 1;
            while (numBlocks < blocksNeeded)
                numBlocks <<= 1;
            blockMask = numBlocks - 1;
            audioMask = numBlocks * K - 1;
            audio.assign ((size_t) (numBlocks * K), 0.0f);
            pos.assign ((size_t) numBlocks, 0.0);
            speed.assign ((size_t) numBlocks, initialSpeed);
            reset (initialSpeed);
        }

        // Silence on the tape and a constant-speed history.
        void reset (double initialSpeed) noexcept
        {
            std::fill (audio.begin(), audio.end(), 0.0f);
            // The next beginBlock() opens block numBlocks at tape position 0;
            // the history before it is a constant-speed tape (every index positive).
            n = numBlocks * K;
            currentBlock = numBlocks - 1;
            for (std::int64_t b = 0; b < numBlocks; ++b)
            {
                pos[(size_t) (b & blockMask)] = (double) (b - numBlocks) * K * initialSpeed;
                speed[(size_t) (b & blockMask)] = initialSpeed;
            }
            for (auto& c : cursor)
                c = numBlocks - 1;
        }

        bool atBlockStart() const noexcept { return (n & (K - 1)) == 0; }

        // Start a control block (call when atBlockStart()) with speed v > 0.
        // Calling it again within the same block only changes that block's speed.
        void beginBlock (double v) noexcept
        {
            const auto blk = n >> blockLog2;
            if (blk != currentBlock)
            {
                const auto prev = (size_t) (currentBlock & blockMask);
                const double p = pos[prev] + K * speed[prev];
                currentBlock = blk;
                pos[(size_t) (blk & blockMask)] = p;
            }
            speed[(size_t) (blk & blockMask)] = v;
        }

        double positionNow() const noexcept
        {
            const auto b = (size_t) (currentBlock & blockMask);
            return pos[b] + (double) (n - (currentBlock << blockLog2)) * speed[b];
        }

        double currentSpeed() const noexcept { return speed[(size_t) (currentBlock & blockMask)]; }

        // Delay (samples before now) of the sample recorded at tape position `target`.
        double locate (int head, double target) noexcept
        {
            auto& c = cursor[(size_t) head];
            const std::int64_t oldest = currentBlock - numBlocks + 2, newest = currentBlock - 1;
            c = std::clamp (c, oldest, newest);
            int steps = 0;
            while (c < newest && pos[(size_t) ((c + 1) & blockMask)] <= target && ++steps < 8)
                ++c;
            while (c > oldest && pos[(size_t) (c & blockMask)] > target && ++steps < 8)
                --c;
            if (steps >= 8) // far jump (only after a reset or extreme modulation): binary search
            {
                std::int64_t lo = oldest, hi = newest;
                while (lo < hi)
                {
                    const auto mid = (lo + hi + 1) >> 1;
                    if (pos[(size_t) (mid & blockMask)] <= target) lo = mid; else hi = mid - 1;
                }
                c = lo;
            }
            const auto b = (size_t) (c & blockMask);
            const double offset = std::clamp ((target - pos[b]) / speed[b], 0.0, (double) K);
            const double d = (double) (n - (c << blockLog2)) - offset;
            return std::clamp (d, 3.0, (double) (numBlocks - 3) * K);
        }

        // The tape `delay` samples before now (cubic Hermite, needs delay >= 3).
        float read (double delay) const noexcept
        {
            const double t = (double) n - delay; // time index of the sample to read
            const double fl = std::floor (t);
            const auto i = (std::int64_t) fl;
            const float f = (float) (t - fl);
            const float xm1 = audio[(size_t) ((i - 1) & audioMask)];
            const float x0  = audio[(size_t) (i & audioMask)];
            const float x1  = audio[(size_t) ((i + 1) & audioMask)];
            const float x2  = audio[(size_t) ((i + 2) & audioMask)];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        // Record the current sample and advance time.
        void write (float x) noexcept
        {
            audio[(size_t) (n & audioMask)] = x;
            ++n;
        }

        std::int64_t sampleIndex() const noexcept { return n; }
        std::int64_t capacitySamples() const noexcept { return numBlocks * K; }
        size_t memoryBytes() const noexcept { return audio.size() * sizeof (float) + pos.size() * 2 * sizeof (double); }

    private:
        std::vector<float> audio;
        std::vector<double> pos, speed;
        std::array<std::int64_t, numHeads> cursor {};
        std::int64_t numBlocks = 1, blockMask = 0, audioMask = 0, currentBlock = 0, n = 0;
    };
}
