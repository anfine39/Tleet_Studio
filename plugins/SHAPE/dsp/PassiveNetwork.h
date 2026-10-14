#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

// The passive equaliser network of SHAPE (MODEL_SPEC.md sections 2-4).
//
// Topology (the EQP-1A network, as compiled from the published schematics by
// Barrera et al., RESEARCH.md S4/S5): one series loop driven by the input
// transformer's secondary (an ideal source here; the source and output
// resistances are folded into the values):
//
//   vin ─► [series arm S] ─► node ─┬─ [HF atten shunt A] ──────────────┐
//                                  └─ [LF atten + load Lb] ─ out ─ R3 ┐ │
//                                                                     ├─┴─ [LF boost J] ─► vin return
//   S  = (1 − b) Rh + ( b Rh ‖ (Rq + L + C) )      HF boost pot, series-resonant LC, BANDWIDTH rheostat Rq
//   A  = (1 − a) Ra + ( a Ra ‖ (Rstop + Ca) )      HF atten pot with the ATTEN SEL capacitor
//   Lb = (Rla ‖ C1) + R2 + R3                      LF atten rheostat across the LF atten capacitor, load
//   J  = Rlb ‖ C2                                  LF boost rheostat across the LF boost capacitor (ground leg)
//   vout = v(J) + v(R3)                            (the grid of the make-up amplifier)
//
// b, a, la, lb are the pot positions after the taper (0..1). With every
// boost/atten at 0 the network is purely resistive (flat); its loss H0 is
// constant and is made up by `makeup` (the tube amplifier's job).
//
// Discretisation: every reactive element is replaced by its trapezoidal
// (bilinear) companion model v = R i + e, each element with its own bilinear
// constant K (element-wise pre-warping): the LF capacitors use K = 2 fs, the
// HF resonator is pre-warped at its resonance (exact peak frequency) with the
// characteristic impedance scaled by 1/gamma to keep the bandwidth (gamma =
// w0 T / sin(w0 T)), the HF atten capacitor is pre-warped at 0.7 x the
// selected frequency. The network stays passive, so the filter is stable for
// any (also time-varying) values. The digital transfer function is exactly the
// analog formula with C -> 1/(R_C psi) and L -> R_L psi, psi = j tan(w T / 2),
// which is what digitalResponse() evaluates: the drawn curve is the filter
// that runs.
namespace tleet::shape
{
    // What the controls ask of the network (the settled state).
    struct EqSettings
    {
        int lfIndex = 2;          // 20 / 30 / 60 / 100 Hz
        double lfBoost = 0.0;     // 0..10
        double lfAtten = 0.0;     // 0..10
        int hfIndex = 4;          // 3 / 4 / 5 / 8 / 10 / 12 / 16 kHz
        double hfBoost = 0.0;     // 0..10
        double hfBandwidth = 5.0; // 0 (Sharp) .. 10 (Broad)
        int attenIndex = 2;       // 5 / 10 / 20 kHz
        double hfAtten = 0.0;     // 0..10
        bool eqIn = true;
    };

    namespace net
    {
        inline constexpr double pi = 3.14159265358979323846;

        // ---- component values (MODEL_SPEC 2.2). Topology and the reactive
        // values from S4/S5; the five starred values are calibrated so the
        // control ranges equal the manufacturer's (S1), MODEL_SPEC 2.3.
        inline constexpr double Rh = 12000.0;             // HF boost pot
        inline constexpr double RqMin = 754.0235291;      // * resonator loss at BANDWIDTH Sharp
        inline constexpr double RqRange = 1381.995749;    // * BANDWIDTH rheostat (Sharp -> Broad, linear)
        inline constexpr double Z0 = 3464.101615137754;   // HF resonator sqrt(L/C) (L/C = 12e6)
        inline constexpr double Ra = 1000.0;              // HF atten pot
        inline constexpr double Rstop = 94.53430709;      // * HF atten stop resistor
        inline constexpr double Rla = 95506.44219;        // * LF atten pot
        inline constexpr double Rlb = 9445.867151;        // * LF boost pot
        inline constexpr double R2 = 1000.0, R3 = 10000.0; // output load (vout across R3, plus J)
        inline constexpr double lfAttenRatio = 30.0;      // C1 = C2 / 30

