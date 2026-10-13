#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

// Linear filters of ECHO, all topology-preserving-transform (TPT) structures
// in double precision: they stay well behaved when their coefficients move
// (glides, tape-speed changes). Each filter has a response function that
// evaluates the discrete transfer function of exactly that structure from
// the same coefficients, so the curves the tests (and the editor) compare
// against are the filters that run (addendum 12 / 26).
namespace tleet::echo::filt
{
    inline constexpr double pi = 3.14159265358979323846;
    inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)
    inline constexpr double maxCutoffShare = 0.45;              // of the sample rate

    using cplx = std::complex<double>;

    inline bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    // g = tan(pi fc / fs), cutoff clamped below 0.45 fs.
    inline double integratorGain (double hz, double fs) noexcept
    {
        return std::tan (pi * std::clamp (hz, 1.0, maxCutoffShare * fs) / fs);
    }

    inline cplx zInv (double hz, double fs) noexcept
    {
        const double w = 2.0 * pi * hz / fs;
        return { std::cos (w), -std::sin (w) };
    }

    // ---- second-order state-variable filter (Zavalishin TPT SVF)
    // D(z) = (1 + g k + g^2) + 2 (g^2 - 1) z^-1 + (1 - g k + g^2) z^-2
    inline cplx svfDen (double g, double k, cplx zi) noexcept
    {
        return (1.0 + g * k + g * g) + 2.0 * (g * g - 1.0) * zi + (1.0 - g * k + g * g) * zi * zi;
    }
    inline cplx svfLowpass (double g, double k, cplx zi) noexcept { return g * g * (1.0 + zi) * (1.0 + zi) / svfDen (g, k, zi); }
    inline cplx svfHighpass (double g, double k, cplx zi) noexcept { return (1.0 - zi) * (1.0 - zi) / svfDen (g, k, zi); }

    struct Svf
    {
        enum Type { lowpass, highpass };

        void reset() noexcept { s1 = s2 = 0.0; }
        void set (double gIn, double kIn) noexcept
        {
            g = gIn;
            k = kIn;
            a = 1.0 / (1.0 + g * (g + k));
        }

        template <Type type>
        double process (double x) noexcept
        {
            const double hp = (x - (g + k) * s1 - s2) * a;
            const double v1 = g * hp;
            const double bp = v1 + s1;
            s1 = bp + v1;
            const double v2 = g * bp;
            const double lp = s2 + v2;
            s2 = lp + v2;
            return type == lowpass ? lp : hp;
        }

        cplx response (Type type, cplx zi) const noexcept
        {
            return type == lowpass ? svfLowpass (g, k, zi) : svfHighpass (g, k, zi);
        }

        double g = 0.1, k = butterworthK, a = 1.0, s1 = 0.0, s2 = 0.0;
    };

    // ---- first-order TPT one-pole: LP = g (1 + z^-1) / ((1 + g) + (g - 1) z^-1)
    inline cplx onePoleLowpass (double g, cplx zi) noexcept { return g * (1.0 + zi) / ((1.0 + g) + (g - 1.0) * zi); }

    struct OnePole
    {
        void reset() noexcept { s = 0.0; }
        void set (double gIn) noexcept { G = gIn / (1.0 + gIn); g = gIn; }
        double lowpass (double x) noexcept
        {
            const double v = (x - s) * G;
            const double lp = v + s;
            s = lp + v;
            return lp;
        }
        double g = 0.1, G = 0.1, s = 0.0;
    };

    // ---- first-order shelves with a fixed half-gain (mid-dB) frequency f0.
    // Low shelf  (DC gain G):  H(s) = 1 + (G - 1) wp / (s + wp),  wp = w0 / sqrt(G)
    // High shelf (HF gain G):  H(s) = 1 + (G - 1) s  / (s + wp),  wp = w0 * sqrt(G)
    // Both are discretised with the bilinear map pre-warped at f0 (the same
    // constant for the pole, whatever G), so a high shelf of gain G followed by
    // one of gain 1/G at the same f0 is an exact inverse (tape emphasis).
    struct Shelf
    {
        // g of the one-pole for a shelf of gain `gainLin` at f0.
        static double poleG (double f0, double fs, double gainLin, bool high) noexcept
        {
            const double w0warp = std::tan (pi * std::clamp (f0, 1.0, maxCutoffShare * fs) / fs); // = w0 / c
            return high ? w0warp * std::sqrt (gainLin) : w0warp / std::sqrt (gainLin);
        }

        void reset() noexcept { lp.reset(); }
        void set (double f0, double fs, double gainLin, bool isHigh) noexcept
        {
            high = isHigh;
            gain = gainLin;
            lp.set (poleG (f0, fs, gainLin, isHigh));
        }
        double process (double x) noexcept
        {
            const double l = lp.lowpass (x);
            return high ? x + (gain - 1.0) * (x - l) : x + (gain - 1.0) * l;
        }
        cplx response (cplx zi) const noexcept
        {
            const cplx l = onePoleLowpass (lp.g, zi);
            return high ? 1.0 + (gain - 1.0) * (1.0 - l) : 1.0 + (gain - 1.0) * l;
        }

        OnePole lp;
        double gain = 1.0;
        bool high = false;
    };

    inline double toDb (cplx h) noexcept { return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h))); }

    // Group delay (samples) of a response function at hz, by central difference of the phase.
    template <typename Fn>
    double groupDelaySamples (Fn&& response, double hz, double fs) noexcept
    {
        const double dHz = std::max (0.5, hz * 1.0e-3);
        const double p1 = std::arg (response (zInv (hz - dHz, fs)));
        const double p2 = std::arg (response (zInv (hz + dHz, fs)));
        double d = p2 - p1;
        while (d > pi) d -= 2.0 * pi;
        while (d < -pi) d += 2.0 * pi;
        return -d / (2.0 * pi * 2.0 * dHz / fs);
    }

    // ---- the user HPF + LPF pair (12 dB/oct each, Butterworth)
    struct HpLp
    {
        void reset() noexcept { hp.reset(); lp.reset(); }
        void set (double hpfHz, double lpfHz, double fs) noexcept
        {
            hp.set (integratorGain (hpfHz, fs), butterworthK);
            lp.set (integratorGain (lpfHz, fs), butterworthK);
        }
        double process (double x) noexcept { return lp.process<Svf::lowpass> (hp.process<Svf::highpass> (x)); }
        cplx response (cplx zi) const noexcept { return hp.response (Svf::highpass, zi) * lp.response (Svf::lowpass, zi); }

        Svf hp, lp;
    };

    // Magnitude (dB) of the HPF + LPF exactly as run, at `hz`.
    inline double hpLpResponseDb (double hz, double hpfHz, double lpfHz, double fs) noexcept
    {
        HpLp f;
        f.set (hpfHz, lpfHz, fs);
        return toDb (f.response (zInv (hz, fs)));
    }
}
