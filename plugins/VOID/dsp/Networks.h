#pragma once

#include "Filters.h"
#include "Primitives.h"
#include <array>

// The five reverberation networks of VOID (MODEL_SPEC 4 and 5; literature in
// RESEARCH.md). Each ALGORITHM is its own topology, not a parameter set on a
// shared network:
//
//   ROOM    image-source early reflections (shoebox, orders 1-2, 24 taps)
//           + 2 input allpasses + 8-line Householder FDN (Jot), ER-dominant
//   PLATE   4 input diffusers + Dattorro's figure-eight tank (two halves,
//           modulated and fixed allpasses in the loop), 7 output taps per side
//   CHAMBER image-source reflections (orders 1-3, 32 taps) through allpass
//           smear + two cross-coupled Gardner nested-allpass loops
//   HALL    sparse image-source reflections (16 taps) + 4 input diffusers +
//           16-line FDN with a Hadamard matrix, LF/HF two-band decay
//   VOID    two 6-stage "bloom" allpass chains (slow build) + 16-line
//           allpass-FDN: every line has a modulated delay and its own
//           Schroeder allpass in the loop, Householder matrix
//
// Common to all: every loop element is lossless except the Jot absorption
// filter of each line/half/loop, whose gain is computed from the element's
// actual loop length so that the reverberation time at 1 kHz equals DECAY
// (MODEL_SPEC 5.2). Modulated delays use allpass interpolation (lossless).
namespace tleet::voidrev
{
    inline constexpr int numAlgorithms = 5;
    enum AlgorithmIndex { algoRoom = 0, algoPlate, algoChamber, algoHall, algoVoid };

    // Control block (samples): controls, glides and coefficients update every K samples.
    inline constexpr int kControlBlock = 32;

    // Values the engine hands every network once per control block (block-end values).
    struct BlockParams
    {
        double fs = 48000.0;
        double sizePct = 50.0;    // effective SIZE (after DROWN)
        double lambda = 0.4;      // 1 / RT60(1 kHz), 1/s; 0 = lossless (infinite DECAY, FREEZE)
        double diffusion = 0.7;   // 0..1 effective DIFFUSION
        double modDepth = 0.3;    // 0..1 effective MOD DEPTH
        double modRateHz = 0.5;
        double ruin = 0.0;        // 0..1 COLOR weights (glided)
        double dark = 0.0;
        double loopSat = 0.0;     // 0..1 RUIN's in-loop saturation (0 while frozen)
        double loopW = 1.0;       // LOOP filter weight
        const filt::HpLp* loopCoefficients = nullptr; // HPF/LPF coefficients of this block
        bool hold = false;        // FREEZE fully engaged: hold the frozen level
    };

    struct NetOut { double eL = 0.0, eR = 0.0, lL = 0.0, lR = 0.0; };

    namespace design
    {
        inline constexpr double refHz = 1000.0;          // DECAY reference frequency
        inline constexpr double lateCeiling = 0.35;      // late-output RMS bound (per channel, -9.1 dBFS), MODEL_SPEC 6.4
        inline constexpr double boundDetectS = 0.05, boundAttackS = 0.02, boundReleaseS = 0.4;
        inline constexpr double holdDetectS = 1.0, holdSettleS = 5.0;   // FREEZE level hold (MODEL_SPEC 6.5)
        inline constexpr double holdTauS = 2.0, holdMaxDbPerS = 0.5, holdFloor = 1.0e-12;
        inline constexpr double satLevel = 0.7;          // RUIN in-loop saturation level
        inline constexpr double maxPitchDev = 0.03;      // modulation pitch deviation cap (3 %)