        inline constexpr double lfBoostCaps[] { 2.2e-6, 1.1e-6, 560.0e-9, 330.0e-9 }; // C2 at 20 / 30 / 60 / 100 Hz
        inline constexpr double hfBoostHz[] { 3000.0, 4000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0 };
        inline constexpr double attenCaps[] { 270.0e-9, 135.0e-9, 68.0e-9 };          // Ca at 5 / 10 / 20 kHz
        inline constexpr double attenHz[] { 5000.0, 10000.0, 20000.0 };
        inline constexpr double attenPrewarpShare = 0.7;  // Ca pre-warped at 0.7 x the selected frequency

        // Pot tapers r(x) = (B^x - 1) / (B - 1), x = knob / 10. B is chosen per
        // control so that knob 5 gives 40 % of the control's full range in dB at
        // its reference position (MODEL_SPEC 2.4) [APPROXIMATION].
        inline constexpr double taperLfBoost = 106.4270236;
        inline constexpr double taperLfAtten = 29.32986194;
        inline constexpr double taperHfBoost = 0.2975393072;
        inline constexpr double taperHfAtten = 0.2484878185;

        inline double taper (double knob, double B) noexcept
        {
            const double x = std::clamp (knob * 0.1, 0.0, 1.0);
            if (x <= 0.0) return 0.0;
            if (x >= 1.0) return 1.0;
            return (std::pow (B, x) - 1.0) / (B - 1.0);
        }

        // Flat (all controls at 0) voltage ratio of the network, and its inverse.
        inline constexpr double flatGain = (R3 * Ra / (Ra + R2 + R3)) / (Rh + Ra * (R2 + R3) / (Ra + R2 + R3));
        inline constexpr double makeup = 1.0 / flatGain;

        // Continuous, glidable state of the network (the model glides these).
        struct Physical
        {
            double lfBoostCap = lfBoostCaps[2];   // C2 (F); C1 = C2 / 30
            double lfBoost = 0.0, lfAtten = 0.0; // knobs 0..10
            double hfHz = 10000.0;               // resonance (Hz)
            double hfBoost = 0.0, bandwidth = 5.0;
            double attenCap = attenCaps[2];       // Ca (F)
            double attenHz = 20000.0;            // selected atten frequency (pre-warp reference)
            double hfAtten = 0.0;
        };

        inline Physical physicalFor (const EqSettings& s) noexcept
        {
            Physical p;
            p.lfBoostCap = lfBoostCaps[std::clamp (s.lfIndex, 0, 3)];
            p.lfBoost = std::clamp (s.lfBoost, 0.0, 10.0);
            p.lfAtten = std::clamp (s.lfAtten, 0.0, 10.0);
            p.hfHz = hfBoostHz[std::clamp (s.hfIndex, 0, 6)];
            p.hfBoost = std::clamp (s.hfBoost, 0.0, 10.0);
            p.bandwidth = std::clamp (s.hfBandwidth, 0.0, 10.0);
            p.attenCap = attenCaps[std::clamp (s.attenIndex, 0, 2)];
            p.attenHz = attenHz[std::clamp (s.attenIndex, 0, 2)];
            p.hfAtten = std::clamp (s.hfAtten, 0.0, 10.0);
            return p;
        }

        // Resistive parts of the network for a control state (pot legs).
        struct Pots
        {
            double rh1, rh2, rq, ra1, ra2, rla, rlb;
        };

        inline Pots potsFor (const Physical& p) noexcept
        {
            const double b = taper (p.hfBoost, taperHfBoost);
            const double a = taper (p.hfAtten, taperHfAtten);
            return { (1.0 - b) * Rh, b * Rh,
                     RqMin + RqRange * std::clamp (p.bandwidth * 0.1, 0.0, 1.0),
                     (1.0 - a) * Ra, a * Ra,
                     taper (p.lfAtten, taperLfAtten) * Rla,
                     taper (p.lfBoost, taperLfBoost) * Rlb };
        }

        // Element values of the digital network at internal rate fs: the pot
        // legs plus the companion resistances of the five reactive elements
        // (capacitor R = 1 / (K C), inductor R = K L, each with its own K).
        struct Elements
        {
            Pots pots;
            double rL, rC;   // HF resonator inductor / capacitor
            double rCa;      // HF atten capacitor
            double rC1, rC2; // LF atten / LF boost capacitors
        };

        inline double clampHz (double hz, double fs) noexcept { return std::clamp (hz, 1.0, 0.45 * fs); }

