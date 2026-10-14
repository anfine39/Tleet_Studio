#pragma once

#include <cmath>
#include <complex>
#include <algorithm>
#include "VoiceTemplates.h"

// Wet-path tone filters of DOUBLE: HPF (12 dB/oct) and LPF (12 dB/oct), both
// Butterworth, plus the GHOST high shelf. They run once on the mono voice
// feed, ahead of the voice taps, so every voice and every voice meter sees
// exactly this response (MODEL_SPEC.md section 5).
//
// Implementation: topology-preserving-transform state-variable filters
// (bilinear transform with the cutoff pre-warped), double precision. The
// response functions below evaluate the discrete-time transfer function of
// those same structures from the same coefficients (g, k), so the curve the
// editor draws is the filter that runs, not an analogue approximation.
namespace tleet::dbl
{
    namespace wetfilter
    {
        inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)

        // exact comparison without -Wfloat-equal noise (glide end points are exact by construction)
        inline constexpr bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }
        inline constexpr double maxCutoffShare = 0.49;              // of the sample rate

        // g = tan(pi fc / fs): the pre-warped integrator gain shared by the DSP and the response.
        inline double integratorGain (double cutoffHz, double sampleRate) noexcept
        {
            const double fc = std::clamp (cutoffHz, 1.0, maxCutoffShare * sampleRate);
            return std::tan (3.14159265358979323846 * fc / sampleRate);
        }

        inline double shelfGain (int mode) noexcept
        {
            return std::pow (10.0, templates::modes[(size_t) templates::clampMode (mode)].shelfDb / 20.0);
        }

        // z^-1 on the unit circle
        inline std::complex<double> zInv (double hz, double sampleRate) noexcept
        {
            const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
            return { std::cos (w), -std::sin (w) };
        }

        // Second-order TPT SVF: D(z) = (1 + g k + g^2) + 2 (g^2 - 1) z^-1 + (1 - g k + g^2) z^-2
        inline std::complex<double> svfDenominator (double g, double k, std::complex<double> zi) noexcept
        {
            return (1.0 + g * k + g * g) + 2.0 * (g * g - 1.0) * zi + (1.0 - g * k + g * g) * zi * zi;
        }

        inline std::complex<double> svfLowpass (double g, double k, std::complex<double> zi) noexcept
        {
            return g * g * (1.0 + zi) * (1.0 + zi) / svfDenominator (g, k, zi);
        }

        inline std::complex<double> svfHighpass (double g, double k, std::complex<double> zi) noexcept
        {
            return (1.0 - zi) * (1.0 - zi) / svfDenominator (g, k, zi);
        }

        // First-order TPT low-pass: g (1 + z^-1) / ((1 + g) + (g - 1) z^-1)
        inline std::complex<double> onePoleLowpass (double g, std::complex<double> zi) noexcept
        {
            return g * (1.0 + zi) / ((1.0 + g) + (g - 1.0) * zi);
        }

        // High shelf y = LP1 + G * (1 - LP1)
        inline std::complex<double> shelf (double g1, double gain, std::complex<double> zi) noexcept
        {
            const auto lp = onePoleLowpass (g1, zi);
            return lp + gain * (1.0 - lp);
        }
    }

    // Magnitude (dB) of the wet HPF + LPF exactly as implemented, at `hz`.
    inline double wetFilterResponseDb (double hz, double hpfHz, double lpfHz, double sampleRate)
    {
        using namespace wetfilter;
        const auto zi = zInv (hz, sampleRate);
        const auto h = svfHighpass (integratorGain (hpfHz, sampleRate), butterworthK, zi)
                     * svfLowpass (integratorGain (lpfHz, sampleRate), butterworthK, zi);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    // Full wet tone (HPF + LPF + the mode's high shelf: GHOST is darker).
    // Identical to the four-argument version in TIGHT and WIDE.
    inline double wetFilterResponseDb (double hz, double hpfHz, double lpfHz, double sampleRate, int mode)
    {
        using namespace wetfilter;
        const double gain = shelfGain (mode);
        if (same (gain, 1.0))
            return wetFilterResponseDb (hz, hpfHz, lpfHz, sampleRate);
        const auto zi = zInv (hz, sampleRate);
        const auto h = svfHighpass (integratorGain (hpfHz, sampleRate), butterworthK, zi)
                     * svfLowpass (integratorGain (lpfHz, sampleRate), butterworthK, zi)
                     * shelf (integratorGain (templates::ghostShelfHz, sampleRate), gain, zi);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    // The running filter (mono, double precision). Cutoffs glide in the log
    // domain; coefficients are recomputed per control block while gliding and
    // are bit-identical to integratorGain() of the target once settled.
    class WetFilter
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate;
            // one-pole glide per control block of `controlInterval` samples, ~20 ms
            glide = 1.0 - std::exp (-(double) controlInterval / (0.02 * fs));
            shelfStep = 1.0 / (0.04 * fs);
            reset();
        }

        void reset() noexcept
        {
            hp1 = hp2 = lp1 = lp2 = sh = 0.0;
            curHpf = targetHpf;
            curLpf = targetLpf;
            curShelf = targetShelf;
            updateCoefficients();
        }

        static constexpr int controlInterval = 32;

        void setTargets (double hpfHz, double lpfHz, double shelfGainLinear) noexcept
        {
            targetHpf = hpfHz;
            targetLpf = lpfHz;
            targetShelf = shelfGainLinear;
        }

        // Called once per control block, before process().
        void control() noexcept
        {
            bool changed = false;
            changed |= glideLog (curHpf, targetHpf);
            changed |= glideLog (curLpf, targetLpf);
            if (changed)
                updateCoefficients();
        }

        void process (const float* in, float* out, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                double x = (double) in[i];

                // HPF (TPT SVF, high-pass output)
                {
                    const double hp = (x - (gh + kh) * hp1 - hp2) * ah;
                    const double v1 = gh * hp;
                    const double bp = v1 + hp1;
                    hp1 = bp + v1;
                    const double v2 = gh * bp;
                    hp2 = hp2 + 2.0 * v2;
                    x = hp;
                }
                // LPF (TPT SVF, low-pass output)
                {
                    const double hp = (x - (gl + kl) * lp1 - lp2) * al;
                    const double v1 = gl * hp;
                    const double bp = v1 + lp1;
                    lp1 = bp + v1;
                    const double v2 = gl * bp;
                    const double lp = lp2 + v2;
                    lp2 = lp + v2;
                    x = lp;
                }
                // GHOST high shelf: y = LP1 + G (x - LP1); G glides linearly
                if (! wetfilter::same (curShelf, targetShelf))
                    curShelf = curShelf < targetShelf ? std::min (targetShelf, curShelf + shelfStep)
                                                      : std::max (targetShelf, curShelf - shelfStep);
                const double v = (x - sh) * gs1;
                const double lp = v + sh;
                sh = lp + v;
                if (! wetfilter::same (curShelf, 1.0))
                    x = lp + curShelf * (x - lp);

                out[i] = (float) x;
            }
        }

        double getSampleRate() const noexcept { return fs; }
        double getCurrentHpf() const noexcept { return curHpf; }
        double getCurrentLpf() const noexcept { return curLpf; }

    private:
        bool glideLog (double& cur, double target) noexcept
        {
            if (wetfilter::same (cur, target))
                return false;
            const double next = cur * std::pow (target / cur, glide);
            cur = std::abs (next / target - 1.0) < 1.0e-6 ? target : next;
            return true;
        }

        void updateCoefficients() noexcept
        {
            gh = wetfilter::integratorGain (curHpf, fs);
            gl = wetfilter::integratorGain (curLpf, fs);
            kh = kl = wetfilter::butterworthK;
            ah = 1.0 / (1.0 + gh * (gh + kh));
            al = 1.0 / (1.0 + gl * (gl + kl));
            const double g1 = wetfilter::integratorGain (templates::ghostShelfHz, fs);
            gs1 = g1 / (1.0 + g1);
        }

        double fs = 48000.0, glide = 0.1, shelfStep = 0.001;
        double targetHpf = 20.0, targetLpf = 20000.0, targetShelf = 1.0;
        double curHpf = 20.0, curLpf = 20000.0, curShelf = 1.0;
        double gh = 0, gl = 0, kh = wetfilter::butterworthK, kl = wetfilter::butterworthK, ah = 1, al = 1, gs1 = 0;
        double hp1 = 0, hp2 = 0, lp1 = 0, lp2 = 0, sh = 0;
    };
}
