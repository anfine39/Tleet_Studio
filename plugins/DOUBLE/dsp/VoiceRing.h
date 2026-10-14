#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// Mono delay ring of the voice feed with band-limited fractional reads.
//
// Every voice is a moving read tap on this ring (delay + micro-pitch). The
// read uses a 12-tap Kaiser-windowed sinc (beta 5) with 512 tabulated phases,
// linearly interpolated: flat within 0.05 dB to 16 kHz at 44.1 kHz and
// 17 kHz at 48 kHz for every fractional position (MODEL_SPEC.md section 4),
// and exactly a unit impulse at integer delays, so an unmodulated integer
// delay is bit-transparent. Below 5 samples of delay the centred kernel would
// need future input; there the read falls back to linear interpolation
// (exact at integer delays), keeping the plugin at zero latency.
//
// The ring is mirrored (every sample written twice) so a kernel or a
// correlation window is always a contiguous run of memory.
namespace tleet::dbl
{
    class SincTable
    {
    public:
        static constexpr int taps = 12;      // delays n-5 ... n+6 around floor(delay) = n
        static constexpr int phases = 512;
        static constexpr double beta = 5.0;

        SincTable()
        {
            const double halfWidth = taps / 2;
            for (int p = 0; p <= phases; ++p)
            {
                const double frac = (double) p / phases;
                double sum = 0.0;
                std::array<double, taps> h {};
                // index m covers delay offset j = 6 - m (ascending memory order)
                for (int m = 0; m < taps; ++m)
                {
                    const int j = taps / 2 - m;
                    const double t = (double) j - frac;
                    const double x = t / halfWidth;
                    const double w = x * x < 1.0 ? besselI0 (beta * std::sqrt (1.0 - x * x)) / besselI0 (beta) : 0.0;
                    // integer positions are exact zeros / one, so integer delays read bit-exactly
                    const bool integer = p == 0 || p == phases;
                    const double s = integer ? (std::abs (t) < 0.5 ? 1.0 : 0.0) : std::sin (pi * t) / (pi * t);
                    h[(size_t) m] = s * w;
                    sum += h[(size_t) m];
                }
                for (int m = 0; m < taps; ++m)
                    table[(size_t) (p * taps + m)] = (float) (h[(size_t) m] / sum); // unity DC gain
            }
        }

        const float* phase (int p) const noexcept { return table.data() + (size_t) p * taps; }

        static const SincTable& get()
        {
            static const SincTable instance; // static storage: no heap, built on first use (prepare)
            return instance;
        }

    private:
        static constexpr double pi = 3.14159265358979323846;

        static double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-17 * sum)
                    break;
            }
            return sum;
        }

        std::array<float, (size_t) ((phases + 1) * taps)> table {};
    };

    class VoiceRing
    {
    public:
        static constexpr int minSincDelay = SincTable::taps / 2 - 1; // 5 samples

        void prepare (int minimumCapacity)
        {
            size = 1;
            while (size < minimumCapacity)
                size <<= 1;
            mask = size - 1;
            data.assign ((size_t) (2 * size), 0.0f);
            writePos = 0;
            table = &SincTable::get();
        }

        void reset() noexcept
        {
            std::fill (data.begin(), data.end(), 0.0f);
            writePos = 0;
        }

        int capacity() const noexcept { return size; }

        // Appends samples; sample k of this call gets index position() + k.
        void write (const float* x, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const int w = (writePos + i) & mask;
                data[(size_t) w] = x[i];
                data[(size_t) (w + size)] = x[i];
            }
            writePos = (writePos + n) & mask;
        }

        int position() const noexcept { return writePos; }

        // Index (in the ring) of the sample written `k` samples after `start`.
        int indexFrom (int start, int k) const noexcept { return (start + k) & mask; }

        // Value `delay` samples before the sample at ring index `now` (fractional, band-limited).
        float read (int now, double delay) const noexcept
        {
            if (delay < (double) minSincDelay)
            {
                delay = std::max (0.0, delay);
                const int n = (int) delay;
                const float f = (float) (delay - n);
                const float a = data[(size_t) ((now - n) & mask)];
                const float b = data[(size_t) ((now - n - 1) & mask)];
                return a + f * (b - a);
            }

            const int n = (int) delay;
            const double ph = (delay - n) * SincTable::phases;
            const int p = (int) ph;
            const float t = (float) (ph - p);
            const float* k0 = table->phase (p);
            const float* k1 = table->phase (p + 1);
            const float* x = data.data() + ((now - n - SincTable::taps / 2) & mask); // delay n+6 first
            float acc = 0.0f;
            for (int m = 0; m < SincTable::taps; ++m)
                acc += (k0[m] + t * (k1[m] - k0[m])) * x[m];
            return acc;
        }

        // Contiguous run of `n` samples, oldest first, ending `delay` samples before `now`.
        const float* run (int now, int delay, int n) const noexcept
        {
            return data.data() + ((now - delay - (n - 1)) & mask);
        }

    private:
        std::vector<float> data;
        const SincTable* table = nullptr;
        int size = 1, mask = 0, writePos = 0;
    };
}
