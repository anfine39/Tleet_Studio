#pragma once

#include "TapeModel.h"
#include <array>
#include <cmath>
#include <algorithm>

// Magnetic hysteresis of the tape (MODEL_SPEC 4): the Jiles-Atherton model
// (R7) in the form Chowdhury uses for tape (R8, eq. 6, 9, 10), normalised to
// m = M / Ms and h = H / a:
//
//   Q   = h + alpha' m                       effective field
//   man = L(Q) = coth Q - 1/Q                anhysteretic magnetisation (Langevin)
//   d   = man - m
//   dm/dh = [ (1-c) delta_M d / ((1-c) delta k' - alpha' d) + c L'(Q) ] / (1 - c alpha' L'(Q))
//
// delta = sign(dh/dt); delta_M = 1 when the irreversible part moves towards
// the anhysteretic curve (sign(delta) == sign(d)), else 0. The c-term includes
// the alpha' feedback of dMan/dH exactly (solved for dm/dh), which keeps the
// purely reversible limit c -> 1 on the anhysteretic curve.
//
// Solver: classical RK4 per sample of the top rate (176.4 / 192 kHz), with h
// linear between samples (midpoint by interpolation, as in R8). The field is
// band-limited and the bias is not simulated as a signal (MODEL_SPEC 4.2), so
// the explicit RK4 is stable without R8's trapezoidal derivative.
namespace tleet::tape
{
    struct JAParams
    {
        double k = 1.227, alpha = 0.02545, c = 0.88;
    };

    namespace ja
    {
        // Langevin function and its derivative. Series below |Q| = 0.2 (the
        // closed form cancels catastrophically there), one exp() above.
        inline void langevin (double q, double& L, double& Lp) noexcept
        {
            const double s = std::abs (q);
            if (s < 0.2)
            {
                const double q2 = q * q;
                L = q * (1.0 / 3.0 - q2 * (1.0 / 45.0 - q2 * (2.0 / 945.0 - q2 / 4725.0)));
                Lp = 1.0 / 3.0 - q2 * (1.0 / 15.0 - q2 * (2.0 / 189.0 - q2 / 675.0));
                return;
            }
            const double e = std::exp (-2.0 * s), ir = 1.0 / (1.0 - e), is = 1.0 / s;
            L = std::copysign ((1.0 + e) * ir - is, q);
            Lp = is * is - 4.0 * e * ir * ir;
        }

        // dm/dh at (h, m) for a field moving in direction sgn (+1 / -1).
        // Also returns Q (for the saturation telemetry).
        inline double slope (double h, double m, double sgn, const JAParams& p, double& qOut) noexcept
        {
            const double q = h + p.alpha * m;
            double L, Lp;
            langevin (q, L, Lp);
            qOut = q;
            const double d = L - m;
            const double oneMinusC = 1.0 - p.c;
            const double rev = p.c * Lp, den2 = 1.0 - p.alpha * rev;
            if (sgn * d > 0.0)
            {
                // irreversible term (1-c) d / ((1-c) sgn k' - alpha' d) = (1-c) |d| / ((1-c) k' - alpha' |d|)
                // (sgn d > 0), its denominator kept away from zero; one division for the sum
                const double ad = std::abs (d);
                const double den = std::max (oneMinusC * p.k - p.alpha * ad, 0.25 * oneMinusC * p.k);
                return (oneMinusC * ad + rev * den) / (den * den2);
            }
            return rev / den2;
        }

        // Instantaneous saturation of the anhysteretic curve at the effective field:
        // 1 - L(|Q|) / (|Q| / 3). 0 for a linear medium, 0.5 when the layer delivers
        // half the magnetisation a linear medium would, -> 1 in hard saturation.
        inline double knee (double q) noexcept
        {
            const double s = std::abs (q);
            if (s < 1.0e-3)
                return s * s / 15.0;
            double L, Lp;
            langevin (s, L, Lp);
            return std::clamp (1.0 - 3.0 * L / s, 0.0, 1.0);
        }
    }

    class HysteresisSolver
    {
    public:
        void reset() noexcept { m = hPrev = 0.0; lastQ = 0.0; }

