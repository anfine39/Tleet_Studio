#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Building blocks of the lookahead gain computer (MODEL_SPEC 4.2). All
// buffers are sized in prepare(); the audio thread never allocates.
namespace tleet::steel
{
    inline int nextPow2 (int v) noexcept
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    // Integer delay line (power-of-two ring). Delay may change between calls
    // (the caller only does so while the line's output is not heard).
    class DelayLine
    {
    public:
        void prepare (int maxDelay)
        {
            size = nextPow2 (maxDelay + 2);
            ring.assign ((size_t) size, 0.0f);
            reset();
        }
        void reset (float value = 0.0f) noexcept { std::fill (ring.begin(), ring.end(), value); w = 0; }
        void setDelay (int d) noexcept { delay = std::clamp (d, 0, size - 2); }
        int getDelay() const noexcept { return delay; }
        float process (float x) noexcept
        {
            ring[(size_t) w] = x;
            const float y = ring[(size_t) ((w - delay) & (size - 1))];
            w = (w + 1) & (size - 1);
            return y;
        }
        // read d samples back without writing (d <= capacity)
        float read (int d) const noexcept { return ring[(size_t) ((w - 1 - d) & (size - 1))]; }
        void push (float x) noexcept { ring[(size_t) w] = x; w = (w + 1) & (size - 1); }
        int capacity() const noexcept { return size - 2; }

    private:
        std::vector<float> ring;
        int size = 2, w = 0, delay = 0;
    };

    // Running minimum over the last `window` values (Lemire's monotonic
    // wedge in a fixed ring): O(1) amortised per sample, no allocation.
    class SlidingMin
    {
    public:
        void prepare (int maxWindow)
        {
            cap = nextPow2 (maxWindow + 2);
            vals.assign ((size_t) cap, 0.0f);
            idx.assign ((size_t) cap, 0);
            setWindow (1);
            reset();
        }
        void setWindow (int w) noexcept { window = std::clamp (w, 1, cap - 1); }
        int getWindow() const noexcept { return window; }
        void reset() noexcept { head = tail = 0; t = 0; }

        float process (float v) noexcept
        {
            if (window == 1)
            {
                ++t;
                return v;
            }
            while (tail != head && vals[(size_t) ((tail - 1) & (cap - 1))] >= v)
                --tail;
            vals[(size_t) (tail & (cap - 1))] = v;
            idx[(size_t) (tail & (cap - 1))] = t;
            ++tail;
            while (idx[(size_t) (head & (cap - 1))] <= t - window)
                ++head;
            ++t;
            return vals[(size_t) (head & (cap - 1))];
        }

    private:
        std::vector<float> vals;
        std::vector<int64_t> idx;
        int64_t head = 0, tail = 0, t = 0;
        int cap = 4, window = 1;
    };

    // Moving average of length `len` (exact: float values summed in double).
    class Box
    {
    public:
        void prepare (int maxLen)
        {
            size = nextPow2 (maxLen + 1);
            ring.assign ((size_t) size, 1.0f);
            setLength (1);
            reset (1.0f);
        }
        void setLength (int l) noexcept { len = std::clamp (l, 1, size - 1); inv = 1.0 / len; }
        int getLength() const noexcept { return len; }
        void reset (float value) noexcept
        {
            std::fill (ring.begin(), ring.end(), value);
            sum = (double) value * len;
            w = 0;
        }
        float process (float x) noexcept
        {
            if (len == 1)
                return x;
            sum += (double) x - (double) ring[(size_t) ((w - len) & (size - 1))];
            ring[(size_t) w] = x;
            w = (w + 1) & (size - 1);
            return (float) (sum * inv);
        }

    private:
        std::vector<float> ring;
        double sum = 0.0, inv = 1.0;
        int size = 2, len = 1, w = 0;
    };

    // Cascade of up to three boxes whose combined support is `support`
    // samples (sum of lengths - count + 1). One box = linear ramp, two =
    // piecewise-quadratic S, three = piecewise-cubic S (near Gaussian).
    class BoxCascade
    {
    public:
        void prepare (int maxSupport)
        {
            for (auto& b : boxes)
                b.prepare (maxSupport);
        }
        void configure (int support, int count) noexcept
        {
            support = std::max (1, support);
            count = std::clamp (count, 1, 3);
            count = std::min (count, support);
            n = count;
            // lengths l_i >= 1 with sum(l_i) - n + 1 = support
            const int total = support + n - 1;
            for (int i = 0; i < 3; ++i)
                boxes[(size_t) i].setLength (i < n ? total / n + (i < total % n ? 1 : 0) : 1);
            reset (1.0f);
        }
        void reset (float value) noexcept
        {
            for (auto& b : boxes)
                b.reset (value);
        }
        float process (float x) noexcept
        {
            for (int i = 0; i < n; ++i)
                x = boxes[(size_t) i].process (x);
            return x;
        }
        int support() const noexcept
        {
            int s = 1;
            for (int i = 0; i < n; ++i)
                s += boxes[(size_t) i].getLength() - 1;
            return s;
        }

    private:
        Box boxes[3];
        int n = 1;
    };

    // Gain smoother for INPUT, OUTPUT, CEILING and LEVEL MATCH: a dB-linear (multiplicative)
    // ramp followed by a short one-pole that rounds the ramp's corners, so even a 24 dB jump
    // at full scale has no slope discontinuity (no HF click). Settles exactly on the target.
    class SmoothGain
    {
    public:
        void prepare (double sampleRate, double rampSeconds, double cornerSeconds, float initial) noexcept
        {
            steps = std::max (1, (int) std::lround (rampSeconds * sampleRate));
            a = (float) (1.0 - std::exp (-1.0 / std::max (1.0, cornerSeconds * sampleRate)));
            snap (initial);
        }
        void snap (float v) noexcept { target = current = value = v; left = 0; }
        void setTarget (float t) noexcept
        {
            if (! (t < target) && ! (t > target))
                return;
            target = t;
            left = steps;
            factor = (float) std::pow ((double) t / (double) current, 1.0 / steps);
        }
        float next() noexcept
        {
            if (left > 0)
                current = --left == 0 ? target : current * factor;
            value += a * (current - value);
            if (left == 0 && std::abs (current - value) <= 1.0e-5f * current)
                value = current;
            return value;
        }
        float getTarget() const noexcept { return target; }
        float getValue() const noexcept { return value; }
        bool isSmoothing() const noexcept { return left > 0 || value < current || value > current; }

    private:
        float target = 1.0f, current = 1.0f, value = 1.0f, factor = 1.0f, a = 1.0f;
        int steps = 1, left = 0;
    };
}
