#pragma once

#include "TapeModel.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Tape transport (MODEL_SPEC 6): speed deviation -> time warp, and the noise
// sources. All random sources are seeded at reset: renders are deterministic.
namespace tleet::tape
{
    // xorshift64* (Vigna): uniform in [-1, 1).
    struct Rng
    {
        explicit Rng (std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : s (seed ? seed : 1) {}
        void seed (std::uint64_t v) noexcept { s = v ? v : 1; }
        double uniform() noexcept
        {
            s ^= s >> 12;
            s ^= s << 25;
            s ^= s >> 27;
            const std::uint64_t r = s * 0x2545F4914F6CDD1Dull;
            return (double) (r >> 11) * (2.0 / 9007199254740992.0) - 1.0;
        }
        std::uint64_t s;
    };

    // Band-pass filtered white noise with unit RMS (TPT SVF band output). For a
    // band-pass with peak gain 1 the noise bandwidth is (pi / 2) f0 / Q, so white
    // uniform noise (variance 1/3, density 2/3 / rate) needs sqrt(3 Q rate / (pi f0)).
    struct BandNoise
    {
        void prepare (double f0, double q, double rate) noexcept
        {
            const double g = std::tan (model::pi * std::min (f0, 0.45 * rate) / rate);
            k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            norm = k * std::sqrt (3.0 * q * rate / (model::pi * f0)); // k: the SVF band output peaks at 1/k
            ic1 = ic2 = 0.0;
        }
        double next (Rng& rng) noexcept
        {
            const double v0 = rng.uniform();
            const double v3 = v0 - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return v1 * norm;
        }
        double k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, norm = 1.0, ic1 = 0.0, ic2 = 0.0;
    };

    struct TransportSettings
    {
        double ips = 15.0;
        double wow = 0.0, flutter = 0.0, age = 0.0; // 0..1
    };

    // Time warp of the transport: delay deviation tau(t) (seconds, relative to the
    // centre delay) at a control rate. A periodic speed deviation m(t) = A sin(2 pi f t)
    // plays back as a delay deviation -(A / (2 pi f)) cos(2 pi f t) (tau' = -m).
    // Random components are band-limited speed noise integrated by a leaky
    // integrator (bounded). The total is soft-limited to +-transportMaxDeviation.
    class WowFlutter
    {
    public:
        void prepare (double controlRate) noexcept
        {
            rate = controlRate;
            wowDrift.prepare (1.2, 0.7, rate);
            flutterRandom.prepare (12.0, 0.7, rate);
            ageWow.prepare (0.9, 0.8, rate);
            ageFlutter.prepare (9.0, 0.6, rate);
            leak = 1.0 - std::exp (-2.0 * model::pi * 0.25 / rate);
            reset();
        }

        void reset() noexcept
        {
            rng.seed (0x7A9E5F1Du);
            phSupply = 0.0; phTakeup = 0.31; phCapstan = 0.57; phRoller = 0.83;
            intDrift = intFlutter = intAgeW = intAgeF = 0.0;
            for (auto* b : { &wowDrift, &flutterRandom, &ageWow, &ageFlutter })
                b->ic1 = b->ic2 = 0.0;
            last = 0.0;
        }

        // Advances one control period. Returns the delay deviation in seconds.
        double next (const TransportSettings& s) noexcept
        {
            const double v = model::speedMetres (s.ips);
            const double fSup = v / (2.0 * model::pi * model::supplyRadius);
            const double fTak = v / (2.0 * model::pi * model::takeupRadius);
            const double fCap = v / (model::pi * model::capstanDiameter);
            const double fRol = v / (model::pi * model::rollerDiameter);
            const double dt = 1.0 / rate, twoPi = 2.0 * model::pi;
            auto adv = [dt] (double& ph, double f) { ph += f * dt; ph -= std::floor (ph); };
            adv (phSupply, fSup);
            adv (phTakeup, fTak);
            adv (phCapstan, fCap);
            adv (phRoller, fRol);

            const double fl = model::flutterSpeedScale (s.ips);
            const double aSup = model::wowSupplyMax * s.wow, aTak = model::wowTakeupMax * s.wow;
            const double aCap = (model::flutterCapstanMax * s.flutter + model::ageCapstanPeak * s.age) * fl;
            const double aRol = model::flutterRollerMax * s.flutter * fl;

            double tau = -(aSup / (twoPi * fSup)) * std::cos (twoPi * phSupply)
                         - (aTak / (twoPi * fTak)) * std::cos (twoPi * phTakeup)
                         - (aCap / (twoPi * fCap)) * std::cos (twoPi * phCapstan)
                         - (aRol / (twoPi * fRol)) * std::cos (twoPi * phRoller);

            // random speed components (unit-RMS band noise x RMS depth), integrated with a leak
            auto integrate = [this, dt] (double& state, double speed)
            {
                state += -speed * dt - leak * state;
                return state;
            };
            const double nDrift = wowDrift.next (rng), nFl = flutterRandom.next (rng);
            const double nAw = ageWow.next (rng), nAf = ageFlutter.next (rng);
            tau += integrate (intDrift, model::wowDriftRmsMax * s.wow * nDrift);
            tau += integrate (intFlutter, model::flutterRandomRmsMax * s.flutter * fl * nFl);
            tau += integrate (intAgeW, model::ageWowRms * s.age * nAw);
            tau += integrate (intAgeF, model::ageFlutterRms * s.age * fl * nAf);

            // soft limit (never reached by the specified ranges, see MODEL_SPEC 6.3)
            const double lim = model::transportMaxDeviation;
            last = lim * std::tanh (tau / lim);
            return last;
        }

        double lastDeviation() const noexcept { return last; }

    private:
        Rng rng;
        BandNoise wowDrift, flutterRandom, ageWow, ageFlutter;
        double rate = 3000.0, leak = 0.0;
        double phSupply = 0.0, phTakeup = 0.0, phCapstan = 0.0, phRoller = 0.0;
        double intDrift = 0.0, intFlutter = 0.0, intAgeW = 0.0, intAgeF = 0.0, last = 0.0;
    };

    // Fractional delay line at the top rate, 4-point Lagrange read. At the top
    // rate (>= 176.4 kHz) the audio band is below 0.12 of the rate, where the
    // cubic is flat within 0.01 dB; an integer delay reads the sample exactly.
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            size = 1;
            while (size < maxDelaySamples + 8)
                size <<= 1;
            mask = size - 1;
            buf.assign ((size_t) size, 0.0f);
            w = 0;
        }
        void reset() noexcept
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            w = 0;
        }
        void push (float x) noexcept
        {
            buf[(size_t) (w & mask)] = x;
            ++w;
        }
        // delay in samples (>= 1) behind the most recent push
        float read (double delay) const noexcept
        {
            const double fl = std::floor (delay);
            const double f = delay - fl;
            const int d = (int) fl;
            const std::int64_t base = w - 1 - d; // the sample `d` behind the newest
            const float xm1 = buf[(size_t) ((base + 1) & mask)];
            const float x0 = buf[(size_t) (base & mask)];
            if (exactly (f, 0.0))
                return x0;
            const float x1 = buf[(size_t) ((base - 1) & mask)];
            const float x2 = buf[(size_t) ((base - 2) & mask)];
            // Lagrange through t = -1, 0, 1, 2 (t = delay offset), evaluated at f
            const double c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
            const double c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
            const double c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
            const double c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
            return (float) (c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2);
        }

    private:
        std::vector<float> buf;
        std::int64_t w = 0, mask = 0, size = 1;
    };
}
