#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/PunchModel.h"
#include <juce_dsp/juce_dsp.h>

namespace tleet::punch
{
    // TLEET // PUNCH: feed-forward VCA compressor, dbx 160A family DNA
    // (MODEL_SPEC.md). Buses: main stereo in == out, or mono == mono. No
    // external sidechain (the skin has no control for one; master spec 12).
    class PunchProcessor : public TleetProcessorBase
    {
    public:
        PunchProcessor();
        ~PunchProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // Gain-reduction hand-off for the GR LED meter: -20*log10 of the VCA
        // gain actually applied (stereo is fully linked: one value is exact).
        // Not clipped at the meter's 20 dB.
        dsp::MeterSource grMeter;

        // Detector level (dB RMS re full scale) at the end of the last block:
        // the x position of a live dot on the static curve
        // (PunchModel::gainComputerDb). Lock-free, any thread.
        float getDetectorLevelDb() const noexcept { return detectorLevel.load (std::memory_order_relaxed); }

        static constexpr int oversamplingFactorLog2 = 1; // 2x around the VCA and the GRIT stages

        // ---- test hooks
        PunchModel& getModelForTesting() noexcept { return model; }

    private:
        void pushControlsToModel() noexcept;
        void processChunk (juce::AudioBuffer<float>& mainBuffer, int start, int numSamples, int numChannels);

        struct Params
        {
            std::atomic<float>* threshold;
            std::atomic<float>* ratio;
            std::atomic<float>* overEasy;
            std::atomic<float>* grit;
            std::atomic<float>* mix;
            std::atomic<float>* output;
            std::atomic<float>* bypass;
        } p {};

        PunchModel model;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        dsp::LatencyAlignedDry dry;
        juce::SmoothedValue<float> mixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        dsp::Ramp bypassRamp;
        std::atomic<float> detectorLevel { -300.0f };
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PunchProcessor)
    };
}
