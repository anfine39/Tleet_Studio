#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace tleet::steel
{
    // ------------------------------------------------------------------ SUB FILTER
    // 4th-order Butterworth high-pass (24 dB/oct, -3.01 dB at the selected
    // frequency) as two cascaded TPT state-variable sections (Zavalishin,
    // "The Art of VA Filter Design", ch. 4), Q = 0.5412 and 1.3066. TPT keeps
    // the response bilinear-exact and stays well behaved while the cut-off
    // glides between 20, 25 and 30 Hz (MODEL_SPEC 3.2).
    class SubFilter
    {
    public:
        static constexpr double q1 = 0.54119610014619698, q2 = 1.3065629648763766;

        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate;
            setCutoff (cutoff);
            reset();
        }
        void reset() noexcept { s.fill (0.0); }

        void setCutoff (double hz) noexcept
        {
            cutoff = hz;
            g = std::tan (3.14159265358979323846 * std::min (hz, 0.45 * fs) / fs);
            for (int i = 0; i < 2; ++i)
            {
                const double k = 1.0 / (i == 0 ? q1 : q2);
                kk[(size_t) i] = k;
                d[(size_t) i] = 1.0 / (1.0 + k * g + g * g);
            }
        }
        double getCutoff() const noexcept { return cutoff; }

        double process (double x) noexcept
        {
            for (int i = 0; i < 2; ++i)
            {
                auto& s1 = s[(size_t) (2 * i)];
                auto& s2 = s[(size_t) (2 * i + 1)];
                const double k = kk[(size_t) i];
                const double hp = (x - (k + g) * s1 - s2) * d[(size_t) i];
                const double v1 = g * hp;
                const double bp = v1 + s1;
                s1 = bp + v1;
                const double v2 = g * bp;
                const double lp = v2 + s2;
                s2 = lp + v2;
                x = hp;
            }
            return x;
        }

        // Exact magnitude of the digital filter at `hz` (bilinear transform of
        // the analog Butterworth prototype with prewarped cut-off).
        double magnitudeDb (double hz) const noexcept
        {
            const double w = std::tan (3.14159265358979323846 * hz / fs) / g; // prewarped, normalised
            double m = 1.0;
            for (int i = 0; i < 2; ++i)
            {
                const double k = kk[(size_t) i];
                const double re = 1.0 - w * w, im = k * w;
                m *= (w * w) / std::sqrt (re * re + im * im);
            }
            return 20.0 * std::log10 (std::max (m, 1.0e-30));
        }

    private:
        double fs = 48000.0, cutoff = 20.0, g = 0.0;
        std::array<double, 2> kk {}, d {};
        std::array<double, 4> s {};
    };

    // ------------------------------------------------------------------ K-weighting
    // ITU-R BS.1770-4 K-weighting: the "pre-filter" (high shelf, +4 dB, head
    // effects) followed by the RLB high-pass. The Recommendation tabulates the
    // coefficients at 48 kHz only. For any rate they are re-derived from the
    // analogue-style parameters that reproduce that table (the derivation used
    // by libebur128 and pyloudnorm): shelf f0 = 1681.974450955533 Hz,
    // G = 3.999843853973347 dB, Q = 0.7071752369554196; high-pass
    // f0 = 38.13547087602444 Hz, Q = 0.5003270373238773. At 48 kHz this gives
    // the BS.1770 table to better than 1e-8 (test "K-weighting").
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0; }
        double process (double x) noexcept // transposed direct form II
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        double magnitude (double hz, double fs) const noexcept
        {
            const double w = 2.0 * 3.14159265358979323846 * hz / fs;
            const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
            const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
            const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
            return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
        }
    };

    struct KWeighting
    {
        Biquad shelf, highpass;

        static void design (double fs, Biquad& shelf, Biquad& hp) noexcept
        {
            const double pi = 3.14159265358979323846;
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (pi * f0 / fs);
                const double Vh = std::pow (10.0, G / 20.0);
                const double Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelf.b1 = 2.0 * (K * K - Vh) / a0;
                shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelf.a1 = 2.0 * (K * K - 1.0) / a0;
                shelf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (pi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                hp.b0 = 1.0;
                hp.b1 = -2.0;
                hp.b2 = 1.0;
                hp.a1 = 2.0 * (K * K - 1.0) / a0;
                hp.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }

        void prepare (double fs) noexcept
        {
            design (fs, shelf, highpass);
            reset();
        }
        void reset() noexcept { shelf.reset(); highpass.reset(); }
        double process (double x) noexcept { return highpass.process (shelf.process (x)); }
        double magnitude (double hz, double fs) const noexcept { return shelf.magnitude (hz, fs) * highpass.magnitude (hz, fs); }
    };
}