        // SIZE -> network scale: s0 * 2^(k (size - 50) / 50)
        struct SizeLaw { double s0, k; };
        inline constexpr SizeLaw sizeLaw[numAlgorithms] = { { 1.0, 1.0 }, { 0.7, 0.8 }, { 1.0, 0.7 }, { 1.0, 1.0 }, { 1.0, 1.1 } };
        inline double scaleFor (int algo, double sizePct) noexcept
        {
            const auto& l = sizeLaw[std::clamp (algo, 0, numAlgorithms - 1)];
            return l.s0 * std::exp2 (l.k * (std::clamp (sizePct, 0.0, 100.0) - 50.0) / 50.0);
        }
        inline double maxScale (int algo) noexcept { return scaleFor (algo, 100.0); }

        // Two-band decay: RT60 ratios re RT60(1 kHz) below loHz / above hiHz.
        struct DecayShape { double loRatio, loHz, hiRatio, hiHz; };
    }

    // ---- early reflections (image-source shoebox, Allen & Berkley 1979) ----------
    struct ErTap { double ms = 0.0, gL = 0.0, gR = 0.0; };
    struct ErTable
    {
        std::array<ErTap, 48> taps {};
        int count = 0;
        double maxMs = 0.0;
    };

    struct ShoeboxSpec
    {
        double lx, ly, lz;         // room (m) at scale 1
        double sx, sy, sz;         // source
        double rx, ry, rz;         // receiver (y = left/right axis)
        double beta;               // wall reflection coefficient
        int maxOrder, maxTaps;
    };
    // Image sources up to maxOrder, the maxTaps strongest, sorted by time; the
    // taps' summed power per output channel is normalised to 1.
    ErTable makeImageSourceTable (const ShoeboxSpec& s) noexcept;
    const ErTable& erTable (int algo) noexcept; // Room, Chamber, Hall (computed once)

    struct EarlyDesign
    {
        double gain;          // ER level
        double lateOnsetMs;   // late network fed from the ER line at this delay
        int diffStages;       // allpass smear on the ER sum (Chamber), up to 3
        double diffMs[2][3];
        double diffG;         // at DIFFUSION 1
    };

    class EarlyBank
    {
    public:
        template <class Carver> void layout (Carver& c, int algo, double fs);
        void configure (Arena& a, int algo, double fs);
        void reset (const BlockParams& bp) noexcept;
        void beginBlock (const BlockParams& bp) noexcept;
        // x: gated, pre-delayed input. Returns the ER (stereo) and the late feed.
        void process (double xL, double xR, double& eL, double& eR, double& lateL, double& lateR) noexcept;

    private:
        const ErTable* table = nullptr;
        const EarlyDesign* d = nullptr;
        int algo = 0;
        double fs = 48000.0;
        DelayLine line[2];
        std::array<SchroederAllpass, 3> diff[2];
        int diffLen[2][3] {};
        double spm = 48.0, spmStep = 0.0; // samples per ms at the current scale
        double dif = 0.7, difStep = 0.0;
    };

