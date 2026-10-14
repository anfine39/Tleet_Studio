#include "BiteModel.h"

namespace tleet::bite
{
    // ---- fixed constants (see MODEL_SPEC.md, sections 2-5)
    static constexpr double controlSmoothS = 0.015; // gains, ratio network and voicing glide (~45 ms to 95 %)  [APPROXIMATION]
    static constexpr double memoryS        = 0.6;   // sustained-reduction memory for program-dependent release  [APPROXIMATION]
    static constexpr float  memoryFullDb   = 20.0f; // GR at which the memory saturates                           [APPROXIMATION]
    static constexpr float  inputHpHz      = 2.0f;  // input transformer coupling (no DC through the unit)        [APPROXIMATION]
    static constexpr float  outputHpHz     = 4.0f;  // removes the offset made by the asymmetric stages           [APPROXIMATION]
    static constexpr double xfmrLowHz      = 80.0;  // output transformer: band whose flux saturates              [APPROXIMATION]
    static constexpr float  biasKneeDb     = 6.0f;  // ALL: softness of the onset of the bias shift              [APPROXIMATION]

    // Knee widths are chosen on the input axis and converted to the
    // output-referred overshoot the feedback detector sees: an input span of
    // `in` dB maps to 2 * in / (R + 1) dB of output overshoot.
    static constexpr float outputKnee (float inputKneeDb, float ratio) { return 2.0f * inputKneeDb / (ratio + 1.0f); }

    const BiteModel::RatioLaw& BiteModel::ratioLaw (int index) noexcept
    {
        // slope-1  thresh  knee                      bias  biasThr biasMax atk   rel   progDep fetEven fetOdd
        static const RatioLaw laws[numRatios] = {
            {  3.0f, -15.0f, outputKnee (12.0f,  4.0f), 0.0f, -9.0f,  6.0f,  1.0f, 1.0f, 0.5f,  0.012f, 0.0010f }, //  4:1
            {  7.0f, -14.0f, outputKnee (10.0f,  8.0f), 0.0f, -9.0f,  6.0f,  1.0f, 1.0f, 0.5f,  0.012f, 0.0010f }, //  8:1
            { 11.0f, -13.0f, outputKnee ( 8.0f, 12.0f), 0.0f, -9.0f,  6.0f,  1.0f, 1.0f, 0.5f,  0.012f, 0.0010f }, // 12:1
            { 19.0f, -12.0f, outputKnee ( 6.0f, 20.0f), 0.0f, -9.0f,  6.0f,  1.0f, 1.0f, 0.5f,  0.012f, 0.0010f }, // 20:1
            // ALL: the paralleled ratio network shifts the GR-amp bias. Very
            // high feedback ratio, lower knee, a threshold that drops as the
            // drive rises (over-compression: output falls while input rises),
            // faster attack, quicker short release with stronger program
            // dependence, and a FET pushed off its linearised operating point.
            { 39.0f, -15.0f, outputKnee ( 4.0f, 40.0f), 0.4f, -9.0f,  6.0f,  0.5f, 0.7f, 1.5f,  0.045f, 0.0120f }, // ALL
        };
        return laws[std::clamp (index, 0, numRatios - 1)];
    }

    const BiteModel::CharacterLaw& BiteModel::characterLaw (int index) noexcept
    {
        //                                  fetDrive ampDrive ampBias xfmrDrive
        static const CharacterLaw voices[numCharacters] = {
            { 0.35f, 0.05f, 0.05f, 0.0f },  // Clean: core FET behaviour only
            { 1.00f, 0.25f, 0.12f, 0.8f },  // TLEET: default voicing
            { 2.20f, 1.20f, 0.20f, 2.0f },  // Burn: driven harder inside the model
        };
        return voices[std::clamp (index, 0, numCharacters - 1)];
    }

    void BiteModel::prepare (double sampleRateHz)
    {
        fs = sampleRateHz;
        controlSmooth = dsp::onePoleCoeff (controlSmoothS, fs);
        aMemory = dsp::onePoleCoeff (memoryS, fs);
        aXfmr = dsp::onePoleCoeff (1.0 / (2.0 * dsp::pi * xfmrLowHz), fs);

        for (auto& d : dcIn)  d.prepare (fs, inputHpHz);
        for (auto& d : dcOut) d.prepare (fs, outputHpHz);
        reset();
    }

    void BiteModel::reset()
    {
        grDb = memory = 0.0f;
        gain = 1.0f;
        minGainSinceRead = 1.0f;
        inputGain = inputTarget;
        outputGain = outputTarget;
        law = ratioLaw (ratioIndex);
        voice = characterLaw (characterIndex);
        for (auto& d : dcIn)  d.reset();
        for (auto& d : dcOut) d.reset();
        xfmrLow.fill (0.0f);
    }

    void BiteModel::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        const RatioLaw& lawTarget = ratioLaw (ratioIndex);
        const CharacterLaw& voiceTarget = characterLaw (characterIndex);
        const float cs = controlSmooth;
        const float fsf = (float) fs;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- control smoothing: discrete ratio/character switches glide
            // through intermediate laws, so switching never steps the gain.
            inputGain  += (inputTarget - inputGain) * cs;
            outputGain += (outputTarget - outputGain) * cs;

