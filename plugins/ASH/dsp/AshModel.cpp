#include "AshModel.h"

namespace tleet::ash
{
    // ---- time constants (see MODEL_SPEC.md, section "Optical cell")
    static constexpr double elAttackS      = 0.0005; // EL panel follows its drive almost instantly   [APPROXIMATION]
    static constexpr double elReleaseS     = 0.008;  // EL afterglow / rectifier smoothing            [APPROXIMATION]
    static constexpr double fastAttackS    = 0.030;  // "average attack ~10 ms"                         [SOURCE_CONFIRMED value, model APPROXIMATION]
    static constexpr double fastReleaseS   = 0.050;  // first release stage, ~60 ms to 50 %             [SOURCE_CONFIRMED value, model APPROXIMATION]
    static constexpr double slowChargeS    = 0.80;   // memory builds over sustained reduction          [APPROXIMATION]
    static constexpr double historyS       = 6.0;    // long-term exposure integrator                   [APPROXIMATION]
    static constexpr float  slowReleaseMin = 0.5f;   // "0.5 to 5 s" full release depending on history [SOURCE_CONFIRMED range]
    static constexpr float  slowReleaseMax = 5.0f;
    static constexpr float  fastWeight     = 0.55f;  // split of cell conductance between fast/slow    [APPROXIMATION]
    // Limit law: light ~ drive^4, giving a static ratio of about 1 + 2*4 = 9:1
    // against 3:1 for the linear Compress law (cell gain law is 1/(1+L^2)).  [APPROXIMATION]

    void AshModel::prepare (double sampleRateHz)
    {
        fs = sampleRateHz;
        aElAttack    = dsp::onePoleCoeff (elAttackS, fs);
        aElRelease   = dsp::onePoleCoeff (elReleaseS, fs);
        aFastAttack  = dsp::onePoleCoeff (fastAttackS, fs);
        aFastRelease = dsp::onePoleCoeff (fastReleaseS, fs);
        aSlowCharge  = dsp::onePoleCoeff (slowChargeS, fs);
        aHistory     = dsp::onePoleCoeff (historyS, fs);
        controlSmooth = dsp::onePoleCoeff (0.02, fs);
        releaseSlowMin = slowReleaseMin;
        releaseSlowMax = slowReleaseMax;
        modeBlend.prepare (fs, 0.03);

        for (auto& d : dcIn)  d.prepare (fs, 4.0f);
        for (auto& d : dcOut) d.prepare (fs, 4.0f);
        reset();
    }

    void AshModel::reset()
    {
        el = fast = slow = history = 0.0f;
        gain = 1.0f;
        minGainSinceRead = 1.0f;
        scGain = scTarget;
        character = characterTarget;
        makeup = makeupTarget;
        modeBlend.snap (modeBlend.target);
        for (auto& d : dcIn)  d.reset();
        for (auto& d : dcOut) d.reset();
    }

    void AshModel::setPeakReduction (float pr) noexcept
    {
        scTarget = sidechainGainFor (std::clamp (pr, 0.0f, 100.0f));
    }

    void AshModel::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            // control smoothing (multiplicative quantities smoothed linearly is fine at 20 ms)
            scGain    += (scTarget - scGain) * controlSmooth;
            character += (characterTarget - character) * controlSmooth;
            makeup    += (makeupTarget - makeup) * controlSmooth;
            const float blend = modeBlend.next();

            // Character only scales the analogue stages; it never touches timing.
            // Squared taper: the default (0.5) gives ~0.4 % THD at -6 dBFS, the
            // full ASH voicing ~6 %, Clean ~0.01 %.  [APPROXIMATION, see MODEL_SPEC]
            const float c2 = character * character;
            const float inDrive  = 0.04f + 0.90f * c2;
            const float inBias   = 0.10f + 0.12f * character;
            const float outDrive = 0.04f + 0.60f * c2;
            const float outBias  = 0.06f + 0.06f * character;

            float detector = 0.0f;
            float post[maxChannels] {};

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float x = dsp::sanitize (channels[ch][i]);
                x = dcIn[(size_t) ch].process (tubeStage (x, inDrive, inBias));
                const float s = x * gain;            // optical attenuator
                post[ch] = s;
                detector = std::max (detector, std::abs (s)); // feedback detector after the cell
            }

            // EL panel: driven by the amplified sidechain, fast rise, short afterglow.
            const float drive = detector * scGain;
            el += (drive - el) * (drive > el ? aElAttack : aElRelease);

            // Compress and Limit are different light laws, crossfaded when switched.
            const float lightCompress = el;
            const float el2 = el * el;
            const float lightLimit = el2 * el2;
            const float light = lightCompress + (lightLimit - lightCompress) * blend;

            // Photoresistor: fast component and slow memory component.
            fast += (light - fast) * (light > fast ? aFastAttack : aFastRelease);

            const float slowReleaseS = releaseSlowMin + (releaseSlowMax - releaseSlowMin) * history;
            const float aSlowRelease = (float) (1.0 / (slowReleaseS * fs)); // tau >> 1/fs, linearised exp
            slow += (light - slow) * (light > slow ? aSlowCharge : aSlowRelease);

            const float exposure = std::min (light, 1.5f) / 1.5f;
            history += (exposure - history) * aHistory;

            const float cellLight = fastWeight * fast + (1.0f - fastWeight) * slow;
            gain = cellGain (cellLight);
            if (! std::isfinite (gain))
            {
                el = fast = slow = history = 0.0f;
                gain = 1.0f;
            }
            minGainSinceRead = std::min (minGainSinceRead, gain);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float y = post[ch] * makeup;
                y = dcOut[(size_t) ch].process (tubeStage (y, outDrive, outBias));
                channels[ch][i] = y;
            }
        }
    }
}
