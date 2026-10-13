#include "VeilModel.h"

namespace tleet::veil
{
    template <typename T>
    static inline void glide (T& v, T target, T coeff) noexcept
    {
        const T d = target - v;
        v = std::abs (d) <= (T) 1.0e-9 * ((T) 1 + std::abs (target)) ? target : v + d * coeff;
    }

    static inline bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    static inline void ramp (double& v, double target, double step) noexcept
    {
        if (v < target)      v = std::min (target, v + step);
        else if (v > target) v = std::max (target, v - step);
    }

    void VeilModel::setAttackMs (float ms) noexcept
    {
        const double s = std::clamp ((double) ms, 1.0, 10000.0) * 0.001;
        if (s < attackS || s > attackS)
        {
            attackS = s;
            ka = coeffFor (attackS, fsOs);
        }
    }

    void VeilModel::setRecoveryMs (float ms) noexcept
    {
        const double s = std::clamp ((double) ms, 1.0, 10000.0) * 0.001;
        if (s < recoveryS || s > recoveryS)
        {
            recoveryS = s;
            kr = coeffFor (recoveryS, fsOs);
        }
    }

    void VeilModel::prepare (double hostRate, int oversamplingFactor)
    {
        fsHost = hostRate;
        fsOs = hostRate * std::max (1, oversamplingFactor);
        kGlideSample = coeffFor (controlGlideS, fsOs);
        kGlideControl = coeffFor (controlGlideS, fsOs / controlInterval);
        rampStep = 1.0 / std::max (1.0, rampS * fsOs);
        ka = coeffFor (attackS, fsOs);
        kr = coeffFor (recoveryS, fsOs);
        kd = coeffFor (detectorTauS, fsOs);
        weightCoeff = OnePole::coeff (filt::weightShelfHz, fsHost);
        couplingCoeffHost = OnePole::coeff (filt::couplingHz, fsHost);
        couplingCoeffOs = OnePole::coeff (filt::couplingHz, fsOs);
        tc.dcCoeff = OnePole::coeff (tube::dcBlockHz, fsOs);
        hpfControl.prepare (fsHost);
        scHpfControl.prepare (fsOs);
        reset();
    }

    void VeilModel::reset()
    {
        avg1.fill (0.0);
        avg2.fill (0.0);
        yPrev.fill (0.0);
        xPrev.fill (0.0);
        slopePrev.fill (1.0);
        target.fill (0.0);
        cap.fill (0.0);
        cv.fill (0.0);
        gain.fill (1.0f);
        blockMaxGr.fill (0.0f);
        for (auto& s : hpf) s.reset();
        for (auto& s : scInternal) s.reset();
        for (auto& s : scKey) s.reset();
        for (auto& s : couplingIn) s.reset();
        for (auto& s : couplingKey) s.reset();
        for (auto& s : weight) s.reset();
        for (auto& t : tubes) t.reset();

        thresholdDb = thresholdTarget;
        mix = mixTarget;
        link = linkTarget;
        extBlend = useExternal ? 1.0 : 0.0;
        hpfControl.snap();
        scHpfControl.snap();
        controlCountdown = 0;
        controlUpdate (true);
    }

    void VeilModel::controlUpdate (bool snapAll) noexcept
    {
        const auto& v = tube::voicing (characterIndex);
        if (snapAll)
        {
            compress = compressTarget;
            drive = driveTarget;
            harmonics = harmonicsTarget;
            saturation = saturationTarget;
            voice = v;
            weightGain = std::pow (10.0, v.weightDb / 20.0);
        }
        else
        {
            if (same (compress, compressTarget) && same (drive, driveTarget) && same (harmonics, harmonicsTarget)
                && same (saturation, saturationTarget) && same (voice.b2, v.b2) && same (voice.b3, v.b3)
                && same (voice.headroom, v.headroom) && same (voice.element, v.element)
                && same (voice.softness, v.softness) && same (voice.weightDb, v.weightDb))
                return; // settled: the derived coefficients are already current
            const double k = kGlideControl;
            glide (compress, compressTarget, k);
            glide (drive, driveTarget, k);
            glide (harmonics, harmonicsTarget, k);
            glide (saturation, saturationTarget, k);
            glide (voice.b2, v.b2, k);
            glide (voice.b3, v.b3, k);
            glide (voice.headroom, v.headroom, k);
            glide (voice.element, v.element, k);
            glide (voice.softness, v.softness, k);
            glide (voice.weightDb, v.weightDb, k);
            weightGain = std::pow (10.0, voice.weightDb / 20.0);
        }

        lawConst = law::constantsFor (compress);
        kneeStart = law::kneeStartDb (lawConst);

        const double d = tube::driveGain (drive);
        const double vc = tube::inputLimit * voice.headroom;
        tc.inScale = d / vc;
        tc.outScale = vc / d;
        tc.h2 = harmonics * voice.b2;
        tc.h3 = harmonics * voice.b3;
        tc.hElement = harmonics * voice.element;
        tc.satDepth = saturation < 1.0e-6 ? 0.0 : saturation;
        tc.invCeiling = 1.0 / tube::saturationCeiling (saturation);
        tc.p = (float) voice.softness;
        tc.invP = (float) (1.0 / voice.softness);
    }

