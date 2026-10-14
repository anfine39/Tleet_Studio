#pragma once

#include "TapeModel.h"
#include <vector>
#include <cmath>
#include <algorithm>

// Wavelength losses of the tape path relative to the aligned machine
// (MODEL_SPEC 3.5), as Chowdhury realises them (R8): a linear-phase FIR by
// frequency sampling. The losses are real functions of the wavenumber
// k = 2 pi f / v (R3, R8 eq. 13):
//
//   gap      sin(k g / 2) / (k g / 2)          repro gap g (not equalised: it sets the top end)
//   spacing  exp(-k d_age)                      head wear / debris above the aligned spacing (AGE)
//   bias     exp(-k d_rec(I))                   recording-zone depth vs bias current (BIAS)
//
// The aligned machine's own spacing and thickness losses are cancelled by its
// alignment EQ, so they do not appear. Zero phase in space is linear phase in
// time: the FIR is symmetric, its delay (N - 1) / 2 samples is reported.
// N = 33 at 44.1 / 48 kHz, 65 at 88.2 / 96 kHz, 129 at 176.4 / 192 kHz
// (0.33-0.36 ms at every rate).
namespace tleet::tape
{
    struct LossParams
    {
        double ips = 15.0;
        double spacing = 0.0;  // metres above the aligned spacing
        double biasZone = 0.0; // metres (negative = under-biased)

        bool operator== (const LossParams& o) const noexcept { return ips == o.ips && spacing == o.spacing && biasZone == o.biasZone; }
    };

    inline double lossMagnitude (const LossParams& p, double hz) noexcept
    {
        const double v = model::speedMetres (p.ips);
        const double k = 2.0 * model::pi * hz / v;
        const double x = 0.5 * k * model::reproGapMetres;
        const double gap = x < 1.0e-9 ? 1.0 : std::sin (x) / x;
        return gap * std::exp (-k * (p.spacing + p.biasZone));
    }

    class LossFirDesigner
    {
    public:
        static int tapsForRate (double fs) noexcept { return fs < 60000.0 ? 33 : (fs < 120000.0 ? 65 : 129); }

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            taps = tapsForRate (fs);
            centre = (taps - 1) / 2;
            bins = 4 * (taps - 1); // frequency grid (fs / bins resolution)
            cosTable.resize ((size_t) bins);
            for (int i = 0; i < bins; ++i)
                cosTable[(size_t) i] = std::cos (2.0 * model::pi * i / bins);
            window.resize ((size_t) taps);
            for (int n = 0; n < taps; ++n) // Hann over N + 1 intervals (no zero end taps)
                window[(size_t) n] = 0.5 - 0.5 * std::cos (2.0 * model::pi * (n + 1) / (taps + 1));
            mag.resize ((size_t) (bins / 2 + 1));
        }

        int numTaps() const noexcept { return taps; }
        int latency() const noexcept { return centre; }

        // Zero-phase frequency sampling, Hann window, DC normalised to exactly 1.
        // Allocation-free after prepare(). out holds numTaps() values.
        void design (const LossParams& p, float* out) noexcept
        {
            const int half = bins / 2;
            for (int b = 0; b <= half; ++b)
                mag[(size_t) b] = lossMagnitude (p, fs * b / bins);
            double sum = 0.0;
            double tmp[129];
            for (int n = 0; n <= centre; ++n)
            {
                double acc = mag[0] + mag[(size_t) half] * ((n % 2) == 0 ? 1.0 : -1.0); // DC and Nyquist bins
                for (int b = 1; b < half; ++b)
                    acc += 2.0 * mag[(size_t) b] * cosTable[(size_t) ((b * n) % bins)];
                tmp[n] = acc / bins * window[(size_t) (centre + n)];
            }
            for (int n = -centre; n <= centre; ++n)
                sum += tmp[std::abs (n)];
            for (int n = 0; n < taps; ++n)
                out[n] = (float) (tmp[std::abs (n - centre)] / sum);
        }

    private:
        double fs = 48000.0;
        int taps = 33, centre = 16, bins = 128;
        std::vector<double> cosTable, window, mag;
    };
}
