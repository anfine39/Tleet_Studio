#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/MeterSource.h"
#include "dsp/SpectrumTap.h"
#include "dsp/SteelEngine.h"
#include "dsp/Filters.h"
#include "dsp/Loudness.h"
#include "dsp/Telemetry.h"
#include "Parameters.h"
#include <array>
#include <atomic>
#include <vector>

namespace tleet::steel
{
    // Level matching for monitoring (MODEL_SPEC 7.3): compares the K-weighted
    // short-term (3 s) loudness of the plugin input with that of the processed
    // signal after OUTPUT and returns the attenuation that brings the second
    // back to the first. Never boosts (clamped to -30 .. 0 dB), so it can only
    // lower the exported peak level.
    class LevelMatcher
    {
    public:
        static constexpr double timeConstantSeconds = 1.0;
        static constexpr float maxCutDb = 30.0f;

        void prepare (double sampleRate, int numChannels)
        {
            fs = sampleRate;
            channels = std::clamp (numChannels, 1, 2);
            hopLength = std::max (1, (int) std::lround (0.1 * fs));
            for (auto& k : kIn) k.prepare (fs);
            for (auto& k : kOut) k.prepare (fs);
            hopIn.fill (0.0); hopOut.fill (0.0);
            sumIn = sumOut = 0.0;
            fill = 0; count = 0;
            targetDb = 0.0;
            smoothCoeff = 1.0 - std::exp (-0.1 / timeConstantSeconds);
        }

