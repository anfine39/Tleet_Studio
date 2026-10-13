#pragma once

#include <algorithm>
#include <cmath>

// The vari-mu gain law of VEIL (MODEL_SPEC.md sections 2-3). Pure functions:
// the DSP (VeilModel) and the editor's curves use exactly these.
//
// Physics being modelled (RESEARCH.md F1-F3):
//   * the Manley detector reads the OUTPUT of the gain stage (feedback);
//   * the rectified output voltage above a "delay" bias (the threshold)
//     charges the timing capacitor; that DC re-biases the remote-cutoff tube;
//   * a remote-cutoff tube's gain in dB falls roughly linearly with grid bias
//     and then faster than linearly toward cutoff.
//
// In normalised terms, with levels in dB relative to the threshold:
//   x = input level over threshold,  y = output level over threshold,
//   v = 10^(y/20)                 (output amplitude / threshold amplitude),
//   e = knee(v - 1)               (rectified excess, linear volts, quadratic knee of width w),
//   GR = G(e) = (K/lambda)(exp(lambda e) - 1)   (lambda -> 0: K e),
//   x = y + GR                    (feedback: the GR follows from the output).
// The local ratio dx/dy = 1 + G'(e) e'(v) v ln10/20 rises with the GR
// (above the knee: 1 + 0.1151 (1 + e)(K + lambda GR)), so the effective
// ratio grows with gain-reduction depth: the vari-mu signature.
//
// COMPRESS (0..10, TLEET_EXTENSION) is a macro over the law's constants:
// the side-chain gain K, the cutoff curvature lambda and the knee width w.
// 0 = onset ratio 1.25:1, no curvature, wide knee (gentler than the
// hardware's COMPRESS); 10 = onset 4:1 rising to ~20:1 at 12 dB of GR
// (the hardware's LIMIT figures, RESEARCH.md F2).
namespace tleet::veil::law
{
    inline constexpr double ln10Over20 = 0.11512925464970229;
    inline constexpr double maxGainReductionDb = 60.0; // the gain element's floor (cutoff) [APPROXIMATION]
    inline constexpr double onsetRatioMin = 1.25, onsetRatioMax = 4.0;
    inline constexpr double curvatureMax = 9.5;        // lambda at COMPRESS 10
    inline constexpr double kneeWidthMin = 1.0, kneeWidthMax = 0.4; // w at COMPRESS 0 / 10 (linear amplitude units)
    inline constexpr double maxExponent = 40.0;

    struct Constants
    {
        double k = 1.0;      // side-chain gain: dB of GR per unit of rectified excess (slope at e = 0)
        double lambda = 0.0; // remote-cutoff curvature
        double w = 1.0;      // knee width, in units of the threshold amplitude
    };

    inline Constants constantsFor (double compress) noexcept
    {
        const double t = std::clamp (compress, 0.0, 10.0) / 10.0;
        const double k0 = (onsetRatioMin - 1.0) / ln10Over20, k1 = (onsetRatioMax - 1.0) / ln10Over20;
        Constants c;
        c.k = k0 * std::pow (k1 / k0, t);
        c.lambda = curvatureMax * t * t;
        c.w = kneeWidthMin + (kneeWidthMax - kneeWidthMin) * t;
        return c;
    }

    // Rectified excess e(v) and its derivative de/dv (quadratic knee in amplitude).
    inline double excess (double v, double w, double* slope = nullptr) noexcept
    {
        const double lo = 1.0 - 0.5 * w;
        if (v <= lo)
        {
            if (slope) *slope = 0.0;
            return 0.0;
        }
        if (v < 1.0 + 0.5 * w)
        {
            const double t = v - lo;
            if (slope) *slope = t / w;
            return t * t / (2.0 * w);
        }
        if (slope) *slope = 1.0;
        return v - 1.0;
    }

    // Tube law: GR (dB) for a rectified excess e, and dGR/de.
    inline double tubeGrDb (double e, const Constants& c, double* slope = nullptr) noexcept
    {
        double g, d;
        if (c.lambda < 1.0e-9)
        {
            g = c.k * e;
            d = c.k;
        }
        else
        {
            const double a = std::min (c.lambda * e, maxExponent);
            const double ex = std::exp (a);
            g = c.k / c.lambda * (ex - 1.0);
            d = c.k * ex;
        }
        if (g >= maxGainReductionDb)
        {
            g = maxGainReductionDb;
            d = 0.0;
        }
        if (slope) *slope = d;
        return g;
    }

