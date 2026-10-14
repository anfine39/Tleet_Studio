#pragma once

#include "dsp/DspUtils.h"
#include "DetectorFilters.h"
#include "GrTelemetry.h"
#include "OutputStage.h"
#include <array>

namespace tleet::weight
{
    // Stereo VCA bus compressor model (API 2500 DNA). See
    // plugins/WEIGHT/RESEARCH.md for the evidence and MODEL_SPEC.md for the
    // provenance of every block and constant.
    //
    //   audio x[ch] -> VCA (g[ch]) -> makeup -> output stage (TONE) -> out
    //
    //   detector feed per channel:
    //       Feed Forward (API NEW): x[ch]                  (before the VCA)
    //       Feed Back    (API OLD): x[ch] * g[ch]          (after the VCA, pre-makeup)
    //       External key          : key[ch] (forces feed-forward, as the hardware)
    //   -> SC HPF -> THRUST -> square -> RMS (one pole on the mean square,
    //      tau 5 ms) -> level (AES17 dBFS RMS) -> gain computer (threshold,
    //      fixed MED knee, slope S = 1 - 1/ratio) -> attack/release ballistics
    //      on the GR in dB (one capacitor, steered by a diode: attack when the
    //      target is above the capacitor, release otherwise) -> c[ch]
    //   stereo link: cv[L] = c[L] + (link/2)(c[R] - c[L]), and symmetric
    //   (the control voltages are summed; 100 % = both channels identical)
    //   g[ch] = 10^(-cv[ch]/20)
    //
    // The feedback detector reads the VCA output of the previous sample
    // (one-sample loop delay, stable for every setting: the loop gain is at
    // most S <= 1 and both smoothers are passive). Runs at the host rate.
    class WeightModel
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int numAttacks = 7;    // 0.03 .. 30 ms
        static constexpr int numReleases = 6;   // 0.05 .. 2 s
        static constexpr int numRatios = 7;     // 1.5, 2, 3, 4, 6, 10, infinity
        static constexpr int infiniteRatio = 6;
        static constexpr int feedForward = 0, feedBack = 1;

        // ---- fixed laws (MODEL_SPEC 2-3)
        static constexpr double rmsTimeSeconds = 0.005;  // RMS detector averaging          [APPROXIMATION]
        static constexpr double kneeWidthDb = 6.0;       // the 2500's MED knee, fixed        [APPROXIMATION]
        static constexpr double aesOffsetDb = 3.0102999566398120; // 10 log10(2): sine RMS -> AES17 dBFS
        static constexpr double maxGainReductionDb = 80.0;
        static constexpr double ln10Over20 = 0.11512925464970229;

        static double attackSeconds (int index) noexcept;
        static double releaseSeconds (int index) noexcept;
        static double ratioValue (int index) noexcept;    // infinity for the last position
        static double ratioSlope (int index) noexcept;    // S = 1 - 1/ratio (1 for infinity)

        // Quadratic soft knee of width w (dB) on the overshoot x (dB).
        static double softKnee (double x, double w) noexcept
        {
            if (x <= -0.5 * w) return 0.0;
            if (x >=  0.5 * w) return x;
            const double t = x + 0.5 * w;
            return t * t / (2.0 * w);
        }

        // Gain computer: target GR (dB) for a detector level (AES17 dBFS RMS).
        static double gainComputerDb (double levelDbfs, double thresholdDbfs, double slope) noexcept
        {
            return slope * softKnee (levelDbfs - thresholdDbfs, kneeWidthDb);
        }

        // Static GR (dB) for a steady input whose detector level (after the
        // detector filters) is `levelDbfs`. Feed-forward: the gain computer.
        // Feedback: the detector reads the output, so c = T(level - c)
        // (solved exactly). Above the knee the effective ratio is the label
        // in feed-forward and 2 - 1/ratio in feedback (MODEL_SPEC 2.4).
        static double staticGainReductionDb (double levelDbfs, double thresholdDbfs, int ratio, int topology) noexcept;
        static double effectiveRatio (int ratio, int topology) noexcept;

        // Detector level of a mean square value.
        static double levelDbOfMeanSquare (double ms) noexcept { return 10.0 * std::log10 (ms + 1.0e-30) + aesOffsetDb; }

        void prepare (double sampleRateHz);
        void reset();

