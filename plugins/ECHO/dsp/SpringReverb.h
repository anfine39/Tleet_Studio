#pragma once

#include "Filters.h"
#include <vector>
#include <algorithm>
#include <cmath>

// Spring reverb (MODEL_SPEC 8): the parametric dispersive-waveguide structure
// of Välimäki, Parker and Abel, "Parametric spring reverberation effect",
// JAES 58(7/8), 2010 (RESEARCH S5), as summarised and refined by Parker,
// EURASIP JASP 2011:646134 (S6). Two parallel recirculating paths:
//
//   C_lf (low chirps, the "drip"): x + g_lf * fb -> M_lf stretched allpasses
//        A(z) = (a + z^-k) / (1 + a z^-k), a > 0  -> 4th-order lowpass at f_C -> y_lf
//        y_lf -> delay DL_lf -> fb
//     group delay of the cascade: D(w) = k M (1 - a^2) / (1 + 2 a cos(w k) + a^2)
//     rises from k M (1-a)/(1+a) at DC to k M (1+a)/(1-a) at w k = pi, i.e. at
//     f_C = fs / (2k): low frequencies arrive first, the characteristic chirp.
//     The stretched allpass repeats its response above f_C; the lowpass
//     removes those copies (S5/S6 use an 8th-order crossover; 4th order here).
//   C_hf (high chirps): x + g_hf * fb -> M_hf first-order allpasses (k = 1, a < 0)
//        -> y_hf -> delay DL_hf -> fb. Wide band, much lower level (S6).
//
// k is an integer (fs / (2 f_C) rounded, f_C designed at 4.4 kHz), so the actual
// f_C is 4.36..4.80 kHz depending on the sample rate (4.41 kHz at 44.1, 4.80 at 48,
// 4.36 at 96 kHz). No modulation of the delay lines (S5's random modulation is
// omitted; MODEL_SPEC 13).
namespace tleet::echo
{
    struct SpringDesign
    {
        double fcHz;      // transition frequency f_C
        int mLow;         // stretched sections
        double aLow;
        double dlLowMs;
        double gLow;
        int mHigh;
        double aHigh;
        double dlHighMs;
        double gHigh;
        double highLevel; // C_hf level relative to C_lf
    };

    namespace spring
    {
        // Two springs (the RE-201 tank has three; two decorrelated pickups give
        // the stereo image, MODEL_SPEC 8.2). [APPROXIMATION values in the S5 structure]
        inline constexpr SpringDesign designA { 4400.0, 60, 0.62, 41.0, 0.88, 30, -0.62, 23.0, 0.86, 0.12 };
        inline constexpr SpringDesign designB { 4400.0, 56, 0.58, 46.5, 0.88, 28, -0.58, 26.3, 0.86, 0.12 };
        inline constexpr double inputHpfHz = 90.0;   // the tank's driver passes little bass
        inline constexpr double outputGain = 0.42;   // level calibration (MODEL_SPEC 8.3)
    }

    class Spring
    {
    public:
        void prepare (double sampleRate, const SpringDesign& d)
        {
            fs = sampleRate;
            design = d;
            k = std::max (1, (int) std::lround (fs / (2.0 * d.fcHz)));
            lowBuf.assign ((size_t) (d.mLow * k), 0.0);
            highState.assign ((size_t) d.mHigh, 0.0);
            dlLow = std::max (1, (int) std::lround (d.dlLowMs * 0.001 * fs));
            dlHigh = std::max (1, (int) std::lround (d.dlHighMs * 0.001 * fs));
            lowDelay.assign ((size_t) dlLow, 0.0);
            highDelay.assign ((size_t) dlHigh, 0.0);
            const double fcActual = fs / (2.0 * k);
            const double gl = filt::integratorGain (0.92 * fcActual, fs);
            lp1.set (gl, 0.7653668647301796);  // 4th-order Butterworth: Q1 = 1.307, Q2 = 0.541
            lp2.set (gl, 1.8477590650225735);
            inHp.set (filt::integratorGain (spring::inputHpfHz, fs), filt::butterworthK);
            reset();
        }

        void reset() noexcept
        {
            std::fill (lowBuf.begin(), lowBuf.end(), 0.0);
            std::fill (highState.begin(), highState.end(), 0.0);
            std::fill (lowDelay.begin(), lowDelay.end(), 0.0);
            std::fill (highDelay.begin(), highDelay.end(), 0.0);
            pos = 0; lowPos = 0; highPos = 0;
            lp1.reset(); lp2.reset(); inHp.reset();
        }

        double process (double x) noexcept
        {
            x = inHp.process<filt::Svf::highpass> (x);

            // ---- C_lf: stretched allpass cascade (all sections share one ring index)
            double u = x + design.gLow * lowDelay[(size_t) lowPos];
            const double a = design.aLow;
            double* b = lowBuf.data() + pos;
            for (int m = 0; m < design.mLow; ++m, b += k)
            {
                const double wOld = *b;          // w[n - k]
                const double w = u - a * wOld;
                u = a * w + wOld;
                *b = w;
            }
            pos = pos + 1 == k ? 0 : pos + 1;
            const double yLow = lp2.process<filt::Svf::lowpass> (lp1.process<filt::Svf::lowpass> (u));
            lowDelay[(size_t) lowPos] = yLow;
            lowPos = lowPos + 1 == dlLow ? 0 : lowPos + 1;

            // ---- C_hf: first-order allpass cascade
            double v = x + design.gHigh * highDelay[(size_t) highPos];
            const double ah = design.aHigh;
            for (auto& s : highState)
            {
                const double w = v - ah * s;
                v = ah * w + s;
                s = w;
            }
            highDelay[(size_t) highPos] = v;
            highPos = highPos + 1 == dlHigh ? 0 : highPos + 1;

            return spring::outputGain * (yLow + design.highLevel * v);
        }

        // Group delay (samples) of the C_lf cascade at hz: the dispersion law.
        double lowChirpDelaySamples (double hz) const noexcept
        {
            const double w = 2.0 * filt::pi * hz / fs, a = design.aLow;
            return k * design.mLow * (1.0 - a * a) / (1.0 + 2.0 * a * std::cos (w * k) + a * a);
        }

        double getTransitionHz() const noexcept { return fs / (2.0 * k); }
        int getStretch() const noexcept { return k; }
        int getLowLoopDelay() const noexcept { return dlLow; }

    private:
        SpringDesign design = spring::designA;
        double fs = 48000.0;
        int k = 6, pos = 0, dlLow = 1, dlHigh = 1, lowPos = 0, highPos = 0;
        std::vector<double> lowBuf, highState, lowDelay, highDelay;
        filt::Svf lp1, lp2, inHp;
    };
}
