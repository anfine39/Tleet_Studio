#pragma once

#include "Halfband.h"
#include "TruePeak.h"
#include "Primitives.h"
#include "Modes.h"
#include <array>
#include <atomic>

// STEEL's limiting engine (MODEL_SPEC 2-6): from the pre-limiter signal u
// (after INPUT and SUB FILTER) to the ceiling-safe signal, plus everything
// time-aligned with it (dry input, u, INPUT gain, applied gain).
//
//   u ─► LimiterSlot (oversampled: up ─► pad ─► SOFT CLIP ─► lookahead limiter ─► down)
//     ─► [slot crossfade while MODE / OVERSAMPLING / LOOKAHEAD change]
//     ─► CeilingStage (host rate: true-peak or sample-peak detector, short
//        hold-and-ramp limiter at the CEILING) ─► out
//
// Latency (host samples) = OS(factor) + LOOKAHEAD + ceiling stage, exactly.
namespace tleet::steel
{
    struct SlotConfig
    {
        int mode = (int) Mode::steel;
        int osLog2 = 2;          // 0..3 = 1x..8x
        int lookaheadHost = 0;   // host samples
        bool operator== (const SlotConfig& o) const noexcept
        {
            return mode == o.mode && osLog2 == o.osLog2 && lookaheadHost == o.lookaheadHost;
        }
        bool operator!= (const SlotConfig& o) const noexcept { return ! (*this == o); }
    };

    // Per-host-sample controls (arrays of the chunk length) and per-chunk switches.
    struct EngineControls
    {
        const float* ceiling = nullptr;    // linear ceiling (smoothed)
        const float* clipMix = nullptr;    // SOFT CLIP on/off crossfade 0..1
        const float* clipAmount = nullptr; // SOFT CLIP AMOUNT 0..1 (before the mode's scale)
        const float* link = nullptr;       // STEREO LINK 0..1
        const float* tpMix = nullptr;      // TRUE PEAK detector crossfade 0 (sample peak) .. 1 (true peak)
        bool autoRelease = true;
        float manualReleaseMs = 180.0f;
    };

    struct EngineOutputs
    {
        float* const* out = nullptr;        // limited signal (per channel)
        float* const* dryAligned = nullptr; // plugin input x, delayed by the latency
        float* const* refAligned = nullptr; // u (pre-limiter), delayed by the latency
        float* inputGainAligned = nullptr;  // the INPUT gain that made u, delayed by the latency
        float* const* gain = nullptr;       // total applied gain (slot x ceiling stage) per channel, aligned with out
    };

