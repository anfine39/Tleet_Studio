#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <algorithm>

// Detector-path filters of WEIGHT (MODEL_SPEC.md section 4): the SC HPF
// (12 dB/oct Butterworth high-pass, 20..240 Hz) and the THRUST weighting
// (a +10 dB/decade tilt before the RMS detector, API's "inverse pink noise"
// curve). They only ever filter the side-chain: the audio never passes
// through them.
//
// Implementation: topology-preserving-transform (TPT) filters, bilinear
// transform, double precision, at the host rate. scHpfResponseDb(),
// thrustResponseDb() and detectorResponseDb() evaluate the discrete-time
// transfer functions of these same structures from the same coefficients, so
// a curve drawn from them is the filter that runs.
namespace tleet::weight
{
    namespace detector
    {
        inline constexpr double pi = 3.14159265358979323846;
        inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)
        inline constexpr double maxCutoffShare = 0.45;             // SC HPF cutoff limit (share of the rate)
        inline constexpr double maxZeroShare = 0.49;               // THRUST zero limit

        // exact comparison without -Wfloat-equal noise (glide end points are exact by construction)
        inline constexpr bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

        // g = tan(pi fc / fs): the pre-warped integrator gain shared by the DSP and the response.
        inline double integratorGain (double cutoffHz, double sampleRate) noexcept
        {
            const double fc = std::clamp (cutoffHz, 1.0, maxCutoffShare * sampleRate);
            return std::tan (pi * fc / sampleRate);
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

        // ---- THRUST (MODEL_SPEC 4.2)
        //
        // The +10 dB/decade (+3 dB/octave) slope is a half-order response; it
        // is approximated by first-order zero/pole pairs (each pair rises at
        // 20 dB/decade between its zero and its pole), log-spaced so that the
        // average slope is exactly 10 dB/decade:
        //   pair k of n over [fa, fb] (D = log10(fb/fa) decades):
        //   zero = fa 10^(D (k + 1/4) / n), pole = fa 10^(D (k + 3/4) / n).
        // Hard (API LOUD): one tilt over 10 Hz..40 kHz, 4 pairs, 0 dB at
        //   632.5 Hz = sqrt(20 Hz x 20 kHz), so -15 dB at 20 Hz, +15 dB at 20 kHz.
        // Neutral (API MED): the same slope below 200 Hz (10..200 Hz, 2 pairs)
        //   and above 3 kHz (3..40 kHz, 2 pairs), flat in between, 0 dB at
        //   774.6 Hz = sqrt(200 Hz x 3 kHz).
        // Clean (API NORM): no filter.
        inline constexpr int numThrustModes = 3;
        inline constexpr int thrustSections = 4;

        struct ThrustPair { double zeroHz, poleHz; };
        struct ThrustDesign { int numPairs; std::array<ThrustPair, thrustSections> pairs; double pivotHz; };

        inline ThrustPair tiltPair (double fa, double fb, int n, int k) noexcept
        {
            const double d = std::log10 (fb / fa);
            return { fa * std::pow (10.0, d * (k + 0.25) / n), fa * std::pow (10.0, d * (k + 0.75) / n) };
        }

        inline ThrustDesign thrustDesign (int mode) noexcept
        {
            switch (std::clamp (mode, 0, numThrustModes - 1))
            {
                case 2: return { 4, { tiltPair (10.0, 40000.0, 4, 0), tiltPair (10.0, 40000.0, 4, 1),
                                      tiltPair (10.0, 40000.0, 4, 2), tiltPair (10.0, 40000.0, 4, 3) },
                                 std::sqrt (20.0 * 20000.0) };
                case 1: return { 4, { tiltPair (10.0, 200.0, 2, 0), tiltPair (10.0, 200.0, 2, 1),
                                      tiltPair (3000.0, 40000.0, 2, 0), tiltPair (3000.0, 40000.0, 2, 1) },
                                 std::sqrt (200.0 * 3000.0) };
                default: return { 0, {}, 1000.0 };
            }
        }

        // Digital section y = x + (k - 1) HP1(x), HP1 a TPT one-pole high-pass
        // with integrator gain g. k is the analog pair's gain at Nyquist and
        // g = k tan(pi fz / fs) puts the zero exactly at fz (bilinear,
        // pre-warped): the digital pair matches the analog one at DC, at its
        // zero and at Nyquist (no over-boost when the pole is above Nyquist).
        struct ThrustSectionCoeffs { double g = 1.0, k = 1.0; };

        inline ThrustSectionCoeffs thrustSection (const ThrustPair& p, double sampleRate) noexcept
        {
            const double nyq = 0.5 * sampleRate;
            const double fz = std::min (p.zeroHz, maxZeroShare * sampleRate);
            const double k = std::sqrt ((1.0 + (nyq / p.zeroHz) * (nyq / p.zeroHz)) / (1.0 + (nyq / p.poleHz) * (nyq / p.poleHz)));
            return { k * std::tan (pi * fz / sampleRate), k };
        }

        inline std::complex<double> thrustSectionResponse (const ThrustSectionCoeffs& c, std::complex<double> zi) noexcept
        {
            return 1.0 + (c.k - 1.0) * (1.0 - onePoleLowpass (c.g, zi));
        }

        // Settled coefficients of one THRUST mode at a rate: the sections and
        // the gain that puts 0 dB at the pivot.
        struct ThrustCoeffs
        {
            std::array<ThrustSectionCoeffs, thrustSections> s {};
            double norm = 1.0;
        };

        inline ThrustCoeffs thrustCoeffs (int mode, double sampleRate) noexcept
        {
            const auto d = thrustDesign (mode);
            ThrustCoeffs c;
            for (int i = 0; i < d.numPairs; ++i)
                c.s[(size_t) i] = thrustSection (d.pairs[(size_t) i], sampleRate);
            if (d.numPairs > 0)
            {
                std::complex<double> h = 1.0;
                const auto zi = zInv (d.pivotHz, sampleRate);
                for (const auto& sc : c.s)
                    h *= thrustSectionResponse (sc, zi);
                c.norm = 1.0 / std::abs (h);
            }
            return c;
        }

        inline std::complex<double> thrustResponse (const ThrustCoeffs& c, std::complex<double> zi) noexcept
        {
            std::complex<double> h = c.norm;
            for (const auto& sc : c.s)
                if (! same (sc.k, 1.0))
                    h *= thrustSectionResponse (sc, zi);
            return h;
        }

        // The documented ideal THRUST curves (dB), for comparison only.
        inline double thrustIdealDb (double hz, int mode) noexcept
        {
            switch (std::clamp (mode, 0, numThrustModes - 1))
            {
                case 2: return 10.0 * std::log10 (hz / std::sqrt (20.0 * 20000.0));
                case 1: return hz < 200.0 ? 10.0 * std::log10 (hz / 200.0) : hz > 3000.0 ? 10.0 * std::log10 (hz / 3000.0) : 0.0;
                default: return 0.0;
            }
        }
    }

