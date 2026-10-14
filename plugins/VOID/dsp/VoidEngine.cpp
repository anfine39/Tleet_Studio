#include "VoidEngine.h"

namespace tleet::voidrev
{
    namespace
    {
        // Exponential glide in the log domain, snapped when within 1e-9.
        void glideLog (double& v, double t, double k) noexcept
        {
            const double r = std::log (t / v);
            v = std::abs (r) < 1.0e-9 ? t : v * std::exp (k * r);
        }

        double dbToLin (double db) noexcept { return std::pow (10.0, db / 20.0); }
    }

    EffectiveSettings applyDrown (const Settings& s) noexcept
    {
        const double c = drown::curve (s.drownPct);
        EffectiveSettings e;
        const double size = std::clamp (s.sizePct, 0.0, 100.0);
        const double dif = std::clamp (s.diffusionPct, 0.0, 100.0);
        const double depth = std::clamp (s.modDepthPct, 0.0, 100.0);
        const double width = std::clamp (s.widthPct, 0.0, 200.0);
        e.sizePct = size + (100.0 - size) * drown::sizeShare * c;
        e.diffusionPct = dif + (100.0 - dif) * drown::diffusionShare * c;
        e.modDepthPct = depth + (100.0 - depth) * drown::depthShare * c;
        e.widthPct = width + (200.0 - width) * drown::widthShare * c;
        e.lpfHz = std::max (1000.0, std::clamp (s.lpfHz, 1000.0, 20000.0) * std::exp2 (-drown::lpfOctaves * c));
        e.decayInfinite = s.decayInfinite;
        e.decaySeconds = s.decayInfinite ? std::numeric_limits<double>::infinity()
                                         : std::min (drown::maxFiniteDecayS, std::max (0.1, s.decayS) * std::exp2 (drown::decayOctaves * c));
        return e;
    }

    void VoidEngine::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        controlRate = fs / K;
        kSize = blockCoeff (engine::sizeGlideS, controlRate);
        preFadeStep = 1.0 / (engine::preDelayFadeS * fs);
        kLambda = blockCoeff (engine::decayGlideS, controlRate);
        kFast = blockCoeff (engine::controlGlideS, controlRate);
        kGate = blockCoeff (engine::gateGlideS, controlRate);
        kLog = blockCoeff (engine::logGlideS, controlRate);
        kDuckAtt = blockCoeff (engine::duckAttackS, controlRate);
        kDuckRel = blockCoeff (engine::duckReleaseS, controlRate);
        duckDetRel = std::exp (-1.0 / (engine::duckDetectorReleaseS * fs));
        dcR = std::exp (-2.0 * filt::pi * engine::dcBlockHz / fs);

