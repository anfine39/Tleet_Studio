#pragma once

#include "Networks.h"
#include "dsp/MeterSource.h"
#include <atomic>
#include <limits>
#include <vector>

// The wet engine of VOID (MODEL_SPEC.md; literature in RESEARCH.md).
//
//   in L/R ─► [PRE filter] ─► DARK input band limit ─► RUIN drive ─► DC block ─► PRE-DELAY ─► × FREEZE gate
//        │                                                                            │
//        │      ┌────────────────── algorithm slot A / B (crossfaded on ALGORITHM) ◄──┘
//        │      │  early reflections / diffusion ─► late network (decay, LOOP filter, modulation, RUIN smear/saturation)
//        │      └──► early + late
//        │                 │
//        │                 └─► [POST filter] ─► DARK output band limit ─► WIDTH (M/S) ─► × DUCK ─► wet L/R
//        └─► DUCK detector ──────────────────────────────────────────────────────────────▲
//
// Runs sample by sample; controls, glides and coefficients update every
// kControlBlock (32) samples and are interpolated per sample inside the block.
namespace tleet::voidrev
{
    struct Settings
    {
        int algorithm = algoHall;
        int color = 0;               // 0 Clean, 1 Dark, 2 Ruin
        double preDelayMs = 20.0;
        double sizePct = 50.0;
        double decayS = 2.5;
        bool decayInfinite = false;  // DECAY at its maximum
        double diffusionPct = 70.0;
        double drownPct = 0.0;
        double modRateHz = 0.5;
        double modDepthPct = 30.0;
        double widthPct = 100.0;
        double hpfHz = 20.0, lpfHz = 20000.0;
        int filterMode = 1;          // 0 Pre, 1 Loop, 2 Post
        double duckPct = 0.0;
        bool freeze = false;
    };

    // The values the DSP uses after the DROWN macro (MODEL_SPEC 9). The user's
    // base values (the parameters) are never written.
    struct EffectiveSettings
    {
        double sizePct = 50.0;
        double decaySeconds = 2.5;   // +infinity when DECAY is at its maximum
        bool decayInfinite = false;
        double diffusionPct = 70.0;
        double modDepthPct = 30.0;
        double widthPct = 100.0;
        double lpfHz = 20000.0;
    };

    namespace drown
    {
        // c(d) = 3 d^2 - 2 d^3 (smoothstep of d = DROWN / 100): gentle at both ends
        inline double curve (double pct) noexcept
        {
            const double d = std::clamp (pct * 0.01, 0.0, 1.0);
            return d * d * (3.0 - 2.0 * d);
        }
        inline constexpr double sizeShare = 0.6;       // of the headroom to 100 %
        inline constexpr double decayOctaves = 2.0;    // DECAY x 2^(2 c): x4 at full DROWN (capped at 60 s)
        inline constexpr double diffusionShare = 0.8;  // of the headroom to 100 %
        inline constexpr double depthShare = 0.5;      // of the headroom to 100 %
        inline constexpr double widthShare = 0.25;     // of the headroom to 200 %
        inline constexpr double lpfOctaves = 0.75;     // LPF x 2^(-0.75 c)
        inline constexpr double maxFiniteDecayS = 60.0;
    }

    EffectiveSettings applyDrown (const Settings& s) noexcept;

    class VoidEngine
    {
    public:
        static constexpr int K = kControlBlock;

        void prepare (double sampleRate, int maxBlock);
        void reset();
        void setSettings (const Settings& s) noexcept { target = s; }

        // One host block. inR may equal inL (mono); outputs may alias inputs.
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

        // ---- telemetry (any thread)
        float takeEarlyLevel() noexcept { return take (earlyMeter, lastEarly); }
        float takeLateLevel() noexcept { return take (lateMeter, lastLate); }
        bool isFrozen() const noexcept { return frozen.load (std::memory_order_relaxed); }
        double filterResponseDb (double hz) const noexcept
        {
            return filt::hpLpResponseDb (hz, hpfNow.load (std::memory_order_relaxed), lpfNow.load (std::memory_order_relaxed), fs);
        }
        double getCurrentHpfHz() const noexcept { return hpfNow.load (std::memory_order_relaxed); }
        double getCurrentLpfHz() const noexcept { return lpfNow.load (std::memory_order_relaxed); }

