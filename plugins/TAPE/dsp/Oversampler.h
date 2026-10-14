#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

// Oversampler for the magnetic stage (MODEL_SPEC 5). The hysteresis and the
// transport always run at 176.4 / 192 kHz ("top rate"): 4x at 44.1 / 48 kHz,
// 2x at 88.2 / 96 kHz, 1x at 176.4 / 192 kHz.
//
// Same design as STEEL's oversampler (plugins/STEEL/dsp/Halfband.h, written for
// this suite): linear-phase Kaiser-windowed half-band FIR stages of length
// N = 4k + 3 (every other tap zero, centre tap exactly 0.5). Each direction
// delays by c = 2k + 1 samples of its high rate. A short pad at the top rate
// makes the round trip a whole number of host samples, so the latency reported
// to the host is exact.
//   stage 1 (fs <-> 2 fs): k = 23 (N = 95), Kaiser 85 dB: flat (< 0.01 dB) to
//     0.44 fs = 19.4 kHz at 44.1 kHz; images above 0.56 fs rejected ~85 dB.
//   stage 2 (2 fs <-> 4 fs): k = 6 (N = 27), Kaiser 90 dB.
//   round trip, host samples: 4x = 54, 2x = 47, 1x = 0.
namespace tleet::tape
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

        inline double kaiserBeta (double db) noexcept
        {
            if (db > 50.0) return 0.1102 * (db - 8.7);
            if (db >= 21.0) return 0.5842 * std::pow (db - 21.0, 0.4) + 0.07886 * (db - 21.0);
            return 0.0;
        }
    }

    struct HalfbandDesign
    {
        int k = 0, length = 0, centre = 0;
        std::vector<float> even; // h[2j], j = 0 .. 2k + 1 (centre tap 0.5 not stored)

        static HalfbandDesign make (int k, double stopbandDb)
        {
            HalfbandDesign d;
            d.k = k;
            d.length = 4 * k + 3;
            d.centre = 2 * k + 1;
            const double beta = hb::kaiserBeta (stopbandDb), pi = 3.14159265358979323846;
            std::vector<double> h ((size_t) d.length);
            double sumEven = 0.0;
            for (int i = 0; i < d.length; ++i)
            {
                const int m = i - d.centre;
                const double x = (double) m / d.centre;
                const double w = hb::besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / hb::besselI0 (beta);
                h[(size_t) i] = (m == 0 ? 0.5 : std::sin (0.5 * pi * m) / (pi * m)) * w;
                if ((i & 1) == 0)
                    sumEven += h[(size_t) i];
            }
            d.even.resize ((size_t) (2 * k + 2));
            for (int j = 0; j < 2 * k + 2; ++j)
                d.even[(size_t) j] = (float) (h[(size_t) (2 * j)] * 0.5 / sumEven);
            return d;
        }

        // |H| of the full filter at f cycles per high-rate sample (unity at DC).
        double magnitude (double f) const noexcept
        {
            const double pi = 3.14159265358979323846;
            double re = 0.5 * std::cos (2 * pi * f * centre), im = -0.5 * std::sin (2 * pi * f * centre);
            for (int j = 0; j < (int) even.size(); ++j)
            {
                re += even[(size_t) j] * std::cos (2 * pi * f * 2 * j);
                im -= even[(size_t) j] * std::sin (2 * pi * f * 2 * j);
            }
            return std::sqrt (re * re + im * im);
        }
    };

    struct HalfbandSet
    {
        HalfbandDesign stage[2];
        static const HalfbandSet& get()
        {
            static const HalfbandSet s;
            return s;
        }

    private:
        HalfbandSet()
        {
            stage[0] = HalfbandDesign::make (23, 85.0);
            stage[1] = HalfbandDesign::make (6, 90.0);
        }
    };

    namespace hb
    {
        inline float dot8 (const float* c, const float* x, int n) noexcept
        {
            float s[8] {};
            for (int j = 0; j < n; j += 8)
                for (int l = 0; l < 8; ++l)
                    s[l] += c[j + l] * x[j + l];
            return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        }

        inline int roundUp8 (int n) noexcept { return (n + 7) & ~7; }

        // Mirrored history: after push (x[n]), window()[i] = x[n - len + 1 + i] (oldest first).
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

        inline std::vector<float> paddedEven (const std::vector<float>& even)
        {
            std::vector<float> c ((size_t) roundUp8 ((int) even.size()), 0.0f);
            std::copy (even.begin(), even.end(), c.begin());
            return c;
        }
    }

    class HalfbandUp
    {
    public:
        void prepare (const HalfbandDesign& d)
        {
            k = d.k;
            len = (int) d.even.size();
            coeffs = hb::paddedEven (d.even);
            hist.prepare (len, (int) coeffs.size() - len);
        }
        void reset() noexcept { hist.reset(); }

        // out holds 2 n samples
        void process (const float* in, float* out, int n) noexcept
        {
            const int taps = (int) coeffs.size();
            for (int i = 0; i < n; ++i)
            {
                hist.push (in[i]);
                const float* x = hist.window();
                out[2 * i] = 2.0f * hb::dot8 (coeffs.data(), x, taps);
                out[2 * i + 1] = x[len - 1 - k];
            }
        }

    private:
        std::vector<float> coeffs;
        hb::History hist;
        int len = 2, k = 0;
    };

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

        // in holds 2 n samples; in place is safe
        void process (const float* in, float* out, int n) noexcept
        {
            const int taps = (int) coeffs.size();
            for (int i = 0; i < n; ++i)
            {
                const float a = in[2 * i], b = in[2 * i + 1];
                histA.push (a);
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

    // One channel: host rate <-> top rate (factor 1, 2 or 4) with an exact
    // integer round-trip latency in host samples.
    class Oversampler
    {
    public:
        static int factorLog2ForRate (double fs) noexcept { return fs < 60000.0 ? 2 : (fs < 120000.0 ? 1 : 0); }

        static int roundTripTopSamples (int factorLog2) noexcept
        {
            const auto& s = HalfbandSet::get();
            int up = 0;
            for (int st = 0; st < factorLog2; ++st)
                up += s.stage[st].centre << (factorLog2 - 1 - st);
            return 2 * up;
        }
        static int padTopSamples (int factorLog2) noexcept
        {
            const int f = 1 << factorLog2, rt = roundTripTopSamples (factorLog2);
            return (f - rt % f) % f;
        }
        static int latencyHostSamples (int factorLog2) noexcept
        {
            return (roundTripTopSamples (factorLog2) + padTopSamples (factorLog2)) >> factorLog2;
        }

        void prepare (int factorLog2, int maxHostBlock)
        {
            log2 = std::clamp (factorLog2, 0, 2);
            const auto& s = HalfbandSet::get();
            for (int st = 0; st < 2; ++st)
            {
                up[st].prepare (s.stage[st]);
                down[st].prepare (s.stage[st]);
            }
            mid.assign ((size_t) (2 * std::max (1, maxHostBlock)), 0.0f);
            pad.assign (8, 0.0f);
            padLen = padTopSamples (log2);
            reset();
        }

        void reset() noexcept
        {
            for (auto& u : up) u.reset();
            for (auto& d : down) d.reset();
            std::fill (pad.begin(), pad.end(), 0.0f);
            padPos = 0;
        }

        int factor() const noexcept { return 1 << log2; }
        int latency() const noexcept { return latencyHostSamples (log2); }

        // in: n host samples -> out: n * factor top-rate samples
        void upsample (const float* in, float* out, int n) noexcept
        {
            if (log2 == 0)
            {
                std::copy (in, in + n, out);
                return;
            }
            if (log2 == 1)
            {
                up[0].process (in, out, n);
                return;
            }
            up[0].process (in, mid.data(), n);
            up[1].process (mid.data(), out, 2 * n);
        }

        // in: n * factor top-rate samples (modified: the pad is applied in place) -> out: n host samples
        void downsample (float* in, float* out, int n) noexcept
        {
            const int total = n << log2;
            if (padLen > 0)
                for (int i = 0; i < total; ++i)
                {
                    // pad delay of padLen top-rate samples (ring of 8)
                    const float x = in[i];
                    in[i] = pad[(size_t) ((padPos - padLen) & 7)];
                    pad[(size_t) (padPos & 7)] = x;
                    ++padPos;
                }
            if (log2 == 0)
            {
                std::copy (in, in + n, out);
                return;
            }
            if (log2 == 1)
            {
                down[0].process (in, out, n);
                return;
            }
            down[1].process (in, mid.data(), 2 * n);
            down[0].process (mid.data(), out, n);
        }

    private:
        HalfbandUp up[2];
        HalfbandDown down[2];
        std::vector<float> mid, pad;
        int log2 = 2, padLen = 0, padPos = 0;
    };
}