        // One sample of the top rate: field h -> magnetisation m (normalised).
        // order 4 = classical RK4 (4 slope evaluations, the audio path), 2 = explicit
        // midpoint (2; kept for the solver comparison in the tests).
        template <int order = 4>
        double process (double h, const JAParams& p) noexcept
        {
            const double dh = h - hPrev;
            if (! exactly (dh, 0.0))
            {
                const double sgn = dh > 0.0 ? 1.0 : -1.0;
                const double hm = 0.5 * (h + hPrev);
                double q, next;
                if constexpr (order == 4)
                {
                    const double k1 = ja::slope (hPrev, m, sgn, p, q) * dh;
                    const double k2 = ja::slope (hm, m + 0.5 * k1, sgn, p, q) * dh;
                    const double k3 = ja::slope (hm, m + 0.5 * k2, sgn, p, q) * dh;
                    const double k4 = ja::slope (h, m + k3, sgn, p, q) * dh;
                    next = m + (k1 + 2.0 * k2 + 2.0 * k3 + k4) * (1.0 / 6.0);
                }
                else
                {
                    const double k1 = ja::slope (hPrev, m, sgn, p, q) * dh;
                    next = m + ja::slope (hm, m + 0.5 * k1, sgn, p, q) * dh;
                    q = h + p.alpha * next;
                }
                m = std::isfinite (next) ? std::clamp (next, -1.0, 1.0) : 0.0;
                lastQ = q;
            }
            hPrev = h;
            return m;
        }

        double magnetisation() const noexcept { return m; }
        double effectiveField() const noexcept { return lastQ; }

    private:
        double m = 0.0, hPrev = 0.0, lastQ = 0.0;
    };

    // Fundamental gain of the solver for a 1 kHz sine of amplitude A (field units)
    // at 96 steps per cycle after 3 cycles from the demagnetised state, for the three
    // tape types and a grid of reversibilities c. Used for:
    //   - the small-signal sensitivity g_ss(c) = G(-40 dB, c) (bias sensitivity, MODEL_SPEC 4.3)
    //   - AUTO GAIN's compression make-up for the 0 VU reference tone (MODEL_SPEC 8)
    // Computed once per process (about 0.1 s), on first use from prepareToPlay.
    class HysteresisTables
    {
    public:
        static constexpr int numC = 14, numA = 41;
        static constexpr double aMinDb = -40.0, aStepDb = 2.0; // -40 .. +40 dB re 1 field unit
        static constexpr std::array<double, numC> cGrid { 0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.72, 0.78, 0.82, 0.85, 0.88, 0.91, 0.93, 0.95 };

        static const HysteresisTables& get()
        {
            static const HysteresisTables t;
            return t;
        }

        // raw fundamental gain (m amplitude / h amplitude) of type t
        double gain (int type, double c, double amplitudeDb) const noexcept
        {
            type = std::clamp (type, 0, 2);
            c = std::clamp (c, cGrid.front(), cGrid.back());
            int ci = 0;
            while (ci < numC - 2 && c > cGrid[(size_t) ci + 1])
                ++ci;
            const double fc = (c - cGrid[(size_t) ci]) / (cGrid[(size_t) ci + 1] - cGrid[(size_t) ci]);
            const double ap = std::clamp ((amplitudeDb - aMinDb) / aStepDb, 0.0, (double) (numA - 1) - 1.0e-9);
            const int ai = (int) ap;
            const double fa = ap - ai;
            auto at = [&] (int cc, int aa) { return table[(size_t) type][(size_t) cc][(size_t) aa]; };
            const double g0 = at (ci, ai) + fa * (at (ci, ai + 1) - at (ci, ai));
            const double g1 = at (ci + 1, ai) + fa * (at (ci + 1, ai + 1) - at (ci + 1, ai));
            return g0 + fc * (g1 - g0);
        }

        double smallSignalGain (int type, double c) const noexcept { return gain (type, c, aMinDb); }

        // Measure one point (also used by the tests as the reference definition).
        template <int order = 4>
        static double measure (const JAParams& p, double amplitude, int stepsPerCycle = 192, int warmCycles = 2, int cycles = 1) noexcept
        {
            HysteresisSolver s;
            s.reset();
            const double w = 2.0 * model::pi / stepsPerCycle;
            double re = 0.0, im = 0.0;
            const int total = (warmCycles + cycles) * stepsPerCycle;
            for (int n = 0; n < total; ++n)
            {
                const double y = s.template process<order> (amplitude * std::sin (w * n), p);
                if (n >= warmCycles * stepsPerCycle)
                {
                    re += y * std::cos (w * n);
                    im += y * std::sin (w * n);
                }
            }
            return 2.0 * std::sqrt (re * re + im * im) / (cycles * stepsPerCycle) / amplitude;
        }

    private:
        HysteresisTables()
        {
            for (int t = 0; t < 3; ++t)
            {
                const auto& ts = model::typeSpec (t);
                for (int ci = 0; ci < numC; ++ci)
                {
                    JAParams p { ts.kPrime, ts.alphaPrime, cGrid[(size_t) ci] };
                    for (int ai = 0; ai < numA; ++ai)
                        table[(size_t) t][(size_t) ci][(size_t) ai] = measure (p, std::pow (10.0, (aMinDb + ai * aStepDb) / 20.0));
                }
            }
        }

        std::array<std::array<std::array<double, numA>, numC>, 3> table {};
    };
}
