#pragma once

#include "dsp/DspUtils.h"
#include <array>

namespace tleet::bite
{
    // FET limiting amplifier model (1176LN DNA). See plugins/BITE/MODEL_SPEC.md
    // for the reasoning and provenance of every block and constant.
    //
    //   in -> Input gain -> input transformer (HP) -> FET shunt attenuator
    //      -> line amplifier -> output transformer -> Output gain
    //   FET output -> [detector tap: feedback sidechain, ratio network]
    //      -> rectifier -> attack/release capacitor -> FET gate
    //
    // Runs at the oversampled rate. Stereo is processed with one shared
    // detector (max of both channels), like a linked hardware pair, so the
    // image never shifts under compression.
    class BiteModel
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int numRatios = 5;      // 4, 8, 12, 20, ALL
        static constexpr int numCharacters = 3;  // Clean, TLEET, Burn

        void prepare (double sampleRateHz);
        void reset();

        // Control inputs (call per block). Gains, the ratio network and the
        // character voicing are smoothed per sample; attack and release act on
        // the next sample, like turning the hardware pots (no hidden envelope).
        void setInputDb (float db) noexcept           { inputTarget = dsp::dbToGain (db); }
        void setOutputDb (float db) noexcept          { outputTarget = dsp::dbToGain (db); }
        void setAttackMicroseconds (float us) noexcept { attackS = std::clamp (us, 1.0f, 100000.0f) * 1.0e-6f; }
        void setReleaseMilliseconds (float ms) noexcept { releaseS = std::clamp (ms, 1.0f, 100000.0f) * 1.0e-3f; }
        void setRatio (int index) noexcept            { ratioIndex = std::clamp (index, 0, numRatios - 1); }
        void setCharacter (int index) noexcept        { characterIndex = std::clamp (index, 0, numCharacters - 1); }

        // Processes `numSamples` of `numChannels` channels in place.
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        // Lowest small-signal gain of the FET attenuator since the last call
        // (linear, 0..1]. GR telemetry is -20*log10 of this value.
        float takeMinGain() noexcept { const float g = minGainSinceRead; minGainSinceRead = 1.0f; return g; }
        float getCurrentGain() const noexcept { return gain; }
        float getGainReductionDb() const noexcept { return grDb; }

        // ---- Static laws, shared with the tests and MODEL_SPEC.md

        // Ratio network: feedback law GR = slopeMinus1 * knee(out - threshold).
        // In ALL the GR-amp bias shift lowers that threshold as the drive into
        // the unit rises (bias = biasMaxDb * tanh(biasSlope * knee(in - biasThreshold) / biasMaxDb)),
        // which is what makes the output fall while the input rises.
        struct RatioLaw
        {
            float slopeMinus1;       // R - 1 of the feedback law
            float thresholdDb;       // knee centre, dBFS peak at the FET output (Input 0 dB)
            float kneeDb;            // knee width on the output-referred overshoot
            float biasSlope;         // threshold drop per dB of drive (ALL only)
            float biasThresholdDb;   // drive level where the bias starts to move, dBFS peak at the FET input
            float biasMaxDb;         // largest threshold drop
            float attackScale;       // multiplies the Attack knob time
            float releaseScale;      // multiplies the Release knob time
            float programDependence; // release stretch after sustained reduction
            float fetEven;           // uncancelled Vds term of the FET (2nd harmonic)
            float fetOdd;            // Vds^2 term (3rd harmonic)
        };

        // Character voicing: how hard the FET and the amplifiers are driven.
        struct CharacterLaw
        {
            float fetDrive;  // scales the signal seen by the FET nonlinearity
            float ampDrive;  // line amplifier (class A) drive
            float ampBias;   // its asymmetry (even harmonics)
            float xfmrDrive; // output transformer low-frequency saturation
        };

        static const RatioLaw& ratioLaw (int index) noexcept;
        static const CharacterLaw& characterLaw (int index) noexcept;

        // Quadratic soft knee: 0 below -w/2, x above +w/2.
        static float softKnee (float x, float w) noexcept
        {
            if (x <= -0.5f * w) return 0.0f;
            if (x >=  0.5f * w) return x;
            const float t = x + 0.5f * w;
            return t * t / (2.0f * w);
        }

        static float softKneeSlope (float x, float w) noexcept
        {
            if (x <= -0.5f * w) return 0.0f;
            if (x >=  0.5f * w) return 1.0f;
            return (x + 0.5f * w) / w;
        }

        // Class A line amplifier: asymmetric tanh normalised to unity
        // small-signal gain (same family as the ASH stages).
        static float ampStage (float x, float drive, float bias) noexcept
        {
            const float tb = std::tanh (drive * bias);
            const float norm = drive * (1.0f - tb * tb);
            return (std::tanh (drive * (x + bias)) - tb) / norm;
        }

        // FET attenuator: a voltage divider whose shunt leg is the FET channel.
        // `smallSignalGain` is set by the gate; the channel conductance also
        // depends on the voltage across it, so distortion grows with GR.
        static float fetStage (float x, float smallSignalGain, float even, float odd, float drive) noexcept
        {
            const float k = 1.0f / smallSignalGain - 1.0f;   // Rseries * Gchannel
            float v = x * smallSignalGain * drive * (1.0f / fetReference);
            v = v / std::sqrt (1.0f + v * v * (1.0f / 16.0f)); // channel saturates, |v| < 4
            const float conductance = std::max (0.2f, 1.0f + even * v + odd * v * v);
            return x / (1.0f + k * conductance);
        }

        // FET operating level that the nonlinearity is normalised to (the 4:1 knee).
        static constexpr float fetReference = 0.17783f; // -15 dBFS

        static constexpr float maxGainReductionDb = 80.0f;

    private:
        double fs = 96000.0;

        // controls
        float inputGain = 1.0f, inputTarget = 1.0f;
        float outputGain = 1.0f, outputTarget = 1.0f;
        float attackS = 250.0e-6f, releaseS = 0.3f;
        int ratioIndex = 0, characterIndex = 1;
        float controlSmooth = 0.001f;

        RatioLaw law {};       // smoothed copy of ratioLaw (ratioIndex)
        CharacterLaw voice {}; // smoothed copy of characterLaw (characterIndex)

        // detector / FET gate state
        float grDb = 0.0f;       // capacitor voltage, expressed as gain reduction in dB
        float memory = 0.0f;     // sustained-reduction memory (program-dependent release)
        float gain = 1.0f;       // current small-signal FET gain
        float minGainSinceRead = 1.0f;
        float aMemory = 0.0f;

        std::array<dsp::DCBlocker, maxChannels> dcIn, dcOut;
        std::array<float, maxChannels> xfmrLow {};
        float aXfmr = 0.0f;
    };
}
