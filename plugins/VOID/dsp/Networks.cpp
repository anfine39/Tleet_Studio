#include "Networks.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace tleet::voidrev
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double dattorroRate = 29761.0; // Dattorro (1997) tank figures are samples at 29.761 kHz

        inline double msOfDattorro (double samples) noexcept { return samples * 1000.0 / dattorroRate; }

        // Sylvester-Hadamard sign: row k, column i
        inline double hsign (int i, int k) noexcept { return (std::popcount ((unsigned) (i & k)) & 1) ? -1.0 : 1.0; }

        inline void fwht (double* v, int n) noexcept
        {
            for (int h = 1; h < n; h <<= 1)
                for (int i = 0; i < n; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const double a = v[j], b = v[j + h];
                        v[j] = a + b;
                        v[j + h] = a - b;
                    }
            const double s = 1.0 / std::sqrt ((double) n);
            for (int i = 0; i < n; ++i)
                v[i] *= s;
        }

        inline void householder (double* v, int n) noexcept
        {
            double s = 0.0;
            for (int i = 0; i < n; ++i)
                s += v[i];
            s *= 2.0 / n;
            for (int i = 0; i < n; ++i)
                v[i] -= s;
        }

        inline int lenFor (double ms, double fs, double extraSamples = 0.0) noexcept
        {
            return (int) std::ceil (ms * fs * 0.001 + extraSamples) + 8;
        }

        // Mod excursion (samples) at depth, capped so that the pitch deviation
        // 2 pi f E stays below maxPitchDev (MODEL_SPEC 6.6).
        inline double excursionSamples (double modMs, double depth, double rateHz, double fs) noexcept
        {
            const double e = modMs * 0.001 * std::clamp (depth, 0.0, 1.0);
            const double cap = design::maxPitchDev / (twoPi * std::max (0.01, rateHz));
            return std::min (e, cap) * fs;
        }

        // Decay of D samples spent in the late path: gamma^D, gamma^fs = 10^(-3 lambda)
        // (-60 dB per RT60). Applied to every delay of the late path (Jot's
        // substitution z^-1 -> gamma z^-1): the late response is the lossless
        // network's times gamma^n, whatever the allpasses do (MODEL_SPEC 5.1).
        inline double lossFor (double D, double lambda, double fs) noexcept
        {
            return lambda > 0.0 ? std::pow (10.0, -3.0 * lambda * D / fs) : 1.0;
        }

        // Jot two-band absorption of a loop element: mid-band gain gamma^Lmid for
        // the pure delays it stands for; the DC/Nyquist ratios give the bands'
        // RT60 ratios over the element's whole loop length Lshape (MODEL_SPEC 5.2).
        inline void designAbsorption (filt::Absorption& a, double Lmid, double Lshape, double fs, double lambda, double loRatio,
                                      double hiRatio, double gLo, double gHi, filt::cplx ziRef) noexcept
        {
            const double e = -3.0 * lambda * Lshape / fs; // log10 of the mid-band gain over the whole element
            const double mid = lossFor (Lmid, lambda, fs);
            const double dc = std::pow (10.0, e * (1.0 / loRatio - 1.0));
            const double ny = std::pow (10.0, e * (1.0 / hiRatio - 1.0));
            a.design (mid, dc, ny, gLo, gHi, ziRef);
        }

        // Effective HF decay ratio and corner with the COLOR weights.
        inline double hiRatioFor (const design::DecayShape& s, double dark, double ruin) noexcept
        {
            return s.hiRatio * (1.0 - 0.55 * dark) * (1.0 - 0.3 * ruin);
        }
        inline double hiHzFor (const design::DecayShape& s, double dark, double ruin) noexcept
        {
            return s.hiHz * (1.0 - 0.45 * dark) * (1.0 - 0.2 * ruin);
        }

        // Jot's tone correction: equalises the initial spectrum of the late field
        // for the algorithm's own decay shape (|T|^2 ~ RT60(1 kHz) / RT60(f)).
        inline void designToneCorrection (filt::ToneCorrection& t, const design::DecayShape& s, double fs) noexcept
        {
            t.design (1.0, std::sqrt (1.0 / s.loRatio), std::sqrt (1.0 / s.hiRatio), filt::integratorGain (s.loHz, fs),
                      filt::integratorGain (s.hiHz, fs), filt::zInv (design::refHz, fs), false);
        }

        // Wander filter coefficient for a cutoff at the control rate.
        inline double wanderG (double rateHz, double controlRate) noexcept
        {
            const double fc = std::clamp (1.5 * rateHz, 0.05, 8.0);
            const double g = std::tan (filt::pi * std::min (fc, 0.45 * controlRate) / controlRate);
            return g / (1.0 + g);
        }

        // Diffusion with RUIN's smear (MODEL_SPEC 8.3).
        inline double diffusionWithRuin (const BlockParams& bp) noexcept
        {
            return std::clamp (bp.diffusion + 0.2 * bp.ruin, 0.0, 1.0);
        }

        // ---------------------------------------------------------------- designs
        const design::DecayShape decayShapes[numAlgorithms] = {
            { 1.00, 250.0, 0.42, 4500.0 },  // Room
            { 1.00, 200.0, 0.65, 5000.0 },  // Plate: bright, long HF
            { 1.10, 300.0, 0.50, 4500.0 },  // Chamber
            { 1.25, 300.0, 0.50, 3500.0 },  // Hall: longer bass, darker HF
            { 1.15, 200.0, 0.72, 3000.0 },  // Void
        };

        const FdnDesign roomDesign {
            8, false,
            { 21.1, 24.7, 28.3, 32.9, 36.1, 40.3, 44.9, 49.7 },
            {},
            0.0,
            0.12, 0xFFu,
            decayShapes[algoRoom],
            0.62,
            2, false,
            { { 3.1, 4.3 }, { 3.4, 4.7 } },
            { 0.6, 0.6 },
            0.0 };

        const FdnDesign hallDesign {
            16, true,
            { 43.1, 46.9, 51.7, 55.3, 60.1, 64.9, 70.3, 75.7, 81.1, 87.7, 94.3, 101.9, 109.1, 118.3, 127.7, 139.3 },
            {},
            0.0,
            0.45, 0xAAAAu,
            decayShapes[algoHall],
            1.85,
            4, false,
            { { 4.771, 3.595, 12.735, 9.307 }, { 5.171, 3.887, 13.791, 10.087 } },
            { 0.75, 0.75, 0.625, 0.625 },
            0.0 };

        const FdnDesign voidDesign {
            16, false,
            { 61.3, 67.9, 73.1, 79.7, 86.3, 93.1, 101.3, 109.9, 118.7, 127.1, 137.9, 149.3, 160.1, 173.9, 187.3, 203.9 },
            { 9.6, 4.1, 12.8, 6.4, 15.2, 8.1, 4.9, 11.3, 7.3, 14.5, 5.7, 10.4, 16.1, 8.9, 13.7, 12.1 },
            0.6,
            1.6, 0xFFFFu,
            decayShapes[algoVoid],
            4.5,
            6, true,
            { { 8.9, 14.3, 23.1, 37.3, 60.4, 97.7 }, { 9.6, 15.4, 24.8, 40.1, 64.9, 104.6 } },
            { 0.72, 0.72, 0.72, 0.72, 0.72, 0.72 },
            0.5 };

        const EarlyDesign earlyDesigns[numAlgorithms] = {
            { 1.00, 5.0, 0, { { 0, 0, 0 }, { 0, 0, 0 } }, 0.0 },                // Room: discrete, localised reflections
            { 0.0, 0.0, 0, { { 0, 0, 0 }, { 0, 0, 0 } }, 0.0 },                  // Plate (none)
            { 0.70, 3.0, 3, { { 1.9, 3.7, 6.1 }, { 2.3, 4.3, 6.9 } }, 0.85 },     // Chamber: dense smeared cluster
            { 0.50, 15.0, 0, { { 0, 0, 0 }, { 0, 0, 0 } }, 0.0 },               // Hall
            { 0.0, 0.0, 0, { { 0, 0, 0 }, { 0, 0, 0 } }, 0.0 },                  // Void (none)
        };

        // Dattorro (1997) plate: input diffusers and tank, samples at 29.761 kHz
        constexpr double plateInDiff[4] = { 142.0, 107.0, 379.0, 277.0 };
        constexpr double plateInDiffG[4] = { 0.75, 0.75, 0.625, 0.625 };
        constexpr double plateTank[2][4] = { { 672.0, 4453.0, 1800.0, 3720.0 }, { 908.0, 4217.0, 2656.0, 3163.0 } };
        constexpr double plateDecayDiff1 = 0.70, plateDecayDiff2 = 0.50;
        constexpr double plateModMs = 0.8, plateEarly = 0.35, plateLate = 0.6;
        struct PlateTap { int half, elem; double samples, sign; };
        // Dattorro's output taps (Table 2): yL from the right half first, yR from the left
        constexpr PlateTap plateTapsL[7] = { { 1, 1, 266, 1 }, { 1, 1, 2974, 1 }, { 1, 2, 1913, -1 }, { 1, 3, 1996, 1 },
                                             { 0, 1, 1990, -1 }, { 0, 2, 187, -1 }, { 0, 3, 1066, -1 } };
        constexpr PlateTap plateTapsR[7] = { { 0, 1, 353, 1 }, { 0, 1, 3627, 1 }, { 0, 2, 1228, -1 }, { 0, 3, 2673, 1 },
                                             { 1, 1, 2111, -1 }, { 1, 2, 335, -1 }, { 1, 3, 121, -1 } };

        // Chamber: two Gardner-style nested-allpass loops (ms, coefficients at DIFFUSION 1)
        constexpr double chamberMs[2][ChamberNet::numElems] = {
            { 19.7, 5.3, 7.1, 29.3, 7.7, 3.1, 31.1, 11.9, 4.1, 23.3 },
            { 21.3, 5.9, 6.3, 27.7, 8.3, 3.4, 34.3, 13.1, 4.6, 21.1 } };
        constexpr double chamberG[ChamberNet::numElems] = { 0.45, 0.6, 0.0, 0.4, 0.5, 0.6, 0.0, 0.45, 0.5, 0.0 };
        constexpr double chamberModMs = 0.35, chamberRotation = 0.6, chamberLate = 0.5;
    }

    // ================================================================ tables
    const FdnDesign& fdnDesign (int algo) noexcept
    {
        return algo == algoRoom ? roomDesign : (algo == algoVoid ? voidDesign : hallDesign);
    }
    const EarlyDesign& earlyDesign (int algo) noexcept { return earlyDesigns[std::clamp (algo, 0, numAlgorithms - 1)]; }
    const design::DecayShape& decayShape (int algo) noexcept { return decayShapes[std::clamp (algo, 0, numAlgorithms - 1)]; }
    bool hasEarlyBank (int algo) noexcept { return algo == algoRoom || algo == algoChamber || algo == algoHall; }

    ErTable makeImageSourceTable (const ShoeboxSpec& s) noexcept
    {
        struct Cand { double ms, g, pan; };
        std::array<Cand, 256> cand {};
        int nc = 0;
        constexpr double c = 343.0;
        const double d0 = std::sqrt ((s.sx - s.rx) * (s.sx - s.rx) + (s.sy - s.ry) * (s.sy - s.ry) + (s.sz - s.rz) * (s.sz - s.rz));
        for (int kx = -2; kx <= 2; ++kx)
            for (int px = 0; px <= 1; ++px)
                for (int ky = -2; ky <= 2; ++ky)
                    for (int py = 0; py <= 1; ++py)
                        for (int kz = -2; kz <= 2; ++kz)
                            for (int pz = 0; pz <= 1; ++pz)
                            {
                                const int order = std::abs (2 * kx - px) + std::abs (2 * ky - py) + std::abs (2 * kz - pz);
                                if (order < 1 || order > s.maxOrder || nc >= (int) cand.size())
                                    continue;
                                const double ix = (1 - 2 * px) * s.sx + 2.0 * kx * s.lx;
                                const double iy = (1 - 2 * py) * s.sy + 2.0 * ky * s.ly;
                                const double iz = (1 - 2 * pz) * s.sz + 2.0 * kz * s.lz;
                                const double dx = ix - s.rx, dy = iy - s.ry, dz = iz - s.rz;
                                const double dist = std::sqrt (dx * dx + dy * dy + dz * dz);
                                cand[(size_t) nc++] = { std::max (0.2, (dist - d0) / c * 1000.0), std::pow (s.beta, order) * d0 / dist, dy / dist };
                            }
        std::sort (cand.begin(), cand.begin() + nc, [] (const Cand& a, const Cand& b) { return a.g > b.g; });
        const int n = std::min ({ nc, s.maxTaps, 48 });
        std::sort (cand.begin(), cand.begin() + n, [] (const Cand& a, const Cand& b) { return a.ms < b.ms; });
        ErTable t;
        double power = 0.0;
        for (int i = 0; i < n; ++i)
            power += cand[(size_t) i].g * cand[(size_t) i].g;
        const double norm = power > 0.0 ? 1.0 / std::sqrt (power) : 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto& k = cand[(size_t) i];
            const double g = k.g * norm;
            t.taps[(size_t) i] = { k.ms, g * std::sqrt (0.5 * (1.0 + k.pan)), g * std::sqrt (0.5 * (1.0 - k.pan)) };
            t.maxMs = std::max (t.maxMs, k.ms);
        }
        t.count = n;
        return t;
    }

    const ErTable& erTable (int algo) noexcept
    {
        //                                      room (m)            source              receiver            beta  order taps
        static const ErTable room = makeImageSourceTable ({ 6.2, 4.6, 3.0, 2.0, 1.4, 1.5, 4.3, 3.1, 1.6, 0.82, 2, 24 });
        static const ErTable chamber = makeImageSourceTable ({ 4.4, 3.6, 2.9, 1.1, 1.0, 1.3, 3.2, 2.5, 1.5, 0.90, 3, 32 });
        static const ErTable hall = makeImageSourceTable ({ 30.0, 19.0, 12.0, 7.5, 10.5, 1.8, 19.0, 8.0, 1.7, 0.80, 2, 16 });
        return algo == algoRoom ? room : (algo == algoChamber ? chamber : hall);
    }

    // ================================================================ EarlyBank
    template <class Carver>
    void EarlyBank::layout (Carver& c, int a, double sr)
    {
        const auto& t = erTable (a);
        const auto& ed = earlyDesign (a);
        const int len = lenFor (std::max (t.maxMs, ed.lateOnsetMs) * design::maxScale (a), sr, 4.0);
        for (auto& l : line)
            l.attach (c.carve ((size_t) len), len);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < ed.diffStages; ++s)
            {
                const int dl = std::max (2, (int) std::lround (ed.diffMs[ch][s] * sr * 0.001));
                diff[ch][(size_t) s].line.attach (c.carve ((size_t) dl + 4), dl + 4);
                diffLen[ch][s] = dl;
            }
    }

    void EarlyBank::configure (Arena& a, int al, double sr)
    {
        algo = al;
        fs = sr;
        table = &erTable (al);
        d = &earlyDesign (al);
        layout (a, al, sr);
    }

    void EarlyBank::reset (const BlockParams& bp) noexcept
    {
        for (auto& ch : diff)
            for (auto& s : ch)
                s.reset();
        spm = fs * 0.001 * design::scaleFor (algo, bp.sizePct);
        spmStep = 0.0;
        dif = diffusionWithRuin (bp);
        difStep = 0.0;
    }

    void EarlyBank::beginBlock (const BlockParams& bp) noexcept
    {
        const double e = fs * 0.001 * design::scaleFor (algo, bp.sizePct);
        spmStep = (e - spm) / kControlBlock;
        difStep = (diffusionWithRuin (bp) - dif) / kControlBlock;
    }

    void EarlyBank::process (double xL, double xR, double& eL, double& eR, double& lateL, double& lateR) noexcept
    {
        spm += spmStep;
        dif += difStep;
        double l = 0.0, r = 0.0;
        for (int i = 0; i < table->count; ++i)
        {
            const auto& t = table->taps[(size_t) i];
            const double pos = t.ms * spm;
            const double a = line[0].readCubic (pos), b = line[1].readCubic (pos);
            l += t.gL * a + t.gR * b;
            r += t.gR * a + t.gL * b;
        }
        for (int s = 0; s < d->diffStages; ++s)
        {
            const double g = d->diffG * dif;
            l = diff[0][(size_t) s].processInt (l, diffLen[0][s], g);
            r = diff[1][(size_t) s].processInt (r, diffLen[1][s], g);
        }
        eL = d->gain * l;
        eR = d->gain * r;
        const double onset = d->lateOnsetMs * spm;
        lateL = line[0].readCubic (onset);
        lateR = line[1].readCubic (onset);
        line[0].write (xL);
        line[1].write (xR);
    }

    // ================================================================ FdnNet
    template <class Carver>
    void FdnNet::layout (Carver& c, const FdnDesign& des, int a, double sr)
    {
        const double ms = design::maxScale (a);
        const double exc = 1.7 * des.modMs * sr * 0.001;
        for (int i = 0; i < des.n; ++i)
        {
            const int len = lenFor (des.lineMs[i] * ms, sr, exc);
            line[(size_t) i].attach (c.carve ((size_t) len), len);
            if (des.apMs[i] > 0.0)
            {
                const int al = lenFor (des.apMs[i] * ms, sr);
                ap[(size_t) i].line.attach (c.carve ((size_t) al), al);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < des.diffStages; ++s)
            {
                const int len = lenFor (des.diffMs[ch][s] * (des.diffScaled ? ms : 1.0), sr);
                diff[ch][(size_t) s].line.attach (c.carve ((size_t) len), len);
            }
    }

    void FdnNet::configure (Arena& a, const FdnDesign& des, int al, double sr)
    {
        d = &des;
        algo = al;
        n = des.n;
        fs = sr;
        layout (a, des, al, sr);
        const double inNorm = 1.0 / std::sqrt (0.5 * n), outNorm = 1.0 / std::sqrt ((double) n);
        for (int i = 0; i < n; ++i)
        {
            // L feeds the even lines, R the odd ones, with Hadamard sign patterns;
            // the outputs are two orthogonal Hadamard rows (decorrelated L/R).
            bL[(size_t) i] = (i % 2 == 0) ? inNorm * hsign (i, 3) : 0.0;
            bR[(size_t) i] = (i % 2 == 1) ? inNorm * hsign (i, 6) : 0.0;
            cL[(size_t) i] = outNorm * hsign (i, 5);
            cR[(size_t) i] = outNorm * hsign (i, n == 16 ? 10 : 3);
            // modulation: rates spread by the golden ratio, phases by its complement
            const double gr = 0.6180339887498949;
            rateMul[(size_t) i] = 0.7 + 0.6 * (i * gr - std::floor (i * gr));
            wander[(size_t) i].reset ((std::uint64_t) 0xC0FFEE + (std::uint64_t) 7919 * (std::uint64_t) i);
        }
        bound.prepare (fs / kControlBlock);
        designToneCorrection (toneL, des.decay, fs);
        designToneCorrection (toneR, des.decay, fs);
    }

    void FdnNet::computeTargets (const BlockParams& bp, bool snap) noexcept
    {
        const double scale = design::scaleFor (algo, bp.sizePct);
        const double spm = fs * 0.001 * scale;
        const double controlRate = fs / kControlBlock;
        const double exc = excursionSamples (d->modMs, bp.modDepth, bp.modRateHz, fs);
        const double G = wanderG (bp.modRateHz, controlRate), norm = Wander::normFor (G);
        const double sinAmp = 1.0 - 0.3 * bp.ruin, wanderAmp = 0.25 + 0.75 * bp.ruin;
        const double gLo = filt::integratorGain (d->decay.loHz, fs);
        const double gHi = filt::integratorGain (hiHzFor (d->decay, bp.dark, bp.ruin), fs);
        const double hiRatio = hiRatioFor (d->decay, bp.dark, bp.ruin);
        const auto ziRef = filt::zInv (design::refHz, fs);
        const double k = (double) kControlBlock;
        double meanLoop = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const size_t u = (size_t) i;
            double off = 0.0;
            if (! snap)
                phase[u] += bp.modRateHz * rateMul[u] * k / fs;
            phase[u] -= std::floor (phase[u]);
            const double w = wander[u].next (G, norm);
            if (((d->modMask >> i) & 1u) != 0 && exc > 0.0)
            {
                off = exc * (sinAmp * std::sin (twoPi * (phase[u] + 0.381966 * i)) + wanderAmp * std::clamp (w, -2.0, 2.0));
                off = std::clamp (off, -1.6 * exc, 1.6 * exc);
            }
            const double base = d->lineMs[i] * spm;
            const double de = std::max (2.0, base + off);
            const double ae = d->apMs[i] > 0.0 ? std::max (2.0, d->apMs[i] * spm) : 0.0;

            // absorption from the element's mean loop length (line + in-line allpass)
            const double g0 = gainEnd[u];
            designAbsorption (absorb[u], base, base + ae, fs, bp.lambda, d->decay.loRatio, hiRatio, gLo, gHi, ziRef);
            absorb[u].rampShelves (kControlBlock, snap);
            meanLoop += (base + ae) / n;
            apLoss[u] = lossFor (ae, bp.lambda, fs);
            const double g1 = absorb[u].gain;
            if (snap)
            {
                dCur[u] = de; dStep[u] = 0.0;
                apCur[u] = ae; apStep[u] = 0.0;
                gainStep[u] = 0.0;
            }
            else
            {
                dCur[u] = dEnd[u]; dStep[u] = (de - dEnd[u]) / k;
                apCur[u] = apEnd[u]; apStep[u] = (ae - apEnd[u]) / k;
                absorb[u].gain = g0;
                gainStep[u] = (g1 - g0) / k;
            }
            dEnd[u] = de;
            apEnd[u] = ae;
            gainEnd[u] = g1;
            if (bp.loopCoefficients != nullptr)
                loopF[u].copyCoefficients (*bp.loopCoefficients);
        }
        loopSamples = meanLoop;

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < d->diffStages; ++s)
            {
                const double e = d->diffMs[ch][s] * (d->diffScaled ? spm : fs * 0.001);
                const size_t u = (size_t) s;
                diffLoss[ch][u] = lossFor (d->diffScaled ? e : std::floor (e), bp.lambda, fs);
                diffStep[ch][u] = snap ? 0.0 : (e - diffCur[ch][u]) / k;
                if (snap) diffCur[ch][u] = e;
            }

        const double difT = diffusionWithRuin (bp);
        if (snap)
        {
            dif = difT; difStep = 0.0;
            loopW = bp.loopW; loopWStep = 0.0;
            sat = bp.loopSat; satStep = 0.0;
        }
        else
        {
            difStep = (difT - dif) / k;
            loopWStep = (bp.loopW - loopW) / k;
            satStep = (bp.loopSat - sat) / k;
        }
        const bool active = loopW > 0.0 || bp.loopW > 0.0;
        if (active && ! loopActive)
            for (auto& f : loopF) f.reset(); // the weight starts from 0: no click
        loopActive = active;
    }

    void FdnNet::reset (const BlockParams& bp) noexcept
    {
        for (auto& r : rd) r.reset();
        for (auto& a : ap) a.reset();
        for (auto& a : absorb) a.reset();
        for (auto& f : loopF) f.reset();
        for (auto& ch : diff) for (auto& a : ch) a.reset();
        toneL.reset(); toneR.reset();
        bound.reset();
        for (int i = 0; i < 16; ++i)
        {
            phase[(size_t) i] = 0.0;
            wander[(size_t) i].reset ((std::uint64_t) 0xC0FFEE + (std::uint64_t) 7919 * (std::uint64_t) i);
        }
        loopActive = true;
        computeTargets (bp, true);
    }

    void FdnNet::beginBlock (const BlockParams& bp) noexcept
    {
        bound.block (kControlBlock, bp.hold, loopSamples / fs);
        computeTargets (bp, false);
    }

    void FdnNet::process (double xL, double xR, NetOut& o) noexcept
    {
        dif += difStep;
        loopW += loopWStep;
        sat += satStep;

        // input diffusion (Room, Hall) / bloom (Void)
        double uL = xL, uR = xR;
        for (int s = 0; s < d->diffStages; ++s)
        {
            const size_t u = (size_t) s;
            const double g = d->diffG[s] * dif;
            if (d->diffScaled)
            {
                diffCur[0][u] += diffStep[0][u];
                diffCur[1][u] += diffStep[1][u];
                uL = diff[0][u].process (uL, diffCur[0][u], g, diffLoss[0][u]);
                uR = diff[1][u].process (uR, diffCur[1][u], g, diffLoss[1][u]);
            }
            else
            {
                uL = diff[0][u].processInt (uL, (int) diffCur[0][u], g, diffLoss[0][u]);
                uR = diff[1][u].processInt (uR, (int) diffCur[1][u], g, diffLoss[1][u]);
            }
        }
        o.eL = d->earlyFromDiff * uL;
        o.eR = d->earlyFromDiff * uR;

        double v[16];
        const double gAp = d->apG * dif;
        for (int i = 0; i < n; ++i)
        {
            const size_t u = (size_t) i;
            dCur[u] += dStep[u];
            double x = rd[u].read (line[u], dCur[u]);
            if (d->apMs[i] > 0.0)
            {
                apCur[u] += apStep[u];
                x = ap[u].process (x, apCur[u], gAp, apLoss[u]);
            }
            absorb[u].gain += gainStep[u];
            x = absorb[u].process (x);
            if (loopActive)
                x += loopW * (loopF[u].process (x) - x);
            if (sat > 0.0)
                x += sat * (softSat (x, design::satLevel) - x);
            v[i] = x;
        }

        double yL = 0.0, yR = 0.0;
        for (int i = 0; i < n; ++i)
        {
            yL += cL[(size_t) i] * v[i];
            yR += cR[(size_t) i] * v[i];
        }
        if (d->hadamard)
            fwht (v, n);
        else
            householder (v, n);
        const double gl = bound.next();
        for (int i = 0; i < n; ++i)
            line[(size_t) i].write (loopGuard (gl * v[i] + bL[(size_t) i] * uL + bR[(size_t) i] * uR));

        o.lL = d->lateGain * toneL.process (yL);
        o.lR = d->lateGain * toneR.process (yR);
        bound.acc += 0.5 * (o.lL * o.lL + o.lR * o.lR);
    }

    // ================================================================ PlateNet
    template <class Carver>
    void PlateNet::layout (Carver& c, double sr)
    {
        const double ms = design::maxScale (algoPlate);
        for (int s = 0; s < 4; ++s)
        {
            const int dl = std::max (2, (int) std::lround (msOfDattorro (plateInDiff[s]) * sr * 0.001));
            inDiff[(size_t) s].line.attach (c.carve ((size_t) dl + 4), dl + 4);
            inDiffLen[(size_t) s] = dl;
        }
        const double exc = 1.7 * plateModMs * sr * 0.001;
        for (int h = 0; h < 2; ++h)
        {
            int len = lenFor (msOfDattorro (plateTank[h][0]) * ms, sr, exc);
            apMod[h].line.attach (c.carve ((size_t) len), len);
            len = lenFor (msOfDattorro (plateTank[h][1]) * ms, sr);
            dl1[h].attach (c.carve ((size_t) len), len);
            len = lenFor (msOfDattorro (plateTank[h][2]) * ms, sr);
            apFix[h].line.attach (c.carve ((size_t) len), len);
            len = lenFor (msOfDattorro (plateTank[h][3]) * ms, sr);
            dl2[h].attach (c.carve ((size_t) len), len);
        }
    }

    void PlateNet::configure (Arena& a, double sr)
    {
        fs = sr;
        layout (a, sr);
        bound.prepare (fs / kControlBlock);
        designToneCorrection (toneL, decayShapes[algoPlate], fs);
        designToneCorrection (toneR, decayShapes[algoPlate], fs);
    }

    double PlateNet::halfLengthSamples (int h) const noexcept
    {
        return endv[h][ap1] + endv[h][d1] + endv[h][ap2] + endv[h][d2];
    }

    void PlateNet::computeTargets (const BlockParams& bp, bool snap) noexcept
    {
        const double scale = design::scaleFor (algoPlate, bp.sizePct);
        const double sp = fs * 0.001 * scale;
        const double controlRate = fs / kControlBlock;
        const double exc = excursionSamples (plateModMs, bp.modDepth, bp.modRateHz, fs);
        const double G = wanderG (bp.modRateHz, controlRate), norm = Wander::normFor (G);
        const auto& shape = decayShapes[algoPlate];
        const double gLo = filt::integratorGain (shape.loHz, fs);
        const double gHi = filt::integratorGain (hiHzFor (shape, bp.dark, bp.ruin), fs);
        const double hiRatio = hiRatioFor (shape, bp.dark, bp.ruin);
        const auto ziRef = filt::zInv (design::refHz, fs);
        const double k = (double) kControlBlock;

        for (int h = 0; h < 2; ++h)
        {
            if (! snap)
                phase[h] += bp.modRateHz * (h == 0 ? 1.0 : 1.13) * k / fs;
            phase[h] -= std::floor (phase[h]);
            const double w = wander[h].next (G, norm);
            double off = exc * ((1.0 - 0.3 * bp.ruin) * std::sin (twoPi * (phase[h] + 0.25 * h)) + (0.25 + 0.75 * bp.ruin) * std::clamp (w, -2.0, 2.0));
            off = std::clamp (off, -1.6 * exc, 1.6 * exc);
            std::array<double, numElems> e {};
            for (int el = 0; el < numElems; ++el)
                e[(size_t) el] = msOfDattorro (plateTank[h][el]) * sp;
            const double L = e[ap1] + e[d1] + e[ap2] + e[d2];
            apModLoss[h] = lossFor (e[ap1], bp.lambda, fs);
            apFixLoss[h] = lossFor (e[ap2], bp.lambda, fs);
            e[ap1] = std::max (2.0, e[ap1] + off);

            const double g0 = gainEnd[h];
            designAbsorption (absorb[h], e[d1] + e[d2], L, fs, bp.lambda, shape.loRatio, hiRatio, gLo, gHi, ziRef);
            absorb[h].rampShelves (kControlBlock, snap);
            if (h == 0) loopSamples = L;
            const double g1 = absorb[h].gain;
            for (int el = 0; el < numElems; ++el)
            {
                const size_t u = (size_t) el;
                if (snap) { cur[h][u] = e[u]; stp[h][u] = 0.0; }
                else { cur[h][u] = endv[h][u]; stp[h][u] = (e[u] - endv[h][u]) / k; }
                endv[h][u] = e[u];
            }
            if (snap) gainStep[h] = 0.0;
            else { absorb[h].gain = g0; gainStep[h] = (g1 - g0) / k; }
            gainEnd[h] = g1;
            if (bp.loopCoefficients != nullptr)
                loopF[h].copyCoefficients (*bp.loopCoefficients);
        }

        for (int s = 0; s < 4; ++s)
            inDiffLoss[(size_t) s] = lossFor (inDiffLen[(size_t) s], bp.lambda, fs);
        const double difT = diffusionWithRuin (bp);
        if (snap)
        {
            scaleCur = scale; scaleStep = 0.0;
            dif = difT; difStep = 0.0;
            loopW = bp.loopW; loopWStep = 0.0;
            sat = bp.loopSat; satStep = 0.0;
        }
        else
        {
            scaleStep = (scale - scaleCur) / k;
            difStep = (difT - dif) / k;
            loopWStep = (bp.loopW - loopW) / k;
            satStep = (bp.loopSat - sat) / k;
        }
        spm = fs * 0.001;
        const bool active = loopW > 0.0 || bp.loopW > 0.0;
        if (active && ! loopActive)
            for (auto& f : loopF) f.reset();
        loopActive = active;
    }

    void PlateNet::reset (const BlockParams& bp) noexcept
    {
        for (auto& a : inDiff) a.reset();
        for (int h = 0; h < 2; ++h)
        {
            apMod[h].reset(); apFix[h].reset(); rd1[h].reset(); rd2[h].reset();
            absorb[h].reset(); loopF[h].reset();
            phase[h] = 0.0;
            wander[h].reset ((std::uint64_t) 0xB1A7E + (std::uint64_t) 104729 * (std::uint64_t) h);
        }
        toneL.reset(); toneR.reset();
        bound.reset();
        loopActive = true;
        computeTargets (bp, true);
    }

    void PlateNet::beginBlock (const BlockParams& bp) noexcept
    {
        bound.block (kControlBlock, bp.hold, loopSamples / fs);
        computeTargets (bp, false);
    }

    void PlateNet::process (double xL, double xR, NetOut& o) noexcept
    {
        dif += difStep;
        loopW += loopWStep;
        sat += satStep;
        scaleCur += scaleStep;

        // mono input (Dattorro) through four input diffusers; the plate's onset
        // (early output) is the diffused input after stages 3 and 4
        double u = 0.5 * (xL + xR);
        double early[2] {};
        for (int s = 0; s < 4; ++s)
        {
            u = inDiff[(size_t) s].processInt (u, inDiffLen[(size_t) s], plateInDiffG[s] * dif, inDiffLoss[(size_t) s]);
            if (s >= 2) early[s - 2] = u;
        }
        o.eL = plateEarly * early[0];
        o.eR = plateEarly * early[1];

        // tank: the end of each half feeds the start of the other (figure eight)
        for (int h = 0; h < 2; ++h)
            for (int el = 0; el < numElems; ++el)
                cur[h][(size_t) el] += stp[h][(size_t) el];
        const double endL = rd2[0].read (dl2[0], cur[0][d2]);
        const double endR = rd2[1].read (dl2[1], cur[1][d2]);
        const double gl = bound.next();
        for (int h = 0; h < 2; ++h)
        {
            const double in = u + (h == 0 ? endR : endL);
            const double a = apMod[h].process (in, cur[h][ap1], -plateDecayDiff1 * dif, apModLoss[h]);
            double t = rd1[h].read (dl1[h], cur[h][d1]);
            dl1[h].write (a);
            absorb[h].gain += gainStep[h];
            t = absorb[h].process (t);
            if (loopActive)
                t += loopW * (loopF[h].process (t) - t);
            if (sat > 0.0)
                t += sat * (softSat (t, design::satLevel) - t);
            const double b = apFix[h].process (gl * t, cur[h][ap2], plateDecayDiff2 * dif, apFixLoss[h]);
            dl2[h].write (loopGuard (b));
        }

        // output taps (Dattorro Table 2), scaled with the tank
        const double tapSpm = spm * scaleCur * 1000.0 / dattorroRate;
        auto tapRead = [&] (const PlateTap& t)
        {
            const double pos = std::max (2.0, t.samples * tapSpm);
            const DelayLine& l = t.elem == 1 ? dl1[t.half] : (t.elem == 2 ? apFix[t.half].line : dl2[t.half]);
            return t.sign * l.readCubic (pos);
        };
        double yL = 0.0, yR = 0.0;
        for (const auto& t : plateTapsL) yL += tapRead (t);
        for (const auto& t : plateTapsR) yR += tapRead (t);
        o.lL = plateLate * toneL.process (yL);
        o.lR = plateLate * toneR.process (yR);
        bound.acc += 0.5 * (o.lL * o.lL + o.lR * o.lR);
    }

    // ================================================================ ChamberNet
    template <class Carver>
    void ChamberNet::layout (Carver& c, double sr)
    {
        const double ms = design::maxScale (algoChamber);
        const double exc = 1.7 * chamberModMs * sr * 0.001;
        for (int h = 0; h < 2; ++h)
        {
            auto carve = [&] (DelayLine& l, int el, double extra)
            {
                const int len = lenFor (chamberMs[h][el] * ms, sr, extra);
                l.attach (c.carve ((size_t) len), len);
            };
            carve (na[h].outer.line, aO, 0.0);
            carve (na[h].inner[0].line, aI, 0.0);
            na[h].innerCount = 1;
            carve (tl[h][0], t1, 0.0);
            carve (nb[h].outer.line, bO, 0.0);
            carve (nb[h].inner[0].line, bI1, 0.0);
            carve (nb[h].inner[1].line, bI2, 0.0);
            nb[h].innerCount = 2;
            carve (tl[h][1], t2, exc);
            carve (nc[h].outer.line, cO, 0.0);
            carve (nc[h].inner[0].line, cI, 0.0);
            nc[h].innerCount = 1;
            carve (tl[h][2], t3, 0.0);
        }
    }

    void ChamberNet::configure (Arena& a, double sr)
    {
        fs = sr;
        layout (a, sr);
        bound.prepare (fs / kControlBlock);
        designToneCorrection (toneL, decayShapes[algoChamber], fs);
        designToneCorrection (toneR, decayShapes[algoChamber], fs);
    }

    double ChamberNet::loopLengthSamples (int h) const noexcept
    {
        double L = 1.0; // the loop end is fed back one sample later
        for (int el = 0; el < numElems; ++el)
            L += endv[h][(size_t) el];
        return L;
    }

    void ChamberNet::computeTargets (const BlockParams& bp, bool snap) noexcept
    {
        const double sp = fs * 0.001 * design::scaleFor (algoChamber, bp.sizePct);
        const double controlRate = fs / kControlBlock;
        const double exc = excursionSamples (chamberModMs, bp.modDepth, bp.modRateHz, fs);
        const double G = wanderG (bp.modRateHz, controlRate), norm = Wander::normFor (G);
        const auto& shape = decayShapes[algoChamber];
        const double gLo = filt::integratorGain (shape.loHz, fs);
        const double gHi = filt::integratorGain (hiHzFor (shape, bp.dark, bp.ruin), fs);
        const double hiRatio = hiRatioFor (shape, bp.dark, bp.ruin);
        const auto ziRef = filt::zInv (design::refHz, fs);
        const double k = (double) kControlBlock;

        for (int h = 0; h < 2; ++h)
        {
            if (! snap)
                phase[h] += bp.modRateHz * (h == 0 ? 0.91 : 1.09) * k / fs;
            phase[h] -= std::floor (phase[h]);
            const double w = wander[h].next (G, norm);
            double off = exc * ((1.0 - 0.3 * bp.ruin) * std::sin (twoPi * (phase[h] + 0.5 * h)) + (0.25 + 0.75 * bp.ruin) * std::clamp (w, -2.0, 2.0));
            off = std::clamp (off, -1.6 * exc, 1.6 * exc);
            std::array<double, numElems> e {};
            double L = 1.0;
            for (int el = 0; el < numElems; ++el)
            {
                e[(size_t) el] = std::max (2.0, chamberMs[h][el] * sp);
                L += e[(size_t) el];
            }
            for (int el : { (int) aO, (int) aI, (int) bO, (int) bI1, (int) bI2, (int) cO, (int) cI })
                loss[h][(size_t) el] = lossFor (e[(size_t) el], bp.lambda, fs);
            const double Lmid = e[t1] + e[t2] + e[t3] + 1.0;
            e[t2] = std::max (2.0, e[t2] + off);

            const double g0 = gainEnd[h];
            designAbsorption (absorb[h], Lmid, L, fs, bp.lambda, shape.loRatio, hiRatio, gLo, gHi, ziRef);
            absorb[h].rampShelves (kControlBlock, snap);
            if (h == 0) loopSamples = L;
            const double g1 = absorb[h].gain;
            for (int el = 0; el < numElems; ++el)
            {
                const size_t u = (size_t) el;
                if (snap) { cur[h][u] = e[u]; stp[h][u] = 0.0; }
                else { cur[h][u] = endv[h][u]; stp[h][u] = (e[u] - endv[h][u]) / k; }
                endv[h][u] = e[u];
            }
            if (snap) gainStep[h] = 0.0;
            else { absorb[h].gain = g0; gainStep[h] = (g1 - g0) / k; }
            gainEnd[h] = g1;
            if (bp.loopCoefficients != nullptr)
                loopF[h].copyCoefficients (*bp.loopCoefficients);
        }

        const double difT = diffusionWithRuin (bp);
        if (snap)
        {
            dif = difT; difStep = 0.0;
            loopW = bp.loopW; loopWStep = 0.0;
            sat = bp.loopSat; satStep = 0.0;
        }
        else
        {
            difStep = (difT - dif) / k;
            loopWStep = (bp.loopW - loopW) / k;
            satStep = (bp.loopSat - sat) / k;
        }
        const bool active = loopW > 0.0 || bp.loopW > 0.0;
        if (active && ! loopActive)
            for (auto& f : loopF) f.reset();
        loopActive = active;
    }

    void ChamberNet::reset (const BlockParams& bp) noexcept
    {
        for (int h = 0; h < 2; ++h)
        {
            na[h].reset(); nb[h].reset(); nc[h].reset();
            for (auto& r : trd[h]) r.reset();
            absorb[h].reset(); loopF[h].reset();
            loopEnd[h] = 0.0;
            phase[h] = 0.0;
            wander[h].reset ((std::uint64_t) 0xC4A3B + (std::uint64_t) 15485863 * (std::uint64_t) h);
        }
        toneL.reset(); toneR.reset();
        bound.reset();
        loopActive = true;
        computeTargets (bp, true);
    }

    void ChamberNet::beginBlock (const BlockParams& bp) noexcept
    {
        bound.block (kControlBlock, bp.hold, loopSamples / fs);
        computeTargets (bp, false);
    }

    void ChamberNet::process (double xL, double xR, NetOut& o) noexcept
    {
        dif += difStep;
        loopW += loopWStep;
        sat += satStep;
        double g[numElems];
        for (int el = 0; el < numElems; ++el)
            g[el] = chamberG[el] * dif;

        const double gl = bound.next();
        double taps[2][3] {}, ends[2] {};
        for (int h = 0; h < 2; ++h)
        {
            auto& c = cur[h];
            for (int el = 0; el < numElems; ++el)
                c[(size_t) el] += stp[h][(size_t) el];
            const double x = (h == 0 ? xL : xR) + loopEnd[h];
            const auto& ls = loss[h];
            const double ya = na[h].process (x, c[aO], &c[aI], g[aO], &g[aI], ls[aO], &ls[aI]);
            const double v1 = trd[h][0].read (tl[h][0], c[t1]);
            tl[h][0].write (ya);
            const double yb = nb[h].process (v1, c[bO], &c[bI1], g[bO], &g[bI1], ls[bO], &ls[bI1]);
            const double v2 = trd[h][1].read (tl[h][1], c[t2]);
            tl[h][1].write (yb);
            const double yc = nc[h].process (v2, c[cO], &c[cI], g[cO], &g[cI], ls[cO], &ls[cI]);
            double v3 = trd[h][2].read (tl[h][2], c[t3]);
            tl[h][2].write (yc);
            taps[h][0] = v1; taps[h][1] = v2; taps[h][2] = v3;

            absorb[h].gain += gainStep[h];
            double e = absorb[h].process (v3);
            if (loopActive)
                e += loopW * (loopF[h].process (e) - e);
            if (sat > 0.0)
                e += sat * (softSat (e, design::satLevel) - e);
            ends[h] = gl * e;
        }
        // cross-coupling of the two loops: a rotation (orthogonal, lossless)
        const double cs = std::cos (chamberRotation), sn = std::sin (chamberRotation);
        loopEnd[0] = loopGuard (cs * ends[0] - sn * ends[1]);
        loopEnd[1] = loopGuard (sn * ends[0] + cs * ends[1]);

        const double yL = 0.5 * taps[0][0] + 0.6 * taps[0][1] - 0.45 * taps[1][2];
        const double yR = 0.5 * taps[1][0] + 0.6 * taps[1][1] - 0.45 * taps[0][2];
        o.eL = o.eR = 0.0;
        o.lL = chamberLate * toneL.process (yL);
        o.lR = chamberLate * toneR.process (yR);
        bound.acc += 0.5 * (o.lL * o.lL + o.lR * o.lR);
    }

    // ================================================================ ReverbSlot
    size_t ReverbSlot::memoryFloats (double sr)
    {
        size_t best = 0;
        for (int a = 0; a < numAlgorithms; ++a)
        {
            struct Counter
            {
                size_t total = 0;
                float* carve (size_t n) noexcept { total += n; return nullptr; }
            } c;
            EarlyBank e;
            FdnNet f;
            PlateNet p;
            ChamberNet ch;
            if (hasEarlyBank (a))
                e.layout (c, a, sr);
            if (a == algoPlate)
                p.layout (c, sr);
            else if (a == algoChamber)
                ch.layout (c, sr);
            else
                f.layout (c, fdnDesign (a), a, sr);
            best = std::max (best, c.total);
        }
        return best + 64;
    }

    void ReverbSlot::prepare (double sr)
    {
        fs = sr;
        arena.allocate (memoryFloats (sr));
        clearPos = 0;
    }

    void ReverbSlot::configure (int a, const BlockParams& bp) noexcept
    {
        algo = std::clamp (a, 0, numAlgorithms - 1);
        arena.reset();
        if (hasEarlyBank (algo))
        {
            early.configure (arena, algo, fs);
            early.reset (bp);
        }
        if (algo == algoPlate)
        {
            plateNet.configure (arena, fs);
            plateNet.reset (bp);
        }
        else if (algo == algoChamber)
        {
            chamberNet.configure (arena, fs);
            chamberNet.reset (bp);
        }
        else
        {
            fdnNet.configure (arena, fdnDesign (algo), algo, fs);
            fdnNet.reset (bp);
        }
        clearPos = 0;
    }

    void ReverbSlot::beginBlock (const BlockParams& bp) noexcept
    {
        if (hasEarlyBank (algo))
            early.beginBlock (bp);
        if (algo == algoPlate) plateNet.beginBlock (bp);
        else if (algo == algoChamber) chamberNet.beginBlock (bp);
        else fdnNet.beginBlock (bp);
    }

    void ReverbSlot::process (double xL, double xR, NetOut& o) noexcept
    {
        if (hasEarlyBank (algo))
        {
            double eL, eR, lateL, lateR;
            early.process (xL, xR, eL, eR, lateL, lateR);
            if (algo == algoChamber) chamberNet.process (lateL, lateR, o);
            else fdnNet.process (lateL, lateR, o);
            o.eL += eL;
            o.eR += eR;
            return;
        }
        if (algo == algoPlate) plateNet.process (xL, xR, o);
        else fdnNet.process (xL, xR, o);
    }

    bool ReverbSlot::clearStep (size_t maxFloats) noexcept
    {
        const size_t used = arena.used();
        if (clearPos < used)
        {
            const size_t n = std::min (maxFloats, used - clearPos);
            std::memset (arena.data() + clearPos, 0, n * sizeof (float));
            clearPos += n;
        }
        return clearPos >= used;
    }

    void ReverbSlot::clearAll() noexcept
    {
        std::memset (arena.data(), 0, arena.capacity() * sizeof (float));
        clearPos = arena.used();
    }

    double ReverbSlot::getBoundGain() const noexcept
    {
        if (algo == algoPlate) return plateNet.getBoundGain();
        if (algo == algoChamber) return chamberNet.getBoundGain();
        return fdnNet.getBoundGain();
    }
}
