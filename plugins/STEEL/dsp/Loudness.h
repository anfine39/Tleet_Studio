#pragma once

#include "Filters.h"
#include <atomic>
#include <limits>
#include <vector>

// Loudness meter after ITU-R BS.1770-4 / EBU R128 (MODEL_SPEC 8).
//
//   K-weighting per channel, channel weights G = 1 (L, R or a single mono
//   channel; a mono bus is ONE channel, not dual mono), mean square per 100 ms
//   hop (hop = round(0.1 fs) samples).
//   Momentary  M: the last 4 hops (400 ms rectangular window).
//   Short-term S: the last 30 hops (3 s). Both update every 100 ms. Before the
//                 window has filled, the missing hops count as silence.
//   Integrated I: gating blocks of 400 ms with 75 % overlap (one per hop),
//                 absolute gate -70 LUFS, relative gate -10 LU below the
//                 absolute-gated mean (BS.1770-4 2.8).
//   LRA (EBU Tech 3342): the short-term values (3 s, one per hop), absolute
//                 gate -70 LUFS, relative gate -20 LU below their power mean,
//                 LRA = L95 - L10 of what remains (rank round(p (n - 1))).
//   L = -0.691 + 10 log10 (sum_ch G_ch * z_ch).
//
// I and LRA keep a histogram of 0.01 LU bins from -70 to +10 LUFS (count and
// the exact sum of block energies per bin, values above +10 in the top bin),
// so memory is fixed however long the measurement runs. The only
// approximation is gate placement within one 0.01 LU bin.
//
// Threads: process() and the computations run on the audio thread; results
// are published through atomics once per hop. reset() is requested from any
// thread with requestReset() and performed by the audio thread at its next
// call.
namespace tleet::steel
{
    struct LoudnessReadings
    {
        // -infinity (std::isfinite false) = no reading: nothing measured yet,
        // or every block is below the gates.
        float momentaryLufs = -std::numeric_limits<float>::infinity();
        float shortTermLufs = -std::numeric_limits<float>::infinity();
        float integratedLufs = -std::numeric_limits<float>::infinity();
        float loudnessRangeLu = 0.0f;        // 0 until two short-term values pass the gates
        float maxTruePeakDbtp = -std::numeric_limits<float>::infinity();
        float maxSamplePeakDbfs = -std::numeric_limits<float>::infinity();
        double measuredSeconds = 0.0;        // audio measured since the last reset
    };

    class LoudnessMeter
    {
    public:
        static constexpr double lowestLufs = -70.0, highestLufs = 10.0, binWidth = 0.01;
        static constexpr int numBins = 8000;
        static constexpr int maxChannels = 2;

        static double lufs (double energy) noexcept
        {
            return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy) : -std::numeric_limits<double>::infinity();
        }

        void prepare (double sampleRate, int numChannels)
        {
            fs = sampleRate;
            channels = std::clamp (numChannels, 1, maxChannels);
            hopLength = std::max (1, (int) std::lround (0.1 * fs));
            for (auto& k : kw)
                k.prepare (fs);
            intCount.assign ((size_t) numBins, 0);
            intEnergy.assign ((size_t) numBins, 0.0);
            lraCount.assign ((size_t) numBins, 0);
            lraEnergy.assign ((size_t) numBins, 0.0);
            doReset();
        }

        void requestReset() noexcept { resetPending.store (true, std::memory_order_release); }
        bool isResetPending() const noexcept { return resetPending.load (std::memory_order_acquire); }