    // Loop energy bound and frozen-level hold (MODEL_SPEC 6.4, 6.5): one slow
    // gain on the whole loop.
    //  - bound: keeps the RMS of the network's late output (per channel) below
    //    lateCeiling; exactly 1 (transparent) below it.
    //  - hold (FREEZE fully engaged): a lossless loop whose delays are modulated
    //    is not exactly energy preserving (every pass through a delay that is
    //    lengthening or shortening stretches or compresses what it holds), so
    //    its level random-walks. The hold measures the frozen level for
    //    holdSettleS, then keeps it with a proportional servo (time constant
    //    holdTauS, at most holdMaxDbPerS; what it corrects is a few
    //    hundredths of a dB per second, far below audibility).
    // The network adds 0.5 (lL^2 + lR^2) to `acc` every sample; block() closes
    // a control block.
    struct LoopBound
    {
        double acc = 0.0, env = 0.0, slow = 0.0, ref = 0.0, cur = 1.0, end = 1.0, step = 0.0;
        double kEnv = 0.1, kAtt = 0.1, kRel = 0.01, kSlow = 0.01;
        int holdBlocks = 0, settleBlocks = 3000;
        void prepare (double controlRate) noexcept
        {
            kEnv = blockCoeff (design::boundDetectS, controlRate);
            kAtt = blockCoeff (design::boundAttackS, controlRate);
            kRel = blockCoeff (design::boundReleaseS, controlRate);
            kSlow = blockCoeff (design::holdDetectS, controlRate);
            settleBlocks = (int) std::ceil (design::holdSettleS * controlRate);
        }
        void reset() noexcept { acc = env = slow = ref = 0.0; cur = end = 1.0; step = 0.0; holdBlocks = 0; }
        void block (int samples, bool hold, double loopSeconds) noexcept
        {
            const double ms = acc / std::max (1, samples);
            acc = 0.0;
            env += kEnv * (ms - env);
            slow += kSlow * (ms - slow);
            const double c2 = design::lateCeiling * design::lateCeiling;
            const bool over = env > c2;
            double target = over ? design::lateCeiling / std::sqrt (env) : 1.0;
            if (! hold)
                holdBlocks = 0;
            else if (holdBlocks < settleBlocks)
            {
                ++holdBlocks; // what is still in the diffusers / bloom enters the loop
                ref = slow;
            }
            else if (ref > design::holdFloor && slow > design::holdFloor)
            {
                // correction rate (dB/s) proportional to the level error, as a
                // per-pass gain over the network's mean loop time
                const double errDb = 10.0 * std::log10 (ref / slow);
                const double rate = std::clamp (errDb / design::holdTauS, -design::holdMaxDbPerS, design::holdMaxDbPerS);
                const double h = std::pow (10.0, rate * loopSeconds / 20.0);
                target = over ? std::min (target, h) : h;
            }
            cur = end;
            double e = end + (target < end ? kAtt : kRel) * (target - end);
            if (same (target, 1.0) && std::abs (e - 1.0) < 1.0e-9)
                e = 1.0;
            step = (e - cur) / samples;
            end = e;
        }
        double next() noexcept { cur += step; return cur; }
    };

    // Safety for pathological settings only: a soft knee beyond |x| = 2 that
    // never lets a loop sample exceed 4 or become non-finite.
    inline double loopGuard (double v) noexcept
    {
        const double a = std::abs (v);
        if (a <= 2.0)
            return v;
        if (! std::isfinite (v))
            return 0.0;
        const double o = a - 2.0;
        return std::copysign (2.0 + o / (1.0 + 0.5 * o), v);
    }

    // RUIN's saturation: x / sqrt(1 + (x / L)^2) (odd, unity small-signal gain).
    inline double softSat (double x, double level) noexcept
    {
        const double u = x / level;
        return x / std::sqrt (1.0 + u * u);
    }

    // RUIN's input saturation f(x) = x / sqrt(1 + x^2) with first-order
    // antiderivative antialiasing (Parker et al. 2016; Bilbao et al. 2017):
    //   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),  F(x) = sqrt(1 + x^2)
    // which simplifies exactly to (x[n] + x[n-1]) / (F(x[n]) + F(x[n-1])):
    // no ill-conditioned difference, no special case. Costs half a sample of
    // delay and a gentle top-octave roll-off (MODEL_SPEC 8.3).
    inline double softSatAdaa (double x, double x1) noexcept
    {
        return (x + x1) / (std::sqrt (1.0 + x * x) + std::sqrt (1.0 + x1 * x1));
    }

    // ---- FDN (Room, Hall, Void) ----------------------------------------------------
    struct FdnDesign
    {
        int n;                    // lines (8 or 16)
        bool hadamard;            // Hadamard matrix (else Householder)
        double lineMs[16];
        double apMs[16];          // in-line allpass per line (0 = none)
        double apG;               // in-line allpass coefficient at DIFFUSION 1
        double modMs;             // delay excursion at MOD DEPTH 1
        unsigned modMask;         // modulated lines
        design::DecayShape decay;
        double lateGain;
        int diffStages;           // input diffusers per channel
        bool diffScaled;          // diffusers scale with SIZE (Void's bloom)
        double diffMs[2][6];
        double diffG[6];          // at DIFFUSION 1
        double earlyFromDiff;     // early output = diffuser output x this (Void); 0 = none
    };

