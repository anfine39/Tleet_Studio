#pragma once

#include "Filters.h"
#include "Modulation.h"
#include "Oversampler2x.h"
#include "Saturation.h"
#include "SpringReverb.h"
#include "TapeTransport.h"
#include "Timing.h"
#include <array>
#include <atomic>

// The wet engine of ECHO (Roland RE-201 DNA). See plugins/ECHO/RESEARCH.md for
// the evidence and MODEL_SPEC.md for the provenance of every block.
//
//   in L/R ─► mono ─► [PRE filter] ─┐            (record stage, 2x below 80 kHz)
//                                   ├─► ↑2 ─► preamp (DRIVE, CHARACTER) ─► (+) ─► tape saturation ─► ↓2 ─► + hiss ─► × dropouts ─► TAPE
//   feedback ───────────────────────┘                                        ▲
//       ▲                                                                    └── ↑2 ◄── feedback
//       │
//   TAPE ─► heads 1/2/3 (MODE) ─► playback: LF roll-off, head loss (speed, AGE, condition), BASS, TREBLE
//            │                       │
//            │                       ├─► [LOOP filter] ─► × INTENSITY (degeneration) ─► feedback
//            │                       └─► [POST filter] ─► × ECHO VOLUME ─► echo (M/S) ─┐
//            │                                                                          ├─► + spring × REVERB VOLUME ─► STEREO width ─► DUCK ─► wet L/R
//   preamp out + echo ─► springs A/B ───────────────────────────────────────────────────┘
//
// Runs sample by sample (the feedback delay can be shorter than a host block);
// controls and the tape speed update every TapeTransport::K samples.
namespace tleet::echo
{
    struct Settings
    {
        int headMask = 0b011;
        double head1Ms = 200.0;      // nominal head-1 delay (FREE: REPEAT RATE; SYNC: note value)
        double intensity = 4.0;      // 0..10
        double echoVolume = 7.0;     // 0..10
        double reverbVolume = 0.0;   // 0..10
        double bassDb = 0.0, trebleDb = 0.0;
        double drive = 3.0;          // 0..10
        double age = 2.0;            // 0..10
        double wowPct = 20.0, flutterPct = 20.0;
        double hpfHz = 20.0, lpfHz = 20000.0;
        int filterMode = 1;          // 0 Pre, 1 Loop, 2 Post
        double stereoPct = 30.0, duckPct = 0.0;
        int condition = 0;           // 0 Clean, 1 Worn, 2 Magnetic dust
        double noisePct = 10.0;
        int character = 1;           // 0 Clean, 1 Original, 2 Burn
    };

    // TAPE CONDITION macro: offsets added to the user controls (MODEL_SPEC 7.4).
    struct ConditionOffsets
    {
        double wow;       // added peak wow (fraction of speed)
        double flutter;   // added peak flutter
        double irregular; // RMS of the irregular scrape modulation
        double noiseLin;  // added hiss (linear RMS at the record head)
        double hfOct;     // added head loss (octaves the head-loss corner moves down)
        double fbLossDb;  // feedback degeneration per repeat
        double dropoutDb; // depth of the dust dropouts
        double ltScale;   // tape saturation level factor (worn oxide saturates earlier)
    };

    namespace engine
    {
        inline constexpr int numConditions = 3;
        inline const ConditionOffsets& condition (int index) noexcept
        {
            //                                                  wow     flutter  irreg    noise                hfOct  fbLoss  drop  Lt
            static const ConditionOffsets c[numConditions] = { { 0.0,    0.0,     0.0,     0.0,                 0.0,   0.0,    0.0,  1.0 },    // Clean
                                                               { 0.0012, 0.0004,  0.0003,  2.5118864315e-4,     0.5,   0.6,    1.0,  0.85 },   // Worn  (-72 dBFS hiss)
                                                               { 0.0025, 0.0008,  0.0012,  7.9432823472e-4,     1.2,   1.8,    4.0,  0.7 } };  // Magnetic dust (-62 dBFS)
            return c[std::clamp (index, 0, numConditions - 1)];
        }

        inline constexpr double fbMax = 1.25;            // INTENSITY 10: loop gain 1.25 (oscillation from ~8)
        inline constexpr double headLossHz = 6500.0;     // head-loss corner at the slowest hardware speed
        inline constexpr double playbackLfHz = 40.0;     // playback LF roll-off (2nd order)
        inline constexpr double bassHz = 150.0, trebleHz = 4000.0; // tone shelf half-gain frequencies
        inline constexpr double ageHfOct = 0.15;         // per AGE step: head-loss corner down by 0.15 oct
        inline constexpr double ageFbLossDb = 0.15;      // per AGE step: feedback loss 0.15 dB
        inline constexpr double ageLtDb = 0.3;           // per AGE step: tape saturates 0.3 dB earlier
        inline constexpr double noiseFloorDb = -80.0, noiseSpanDb = 35.0; // NOISE 0+..100 % = -80..-45 dBFS RMS
        inline constexpr double duckMaxDb = 24.0;
        inline constexpr double duckLowDb = -45.0, duckSpanDb = 25.0;    // detector range: none at -45, full at -20 dBFS
        inline constexpr double duckAttackS = 0.010, duckReleaseS = 0.250, duckDetectorReleaseS = 0.050;
        inline constexpr double gainGlideS = 0.015;      // per stage (two stages: about 30 ms)
        inline constexpr double conditionGlideS = 0.15;
        inline constexpr double speedGlideS = 0.06;      // the "motor": per stage, log domain
        inline constexpr double timingRefHz = 1000.0;    // the chain's group delay is compensated at this frequency
        inline constexpr double maxSpeedDeviation = 0.21; // |wow + flutter| bound (tape buffer margin)

