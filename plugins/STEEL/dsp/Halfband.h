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
        }
    };

    // x[n] (low rate) -> y[2n], y[2n+1] (high rate). Mirrored history ring.
    class HalfbandUp
    {
    public:
        void prepare (const HalfbandDesign& d)
        {
            design = &d;
            len = (int) d.even.size(); // 2k + 2 low-rate samples of history
            hist.assign ((size_t) (2 * len), 0.0f);
            reset();
        }

        void reset() noexcept
        {
            std::fill (hist.begin(), hist.end(), 0.0f);
            pos = 0;
        }

        // out holds 2 * n samples
        void process (const float* in, float* out, int n) noexcept
        {
            const float* c = design->even.data();
            const int half = len / 2; // k + 1 symmetric pairs
            const int k = design->k;
            for (int i = 0; i < n; ++i)
            {
                pos = (pos + len - 1) % len;
                hist[(size_t) pos] = in[i];
                hist[(size_t) (pos + len)] = in[i];
                const float* xh = hist.data() + pos; // xh[j] = x[n - j]
                float acc = 0.0f;
                for (int j = 0; j < half; ++j)
                    acc += c[j] * (xh[j] + xh[len - 1 - j]);
                out[2 * i] = 2.0f * acc;      // interpolated phase
                out[2 * i + 1] = xh[k];       // the input, delayed (2 * 0.5 * x[n - k])
            }
        }

    private:
        const HalfbandDesign* design = nullptr;
        std::vector<float> hist;
        int len = 1, pos = 0;
    };

    // v[2n], v[2n+1] (high rate) -> y[n] (low rate).
    class HalfbandDown
    {
    public:
        void prepare (const HalfbandDesign& d)
        {
            design = &d;
            lenA = (int) d.even.size();     // even-phase history (a_n = v[2n])
            lenB = d.k + 2;                 // odd-phase history (b_n = v[2n+1]); centre needs b[n-k-1]
            histA.assign ((size_t) (2 * lenA), 0.0f);
            histB.assign ((size_t) (2 * lenB), 0.0f);
            reset();
        }

        void reset() noexcept
        {
            std::fill (histA.begin(), histA.end(), 0.0f);
            std::fill (histB.begin(), histB.end(), 0.0f);
            posA = posB = 0;
        }

        // in holds 2 * n samples
        void process (const float* in, float* out, int n) noexcept
        {
            const float* c = design->even.data();
            const int half = lenA / 2;
            for (int i = 0; i < n; ++i)
            {
                posA = (posA + lenA - 1) % lenA;
                histA[(size_t) posA] = in[2 * i];
                histA[(size_t) (posA + lenA)] = in[2 * i];
                const float* a = histA.data() + posA; // a[j] = v[2(n - j)]
                // b history holds v[2m+1] for m <= n - 1 at this point
                const float* b = histB.data() + posB; // b[j] = v[2(n - 1 - j) + 1]
                float acc = 0.0f;
                for (int j = 0; j < half; ++j)
                    acc += c[j] * (a[j] + a[lenA - 1 - j]);
                // centre: 0.5 * v[2n - c] with c = 2k + 1 -> v[2(n - k - 1) + 1] = b[k]
                acc += 0.5f * b[design->k];
                out[i] = acc;
                posB = (posB + lenB - 1) % lenB;
                histB[(size_t) posB] = in[2 * i + 1];
                histB[(size_t) (posB + lenB)] = in[2 * i + 1];
            }
        }

    private:
        const HalfbandDesign* design = nullptr;
        std::vector<float> histA, histB;
        int lenA = 1, lenB = 1, posA = 0, posB = 0;
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