    class FdnNet
    {
    public:
        template <class Carver> void layout (Carver& c, const FdnDesign& d, int algo, double fs);
        void configure (Arena& a, const FdnDesign& d, int algo, double fs);
        void reset (const BlockParams& bp) noexcept;
        void beginBlock (const BlockParams& bp) noexcept;
        void process (double xL, double xR, NetOut& o) noexcept;
        double getBoundGain() const noexcept { return bound.end; }
        double loopLengthSamples (int i) const noexcept { return dEnd[(size_t) i] + apEnd[(size_t) i]; }
        const filt::Absorption& absorption (int i) const noexcept { return absorb[(size_t) i]; }

    private:
        void computeTargets (const BlockParams& bp, bool snap) noexcept;

        const FdnDesign* d = nullptr;
        int algo = 0, n = 8;
        double fs = 48000.0;
        std::array<DelayLine, 16> line;
        std::array<AllpassRead, 16> rd;
        std::array<SchroederAllpass, 16> ap;
        std::array<filt::Absorption, 16> absorb;
        std::array<filt::HpLp, 16> loopF;
        std::array<SchroederAllpass, 6> diff[2];
        std::array<double, 16> dCur {}, dStep {}, dEnd {}, apCur {}, apStep {}, apEnd {};
        std::array<double, 16> gainStep {}, gainEnd {};
        std::array<double, 6> diffCur[2] {}, diffStep[2] {}, diffLoss[2] {};
        std::array<double, 16> apLoss {};
        std::array<double, 16> phase {}, rateMul {}, bL {}, bR {}, cL {}, cR {};
        std::array<Wander, 16> wander;
        filt::ToneCorrection toneL, toneR;
        LoopBound bound;
        double loopSamples = 2000.0; // mean loop time of one pass (for the hold servo)
        double dif = 0.7, difStep = 0.0, loopW = 1.0, loopWStep = 0.0, sat = 0.0, satStep = 0.0;
        bool loopActive = true;
    };

    // ---- Dattorro plate --------------------------------------------------------------
    class PlateNet
    {
    public:
        template <class Carver> void layout (Carver& c, double fs);
        void configure (Arena& a, double fs);
        void reset (const BlockParams& bp) noexcept;
        void beginBlock (const BlockParams& bp) noexcept;
        void process (double xL, double xR, NetOut& o) noexcept;
        double getBoundGain() const noexcept { return bound.end; }
        double halfLengthSamples (int half) const noexcept;

    private:
        void computeTargets (const BlockParams& bp, bool snap) noexcept;
        enum { ap1 = 0, d1, ap2, d2, numElems };

        double fs = 48000.0;
        std::array<SchroederAllpass, 4> inDiff;
        std::array<int, 4> inDiffLen {};
        std::array<double, 4> inDiffLoss {};
        double apModLoss[2] {}, apFixLoss[2] {};
        // per half: AP1 (modulated, delay inside the allpass), D1, AP2, D2
        SchroederAllpass apMod[2], apFix[2];
        DelayLine dl1[2], dl2[2];
        AllpassRead rd1[2], rd2[2];
        std::array<double, numElems> cur[2] {}, stp[2] {}, endv[2] {};
        filt::Absorption absorb[2];
        filt::HpLp loopF[2];
        filt::ToneCorrection toneL, toneR;
        double gainStep[2] {}, gainEnd[2] {};
        double phase[2] {};
        Wander wander[2];
        LoopBound bound;
        double loopSamples = 2000.0; // mean loop time of one pass (for the hold servo)
        double scaleCur = 0.7, scaleStep = 0.0;
        double dif = 0.7, difStep = 0.0, loopW = 1.0, loopWStep = 0.0, sat = 0.0, satStep = 0.0;
        double spm = 48.0; // samples per ms
        bool loopActive = true;
    };

