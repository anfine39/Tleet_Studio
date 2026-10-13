#include "WeightModel.h"
#include <limits>

namespace tleet::weight
{
    // ---- fixed constants (MODEL_SPEC.md sections 2-5)
    static constexpr double thresholdSmoothS = 0.020; // threshold, makeup, link glide              [APPROXIMATION]
    static constexpr double slopeSmoothS     = 0.010; // ratio glide                                [APPROXIMATION]
    static constexpr double blendS           = 0.010; // topology and internal <-> external crossfade [TLEET_EXTENSION]

    template <typename T>
    static inline void glide (T& v, T target, T coeff) noexcept
    {
        const T d = target - v;
        v = std::abs (d) <= (T) 1.0e-6 * ((T) 1 + std::abs (target)) ? target : v + d * coeff;
    }

    static inline void ramp (double& v, double target, double step) noexcept
    {
        if (v < target)      v = std::min (target, v + step);
        else if (v > target) v = std::max (target, v - step);
    }

    double WeightModel::attackSeconds (int index) noexcept
    {
        static constexpr double t[numAttacks] = { 0.00003, 0.0001, 0.0003, 0.001, 0.003, 0.010, 0.030 };
        return t[std::clamp (index, 0, numAttacks - 1)];
    }

    double WeightModel::releaseSeconds (int index) noexcept
    {
        static constexpr double t[numReleases] = { 0.05, 0.1, 0.2, 0.5, 1.0, 2.0 };
        return t[std::clamp (index, 0, numReleases - 1)];
    }

    double WeightModel::ratioValue (int index) noexcept
    {
        static constexpr double r[numRatios] = { 1.5, 2.0, 3.0, 4.0, 6.0, 10.0, std::numeric_limits<double>::infinity() };
        return r[std::clamp (index, 0, numRatios - 1)];
    }

    double WeightModel::ratioSlope (int index) noexcept
    {
        const double r = ratioValue (index);
        return std::isinf (r) ? 1.0 : 1.0 - 1.0 / r;
    }

    double WeightModel::effectiveRatio (int ratio, int topology) noexcept
    {
        const double s = ratioSlope (ratio);
        if (topology == feedBack)
            return 1.0 + s; // output slope 1 / (1 + S)
        return s >= 1.0 ? std::numeric_limits<double>::infinity() : 1.0 / (1.0 - s);
    }

    double WeightModel::staticGainReductionDb (double levelDbfs, double thresholdDbfs, int ratio, int topology) noexcept
    {
        const double s = ratioSlope (ratio);
        if (topology != feedBack)
            return gainComputerDb (levelDbfs, thresholdDbfs, s);

        // c - T(level - c) is increasing in c: bisection.
        double lo = 0.0, hi = std::max (0.0, levelDbfs - thresholdDbfs) + kneeWidthDb;
        for (int i = 0; i < 200; ++i)
        {
            const double c = 0.5 * (lo + hi);
            (c - gainComputerDb (levelDbfs - c, thresholdDbfs, s) > 0.0 ? hi : lo) = c;
        }
        return 0.5 * (lo + hi);
    }

    void WeightModel::prepare (double sampleRateHz)
    {
        fs = sampleRateHz;
        cs = dsp::onePoleCoeff (thresholdSmoothS, fs);
        csFast = dsp::onePoleCoeff (slopeSmoothS, fs);
        blendStep = 1.0 / std::max (1.0, blendS * fs);
        kRms = -std::expm1 (-1.0 / (rmsTimeSeconds * fs));
        filterControl.prepare (fs);
        for (auto& s : stage)
            s.prepare (fs);
        reset();
    }

    void WeightModel::reset()
    {
        ms.fill (0.0);
        cap.fill (0.0);
        cv.fill (0.0);
        gain.fill (1.0f);

        thresholdDb = thresholdTarget;
        slope = ratioSlope (ratioIndex);
        makeupGain = makeupTarget;
        link = linkTarget;
        extBlend = useExternal ? 1.0 : 0.0;
        topoBlend = topologyIndex == feedBack ? 1.0 : 0.0;

        filterControl.setTargets (hpfHz, thrustIndex);
        filterControl.snap();
        for (auto* bank : { &filterFF, &filterFB, &filterKey })
            for (auto& f : *bank)
                f.reset();
        for (auto& s : stage)
        {
            s.setTone (toneIndex);
            s.reset();
        }

        controlCountdown = 0;
        controlUpdate();
    }

