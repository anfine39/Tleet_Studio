#include "EchoEngine.h"

namespace tleet::echo
{
    namespace
    {
        double blockCoeff (double seconds, double rate) noexcept
        {
            return -std::expm1 (-(double) EchoEngine::K / std::max (1.0, seconds * rate));
        }

        int popcount3 (int mask) noexcept { return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1); }

        double dbToLin (double db) noexcept { return std::pow (10.0, db / 20.0); }

        // Exponential glide in the log domain, snapped when within 1e-9.
        void glideLog (double& v, double t, double k) noexcept
        {
            const double r = std::log (t / v);
            v = std::abs (r) < 1.0e-9 ? t : v * std::exp (k * r);
        }
    }

    double toneResponseDb (double hz, double bassDb, double trebleDb, double fs) noexcept
    {
        filt::Shelf b, t;
        b.set (engine::bassHz, fs, dbToLin (bassDb), false);
        t.set (engine::trebleHz, fs, dbToLin (trebleDb), true);
        const auto zi = filt::zInv (hz, fs);
        return filt::toDb (b.response (zi) * t.response (zi));
    }

    void EchoEngine::prepare (double sampleRate, int)
    {
        fs = sampleRate;
        // The record stage runs at 176.4 / 192 kHz at every host rate: 4x below
        // 50 kHz, 2x below 100 kHz, 1x above (MODEL_SPEC 5.4).
        os = fs < 50000.0 ? 4 : (fs < 100000.0 ? 2 : 1);
        recordRate = fs * os;
        // each 2x stage: 23 samples up + 23 down at its own rate = 23 samples at its input rate
        osLatency = os == 4 ? Downsampler2x::latency2x * 1.5 : (os == 2 ? (double) Downsampler2x::latency2x : 0.0);

        const double controlRate = fs / K;
        kGain = blockCoeff (engine::gainGlideS, fs);
        kCond = blockCoeff (engine::conditionGlideS, fs);
        kSpeed = blockCoeff (engine::speedGlideS, fs);
        kDuckAtt = blockCoeff (engine::duckAttackS, fs);
        kDuckRel = blockCoeff (engine::duckReleaseS, fs);
        duckDetRel = std::exp (-1.0 / (engine::duckDetectorReleaseS * fs));

        // Longest delay: head 3 at the longest head-1 time, at the slowest
        // speed the modulation allows, plus the chain compensation margin.
        const double maxDelay = timing::maxHead1Ms * 0.001 * fs * timing::headRatio[2] / (1.0 - engine::maxSpeedDeviation) + 4096.0;
        tape.prepare (maxDelay, 1.0 / (target.head1Ms * 0.001 * fs));

        wowFlutter.prepare (controlRate);
        dropouts.prepare (controlRate);
        hiss.prepare (fs);
        preamp.prepare (recordRate);
        springA.prepare (fs, spring::designA);
        springB.prepare (fs, spring::designB);
        reset();
    }

    void EchoEngine::snapToTargets() noexcept
    {
        cond = engine::condition (target.condition);
        voice = sat::voicing (target.character);
        ageCur = std::clamp (target.age, 0.0, 10.0);
        const double d1 = std::clamp (target.head1Ms, timing::minHead1Ms, timing::maxHead1Ms) * 0.001 * fs;
        lnD1 = lnD1Mid = std::log (d1);
        hpfCur = std::clamp (target.hpfHz, 10.0, 0.45 * fs);
        lpfCur = std::clamp (target.lpfHz, 10.0, 0.45 * fs);
        wowA = target.wowPct * 0.01 * mod::wowMax + cond.wow;
        flutterA = target.flutterPct * 0.01 * mod::flutterMax + cond.flutter;
        duckAttDb = 0.0;
        // the glides land exactly on the targets of this first control block
        controlBlock();
        for (auto* g : { &gEcho, &gRev, &gFb, &gLoopScale, &gWidth, &gNoise, &gDrop, &gWPre, &gWLoop, &gWPost, &gDrive, &gDuck, &gBass, &gTreble,
                         &gDriveOff, &gPreVc, &gPreEven, &gLt })
            g->snap (g->target);
        for (auto& g : gHead) g.snap (g.target);
        for (auto& g : gPan) g.snap (g.target);
        updateCoefficients (false);
        gComp.snap (comp);
    }

    void EchoEngine::reset()
    {
        chainM.reset(); chainS.reset();
        preFilter.reset(); loopFilter.reset(); postM.reset(); postS.reset();
        upIn.reset(); upFb.reset(); downTape.reset(); downPre.reset();
        upIn4.reset(); upFb4.reset(); downTape4.reset(); downPre4.reset();
        preamp.reset(); tapeSat.reset();
        springA.reset(); springB.reset();
        wowFlutter.reset(); dropouts.reset(); hiss.reset();
        duckEnv = duckBlockPeak = 0.0;
        tape.reset (1.0 / (std::clamp (target.head1Ms, timing::minHead1Ms, timing::maxHead1Ms) * 0.001 * fs));
        snapToTargets();
        springRunning = gRev.target > 0.0;
        for (auto& a : headPeak) a.store (0.0f);
    }

    double EchoEngine::chainGroupDelay() const noexcept
    {
        const auto& c = chainM;
        return filt::groupDelaySamples ([&c] (filt::cplx zi)
                                        {
                                            return c.lfHp.response (filt::Svf::highpass, zi) * c.headLp.response (filt::Svf::lowpass, zi)
                                                 * c.bass.response (zi) * c.treble.response (zi);
                                        },
                                        engine::timingRefHz, fs);
    }

    void EchoEngine::updateCoefficients (bool ramp) noexcept
    {
        const double d1Ms = std::exp (lnD1) * 1000.0 / fs;
        headLossHz = std::min (engine::headLossCorner (d1Ms, ageCur * engine::ageHfOct + cond.hfOct), 0.45 * fs);
        const double gLf = filt::integratorGain (engine::playbackLfHz, fs);
        const double gHl = filt::integratorGain (headLossHz, fs);
        const double bassLin = dbToLin (gBass.end), trebleLin = dbToLin (gTreble.end);
        const int steps = ramp ? K : 0;
        for (auto* c : { &chainM, &chainS })
        {
            c->lfHp.set (gLf, filt::butterworthK);
            c->headLp.set (gHl, filt::butterworthK);
            c->bass.setRamped (engine::bassHz, fs, bassLin, false, steps);
            c->treble.setRamped (engine::trebleHz, fs, trebleLin, true, steps);
        }
        for (auto* f : { &preFilter, &loopFilter, &postM, &postS })
            f->set (hpfCur, lpfCur, fs);
        tapeSat.set (voice.emphDb, recordRate);
        comp = osLatency + chainGroupDelay();
    }

    void EchoEngine::controlBlock() noexcept
    {
        const Settings& s = target;

        // ---- MODE: head gains (1/sqrt(N) each), pans (active heads spread evenly), loop scale
        const int mask = s.headMask & 7;
        const int n = popcount3 (mask);
        const double norm = n > 0 ? 1.0 / std::sqrt ((double) n) : 0.0;
        int order = 0;
        for (int h = 0; h < 3; ++h)
        {
            const bool on = (mask >> h) & 1;
            gHead[(size_t) h].target = on ? norm : 0.0;
            if (on)
            {
                gPan[(size_t) h].target = n == 1 ? 0.0 : -1.0 + 2.0 * order / (n - 1);
                ++order;
            }
        }
        gLoopScale.target = norm; // loop sum = (1/N) sum of heads

        // ---- TAPE CONDITION macro (slow morph) and CHARACTER voicing
        {
            const auto& c = engine::condition (s.condition);
            auto g = [this] (double& v, double t) { v += kCond * (t - v); };
            g (cond.wow, c.wow); g (cond.flutter, c.flutter); g (cond.irregular, c.irregular);
            g (cond.noiseLin, c.noiseLin); g (cond.hfOct, c.hfOct); g (cond.fbLossDb, c.fbLossDb);
            g (cond.dropoutDb, c.dropoutDb); g (cond.ltScale, c.ltScale);
            const auto& tv = sat::voicing (s.character);
            voice.emphDb += kGain * (tv.emphDb - voice.emphDb);
            if (std::abs (voice.emphDb - tv.emphDb) < 1.0e-9) voice.emphDb = tv.emphDb;
            ageCur += kGain * (std::clamp (s.age, 0.0, 10.0) - ageCur);
            if (std::abs (ageCur - s.age) < 1.0e-9) ageCur = std::clamp (s.age, 0.0, 10.0);
            gDriveOff.target = tv.driveOffsetDb;
            gPreVc.target = tv.preVc;
            gPreEven.target = tv.preEven;
            gLt.target = tv.tapeLt * cond.ltScale * dbToLin (-ageCur * engine::ageLtDb);
        }

        // ---- levels
        gEcho.target = engine::echoGain (s.echoVolume);
        gRev.target = engine::echoGain (s.reverbVolume);
        gWidth.target = std::clamp (s.stereoPct, 0.0, 100.0) * 0.01;
        gFb.target = std::clamp (s.intensity, 0.0, 10.0) * 0.1 * engine::fbMax
                   * dbToLin (-(s.age * engine::ageFbLossDb + cond.fbLossDb));
        const double userNoise = s.noisePct > 0.0 ? dbToLin (engine::noiseFloorDb + engine::noiseSpanDb * std::min (100.0, s.noisePct) * 0.01) : 0.0;
        gNoise.target = userNoise + cond.noiseLin;
        gWPre.target = s.filterMode == 0 ? 1.0 : 0.0;
        gWLoop.target = s.filterMode == 1 ? 1.0 : 0.0;
        gWPost.target = s.filterMode == 2 ? 1.0 : 0.0;
        gDrive.target = std::clamp (s.drive, 0.0, 10.0);
        gBass.target = std::clamp (s.bassDb, -10.0, 10.0);
        gTreble.target = std::clamp (s.trebleDb, -10.0, 10.0);
        wowA += kGain * (s.wowPct * 0.01 * mod::wowMax + cond.wow - wowA);
        flutterA += kGain * (s.flutterPct * 0.01 * mod::flutterMax + cond.flutter - flutterA);

        // ---- DUCK: detector peak of this block -> attenuation (dB), attack/release
        {
            const double envDb = 20.0 * std::log10 (std::max (duckBlockPeak, 1.0e-9));
            const double k = std::clamp ((envDb - engine::duckLowDb) / engine::duckSpanDb, 0.0, 1.0);
            const double att = std::clamp (s.duckPct, 0.0, 100.0) * 0.01 * engine::duckMaxDb * k;
            duckAttDb += (att > duckAttDb ? kDuckAtt : kDuckRel) * (att - duckAttDb);
            if (duckAttDb < 1.0e-6) duckAttDb = 0.0;
            gDuck.target = dbToLin (-duckAttDb);
            duckBlockPeak = 0.0;
        }

        // ---- tape speed: the motor glides ln(head-1 delay); wow/flutter modulate the speed
        {
            const double t = std::log (std::clamp (s.head1Ms, timing::minHead1Ms, timing::maxHead1Ms) * 0.001 * fs);
            lnD1Mid += kSpeed * (t - lnD1Mid);
            lnD1 += kSpeed * (lnD1Mid - lnD1);
            if (std::abs (t - lnD1Mid) < 1.0e-10 && std::abs (t - lnD1) < 1.0e-10)
                lnD1 = lnD1Mid = t;
            lastM = std::clamp (wowFlutter.next (wowA, flutterA, cond.irregular), -engine::maxSpeedDeviation, engine::maxSpeedDeviation);
            tape.beginBlock ((1.0 + lastM) / std::exp (lnD1));
        }
        gDrop.target = dropouts.next (cond.dropoutDb);

        // ---- filters (log glides), coefficients, chain compensation
        glideLog (hpfCur, std::clamp (s.hpfHz, 10.0, 0.45 * fs), kGain);
        glideLog (lpfCur, std::clamp (s.lpfHz, 10.0, 0.45 * fs), kGain);

        for (auto* g : { &gEcho, &gRev, &gFb, &gLoopScale, &gWidth, &gNoise, &gWPre, &gWLoop, &gWPost, &gDrive, &gBass, &gTreble,
                         &gDriveOff, &gPreVc, &gPreEven, &gLt })
            g->block (kGain);
        for (auto& g : gHead) g.block (kGain);
        for (auto& g : gPan) g.block (kGain);
        gDrop.block (1.0); // already a smooth random process: linear between control points
        gDuck.block (1.0); // attack/release applied above

        updateCoefficients (true);
        gComp.target = comp;
        gComp.block (kGain); // a moving chain delay is a smooth tape-delay change, never a jump

        // the spring runs only while it can be heard (restarted silent: no click, REVERB VOLUME ramps from 0)
        if (! springRunning && gRev.target > 0.0)
        {
            springA.reset();
            springB.reset();
            springRunning = true;
        }
        else if (springRunning && gRev.target <= 0.0 && gRev.end <= 0.0 && gRev.cur <= 0.0)
        {
            springRunning = false;
        }
    }

    void EchoEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
    {
        std::array<float, 3> peaks {};
        for (int i = 0; i < numSamples; ++i)
        {
            if (tape.atBlockStart())
                controlBlock();
            const bool telemetry = tape.atBlockStart();

            const double xl = inL[i], xr = inR[i];

            // DUCK detector: peak of the dry input, instant attack, 50 ms release
            const double pk = std::max (std::abs (xl), std::abs (xr));
            duckEnv = pk > duckEnv ? pk : duckEnv * duckDetRel;
            duckBlockPeak = std::max (duckBlockPeak, duckEnv);

            // ---- playback heads
            const double pNow = tape.positionNow();
            const double compNow = gComp.next();
            const double cv = compNow * tape.currentSpeed();
            double m = 0.0, sd = 0.0;
            for (int h = 0; h < 3; ++h)
            {
                const double d = tape.locate (h, pNow - timing::headRatio[(size_t) h] + cv);
                const double r = tape.read (d);
                if (telemetry)
                    headDelayMs[(size_t) h].store ((float) ((d + compNow) * 1000.0 / fs), std::memory_order_relaxed);
                const double g = gHead[(size_t) h].next();
                const double p = gPan[(size_t) h].next();
                peaks[(size_t) h] = std::max (peaks[(size_t) h], (float) std::abs (r));
                m += g * r;
                sd -= g * p * r;
            }

            // ---- playback amplifier: LF roll-off, head loss, BASS, TREBLE
            const double mF = chainM.process (m);
            const double sF = chainS.process (sd);

            // ---- POST filter (crossfaded weight; one of PRE/LOOP/POST is 1 at rest)
            const double wPost = gWPost.next();
            const double mP = mF + wPost * (postM.process (mF) - mF);
            const double sP = sF + wPost * (postS.process (sF) - sF);

            // ---- feedback: [LOOP filter] -> INTENSITY
            double l = mF * gLoopScale.next();
            const double wLoop = gWLoop.next();
            l += wLoop * (loopFilter.process (l) - l);
            const double fb = l * gFb.next();

            // ---- input: mono, [PRE filter]
            const double x = 0.5 * (xl + xr);
            const double wPre = gWPre.next();
            const double xp = x + wPre * (preFilter.process (x) - x);

            // ---- record stage: preamp -> (+ feedback) -> tape saturation
            const double drive = gDrive.next();
            const double vc = gPreVc.next(), even = gPreEven.next();
            const double dGain = sat::driveGain (drive, gDriveOff.next());
            const double inGain = dGain / vc, outGain = vc / dGain;
            const double lt = gLt.next();
            double rec, pre;
            if (os == 4)
            {
                double a[2], b[2], r[2], q[2];
                upIn.process (xp, a[0], a[1]);
                upFb.process (fb, b[0], b[1]);
                for (int k = 0; k < 2; ++k)
                {
                    double a0, a1, b0, b1;
                    upIn4.process (a[k], a0, a1);
                    upFb4.process (b[k], b0, b1);
                    const double p0 = preamp.process (a0, inGain, outGain, even);
                    const double p1 = preamp.process (a1, inGain, outGain, even);
                    const double s0 = tapeSat.process (p0 + b0, lt);
                    const double s1 = tapeSat.process (p1 + b1, lt);
                    r[k] = downTape4.process (s0, s1);
                    q[k] = downPre4.process (p0, p1);
                }
                rec = downTape.process (r[0], r[1]);
                pre = downPre.process (q[0], q[1]);
            }
            else if (os == 2)
            {
                double a0, a1, b0, b1;
                upIn.process (xp, a0, a1);
                upFb.process (fb, b0, b1);
                const double p0 = preamp.process (a0, inGain, outGain, even);
                const double p1 = preamp.process (a1, inGain, outGain, even);
                const double s0 = tapeSat.process (p0 + b0, lt);
                const double s1 = tapeSat.process (p1 + b1, lt);
                rec = downTape.process (s0, s1);
                pre = downPre.process (p0, p1);
            }
            else
            {
                pre = preamp.process (xp, inGain, outGain, even);
                rec = tapeSat.process (pre + fb, lt);
            }
            rec = (rec + gNoise.next() * hiss.next()) * gDrop.next();
            tape.write (std::isfinite (rec) ? (float) rec : 0.0f);

            // ---- echo and spring, stereo width, duck
            const double eg = gEcho.next();
            const double eM = eg * mP, eS = eg * sP;
            const double rg = gRev.next();
            double rM = 0.0, rS = 0.0;
            if (springRunning)
            {
                const double sIn = pre + eM;
                const double a = springA.process (sIn), b = springB.process (sIn);
                rM = rg * 0.5 * (a + b);
                rS = rg * 0.5 * (a - b);
            }
            const double w = gWidth.next(), dk = gDuck.next();
            const double M = eM + rM, S = eS + rS;
            outL[i] = (float) ((M + w * S) * dk);
            outR[i] = (float) ((M - w * S) * dk);
        }

        for (int h = 0; h < 3; ++h)
        {
            float cur = headPeak[(size_t) h].load (std::memory_order_relaxed);
            while (peaks[(size_t) h] > cur && ! headPeak[(size_t) h].compare_exchange_weak (cur, peaks[(size_t) h], std::memory_order_relaxed)) {}
        }
    }
}
