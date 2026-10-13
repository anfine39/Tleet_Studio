#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

// Transport modulation (wow, flutter, the dust's irregular scrape), tape hiss
// and dust dropouts (MODEL_SPEC 7). All random sources are seeded at reset,
// so a render is deterministic (state round trip, golden renders).
namespace tleet::echo
{
    // xorshift64* (Vigna): fast, good enough for audio noise.
    struct Rng
    {
        explicit Rng (std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : s (seed ? seed : 1) {}
        void seed (std::uint64_t v) noexcept { s = v ? v : 1; }
        // uniform in [-1, 1)
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

    // Lowpassed white noise with unit RMS (one-pole, coefficient G).
    struct SmoothNoise
    {
        void prepare (double cutoffHz, double rate) noexcept
        {
            const double g = std::tan (3.14159265358979323846 * std::min (cutoffHz, 0.45 * rate) / rate);
            G = g / (1.0 + g);
            // output variance of a TPT one-pole driven by unit-variance white noise: G / (2 - G) (approx. for small G)
            norm = std::sqrt ((2.0 - G) / G) * std::sqrt (3.0); // uniform [-1,1) has variance 1/3
            state = 0.0;
        }
        double next (Rng& rng) noexcept
        {
            const double v = (rng.uniform() - state) * G;
            const double lp = v + state;
            state = lp + v;
            return lp * norm;
        }
        double G = 0.1, norm = 1.0, state = 0.0;
    };

    namespace mod
    {
        inline constexpr double wowHz = 0.55;        // capstan / reel eccentricity class
        inline constexpr double flutterHz = 6.5;     // capstan / pinch-roller class
        inline constexpr double flutter2Hz = 11.3;   // second flutter line
        inline constexpr double flutter2Share = 0.35;
        inline constexpr double wowDriftShare = 0.35;     // random wow drift (RMS) per unit of the wow line
        inline constexpr double flutterRandShare = 0.3;   // random flutter (RMS) per unit of the flutter line
        inline constexpr double wowMax = 0.006;      // WOW 100 % = 0.6 % peak speed deviation (main line)
        inline constexpr double flutterMax = 0.002;  // FLUTTER 100 % = 0.2 % peak (main line)
    }

    // Speed deviation m(t) of the virtual tape (fraction of nominal speed),
    // evaluated once per control block of the tape transport.
    class WowFlutter
    {
    public:
        void prepare (double controlRate) noexcept
        {
            rate = controlRate;
            drift.prepare (0.4, rate);
            fast.prepare (12.0, rate);
            irrLo.prepare (3.0, rate);
            irrHi.prepare (25.0, rate);
            reset();
        }

        void reset() noexcept
        {
            rng.seed (0x51ED2701u);
            ph1 = 0.0; ph2 = 0.37; ph3 = 0.71;
            drift.state = fast.state = irrLo.state = irrHi.state = 0.0;
        }

        // wowA, flutterA: peak amplitudes of the main lines; irregular: RMS of the scrape component.
        double next (double wowA, double flutterA, double irregular) noexcept
        {
            constexpr double twoPi = 6.283185307179586;
            ph1 += mod::wowHz / rate;      ph1 -= std::floor (ph1);
            ph2 += mod::flutterHz / rate;  ph2 -= std::floor (ph2);
            ph3 += mod::flutter2Hz / rate; ph3 -= std::floor (ph3);
            const double d = drift.next (rng), f = fast.next (rng);
            const double irr = irrHi.next (rng) - irrLo.next (rng);
            const double m = wowA * (std::sin (twoPi * ph1) + mod::wowDriftShare * d)
                           + flutterA * (std::sin (twoPi * ph2) + mod::flutter2Share * std::sin (twoPi * ph3) + mod::flutterRandShare * f)
                           + irregular * irr * 0.7;
            return std::clamp (m, -0.2, 0.2);
        }

    private:
        Rng rng;
        SmoothNoise drift, fast, irrLo, irrHi;
        double rate = 1500.0, ph1 = 0.0, ph2 = 0.0, ph3 = 0.0;
    };

    // Dust dropouts: short level dips where a slow random process exceeds a threshold.
    class Dropouts
    {
    public:
        void prepare (double controlRate) noexcept { src.prepare (6.0, controlRate); reset(); }
        void reset() noexcept { rng.seed (0xD0570u); src.state = 0.0; }
        // returns a gain (linear) for this control block
        double next (double depthDb) noexcept
        {
            const double u = src.next (rng);
            const double s = std::clamp ((u - 1.2) / 1.0, 0.0, 1.0);
            return depthDb > 0.0 ? std::pow (10.0, -depthDb * s * s * (3.0 - 2.0 * s) / 20.0) : 1.0;
        }
    private:
        Rng rng;
        SmoothNoise src;
    };

    // Tape hiss: white noise with a gentle high-frequency tilt (+ ~4 dB above
    // 2 kHz), unit RMS before the level. Recorded onto the tape, so the heads,
    // the head loss and the loop shape it as they shape the signal.
    class TapeHiss
    {
    public:
        void prepare (double fs) noexcept
        {
            const double g = std::tan (3.14159265358979323846 * std::min (2000.0, 0.45 * fs) / fs);
            G = g / (1.0 + g);
            reset();
        }
        void reset() noexcept { rng.seed (0x7A9E5u); state = 0.0; }
        double next() noexcept
        {
            const double w = rng.uniform() * 1.7320508075688772; // unit RMS
            const double v = (w - state) * G;
            const double lp = v + state;
            state = lp + v;
            return 0.75 * (w + 0.6 * (w - lp));
        }
    private:
        Rng rng;
        double G = 0.1, state = 0.0;
    };
}
