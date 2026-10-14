#pragma once

#include "TapeModel.h"
#include "Filters.h"
#include "Hysteresis.h"
#include "HeadLoss.h"
#include "Oversampler.h"
#include "Transport.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// The tape machine's wet path (MODEL_SPEC 2), per channel:
//
//   host rate:  x -> INPUT -> [RECORD VU] -> record pre-emphasis E(f; speed)
//   top rate:   record amp limit -> x drive (CAL, SATURATION, type) + head magnetisation (AGE)
//               -> Jiles-Atherton hysteresis (bias -> reversibility) -> x make-up
//               -> transport delay (wow, flutter, AGE, azimuth)
//   host rate:  de-emphasis 1/E -> + hiss, modulation noise -> head-loss FIR (gap, AGE spacing, bias zone)
//               -> head roll-off, dip, bump -> repro LF / HF trims -> wear variation -> L/R mismatch
//               -> crosstalk -> [REPRO VU] -> x AUTO GAIN -> wet
//
// The engine owns all smoothing of its controls (control block = 16 host samples,
// gains interpolated per sample), so every control change is click-free.
namespace tleet::tape
{
    struct EngineSettings
    {
        float inputDb = 0.0f;
        int type = 0, speed = 1, calibration = 1; // indices
        float bias = 0.0f, saturation = 42.0f, headBump = 36.0f, lf = 0.0f, hf = 0.0f;
        float age = 18.0f, wow = 6.0f, flutter = 4.0f;
        bool noise = false;
        float noiseAmount = 12.0f, crosstalk = 8.0f;
        bool autoGain = true;
    };

    // Snapshot of the linear small-signal state, for the response evaluator.
    struct ResponseState
    {
        double ips = 15.0, spacingL = 0.0, spacingR = 0.0, biasZone = 0.0;
        double bumpDb = 0.0, lfDb = 0.0, hfDb = 0.0, sensitivityDb = 0.0, age = 0.0;
    };

    class TapeEngine
    {
    public:
        static constexpr int controlBlock = 16;
        static constexpr double calibrationDb[4] { -3.0, 0.0, 3.0, 6.0 };

        TapeEngine();

        // osLog2Override: -1 = the rate rule (top rate 176.4 / 192 kHz); 0..2 forces 1x / 2x / 4x (tests).
        void prepare (double sampleRate, int maxBlock, int numChannels, const EngineSettings& initial, int osLog2Override = -1);
        void reset();

        void setTargets (const EngineSettings& s) noexcept { targets = s; }

        // in: plugin input (finite). wet: machine output x AUTO GAIN. repro: machine output
        // before AUTO GAIN (the REPRO VU point). n <= maxBlock.
        void process (const float* const* in, float* const* wet, float* const* repro, int n) noexcept;

        int latency() const noexcept { return latencySamples; }
        int oversamplingFactor() const noexcept { return 1 << osLog2; }
        double topRate() const noexcept { return fsTop; }

        // ---- telemetry of the last process() call
        // sum of squares of the record-point signal / (0 VU RMS)^2, per channel
        double recordSquares (int ch) const noexcept { return recSq[(size_t) ch]; }
        double reproSquares (int ch) const noexcept { return repSq[(size_t) ch]; }
        double recordPeak (int ch) const noexcept { return recPk[(size_t) ch]; }
        double reproPeak (int ch) const noexcept { return repPk[(size_t) ch]; }
        // largest instantaneous knee depth 1 - L(|Q|) / (|Q| / 3) over the call, both channels
        double saturationActivity() const noexcept { return satActivity; }

        double currentAutoGainDb() const noexcept { return agValueDb; }
        double currentIps() const noexcept { return std::exp (logIps.value); }
        double currentReversibility() const noexcept { return jaParams.c; }

        // Small-signal magnitude of the machine (dB) at hz: everything between the plugin
        // input and the REPRO point except INPUT, from the same coefficients the audio uses.
        // channel 0 / 1, or -1 = power mean of the two. Any thread (the UI); uses its own designer.
        double responseDb (double hz, int channel = -1) const;
        void responseDb (const double* hz, double* outDb, int count, int channel = -1) const;
        ResponseState responseState() const noexcept;

        // AUTO GAIN definition (MODEL_SPEC 8): dB that brings a 0 VU 1 kHz sine back to its
        // input level at the given settings (bias as current I). Exposed for the tests.
        double autoGainDbFor (double inputDb, double calDb, double satDb, int type, double current, double ips) const noexcept;

