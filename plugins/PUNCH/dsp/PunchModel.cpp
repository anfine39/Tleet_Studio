#include "PunchModel.h"

namespace tleet::punch
{
    // ---- fixed constants (MODEL_SPEC.md sections 2-5)
    static constexpr double controlSmoothS = 0.008; // per stage of the 2-pole glide (~27 ms to 99 %)  [APPROXIMATION]
    static constexpr float  inputHpHz      = 0.5f;  // input coupling: -3 dB at 0.5 Hz (160A spec)     [SOURCE_CONFIRMED]
    static constexpr float  outputHpHz     = 0.5f;  // removes the offset of the asymmetric GRIT stages [APPROXIMATION]

    // Critically damped glide (two one-poles in series). Each stage lands
    // exactly on its target, so a settled control is bit-identical to the
    // value it glided to and no denormal tail remains.
    static inline void glide (double& v, double target, double coeff) noexcept
    {
        const double d = target - v;
        v = std::abs (d) <= 1.0e-9 * (1.0 + std::abs (target)) ? target : v + d * coeff;
    }

    double PunchModel::Glide2::next (double target, double k) noexcept
    {
        glide (a, target, k);
        glide (b, a, k);
        return b;
    }

    void PunchModel::prepare (double sampleRateHz)
    {
        fs = sampleRateHz;
        cs = dsp::onePoleCoeff (controlSmoothS, fs);
        // Exact discretisation of d(ms)/dt = (e - ms) / tau: a free decay
        // falls by exactly exp(-1 / (tau fs)) per sample (125 dB/s).
        msCoeff = -std::expm1 (-1.0 / (detectorTauSeconds * fs));
        for (auto& d : dcIn)  d.prepare (fs, inputHpHz);
        for (auto& d : dcOut) d.prepare (fs, outputHpHz);
        reset();
    }

    void PunchModel::reset()
    {
        thresholdDb.snap (thresholdTarget);
        slope.snap (slopeTarget);
        overEasy.snap (overEasyTarget);
        grit.snap (gritTarget);
        amp.set (gritTarget * gritAmpDrive, gritAmpBias);
        meanSquare = 0.0;
        grDb = 0.0;
        gain = 1.0f;
        minGainSinceRead = 1.0f;
        for (auto& d : dcIn)  d.reset();
        for (auto& d : dcOut) d.reset();
    }

    void PunchModel::process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = std::clamp (numChannels, 0, maxChannels);
        if (numChannels == 0)
            return;
        const double powerScale = 1.0 / numChannels; // power sum, normalised so L = R reads like mono

        for (int i = 0; i < numSamples; ++i)
        {
            const double thr = thresholdDb.next (thresholdTarget, cs);
            const double s = slope.next (slopeTarget, cs);
            const double oe = overEasy.next (overEasyTarget, cs);
            const float gr = (float) grit.next (gritTarget, cs);
            if (std::abs (gr * gritAmpDrive - amp.drive) > 0.0f) // only while GRIT glides
                amp.set (gr * gritAmpDrive, gritAmpBias);

            // ---- input coupling, then the true-RMS detector on the power sum
            float x[maxChannels] {};
            double power = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                x[ch] = (float) dcIn[(size_t) ch].process ((double) dsp::sanitize (audio[ch][i]));
                power += (double) x[ch] * (double) x[ch];
            }
            meanSquare += msCoeff * (power * powerScale - meanSquare);
            if (! std::isfinite (meanSquare) || meanSquare < 1.0e-30)
                meanSquare = 0.0; // silence settles to exact zero (no denormal tail)

            // ---- gain computer -> VCA control voltage
            double c = meanSquare > 0.0 ? gainComputerDb (levelDb (meanSquare), thr, s, oe) : 0.0;
            if (! std::isfinite (c) || c < 1.0e-9)
                c = 0.0;
            grDb = c;
            gain = (float) std::exp (-ln10Over20 * c);
            minGainSinceRead = std::min (minGainSinceRead, gain);
            if (trace != nullptr && traceCount < traceCapacity)
                trace[traceCount++] = gain;

            // ---- audio path: VCA (one gain for both channels) -> GRIT -> DC blocker
            const float depth = gritDepth (gr, c);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float y = amp.process (vcaStage (x[ch], gain, gritVcaEven, gritVcaOdd, depth));
                audio[ch][i] = (float) dcOut[(size_t) ch].process ((double) y);
            }
        }
    }
}
