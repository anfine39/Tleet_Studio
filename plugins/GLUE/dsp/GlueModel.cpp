#include "GlueModel.h"
#include <limits>

namespace tleet::glue
{
    // ---- fixed constants (MODEL_SPEC.md sections 2-6)
    static constexpr double controlSmoothS = 0.020;  // threshold, makeup, CRUSH, character glide        [APPROXIMATION]
    static constexpr double lawSmoothS     = 0.010;  // ratio law glide (slope, knee, threshold offset)   [APPROXIMATION]
    static constexpr double extBlendS      = 0.010;  // internal <-> external detector crossfade           [TLEET_EXTENSION]
    static constexpr float  inputHpHz      = 1.0f;   // input coupling (no DC into VCA or detector)       [APPROXIMATION]
    static constexpr float  outputHpHz     = 2.0f;   // removes the offset of the asymmetric stages       [APPROXIMATION]
    static constexpr double kneeDiodeDb    = 3.24;   // diode onset softness, in dB of control voltage    [APPROXIMATION]
    static constexpr double depthFullDb    = 20.0;   // GR at which the VCA term reaches full depth       [APPROXIMATION]
    static constexpr double calibrationDb  = 10.0;   // ratio label = secant over the first 10 dB         [APPROXIMATION]

    // Slope that makes the hard-knee law pass through (T, T) and
    // (T + 10, T + 10/R): the label is the secant ratio over the first 10 dB
    // above the ratio's threshold. With c = K (u - 1) and u = 10^(y/20):
    // K = (10 - 10/R) / (10^((10/R)/20) - 1).
    // One-pole glide that lands exactly on its target (no denormal tail, and a
    // settled value is bit-identical to the law it glided to).
    template <typename T>
    static inline void glide (T& v, T target, T coeff) noexcept
    {
        const T d = target - v;
        v = std::abs (d) <= (T) 1.0e-6 * ((T) 1 + std::abs (target)) ? target : v + d * coeff;
    }

    static double calibratedSlope (double r)
    {
        const double y = calibrationDb / r;
        return (calibrationDb - y) / (std::pow (10.0, y / 20.0) - 1.0);
    }

    static GlueModel::RatioLaw makeRatioLaw (double ratio, double offsetDb)
    {
        const double k = calibratedSlope (ratio);
        // The diode's onset is a fixed voltage span after the slope amplifier,
        // so its width on the detector level shrinks as the slope rises.
        return { (float) ratio, (float) offsetDb, (float) k, (float) (kneeDiodeDb / k), false };
    }

    static GlueModel::RatioLaw makeLimiterLaw()
    {
        // The loop constants are the 10:1 ones: they only matter while a
        // switch glides between the loop law and the limiter law.
        auto l = makeRatioLaw (10.0, 0.0);
        l.ratio = std::numeric_limits<float>::infinity();
        l.limiter = true;
        return l;
    }

    const GlueModel::RatioLaw& GlueModel::ratioLaw (int index) noexcept
    {
        //                                         ratio  threshold offset (dB)
        static const RatioLaw laws[numRatios] = { makeRatioLaw ( 1.5, -2.5),  // TLEET extension
                                                  makeRatioLaw ( 2.0, -2.0),
                                                  makeRatioLaw ( 4.0, -1.0),
                                                  makeRatioLaw (10.0,  0.0),
                                                  makeLimiterLaw() };         // TLEET extension
        return laws[std::clamp (index, 0, numRatios - 1)];
    }

    const GlueModel::CharacterLaw& GlueModel::characterLaw (int index) noexcept
    {
        // The output stage's bias is negative so its 2nd harmonic has the
        // same sign as the VCA's (+x^2): the two stages add, never cancel.
        //                                     vcaEven  vcaOdd   floor  ampDrive ampBias
        static const CharacterLaw voices[numCharacters] = {
            { 0.0f,    0.0f,    0.0f,  0.0f,  0.0f   },  // Clean: linear VCA and output stage
            { 0.004f,  0.0015f, 0.15f, 0.12f, -0.08f },  // TLEET: subtle, grows with GR and level
            { 0.020f,  0.008f,  0.30f, 0.55f, -0.20f },  // Burn: driven VCA and output stage
        };
        return voices[std::clamp (index, 0, numCharacters - 1)];
    }

