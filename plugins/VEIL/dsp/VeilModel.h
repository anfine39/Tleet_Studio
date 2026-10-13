#pragma once

#include "Filters.h"
#include "TubeStages.h"
#include "VeilLaw.h"
#include <array>

namespace tleet::veil
{
    // Stereo variable-mu compressor model (Manley Variable Mu DNA). See
    // plugins/VEIL/RESEARCH.md for the evidence and MODEL_SPEC.md for the
    // provenance of every block and constant.
    //
    // host rate, preProcess():   x -> HPF (audio, optional) -> coupling HP (2 Hz)
    // 2x rate,   process():      a = input tube stage(x)
    //                            detector: a (or the key) -> SC HPF -> |.| -> two one-poles
    //                              -> level L (dBFS, sine peak) -> vari-mu law (target GR)
    //                              -> attack / recovery ballistics (GR in dB)
    //                              -> link (max of the two control voltages) -> g = 10^(-GR/20)
    //                            b = gain element(a, g)          (the compressed signal)
    //                            m = a + MIX (b - a)             (parallel blend)
    //                            y = output stage(m)             (SATURATION)
    // host rate, postProcess():  y -> LF weight shelf (CHARACTER)
    class VeilModel
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int controlInterval = 8;       // 2x-rate samples between control updates
        static constexpr double detectorTauS = 0.0025;  // each of the two rectifier smoothing poles [APPROXIMATION]
        static constexpr double controlGlideS = 0.02;   // threshold, compress, drive, ... glides
        static constexpr double rampS = 0.02;           // mix, link and key-source crossfades
        static constexpr double cornerS = 0.002;        // their corners are rounded with this one pole
        static constexpr double halfPi = 1.5707963267948966; // full-wave average -> sine peak

        // Detector level (dBFS, sine-peak calibrated) of a full-wave average.
        static double levelDbOfAverage (double avg) noexcept { return 20.0 * std::log10 (halfPi * avg + 1.0e-30); }

        // Exact one-pole coefficient of a time constant at a rate.
        static double coeffFor (double seconds, double rate) noexcept { return -std::expm1 (-1.0 / (seconds * rate)); }

        void prepare (double hostRate, int oversamplingFactor);
        void reset();

        // ---- controls (call per block)
        void setThresholdDbfs (float v) noexcept { thresholdTarget = v; }
        void setCompress (float v) noexcept      { compressTarget = std::clamp ((double) v, 0.0, 10.0); }
        void setAttackMs (float ms) noexcept;
        void setRecoveryMs (float ms) noexcept;
        void setMix (float v01) noexcept         { mixTarget = std::clamp ((double) v01, 0.0, 1.0); }
        void setTubeDrive (float v01) noexcept   { driveTarget = std::clamp ((double) v01, 0.0, 1.0); }
        void setHarmonics (float v01) noexcept   { harmonicsTarget = std::clamp ((double) v01, 0.0, 1.0); }
        void setSaturation (float v01) noexcept  { saturationTarget = std::clamp ((double) v01, 0.0, 1.0); }
        void setCharacter (int index) noexcept   { characterIndex = std::clamp (index, 0, tube::numCharacters - 1); }
        void setScHpf (float hz) noexcept        { scHpfControl.setTarget (hz); }
        void setHpf (float hz) noexcept          { hpfControl.setTarget (hz); }
        void setStereoLink (bool linked) noexcept { linkTarget = linked ? 1.0 : 0.0; }
        void setUseExternalKey (bool external) noexcept { useExternal = external; }

        // ---- processing
        void preProcess (float* const* io, int numChannels, int numSamples) noexcept;          // host rate
        void process (float* const* audio, int numAudio, const float* const* key, int numKey,
                      int numSamples) noexcept;                                                // 2x rate
        void postProcess (float* const* io, int numChannels, int numSamples) noexcept;         // host rate

        // ---- telemetry (audio thread). The GR (dB) the gain element actually
        // applied: per channel, the deepest over the last process() call and
        // the last sample. With one audio channel both report it.
        float getBlockMaxGrDb (int ch) const noexcept { return blockMaxGr[(size_t) std::clamp (ch, 0, 1)]; }
        float getCurrentGrDb (int ch) const noexcept  { return (float) cv[(size_t) std::clamp (ch, 0, 1)]; }
        float getAppliedGain (int ch) const noexcept  { return gain[(size_t) std::clamp (ch, 0, 1)]; }
        double getControlDb (int ch) const noexcept   { return cap[(size_t) std::clamp (ch, 0, 1)]; } // before the link
        double getDetectorLevelDb (int ch) const noexcept { return levelDbOfAverage (avg2[(size_t) std::clamp (ch, 0, 1)]); }
        double getTargetGrDb (int ch) const noexcept  { return target[(size_t) std::clamp (ch, 0, 1)]; }

        double getHostRate() const noexcept { return fsHost; }
        double getDetectorRate() const noexcept { return fsOs; }
        double getExternalBlend() const noexcept { return extBlend; }
        double getLinkBlend() const noexcept { return link; }
        const HighPassControl& getScHpfControl() const noexcept { return scHpfControl; }
        const HighPassControl& getHpfControl() const noexcept { return hpfControl; }

        // Test hook: every applied element gain (linear, 2x rate) of channel ch
        // is written to dest[ch] until `capacity` values are stored. Null disables it.
        void setGainTraceForTesting (float* left, float* right, int capacity) noexcept
        {
            trace[0] = left; trace[1] = right;
            traceCapacity = capacity;
            traceCount = 0;
        }
        int getGainTraceCount() const noexcept { return traceCount; }

    private:
        void controlUpdate (bool snapAll) noexcept;

        double fsHost = 48000.0, fsOs = 96000.0;

        // control targets
        float thresholdTarget = -18.0f;
        double compressTarget = 5.0, mixTarget = 1.0, driveTarget = 0.28, harmonicsTarget = 0.42, saturationTarget = 0.18;
        double linkTarget = 1.0, attackS = 0.05, recoveryS = 0.4;
        int characterIndex = 1;
        bool useExternal = false;

        // smoothed controls
        double thresholdDb = -18.0, compress = 5.0, drive = 0.28, harmonics = 0.42, saturation = 0.18;
        Voicing voice = tube::voicing (1);
        double mix = 1.0, link = 1.0, extBlend = 0.0, weightGain = 1.0;
        double mixLin = 1.0, linkLin = 1.0, extLin = 0.0, kRound = 0.005;
        double kGlideSample = 0.001, kGlideControl = 0.01, rampStep = 0.001;
        double ka = 0.001, kr = 0.0001, kd = 0.01;
        double weightCoeff = 0.0, kWeightHost = 0.001, couplingCoeffHost = 0.0, couplingCoeffOs = 0.0;
        int controlCountdown = 0;
        law::Constants lawConst {};
        double kneeStart = -6.0;
        TubeCoeffs tc {};

        // per-channel state
        std::array<double, maxChannels> avg1 {}, avg2 {}, yPrev {}, xPrev {}, slopePrev { 1.0, 1.0 }, target {}, cap {}, cv {};
        std::array<float, maxChannels> gain { 1.0f, 1.0f }, blockMaxGr {};
        std::array<HighPassState, maxChannels> hpf {}, scInternal {}, scKey {};
        std::array<OnePole, maxChannels> couplingIn {}, couplingKey {}, weight {};
        std::array<TubeChannel, maxChannels> tubes {};
        HighPassControl hpfControl, scHpfControl;

        float* trace[maxChannels] { nullptr, nullptr };
        int traceCapacity = 0, traceCount = 0;
    };
}