    void VeilModel::preProcess (float* const* io, int numChannels, int numSamples) noexcept
    {
        numChannels = std::clamp (numChannels, 0, maxChannels);
        for (int i = 0; i < numSamples; ++i)
        {
            if (hpfControl.advance())
                for (auto& s : hpf) s.reset();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = (size_t) ch;
                double x = (double) io[ch][i];
                x = hpf[c].process (x, hpfControl);           // bit-transparent while Off
                x = couplingIn[c].highpass (x, couplingCoeffHost);
                io[ch][i] = (float) x;
            }
        }
    }

    void VeilModel::postProcess (float* const* io, int numChannels, int numSamples) noexcept
    {
        numChannels = std::clamp (numChannels, 0, maxChannels);
        const double gw = weightGain - 1.0;
        if (std::abs (gw) < 1.0e-9 && std::abs (voice.weightDb - tube::voicing (characterIndex).weightDb) < 1.0e-9)
        {
            for (auto& s : weight) s.reset();
            return;
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& w = weight[(size_t) ch];
            for (int i = 0; i < numSamples; ++i)
            {
                const double y = (double) io[ch][i];
                io[ch][i] = (float) (y + gw * w.lowpass (y, weightCoeff));
            }
        }
    }

    void VeilModel::process (float* const* audio, int numAudio, const float* const* key, int numKey, int numSamples) noexcept
    {
        numAudio = std::clamp (numAudio, 0, maxChannels);
        numKey = key != nullptr ? std::clamp (numKey, 0, maxChannels) : 0;
        const bool haveKey = numKey > 0;
        const double blendTarget = (useExternal && haveKey) ? 1.0 : 0.0;
        blockMaxGr.fill (0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            if (--controlCountdown <= 0)
            {
                controlCountdown = controlInterval;
                controlUpdate (false);
            }
            glide (thresholdDb, (double) thresholdTarget, kGlideSample);
            ramp (mix, mixTarget, rampStep);
            ramp (link, linkTarget, rampStep);
            ramp (extBlend, blendTarget, rampStep);
            if (scHpfControl.advance())
            {
                for (auto& s : scInternal) s.reset();
                for (auto& s : scKey) s.reset();
            }

            // ---- input stage and detector (one per channel)
            double a[maxChannels] {};
            for (int ch = 0; ch < numAudio; ++ch)
            {
                const auto c = (size_t) ch;
                a[ch] = tubes[c].inputStage ((double) audio[ch][i], tc);

                double r = std::abs (scInternal[c].process (a[ch], scHpfControl));
                if (haveKey)
                {
                    double rk;
                    if (numAudio == 1 && numKey == 2)
                    {
                        // mono main, stereo key: the mean of both rectified key channels
                        const double k0 = scKey[0].process (couplingKey[0].highpass ((double) key[0][i], couplingCoeffOs), scHpfControl);
                        const double k1 = scKey[1].process (couplingKey[1].highpass ((double) key[1][i], couplingCoeffOs), scHpfControl);
                        rk = 0.5 * (std::abs (k0) + std::abs (k1));
                    }
                    else
                    {
                        const int kc = std::min (ch, numKey - 1);
                        rk = std::abs (scKey[c].process (couplingKey[c].highpass ((double) key[kc][i], couplingCoeffOs), scHpfControl));
                    }
                    r += extBlend * (rk - r);
                }

                avg1[c] += kd * (r - avg1[c]);
                avg2[c] += kd * (avg1[c] - avg2[c]);
                if (! (avg2[c] >= 1.0e-12) || ! std::isfinite (avg2[c]))
                {
                    avg1[c] = std::isfinite (avg1[c]) && avg1[c] >= 1.0e-12 ? avg1[c] : 0.0;
                    avg2[c] = 0.0;
                }

                // vari-mu law: steady GR the feedback loop would settle at for this level
                double t = 0.0;
                if (avg2[c] > 0.0)
                {
                    const double x = levelDbOfAverage (avg2[c]) - thresholdDb;
                    const double guess = yPrev[c] + (x - xPrev[c]) / slopePrev[c];
                    const double y = law::solveOutputDb (x, lawConst, kneeStart, guess, &slopePrev[c]);
                    yPrev[c] = y;
                    xPrev[c] = x;
                    t = std::max (0.0, x - y);
                }
                target[c] = t;

                // attack / recovery ballistics on the control voltage (GR in dB)
                double k1 = cap[c] + (t > cap[c] ? ka : kr) * (t - cap[c]);
                if (! std::isfinite (k1) || k1 < 1.0e-9)
                    k1 = 0.0;
                cap[c] = std::min (k1, law::maxGainReductionDb);
            }

            // ---- stereo link: both channels take the larger control voltage
            if (numAudio == 2)
            {
                const double hi = std::max (cap[0], cap[1]);
                cv[0] = cap[0] + link * (hi - cap[0]);
                cv[1] = cap[1] + link * (hi - cap[1]);
            }
            else
            {
                cv[0] = cv[1] = cap[0];
            }

            // ---- gain element, parallel MIX, output stage
            for (int ch = 0; ch < numAudio; ++ch)
            {
                const auto c = (size_t) ch;
                const float g = (float) std::exp (-law::ln10Over20 * cv[c]);
                gain[c] = g;
                const double b = tubes[c].element (a[ch], (double) g, cv[c], tc);
                const double m = a[ch] + mix * (b - a[ch]);
                audio[ch][i] = (float) TubeChannel::outputStage (m, tc);
                blockMaxGr[c] = std::max (blockMaxGr[c], (float) cv[c]);
            }
            if (numAudio == 1)
            {
                gain[1] = gain[0];
                blockMaxGr[1] = blockMaxGr[0];
            }

            if (traceCount < traceCapacity)
            {
                if (trace[0] != nullptr) trace[0][traceCount] = gain[0];
                if (trace[1] != nullptr) trace[1][traceCount] = gain[1];
                ++traceCount;
            }
        }
    }
}
