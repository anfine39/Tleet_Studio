#pragma once

#include "dsp/DspUtils.h"
#include "DetectorFilter.h"
#include <array>

namespace tleet::glue
{
    // Stereo VCA bus compressor model (SSL G-Series bus compressor DNA). See
    // plugins/GLUE/RESEARCH.md for the evidence and MODEL_SPEC.md for the
    // provenance of every block and constant.
    //
    //   audio L/R -> input coupling -> main VCAs (one control voltage) -> makeup
    //       -> output stage (Character) -> DC blocker -> out
    //   side-chain feed L/R (internal: the inputs, or the external key)
    //       -> SC HPF -> DARK emphasis -> side-chain VCA (threshold gain, tracks
    //          the applied gain reduction) -> full-wave peak rectifier per channel
    //       -> dominant (largest channel wins) -> slope stage (ratio, knee)
    //       -> attack resistor -> release network (one RC, or the Auto two-cap
    //          network) -> control voltage = gain reduction in dB
    //
    // The side-chain VCA follows the applied gain, so the detector sees the
    // gain-reduced signal: a feedback loop around the side-chain. The loop is
    // solved implicitly every sample (no unit delay), which keeps 10:1 at
    // 0.1 ms stable and gives the loop's real attack acceleration.
    //
    // Runs at the oversampled rate (2x the host rate in GlueProcessor).
    class GlueModel
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int numRatios = 5;      // 1.5, 2, 4, 10, infinity
        static constexpr int infiniteRatio = 4;
        static constexpr int numAttacks = 6;     // 0.1 .. 30 ms
        static constexpr int numReleases = 5;    // 0.1, 0.3, 0.6, 1.2 s, Auto
        static constexpr int autoRelease = 4;
        static constexpr int numCharacters = 3;  // Clean, TLEET, Burn

        void prepare (double sampleRateHz);
        void reset();

        // ---- controls (call per block). Threshold, makeup, ratio law,
        // character, DARK, CRUSH and the SC HPF glide; attack and release
        // switch the timing network on the next sample (no hidden envelope).
        // Threshold in dBFS peak at the detector (the 10:1 / infinity knee centre).
        void setThresholdDb (float dbfs) noexcept    { thresholdTarget = dbfs; }
        void setMakeupDb (float db) noexcept         { makeupTarget = dsp::dbToGain (db); }
        void setAttack (int index) noexcept          { attackIndex = std::clamp (index, 0, numAttacks - 1); }
        void setRelease (int index) noexcept         { releaseIndex = std::clamp (index, 0, numReleases - 1); }
        void setRatio (int index) noexcept           { ratioIndex = std::clamp (index, 0, numRatios - 1); }
        void setCharacter (int index) noexcept       { characterIndex = std::clamp (index, 0, numCharacters - 1); }
        void setDark (float amount) noexcept         { darkTarget = std::clamp (amount, 0.0f, 1.0f); }
        void setCrush (float amount) noexcept        { crushTarget = std::clamp (amount, 0.0f, 1.0f); }
        void setScHpf (float hz) noexcept            { hpfHz = hz; }
        // Detector source: the external key when true (and a key is passed to process()).
        void setUseExternalKey (bool external) noexcept { useExternal = external; }

        // Processes `numSamples` of `numAudio` audio channels in place. `key`
        // holds `numKey` external side-chain channels (may be null / 0); they
        // only ever feed the detector.
        void process (float* const* audio, int numAudio, const float* const* key, int numKey, int numSamples) noexcept;

        // Lowest applied VCA gain (linear, pre-makeup) since the last call.
        // GR telemetry is -20*log10 of this value.
        float takeMinGain() noexcept { const float g = minGainSinceRead; minGainSinceRead = 1.0f; return g; }
        float getCurrentGain() const noexcept { return gain; }
        float getGainReductionDb() const noexcept { return (float) capDb; }
        float getSlowCapacitorDb() const noexcept { return (float) slowDb; }
        double getSampleRate() const noexcept { return fs; }
        float getExternalBlend() const noexcept { return (float) extBlend; }
        const DetectorFilterControl& getDetectorFilter() const noexcept { return filterControl; }

        // Test hook: every applied VCA gain (linear) is written to `dest`
        // until `capacity` values are stored. Null disables it. Never allocates.
        void setGainTraceForTesting (float* dest, int capacity) noexcept { trace = dest; traceCapacity = capacity; traceCount = 0; }
        int getGainTraceCount() const noexcept { return traceCount; }

        // ---- static laws, shared with the tests and MODEL_SPEC.md

        // Slope stage per ratio. The detector level u is normalised to the
        // ratio's own threshold (knob + thresholdOffsetDb); the target GR is
        // slopeDb * softKnee(u - 1, knee): linear in the rectified amplitude,
        // like the hardware's slope amplifier and diode (MODEL_SPEC 2.3).
        struct RatioLaw
        {
            float ratio;              // nominal (label); infinity for the limiter
            float thresholdOffsetDb;  // lower ratios start lower
            float slopeDb;            // dB of GR per unit of normalised overshoot
            float knee;               // knee width on u (diode softness / slope gain)
            bool limiter;             // infinity: GR = overshoot in dB (MODEL_SPEC 2.6)
        };

        // Character: VCA and output-stage nonlinearity.
        struct CharacterLaw
        {
            float vcaEven;   // 2nd-order term of the VCA transfer at full depth
            float vcaOdd;    // 3rd-order term
            float vcaFloor;  // depth without gain reduction (0..1)
            float ampDrive;  // output stage (line amplifier) drive
            float ampBias;   // its asymmetry
        };