    float GlueModel::attackSeconds (int index) noexcept
    {
        static constexpr float t[numAttacks] = { 0.0001f, 0.0003f, 0.001f, 0.003f, 0.010f, 0.030f };
        return t[std::clamp (index, 0, numAttacks - 1)];
    }

    float GlueModel::releaseSeconds (int index) noexcept
    {
        static constexpr float t[numReleases] = { 0.1f, 0.3f, 0.6f, 1.2f, (float) autoMainS };
        return t[std::clamp (index, 0, numReleases - 1)];
    }

    double GlueModel::steadyStateFraction (int attack, int release, float crushAmount) noexcept
    {
        const double scale = 1.0 - crushReleaseScale * std::clamp (crushAmount, 0.0f, 1.0f);
        const double ta = attackSeconds (attack);
        const double tr = (release == autoRelease ? autoMainS : (double) releaseSeconds (release)) * scale;
        return tr / (ta + tr);
    }

    double GlueModel::staticGainReductionDb (double detectorPeakDbfs, double thresholdDbfs, int ratio,
                                             int attack, int release, float crushAmount) noexcept
    {
        const auto& law = ratioLaw (ratio);
        const double cr = std::clamp (crushAmount, 0.0f, 1.0f);
        if (law.limiter)
            return softKnee (detectorPeakDbfs - thresholdDbfs + crushScGainDb * cr - law.thresholdOffsetDb, limiterKneeDb);

        const double a = std::pow (10.0, (detectorPeakDbfs - thresholdDbfs + crushScGainDb * cr - law.thresholdOffsetDb) / 20.0);
        const double slope = law.slopeDb * (1.0 + crushSlopeGain * cr);
        const double rho = steadyStateFraction (attack, release, (float) cr);

        // c - rho * T(c) is increasing in c: bisection.
        double lo = 0.0, hi = 200.0;
        for (int i = 0; i < 100; ++i)
        {
            const double c = 0.5 * (lo + hi);
            const double f = c - rho * slopeTarget (a * std::exp (-ln10Over20 * c), slope, law.knee);
            (f > 0.0 ? hi : lo) = c;
        }
        return 0.5 * (lo + hi);
    }

    void GlueModel::prepare (double sampleRateHz)
    {
        fs = sampleRateHz;
        cs = dsp::onePoleCoeff (controlSmoothS, fs);
        csLaw = dsp::onePoleCoeff (lawSmoothS, fs);
        extStep = 1.0 / std::max (1.0, extBlendS * fs);
        filterControl.prepare (fs);

        for (auto& d : dcIn)  d.prepare (fs, inputHpHz);
        for (auto& d : dcKey) d.prepare (fs, inputHpHz);
        for (auto& d : dcOut) d.prepare (fs, outputHpHz);
        reset();
    }

    void GlueModel::reset()
    {
        capDb = slowDb = 0.0;
        gain = 1.0f;
        minGainSinceRead = 1.0f;

        thresholdDb = thresholdTarget;
        crush = crushTarget;
        makeupGain = makeupTarget;
        const auto& law = ratioLaw (ratioIndex);
        lawThreshold = std::pow (10.0, law.thresholdOffsetDb / 20.0);
        lawSlope = law.slopeDb;
        lawKnee = law.knee;
        limiterWeight = law.limiter ? 1.0 : 0.0;
        voice = characterLaw (characterIndex);
        extBlend = useExternal ? 1.0 : 0.0;
        scGainThreshold = 1.0e9;

        filterControl.setTargets (hpfHz, darkTarget);
        filterControl.snap();
        for (auto& f : filterInt) f.reset();
        for (auto& f : filterExt) f.reset();
        for (auto& d : dcIn)  d.reset();
        for (auto& d : dcKey) d.reset();
        for (auto& d : dcOut) d.reset();

        controlCountdown = 0;
        controlUpdate();
    }