        // ---- state for tests
        double getSampleRate() const noexcept { return fs; }
        int getActiveAlgorithm() const noexcept { return slots[(size_t) act].slot.getAlgorithm(); }
        bool isSwitching() const noexcept { return slots[(size_t) (1 - act)].state != idle || pending; }
        double getLossRate() const noexcept { return gLambda.end; }            // 1/RT60 in use (0 = lossless)
        double getFreezeGate() const noexcept { return gFreezeGate.end; }      // input injection gain
        double getDuckAttenuationDb() const noexcept { return duckAttDb; }
        double getLoopBoundGain() const noexcept { return slots[(size_t) act].slot.getBoundGain(); }
        double getPreDelaySamples() const noexcept { return preFading ? preB : preA; } // the tap in use (target of a running fade)
        bool isPreDelayFading() const noexcept { return preFading; }
        const ReverbSlot& getActiveSlot() const noexcept { return slots[(size_t) act].slot; }
        size_t getMemoryBytes() const noexcept { return 2 * ReverbSlot::memoryFloats (fs) * sizeof (float); }
        // 0: early + late (normal), 1: early only, 2: late only (test hook)
        void setComponentSolo (int s) noexcept { solo = s; }

    private:
        enum SlotState { idle = 0, active, ringOut, clearing };
        struct SlotRun
        {
            ReverbSlot slot;
            SlotState state = idle;
            Glide gate;           // input gate (switching)
            double fade = 1.0, fadeStep = 0.0, fadePhase = 0.0;
            bool fast = false;
        };

        static float take (dsp::MeterSource& m, std::atomic<float>& last) noexcept
        {
            const float r = m.takeRms();
            if (r < 0.0f)
                return last.load (std::memory_order_relaxed);
            last.store (r, std::memory_order_relaxed);
            return r;
        }

        void controlBlock (bool runSlots = true) noexcept;
        void snapToTargets() noexcept;
        BlockParams blockParams() const noexcept;
        void runSlotMachine (const BlockParams& bp) noexcept;
        double readPreDelay (const DelayLine& l, double d) const noexcept;

        double fs = 48000.0, controlRate = 1500.0;
        double kSize = 0.1, kLambda = 0.1, kFast = 0.1, kGate = 0.1, kLog = 0.1;
        double kDuckAtt = 0.1, kDuckRel = 0.01, duckDetRel = 0.999;
        Settings target {};
        int blockPos = 0;

        double preTarget = 0.0, preA = 0.0, preB = 0.0, preFade = 0.0, preFadeStep = 0.001;
        bool preFading = false;
        Glide gSize, gLambda, gDiff, gDepth, gWidth, gRuin, gDark, gWPre, gWLoop, gWPost, gFreezeGate, gFreezeW, gDuck;
        double rateCur = 0.5, hpfCur = 20.0, lpfCur = 20000.0;
        bool preOn = false, postOn = false, darkOn = false;

        filt::HpLp pre[2], post[2], loopCoeffs;
        filt::Lowpass4 darkIn[2];
        filt::Svf darkOut[2];

        std::vector<float> preMem[2];
        DelayLine preLine[2];

        std::array<SlotRun, 2> slots;
        int act = 0;
        bool pending = false;

        double duckEnv = 0.0, duckBlockPeak = 0.0, duckAttDb = 0.0;
        double dcR = 0.9993, dcX1[2] {}, dcY1[2] {};
        double ruinX1[2] {};
        int solo = 0;

        dsp::MeterSource earlyMeter, lateMeter;
        std::atomic<float> lastEarly { 0.0f }, lastLate { 0.0f };
        std::atomic<bool> frozen { false };
        std::atomic<double> hpfNow { 20.0 }, lpfNow { 20000.0 };
    };

    namespace engine
    {
        inline constexpr double sizeGlideS = 0.1;       // per stage (two stages)
        inline constexpr double preDelayFadeS = 0.02;   // PRE-DELAY change: tap crossfade
        inline constexpr double decayGlideS = 0.04;
        inline constexpr double controlGlideS = 0.02;   // diffusion, depth, width, colour, filter routing
        inline constexpr double gateGlideS = 0.015;     // freeze and slot input gates
        inline constexpr double logGlideS = 0.015;      // HPF/LPF and MOD RATE (log domain, one stage)
        inline constexpr double ringOutS = 1.0;         // old algorithm's tail fade after a switch
        inline constexpr double fastRingOutS = 0.03;    // when its slot is needed again
        inline constexpr size_t clearFloatsPerBlock = 8192;
        inline constexpr double ruinDrive = 4.0;        // +12 dB into the input saturation
        inline constexpr double darkInHz = 8000.0;      // 4th-order Butterworth
        inline constexpr double darkOutHz = 10000.0;    // 2nd-order Butterworth
        inline constexpr double duckMaxDb = 24.0;
        inline constexpr double duckLowDb = -45.0, duckSpanDb = 25.0;
        inline constexpr double duckAttackS = 0.010, duckReleaseS = 0.250, duckDetectorReleaseS = 0.050;
        inline constexpr double maxPreDelayMs = 250.0;
        inline constexpr double dcBlockHz = 5.0;        // first-order high-pass on the network input
    }
}