    // ------------------------------------------------------------------ slot
    class LimiterSlot
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate, int maxBlock, int numChannels, int maxLookaheadHost);
        void configure (const SlotConfig& cfg) noexcept;   // resets all state (no allocation)
        void inheritEnvelope (const LimiterSlot& other) noexcept;
        const SlotConfig& config() const noexcept { return cfg; }
        int latency() const noexcept { return osLatencyHost + cfg.lookaheadHost; }
        int osLatency() const noexcept { return osLatencyHost; }

        // u: n host samples per channel; y: n host samples; gainTel: per channel and host
        // sample, the smallest gain the limiter applied to the oversampled samples that make
        // up output sample i (aligned with y)
        void process (const float* const* u, float* const* y, float* const* gainTel, int n,
                      const EngineControls& c, int offset) noexcept;

        // AUTO release bookkeeping, summed over channels and host samples since the last take:
        // GR held by the slow branch x total GR, and total GR squared (dB^2)
        double takeSlowDb() noexcept { auto v = slowDb; slowDb = 0.0; return v; }
        double takeTotalDb() noexcept { auto v = totalDb; totalDb = 0.0; return v; }
        float getFastEnvelope (int ch) const noexcept { return (float) chans[(size_t) ch].ef; }

    private:
        struct Channel
        {
            HalfbandUp up[3];
            HalfbandDown down[3];
            std::vector<float> bufA, bufB;
            DelayLine sig, rdel, tel;
            SlidingMin smin;
            BoxCascade box;
            // fast-branch / manual envelope (linear gain). Double: in float, the release step
            // aF (1 - ef) falls below the resolution of ef near 1 and the gain would stall
            // short of unity (-0.2 dB at 8x with a 1 s release).
            double ef = 1.0;
            double esDb = 0.0;      // slow branch, GR in dB (>= 0)
            float esLin = 1.0f;
            float frameMinEf = 1.0f;
            float frameMinG = 1.0f;
        };

        float requiredGain (float a, float T, float kneeLow) const noexcept;

        std::array<Channel, maxChannels> chans;
        SlotConfig cfg;
        double fs = 48000.0;
        int channels = 2, osLatencyHost = 0, padTop = 0, telDelay = 0, attackSupport = 1;
        float kneeDb = 0.0f;
        bool lastAuto = true;
        double slowDb = 0.0, totalDb = 0.0;
    };

    // ------------------------------------------------------------------ ceiling stage
    class CeilingStage
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr double releaseMs = 15.0;

        void prepare (double sampleRate, int maxBlock, int numChannels);
        void reset() noexcept;
        int latency() const noexcept { return detectorLatency + boxSupport - 1 + hold; }
        int getHold() const noexcept { return hold; }
        int getBoxSupport() const noexcept { return boxSupport; }
        int getDetectorLatency() const noexcept { return detectorLatency; }

        // in-place on io; gain receives the applied gain per channel and sample
        void process (float* const* io, float* const* gain, int n, const EngineControls& c) noexcept;

    private:
        struct Channel
        {
            TruePeakDetector tp;
            DelayLine spDelay, sig;
            SlidingMin smin;
            BoxCascade box;
            std::vector<float> tpBuf;
            double ef = 1.0; // double: see LimiterSlot::Channel::ef
        };
        std::array<Channel, maxChannels> chans;
        int channels = 2, detectorLatency = 0, boxSupport = 1, hold = 0;
        double alpha = 0.001;
    };

    // ------------------------------------------------------------------ engine
    class SteelEngine
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr double maxLookaheadMs = 10.0;
        static constexpr double fadeSeconds = 0.03; // slot crossfade

        static int lookaheadSamples (double ms, double sampleRate) noexcept
        {
            return (int) std::lround (std::clamp (ms, 0.0, maxLookaheadMs) * 0.001 * sampleRate);
        }

        void prepare (double sampleRate, int maxBlock, int numChannels, const SlotConfig& initial);
        void reset (const SlotConfig& cfg) noexcept;

        // Target configuration; a change starts a crossfade to a freshly configured slot.
        void setConfig (const SlotConfig& cfg) noexcept { target = cfg; }

        // Latency of the target configuration (what the host must compensate), host samples.
        int latency() const noexcept { return latencyFor (target); }
        int latencyFor (const SlotConfig& c) const noexcept
        {
            return OversamplingLatency::hostSamples (c.osLog2) + c.lookaheadHost + stageB.latency();
        }
        bool isSwitching() const noexcept { return incoming >= 0; }
        const SlotConfig& activeConfig() const noexcept { return slots[(size_t) active].config(); }
        const CeilingStage& ceilingStage() const noexcept { return stageB; }

        // n <= maxBlock
        void process (const float* const* x, const float* const* u, const float* inputGain, int n,
                      const EngineControls& c, const EngineOutputs& o) noexcept;

        // AUTO, last chunk: sum of (slow-branch GR x total GR) and of total GR squared over
        // channels and samples (dB^2); their ratio is the GR-weighted slow share
        double lastSlowDb() const noexcept { return slowDbSum; }
        double lastTotalDb() const noexcept { return totalDbSum; }

    private:
        void startSwitch() noexcept;

        std::array<LimiterSlot, 2> slots;
        CeilingStage stageB;
        SlotConfig target;
        int active = 0, incoming = -1, warm = 0, fadePos = 0, fadeLen = 1;
        int channels = 2, maxBlock = 512;
        double fs = 48000.0;
        double slowDbSum = 0.0, totalDbSum = 0.0;

        // aligned references: plugin input, u, INPUT gain; delay pair + weight delayed by the ceiling stage
        std::array<DelayLine, maxChannels> xRing, uRing;
        DelayLine gRing, dA, dI, wRing;
        std::array<DelayLine, maxChannels> telRing;
        std::array<std::vector<float>, maxChannels> yA, yI, tA, tI, gB;
        std::vector<float> weight, fadeCurve;
    };
}