        // Bilinear constant pre-warped at hz: maps hz exactly.
        inline double prewarpK (double hz, double fs) noexcept
        {
            const double w = 2.0 * pi * clampHz (hz, fs);
            return w / std::tan (w / (2.0 * fs));
        }

        inline Elements elementsFor (const Physical& p, double fs) noexcept
        {
            Elements e;
            e.pots = potsFor (p);

            // HF resonator: exact resonance, bandwidth kept by scaling sqrt(L/C) by 1/gamma.
            const double f0 = clampHz (p.hfHz, fs);
            const double w0 = 2.0 * pi * f0;
            const double kh = prewarpK (f0, fs);
            const double wt = w0 / fs;
            const double gamma = wt / std::sin (wt);
            const double L = Z0 / (w0 * gamma), C = gamma / (w0 * Z0);
            e.rL = kh * L;
            e.rC = 1.0 / (kh * C);

            e.rCa = 1.0 / (prewarpK (attenPrewarpShare * p.attenHz, fs) * p.attenCap);

            const double k0 = 2.0 * fs;
            e.rC2 = 1.0 / (k0 * p.lfBoostCap);
            e.rC1 = lfAttenRatio / (k0 * p.lfBoostCap);
            return e;
        }

        // Voltage ratio of the network (relative to flat) from the element impedances.
        template <typename Z>
        Z responseFrom (const Pots& r, Z zL, Z zC, Z zCa, Z zC1, Z zC2) noexcept
        {
            const Z zr = r.rq + zL + zC;
            const Z zs = r.rh1 + r.rh2 * zr / (r.rh2 + zr);
            const Z zb = Rstop + zCa;
            const Z za = r.ra1 + r.ra2 * zb / (r.ra2 + zb);
            const Z zl = r.rla * zC1 / (r.rla + zC1) + R2 + R3;
            const Z zj = r.rlb * zC2 / (r.rlb + zC2);
            const Z zp = za * zl / (za + zl);
            const Z i = 1.0 / (zs + zp + zj);
            return (zj + R3 * za / (za + zl)) * i * makeup;
        }

        // The digital network's transfer function at hz (fs = internal rate).
        inline std::complex<double> digitalResponse (const Elements& e, double hz, double fs) noexcept
        {
            using C = std::complex<double>;
            const C psi (0.0, std::tan (pi * std::max (hz, 1.0e-3) / fs)); // (1 - z^-1) / (1 + z^-1) on the unit circle
            return responseFrom<C> (e.pots, e.rL * psi, e.rC / psi, e.rCa / psi, e.rC1 / psi, e.rC2 / psi);
        }

        // The analog prototype (the same network, continuous time, no pre-warping).
        inline std::complex<double> analogResponse (const Physical& p, double hz) noexcept
        {
            using C = std::complex<double>;
            const C s (0.0, 2.0 * pi * std::max (hz, 1.0e-3));
            const double w0 = 2.0 * pi * p.hfHz;
            const double L = Z0 / w0, Cr = 1.0 / (w0 * Z0);
            return responseFrom<C> (potsFor (p), L * s, 1.0 / (Cr * s), 1.0 / (p.attenCap * s),
                                    lfAttenRatio / (p.lfBoostCap * s), 1.0 / (p.lfBoostCap * s));
        }

        inline double toDb (std::complex<double> h) noexcept { return 20.0 * std::log10 (std::max (1.0e-15, std::abs (h))); }

        // Per-sample coefficients of the companion-model network (from Elements).
        // Stored as one array so the model can interpolate them per sample
        // between control updates (no zipper steps while a control glides).
        struct Coeffs
        {
            enum Index { rL, rC, rCa, rC1, rC2, gP1, k1, gP2, kb, invRA, gP3, k3, invRLb, gA, rP, gJ, kJ, rJ, invRtot, count };
            double v[count] {};

            double operator[] (Index i) const noexcept { return v[i]; }