    void WeightModel::controlUpdate() noexcept
    {
        filterControl.setTargets (hpfHz, thrustIndex);
        filterControl.control();
        // Exact exponential ballistics: c += k (target - c), k = 1 - exp(-1 / (tau fs)).
        ka = -std::expm1 (-1.0 / (attackSeconds (attackIndex) * fs));
        kr = -std::expm1 (-1.0 / (releaseSeconds (releaseIndex) * fs));
    }

    void WeightModel::process (float* const* audio, int numAudio, const float* const* key, int numKey, int numSamples) noexcept
    {
        numAudio = std::clamp (numAudio, 0, maxChannels);
        numKey = key != nullptr ? std::clamp (numKey, 0, maxChannels) : 0;
        const bool haveKey = numKey > 0;
        const double blendTarget = (useExternal && haveKey) ? 1.0 : 0.0;
        const double topoTarget = topologyIndex == feedBack ? 1.0 : 0.0;
        const double slopeTarget = ratioSlope (ratioIndex);
        for (auto& s : stage)
            s.setTone (toneIndex);

        for (int i = 0; i < numSamples; ++i)
        {
            if (--controlCountdown <= 0)
            {
                controlCountdown = DetectorFilterControl::controlInterval;
                controlUpdate();
            }

            glide (thresholdDb, (double) thresholdTarget, cs);
            glide (slope, slopeTarget, csFast);
            glide (link, (double) linkTarget, cs);
            glide (makeupGain, makeupTarget, (float) cs);
            ramp (extBlend, blendTarget, blendStep);
            ramp (topoBlend, topoTarget, blendStep);

            // ---- detectors (one per channel)
            float x[maxChannels] {};
            for (int ch = 0; ch < numAudio; ++ch)
            {
                const auto c = (size_t) ch;
                x[ch] = dsp::sanitize (audio[ch][i]);

                const double ff = filterFF[c].process ((double) x[ch], filterControl);
                const double fb = filterFB[c].process ((double) x[ch] * (double) gain[c], filterControl);
                double p = ff * ff + topoBlend * (fb * fb - ff * ff);

                if (haveKey)
                {
                    double pk;
                    if (numAudio == 1 && numKey == 2)
                    {
                        // mono main, stereo key: the mean power of both key channels
                        const double k0 = filterKey[0].process ((double) dsp::sanitize (key[0][i]), filterControl);
                        const double k1 = filterKey[1].process ((double) dsp::sanitize (key[1][i]), filterControl);
                        pk = 0.5 * (k0 * k0 + k1 * k1);
                    }
                    else
                    {
                        const double k = filterKey[c].process ((double) dsp::sanitize (key[std::min (ch, numKey - 1)][i]), filterControl);
                        pk = k * k;
                    }
                    p += extBlend * (pk - p);
                }

                ms[c] += kRms * (p - ms[c]);
                if (! (ms[c] >= 1.0e-30) || ! std::isfinite (ms[c]))
                    ms[c] = 0.0;

                const double target = gainComputerDb (levelDbOfMeanSquare (ms[c]), thresholdDb, slope);
                double cap1 = cap[c] + (target > cap[c] ? ka : kr) * (target - cap[c]);
                cap1 = std::clamp (cap1, 0.0, maxGainReductionDb);
                if (! std::isfinite (cap1) || cap1 < 1.0e-9)
                    cap1 = 0.0; // no denormal tail on the capacitor
                cap[c] = cap1;
            }

            // ---- stereo link (control voltages summed, normalised)
            if (numAudio == 2)
            {
                const double h = 0.5 * link;
                cv[0] = cap[0] + h * (cap[1] - cap[0]);
                cv[1] = cap[1] + h * (cap[0] - cap[1]);
            }
            else
            {
                cv[0] = cv[1] = cap[0];
            }

            // ---- VCA, makeup, output stage
            stage[0].advance();
            stage[1].advance();
            for (int ch = 0; ch < numAudio; ++ch)
            {
                const auto c = (size_t) ch;
                const float g = (float) std::exp (-ln10Over20 * cv[c]);
                gain[c] = g;
                audio[ch][i] = stage[c].process (x[ch] * g * makeupGain);
            }
            if (numAudio == 1)
                gain[1] = gain[0];

            if (tel[0] != nullptr) tel[0]->push ((float) cv[0], gain[0]);
            if (tel[1] != nullptr) tel[1]->push ((float) cv[1], gain[1]);

            if (traceCount < traceCapacity)
            {
                if (trace[0] != nullptr) trace[0][traceCount] = gain[0];
                if (trace[1] != nullptr) trace[1][traceCount] = gain[1];
                ++traceCount;
            }
        }
    }
}