    void GlueModel::controlUpdate() noexcept
    {
        filterControl.setTargets (hpfHz, darkTarget);
        filterControl.control();

        // Timing network. k = exp(h / tau) - 1 makes a free decay exact:
        // c[n] = c[n-1] / (1 + k) = c[n-1] exp(-h / tau).
        const double h = 1.0 / fs;
        const double scale = 1.0 - crushReleaseScale * crush;
        ka = std::expm1 (h / attackSeconds (attackIndex));
        networkAuto = releaseIndex == autoRelease;
        if (networkAuto)
        {
            k1  = std::expm1 (h / (autoMainS * scale));
            k2a = std::expm1 (h / (autoCouplingS * scale));
            kb  = std::expm1 (h / (autoSlowS * scale));
        }
        else
        {
            kr = std::expm1 (h / (releaseSeconds (releaseIndex) * scale));
        }
    }

    void GlueModel::releaseNetwork (double& L1, double& L0) const noexcept
    {
        // Release network alone is linear in the new capacitor value:
        // leak(c) = L1 c - L0 (backward Euler, see MODEL_SPEC 3.2/3.3).
        if (networkAuto)
        {
            const double inv = 1.0 / (1.0 + kb);
            L1 = 1.0 + k1 + k2a * inv;
            L0 = capDb + k2a * inv * slowDb;
        }
        else
        {
            L1 = 1.0 + kr;
            L0 = capDb;
        }
    }

    double GlueModel::solveLimiter (double levelOverThreshold) const noexcept
    {
        // Infinity: the required GR is the soft-kneed overshoot in dB. The
        // capacitor charges toward it with exactly the attack time constant
        // (no loop acceleration, no leak while charging) and releases through
        // the same release network as the other ratios.
        double L1, L0;
        releaseNetwork (L1, L0);
        const double released = L0 / L1;
        const double target = levelOverThreshold > 1.0e-12
                                ? softKnee (20.0 * std::log10 (levelOverThreshold), limiterKneeDb) : 0.0;
        if (target <= released)
            return released;
        if (target < capDb)
            return target;                         // the diode holds the capacitor at the target
        return (capDb + ka * target) / (1.0 + ka); // exact exponential approach, tau = attack
    }

    double GlueModel::solveLoop (double a) noexcept
    {
        double L1, L0;
        releaseNetwork (L1, L0);

        double c = L0 / L1; // release only: the diode is off
        const double slope = lawSlope * (1.0 + crushSlopeGain * crush);
        const double kneeLow = 1.0 - 0.5 * lawKnee;

        // The diode conducts only while the slope target exceeds the capacitor.
        if (a > kneeLow && slopeTarget (a * std::exp (-ln10Over20 * c), slope, lawKnee) > c)
        {
            // Attack: (L1 + ka) c - L0 - ka T(c) = 0. T is convex and
            // decreasing in c, so Newton from the release solution (left of
            // the root) converges monotonically.
            const double l = L1 + ka;
            for (int it = 0; it < 16; ++it)
            {
                const double u = a * std::exp (-ln10Over20 * c);
                const double x = u - 1.0;
                const double t = slope * softKnee (x, lawKnee);
                const double dt = -slope * softKneeSlope (x, lawKnee) * ln10Over20 * u;
                const double f = l * c - L0 - ka * t;
                const double step = f / (l - ka * dt);
                c -= step;
                if (std::abs (step) < 1.0e-7)
                    break;
            }
        }

        c = std::clamp (c, 0.0, maxGainReductionDb);
        if (! std::isfinite (c))
            c = 0.0;
        if (c < 1.0e-9)
            c = 0.0; // no denormal tail on the capacitor
        return c;
    }

