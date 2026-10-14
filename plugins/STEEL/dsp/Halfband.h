#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

// Linear-phase half-band FIR stages for STEEL's oversampler and true-peak
// estimator (MODEL_SPEC 5). Every stage is a Kaiser-windowed sinc with its
// cut-off exactly at a quarter of the high rate, length N = 4k + 3, so:
//   - every other tap except the centre is exactly zero (half-band);
//   - the upsampler's odd output phase is the input itself (a pure delay),
//     so original samples survive the cascade bit-exactly;
//   - the transition band is symmetric about the low rate's Nyquist, which
//     is what lets stage 1 pass 20 kHz at 44.1 kHz while rejecting the image
//     at 44.1 - 20 = 24.1 kHz.
// Group delay c = (N - 1) / 2 samples at the high rate per direction.
namespace tleet::steel
{
    namespace hb
    {
        inline double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 200; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-18 * sum)
                    break;
            }
            return sum;
        }

        // Kaiser beta for a stopband attenuation of `db` (Kaiser's formula).
        inline double kaiserBeta (double db) noexcept
        {
            if (db > 50.0) return 0.1102 * (db - 8.7);
            if (db >= 21.0) return 0.5842 * std::pow (db - 21.0, 0.4) + 0.07886 * (db - 21.0);
            return 0.0;
        }
    }

    // Coefficients of one half-band stage. `even[j]` = h[2j], j = 0 .. 2k+1
    // (the non-zero taps other than the centre, which is exactly 0.5).
    struct HalfbandDesign
    {
        int length = 0;   // N = 4k + 3
        int k = 0;
        int centre = 0;   // c = 2k + 1 (group delay in high-rate samples)
        std::vector<float> even;

        static HalfbandDesign make (int k, double stopbandDb)
        {
            HalfbandDesign d;
            d.k = k;
            d.length = 4 * k + 3;
            d.centre = 2 * k + 1;
            const double beta = hb::kaiserBeta (stopbandDb);
            const double pi = 3.14159265358979323846;
            std::vector<double> h ((size_t) d.length);
            double sumEven = 0.0;
            for (int i = 0; i < d.length; ++i)
            {
                const int m = i - d.centre;
                const double x = (double) m / d.centre;
                const double w = hb::besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / hb::besselI0 (beta);
                const double s = m == 0 ? 0.5 : std::sin (0.5 * pi * m) / (pi * m);
                h[(size_t) i] = s * w;
                if ((i & 1) == 0)
                    sumEven += h[(size_t) i];
            }
            // exact half-band: centre 0.5, the even taps sum to 0.5 (unity DC per polyphase branch)
            d.even.resize ((size_t) (2 * k + 2));
            for (int j = 0; j < 2 * k + 2; ++j)
                d.even[(size_t) j] = (float) (h[(size_t) (2 * j)] * 0.5 / sumEven);
            return d;
        }

        // Magnitude of the full filter (as used at the high rate, unity DC) at
        // normalised frequency f (cycles per high-rate sample).
        double magnitude (double f) const noexcept
        {
            const double pi = 3.14159265358979323846;
            double re = 0.0, im = 0.0;
            auto add = [&] (int i, double c) { re += c * std::cos (2 * pi * f * i); im -= c * std::sin (2 * pi * f * i); };
            add (centre, 0.5);
            for (int j = 0; j < (int) even.size(); ++j)
                add (2 * j, even[(size_t) j]);
            return std::sqrt (re * re + im * im);
        }
    };

    // The three stages of the cascade (MODEL_SPEC 5.1). Built once.
    struct HalfbandSet
    {
        HalfbandDesign stage[3];
        // First stage of the true-peak estimator (TruePeak.h): N = 511, Kaiser
        // 60 dB. Its passband reaches 0.493 fs (23.7 kHz at 48 kHz), because the
        // reconstructed peak includes content up to fs/2 (MODEL_SPEC 6.1); images
        // at -60 dB move a peak estimate by at most 0.009 dB.
        HalfbandDesign truePeakStage1;

        static const HalfbandSet& get()
        {
            static const HalfbandSet s;
            return s;
        }

    private:
        HalfbandSet()
        {
            // stage 1 (fs <-> 2 fs): N = 127, Kaiser 90 dB: flat to 0.2273 of 2 fs (20.05 kHz at
            // 44.1 kHz), images and aliases from 0.2727 (24.05 kHz) down by about 90 dB.
            stage[0] = HalfbandDesign::make (31, 90.0);
            // stage 2 (2 fs <-> 4 fs): N = 35, 100 dB; only has to protect 0..24.1 kHz.
            stage[1] = HalfbandDesign::make (8, 100.0);
            // stage 3 (4 fs <-> 8 fs): N = 23, 100 dB.
            stage[2] = HalfbandDesign::make (5, 100.0);
            truePeakStage1 = HalfbandDesign::make (127, 60.0);
        }
    };

    namespace hb
    {
        // Dot product over n (a multiple of 8) values as eight independent partial sums:
        // the compiler can vectorise it without reassociating one long sum (no fast-math).
        inline float dot8 (const float* c, const float* x, int n) noexcept
        {
            float s[8] {};
            for (int j = 0; j < n; j += 8)
                for (int l = 0; l < 8; ++l)
                    s[l] += c[j + l] * x[j + l];
            return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        }

        inline int roundUp8 (int n) noexcept { return (n + 7) & ~7; }

        // Mirrored chronological history: after push (x[n]), window()[i] = x[n - len + 1 + i]
        // for i < len (oldest first), followed by `extra` further finite values.
        class History
        {
        public:
            void prepare (int length, int extra)
            {
                len = std::max (1, length);
                buf.assign ((size_t) (2 * len + std::max (0, extra)), 0.0f);
                reset();
            }
            void reset() noexcept
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                w = len - 1;
            }
            void push (float x) noexcept
            {
                if (++w == len)
                    w = 0;
                buf[(size_t) w] = x;
                buf[(size_t) (w + len)] = x;
            }
            const float* window() const noexcept { return buf.data() + w + 1; }

        private:
            std::vector<float> buf;
            int len = 1, w = 0;
        };

        // The even-phase taps in chronological order (they are symmetric), zero-padded to a
        // multiple of 8 for dot8.
        inline std::vector<float> paddedEven (const std::vector<float>& even)
        {
            std::vector<float> c ((size_t) roundUp8 ((int) even.size()), 0.0f);
            std::copy (even.begin(), even.end(), c.begin());
            return c;
        }
    }

    // x[n] (low rate) -> y[2n], y[2n+1] (high rate).
    class HalfbandUp
    {
    public:
        void prepare (const HalfbandDesign& d)
        {
            k = d.k;
            len = (int) d.even.size(); // 2k + 2 low-rate samples of history
            coeffs = hb::paddedEven (d.even);
            hist.prepare (len, (int) coeffs.size() - len);
        }

        void reset() noexcept { hist.reset(); }

        // out holds 2 * n samples
        void process (const float* in, float* out, int n) noexcept
        {
            const int taps = (int) coeffs.size();
            for (int i = 0; i < n; ++i)
            {
                hist.push (in[i]);
                const float* x = hist.window(); // x[len - 1] = newest
                out[2 * i] = 2.0f * hb::dot8 (coeffs.data(), x, taps); // interpolated phase
                out[2 * i + 1] = x[len - 1 - k];                         // the input, delayed (2 * 0.5 * x[n - k])
            }
        }

    private:
        std::vector<float> coeffs;
        hb::History hist;
        int len = 2, k = 0;
    };

    // v[2n], v[2n+1] (high rate) -> y[n] (low rate). In place is safe (out[i] is written
    // after in[2i] and in[2i + 1] are read).
    class HalfbandDown
    {
    public:
        void prepare (const HalfbandDesign& d)
        {
            k = d.k;
            len = (int) d.even.size();
            coeffs = hb::paddedEven (d.even);
            histA.prepare (len, (int) coeffs.size() - len);
            histB.prepare (k + 2, 0);
        }

        void reset() noexcept
        {
            histA.reset();
            histB.reset();
        }

        // in holds 2 * n samples
        void process (const float* in, float* out, int n) noexcept
        {
            const int taps = (int) coeffs.size();
            for (int i = 0; i < n; ++i)
            {
                const float a = in[2 * i], b = in[2 * i + 1];
                histA.push (a); // even phase a_n = v[2n]
                // centre: 0.5 * v[2n - (2k + 1)] = 0.5 * b_{n - k - 1}; histB holds b up to n - 1
                const float acc = hb::dot8 (coeffs.data(), histA.window(), taps) + 0.5f * histB.window()[1];
                histB.push (b);
                out[i] = acc;
            }
        }

    private:
        std::vector<float> coeffs;
        hb::History histA, histB;
        int len = 2, k = 0;
    };

    // Latencies of the cascade, exactly (MODEL_SPEC 5.2). factorLog2 = 0..3.
    struct OversamplingLatency
    {
        // up path latency in samples of the top rate (2^factorLog2 * fs)
        static int upTopSamples (int factorLog2) noexcept
        {
            const auto& s = HalfbandSet::get();
            int t = 0;
            for (int st = 0; st < factorLog2; ++st)
                t += s.stage[st].centre << (factorLog2 - 1 - st);
            return t;
        }
        // round trip (up + down) in top-rate samples
        static int roundTripTopSamples (int factorLog2) noexcept { return 2 * upTopSamples (factorLog2); }
        // pad (top-rate samples) that makes the round trip a whole number of host samples
        static int padTopSamples (int factorLog2) noexcept
        {
            const int f = 1 << factorLog2;
            const int rt = roundTripTopSamples (factorLog2);
            return (f - rt % f) % f;
        }
        static int hostSamples (int factorLog2) noexcept
        {
            return (roundTripTopSamples (factorLog2) + padTopSamples (factorLog2)) >> factorLog2;
        }
    };
}
