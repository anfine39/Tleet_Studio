#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

// Linear filters of VEIL (MODEL_SPEC.md section 4).
//
//   HPF     (audio path, plugin input, before the input tube stage and the
//            internal detector tap): 12 dB/oct Butterworth, 20..300 Hz, Off at 20.
//   SC HPF  (detector only, internal and external key): 12 dB/oct
//            Butterworth, 20..300 Hz, Off at 20.
//   coupling high-pass (input transformer / key input), 1st order at 2 Hz.
//   LF weight (output transformer voicing, CHARACTER), 1st-order low shelf at 60 Hz.
//
// All are topology-preserving-transform (TPT) filters from the bilinear
// transform with pre-warped cutoffs, double precision. hpfResponseDb() and
// shelfResponseDb() evaluate the discrete-time transfer functions of these
// same structures from the same coefficients, so a curve drawn from them is
// the filter that runs.
namespace tleet::veil
{
    namespace filt
    {
        inline constexpr double pi = 3.14159265358979323846;
        inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)
        inline constexpr double offHz = 20.0;
        inline constexpr double maxCutoffShare = 0.45;
        inline constexpr double couplingHz = 2.0;
        inline constexpr double weightShelfHz = 60.0;

        inline bool isOff (double hz) noexcept { return hz <= offHz + 1.0e-3; }

        inline double integratorGain (double hz, double fs) noexcept
        {
            return std::tan (pi * std::clamp (hz, 0.1, maxCutoffShare * fs) / fs);
        }

        inline std::complex<double> zInv (double hz, double fs) noexcept
        {
            const double w = 2.0 * pi * hz / fs;
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

        inline double toDb (std::complex<double> h) noexcept { return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h))); }
    }

    // Magnitude (dB) of the 12 dB/oct high-pass (HPF or SC HPF) as implemented,
    // settled at `cutoffHz` (Off -> 0 dB everywhere), at sample rate `fs`.
    // The HPF runs at the host rate; the SC HPF at the detector rate (2x the
    // host rate, see VeilProcessor::getDetectorSampleRate()).
    inline double hpfResponseDb (double hz, double cutoffHz, double fs)
    {
        if (filt::isOff (cutoffHz))
            return 0.0;
        return filt::toDb (filt::svfHighpass (filt::integratorGain (cutoffHz, fs), filt::butterworthK, filt::zInv (hz, fs)));
    }

    // Magnitude (dB) of a first-order TPT low shelf of `shelfDb` below `cornerHz`.
    inline double shelfResponseDb (double hz, double shelfDb, double cornerHz, double fs)
    {
        const double gl = std::pow (10.0, shelfDb / 20.0);
        return filt::toDb (1.0 + (gl - 1.0) * filt::onePoleLowpass (filt::integratorGain (cornerHz, fs), filt::zInv (hz, fs)));
    }

    // Shared coefficients of a switchable 12 dB/oct high-pass. The cutoff
    // glides in the log domain (~20 ms) once per control block; switching
    // between Off and on crossfades over 20 ms; once faded out the filter is
    // not computed at all (bit-transparent) and its state is cleared.
    class HighPassControl
    {
    public:
        static constexpr int controlInterval = 16; // samples

        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate;
            glide = 1.0 - std::exp (-1.0 / std::max (1.0, 0.02 * fs / controlInterval));
            fadeStep = 1.0 / std::max (1.0, 0.02 * fs);
            snap();
        }

        void setTarget (double hz) noexcept { targetHz = hz; }

        void snap() noexcept
        {
            on = ! filt::isOff (targetHz);
            fade = on ? 1.0 : 0.0;
            curHz = on ? targetHz : 1000.0;
            countdown = 0;
            update();
        }

        // Per sample, before the channels' process(). Returns true when the
        // channel states must be cleared (the filter (re)starts from rest).
        bool advance() noexcept
        {
            bool restart = false;
            on = ! filt::isOff (targetHz);
            if (on && fade <= 0.0)
            {
                curHz = targetHz; // fade in at the chosen cutoff
                update();
                restart = true;
            }
            fade = on ? std::min (1.0, fade + fadeStep) : std::max (0.0, fade - fadeStep);
            if (--countdown <= 0)
            {
                countdown = controlInterval;
                if (on && (curHz < targetHz || curHz > targetHz))
                {
                    const double next = curHz * std::pow (targetHz / curHz, glide);
                    curHz = std::abs (next / targetHz - 1.0) < 1.0e-6 ? targetHz : next;
                    update();
                }
            }
            return restart;
        }

        bool isActive() const noexcept { return fade > 0.0; }
        double getFade() const noexcept { return fade; }
        double getCurrentHz() const noexcept { return isActive() ? curHz : filt::offHz; }
        double getSampleRate() const noexcept { return fs; }

        double g = 0.0, k = filt::butterworthK, a = 1.0, fade = 0.0;

    private:
        void update() noexcept
        {
            g = filt::integratorGain (curHz, fs);
            a = 1.0 / (1.0 + g * (g + k));
        }

        double fs = 48000.0, glide = 0.1, fadeStep = 0.001;
        double targetHz = filt::offHz, curHz = 1000.0;
        bool on = false;
        int countdown = 0;
    };

    // One channel of a switchable high-pass.
    struct HighPassState
    {
        void reset() noexcept { s1 = s2 = 0.0; }

        double process (double x, const HighPassControl& c) noexcept
        {
            if (! c.isActive())
                return x;
            const double hp = (x - (c.g + c.k) * s1 - s2) * c.a;
            const double v1 = c.g * hp;
            const double bp = v1 + s1;
            s1 = bp + v1;
            const double v2 = c.g * bp;
            s2 += 2.0 * v2;
            return c.fade >= 1.0 ? hp : x + c.fade * (hp - x);
        }

        double s1 = 0.0, s2 = 0.0;
    };

    // First-order TPT one-pole low-pass state (used for the coupling high-pass
    // y = x - LP(x), the LF weight shelf and the even-order DC blockers).
    struct OnePole
    {
        static double coeff (double hz, double fs) noexcept
        {
            const double g = filt::integratorGain (hz, fs);
            return g / (1.0 + g);
        }

        double lowpass (double x, double c) noexcept
        {
            const double v = (x - s) * c;
            const double lp = v + s;
            s = lp + v;
            return lp;
        }

        double highpass (double x, double c) noexcept { return x - lowpass (x, c); }
        void reset() noexcept { s = 0.0; }
        double s = 0.0;
    };
}
