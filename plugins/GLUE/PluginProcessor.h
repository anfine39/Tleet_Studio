#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/GlueModel.h"
#include <juce_dsp/juce_dsp.h>

namespace tleet::glue
{
    // TLEET // GLUE: stereo VCA bus compressor, SSL G-Series bus compressor DNA
    // (MODEL_SPEC.md). Buses: main stereo in == out (or mono == mono), plus an
    // optional "Sidechain" input (mono or stereo, disabled by default) that
    // feeds the detector when External SC is on.
    class GlueProcessor : public TleetProcessorBase
    {
    public:
        GlueProcessor();
        ~GlueProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // Gain-reduction hand-off for the GR LED meter: -20*log10 of the VCA
        // gain actually applied (stereo is fully linked: one value is exact).
        dsp::MeterSource grMeter;

        // True while the detector is fed by the external key: External SC is
        // on AND the host has enabled the Sidechain bus with at least one
        // channel. Otherwise the internal detector is in use (safe fallback).
        bool isUsingExternalSidechain() const noexcept { return usingExternal.load (std::memory_order_relaxed); }

        // Rate the detector filters run at (2x the host rate): pass it to
        // detectorResponseDb() to draw the SC HPF / DARK curve.
        double getDetectorSampleRate() const noexcept { return model.getSampleRate(); }

        static constexpr int oversamplingFactorLog2 = 1; // 2x around the VCA and output stage

        // ---- test hooks
        GlueModel& getModelForTesting() noexcept { return model; }

    private:
        void pushControlsToModel() noexcept;
        void processChunk (juce::AudioBuffer<float>& mainBuffer, const juce::AudioBuffer<float>* keyBuffer,
                           int start, int numSamples, int numChannels, int numKey);

        struct Params
        {
            std::atomic<float>* threshold;
            std::atomic<float>* attack;
            std::atomic<float>* release;
            std::atomic<float>* ratio;
            std::atomic<float>* makeup;
            std::atomic<float>* scHpf;
            std::atomic<float>* mix;
            std::atomic<float>* character;
            std::atomic<float>* dark;
            std::atomic<float>* crush;
            std::atomic<float>* output;
            std::atomic<float>* scExternal;
            std::atomic<float>* bypass;
        } p {};

        GlueModel model;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling, keyOversampling;
        dsp::LatencyAlignedDry dry;
        juce::AudioBuffer<float> keyScratch;
        juce::SmoothedValue<float> mixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        dsp::Ramp bypassRamp;
        std::atomic<bool> usingExternal { false };
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueProcessor)
    };
}
