#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Building blocks of the VOID networks (MODEL_SPEC 4). Nothing here allocates
// after prepare: delay memory is carved out of an Arena that is sized once in
// prepareToPlay; the audio thread only moves a bump pointer.
namespace tleet::voidrev
{
    // ---- memory -----------------------------------------------------------------

    // A preallocated block of floats handed out to delay lines. reset() starts a
    // new configuration (the memory is reused, not freed). `used()` floats from
    // the start were handed out; clearing that region zeroes every line.
    class Arena
    {
    public:
        void allocate (size_t floats) { mem.assign (floats, 0.0f); top = 0; }
        void reset() noexcept { top = 0; }
        float* carve (size_t n) noexcept
        {
            if (top + n > mem.size())
                return nullptr; // sized by the designs' own memory count; cannot happen
            float* p = mem.data() + top;
            top += n;
            return p;
        }
        size_t used() const noexcept { return top; }
        size_t capacity() const noexcept { return mem.size(); }
        float* data() noexcept { return mem.data(); }

    private:
        std::vector<float> mem;
        size_t top = 0;
    };

    // Counts floats instead of carving them: the memory a design needs.
    struct ArenaCounter
    {
        size_t total = 0;
        void add (size_t n) noexcept { total += n; }
    };

    // ---- delay line ---------------------------------------------------------------

    // Circular float buffer. Convention: in a sample, every read happens before
    // the write, so read(d) with d >= 1 returns the sample written d samples ago.
    struct DelayLine
    {
        float* buf = nullptr;
        int size = 1, w = 0;

        void attach (float* p, int n) noexcept { buf = p; size = std::max (1, n); w = 0; }
        void write (double x) noexcept
        {
            buf[w] = (float) x;
            w = w + 1 == size ? 0 : w + 1;
        }
        double read (int d) const noexcept
        {
            int i = w - d;
            if (i < 0) i += size;
            return buf[i];
        }
        // 4-point third-order Lagrange read for d >= 2 (exact at integer d).
        double readCubic (double d) const noexcept
        {
            const int m = (int) d;
            const double f = d - m;
            const double xm1 = read (m - 1), x0 = read (m), x1 = read (m + 1), x2 = read (m + 2);
            const double fm1 = f - 1.0, fm2 = f - 2.0, fp1 = f + 1.0;
            return -xm1 * f * fm1 * fm2 * (1.0 / 6.0) + x0 * fp1 * fm1 * fm2 * 0.5
                   - x1 * fp1 * f * fm2 * 0.5 + x2 * fp1 * f * fm1 * (1.0 / 6.0);
        }
    };

    // First-order allpass interpolated read (Laakso et al. 1996; Dattorro 1997):
    //   y[n] = eta x[n-M] + x[n-M-1] - eta y[n-1],  eta = (1 - f) / (1 + f)
    // for a delay M + f. The fractional part is kept in [0.5, 1.5) so that
    // |eta| <= 1/3: the interpolator is lossless (|H| = 1 at every frequency),
    // which keeps the decay calibration and a frozen loop exact. Needs d >= 1.5.
    //
    // When a moving delay crosses an integer, M changes and the recursion's
    // state belongs to the old filter: the classic allpass-interpolation
    // transient. The state is re-derived for the new M by running the new
    // filter over the last `warmUp` input samples, which the delay line still
    // holds (Valimaki & Laakso 1998, transient elimination by state update);
    // the start-up error decays as |eta|^warmUp <= 3^-5.
    struct AllpassRead
    {
        static constexpr int warmUp = 5;
        double y1 = 0.0;
        int mPrev = -1;
        void reset() noexcept { y1 = 0.0; mPrev = -1; }
        double read (const DelayLine& line, double d) noexcept
        {
            int m = (int) d;
            double f = d - m;
            if (f < 0.5) { --m; f += 1.0; }
            const double eta = (1.0 - f) / (1.0 + f);
            if (m != mPrev)
            {
                if (mPrev >= 0 && m + warmUp + 2 < line.size)
                {
                    double y = line.read (m + warmUp + 1);
                    for (int k = warmUp; k >= 1; --k)
                        y = eta * (line.read (m + k) - y) + line.read (m + k + 1);
                    y1 = y;
                }
                mPrev = m;
            }
            const double y = eta * (line.read (m) - y1) + line.read (m + 1);
            y1 = y;
            return y;
        }
    };

