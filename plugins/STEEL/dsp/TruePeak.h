#pragma once

#include "Halfband.h"

// True-peak estimator (MODEL_SPEC 6). One instance per channel.
//
// ITU-R BS.1770-4 Annex 2 estimates the true peak by oversampling (4x at
// 48 kHz) and taking the largest absolute value. STEEL uses the same method
// with a more accurate interpolator and a higher ratio:
//   - P = 8 below 60 kHz, 4 below 120 kHz, 2 above (so P * fs >= 352.8 kHz);
//   - the interpolator is STEEL's half-band cascade (flat to 20 kHz at 44.1
//     kHz within 0.001 dB, images down ~90 dB), not the 48-tap example filter
//     of the Recommendation;
//   - every local maximum of |v| in the oversampled stream is refined with a
//     parabola through it and its two neighbours, which removes most of the
//     residual "between oversampled points" under-read (at 8x the worst case
//     for a 20 kHz tone falls from 0.12 dB to under 0.001 dB).
// The original samples pass the cascade exactly (Halfband.h), so the
// estimate is never below the sample peak.
//
// Output: one value per host sample, the peak magnitude of the reconstructed
// signal over the interval [j, j + 1) host samples, emitted A = latency()
// samples later (j = n - A).
//
// The same class feeds the limiter's true-peak stage, the output and input
// true-peak meters and the loudness meter's max true peak, so the limiter
// and every displayed true peak share one definition (addendum 11).
namespace tleet::steel
{
    class TruePeakDetector
    {
    public:
        static int factorLog2For (double sampleRate) noexcept
        {
            return sampleRate < 60000.0 ? 3 : (sampleRate < 120000.0 ? 2 : 1);
        }

        void prepare (double sampleRate, int maxBlock)
        {
            log2P = factorLog2For (sampleRate);
            P = 1 << log2P;
            const auto& set = HalfbandSet::get();
            for (int s = 0; s < log2P; ++s)
                up[s].prepare (set.stage[s]);
            for (auto& b : buf)
                b.assign ((size_t) (maxBlock * P), 0.0f);
            lambda = OversamplingLatency::upTopSamples (log2P);
            // interval j owns top samples [P j + lambda, P j + lambda + P); its last sample is
            // examined (it needs its right neighbour) when top sample P j + lambda + P arrives,
            // i.e. during host sample j + 1 + floor(lambda / P)
            A = 1 + lambda / P;
            reset();
        }

        void reset() noexcept
        {
            for (auto& u : up)
                u.reset();
            v1 = v2 = 0.0f;
            topIndex = 0;
            pending.fill (0.0f);
            outIndex = 0;
        }

        int latency() const noexcept { return A; }
        int factor() const noexcept { return P; }

        // in: n host samples; peaks: n interval peaks (interval n - A for output n)
        void process (const float* in, float* peaks, int n) noexcept
        {
            const float* src = in;
            int len = n;
            for (int s = 0; s < log2P; ++s)
            {
                float* dst = buf[(size_t) (s & 1)].data();
                up[s].process (src, dst, len);
                src = dst;
                len *= 2;
            }
            // len = P * n top samples in src
            for (int i = 0; i < len; ++i)
            {
                const float v0 = src[i];
                // examine the sample before the newest (v1) with both neighbours known
                const int64_t m = topIndex - 1;   // index of v1
                float a1 = std::abs (v1);
                if (m >= 0)
                {
                    const float a2 = std::abs (v2), a0 = std::abs (v0);
                    if (a1 >= a2 && a1 >= a0 && a1 > 0.0f)
                    {
                        // parabola through the three points (signed, around the extremum)
                        const float s = v1 > 0.0f ? 1.0f : -1.0f;
                        const float ym = s * v2, y0 = s * v1, yp = s * v0;
                        const float den = ym - 2.0f * y0 + yp;
                        if (den < 0.0f)
                        {
                            const float p = 0.5f * (ym - yp) / den;
                            if (std::abs (p) <= 1.0f)
                                a1 = std::max (a1, y0 - 0.25f * (ym - yp) * p);
                        }
                    }
                    const int64_t q = m - lambda;
                    if (q >= 0)
                    {
                        const int64_t j = q >> log2P;
                        const int slot = (int) (j & (ringSize - 1));
                        if ((q & (P - 1)) == 0)
                            pending[(size_t) slot] = a1;
                        else
                            pending[(size_t) slot] = std::max (pending[(size_t) slot], a1);
                    }
                }
                v2 = v1;
                v1 = v0;
                ++topIndex;
            }
            // emit interval n - A for every host sample n of this block
            for (int i = 0; i < n; ++i)
            {
                const int64_t j = outIndex - A;
                peaks[i] = j >= 0 ? pending[(size_t) (j & (ringSize - 1))] : 0.0f;
                ++outIndex;
            }
        }

    private:
        static constexpr int ringSize = 64; // > A + 2 for every rate
        HalfbandUp up[3];
        std::array<std::vector<float>, 2> buf;
        std::array<float, ringSize> pending {};
        int log2P = 3, P = 8, lambda = 0, A = 1;
        float v1 = 0.0f, v2 = 0.0f;
        int64_t topIndex = 0, outIndex = 0;
    };
}