    inline double toDb (std::complex<double> h) noexcept { return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h))); }

    // Magnitude (dB) of the SC HPF as implemented, settled at `hpfHz`.
    inline double scHpfResponseDb (double hz, double hpfHz, double sampleRate)
    {
        using namespace detector;
        return toDb (svfHighpass (integratorGain (hpfHz, sampleRate), butterworthK, zInv (hz, sampleRate)));
    }

    // Magnitude (dB) of the THRUST weighting as implemented. mode: 0 Clean
    // (API NORM), 1 Neutral (API MED), 2 Hard (API LOUD).
    inline double thrustResponseDb (double hz, int mode, double sampleRate)
    {
        using namespace detector;
        return toDb (thrustResponse (thrustCoeffs (mode, sampleRate), zInv (hz, sampleRate)));
    }

    // The whole detector filter path (SC HPF then THRUST), in dB.
    inline double detectorResponseDb (double hz, double hpfHz, int thrustMode, double sampleRate)
    {
        return scHpfResponseDb (hz, hpfHz, sampleRate) + thrustResponseDb (hz, thrustMode, sampleRate);
    }

    // Coefficients shared by every detector channel. The SC HPF cutoff glides
    // in the log domain; the THRUST sections glide their (log g, log k) and
    // the pivot gain toward the selected mode, once per control block (a
    // section with k = 1 is the identity, so Clean is simply k -> 1). Once
    // settled they are bit-identical to the values the response functions use.
    class DetectorFilterControl
    {
    public:
        static constexpr int controlInterval = 32; // samples

        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate;
            const double blocks = 0.02 * fs / controlInterval; // ~20 ms glides
            glide = 1.0 - std::exp (-1.0 / std::max (1.0, blocks));
            for (int m = 0; m < detector::numThrustModes; ++m)
                modes[(size_t) m] = detector::thrustCoeffs (m, fs);
            snap();
        }

        void setTargets (double hpfHz, int thrustMode) noexcept
        {
            targetHz = std::clamp (hpfHz, 1.0, detector::maxCutoffShare * fs);
            targetMode = std::clamp (thrustMode, 0, detector::numThrustModes - 1);
        }

        void snap() noexcept
        {
            curHz = targetHz;
            updateHpf();
            thrust = target();
            updateThrust();
        }

        // Called once per control block.
        void control() noexcept
        {
            if (! detector::same (curHz, targetHz))
            {
                const double next = curHz * std::pow (targetHz / curHz, glide);
                curHz = std::abs (next / targetHz - 1.0) < 1.0e-6 ? targetHz : next;
                updateHpf();
            }

            const auto t = target();
            for (size_t i = 0; i < thrust.s.size(); ++i)
            {
                logGlide (thrust.s[i].k, t.s[i].k);
                // Clean leaves g where it is (k = 1 makes a section the identity whatever g).
                if (! detector::same (t.s[i].k, 1.0) || ! detector::same (thrust.s[i].k, 1.0))
                    logGlide (thrust.s[i].g, t.s[i].g);
            }
            logGlide (thrust.norm, t.norm);
            updateThrust();
        }

        double getSampleRate() const noexcept { return fs; }
        double getCurrentHz() const noexcept { return curHz; }
        const detector::ThrustCoeffs& getThrust() const noexcept { return thrust; }

        // per-sample coefficients
        double g = 0, k = detector::butterworthK, a = 1;
        detector::ThrustCoeffs thrust {};
        std::array<double, detector::thrustSections> gs {};
        bool thrustActive = false;

    private:
        const detector::ThrustCoeffs& target() const noexcept { return modes[(size_t) targetMode]; }

        static bool isActive (const detector::ThrustCoeffs& c) noexcept
        {
            bool active = ! detector::same (c.norm, 1.0);
            for (const auto& s : c.s)
                active = active || ! detector::same (s.k, 1.0);
            return active;
        }

        void logGlide (double& v, double t) const noexcept
        {
            if (detector::same (v, t))
                return;
            const double next = v * std::pow (t / v, glide);
            v = std::abs (next / t - 1.0) < 1.0e-6 ? t : next;
        }

        void updateThrust() noexcept
        {
            thrustActive = isActive (thrust);
            for (size_t i = 0; i < thrust.s.size(); ++i)
                gs[i] = thrust.s[i].g / (1.0 + thrust.s[i].g);
        }

        void updateHpf() noexcept
        {
            g = detector::integratorGain (curHz, fs);
            a = 1.0 / (1.0 + g * (g + k));
        }

        double fs = 48000.0, glide = 0.1;
        double targetHz = 20.0, curHz = 20.0;
        int targetMode = 0;
        std::array<detector::ThrustCoeffs, detector::numThrustModes> modes {};
    };

    // One detector feed's filter state (SC HPF then THRUST).
    struct DetectorFilter
    {
        void reset() noexcept
        {
            s1 = s2 = 0.0;
            th.fill (0.0);
        }

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

            // THRUST sections: y = x + (k - 1) (x - LP1(x)). The states keep
            // running while Clean so a switch back starts from a warm filter.
            for (size_t i = 0; i < th.size(); ++i)
            {
                const double v = (x - th[i]) * c.gs[i];
                const double lp = v + th[i];
                th[i] = lp + v;
                if (c.thrustActive)
                    x += (c.thrust.s[i].k - 1.0) * (x - lp);
            }
            return c.thrustActive ? x * c.thrust.norm : x;
        }

        // The states are linear in the input: a filter fed with x * g (g
        // steady) holds g times the states of one fed with x. Used to start a
        // detector feed warm when the topology switch brings it in.
        void copyScaled (const DetectorFilter& other, double scale) noexcept
        {
            s1 = other.s1 * scale;
            s2 = other.s2 * scale;
            for (size_t i = 0; i < th.size(); ++i)
                th[i] = other.th[i] * scale;
        }

        double s1 = 0, s2 = 0;
        std::array<double, detector::thrustSections> th {};
    };
}
