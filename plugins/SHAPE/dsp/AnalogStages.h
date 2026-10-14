#pragma once

#include <algorithm>
#include <cmath>

// The analog stages of SHAPE around the passive network (MODEL_SPEC.md
// section 5). They run at the internal rate (176.4 / 192 kHz or more). Every
// constant is an APPROXIMATION calibrated against the manufacturer's THD
// figure and the published listening/measurement descriptions (RESEARCH.md
// F6, F7); DRIVE and CHARACTER are TLEET_EXTENSIONs.
//
//   transformer (input and output): the source resistance drives the
//   magnetising inductance; the winding voltage v loses the drop of the
//   magnetising current, a function of the core flux:
//       v   = x - a * h(phi),     phi = w_ref * integral(v)   (normalised: a sine
//                                  of amplitude A at f has |phi| = A * f_ref / f)
//       h(phi) = phi + k * Phi_s * f(phi / Phi_s),   f(u) = u^3 / (3 + u^2)
//     a = fc / f_ref sets the linear part, a first-order high-pass at fc. f ~
//     u^3/3 for small flux (odd, level- and LF-dependent: H3 ~ A^2 / f^3) and
//     ~u when saturated: the inductance collapses by (1 + k), the LF corner
//     rises and the low end thins and compresses. Trapezoidal integral, solved
//     implicitly per sample (one Newton step from the exact linear solution;
//     dh/dphi >= 1, so the solution is unique and the stage is passive).
//   make-up amplifier (12AX7 voltage stage + 12AU7 push-pull with feedback):
//       z = a / Vc,   s = z / sqrt(1 + z^2)                  (odd: push-pull with NFB)
//       out = Vc * (s + e2 * HP5(s^2))                       (even: the single-ended input stage)
//     Small-signal gain exactly 1 (the even term is high-passed, no DC).
namespace tleet::shape
{
    struct Voicing
    {
        double driveDb;  // extra level into the stages (Burn)
        double levelDb;  // the unit's own output level with EQ IN or out (host Bypass and MIX 0 stay at unity)
        double ampVc;    // amplifier soft-limit level Vc (linear, 1 = 0 dBFS at DRIVE 0)
        double ampEven;  // e2: single-ended (even-order) share of the amplifier
        double xfmrIn;   // k of the input transformer
        double xfmrOut;  // k of the output transformer
    };

    namespace stages
    {
        inline constexpr int numCharacters = 3;
        inline constexpr double pi = 3.14159265358979323846;
        inline constexpr double driveMaxDb = 24.0;      // DRIVE 0..100 % = 0..+24 dB into the stages
        inline constexpr double xfmrCornerHz = 2.0;     // each transformer's LF corner
        inline constexpr double fluxRefHz = 20.0;       // phi normalisation frequency
        inline constexpr double inFluxSat = 1.0;        // Phi_s input transformer (0 dBFS sine at 20 Hz = +18 dBu)
        inline constexpr double outFluxSat = 0.7;       // Phi_s output transformer (-3 dBFS sine at 20 Hz)
        inline constexpr double evenDcHz = 5.0;         // HP on the even term

        inline const Voicing& voicing (int index) noexcept
        {
            // level: the hardware is about 1.13 dB up when it is in the signal
            // path (RESEARCH F9, S2). Owner decision O4 (2026-10-13): Original
            // and Burn have it, Clean stays at unity.
            //                                          drive  level  Vc      e2      k in   k out
            static const Voicing v[numCharacters] = { { 0.0,   0.0,   11.22,  0.002,  0.6,   0.9 },    // Clean
                                                      { 0.0,   1.13,  4.467,  0.012,  7.0,   12.0 },   // Original
                                                      { 6.0,   1.13,  3.162,  0.04,   15.0,  22.5 } }; // Burn
            return v[std::clamp (index, 0, numCharacters - 1)];
        }

        inline double driveGain (double drive01) noexcept
        {
            return std::pow (10.0, driveMaxDb * std::clamp (drive01, 0.0, 1.0) / 20.0);
        }

        // Coefficient of a first-order TPT one-pole at hz.
        inline double onePoleG (double hz, double fs) noexcept
        {
            const double g = std::tan (pi * hz / fs);
            return g / (1.0 + g);
        }
    }

    // Rate-dependent constants shared by the channels.
    struct StageCoeffs
    {
        void prepare (double fs) noexcept
        {
            // trapezoidal integral; the linear corner pre-warped to land exactly at fc
            fluxStep = 2.0 * stages::pi * stages::fluxRefHz / (2.0 * fs);
            a = std::tan (stages::pi * stages::xfmrCornerHz / fs) * 2.0 * fs / (2.0 * stages::pi * stages::fluxRefHz);
            invLinear = 1.0 / (1.0 + a * fluxStep);
            evenHp = stages::onePoleG (stages::evenDcHz, fs);
        }

        double fluxStep = 0.0, a = 0.1, invLinear = 1.0, evenHp = 0.0;
    };

    struct Transformer
    {
        void reset() noexcept { phi = vPrev = 0.0; }

        static double f (double u) noexcept { return u * u * u / (3.0 + u * u); }
        static double df (double u) noexcept { const double q = 3.0 + u * u; return u * u * (9.0 + u * u) / (q * q); }

        double process (double x, double k, double fluxSat, const StageCoeffs& c) noexcept
        {
            const double base = phi + c.fluxStep * vPrev;          // phi = base + step * v
            const double ks = k * fluxSat, inv = 1.0 / fluxSat;
            // exact solution of the linear part with the non-linear drop at the predicted flux
            double v = (x - c.a * (base + ks * f (base * inv))) * c.invLinear;
            // one Newton step on F(v) = v - x + a h(base + step v)
            const double p = base + c.fluxStep * v;
            const double u = p * inv;
            const double F = v - x + c.a * (p + ks * f (u));
            const double dF = 1.0 + c.a * c.fluxStep * (1.0 + k * df (u));
            v -= F / dF;
            phi = base + c.fluxStep * v;
            vPrev = v;
            return v;
        }

        double phi = 0.0, vPrev = 0.0;
    };

    struct Amplifier
    {
        void reset() noexcept { evenState = 0.0; }

        double process (double a, double vc, double e2, const StageCoeffs& c) noexcept
        {
            const double z = a / vc;
            const double sat = z / std::sqrt (1.0 + z * z);
            const double sq = sat * sat;
            const double v = (sq - evenState) * c.evenHp;   // HP5(s^2) = s^2 - LP(s^2)
            const double lp = v + evenState;
            evenState = lp + v;
            return vc * (sat + e2 * (sq - lp));
        }

        double evenState = 0.0;
    };
}