    // ---- Gardner nested-allpass chamber -------------------------------------------------
    struct NestedAllpass
    {
        SchroederAllpass outer;             // its delay line is the outer delay
        std::array<SchroederAllpass, 2> inner;
        int innerCount = 0;
        double process (double x, double dOuter, const double* dInner, double gOuter, const double* gInner,
                        double lossOuter, const double* lossInner) noexcept
        {
            double u = lossOuter * outer.rd.read (outer.line, dOuter);
            for (int i = 0; i < innerCount; ++i)
                u = inner[(size_t) i].process (u, dInner[i], gInner[i], lossInner[i]);
            const double w = x + gOuter * u;
            outer.line.write (w);
            return u - gOuter * w;
        }
        void reset() noexcept { outer.reset(); for (auto& a : inner) a.reset(); }
    };

    class ChamberNet
    {
    public:
        template <class Carver> void layout (Carver& c, double fs);
        void configure (Arena& a, double fs);
        void reset (const BlockParams& bp) noexcept;
        void beginBlock (const BlockParams& bp) noexcept;
        void process (double xL, double xR, NetOut& o) noexcept;
        double getBoundGain() const noexcept { return bound.end; }
        double loopLengthSamples (int loop) const noexcept;

        // element indices of one loop: A outer, A inner, T1, B outer, B inner 1, B inner 2, T2, C outer, C inner, T3
        enum { aO = 0, aI, t1, bO, bI1, bI2, t2, cO, cI, t3, numElems };

    private:
        void computeTargets (const BlockParams& bp, bool snap) noexcept;

        double fs = 48000.0;
        NestedAllpass na[2], nb[2], nc[2];
        DelayLine tl[2][3];
        AllpassRead trd[2][3];
        std::array<double, numElems> cur[2] {}, stp[2] {}, endv[2] {}, loss[2] {};
        filt::Absorption absorb[2];
        filt::HpLp loopF[2];
        filt::ToneCorrection toneL, toneR;
        double gainStep[2] {}, gainEnd[2] {};
        double loopEnd[2] {};
        double phase[2] {};
        Wander wander[2];
        LoopBound bound;
        double loopSamples = 2000.0; // mean loop time of one pass (for the hold servo)
        double dif = 0.7, difStep = 0.0, loopW = 1.0, loopWStep = 0.0, sat = 0.0, satStep = 0.0;
        bool loopActive = true;
    };

    // ---- one algorithm instance with its own memory -------------------------------------
    // The engine runs two slots so that an ALGORITHM change crossfades (MODEL_SPEC 7.1).
    class ReverbSlot
    {
    public:
        // Floats the largest algorithm needs at this rate (SIZE 100 %, full excursion).
        static size_t memoryFloats (double fs);
        void prepare (double fs);              // allocates (message thread)
        void configure (int algo, const BlockParams& bp) noexcept; // carves, resets (audio thread, no allocation)
        void beginBlock (const BlockParams& bp) noexcept;
        void process (double xL, double xR, NetOut& o) noexcept;
        // Zeroes up to `maxFloats` more of the used memory; true when all is zero.
        bool clearStep (size_t maxFloats) noexcept;
        void clearAll() noexcept;
        int getAlgorithm() const noexcept { return algo; }
        size_t usedFloats() const noexcept { return arena.used(); }
        double getBoundGain() const noexcept;

        // test hooks
        const FdnNet& fdn() const noexcept { return fdnNet; }
        const PlateNet& plate() const noexcept { return plateNet; }
        const ChamberNet& chamber() const noexcept { return chamberNet; }

    private:
        Arena arena;
        double fs = 48000.0;
        int algo = algoHall;
        size_t clearPos = 0;
        EarlyBank early;
        FdnNet fdnNet;
        PlateNet plateNet;
        ChamberNet chamberNet;
    };

    const FdnDesign& fdnDesign (int algo) noexcept;   // Room, Hall, Void
    const EarlyDesign& earlyDesign (int algo) noexcept; // Room, Chamber, Hall
    const design::DecayShape& decayShape (int algo) noexcept;
    bool hasEarlyBank (int algo) noexcept;
}
