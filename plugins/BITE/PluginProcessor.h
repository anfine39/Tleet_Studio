#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/BiteModel.h"
#include <juce_dsp/juce_dsp.h>

namespace tleet::bite
{
    class BiteProcessor : public TleetProcessorBase
    {
    public:
        BiteProcessor();
        ~BiteProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // Meter hand-off for the editor (gain reduction comes from the FET gain itself).
        dsp::MeterSource grMeter;

        static constexpr int oversamplingFactorLog2 = 1; // 2x around the FET and amplifier stages

    private:
        void pushControlsToModel() noexcept;
        void processChunk (juce::AudioBuffer<float>&, int start, int numSamples, int numChannels);

        struct Params
        {
            std::atomic<float>* input;
            std::atomic<float>* output;
            std::atomic<float>* attack;
            std::atomic<float>* release;
            std::atomic<float>* ratio;
            std::atomic<float>* mix;
            std::atomic<float>* character;
            std::atomic<float>* bypass;
        } p {};

        BiteModel model;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        dsp::LatencyAlignedDry dry;
        juce::SmoothedValue<float> mixSmoothed;
        dsp::Ramp bypassRamp;
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BiteProcessor)
    };
}