    // GR as a function of the output level over threshold (the feedback view).
    inline double grOfOutputDb (double y, const Constants& c) noexcept
    {
        return tubeGrDb (excess (std::pow (10.0, y / 20.0), c.w), c);
    }

    // Output level (dB over threshold) below which the law does nothing.
    inline double kneeStartDb (const Constants& c) noexcept { return 20.0 * std::log10 (1.0 - 0.5 * c.w); }

    // Reference solution of x = y + G(e(v(y))) by bisection (pure, slow, exact to 1e-12 dB).
    inline double solveOutputDbBisection (double x, const Constants& c) noexcept
    {
        const double yk = kneeStartDb (c);
        if (x <= yk)
            return x;
        double lo = yk, hi = x;
        for (int i = 0; i < 200 && hi - lo > 1.0e-13; ++i)
        {
            const double y = 0.5 * (lo + hi);
            (y + grOfOutputDb (y, c) > x ? hi : lo) = y;
        }
        return 0.5 * (lo + hi);
    }

    // ---- pure static curve (steady state), what the editor draws.
    // levelDbfs: steady detector level of the input (AES17 dBFS, sine peak);
    // thresholdDbfs: the threshold (params::thresholdToDbfs of the knob).

    // Gain reduction (dB, >= 0).
    inline double staticGainReductionDb (double levelDbfs, double thresholdDbfs, double compress) noexcept
    {
        const double x = levelDbfs - thresholdDbfs;
        return std::max (0.0, x - solveOutputDbBisection (x, constantsFor (compress)));
    }

    // Output level (dBFS) of the gain element.
    inline double staticOutputDbfs (double levelDbfs, double thresholdDbfs, double compress) noexcept
    {
        return levelDbfs - staticGainReductionDb (levelDbfs, thresholdDbfs, compress);
    }

    // Local (incremental) ratio d(input dB) / d(output dB) at a steady input level.
    inline double localRatio (double levelDbfs, double thresholdDbfs, double compress) noexcept
    {
        const auto c = constantsFor (compress);
        const double y = solveOutputDbBisection (levelDbfs - thresholdDbfs, c);
        const double v = std::pow (10.0, y / 20.0);
        double de = 0.0, dg = 0.0;
        tubeGrDb (excess (v, c.w, &de), c, &dg);
        return 1.0 + dg * de * v * ln10Over20;
    }

    // ---- fast solver used per sample by the DSP: safeguarded Newton on
    // F(y) = y + G(e(v(y))) - x (F' = local ratio >= 1), bracketed by
    // [kneeStart, x]. The caller passes a first-order prediction of the root
    // (previous root + dx / previous slope), so one Newton step from it is
    // normally enough: the step is accepted once it is below 1e-5 dB, where the
    // quadratic convergence leaves an error far below 1e-9 dB. *slope receives
    // F' at the solution (1 below the knee) for the next prediction.
    inline double solveOutputDb (double x, const Constants& c, double yk, double guess, double* slope = nullptr) noexcept
    {
        if (x <= yk)
        {
            if (slope) *slope = 1.0;
            return x;
        }
        double lo = yk, hi = x;
        double y = std::clamp (guess, lo, hi);
        double fp = 1.0;
        for (int it = 0; it < 40; ++it)
        {
            const double v = std::exp (y * ln10Over20);
            double de = 0.0, dg = 0.0;
            const double g = tubeGrDb (excess (v, c.w, &de), c, &dg);
            const double f = y + g - x;
            if (f > 0.0) hi = y; else lo = y;
            fp = 1.0 + dg * de * v * ln10Over20;
            double next = y - f / fp;
            const bool newton = next > lo && next < hi;
            if (! newton)
                next = 0.5 * (lo + hi);
            if ((newton && std::abs (next - y) < 1.0e-5) || hi - lo < 1.0e-9)
            {
                y = next;
                break;
            }
            y = next;
        }
        if (slope) *slope = fp;
        return y;
    }
}
