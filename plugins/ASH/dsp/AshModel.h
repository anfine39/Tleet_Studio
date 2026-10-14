#pragma once

#include "dsp/DspUtils.h"
#include <array>

namespace tleet::ash
{
    // Optical levelling amplifier model (LA-2A DNA). See plugins/ASH/MODEL_SPEC.md
    // for the reasoning and provenance of every block and constant.
    //
    //   in -> input tube/transformer stage -> optical attenuator (T4 model)
    //      -> [detector tap: feedback sidechain] -> makeup gain -> output stage
    //
    // Runs at the oversampled rate. Stereo is processed with one shared
    // optical cell (linked), so the image never shifts under compression.
    class AshModel
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRateHz);
        void reset();

        // Control inputs (call per block; internally smoothed per sample).
        void setPeakReduction (float zeroToHundred) noexcept;
        void setLimitMode (bool limit) noexcept       { modeBlend.setTarget (limit ? 1.0f : 0.0f); }
        void setCharacter (float zeroToOne) noexcept  { characterTarget = zeroToOne; }
        void setMakeupDb (float db) noexcept          { makeupTarget = dsp::dbToGain (db); }

        // Processes `numSamples` of `numChannels` channels in place.
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Lowest gain of the optical attenuator since the last call (linear, 0..1].
        float takeMinGain() noexcept { const float g = minGainSinceRead; minGainSinceRead = 1.0f; return g; }
        float getCurrentGain() const noexcept { return gain; }

        // Gain-element law shared with the tests: attenuation for a given light level.
        static float cellGain (float light) noexcept { return 1.0f / (1.0f + light * light); }

        // Peak Reduction knob -> sidechain gain (linear).
        static float sidechainGainFor (float peakReduction) noexcept
        {
            return dsp::dbToGain (-10.0f + 0.6f * peakReduction);
        }

        // Analogue stage: asymmetric tanh normalised to unity small-signal gain.
        // `drive` sets how early it bends, `bias` the even-harmonic asymmetry.
        static float tubeStage (float x, float drive, float bias) noexcept
        {
            const float tb = std::tanh (drive * bias);
            const float norm = drive * (1.0f - tb * tb);
            return (std::tanh (drive * (x + bias)) - tb) / norm;
        }

    private:
        double fs = 96000.0;

        // smoothed controls
        float scGain = 1.0f, scTarget = 1.0f;
        float character = 0.5f, characterTarget = 0.5f;
        float makeup = 1.0f, makeupTarget = 1.0f;
        float controlSmooth = 0.001f;
        dsp::Ramp modeBlend;

        // optical cell state
        float el = 0.0f;          // electroluminescent panel light output
        float fast = 0.0f;        // fast photoresistor component
        float slow = 0.0f;        // slow (memory) photoresistor component
        float history = 0.0f;     // long-term exposure, sets slow release time
        float gain = 1.0f;        // current attenuator gain, fed back to the detector
        float minGainSinceRead = 1.0f;

        // coefficients
        float aElAttack = 0, aElRelease = 0, aFastAttack = 0, aFastRelease = 0,
              aSlowCharge = 0, aHistory = 0;
        float releaseSlowMin = 0, releaseSlowMax = 0;

        std::array<dsp::DCBlocker, maxChannels> dcIn, dcOut;
    };
}