            law.slopeMinus1       += (lawTarget.slopeMinus1 - law.slopeMinus1) * cs;
            law.thresholdDb       += (lawTarget.thresholdDb - law.thresholdDb) * cs;
            law.kneeDb            += (lawTarget.kneeDb - law.kneeDb) * cs;
            law.biasSlope         += (lawTarget.biasSlope - law.biasSlope) * cs;
            law.biasThresholdDb   += (lawTarget.biasThresholdDb - law.biasThresholdDb) * cs;
            law.biasMaxDb         += (lawTarget.biasMaxDb - law.biasMaxDb) * cs;
            law.attackScale       += (lawTarget.attackScale - law.attackScale) * cs;
            law.releaseScale      += (lawTarget.releaseScale - law.releaseScale) * cs;
            law.programDependence += (lawTarget.programDependence - law.programDependence) * cs;
            law.fetEven           += (lawTarget.fetEven - law.fetEven) * cs;
            law.fetOdd            += (lawTarget.fetOdd - law.fetOdd) * cs;

            voice.fetDrive  += (voiceTarget.fetDrive - voice.fetDrive) * cs;
            voice.ampDrive  += (voiceTarget.ampDrive - voice.ampDrive) * cs;
            voice.ampBias   += (voiceTarget.ampBias - voice.ampBias) * cs;
            voice.xfmrDrive += (voiceTarget.xfmrDrive - voice.xfmrDrive) * cs;

            // ---- Input control and input transformer
            float x[maxChannels] {};
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                x[ch] = dcIn[(size_t) ch].process (dsp::sanitize (channels[ch][i]) * inputGain);
                peak = std::max (peak, std::abs (x[ch])); // linked: one detector, loudest channel
            }

            // ---- Detector. The hardware side-chain listens to the output
            // (feedback): rectified output -> capacitor -> FET gate. The output
            // depends on the gain being computed, so the loop is solved without
            // a sample of delay (Newton on the capacitor equation). That keeps
            // a 20:1 loop stable at 20 us attack and gives the exact feedback
            // static law out = T + (in - T) / R above the knee.
            const float levelDb = dsp::gainToDb (peak, -120.0f);
            const float drive = softKnee (levelDb - law.biasThresholdDb, biasKneeDb);
            const float threshold = law.thresholdDb - law.biasMaxDb * std::tanh (law.biasSlope * drive / law.biasMaxDb);
            const float c0 = grDb;
            const float target0 = law.slopeMinus1 * softKnee (levelDb - c0 - threshold, law.kneeDb);

            float k;
            if (target0 > c0)
            {
                // Attack: pick the capacitor coefficient so the closed loop
                // (above the knee) has exactly the knob's time constant.
                const float p = std::exp (-1.0f / (attackS * law.attackScale * fsf));
                k = (1.0f - p) / (1.0f + p * law.slopeMinus1);
            }
            else
            {
                // Release: open loop below the knee, time stretched after
                // sustained reduction (program dependence).
                const float t = releaseS * law.releaseScale * (1.0f + law.programDependence * memory);
                k = std::min (1.0f, 1.0f / (t * fsf)); // tau >> 1/fs, linearised exp
            }

            float c = c0;
            for (int it = 0; it < 8; ++it)
            {
                const float over = levelDb - c - threshold;
                const float f = c - c0 - k * (law.slopeMinus1 * softKnee (over, law.kneeDb) - c0);
                const float df = 1.0f + k * law.slopeMinus1 * softKneeSlope (over, law.kneeDb);
                const float step = f / df;
                c -= step;
                if (std::abs (step) < 1.0e-5f)
                    break;
            }
            c = std::clamp (c, 0.0f, maxGainReductionDb);
            if (! std::isfinite (c))
            {
                c = 0.0f;
                memory = 0.0f;
            }
            if (c < 1.0e-7f)
                c = 0.0f; // no denormal tail in the capacitor

            grDb = c;
            gain = dsp::dbToGain (-c);
            minGainSinceRead = std::min (minGainSinceRead, gain);
            // The memory follows what the programme demands (the target), so it
            // builds during sustained reduction and fades during the release.
            memory += (std::min (target0 * (1.0f / memoryFullDb), 1.0f) - memory) * aMemory;

            // ---- Audio path: FET attenuator, line amplifier, output transformer.
            const float xfmrD = std::max (voice.xfmrDrive, 1.0e-3f);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float y = fetStage (x[ch], gain, law.fetEven, law.fetOdd, voice.fetDrive);
                y = ampStage (y, voice.ampDrive, voice.ampBias);

                // Transformer core saturates on low-frequency flux: bend only
                // the low band, the rest passes linearly.
                auto& lo = xfmrLow[(size_t) ch];
                lo += (y - lo) * aXfmr;
                y += std::tanh (xfmrD * lo) / xfmrD - lo;

                y = dcOut[(size_t) ch].process (y);
                channels[ch][i] = y * outputGain;
            }
        }
    }
}
