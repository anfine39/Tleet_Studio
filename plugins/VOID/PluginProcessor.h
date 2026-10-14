#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/MeterSource.h"
#include "dsp/SpectrumTap.h"
#include "dsp/VoidEngine.h"
#include "Parameters.h"
#include <array>
#include <atomic>

namespace tleet::voidrev
{
    // TLEET // VOID: original algorithmic stereo reverb (MODEL_SPEC.md). Buses:
    // main mono -> mono, stereo -> stereo, or mono -> stereo. Zero latency: the
    // dry path is the plugin input itself.
    //
    // Signal flow:
    //   in ─► sanitise ─┬─► VoidEngine (pre-delay, algorithm, filters, colour, width, duck) ─► wet ─┬─► spectrum tap
    //                   └─► dry ────────────────────────────────────────────────────────── MIX ◄────┘
    //                                                                                       │
    //                                                                  OUTPUT ─► host Bypass (dry) ─► out
    class VoidProcessor : public TleetProcessorBase
    {
    public:
        VoidProcessor();
        ~VoidProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        // Longest finite decay (60 s) plus pre-delay and the build-up. DECAY at
        // its maximum (infinite) and FREEZE ring for as long as they are on.
        double getTailLengthSeconds() const override { return 62.0; }

        // ---- meters (audio thread writes once per processed chunk, UI reads lock-free).
        // Index 0 = left, 1 = right; with a mono bus both carry the single channel.
        //   inputMeters[ch]  : the plugin input (IN)
        //   outputMeters[ch] : the final plugin output, after MIX, OUTPUT and Bypass (OUT)
        std::array<dsp::MeterSource, 2> inputMeters, outputMeters;

        // ---- reverb telemetry (MODEL_SPEC 10)
        // RMS (linear, 1 = 0 dBFS) of the early-reflection component and of the
        // late component of the wet signal since the previous call: both channels,
        // at the network output, times the DUCK gain. The previous value when no
        // audio was processed since the last call.
        float takeEarlyLevel() noexcept { return engine.takeEarlyLevel(); }
        float takeLateLevel() noexcept { return engine.takeLateLevel(); }
        // Wet output (after WIDTH and DUCK, before MIX/OUTPUT), mono sum (L + R) / 2.
        // Inactive until the editor calls spectrum.setActive (true).
        dsp::SpectrumTap spectrum;
        // True while the engine holds the tail: input injection stopped, loop lossless.
        bool isFrozen() const noexcept { return engine.isFrozen(); }
        // Magnitude (dB) of the HPF + LPF exactly as they run now (current glided
        // cutoffs, DROWN's LPF lowering included), at `hz`.
        double filterResponseDb (double hz) const noexcept { return engine.filterResponseDb (hz); }
        // SIZE, DECAY, DIFFUSION, MOD DEPTH, WIDTH and LPF after DROWN (from the
        // parameters; the base values are never changed). decaySeconds is
        // +infinity (decayInfinite true) when DECAY is at its maximum.
        EffectiveSettings getEffectiveSettings() const noexcept { return applyDrown (currentSettings()); }

        // ---- test hooks
        VoidEngine& getEngineForTesting() noexcept { return engine; }

    private:
        Settings currentSettings() const noexcept;
        void processChunk (juce::AudioBuffer<float>& io, int start, int numSamples, int numIn, int numOut);

        struct Params
        {
            std::atomic<float>* algorithm;
            std::atomic<float>* color;
            std::atomic<float>* preDelay;
            std::atomic<float>* size;
            std::atomic<float>* decay;
            std::atomic<float>* diffusion;
            std::atomic<float>* drown;
            std::atomic<float>* modRate;
            std::atomic<float>* modDepth;
            std::atomic<float>* width;
            std::atomic<float>* hpf;
            std::atomic<float>* lpf;
            std::atomic<float>* filterMode;
            std::atomic<float>* duck;
            std::atomic<float>* mix;
            std::atomic<float>* freeze;
            std::atomic<float>* output;
            std::atomic<float>* bypass;
        } p {};

        VoidEngine engine;
        dsp::LatencyAlignedDry dry;
        juce::AudioBuffer<float> wet;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        juce::SmoothedValue<float> mixSmoothed;
        float mixRounded = 0.35f, mixRound = 0.01f, outRounded = 1.0f;
        dsp::Ramp bypassRamp;
        int preparedChannels = 0, maxBlock = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoidProcessor)
    };
}
