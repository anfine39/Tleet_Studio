#pragma once

#include "dsp/DspUtils.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace tleet::punch
{
    // Feed-forward VCA compressor model, dbx 160A family. See
    // plugins/PUNCH/RESEARCH.md for the evidence and MODEL_SPEC.md for the
    // provenance of every block and constant.
    //
    //   audio L/R -> input coupling (0.5 Hz) -> VCA (one control voltage)
    //       -> GRIT: VCA colour + output-stage drive -> DC blocker -> out
    //   audio L/R (after coupling) -> true-RMS detector: one-pole mean square
    //       of the power sum (L^2 + R^2) / 2, tau = 34.7 ms (125 dB/s release)
    //       -> level in dB -> gain computer (threshold, slope, hard knee or
    //          OverEasy) -> control voltage = gain reduction in dB
    //
    // Feed-forward: the detector sees the input, never the gain-reduced
    // signal, so the static curve is exactly the gain computer and the
    // program-dependent attack and release are those of the RMS detector.
    //
    // Runs at the oversampled rate (2x the host rate in PunchProcessor).
    class PunchModel
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRateHz);
        void reset();

        // ---- controls (call per block; all glide over ~20 ms, no hidden
        // envelope on the detector itself).
        // Threshold in dB RMS re full scale (MODEL_SPEC 2.4).
        void setThresholdDb (float db) noexcept     { thresholdTarget = db; }
        // Compression slope s = 1 - 1/ratio, 0 (1:1) .. 1 (infinity).
        void setSlope (float s) noexcept            { slopeTarget = std::clamp (s, 0.0f, 1.0f); }
        void setOverEasy (bool on) noexcept         { overEasyTarget = on ? 1.0f : 0.0f; }
        // GRIT 0..1 (Clean .. Punch).
        void setGrit (float amount) noexcept        { gritTarget = std::clamp (amount, 0.0f, 1.0f); }

        // Processes `numSamples` of `numChannels` channels in place.
        void process (float* const* audio, int numChannels, int numSamples) noexcept;

        // Lowest applied VCA gain (linear) since the last call. GR telemetry
        // is -20*log10 of this value.
        float takeMinGain() noexcept { const float g = minGainSinceRead; minGainSinceRead = 1.0f; return g; }
        float getCurrentGain() const noexcept { return gain; }
        float getGainReductionDb() const noexcept { return (float) grDb; }
        // Detector level (dB RMS re full scale, the static curve's x axis).
        float getDetectorLevelDb() const noexcept { return (float) levelDb (meanSquare); }
        double getMeanSquare() const noexcept { return meanSquare; }
        double getSampleRate() const noexcept { return fs; }

        // Test hook: every applied VCA gain (linear) is written to `dest`
        // until `capacity` values are stored. Null disables it. Never allocates.
        void setGainTraceForTesting (float* dest, int capacity) noexcept { trace = dest; traceCapacity = capacity; traceCount = 0; }
        int getGainTraceCount() const noexcept { return traceCount; }

        // ---- static laws, shared with the editor, the tests and MODEL_SPEC.md

        // dbx 160A release rate (SOURCE_CONFIRMED) and the detector time
        // constant it implies: a mean square decaying as exp(-t/tau) falls
        // 10 log10(e) / tau dB per second.
        static constexpr double releaseRateDbPerSecond = 125.0;
        static constexpr double tenLog10e = 4.3429448190325183;
        static constexpr double detectorTauSeconds = tenLog10e / releaseRateDbPerSecond; // 34.74 ms

        // OverEasy: soft knee centred on the threshold, softplus of scale
        // overEasyScaleDb (MODEL_SPEC 2.3). Local slope goes 10 % -> 90 % of
        // the set slope over +-2.197 * scale = +-5.5 dB around the threshold.
        static constexpr double overEasyScaleDb = 2.5;
        static constexpr double maxGainReductionDb = 60.0;  // ">60 dB maximum compression"
        static constexpr double ln10Over20 = 0.11512925464970229;

        static double levelDb (double meanSquare) noexcept
        {
            return tenLog10e * std::log (std::max (meanSquare, 1.0e-30));
        }

        static double hardKnee (double overDb) noexcept { return overDb > 0.0 ? overDb : 0.0; }

        static double overEasyKnee (double overDb) noexcept
        {
            const double z = overDb / overEasyScaleDb;
            if (z > 40.0) return overDb;
            if (z < -40.0) return 0.0;
            return overEasyScaleDb * std::log1p (std::exp (z));
        }

        // The gain computer: gain reduction (dB, >= 0) for a detector level
        // (dB RMS re full scale), threshold (same unit), slope s = 1 - 1/ratio
        // and knee (0 = hard, 1 = OverEasy; in between while the switch glides).
        // Feed-forward, so this is also the static curve of the whole model.
        static double gainComputerDb (double detectorDb, double thresholdDb, double slope, double overEasy) noexcept
        {
            const double x = detectorDb - thresholdDb;
            const double hard = hardKnee (x);
            const double k = overEasy <= 0.0 ? hard : hard + overEasy * (overEasyKnee (x) - hard);
            return std::min (maxGainReductionDb, std::clamp (slope, 0.0, 1.0) * k);
        }

        // Static output level (dB) for a detector level: in - GR.
        static double staticOutputDb (double detectorDb, double thresholdDb, double slope, bool overEasy) noexcept
        {
            return detectorDb - gainComputerDb (detectorDb, thresholdDb, slope, overEasy ? 1.0 : 0.0);
        }

        // ---- GRIT (MODEL_SPEC 5): VCA colour and output-stage drive at 100 %.
        static constexpr float gritVcaEven  = 0.030f;  // 2nd-order term of the VCA transfer at full depth
        static constexpr float gritVcaOdd   = 0.015f;  // 3rd-order term
        static constexpr float gritDepthFloor = 0.1f;  // VCA term depth without gain reduction (0..1)
        static constexpr double gritDepthFullDb = 20.0; // GR at which the VCA term reaches full depth
        static constexpr float gritAmpDrive = 0.25f;   // output stage drive
        static constexpr float gritAmpBias  = -0.15f;  // output stage asymmetry (operating-point offset)
        static constexpr float vcaReference = 0.5f;    // -6 dBFS: VCA term normalisation
        static constexpr float vcaLimit = 4.0f;        // |v| bound (+6 dBFS at the VCA input)

        // VCA: gain G with a small level- and depth-dependent transfer bend.
        static float vcaStage (float x, float G, float even, float odd, float depth) noexcept
        {
            if (depth <= 0.0f)
                return G * x;
            // A pure 3rd-order polynomial up to +6 dBFS (harmonics 2 and 3 only,
            // which the 2x oversampling keeps from folding); held beyond.
            const float v = std::clamp (x * (1.0f / vcaReference), -vcaLimit, vcaLimit);
            return G * x * (1.0f + depth * (even * v + odd * v * v));
        }

        // Output stage: an asymmetric cubic soft clipper (the first two
        // terms of tanh, u - u^3/3, held at +-2/3 beyond |u| = 1), biased and
        // normalised to unity small-signal gain. Only 2nd and 3rd harmonics
        // below clipping, so the 2x oversampling keeps it alias-free.
        struct AmpStage
        {
            static float cubic (float u) noexcept
            {
                u = std::clamp (u, -1.0f, 1.0f);
                return u - u * u * u * (1.0f / 3.0f);
            }
            void set (float driveIn, float biasIn) noexcept
            {
                drive = driveIn;
                bias = biasIn;
                active = drive >= 1.0e-3f;
                const float ub = drive * bias;
                fb = cubic (ub);
                invNorm = active ? 1.0f / (drive * (1.0f - ub * ub)) : 1.0f;
            }
            float process (float x) const noexcept
            {
                return active ? (cubic (drive * (x + bias)) - fb) * invNorm : x;
            }
            float drive = 0.0f, bias = 0.0f, fb = 0.0f, invNorm = 1.0f;
            bool active = false;
        };

        static float ampStage (float x, float drive, float bias) noexcept
        {
            AmpStage a;
            a.set (drive, bias);
            return a.process (x);
        }

        // GRIT depth of the VCA term for an amount (0..1) and a GR (dB).
        static float gritDepth (float amount, double grDbNow) noexcept
        {
            const float g = (float) std::min (1.0, std::max (0.0, grDbNow) / gritDepthFullDb);
            return amount * (gritDepthFloor + (1.0f - gritDepthFloor) * g);
        }

    private:
        double fs = 96000.0;

        // controls
        float thresholdTarget = -15.0f, slopeTarget = 0.75f, overEasyTarget = 0.0f, gritTarget = 0.2f;

        // smoothed state: two cascaded one-poles per control (critically
        // damped glide: the gain's slope starts and ends at zero, no corner)
        struct Glide2
        {
            void snap (double v) noexcept { a = b = v; }
            double next (double target, double k) noexcept;
            double a = 0.0, b = 0.0;
        };
        Glide2 thresholdDb, slope, overEasy, grit;
        double cs = 0.001; // per-sample coefficient of each stage
        AmpStage amp;

        // detector
        double msCoeff = 0.0;
        double meanSquare = 0.0;
        double grDb = 0.0;
        float gain = 1.0f;
        float minGainSinceRead = 1.0f;

        // First-order DC blocker in double precision (the 0.5 Hz corner is
        // 3e-5 of the oversampled rate: float state would add rounding noise).
        struct Coupling
        {
            void prepare (double sampleRate, double hz) noexcept { r = std::exp (-2.0 * dsp::pi * hz / sampleRate); reset(); }
            void reset() noexcept { x1 = y1 = 0.0; }
            double process (double x) noexcept
            {
                const double y = x - x1 + r * y1;
                x1 = x;
                y1 = std::abs (y) < 1.0e-30 ? 0.0 : y;
                return y;
            }
            double r = 0.9999, x1 = 0.0, y1 = 0.0;
        };
        std::array<Coupling, maxChannels> dcIn, dcOut;

        float* trace = nullptr;
        int traceCapacity = 0, traceCount = 0;
    };
}
