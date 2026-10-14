#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/ShapeModel.h"
#include "Parameters.h"
#include <juce_dsp/juce_dsp.h>

namespace tleet::shape
{
    // TLEET // SHAPE: passive program EQ, Pultec EQP-1A DNA (MODEL_SPEC.md).
    // Buses: main stereo in == out, or mono == mono. The whole wet path
    // (transformers, passive network, make-up amplifier) runs at the internal
    // rate (4x at 44.1 / 48 kHz, 2x at 88.2 / 96 kHz, 1x from 176.4 kHz) inside
    // one linear-phase FIR half-band up/down pair; the latency is reported and
    // MIX and Bypass are time-aligned.
    //
    // Signal flow:
    //   in ─► [internal rate: × DRIVE ─► input transformer ─► passive network (EQ IN) ─►
    //          make-up amplifier ─► output transformer ─► × 1/DRIVE] ─►
    //   MIX (with the latency-aligned plugin input) ─► OUTPUT ─► Bypass ─► out
    class ShapeProcessor : public TleetProcessorBase
    {
    public:
        ShapeProcessor();
        ~ShapeProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // ---- response curve (MODEL_SPEC 4). The settled settings of the
        // parameters, and the passive EQ's magnitude exactly as the audio path
        // runs it: eqResponseDb (hz, getEqSettings(), getHostSampleRate()).
        EqSettings getEqSettings() const noexcept;
        double getEqResponseDb (double hz) const noexcept { return eqResponseDb (hz, getEqSettings(), getHostSampleRate()); }

        double getHostSampleRate() const noexcept { return model.getHostRate(); }
        double getInternalSampleRate() const noexcept { return model.getInternalRate(); }
        int getOversamplingFactor() const noexcept { return 1 << osLog2; }

        // ---- test hooks
        ShapeModel& getModelForTesting() noexcept { return model; }

    private:
        void pushControlsToModel() noexcept;
        void processChunk (juce::AudioBuffer<float>& mainBuffer, int start, int numSamples, int numChannels);

        struct Params
        {
            std::atomic<float>* lfFreq;
            std::atomic<float>* lfBoost;
            std::atomic<float>* lfAtten;
            std::atomic<float>* hfFreq;
            std::atomic<float>* hfBoost;
            std::atomic<float>* hfBandwidth;
            std::atomic<float>* hfAttenFreq;
            std::atomic<float>* hfAtten;
            std::atomic<float>* drive;
            std::atomic<float>* character;
            std::atomic<float>* mix;
            std::atomic<float>* output;
            std::atomic<float>* eqIn;
            std::atomic<float>* bypass;
        } p {};

        ShapeModel model;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        int osLog2 = 2;
        dsp::LatencyAlignedDry dry;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        juce::SmoothedValue<float> mixSmoothed;
        float mixRounded = 1.0f, mixRound = 0.01f, outRounded = 1.0f;
        dsp::Ramp bypassRamp;
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeProcessor)
    };
}
