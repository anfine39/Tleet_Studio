#pragma once

#include "TapeModel.h"
#include <cmath>
#include <complex>
#include <algorithm>

// Linear building blocks of the tape path. Every filter has a response
// function computed from the same coefficients the audio uses, so the
// machine-response evaluator (TapeEngine::responseDb) is exact.
namespace tleet::tape
{
    using cplx = std::complex<double>;

    // Record pre-emphasis / its exact inverse (MODEL_SPEC 3.3).
    //   E(s) = (1 + s tau) / (1 + s tau / G)
    // Both directions are the plain bilinear transform of the analogue pair (no
    // prewarping), so the digital de-emphasis is the exact inverse of the digital
    // pre-emphasis at every frequency.
    struct Emphasis
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0, z = 0.0;

        void set (double tau, double gain, double fs, bool inverse) noexcept
        {
            const double K = 2.0 * tau * fs;
            double nb0 = 1.0 + K, nb1 = 1.0 - K;              // 1 + s tau
            double na0 = 1.0 + K / gain, na1 = 1.0 - K / gain; // 1 + s tau / G
            if (inverse)
            {
                std::swap (nb0, na0);
                std::swap (nb1, na1);
            }
            b0 = nb0 / na0;
            b1 = nb1 / na0;
            a1 = na1 / na0;
        }
        void reset() noexcept { z = 0.0; }
        // transposed direct form II
        double process (double x) noexcept
        {
            const double y = b0 * x + z;
            z = b1 * x - a1 * y;
            return y;
        }
        cplx response (double hz, double fs) const noexcept
        {
            const cplx zi = std::polar (1.0, -2.0 * model::pi * hz / fs);
            return (b0 + b1 * zi) / (1.0 + a1 * zi);
        }
    };

    // Trapezoidal (TPT) state-variable filter, Simper's "linear trap" form (R12):
    // stable under coefficient modulation, so speed glides and knob moves can
    // update it every control block.
    struct Svf
    {
        double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
        double ic1 = 0.0, ic2 = 0.0;

        void reset() noexcept { ic1 = ic2 = 0.0; }

        void setIdentity() noexcept { setCoeffs (0.01, 1.0, 1.0, 0.0, 0.0); }

        void setCoeffs (double gg, double kk, double mm0, double mm1, double mm2) noexcept
        {
            g = gg; k = kk; m0 = mm0; m1 = mm1; m2 = mm2;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        static double warp (double hz, double fs) noexcept { return std::tan (model::pi * std::min (hz, 0.49 * fs) / fs); }

        void setBell (double hz, double q, double db, double fs) noexcept
        {
            const double A = std::pow (10.0, db / 40.0);
            const double kk = 1.0 / (q * A);
            setCoeffs (warp (hz, fs), kk, 1.0, kk * (A * A - 1.0), 0.0);
        }
        void setLowShelf (double hz, double q, double db, double fs) noexcept
        {
            const double A = std::pow (10.0, db / 40.0);
            const double kk = 1.0 / q;
            setCoeffs (warp (hz, fs) / std::sqrt (A), kk, 1.0, kk * (A - 1.0), A * A - 1.0);
        }
        void setHighShelf (double hz, double q, double db, double fs) noexcept
        {
            const double A = std::pow (10.0, db / 40.0);
            const double kk = 1.0 / q;
            setCoeffs (warp (hz, fs) * std::sqrt (A), kk, A * A, kk * (1.0 - A) * A, 1.0 - A * A);
        }
        void setHighPass (double hz, double q, double fs) noexcept
        {
            const double kk = 1.0 / q;
            setCoeffs (warp (hz, fs), kk, 1.0, -kk, -1.0);
        }

        double process (double v0) noexcept
        {
            const double v3 = v0 - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return m0 * v0 + m1 * v1 + m2 * v2;
        }

        // H(z) on the unit circle: s = (z - 1) / (z + 1) = j tan(w / 2),
        // band = g s / D, low = g^2 / D, D = s^2 + g k s + g^2.
        cplx response (double hz, double fs) const noexcept
        {
            const cplx s (0.0, std::tan (model::pi * std::min (hz, 0.4999 * fs) / fs));
            const cplx D = s * s + g * k * s + g * g;
            return m0 + (m1 * g * s + m2 * g * g) / D;
        }
    };

    // TPT one-pole low-pass (the high-pass is x - low).
    struct OnePole
    {
        double G = 0.0, s = 0.0;
        void set (double hz, double fs) noexcept
        {
            const double g = std::tan (model::pi * std::min (hz, 0.49 * fs) / fs);
            G = g / (1.0 + g);
        }
        void reset() noexcept { s = 0.0; }
        double low (double x) noexcept
        {
            const double v = (x - s) * G;
            const double lp = v + s;
            s = lp + v;
            return lp;
        }
        cplx lowResponse (double hz, double fs) const noexcept
        {
            const double g = G / (1.0 - G);
            const cplx sv (0.0, std::tan (model::pi * std::min (hz, 0.4999 * fs) / fs));
            return g / (sv + g);
        }
    };
}
