#pragma once

#include <array>
#include <cmath>

// Per-sample 2x up/down sampler for the record stage (preamp + tape
// saturation, MODEL_SPEC 5.4). The record stage sits inside the feedback loop,
// whose shortest delay (10 ms) can be shorter than a host block, so the loop
// runs sample by sample and a block-based oversampler cannot be used.
//
// Linear-phase FIR half-band, 47 taps, Kaiser window (beta 8): passband flat
// to 0.39 fs_os, stopband from 0.61 fs_os (about -80 dB). Polyphase: the
// upsampler's odd phase is the centre tap (a pure delay), the even phase 24
// taps; the downsampler is 24 taps plus the centre tap.
//   latency: up 23 + down 23 samples at 2x = exactly 23 host samples. Two
//   stages in cascade give 4x (23 + 11.5 = 34.5 host samples).
// The tape delay is shortened by that latency (MODEL_SPEC 4.4), so the echo
// lands on its nominal time; the plugin's own latency stays zero (the dry path
// never goes through here).
namespace tleet::echo
{
    class HalfbandCoefficients
    {
    public:
        static constexpr int taps = 47;
        static constexpr int centre = taps / 2;       // 23 (odd index)
        static constexpr int evenTaps = (taps + 1) / 2; // 24 non-zero even-index taps

        HalfbandCoefficients()
        {
            const double pi = 3.14159265358979323846, beta = 8.0;
            double sumEven = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const int m = i - centre;
                const double x = (double) m / centre;
                const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / besselI0 (beta);
                const double s = m == 0 ? 0.5 : std::sin (0.5 * pi * m) / (pi * m);
                h[(size_t) i] = s * w;
                if ((i & 1) == 0)
                    sumEven += h[(size_t) i];
            }
            // exact half-band: centre 0.5, other odd taps 0, even taps sum to 0.5 (unity DC per phase)
            for (int i = 0; i < taps; ++i)
                h[(size_t) i] = (i & 1) == 0 ? h[(size_t) i] * 0.5 / sumEven : (i == centre ? 0.5 : 0.0);
            for (int j = 0; j < evenTaps; ++j)
                even[(size_t) j] = h[(size_t) (2 * j)];
        }

        static const HalfbandCoefficients& get()
        {
            static const HalfbandCoefficients instance; // built on first use (prepare), no heap
            return instance;
        }

        std::array<double, taps> h {};
        std::array<double, evenTaps> even {}; // h[0], h[2], ... h[46]

    private:
        static double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-17 * sum)
                    break;
            }
            return sum;
        }
    };

    // x[n] (host rate) -> y[2n], y[2n+1] (2x rate).
    class Upsampler2x
    {
    public:
        static constexpr int len = HalfbandCoefficients::evenTaps; // 24 host samples of history

        void reset() noexcept { hist.fill (0.0); pos = 0; }

        void process (double x, double& y0, double& y1) noexcept
        {
            const auto& c = HalfbandCoefficients::get().even;
            pos = (pos + len - 1) % len;
            hist[(size_t) pos] = x;  // hist[pos + j] = x[n - j]
            hist[(size_t) (pos + len)] = x;
            double acc = 0.0;
            const double* xh = hist.data() + pos;
            for (int j = 0; j < len; ++j)
                acc += c[(size_t) j] * xh[j];
            y0 = 2.0 * acc;                                   // even output: the interpolated phase
            y1 = xh[HalfbandCoefficients::centre / 2];        // odd output: 2 * 0.5 * x[n - 11]
        }

    private:
        std::array<double, 2 * len> hist {}; // mirrored ring: a contiguous window always exists
        int pos = 0;
    };

    // v[2n], v[2n+1] (2x rate) -> y[n] (host rate).
    class Downsampler2x
    {
    public:
        static constexpr int len = HalfbandCoefficients::taps + 1; // 48 samples at 2x (v[2n+1] .. v[2n-46])
        static constexpr int latency2x = HalfbandCoefficients::centre;

        void reset() noexcept { hist.fill (0.0); pos = 0; }

        double process (double v0, double v1) noexcept
        {
            const auto& hc = HalfbandCoefficients::get();
            pos = (pos + len - 2) % len;
            // newest first: hist[pos] = v1 (= v[2n+1]), hist[pos + 1] = v0
            hist[(size_t) pos] = v1;
            hist[(size_t) (pos + len)] = v1;
            hist[(size_t) (pos + 1)] = v0;
            hist[(size_t) (pos + 1 + len)] = v0;
            // Decimate at the even phase: y[n] = sum_i h[i] v[2n - i], so that up
            // (23) + down (23) = 46 samples at 2x = exactly 23 host samples.
            const double* vh = hist.data() + pos + 1; // vh[i] = v[2n - i]
            // odd i: only the centre tap
            double acc = 0.5 * vh[HalfbandCoefficients::centre];
            for (int j = 0; j < HalfbandCoefficients::evenTaps; ++j)
                acc += hc.even[(size_t) j] * vh[2 * j];
            return acc;
        }

    private:
        std::array<double, 2 * len> hist {};
        int pos = 0;
    };
}
