#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/DoubleEngine.h"
#include "dsp/DoubleTelemetry.h"
#include <juce_dsp/juce_dsp.h>

namespace tleet::dbl
{
    // TLEET // DOUBLE: stereo doubler / micro-pitch / ADT (MODEL_SPEC.md).
    // Zero latency: the voices are read taps on a delay ring, so the dry path
    // needs no compensation and the plugin reports 0 samples.
    class DoubleProcessor : public TleetProcessorBase
    {
    public:
        DoubleProcessor();
        ~DoubleProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        // longest voice (GHOST, Delay 40: 56 ms + window/2 + drift) plus the GHOST all-pass ring-out (~0.1 s to -60 dB)
        double getTailLengthSeconds() const override { return 0.2; }

        // Rate the wet filters run at (the host rate: DOUBLE does not oversample).
        double getWetSampleRate() const noexcept { return engine.getSampleRate(); }

        // Voice display hand-off (audio thread writes, editor reads).
        DoubleTelemetry telemetry;

        // ---- test hooks
        const DoubleEngine& getEngine() const noexcept { return engine; }
        void setSoloVoiceForTesting (int slot) noexcept { soloVoice.store (slot); }

    private:
        DoubleEngine::Settings readSettings() const noexcept;
        void processChunk (juce::AudioBuffer<float>&, int start, int numSamples, int numChannels);

        struct Params
        {
            std::atomic<float>* voices;
            std::atomic<float>* mode;
            std::atomic<float>* detune;
            std::atomic<float>* delay;
            std::atomic<float>* humanize;
            std::atomic<float>* width;
            std::atomic<float>* hpf;
            std::atomic<float>* lpf;
            std::atomic<float>* monoSafe;
            std::atomic<float>* autoLevel;
            std::atomic<float>* mix;
            std::atomic<float>* output;
            std::atomic<float>* bypass;
        } p {};

        DoubleEngine engine;
        dsp::LatencyAlignedDry dry;
        juce::AudioBuffer<float> wetBuffer;
        juce::SmoothedValue<float> mixSmoothed, outputSmoothed;
        dsp::Ramp bypassRamp;
        std::atomic<int> soloVoice { -1 };
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoubleProcessor)
    };
}
