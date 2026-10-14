#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

// Linear filters of VOID, all topology-preserving-transform (TPT) structures
// in double precision (well behaved while their coefficients move). Every
// filter has a response function that evaluates the discrete transfer
// function of exactly that structure from the same coefficients, so the
// curves the tests and the editor compare against are the filters that run
// (addendum 12 / 26). Same structures as ECHO's (plugins/ECHO/dsp/Filters.h),
// kept local so the plugins stay independent.
namespace tleet::voidrev::filt
{
    inline constexpr double pi = 3.14159265358979323846;
    inline constexpr double butterworthK = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2)
    inline constexpr double maxCutoffShare = 0.45;              // of the sample rate

    using cplx = std::complex<double>;

    // g = tan(pi fc / fs), cutoff clamped below 0.45 fs.
    inline double integratorGain (double hz, double fs) noexcept
    {
        return std::tan (pi * std::clamp (hz, 1.0, maxCutoffShare * fs) / fs);
    }

    inline cplx zInv (double hz, double fs) noexcept
    {
        const double w = 2.0 * pi * hz / fs;
        return { std::cos (w), -std::sin (w) };
    }

    inline double toDb (cplx h) noexcept { return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h))); }

    // ---- second-order state-variable filter (Zavalishin TPT SVF)
    // D(z) = (1 + g k + g^2) + 2 (g^2 - 1) z^-1 + (1 - g k + g^2) z^-2
    inline cplx svfDen (double g, double k, cplx zi) noexcept
    {
        return (1.0 + g * k + g * g) + 2.0 * (g * g - 1.0) * zi + (1.0 - g * k + g * g) * zi * zi;
    }

    struct Svf
    {
        enum Type { lowpass, highpass };

        void reset() noexcept { s1 = s2 = 0.0; }
        void set (double gIn, double kIn) noexcept
        {
            g = gEnd = gIn;
            k = kIn;
            a = aEnd = 1.0 / (1.0 + g * (g + k));
            gStep = 0.0;
        }
        // Moves the coefficients linearly, sample by sample, from the previous
        // block-end values to the new ones over n samples (no coefficient steps
        // while a cutoff glides). An unchanged target holds exactly.
        void glideTo (double gIn, double kIn, int n) noexcept
        {
            k = kIn;
            const double aIn = 1.0 / (1.0 + gIn * (gIn + kIn));
            g = gEnd;
            a = aEnd;
            if ((! (gIn < gEnd) && ! (gIn > gEnd)) || n <= 0)
            {
                g = gEnd = gIn;
                a = aEnd = aIn;
                gStep = 0.0;
                return;
            }
            gStep = (gIn - gEnd) / n;
            gEnd = gIn;
            aEnd = aIn;
        }

        template <Type type>
        double process (double x) noexcept
        {
            if (gStep < 0.0 || gStep > 0.0) // exactly zero: not gliding
            {
                g += gStep;
                a = 1.0 / (1.0 + g * (g + k)); // exact for every g: the TPT SVF stays stable while it glides
            }
            const double hp = (x - (g + k) * s1 - s2) * a;
            const double v1 = g * hp;
            const double bp = v1 + s1;
            s1 = bp + v1;
            const double v2 = g * bp;
            const double lp = s2 + v2;
            s2 = lp + v2;
            return type == lowpass ? lp : hp;
        }

        cplx response (Type type, cplx zi) const noexcept
        {
            return type == lowpass ? g * g * (1.0 + zi) * (1.0 + zi) / svfDen (g, k, zi)
                                   : (1.0 - zi) * (1.0 - zi) / svfDen (g, k, zi);
        }

        double g = 0.1, k = butterworthK, a = 1.0, s1 = 0.0, s2 = 0.0;
        double gEnd = 0.1, aEnd = 1.0, gStep = 0.0;
    };

    // ---- open end stops of the user filters (MODEL_SPEC 6.1, owner decision D5)
    // HPF at its minimum (20 Hz) and LPF at its maximum (20 kHz) are fully open:
    // the filters are exactly (HPF) or numerically (LPF, < 1e-5 dB) transparent,
    // so a lossless (infinite) loop with the filters in LOOP mode holds its level.
    // In the last third of an octave before each stop the integrator gain moves
    // continuously from the Butterworth value at the knob frequency to the open
    // value (HPF: g -> 0, LPF: g -> 1000 x): no step anywhere on the knob.
    inline constexpr double hpfOpenHz = 20.0, hpfOpenZoneHz = 25.0;
    inline constexpr double lpfOpenHz = 20000.0, lpfOpenZoneHz = 16000.0;

    inline double openZone (double hz, double zoneHz, double stopHz) noexcept // 0 outside, 1 at the stop
    {
        const double u = std::clamp (std::log (hz / zoneHz) / std::log (stopHz / zoneHz), 0.0, 1.0);
        return u * u * (3.0 - 2.0 * u);
    }
    inline double hpfIntegratorGain (double hz, double fs) noexcept
    {
        return integratorGain (hz, fs) * (1.0 - openZone (hz, hpfOpenZoneHz, hpfOpenHz));
    }
    inline double lpfIntegratorGain (double hz, double fs) noexcept
    {
        return integratorGain (hz, fs) / std::max (1.0e-3, 1.0 - openZone (hz, lpfOpenZoneHz, lpfOpenHz));
    }

    // ---- the user HPF + LPF pair (12 dB/oct each, Butterworth)
    struct HpLp
    {
        void reset() noexcept { hp.reset(); lp.reset(); }
        void set (double hpfHz, double lpfHz, double fs) noexcept
        {
            hp.set (hpfIntegratorGain (hpfHz, fs), butterworthK);
            lp.set (lpfIntegratorGain (lpfHz, fs), butterworthK);
        }
        // glides the coefficients to the new cutoffs over n samples (see Svf::glideTo)
        void glideTo (double hpfHz, double lpfHz, double fs, int n) noexcept
        {
            hp.glideTo (hpfIntegratorGain (hpfHz, fs), butterworthK, n);
            lp.glideTo (lpfIntegratorGain (lpfHz, fs), butterworthK, n);
        }
        // copies the coefficients and their glide of another instance (the states stay)
        void copyCoefficients (const HpLp& o) noexcept
        {
            auto copy = [] (Svf& d, const Svf& src)
            {
                d.g = src.g; d.k = src.k; d.a = src.a;
                d.gEnd = src.gEnd; d.aEnd = src.aEnd; d.gStep = src.gStep;
            };
            copy (hp, o.hp);
            copy (lp, o.lp);
        }
        double process (double x) noexcept { return lp.process<Svf::lowpass> (hp.process<Svf::highpass> (x)); }
        cplx response (cplx zi) const noexcept { return hp.response (Svf::highpass, zi) * lp.response (Svf::lowpass, zi); }

        Svf hp, lp;
    };

    // Magnitude (dB) of the HPF + LPF exactly as run, at `hz`.
    inline double hpLpResponseDb (double hz, double hpfHz, double lpfHz, double fs) noexcept
    {
        HpLp f;
        f.set (hpfHz, lpfHz, fs);
        return toDb (f.response (zInv (hz, fs)));
    }

    // ---- 4th-order Butterworth lowpass (two SVFs, Q = 0.541 and 1.307)
    struct Lowpass4
    {
        void reset() noexcept { a.reset(); b.reset(); }
        void set (double hz, double fs) noexcept
        {
            const double g = integratorGain (hz, fs);
            a.set (g, 0.7653668647301796);
            b.set (g, 1.8477590650225735);
        }
        double process (double x) noexcept { return b.process<Svf::lowpass> (a.process<Svf::lowpass> (x)); }
        cplx response (cplx zi) const noexcept { return a.response (Svf::lowpass, zi) * b.response (Svf::lowpass, zi); }
        Svf a, b;
    };

    // ---- first-order TPT one-pole lowpass: LP = g (1 + z^-1) / ((1 + g) + (g - 1) z^-1)
    inline cplx onePoleLowpass (double g, cplx zi) noexcept { return g * (1.0 + zi) / ((1.0 + g) + (g - 1.0) * zi); }

    struct OnePole
    {
        void reset() noexcept { s = 0.0; }
        void set (double gIn) noexcept { g = gIn; G = gIn / (1.0 + gIn); }
        double lowpass (double x) noexcept
        {
            const double v = (x - s) * G;
            const double lp = v + s;
            s = lp + v;
            return lp;
        }
        double g = 0.1, G = 0.1, s = 0.0;
    };

    // ---- Jot absorption filter of one delay line (MODEL_SPEC 5.2):
    //   H(z) = gain * LS(z) * HS(z)
    //   LS: x + (A - 1) LP_lo(x)        DC gain A, Nyquist gain 1   (LP_lo: one-pole at loHz)
    //   HS: x + (B - 1) (x - LP_hi(x))  DC gain 1, Nyquist gain B   (LP_hi: one-pole at hiHz)
    // Exact gains at DC and Nyquist; `gain` is chosen so that |H(1 kHz)| is
    // exactly the mid-band per-pass gain (the DECAY reference).
    struct Absorption
    {
        void reset() noexcept { lo.reset(); hi.reset(); }
        double process (double x) noexcept
        {
            am1 += am1Step;
            bm1 += bm1Step;
            const double l = lo.lowpass (x);
            const double y = x + am1 * l;
            const double h = hi.lowpass (y);
            return gain * (y + bm1 * (y - h));
        }
        cplx shape (cplx zi) const noexcept
        {
            const cplx ls = 1.0 + am1 * onePoleLowpass (lo.g, zi);
            const cplx hs = 1.0 + bm1 * (1.0 - onePoleLowpass (hi.g, zi));
            return ls * hs;
        }
        cplx response (cplx zi) const noexcept { return gain * shape (zi); }

        // midGain: required per-pass gain at mid frequencies; dcRatio/nyqRatio:
        // DC and Nyquist gain relative to it. gLo/gHi: one-pole integrator gains
        // (tan(pi f/fs)); ziRef: z^-1 at the reference frequency.
        // passive: never exceed unity anywhere (absorption); false for the tone
        // correction, which may boost.
        void design (double midGain, double dcRatio, double nyqRatio, double gLo, double gHi, cplx ziRef, bool passive = true) noexcept
        {
            lo.g = gLo; lo.G = gLo / (1.0 + gLo);
            hi.g = gHi; hi.G = gHi / (1.0 + gHi);
            am1 = dcRatio - 1.0;
            bm1 = nyqRatio - 1.0;
            const double atRef = std::abs (shape (ziRef));
            gain = atRef > 0.0 ? midGain / atRef : midGain;
            // |LS| <= max(A, 1) and |HS| <= max(B, 1) at every frequency: a
            // passive line never exceeds unity
            const double peak = gain * std::max (dcRatio, 1.0) * std::max (nyqRatio, 1.0);
            if (passive && peak > 1.0)
                gain /= peak;
        }

        // After design(): move the shelf amounts from the previous block's values
        // to the new ones linearly over k samples (the caller ramps `gain`), so
        // a DECAY or COLOR change never steps the loop's filters (snap: jump).
        void rampShelves (int k, bool snap) noexcept
        {
            if (snap || k <= 0)
            {
                am1Step = bm1Step = 0.0;
            }
            else
            {
                am1Step = (am1 - am1End) / k;
                bm1Step = (bm1 - bm1End) / k;
                std::swap (am1, am1End);
                std::swap (bm1, bm1End);
                return;
            }
            am1End = am1;
            bm1End = bm1;
        }

        OnePole lo, hi;
        double am1 = 0.0, bm1 = 0.0, gain = 1.0;
        double am1End = 0.0, bm1End = 0.0, am1Step = 0.0, bm1Step = 0.0;
    };

    // ---- first-order shelves of the output tone correction (Jot): same
    // structure as Absorption, fixed gains, unity at mid frequencies.
    using ToneCorrection = Absorption;
}
