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
//     head output = x(t_w)                  (48-tap Kaiser-windowed sinc, see TapeInterpolator)
//
// So the delay of each head follows the whole speed history between record
// and playback exactly, as on a tape machine: a speed change bends the pitch of
// what is already on the tape and never makes the read position jump; wow and
// flutter affect every head through the same transport; head times stay in
// their fixed ratio at any speed.
namespace tleet::echo
{
    // Band-limited fractional read of the tape: a 48-tap Kaiser-windowed sinc
    // (beta 7.5, cutoff fs/2) tabulated at 512 fractional phases, linear between
    // phases, each phase normalised to unity DC gain. Flat within 0.01 dB to
    // 0.42 fs and within 0.1 dB to 0.45 fs at every fraction, zero phase (the
    // read adds no delay). A 4-point Hermite loses up to 5 dB at 0.375 fs
    // depending on the fraction, which would colour the echo with the
    // compensation's fractional part and turn wow into HF amplitude modulation.
    class TapeInterpolator
    {
    public:
        static constexpr int taps = 48, half = taps / 2, phases = 512;

        TapeInterpolator()
        {
            const double pi = 3.14159265358979323846, beta = 7.5;
            for (int ph = 0; ph <= phases; ++ph)
            {
                const double f = (double) ph / phases;
                double sum = 0.0;
                auto* row = table.data() + (size_t) ph * taps;
                for (int j = 0; j < taps; ++j)
                {
                    const double u = (double) (j - (half - 1)) - f; // tap j reads x[i + j - (half - 1)]
                    const double x = u / half;
                    const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / besselI0 (beta);
                    const double s = std::abs (u) < 1.0e-12 ? 1.0 : std::sin (pi * u) / (pi * u);
                    row[j] = s * w;
                    sum += row[j];
                }
                for (int j = 0; j < taps; ++j)
                    row[j] /= sum;
            }
        }

        static const TapeInterpolator& get()
        {
            static const TapeInterpolator instance; // built on first use (prepare)
            return instance;
        }

        // Kernel for fraction f in [0, 1): taps for x[i - 23] .. x[i + 24].
        void kernel (double f, float* out) const noexcept
        {
            const double p = f * phases;
            const int ph = std::min ((int) p, phases - 1);
            const auto fr = (float) (p - ph);
            const float* a = table.data() + (size_t) ph * taps;
            const float* b = a + taps;
            for (int j = 0; j < taps; ++j)
                out[j] = a[j] + fr * (b[j] - a[j]);
        }

    private:
        static double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 60; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-17 * sum)
                    break;
            }
            return sum;
        }

        std::array<float, (size_t) (phases + 1) * taps> table {};
    };

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
            (void) TapeInterpolator::get(); // build the shared table off the audio thread
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
            return std::clamp (d, minReadDelay, (double) (numBlocks - 3) * K);
        }

        // The interpolator reads up to half (24) samples after the read point.
        static constexpr double minReadDelay = TapeInterpolator::half + 1.0;

        // The tape `delay` samples before now (band-limited, needs delay >= minReadDelay).
        float read (double delay) const noexcept
        {
            const double t = (double) n - delay; // time index of the sample to read
            const double fl = std::floor (t);
            const auto i = (std::int64_t) fl;
            float k[TapeInterpolator::taps];
            TapeInterpolator::get().kernel (t - fl, k);
            const auto first = i - (TapeInterpolator::half - 1);
            const auto start = (size_t) (first & audioMask);
            float acc = 0.0f;
            if (start + TapeInterpolator::taps <= audio.size())
            {
                const float* x = audio.data() + start;
                for (int j = 0; j < TapeInterpolator::taps; ++j)
                    acc += k[j] * x[j];
            }
            else
            {
                for (int j = 0; j < TapeInterpolator::taps; ++j)
                    acc += k[j] * audio[(size_t) ((first + j) & audioMask)];
            }
            return acc;
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