        static const RatioLaw& ratioLaw (int index) noexcept;
        static const CharacterLaw& characterLaw (int index) noexcept;

        // CRUSH at 100 % (scaled linearly by the amount)
        static constexpr float crushScGainDb      = 6.0f;  // detector sensitivity (threshold lowered)
        static constexpr float crushSlopeGain     = 1.0f;  // slope x (1 + 1.0): harder ratio
        static constexpr float crushReleaseScale  = 0.5f;  // release times x (1 - 0.5)
        static constexpr float crushDrive         = 3.0f;  // nonlinear terms x (1 + 3)

        // Auto release network (two capacitors, MODEL_SPEC 3.3): C1 = 0.47 uF
        // with 750 k discharge, C2 = 6.8 uF coupled through 91 k.
        static constexpr double autoMainS     = 750.0e3 * 0.47e-6;  // R1 C1 = 0.3525 s
        static constexpr double autoCouplingS = 91.0e3 * 0.47e-6;   // R2 C1 = 0.04277 s
        static constexpr double autoSlowS     = 91.0e3 * 6.8e-6;    // R2 C2 = 0.6188 s

        static float attackSeconds (int index) noexcept;
        static float releaseSeconds (int index) noexcept; // fixed positions; Auto -> autoMainS

        // Equilibrium share of the slope target the capacitor holds for a
        // constant target (the release resistor discharges while the attack
        // resistor charges): tau_r / (tau_a + tau_r).
        static double steadyStateFraction (int attack, int release, float crush) noexcept;

        // Target GR (dB) of the slope stage for detector level `uRel`
        // (linear, 1 = the ratio's threshold).
        static double slopeTarget (double uRel, double slopeDb, double knee) noexcept
        {
            return slopeDb * softKnee (uRel - 1.0, knee);
        }

        // Static gain reduction (dB) for a constant-envelope detector level
        // `detectorPeakDbfs` (after the detector filters): the equilibrium of
        // the loop, c = rho * T(level - c). MODEL_SPEC 2.5.
        static double staticGainReductionDb (double detectorPeakDbfs, double thresholdDbfs, int ratio,
                                             int attack, int release, float crush = 0.0f) noexcept;

        // Infinity: required GR = soft-kneed overshoot in dB, knee this wide
        // (the input-referred width of the 10:1 diode knee).
        static constexpr double limiterKneeDb = 2.0;

        static double softKnee (double x, double w) noexcept
        {
            if (x <= -0.5 * w) return 0.0;
            if (x >=  0.5 * w) return x;
            const double t = x + 0.5 * w;
            return t * t / (2.0 * w);
        }

        static double softKneeSlope (double x, double w) noexcept
        {
            if (x <= -0.5 * w) return 0.0;
            if (x >=  0.5 * w) return 1.0;
            return (x + 0.5 * w) / w;
        }

        // VCA: gain G with a small level- and depth-dependent transfer bend.
        static float vcaStage (float x, float G, float even, float odd, float depth) noexcept
        {
            float v = x * (1.0f / vcaReference);
            v = v / std::sqrt (1.0f + v * v * (1.0f / 16.0f)); // bounded, |v| < 4
            return G * x * (1.0f + depth * (even * v + odd * v * v));
        }

        // Output stage: asymmetric tanh normalised to unity small-signal gain.
        static float ampStage (float x, float drive, float bias) noexcept
        {
            if (drive < 1.0e-3f)
                return x;
            const float tb = std::tanh (drive * bias);
            const float norm = drive * (1.0f - tb * tb);
            return (std::tanh (drive * (x + bias)) - tb) / norm;
        }

        static constexpr float vcaReference = 0.5f;           // -6 dBFS: VCA term normalisation
        static constexpr double maxGainReductionDb = 60.0;
        static constexpr double ln10Over20 = 0.11512925464970229;

    private:
        void controlUpdate() noexcept;
        void releaseNetwork (double& L1, double& L0) const noexcept;
        double solveLoop (double a) noexcept;
        double solveLimiter (double levelOverThreshold) const noexcept;

        double fs = 96000.0;

        // controls
        float thresholdTarget = -20.0f, makeupTarget = 1.0f, darkTarget = 0.0f, crushTarget = 0.0f;
        float hpfHz = 20.0f;
        bool useExternal = false;
        int attackIndex = 4, releaseIndex = autoRelease, ratioIndex = 2, characterIndex = 1;

        // smoothed state
        double thresholdDb = -20.0, crush = 0.0;
        float makeupGain = 1.0f;
        double lawThreshold = 1.0, lawSlope = 1.0, lawKnee = 0.1; // glided RatioLaw (threshold linear)
        double limiterWeight = 0.0;                                // 0 = loop law, 1 = infinity law
        CharacterLaw voice {};
        double extBlend = 0.0, extStep = 0.001;
        double cs = 0.001, csLaw = 0.002; // per-sample smoothing coefficients
        double scGain = 1.0, scGainThreshold = 1.0e9, scGainCrush = -1.0;

        // timing network (per control block)
        double ka = 0, kr = 0, k1 = 0, k2a = 0, kb = 0;
        bool networkAuto = true;
        int controlCountdown = 0;

        // detector state
        double capDb = 0.0;   // C1: control voltage, as gain reduction in dB
        double slowDb = 0.0;  // C2 (Auto network)
        float gain = 1.0f;
        float minGainSinceRead = 1.0f;

        DetectorFilterControl filterControl;
        std::array<DetectorFilter, maxChannels> filterInt {}, filterExt {};
        std::array<dsp::DCBlocker, maxChannels> dcIn, dcKey, dcOut;

        float* trace = nullptr;
        int traceCapacity = 0, traceCount = 0;
    };
}