        // one sample of every channel: x = plugin input, y = processed after OUTPUT
        void push (const float* x, const float* y) noexcept
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const double a = kIn[(size_t) ch].process (x[ch]);
                const double b = kOut[(size_t) ch].process (y[ch]);
                sumIn += a * a;
                sumOut += b * b;
            }
            if (++fill >= hopLength)
            {
                hopIn[(size_t) (count % 30)] = sumIn / hopLength;
                hopOut[(size_t) (count % 30)] = sumOut / hopLength;
                ++count;
                fill = 0;
                sumIn = sumOut = 0.0;
                double ei = 0.0, eo = 0.0;
                for (int i = 0; i < 30; ++i) { ei += hopIn[(size_t) i]; eo += hopOut[(size_t) i]; }
                const double li = LoudnessMeter::lufs (ei / 30.0), lo = LoudnessMeter::lufs (eo / 30.0);
                if (li > -70.0 && lo > -70.0) // below the absolute gate: hold
                {
                    const double want = std::clamp (li - lo, (double) -maxCutDb, 0.0);
                    targetDb += smoothCoeff * (want - targetDb);
                }
            }
        }

        float getTargetDb() const noexcept { return (float) targetDb; }

    private:
        double fs = 48000.0, sumIn = 0.0, sumOut = 0.0, targetDb = 0.0, smoothCoeff = 0.1;
        int channels = 2, hopLength = 4800, fill = 0;
        int64_t count = 0;
        std::array<KWeighting, 2> kIn, kOut;
        std::array<double, 30> hopIn {}, hopOut {};
    };

    // TLEET // STEEL: original true-peak mastering limiter (MODEL_SPEC.md).
    // Buses: mono -> mono or stereo -> stereo.
    //
    //   in ─► sanitise ─► INPUT ─┬─► IN meters, input true peak
    //                            ▼
    //                        SUB FILTER ─► u ─► SteelEngine (OS, SOFT CLIP, limiter, ceiling stage) ─► y
    //   y × OUTPUT × LEVEL MATCH ─► DELTA crossfade with (u − y) / INPUT ─► host Bypass (latency-aligned dry)
    //     ─► out ─► OUT meters, output true peak, loudness, history, warnings, spectrum
    class SteelProcessor : public TleetProcessorBase
    {
    public:
        SteelProcessor();
        ~SteelProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // ================================================================ telemetry (MODEL_SPEC 9)
        // All lock-free; the editor polls on a 30 Hz timer. Index 0 = left, 1 = right;
        // with a mono bus both carry the single channel.

        // IN bars: the signal after INPUT gain, before SUB FILTER (what drives the limiter).
        // OUT bars: the final plugin output (after OUTPUT, LEVEL MATCH, DELTA and Bypass).
        std::array<dsp::MeterSource, 2> inputMeters, outputMeters;

        // True-peak maximum (dBTP, floor -120) per channel since the previous call, by the
        // limiter's own estimator (TruePeak.h). Input: after INPUT gain, before SUB FILTER;
        // output: the final output. Always measured.
        float takeOutputTruePeakDb (int channel) noexcept;
        float takeInputTruePeakDb (int channel) noexcept;

        // Gain reduction = the gain actually applied by the limiter and the ceiling stage
        // (the soft clipper is a waveshaper and is not part of it), in dB, <= 0.
        //   getCurrentGainReductionDb()       the deepest GR in the most recent completed
        //                                     10 ms history slice, deeper channel
        //   getCurrentGainReductionDb (ch)    the same for one channel
        //   takeGainReductionDb()             the deepest GR since the previous call (0 if none)
        float getCurrentGainReductionDb() const noexcept;
        float getCurrentGainReductionDb (int channel) const noexcept;
        float takeGainReductionDb() noexcept;

        // History of the large display: fixed 10 ms slices in a lock-free FIFO.
        using HistorySlice = steel::HistorySlice;
        static constexpr double historySliceSeconds = 0.01;
        int popHistory (HistorySlice* dest, int maxSlices) noexcept { return history.pop (dest, maxSlices); }

        // BS.1770-4 / R128 loudness of the final plugin output since the last reset.
        using LoudnessReadings = steel::LoudnessReadings;
        LoudnessReadings getLoudness() const noexcept { return loudness.read(); }
        void resetLoudness() noexcept { loudness.requestReset(); }

        // Warnings (MODEL_SPEC 9.5). Computed from telemetry only; never change the audio.
        //   TP OVERS: a true peak of the final output above CEILING + 0.1 dB (the ceiling in
        //             force at that sample) within the last 2.0 s of processed audio.
        //   HEAVY LIMITING: the mean applied GR over the last 2.0 s is 4.0 dB or more.
        static constexpr float tpOverToleranceDb = 0.1f;
        static constexpr double tpOverHoldSeconds = 2.0;
        static constexpr float heavyLimitingDb = 4.0f;
        static constexpr double heavyLimitingWindowSeconds = 2.0;
        bool isTruePeakOver() const noexcept { return tpOver.load (std::memory_order_relaxed); }
        bool isHeavyLimiting() const noexcept { return heavy.load (std::memory_order_relaxed); }

        // Final plugin output, (L + R) / 2. Inactive until the editor activates it.
        dsp::SpectrumTap outputSpectrum;

        // The release time in effect: MANUAL = the RELEASE knob. AUTO = the mode's fast and
        // slow release times interpolated geometrically by the share of the gain reduction
        // that the slow (sustained) branch holds, weighted by the GR itself and averaged
        // over ~0.5 s:
        // isolated peaks read near the fast time, sustained limiting moves towards the slow
        // one (at most autoSlowDepth of the way). Held while nothing is limited.
        double getEffectiveReleaseMs() const noexcept { return effectiveRelease.load (std::memory_order_relaxed); }

        // Attenuation LEVEL MATCH applies right now (dB, <= 0; 0 when off).
        float getLevelMatchGainDb() const noexcept { return lmGainDb.load (std::memory_order_relaxed); }

        // ---- test hooks
        SteelEngine& getEngineForTesting() noexcept { return engine; }
        // Appends the applied gain (deeper channel, linear) of every output sample. The
        // vector must be reserved by the caller (no allocation while it has room).
        void setGainCaptureForTesting (std::vector<float>* dest) noexcept { gainCapture = dest; }
        SlotConfig currentSlotConfig() const noexcept;

    private:
        explicit SteelProcessor (std::shared_ptr<params::TextContext> ctx);
        void processChunk (juce::AudioBuffer<float>& io, int start, int numSamples, int numChannels);
        void endOfSlice();

        struct Params
        {
            std::atomic<float>* input;
            std::atomic<float>* ceiling;
            std::atomic<float>* mode;
            std::atomic<float>* releaseMode;
            std::atomic<float>* release;
            std::atomic<float>* lookahead;
            std::atomic<float>* truePeak;
            std::atomic<float>* oversampling;
            std::atomic<float>* stereoLink;
            std::atomic<float>* softClip;
            std::atomic<float>* softClipAmount;
            std::atomic<float>* subFilter;
            std::atomic<float>* delta;
            std::atomic<float>* levelMatch;
            std::atomic<float>* output;
            std::atomic<float>* bypass;
        } p {};

        std::shared_ptr<params::TextContext> textContext;
        SteelEngine engine;
        std::array<SubFilter, 2> sub;
        std::array<TruePeakDetector, 2> inTp, outTp;
        LoudnessMeter loudness;
        LevelMatcher levelMatcher;
        HistoryFifo history;

        // per-chunk work buffers (sized in prepareToPlay)
        std::array<std::vector<float>, 2> xBuf, uBuf, yBuf, dryBuf, refBuf, gainBuf, tpBuf, outPre;
        std::vector<float> gBuf, gAlBuf, ceilBuf, clipMixBuf, clipAmtBuf, linkBuf, tpMixBuf;

        SmoothGain inputSmoothed, outputSmoothed, ceilingSmoothed, lmSmoothed;
        juce::SmoothedValue<float> linkSmoothed, clipAmountSmoothed;
        dsp::Ramp bypassRamp, deltaRamp, clipRamp, tpRamp, subRamp;
        double subLogCutoff = std::log (20.0), subLogTarget = std::log (20.0), subGlide = 0.001;

        double sampleRate = 48000.0;
        int preparedChannels = 0, maxBlock = 512;

        // history / warnings state (audio thread)
        int sliceLength = 480, sliceFill = 0;
        float slicePeak = 0.0f, sliceMinGain = 1.0f;
        double sliceSquares = 0.0, sliceGrDbSum = 0.0;
        std::array<float, 2> sliceMinGainCh { 1.0f, 1.0f };
        std::vector<double> heavyRing;
        int heavyPos = 0;
        double heavySum = 0.0;
        int64_t sliceIndex = 0, samplesSinceOver = std::numeric_limits<int32_t>::max();
        double slowDbAvg = 0.0, totalDbAvg = 0.0, slowShare = 0.0;

        std::array<PeakHold, 2> inTpHold, outTpHold;
        PeakHold grHold; // positive dB
        std::array<std::atomic<float>, 2> currentGr { 0.0f, 0.0f };
        std::atomic<bool> tpOver { false }, heavy { false };
        std::atomic<double> effectiveRelease { 180.0 };
        std::atomic<float> lmGainDb { 0.0f };
        std::vector<float>* gainCapture = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteelProcessor)
    };
}