        // ---- controls (call per block). Threshold, slope, makeup, link and
        // the topology / key crossfades glide; attack and release switch the
        // ballistics on the next sample (no hidden envelope).
        void setThresholdDbfs (float dbfs) noexcept   { thresholdTarget = dbfs; }
        void setMakeupDb (float db) noexcept          { makeupTarget = dsp::dbToGain (db); }
        void setAttack (int index) noexcept           { attackIndex = std::clamp (index, 0, numAttacks - 1); }
        void setRelease (int index) noexcept          { releaseIndex = std::clamp (index, 0, numReleases - 1); }
        void setRatio (int index) noexcept            { ratioIndex = std::clamp (index, 0, numRatios - 1); }
        void setTopology (int index) noexcept         { topologyIndex = std::clamp (index, 0, 1); }
        void setThrust (int index) noexcept           { thrustIndex = std::clamp (index, 0, detector::numThrustModes - 1); }
        void setTone (int index) noexcept             { toneIndex = std::clamp (index, 0, tone::numTones - 1); }
        void setScHpf (float hz) noexcept             { hpfHz = hz; }
        void setLink (float amount01) noexcept        { linkTarget = std::clamp (amount01, 0.0f, 1.0f); }
        // Detector source: the external key when true (and a key is passed to process()).
        void setUseExternalKey (bool external) noexcept { useExternal = external; }

        // Telemetry sinks (may be null). Every applied gain is pushed; with
        // one audio channel both sinks receive it.
        void setTelemetry (GrTelemetry* left, GrTelemetry* right) noexcept { tel[0] = left; tel[1] = right; }

        // Processes `numSamples` of `numAudio` audio channels in place. `key`
        // holds `numKey` external side-chain channels (may be null / 0); they
        // only ever feed the detector.
        void process (float* const* audio, int numAudio, const float* const* key, int numKey, int numSamples) noexcept;

        // ---- state (last processed sample)
        double getGainReductionDb (int ch) const noexcept { return cv[(size_t) std::clamp (ch, 0, 1)]; }
        float getAppliedGain (int ch) const noexcept     { return gain[(size_t) std::clamp (ch, 0, 1)]; }
        double getControlDb (int ch) const noexcept      { return cap[(size_t) std::clamp (ch, 0, 1)]; } // before the link
        double getDetectorLevelDb (int ch) const noexcept { return levelDbOfMeanSquare (ms[(size_t) std::clamp (ch, 0, 1)]); }
        double getSampleRate() const noexcept { return fs; }
        float getExternalBlend() const noexcept { return (float) extBlend; }
        float getTopologyBlend() const noexcept { return (float) topoBlend; }
        const DetectorFilterControl& getDetectorFilter() const noexcept { return filterControl; }

        // Test hook: every applied VCA gain (linear) of channel ch is written
        // to dest[ch] until `capacity` values are stored. Null disables it.
        void setGainTraceForTesting (float* left, float* right, int capacity) noexcept
        {
            trace[0] = left; trace[1] = right;
            traceCapacity = capacity;
            traceCount = 0;
        }
        int getGainTraceCount() const noexcept { return traceCount; }

    private:
        void controlUpdate() noexcept;

        double fs = 48000.0;

        // controls
        float thresholdTarget = -18.0f, makeupTarget = 1.0f, hpfHz = 20.0f, linkTarget = 1.0f;
        bool useExternal = false;
        int attackIndex = 5, releaseIndex = 2, ratioIndex = 1, topologyIndex = feedForward, thrustIndex = 0, toneIndex = 1;

        // smoothed controls
        double thresholdDb = -18.0, slope = 0.5, link = 1.0, extBlend = 0.0, topoBlend = 0.0;
        float makeupGain = 1.0f;
        double cs = 0.001, csFast = 0.002, blendStep = 0.001;
        double kRms = 0.004, ka = 0.002, kr = 0.0001;
        int controlCountdown = 0;
        bool runFF = true, runFB = false; // detector feeds that ran on the previous sample

        // detector state
        std::array<double, maxChannels> ms {}, cap {}, cv {};
        std::array<float, maxChannels> gain { 1.0f, 1.0f };

        DetectorFilterControl filterControl;
        std::array<DetectorFilter, maxChannels> filterFF {}, filterFB {}, filterKey {};
        std::array<OutputStage, maxChannels> stage {};

        GrTelemetry* tel[maxChannels] { nullptr, nullptr };
        float* trace[maxChannels] { nullptr, nullptr };
        int traceCapacity = 0, traceCount = 0;
    };
}