        const int preLen = (int) std::ceil (engine::maxPreDelayMs * 0.001 * fs) + 16;
        for (int ch = 0; ch < 2; ++ch)
        {
            preMem[ch].assign ((size_t) preLen, 0.0f);
            preLine[ch].attach (preMem[ch].data(), preLen);
            darkIn[ch].set (engine::darkInHz, fs);
            darkOut[ch].set (filt::integratorGain (engine::darkOutHz, fs), filt::butterworthK);
        }
        for (auto& s : slots)
            s.slot.prepare (fs);
        reset();
    }

    BlockParams VoidEngine::blockParams() const noexcept
    {
        BlockParams bp;
        bp.fs = fs;
        bp.sizePct = gSize.end;
        bp.lambda = gLambda.end;
        bp.diffusion = gDiff.end;
        bp.modDepth = gDepth.end;
        bp.modRateHz = rateCur;
        bp.ruin = gRuin.end;
        bp.dark = gDark.end;
        bp.loopSat = gRuin.end * (1.0 - gFreezeW.end);
        bp.loopW = gWLoop.end;
        bp.loopCoefficients = &loopCoeffs;
        bp.hold = gFreezeW.end > 0.999;
        return bp;
    }

    void VoidEngine::snapToTargets() noexcept
    {
        duckAttDb = 0.0;
        const auto e = applyDrown (target);
        rateCur = std::clamp (target.modRateHz, 0.01, 5.0);
        hpfCur = std::clamp (target.hpfHz, filt::hpfOpenHz, 1000.0);
        lpfCur = std::clamp (e.lpfHz, 10.0, filt::lpfOpenHz);
        blockPos = 0;
        controlBlock (false); // sets every target; the snaps below land the glides on them
        preA = preB = preTarget;
        preFade = 0.0;
        preFading = false;
        for (auto* g : { &gSize, &gLambda, &gDiff, &gDepth, &gWidth, &gRuin, &gDark, &gWPre, &gWLoop, &gWPost, &gFreezeGate,
                         &gFreezeW, &gDuck })
            g->snap (g->target);
        for (int ch = 0; ch < 2; ++ch)
        {
            pre[ch].set (hpfCur, lpfCur, fs);
            post[ch].set (hpfCur, lpfCur, fs);
        }
        loopCoeffs.set (hpfCur, lpfCur, fs);
        preOn = gWPre.end > 0.0;
        postOn = gWPost.end > 0.0;
        darkOn = gDark.end > 0.0;

        // the active slot runs the selected algorithm from silence, the other is idle
        const auto bp = blockParams();
        for (auto& s : slots)
        {
            s.slot.clearAll();
            s.state = idle;
            s.fade = 1.0;
            s.fadeStep = 0.0;
            s.fast = false;
            s.gate.snap (0.0);
        }
        act = 0;
        pending = false;
        slots[0].slot.configure (target.algorithm, bp);
        slots[0].state = active;
        slots[0].gate.snap (1.0);
        frozen.store (target.freeze, std::memory_order_relaxed);
        hpfNow.store (hpfCur, std::memory_order_relaxed);
        lpfNow.store (lpfCur, std::memory_order_relaxed);
        blockPos = 0;
    }

    void VoidEngine::reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (preMem[ch].begin(), preMem[ch].end(), 0.0f);
            preLine[ch].w = 0;
            pre[ch].reset();
            post[ch].reset();
            darkIn[ch].reset();
            darkOut[ch].reset();
        }
        duckEnv = duckBlockPeak = 0.0;
        dcX1[0] = dcX1[1] = dcY1[0] = dcY1[1] = 0.0;
        ruinX1[0] = ruinX1[1] = 0.0;
        snapToTargets();
        earlyMeter.takeRms();
        lateMeter.takeRms();
        lastEarly.store (0.0f);
        lastLate.store (0.0f);
    }

    void VoidEngine::runSlotMachine (const BlockParams& bp) noexcept
    {
        const int desired = std::clamp (target.algorithm, 0, numAlgorithms - 1);
        auto& a = slots[(size_t) act];
        auto& o = slots[(size_t) (1 - act)];
        pending = desired != a.slot.getAlgorithm();

        // ring-out / clearing of the inactive slot
        if (o.state == ringOut && o.fade <= 0.0 && same (o.fadeStep, 0.0))
            o.state = clearing;
        if (o.state == clearing && o.slot.clearStep (engine::clearFloatsPerBlock))
            o.state = idle;

        if (pending)
        {
            if (o.state == idle)
            {
                // ALGORITHM change: the new network starts silent and its input
                // fades in; the old one stops receiving input and rings out
                // (MODEL_SPEC 7.1). No discontinuity anywhere.
                o.slot.configure (desired, bp);
                o.state = active;
                o.gate.snap (0.0);
                o.gate.target = 1.0;
                o.fade = 1.0;
                o.fadeStep = 0.0;
                o.fast = false;
                a.state = ringOut;
                a.gate.target = 0.0;
                a.fadePhase = 0.0;
                a.fast = false;
                act = 1 - act;
                pending = false;
            }
            else if (o.state == ringOut)
            {
                o.fast = true; // its tail fades in fastRingOutS, then the slot is cleared and reused
            }
        }

        for (auto& s : slots)
        {
            if (s.state == ringOut)
            {
                double next;
                if (s.fast)
                    next = std::max (0.0, s.fade - (double) K / (engine::fastRingOutS * fs));
                else
                {
                    s.fadePhase = std::min (1.0, s.fadePhase + (double) K / (engine::ringOutS * fs));
                    next = 0.5 * (1.0 + std::cos (filt::pi * s.fadePhase));
                }
                if (next < 1.0e-6) next = 0.0;
                s.fadeStep = (next - s.fade) / K;
                if (same (next, 0.0) && same (s.fade, 0.0))
                    s.fadeStep = 0.0;
            }
            if (s.state == active || s.state == ringOut)
            {
                s.gate.block (kGate, K);
                s.slot.beginBlock (bp);
            }
        }
    }

    void VoidEngine::controlBlock (bool runSlots) noexcept
    {
        const auto e = applyDrown (target);
        const bool freeze = target.freeze;
        const int mode = std::clamp (target.filterMode, 0, 2);
        const int color = std::clamp (target.color, 0, 2);

        gSize.target = e.sizePct;
        preTarget = std::clamp (target.preDelayMs, 0.0, engine::maxPreDelayMs) * fs / 1000.0; // exact for whole ms at whole rates
        // PRE-DELAY changes crossfade between two read taps (no pitch sweep, no
        // click); a change during a fade starts the next fade when it ends
        if (! preFading && ! same (preTarget, preA))
        {
            preB = preTarget;
            preFade = 0.0;
            preFading = true;
        }
        gLambda.target = (freeze || e.decayInfinite) ? 0.0 : 1.0 / e.decaySeconds;
        gDiff.target = e.diffusionPct * 0.01;
        gDepth.target = e.modDepthPct * 0.01;
        gWidth.target = e.widthPct * 0.01;
        gRuin.target = color == 2 ? 1.0 : 0.0;
        gDark.target = color == 1 ? 1.0 : 0.0;
        gWPre.target = mode == 0 ? 1.0 : 0.0;
        gWLoop.target = (mode == 1 && ! freeze) ? 1.0 : 0.0;
        // FREEZE: the LOOP filters leave the loop and shape the frozen output instead (MODEL_SPEC 6.5)
        gWPost.target = (mode == 2 || (mode == 1 && freeze)) ? 1.0 : 0.0;
        gFreezeGate.target = freeze ? 0.0 : 1.0;
        gFreezeW.target = freeze ? 1.0 : 0.0;
        glideLog (rateCur, std::clamp (target.modRateHz, 0.01, 5.0), kLog);
        glideLog (hpfCur, std::clamp (target.hpfHz, filt::hpfOpenHz, 1000.0), kLog);
        glideLog (lpfCur, std::clamp (e.lpfHz, 10.0, filt::lpfOpenHz), kLog);

        // DUCK: detector peak of this block -> attenuation (dB), attack/release
        {
            const double envDb = 20.0 * std::log10 (std::max (duckBlockPeak, 1.0e-9));
            const double k = std::clamp ((envDb - engine::duckLowDb) / engine::duckSpanDb, 0.0, 1.0);
            const double att = std::clamp (target.duckPct, 0.0, 100.0) * 0.01 * engine::duckMaxDb * k;
            duckAttDb += (att > duckAttDb ? kDuckAtt : kDuckRel) * (att - duckAttDb);
            if (duckAttDb < 1.0e-6) duckAttDb = 0.0;
            gDuck.target = dbToLin (-duckAttDb);
            duckBlockPeak = 0.0;
        }

        gSize.block (kSize, K);
        gLambda.block (kLambda, K);
        for (auto* g : { &gDiff, &gDepth, &gWidth, &gRuin, &gDark, &gWPre, &gWLoop, &gWPost, &gFreezeW })
            g->block (kFast, K);
        gFreezeGate.block (kGate, K);
        gDuck.block (1.0, K);

        for (int ch = 0; ch < 2; ++ch)
        {
            pre[ch].glideTo (hpfCur, lpfCur, fs, K);
            post[ch].glideTo (hpfCur, lpfCur, fs, K);
        }
        loopCoeffs.glideTo (hpfCur, lpfCur, fs, K);

        // weighted stages run only while they can be heard; they restart from zero
        // state with their weight rising from 0 (no click)
        auto gate = [] (bool& on, const Glide& g, auto&& resetFn)
        {
            const bool now = g.end > 0.0 || g.cur > 0.0;
            if (now && ! on) resetFn();
            on = now;
        };
        gate (preOn, gWPre, [this] { pre[0].reset(); pre[1].reset(); });
        gate (postOn, gWPost, [this] { post[0].reset(); post[1].reset(); });
        gate (darkOn, gDark, [this] { darkIn[0].reset(); darkIn[1].reset(); darkOut[0].reset(); darkOut[1].reset(); });

        if (runSlots)
            runSlotMachine (blockParams());

        frozen.store (freeze, std::memory_order_relaxed);
        hpfNow.store (hpfCur, std::memory_order_relaxed);
        lpfNow.store (lpfCur, std::memory_order_relaxed);
    }

    double VoidEngine::readPreDelay (const DelayLine& l, double d) const noexcept
    {
        // the current sample was written first: read(1) = x[n]
        if (d < 1.0)
            return l.read (1) + d * (l.read (2) - l.read (1));
        return l.readCubic (d + 1.0);
    }

    void VoidEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
    {
        double eSq = 0.0, lSq = 0.0;
        const double soloE = solo == 2 ? 0.0 : 1.0, soloL = solo == 1 ? 0.0 : 1.0;
        for (int i = 0; i < numSamples; ++i)
        {
            if (blockPos == 0)
                controlBlock();
            blockPos = blockPos + 1 == K ? 0 : blockPos + 1;

            double xl = inL[i], xr = inR[i];

            // DUCK detector: peak of the dry input, instant attack, 50 ms release
            const double pk = std::max (std::abs (xl), std::abs (xr));
            duckEnv = pk > duckEnv ? pk : duckEnv * duckDetRel;
            duckBlockPeak = std::max (duckBlockPeak, duckEnv);

            // ---- PRE filter (crossfaded weight)
            const double wPre = gWPre.next();
            if (preOn)
            {
                xl += wPre * (pre[0].process (xl) - xl);
                xr += wPre * (pre[1].process (xr) - xr);
            }
            // ---- COLOR input stages: DARK band limit, RUIN drive
            const double dk = gDark.next();
            if (darkOn)
            {
                xl += dk * (darkIn[0].process (xl) - xl);
                xr += dk * (darkIn[1].process (xr) - xr);
            }
            const double rn = gRuin.next();
            {
                // the antialiased saturator runs always (its state stays current), mixed in by RUIN's weight
                const double dl = engine::ruinDrive * xl, dr = engine::ruinDrive * xr;
                const double sl = softSatAdaa (dl, ruinX1[0]) / engine::ruinDrive;
                const double sr = softSatAdaa (dr, ruinX1[1]) / engine::ruinDrive;
                ruinX1[0] = dl;
                ruinX1[1] = dr;
                if (rn > 0.0)
                {
                    xl += rn * (sl - xl);
                    xr += rn * (sr - xr);
                }
            }

            // ---- DC blocker on the network input (a reverb tank must not
            // integrate DC; inaudible: first order at 5 Hz)
            {
                const double yl0 = xl - dcX1[0] + dcR * dcY1[0];
                const double yr0 = xr - dcX1[1] + dcR * dcY1[1];
                dcX1[0] = xl; dcX1[1] = xr;
                dcY1[0] = yl0; dcY1[1] = yr0;
                xl = yl0; xr = yr0;
            }

            // ---- PRE-DELAY, FREEZE input gate
            preLine[0].write (xl);
            preLine[1].write (xr);
            const double fg = gFreezeGate.next();
            double pl = readPreDelay (preLine[0], preA), pr = readPreDelay (preLine[1], preA);
            if (preFading)
            {
                preFade = std::min (1.0, preFade + preFadeStep);
                const double w = 0.5 - 0.5 * std::cos (filt::pi * preFade);
                pl += w * (readPreDelay (preLine[0], preB) - pl);
                pr += w * (readPreDelay (preLine[1], preB) - pr);
                if (preFade >= 1.0)
                {
                    preA = preB;
                    preFading = false;
                }
            }
            pl *= fg;
            pr *= fg;

            // ---- algorithm slots
            NetOut sum;
            for (auto& s : slots)
            {
                if (s.state != active && s.state != ringOut)
                    continue;
                const double g = s.gate.next();
                s.fade += s.fadeStep;
                NetOut o;
                s.slot.process (pl * g, pr * g, o);
                sum.eL += s.fade * o.eL;
                sum.eR += s.fade * o.eR;
                sum.lL += s.fade * o.lL;
                sum.lR += s.fade * o.lR;
            }
            for (auto& s : slots)
                if (s.state == ringOut && s.fade <= 0.0)
                {
                    s.fade = 0.0;
                    s.fadeStep = 0.0;
                }

            double wl = soloE * sum.eL + soloL * sum.lL;
            double wr = soloE * sum.eR + soloL * sum.lR;

            // ---- POST filter, DARK output band limit
            const double wPost = gWPost.next();
            if (postOn)
            {
                wl += wPost * (post[0].process (wl) - wl);
                wr += wPost * (post[1].process (wr) - wr);
            }
            if (darkOn)
            {
                wl += dk * (darkOut[0].process<filt::Svf::lowpass> (wl) - wl);
                wr += dk * (darkOut[1].process<filt::Svf::lowpass> (wr) - wr);
            }

            // ---- WIDTH (side gain), DUCK
            const double w = gWidth.next();
            const double m = 0.5 * (wl + wr), sd = 0.5 * (wl - wr);
            const double dg = gDuck.next();
            const double yl = (m + w * sd) * dg, yr = (m - w * sd) * dg;

            const double d2 = 0.5 * dg * dg;
            eSq += (sum.eL * sum.eL + sum.eR * sum.eR) * d2;
            lSq += (sum.lL * sum.lL + sum.lR * sum.lR) * d2;
            outL[i] = std::isfinite (yl) ? (float) yl : 0.0f;
            outR[i] = std::isfinite (yr) ? (float) yr : 0.0f;
        }
        earlyMeter.pushLevel ((float) eSq, numSamples, 0.0f);
        lateMeter.pushLevel ((float) lSq, numSamples, 0.0f);
    }
}
