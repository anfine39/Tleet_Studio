#pragma once

#include <cmath>
#include <complex>
#include <algorithm>

// Detector-path filters of GLUE (MODEL_SPEC.md section 4): the SC HPF
// (12 dB/oct Butterworth high-pass, 20..300 Hz) followed by the DARK
// emphasis (first-order high shelf, 0..+12 dB). They only ever
// filter the side-chain: the audio path never passes through them.
//
// Implementation: topology-preserving-transform (TPT) filters, bilinear
// transform with pre-warped cutoff, double precision, running at the model
// rate (2x the host rate). detectorResponseDb() evaluates the discrete-time
// transfer function of these same structures from the same coefficients, so
// a curve drawn from it is the filter that runs.
namespace tleet::glue
{
    namespace detector
    {
        inline constexpr double pi = 3.14159265358979323846;
        inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)
        inline constexpr double maxCutoffShare = 0.45;             // of the sample rate
        inline constexpr double darkShelfHz = 2000.0;              // pole of the DARK shelf's low-pass
        inline constexpr double darkMaxDb = 12.0;                  // DARK 100 % = +12 dB above the shelf

        // exact comparison without -Wfloat-equal noise (glide end points are exact by construction)
        inline constexpr bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

        // g = tan(pi fc / fs): the pre-warped integrator gain shared by the DSP and the response.
        inline double integratorGain (double cutoffHz, double sampleRate) noexcept
        {
            const double fc = std::clamp (cutoffHz, 1.0, maxCutoffShare * sampleRate);
            return std::tan (pi * fc / sampleRate);
        }

        // DARK amount (0..1) -> linear shelf gain
        inline double darkShelfGain (double amount) noexcept
        {
            return std::pow (10.0, std::clamp (amount, 0.0, 1.0) * darkMaxDb / 20.0);
        }

        inline std::complex<double> zInv (double hz, double sampleRate) noexcept
        {
            const double w = 2.0 * pi * hz / sampleRate;
            return { std::cos (w), -std::sin (w) };
        }

        // Second-order TPT SVF high-pass: (1 - z^-1)^2 / D(z),
        // D(z) = (1 + g k + g^2) + 2 (g^2 - 1) z^-1 + (1 - g k + g^2) z^-2
        inline std::complex<double> svfHighpass (double g, double k, std::complex<double> zi) noexcept
        {
            const auto d = (1.0 + g * k + g * g) + 2.0 * (g * g - 1.0) * zi + (1.0 - g * k + g * g) * zi * zi;
            return (1.0 - zi) * (1.0 - zi) / d;
        }

        // First-order TPT low-pass: g (1 + z^-1) / ((1 + g) + (g - 1) z^-1)
        inline std::complex<double> onePoleLowpass (double g, std::complex<double> zi) noexcept
        {
            return g * (1.0 + zi) / ((1.0 + g) + (g - 1.0) * zi);
        }

        // High shelf y = x + (G - 1) (x - LP1(x))
        inline std::complex<double> highShelf (double g1, double gain, std::complex<double> zi) noexcept
        {
            return 1.0 + (gain - 1.0) * (1.0 - onePoleLowpass (g1, zi));
        }
    }

    // Magnitude (dB) of the detector path filters exactly as implemented, at
    // `hz`, for a settled SC HPF cutoff and DARK amount (0..1). `sampleRate`
    // is the model rate (GlueProcessor::getDetectorSampleRate()).
    inline double detectorResponseDb (double hz, double hpfHz, double darkAmount, double sampleRate)
    {
        using namespace detector;
        const auto zi = zInv (hz, sampleRate);
        auto h = svfHighpass (integratorGain (hpfHz, sampleRate), butterworthK, zi);
        const double gain = darkShelfGain (darkAmount);
        if (! same (gain, 1.0))
            h *= highShelf (integratorGain (darkShelfHz, sampleRate), gain, zi);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    // Coefficients shared by every detector channel. The cutoff glides in the
    // log domain and the DARK gain linearly, once per control block; once
    // settled they are bit-identical to the values detectorResponseDb() uses.
    class DetectorFilterControl
    {
    public:
        static constexpr int controlInterval = 32; // samples at the model rate

        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate;
            const double blocks = 0.02 * fs / controlInterval; // ~20 ms glides
            glide = 1.0 - std::exp (-1.0 / std::max (1.0, blocks));
            linearStep = 1.0 / std::max (1.0, blocks);
            g1 = detector::integratorGain (detector::darkShelfHz, fs);
            g1s = g1 / (1.0 + g1);
            snap();
        }

        void setTargets (double hpfHz, double darkAmount) noexcept
        {
            targetHz = std::clamp (hpfHz, 1.0, detector::maxCutoffShare * fs);
            targetShelf = detector::darkShelfGain (darkAmount);
        }

        void snap() noexcept
        {
            curHz = targetHz;
            shelf = targetShelf;
            update();
        }

        // Called once per control block.
        void control() noexcept
        {
            bool changed = false;
            if (! detector::same (curHz, targetHz))
            {
                const double next = curHz * std::pow (targetHz / curHz, glide);
                curHz = std::abs (next / targetHz - 1.0) < 1.0e-6 ? targetHz : next;
                changed = true;
            }
            shelf = step (shelf, targetShelf, linearStep * (detector::darkShelfGain (1.0) - 1.0));
            if (changed)
                update();
        }

        double getSampleRate() const noexcept { return fs; }
        double getCurrentHz() const noexcept { return curHz; }
        double getCurrentShelfGain() const noexcept { return shelf; }

        // per-sample coefficients
        double g = 0, k = detector::butterworthK, a = 1, shelf = 1.0, g1 = 0, g1s = 0;

    private:
        static double step (double cur, double target, double size) noexcept
        {
            if (cur < target) return std::min (target, cur + size);
            if (cur > target) return std::max (target, cur - size);
            return cur;
        }

        void update() noexcept
        {
            g = detector::integratorGain (curHz, fs);
            a = 1.0 / (1.0 + g * (g + k));
        }

        double fs = 96000.0, glide = 0.1, linearStep = 0.02;
        double targetHz = 20.0, curHz = 20.0, targetShelf = 1.0;
    };

    // One detector channel's filter state.
    struct DetectorFilter
    {
        void reset() noexcept { s1 = s2 = sh = 0.0; }

        double process (double x, const DetectorFilterControl& c) noexcept
        {
            // SC HPF (TPT SVF, high-pass output)
            const double hp = (x - (c.g + c.k) * s1 - s2) * c.a;
            const double v1 = c.g * hp;
            const double bp = v1 + s1;
            s1 = bp + v1;
            const double v2 = c.g * bp;
            s2 += 2.0 * v2;
            x = hp;

            // DARK high shelf: y = x + (G - 1) (x - LP1)
            const double v = (x - sh) * c.g1s;
            const double lp = v + sh;
            sh = lp + v;
            if (! detector::same (c.shelf, 1.0))
                x += (c.shelf - 1.0) * (x - lp);
            return x;
        }

        double s1 = 0, s2 = 0, sh = 0;
    };
}