        // ---- test hooks
        const std::vector<float>& lossFirForTesting (int ch) const noexcept { return chans[(size_t) ch].firNew; }
        double transportDeviationSeconds() const noexcept { return wowFlutter.lastDeviation(); }

    private:
        struct LinearRamp
        {
            double value = 0.0, target = 0.0, step = 0.0, seconds = 0.03;
            void snap (double v) noexcept { value = target = v; step = 0.0; }
            void setTarget (double t, double fs) noexcept
            {
                if (t != target)
                {
                    target = t;
                    step = (target - value) / std::max (1.0, seconds * fs);
                }
            }
            void advance (int n) noexcept
            {
                if (value == target) return;
                value += step * n;
                if ((step > 0.0 && value >= target) || (step < 0.0 && value <= target) || step == 0.0)
                    value = target;
            }
        };

        struct Channel
        {
            Emphasis pre, de, hissShape;
            Oversampler os;
            HysteresisSolver ja;
            DelayLine delay;
            std::vector<float> firOld, firNew, firHist;
            int firPos = 0;
            Svf rolloff, dip, bump, lfShelf, hfShelf, hissLp;
            OnePole wearSplit, xtalk;
            Rng hissRng, modRng, wearRng;
            BandNoise modNoise, wear;
            double wearPrev = 0.0, wearNow = 0.0;
            double maxQ = 0.0;
        };

        void updateControl (int n) noexcept;
        void updateFilters() noexcept;
        void designLoss (bool force) noexcept;
        double typeBlend (double a, double b) const noexcept { return a + typeW * (b - a); }
        double sensitivityFor (int type, double c, double cOpt) const noexcept;

        EngineSettings targets;
        double fs = 48000.0, fsTop = 192000.0;
        int osLog2 = 2, numCh = 2, maxBlock = 512, latencySamples = 0, transportCentreTop = 0;

        std::array<Channel, 2> chans;
        WowFlutter wowFlutter;
        LossFirDesigner designer;
        mutable LossFirDesigner uiDesigner;
        mutable std::mutex uiMutex;

        // smoothed controls
        // agDb is the AUTO GAIN on/off weight (0..1)
        LinearRamp inputDb, calDb, satDb, current, logIps, age, bumpPct, lfDb, hfDb, wow, flutter, noiseOn, noiseAmt, xtalk, agDb;
        int typeFrom = 0, typeTo = 0, pendingType = -1;
        double typeW = 1.0, typeStep = 0.0;
        JAParams jaParams;

        // per-sample interpolation endpoints
        double inGainPrev = 1.0, inGainNow = 1.0, drivePrev = 1.0, driveNow = 1.0, makeupPrev = 1.0, makeupNow = 1.0;
        double agPrev = 1.0, agNow = 1.0, tauPrev = 0.0, tauNow = 0.0;
        double hissPrev = 0.0, hissNow = 0.0, modPrev = 0.0, modNow = 0.0, xtPrev = 0.0, xtNow = 0.0;
        double hdc = 0.0, azimuth = 0.0, gainL = 1.0, gainR = 1.0, wearHf = 0.0, wearBroad = 0.0, hissNorm = 1.0;
        double emphasis1k = 1.0;

        // loss FIR state
        LossParams lossDesigned[2];
        int firFade = 0, firFadeLength = 64, samplesSinceDesign = 0, designInterval = 64;

        // AUTO GAIN delay (applied at the output, computed at the input)
        std::vector<float> agDelay;
        int agPos = 0;

        // block buffers
        std::array<std::vector<float>, 2> eBuf, tBuf;
        std::vector<float> topBuf;

        // telemetry
        std::array<double, 2> recSq {}, repSq {}, recPk {}, repPk {};
        double satActivity = 0.0;
        std::atomic<double> rsIps { 15.0 }, rsSpacingL { 0.0 }, rsSpacingR { 0.0 }, rsZone { 0.0 }, rsBump { 0.0 }, rsLf { 0.0 }, rsHf { 0.0 }, rsSens { 0.0 };
        std::atomic<double> rsFs { 48000.0 }, rsAge { 0.0 };
        double agValueDb = 0.0;
        std::int64_t sampleCounter = 0;
    };
}