        inline double echoGain (double v0to10) noexcept { const double x = std::clamp (v0to10, 0.0, 10.0) / 10.0; return 2.0 * x * x; }

        // Head-loss corner for a head-1 delay (ms): proportional to sqrt(speed)
        // inside the hardware's speed range, held outside it.
        inline double headLossCorner (double head1Ms, double extraOct) noexcept
        {
            const double d = std::clamp (head1Ms, timing::hardwareHead1MinMs, timing::hardwareHead1MaxMs);
            return headLossHz * std::sqrt (timing::hardwareHead1MaxMs / d) * std::pow (2.0, -extraOct);
        }
    }

    // Magnitude (dB) of BASS + TREBLE exactly as run.
    double toneResponseDb (double hz, double bassDb, double trebleDb, double fs) noexcept;

    class EchoEngine
    {
    public:
        static constexpr int K = TapeTransport::K;

        void prepare (double sampleRate, int maxBlock);
        void reset();
        void setSettings (const Settings& s) noexcept { target = s; }

        // One host block. in* may alias out* (in place); inR may equal inL (mono).
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

        // ---- telemetry (any thread)
        float takeHeadPeak (int head) noexcept { return headPeak[(size_t) head].exchange (0.0f, std::memory_order_acq_rel); }
        double getHeadDelayMs (int head) const noexcept { return headDelayMs[(size_t) head].load (std::memory_order_relaxed); }

        // ---- state for tests and editors
        double getSampleRate() const noexcept { return fs; }
        int getRecordOversampling() const noexcept { return os; }
        double getCompensationSamples() const noexcept { return comp; }
        double getHeadLossHz() const noexcept { return headLossHz; }
        double getCurrentHead1Samples() const noexcept { return std::exp (lnD1); }
        double getCurrentSpeedDeviation() const noexcept { return lastM; }
        const Spring& getSpring (int i) const noexcept { return i == 0 ? springA : springB; }
        // Current DUCK attenuation (dB, >= 0) applied to the wet return.
        double getDuckAttenuationDb() const noexcept { return duckAttDb; }
        // Magnitude (dB) of the playback chain (LF roll-off, head loss, BASS,
        // TREBLE) exactly as it runs now: the per-pass response of the echo.
        double playbackResponseDb (double hz) const noexcept
        {
            const auto zi = filt::zInv (hz, fs);
            return filt::toDb (chainM.lfHp.response (filt::Svf::highpass, zi) * chainM.headLp.response (filt::Svf::lowpass, zi)
                               * chainM.bass.response (zi) * chainM.treble.response (zi));
        }
        size_t getTapeMemoryBytes() const noexcept { return tape.memoryBytes(); }

    private:
        // Two-stage (critically damped) exponential glide evaluated per control
        // block, linear inside the block.
        struct Glide
        {
            double target = 0.0, mid = 0.0, end = 0.0, cur = 0.0, step = 0.0;
            void snap (double v) noexcept { target = mid = end = cur = v; step = 0.0; }
            void block (double k) noexcept
            {
                cur = end;
                mid += k * (target - mid);
                double e = end + k * (mid - end);
                if (std::abs (e - target) < 1.0e-9 * std::max (1.0, std::abs (target)) && std::abs (mid - target) < 1.0e-9 * std::max (1.0, std::abs (target)))
                    e = mid = target;
                step = (e - cur) / K;
                end = e;
            }
            double next() noexcept { cur += step; return cur; }
        };

        void controlBlock() noexcept;
        void updateCoefficients() noexcept;
        void snapToTargets() noexcept;
        double chainGroupDelay() const noexcept;

        double fs = 48000.0, recordRate = 96000.0, kGain = 0.1, kCond = 0.01, kSpeed = 0.01;
        double kDuckAtt = 0.1, kDuckRel = 0.01, duckDetRel = 0.999;
        int os = 2, osLatency = 23;

        Settings target {};
        ConditionOffsets cond = engine::condition (0); // glided
        Voicing voice = sat::voicing (1);               // glided

        TapeTransport tape;
        WowFlutter wowFlutter;
        Dropouts dropouts;
        TapeHiss hiss;
        double lnD1 = 0.0, lnD1Mid = 0.0, lastM = 0.0, comp = 0.0, headLossHz = 6500.0;
        double hpfCur = 20.0, lpfCur = 20000.0;

        // playback chain on M (also the loop) and S
        struct Chain
        {
            filt::Svf lfHp, headLp;
            filt::Shelf bass, treble;
            double process (double x) noexcept
            {
                x = lfHp.process<filt::Svf::highpass> (x);
                x = headLp.process<filt::Svf::lowpass> (x);
                return treble.process (bass.process (x));
            }
            void reset() noexcept { lfHp.reset(); headLp.reset(); bass.reset(); treble.reset(); }
        } chainM, chainS;

        filt::HpLp preFilter, loopFilter, postM, postS;

        Upsampler2x upIn, upFb;
        Downsampler2x downTape, downPre;
        Preamp preamp;
        TapeSaturation tapeSat;

        Spring springA, springB;
        bool springRunning = false;

        // glided controls
        Glide gEcho, gRev, gFb, gLoopScale, gWidth, gNoise, gDrop, gWPre, gWLoop, gWPost, gDrive, gDuck;
        std::array<Glide, 3> gHead, gPan;
        Glide gBass, gTreble;
        double wowA = 0.0, flutterA = 0.0;

        // duck
        double duckEnv = 0.0, duckBlockPeak = 0.0, duckAttDb = 0.0;

        std::array<std::atomic<float>, 3> headPeak {}, headDelayMs {};
    };
}
