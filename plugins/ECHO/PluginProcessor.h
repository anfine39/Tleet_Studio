#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/MeterSource.h"
#include "dsp/EchoEngine.h"
#include "Parameters.h"
#include <array>
#include <atomic>

namespace tleet::echo
{
    // TLEET // ECHO: multi-head tape echo with spring reverb, Roland RE-201 DNA
    // (MODEL_SPEC.md). Buses: main mono -> mono, stereo -> stereo, or mono ->
    // stereo. Zero latency: the 2x record stage sits inside the tape loop and
    // its delay is taken off the tape delay (MODEL_SPEC 4.4); the dry path is
    // the plugin input itself.
    //
    // Signal flow:
    //   in ─► sanitise ─┬─► EchoEngine (tape, heads, loop, spring, width, duck) ─► wet ─┐
    //                   └─► dry ─────────────────────────────────────────────── MIX ◄──┘
    //                                                                            │
    //                                                       OUTPUT ─► host Bypass (dry) ─► out
    class EchoProcessor : public TleetProcessorBase
    {
    public:
        EchoProcessor();
        ~EchoProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        // Longest echo (head 3 at the longest head-1 time) plus the spring's decay.
        double getTailLengthSeconds() const override { return timing::maxHead1Ms * 0.001 * timing::headRatio[2] + 3.0; }

        // ---- meters (audio thread writes once per processed chunk, UI reads lock-free).
        // Index 0 = left, 1 = right; with a mono bus both carry the single channel.
        //   inputMeters[ch]  : the plugin input (IN VU)
        //   outputMeters[ch] : the final plugin output, after MIX, OUTPUT and Bypass (OUT VU)
        // VU ballistics run in the UI (dsp::VUBallistics); 0 VU = a sine at -18 dBFS
        // (params::alignmentDbfs).
        std::array<dsp::MeterSource, 2> inputMeters, outputMeters;

        // ---- echo telemetry (MODEL_SPEC 9). Heads are 0..2 (head 1..3).
        // True when MODE uses `head` (from the MODE parameter itself).
        bool isHeadActive (int head) const noexcept;
        // Peak |playback signal| of `head` (linear, 1 = 0 dBFS at the head) since
        // the previous call; 0 when MODE does not use the head.
        float takeHeadActivity (int head) noexcept;
        // The head's current actual delay (ms) from the plugin input to the echo
        // output, including tape-speed glides and wow/flutter. Updated every 32 samples.
        double getHeadDelayMs (int head) const noexcept;
        // BPM in use by SYNC: the host's when BPM SOURCE is Host and the host reports
        // a tempo, otherwise MANUAL BPM (also when Host is selected but no tempo comes).
        double getEffectiveBpm() const noexcept;
        // True when the host reported a tempo in the most recent block.
        bool isHostTempoAvailable() const noexcept { return hostTempoAvailable.load (std::memory_order_relaxed); }
        // True in SYNC when the note value lies outside the head-1 range [10 ms, 6 s] and was clamped.
        bool isSyncTimeClamped() const noexcept;
        // The head-1 delay (ms) the tape speed is driven to (after clamping).
        double getTargetHead1Ms() const noexcept;
        // Fixed head positions relative to head 1 (1, 1.90, 2.75).
        static double getHeadRatio (int head) noexcept { return timing::headRatio[(size_t) juce::jlimit (0, 2, head)]; }

        // TAP (message thread). `timeSeconds` is a monotonic time stamp of the
        // tap. Two or more taps less than 2 s apart: SYNC sets MANUAL BPM (the
        // averaged interval as a quarter note, rounded to 1 BPM) and switches
        // BPM SOURCE to Manual; FREE sets REPEAT RATE to the interval (clamped
        // to 20..2000 ms). Parameters change through the APVTS with gestures, so
        // the host records them.
        void tap (double timeSeconds);

        // ---- test hooks
        EchoEngine& getEngineForTesting() noexcept { return engine; }

    private:
        Settings currentSettings() const noexcept;
        void processChunk (juce::AudioBuffer<float>& io, int start, int numSamples, int numIn, int numOut);
        void setParameterFromMessageThread (const char* id, float plainValue);

        struct Params
        {
            std::atomic<float>* mode;
            std::atomic<float>* repeatRate;
            std::atomic<float>* intensity;
            std::atomic<float>* echoVolume;
            std::atomic<float>* reverbVolume;
            std::atomic<float>* bass;
            std::atomic<float>* treble;
            std::atomic<float>* timeMode;
            std::atomic<float>* bpmSource;
            std::atomic<float>* manualBpm;
            std::atomic<float>* noteDivision;
            std::atomic<float>* feel;
            std::atomic<float>* drive;
            std::atomic<float>* age;
            std::atomic<float>* wow;
            std::atomic<float>* flutter;
            std::atomic<float>* hpf;
            std::atomic<float>* lpf;
            std::atomic<float>* filterMode;
            std::atomic<float>* stereo;
            std::atomic<float>* duck;
            std::atomic<float>* mix;
            std::atomic<float>* tapeCondition;
            std::atomic<float>* noise;
            std::atomic<float>* character;
            std::atomic<float>* output;
            std::atomic<float>* bypass;
        } p {};

        EchoEngine engine;
        dsp::LatencyAlignedDry dry;
        juce::AudioBuffer<float> wet;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
        juce::SmoothedValue<float> mixSmoothed;
        float mixRounded = 0.35f, mixRound = 0.01f, outRounded = 1.0f;
        dsp::Ramp bypassRamp;
        int preparedChannels = 0, maxBlock = 512;

        std::atomic<bool> hostTempoAvailable { false };
        std::atomic<double> hostBpm { 120.0 };

        timing::TapTempo tapTempo; // message thread only

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoProcessor)
    };
}
