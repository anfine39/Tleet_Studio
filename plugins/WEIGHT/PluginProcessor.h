#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/WeightModel.h"
#include "Parameters.h"

namespace tleet::weight
{
    // TLEET // WEIGHT: stereo VCA bus compressor, API 2500 DNA (MODEL_SPEC.md).
    // Buses: main stereo in == out (or mono == mono), plus an optional
    // "Sidechain" input (mono or stereo, disabled by default) that feeds the
    // detectors when External SC is on. Zero latency (no oversampling).
    class WeightProcessor : public TleetProcessorBase
    {
    public:
        WeightProcessor();
        ~WeightProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // ---- gain-reduction telemetry, one per channel (MODEL_SPEC 7).
        // Every value is the gain reduction the channel's VCA actually
        // applied (-20 log10 g, before makeup), never an estimate. L and R
        // differ whenever the link is below 100 % and the channels differ.
        // With a mono main bus both report the single channel.
        //   grLeft.take()        -> { peakDb, rmsDb, averageDb } (resets the peak hold)
        //   grLeft.takePeakDb()  -> deepest GR since the last peak read
        //   grLeft.getRmsDb()    -> -10 log10(mean g^2) over the last 300 ms
        //   grLeft.getAverageDb()-> mean GR in dB over the last 300 ms
        GrTelemetry grLeft, grRight;

        using GrMode = params::GrMode;

        // The GR MODE selection (display only; it never reaches the DSP).
        GrMode getGrMode() const noexcept { return (GrMode) juce::jlimit (0, 2, juce::roundToInt (p.grMode->load())); }

        // The reading of `channel` (0 = left, 1 = right) in `mode`. Peak resets
        // that channel's peak hold; RMS and Average do not reset anything.
        float takeGrDb (int channel, GrMode mode) noexcept;
        float takeGrDb (int channel) noexcept { return takeGrDb (channel, getGrMode()); }
        GrTelemetry& getGrTelemetry (int channel) noexcept { return channel <= 0 ? grLeft : grRight; }

        // True while the detector is fed by the external key: External SC is
        // on AND the host has enabled the Sidechain bus with at least one
        // channel. Otherwise the internal detector is in use (safe fallback).
        bool isUsingExternalSidechain() const noexcept { return usingExternal.load (std::memory_order_relaxed); }

        // Rate the detector filters run at (the host rate): pass it to
        // scHpfResponseDb() / thrustResponseDb() to draw the curves.
        double getDetectorSampleRate() const noexcept { return model.getSampleRate(); }

        // ---- test hooks
        WeightModel& getModelForTesting() noexcept { return model; }

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
            std::atomic<float>* thrust;
            std::atomic<float>* detector;
            std::atomic<float>* tone;
            std::atomic<float>* scHpf;
            std::atomic<float>* scExternal;
            std::atomic<float>* mix;
            std::atomic<float>* output;
            std::atomic<float>* link;
            std::atomic<float>* grMode;
            std::atomic<float>* bypass;
        } p {};

        WeightModel model;
        dsp::LatencyAlignedDry dry;
        juce::SmoothedValue<float> mixSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        dsp::Ramp bypassRamp;
        std::atomic<bool> usingExternal { false };
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WeightProcessor)
    };
}