    // Schroeder allpass with a (possibly fractional, moving) delay d:
    //   w[n] = x[n] + g u[n],  y[n] = -g w[n] + u[n],  u[n] = loss * w[n-d]
    // loss = 1: lossless for |g| < 1. loss = gamma^d applies the decay of the
    // time spent inside the allpass (Jot's substitution z^-1 -> gamma z^-1,
    // MODEL_SPEC 5.1), so the response is the lossless one times gamma^n.
    struct SchroederAllpass
    {
        DelayLine line;
        AllpassRead rd;
        void reset() noexcept { rd.reset(); }
        double process (double x, double d, double g, double loss = 1.0) noexcept
        {
            const double u = loss * rd.read (line, d);
            const double w = x + g * u;
            line.write (w);
            return u - g * w;
        }
        // fixed integer delay (feed-forward diffusers that never move)
        double processInt (double x, int d, double g, double loss = 1.0) noexcept
        {
            const double u = loss * line.read (d);
            const double w = x + g * u;
            line.write (w);
            return u - g * w;
        }
    };

    // ---- control glides ---------------------------------------------------------

    // Two-stage (critically damped) exponential glide evaluated per control
    // block, linear inside the block (as ECHO's).
    struct Glide
    {
        double target = 0.0, mid = 0.0, end = 0.0, cur = 0.0, step = 0.0;
        int k = 32;
        void snap (double v) noexcept { target = mid = end = cur = v; step = 0.0; }
        void block (double coeff, int samples) noexcept
        {
            k = samples;
            cur = end;
            mid += coeff * (target - mid);
            double e = end + coeff * (mid - end);
            const double tol = 1.0e-9 * std::max (1.0, std::abs (target));
            if (std::abs (e - target) < tol && std::abs (mid - target) < tol)
                e = mid = target;
            step = (e - cur) / samples;
            end = e;
        }
        double next() noexcept { cur += step; return cur; }
    };

    // Exact floating-point equality without the compiler's -Wfloat-equal noise
    // (used where a value is deliberately snapped to a target).
    inline bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    inline double blockCoeff (double seconds, double controlRate) noexcept
    {
        return -std::expm1 (-1.0 / std::max (1.0e-9, seconds * controlRate));
    }

    // ---- modulation sources --------------------------------------------------------

    // xorshift64* (Vigna): deterministic noise, seeded at reset.
    struct Rng
    {
        explicit Rng (std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : s (seed ? seed : 1) {}
        void seed (std::uint64_t v) noexcept { s = v ? v : 1; }
        double uniform() noexcept // [-1, 1)
        {
            s ^= s >> 12;
            s ^= s << 25;
            s ^= s >> 27;
            const std::uint64_t r = s * 0x2545F4914F6CDD1Dull;
            return (double) (r >> 11) * (2.0 / 9007199254740992.0) - 1.0;
        }
        std::uint64_t s;
    };

    // Band-limited random wander with unit RMS: white noise through two
    // one-poles at the control rate (cutoff set per block).
    struct Wander
    {
        Rng rng;
        double s1 = 0.0, s2 = 0.0;
        void reset (std::uint64_t seed) noexcept { rng.seed (seed); s1 = s2 = 0.0; }
        // G: one-pole coefficient (g / (1 + g)); norm: unit-RMS normalisation
        double next (double G, double norm) noexcept
        {
            const double x = rng.uniform() * 1.7320508075688772;
            double v = (x - s1) * G;
            const double a = v + s1;
            s1 = a + v;
            v = (a - s2) * G;
            const double b = v + s2;
            s2 = b + v;
            return b * norm;
        }
        // A TPT one-pole passes a variance G of unit white noise; two in cascade
        // pass about G / 2 for small G (half the equivalent noise bandwidth).
        static double normFor (double G) noexcept { return 1.0 / std::sqrt (std::max (1.0e-12, 0.5 * G)); }
    };
}