        // Audio thread. `in` are the channels of the measured signal, `truePeaks`
        // the per-sample true-peak values (same layout) or null.
        void process (const float* const* in, const float* const* truePeaks, int n) noexcept
        {
            if (resetPending.exchange (false, std::memory_order_acq_rel))
                doReset();

            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    const double y = kw[(size_t) ch].process (in[ch][i]);
                    hopSum[(size_t) ch] += y * y;
                    samplePeak = std::max (samplePeak, std::abs (in[ch][i]));
                    if (truePeaks != nullptr)
                        truePeak = std::max (truePeak, truePeaks[ch][i]);
                }
                if (++hopFill >= hopLength)
                    endOfHop();
            }
            totalSamples += n;
            publishPeaks();
        }

        LoudnessReadings read() const noexcept
        {
            LoudnessReadings r;
            if (isResetPending())
                return r;
            r.momentaryLufs = pubM.load (std::memory_order_relaxed);
            r.shortTermLufs = pubS.load (std::memory_order_relaxed);
            r.integratedLufs = pubI.load (std::memory_order_relaxed);
            r.loudnessRangeLu = pubLra.load (std::memory_order_relaxed);
            r.maxTruePeakDbtp = pubTp.load (std::memory_order_relaxed);
            r.maxSamplePeakDbfs = pubSp.load (std::memory_order_relaxed);
            r.measuredSeconds = pubSeconds.load (std::memory_order_relaxed);
            return r;
        }

        int getHopLength() const noexcept { return hopLength; }
        const KWeighting& getKWeighting() const noexcept { return kw[0]; }

    private:
        static int binOf (double l) noexcept
        {
            return std::clamp ((int) std::floor ((l - lowestLufs) / binWidth), 0, numBins - 1);
        }
        static double binLow (int b) noexcept { return lowestLufs + b * binWidth; }

        void doReset() noexcept
        {
            for (auto& k : kw)
                k.reset();
            hopSum.fill (0.0);
            hops.fill (0.0);
            hopFill = 0;
            hopCount = 0;
            std::fill (intCount.begin(), intCount.end(), 0);
            std::fill (intEnergy.begin(), intEnergy.end(), 0.0);
            std::fill (lraCount.begin(), lraCount.end(), 0);
            std::fill (lraEnergy.begin(), lraEnergy.end(), 0.0);
            intTotalCount = lraTotalCount = 0;
            intTotalEnergy = lraTotalEnergy = 0.0;
            samplePeak = truePeak = 0.0f;
            totalSamples = 0;
            const float ninf = -std::numeric_limits<float>::infinity();
            pubM.store (ninf); pubS.store (ninf); pubI.store (ninf);
            pubLra.store (0.0f); pubTp.store (ninf); pubSp.store (ninf);
            pubSeconds.store (0.0);
        }

        void publishPeaks() noexcept
        {
            const float ninf = -std::numeric_limits<float>::infinity();
            pubTp.store (truePeak > 0.0f ? 20.0f * std::log10 (truePeak) : ninf, std::memory_order_relaxed);
            pubSp.store (samplePeak > 0.0f ? 20.0f * std::log10 (samplePeak) : ninf, std::memory_order_relaxed);
            pubSeconds.store ((double) totalSamples / fs, std::memory_order_relaxed);
        }

        void endOfHop() noexcept
        {
            double e = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                e += hopSum[(size_t) ch] / hopLength; // channel weight G = 1
                hopSum[(size_t) ch] = 0.0;
            }
            hops[(size_t) (hopCount % 30)] = e;
            ++hopCount;
            hopFill = 0;

            double m = 0.0, s = 0.0;
            for (int i = 0; i < 30; ++i)
            {
                const int64_t idx = hopCount - 1 - i;
                if (idx < 0)
                    break;
                const double h = hops[(size_t) (idx % 30)];
                if (i < 4)
                    m += h;
                s += h;
            }
            m /= 4.0;
            s /= 30.0;
            const double lm = lufs (m), ls = lufs (s);
            pubM.store ((float) lm, std::memory_order_relaxed);
            pubS.store ((float) ls, std::memory_order_relaxed);

            // integrated: one 400 ms gating block per hop once 4 hops exist
            if (hopCount >= 4 && lm > lowestLufs)
            {
                const int b = binOf (lm);
                ++intCount[(size_t) b];
                intEnergy[(size_t) b] += m;
                ++intTotalCount;
                intTotalEnergy += m;
            }
            // LRA: one short-term value per hop once 30 hops exist
            if (hopCount >= 30 && ls > lowestLufs)
            {
                const int b = binOf (ls);
                ++lraCount[(size_t) b];
                lraEnergy[(size_t) b] += s;
                ++lraTotalCount;
                lraTotalEnergy += s;
            }
            computeIntegrated();
            computeRange();
        }

        void computeIntegrated() noexcept
        {
            if (intTotalCount == 0)
            {
                pubI.store (-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
                return;
            }
            const double gate = lufs (intTotalEnergy / (double) intTotalCount) - 10.0;
            double e = 0.0;
            int64_t c = 0;
            for (int b = binOf (gate); b < numBins; ++b)
            {
                if (binLow (b) + 0.5 * binWidth <= gate)
                    continue; // the gate's own bin: blocks whose loudness is above the gate (to 0.005 LU)
                e += intEnergy[(size_t) b];
                c += intCount[(size_t) b];
            }
            pubI.store (c > 0 ? (float) lufs (e / (double) c) : -std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
        }

        void computeRange() noexcept
        {
            if (lraTotalCount == 0)
            {
                pubLra.store (0.0f, std::memory_order_relaxed);
                return;
            }
            const double gate = lufs (lraTotalEnergy / (double) lraTotalCount) - 20.0;
            const int first = binOf (gate) + (binLow (binOf (gate)) + 0.5 * binWidth <= gate ? 1 : 0);
            int64_t c = 0;
            for (int b = first; b < numBins; ++b)
                c += lraCount[(size_t) b];
            if (c < 2)
            {
                pubLra.store (0.0f, std::memory_order_relaxed);
                return;
            }
            // percentiles over the sorted gated values: rank round(p (c - 1)), as libebur128
            const int64_t r10 = (int64_t) std::llround (0.10 * (double) (c - 1));
            const int64_t r95 = (int64_t) std::llround (0.95 * (double) (c - 1));
            double l10 = 0.0, l95 = 0.0;
            int64_t acc = 0;
            bool have10 = false;
            for (int b = first; b < numBins; ++b)
            {
                const int64_t k = lraCount[(size_t) b];
                if (k == 0)
                    continue;
                // representative loudness of the bin: its mean
                const double l = lufs (lraEnergy[(size_t) b] / (double) k);
                if (! have10 && acc + k > r10)
                {
                    l10 = l;
                    have10 = true;
                }
                if (acc + k > r95)
                {
                    l95 = l;
                    break;
                }
                acc += k;
            }
            pubLra.store ((float) std::max (0.0, l95 - l10), std::memory_order_relaxed);
        }

        double fs = 48000.0;
        int channels = 2, hopLength = 4800, hopFill = 0;
        int64_t hopCount = 0, totalSamples = 0;
        std::array<KWeighting, maxChannels> kw;
        std::array<double, maxChannels> hopSum {};
        std::array<double, 30> hops {};
        std::vector<int64_t> intCount, lraCount;
        std::vector<double> intEnergy, lraEnergy;
        int64_t intTotalCount = 0, lraTotalCount = 0;
        double intTotalEnergy = 0.0, lraTotalEnergy = 0.0;
        float samplePeak = 0.0f, truePeak = 0.0f;

        std::atomic<bool> resetPending { false };
        std::atomic<float> pubM { -std::numeric_limits<float>::infinity() }, pubS { -std::numeric_limits<float>::infinity() },
            pubI { -std::numeric_limits<float>::infinity() }, pubLra { 0.0f },
            pubTp { -std::numeric_limits<float>::infinity() }, pubSp { -std::numeric_limits<float>::infinity() };
        std::atomic<double> pubSeconds { 0.0 };
    };
}