    void GlueModel::process (float* const* audio, int numAudio, const float* const* key, int numKey, int numSamples) noexcept
    {
        numAudio = std::clamp (numAudio, 0, maxChannels);
        numKey = key != nullptr ? std::clamp (numKey, 0, maxChannels) : 0;
        const bool haveKey = numKey > 0;
        const double blendTarget = (useExternal && haveKey) ? 1.0 : 0.0;

        const auto& lawTarget = ratioLaw (ratioIndex);
        const double lawThresholdTarget = std::pow (10.0, lawTarget.thresholdOffsetDb / 20.0);
        const CharacterLaw& voiceTarget = characterLaw (characterIndex);
        const float csf = (float) cs;

        for (int i = 0; i < numSamples; ++i)
        {
            if (--controlCountdown <= 0)
            {
                controlCountdown = DetectorFilterControl::controlInterval;
                controlUpdate();
            }

            // ---- control smoothing (switches glide through intermediate laws)
            glide (thresholdDb, (double) thresholdTarget, cs);
            glide (crush, (double) crushTarget, cs);
            glide (makeupGain, makeupTarget, csf);
            glide (lawThreshold, lawThresholdTarget, csLaw);
            glide (lawSlope, (double) lawTarget.slopeDb, csLaw);
            glide (lawKnee, (double) lawTarget.knee, csLaw);
            glide (limiterWeight, lawTarget.limiter ? 1.0 : 0.0, csLaw);
            glide (voice.vcaEven, voiceTarget.vcaEven, csf);
            glide (voice.vcaOdd, voiceTarget.vcaOdd, csf);
            glide (voice.vcaFloor, voiceTarget.vcaFloor, csf);
            glide (voice.ampDrive, voiceTarget.ampDrive, csf);
            glide (voice.ampBias, voiceTarget.ampBias, csf);
            if (extBlend < blendTarget)      extBlend = std::min (blendTarget, extBlend + extStep);
            else if (extBlend > blendTarget) extBlend = std::max (blendTarget, extBlend - extStep);

            // Side-chain VCA gain from the threshold (and CRUSH sensitivity).
            const double scDb = crushScGainDb * crush - thresholdDb;
            if (std::abs (scDb - scGainThreshold) > 1.0e-9)
            {
                scGainThreshold = scDb;
                scGain = std::exp (ln10Over20 * scDb);
            }

            // ---- input coupling and detector feeds
            float x[maxChannels] {};
            double rectInt = 0.0, rectExt = 0.0;
            for (int ch = 0; ch < numAudio; ++ch)
            {
                x[ch] = dcIn[(size_t) ch].process (dsp::sanitize (audio[ch][i]));
                // per-channel full-wave rectifier; the largest channel wins (dominant)
                rectInt = std::max (rectInt, std::abs (filterInt[(size_t) ch].process ((double) x[ch], filterControl)));
            }
            for (int k = 0; k < numKey; ++k)
            {
                const float kx = dcKey[(size_t) k].process (dsp::sanitize (key[k][i]));
                rectExt = std::max (rectExt, std::abs (filterExt[(size_t) k].process ((double) kx, filterControl)));
            }
            const double rect = rectInt + extBlend * (rectExt - rectInt);

            // ---- loop: side-chain VCA (threshold gain x applied gain) -> slope -> network.
            // Infinity uses the limiter law on the same detector and network;
            // a ratio switch to or from it crossfades the two solutions.
            double c;
            if (limiterWeight <= 0.0)
                c = solveLoop (rect * scGain / lawThreshold);
            else if (limiterWeight >= 1.0)
                c = std::clamp (solveLimiter (rect * scGain), 0.0, maxGainReductionDb);
            else
            {
                const double cl = solveLoop (rect * scGain / lawThreshold);
                const double cf = std::clamp (solveLimiter (rect * scGain), 0.0, maxGainReductionDb);
                c = cl + limiterWeight * (cf - cl);
            }
            if (! std::isfinite (c) || c < 1.0e-9)
                c = 0.0;
            slowDb = networkAuto ? (slowDb + kb * c) / (1.0 + kb) : c; // fixed: C2 follows (seamless switch to Auto)
            if (slowDb < 1.0e-9)
                slowDb = 0.0;
            capDb = c;

            gain = (float) std::exp (-ln10Over20 * c);
            minGainSinceRead = std::min (minGainSinceRead, gain);
            if (trace != nullptr && traceCount < traceCapacity)
                trace[traceCount++] = gain;

            // ---- audio path: main VCAs (gain + makeup on the same control
            // voltage), output stage, DC blocker
            const float drive = 1.0f + crushDrive * (float) crush;
            const float depth = voice.vcaFloor + (1.0f - voice.vcaFloor) * (float) std::min (1.0, c / depthFullDb);
            const float even = voice.vcaEven * drive, odd = voice.vcaOdd * drive;
            const float G = gain * makeupGain;
            for (int ch = 0; ch < numAudio; ++ch)
            {
                float y = vcaStage (x[ch], G, even, odd, depth);
                y = ampStage (y, voice.ampDrive * drive, voice.ampBias);
                audio[ch][i] = dcOut[(size_t) ch].process (y);
            }
        }
    }
}