            void set (const Elements& e) noexcept
            {
                const auto& r = e.pots;
                v[rL] = e.rL; v[rC] = e.rC; v[rCa] = e.rCa; v[rC1] = e.rC1; v[rC2] = e.rC2;

                const double rr = r.rq + e.rL + e.rC;            // resonator branch
                v[gP1] = r.rh2 / (r.rh2 + rr);
                v[k1] = (1.0 - v[gP1]) / rr;
                const double rs = r.rh1 + v[gP1] * rr;           // series arm

                const double rb = Rstop + e.rCa;                 // HF atten branch
                v[gP2] = r.ra2 / (r.ra2 + rb);
                v[kb] = (1.0 - v[gP2]) / rb;
                const double ra = r.ra1 + v[gP2] * rb;
                v[invRA] = 1.0 / ra;

                v[gP3] = r.rla / (r.rla + e.rC1);                // LF atten + load
                v[k3] = (1.0 - v[gP3]) / e.rC1;
                const double rlb = v[gP3] * e.rC1 + R2 + R3;
                v[invRLb] = 1.0 / rlb;

                v[gA] = rlb / (ra + rlb);                        // the two shunt branches
                v[rP] = ra * v[gA];

                v[gJ] = r.rlb / (r.rlb + e.rC2);                 // LF boost (ground leg)
                v[kJ] = (1.0 - v[gJ]) / e.rC2;
                v[rJ] = v[gJ] * e.rC2;

                v[invRtot] = 1.0 / (rs + v[rP] + v[rJ]);
            }
        };

        // One channel: the history sources e of the five reactive elements
        // (v = R i + e). Trapezoidal: capacitor e' = v + R i, inductor e' = -(v + R i).
        struct Channel
        {
            void reset() noexcept { eL = eC = eCa = eC1 = eC2 = 0.0; }

            double process (double x, const Coeffs& coeffs) noexcept
            {
                const double* c = coeffs.v;
                using I = Coeffs::Index;
                const double er = eL + eC;
                const double eA = c[I::gP2] * eCa;
                const double eLb = c[I::gP3] * eC1;
                const double eP = c[I::gA] * eA + (1.0 - c[I::gA]) * eLb;
                const double eJ = c[I::gJ] * eC2;
                const double i = (x - c[I::gP1] * er - eP - eJ) * c[I::invRtot];  // loop current

                const double ir = c[I::gP1] * i - c[I::k1] * er;                  // resonator
                eL = -(2.0 * c[I::rL] * ir + eL);
                eC += 2.0 * c[I::rC] * ir;

                const double vP = c[I::rP] * i + eP;                               // shunt node
                const double iA = (vP - eA) * c[I::invRA];
                const double iLb = (vP - eLb) * c[I::invRLb];

                const double ib = c[I::gP2] * iA - c[I::kb] * eCa;                 // HF atten capacitor
                eCa += 2.0 * c[I::rCa] * ib;

                const double iC1 = c[I::gP3] * iLb - c[I::k3] * eC1;               // LF atten capacitor
                eC1 += 2.0 * c[I::rC1] * iC1;

                const double vJ = c[I::rJ] * i + eJ;                               // LF boost leg
                const double iC2 = c[I::gJ] * i - c[I::kJ] * eC2;
                eC2 += 2.0 * c[I::rC2] * iC2;

                return (vJ + R3 * iLb) * makeup;
            }

            double eL = 0, eC = 0, eCa = 0, eC1 = 0, eC2 = 0;
        };
    }

    // Internal (oversampled) rate policy: the analog stages and the network run
    // at 176.4 / 192 kHz or above (MODEL_SPEC 6): 4x at 44.1 / 48 kHz, 2x at
    // 88.2 / 96 kHz, 1x at 176.4 kHz and above.
    inline int oversamplingFactorLog2For (double hostRate) noexcept
    {
        return hostRate < 80000.0 ? 2 : (hostRate < 160000.0 ? 1 : 0);
    }
    inline double internalRateFor (double hostRate) noexcept { return hostRate * (1 << oversamplingFactorLog2For (hostRate)); }

    // Magnitude (dB, relative to the flat network) of the passive EQ exactly as
    // the audio path runs it at host rate `hostRate` (settled at `s`; 0 dB when
    // EQ IN is off). This is the curve an editor may draw (MODEL_SPEC 4).
    inline double eqResponseDb (double hz, const EqSettings& s, double hostRate) noexcept
    {
        if (! s.eqIn)
            return 0.0;
        const double fs = internalRateFor (hostRate);
        return net::toDb (net::digitalResponse (net::elementsFor (net::physicalFor (s), fs), hz, fs));
    }

    // The same curve of the continuous-time prototype (documentation and tests).
    inline double analogResponseDb (double hz, const EqSettings& s) noexcept
    {
        if (! s.eqIn)
            return 0.0;
        return net::toDb (net::analogResponse (net::physicalFor (s), hz));
    }
}
