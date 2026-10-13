#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/MeterSource.h"
#include "dsp/SpectrumTap.h"
#include "dsp/VeilModel.h"
#include "Parameters.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace tleet::veil
{
    // TLEET // VEIL: stereo variable-mu compressor, Manley Variable Mu DNA
    // (MODEL_SPEC.md). Buses: main stereo in == out (or mono == mono), plus an
    // optional "Sidechain" input (mono or stereo, disabled by default) that
    // feeds the detectors when SIDECHAIN EXT is on. The wet path runs at 2x
    // (linear-phase FIR half-band); the latency is reported and both blends
    // (MIX, DRY/WET) and bypass are time-aligned.
    //
    // Signal flow (host rate unless noted):
    //   in ─► INPUT gain ─► HPF ─► coupling HP ─►[2x: input tube stage ─► vari-mu element ─► MIX ─► output stage]─►
    //       LF weight ─► OUTPUT gain ─► DRY/WET (with the latency-aligned plugin input) ─► bypass ─► out ─► spectrum tap
    class VeilProcessor : public TleetProcessorBase
    {
    public:
        VeilProcessor();
        ~VeilProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        static constexpr int oversamplingFactorLog2 = 1; // 2x for the tube stages and the gain element

        // ---- meters (audio thread writes once per processed chunk, UI reads lock-free).
        // Index 0 = left, 1 = right; with a mono main bus both carry the single channel.
        //   inputMeters[ch]  : the plugin input (before INPUT), pushLevel(sum of squares, n, peak)
        //   outputMeters[ch] : the plugin output (after DRY/WET and bypass)
        //   grMeters[ch]     : pushGainReduction(deepest GR the channel's gain element
        //                      actually applied in the chunk, dB, positive = reduction)
        // The METER selector never changes what is measured here; the editor picks
        // which source to draw. VU ballistics run in the UI (dsp::VUBallistics);
        // calibration: params::vuDb (0 VU = a sine at -18 dBFS).
        std::array<dsp::MeterSource, 2> inputMeters, outputMeters, grMeters;

        // Deepest actual applied GR (dB, >= 0) of `channel` since the previous
        // call for that channel; the latest value if no audio was processed since.
        float takeGrDb (int channel) noexcept;
        // GR of the last processed sample (dB).
        float getCurrentGrDb (int channel) const noexcept { return currentGr[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed); }

        // METER selection (display only).
        params::MeterMode getMeterMode() const noexcept
        {
            return (params::MeterMode) juce::jlimit (0, 2, juce::roundToInt (p.meterMode->load()));
        }

        // True while the detectors are fed by the external key: SIDECHAIN EXT is
        // on AND the host has enabled the Sidechain bus with at least one
        // channel. Otherwise the internal key is in use (safe fallback).
        bool isUsingExternalSidechain() const noexcept { return usingExternal.load (std::memory_order_relaxed); }

        // Rates for the response functions: hpfResponseDb (hz, hpf, getHostSampleRate())
        // and hpfResponseDb (hz, scHpf, getDetectorSampleRate()) draw the two filters.
        double getHostSampleRate() const noexcept { return model.getHostRate(); }
        double getDetectorSampleRate() const noexcept { return model.getDetectorRate(); }

        // Spectrum of the final plugin output (after DRY/WET, also in bypass).
        // Inactive until the editor calls spectrum.setActive (true).
        dsp::SpectrumTap spectrum;

        // ---- test hooks
        VeilModel& getModelForTesting() noexcept { return model; }

    private:
        void pushControlsToModel() noexcept;
        void processChunk (juce::AudioBuffer<float>& mainBuffer, const juce::AudioBuffer<float>* keyBuffer,
                           int start, int numSamples, int numChannels, int numKey);

        struct Params
        {
            std::atomic<float>* input;
            std::atomic<float>* threshold;
            std::atomic<float>* attack;
            std::atomic<float>* recovery;
            std::atomic<float>* compress;
            std::atomic<float>* mix;
            std::atomic<float>* output;
            std::atomic<float>* tubeDrive;
            std::atomic<float>* harmonics;
            std::atomic<float>* saturation;
            std::atomic<float>* dryWet;
            std::atomic<float>* hpf;
            std::atomic<float>* scHpf;
            std::atomic<float>* character;
            std::atomic<float>* scExternal;
            std::atomic<float>* stereoLink;
            std::atomic<float>* meterMode;
            std::atomic<float>* bypass;
        } p {};

        VeilModel model;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling, keyOversampling;
        juce::AudioBuffer<float> keyScratch;
        dsp::LatencyAlignedDry dry;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputSmoothed, outputSmoothed;
        juce::SmoothedValue<float> dryWetSmoothed;
        dsp::Ramp bypassRamp;
        std::atomic<bool> usingExternal { false };
        std::array<std::atomic<float>, 2> currentGr { 0.0f, 0.0f };
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VeilProcessor)
    };
}
