#include "TapeEngine.h"

namespace tleet::tape
{
    namespace
    {
        constexpr double sqrt3 = 1.7320508075688772;

        double dbToLin (double db) noexcept { return std::pow (10.0, db / 20.0); }

        // |E(j 2 pi f)| of the analogue record pre-emphasis at speed ips
        double emphasisMagnitude (double hz, double ips) noexcept
        {
            const double wt = 2.0 * model::pi * hz * model::eqTau (ips);
            return std::sqrt ((1.0 + wt * wt) / (1.0 + (wt / model::preEmphasisCeiling) * (wt / model::preEmphasisCeiling)));
        }

        // The type MOLs are specified at 15 ips through the whole machine: the field scale
        // is referred to the 1 kHz pre-emphasis gain at 15 ips (+0.19 dB).
        const double emphasis15At1k = emphasisMagnitude (1000.0, 15.0);

        float recordAmp (float x) noexcept
        {
            // y = x / (1 + |x / Vc|^8)^(1/8): the record amplifier's output stage (MODEL_SPEC 3.2):
            // clean (< 0.05 % H3) up to 5 dB below its clip point, then a firm knee
            const float r = x * (float) (1.0 / model::recordAmpClip);
            const float r2 = r * r, r4 = r2 * r2;
            return x / std::sqrt (std::sqrt (std::sqrt (1.0f + r4 * r4)));
        }

        // Fundamental gain of recordAmp() for a sine of peak amplitude a (in units of the clip
        // level), tabulated -40..+40 dB in 0.25 dB steps (AUTO GAIN's reference, MODEL_SPEC 8).
        struct RecordAmpTable
        {
            static constexpr int size = 321;
            static constexpr double minDb = -40.0, stepDb = 0.25;
            std::array<double, size> g {};
            RecordAmpTable()
            {
                for (int i = 0; i < size; ++i)
                {
                    const double a = std::pow (10.0, (minDb + stepDb * i) / 20.0);
                    double acc = 0.0;
                    const int n = 256;
                    for (int k = 0; k < n; ++k)
                    {
                        const double sn = std::sin (2.0 * model::pi * (k + 0.5) / n);
                        const double r = a * sn, r4 = r * r * r * r;
                        acc += (a * sn / std::pow (1.0 + r4 * r4, 0.125)) * sn;
                    }
                    g[(size_t) i] = (2.0 * acc / n) / a;
                }
            }
            static const RecordAmpTable& get() { static const RecordAmpTable t; return t; }
            double gain (double ampDb) const noexcept
            {
                const double x = std::clamp ((ampDb - minDb) / stepDb, 0.0, (double) (size - 1) - 1.0e-9);
                const int i = (int) x;
                return g[(size_t) i] + (x - i) * (g[(size_t) i + 1] - g[(size_t) i]);
            }
        };

        // FIR dot product in eight independent lanes (vectorises without reassociation flags)
        inline float dot (const float* a, const float* b, int n) noexcept
        {
            float s[8] {};
            int k = 0;
            for (; k + 8 <= n; k += 8)
                for (int j = 0; j < 8; ++j)
                    s[j] += a[k + j] * b[k + j];
            float r = ((s[0] + s[4]) + (s[1] + s[5])) + ((s[2] + s[6]) + (s[3] + s[7]));
            for (; k < n; ++k)
                r += a[k] * b[k];
            return r;
        }

        void setLowPass (Svf& f, double hz, double q, double fs) noexcept
        {
            f.setCoeffs (Svf::warp (hz, fs), 1.0 / q, 0.0, 0.0, 1.0);
        }
    }

    TapeEngine::TapeEngine()
    {
        for (auto* r : { &inputDb, &satDb, &current, &bumpPct, &lfDb, &hfDb, &noiseAmt, &xtalk, &agDb })
            r->seconds = 0.03;
        for (auto* r : { &calDb, &age, &wow, &flutter })
            r->seconds = 0.05;
        noiseOn.seconds = 0.02;
        logIps.seconds = 0.1;
    }

    void TapeEngine::prepare (double sampleRate, int maxBlockSize, int numChannels, const EngineSettings& initial, int osLog2Override)
    {
        fs = sampleRate;
        numCh = std::clamp (numChannels, 1, 2);
        maxBlock = std::max (1, maxBlockSize);
        osLog2 = osLog2Override >= 0 ? std::clamp (osLog2Override, 0, 2) : Oversampler::factorLog2ForRate (fs);
        fsTop = fs * (1 << osLog2);
        (void) HysteresisTables::get(); // built once per process, off the audio thread
        (void) RecordAmpTable::get();

        const int centreHost = (int) std::lround (model::transportCentreSeconds * fs);
        transportCentreTop = centreHost << osLog2;
        designer.prepare (fs);
        {
            std::lock_guard<std::mutex> lock (uiMutex);
            uiDesigner.prepare (fs);
        }
        const int taps = designer.numTaps();
        for (auto& c : chans)
        {
            c.os.prepare (osLog2, controlBlock);
            c.delay.prepare (transportCentreTop + (int) std::ceil (model::transportMaxDeviation * 1.05 * fsTop + model::azimuthSecondsMax * fsTop) + 8);
            c.firOld.assign ((size_t) taps, 0.0f);
            c.firNew.assign ((size_t) taps, 0.0f);
            c.firHist.assign ((size_t) (2 * taps), 0.0f);
            c.wearSplit.set (model::wearSplitHz, fs);
            setLowPass (c.hissLp, std::min (20000.0, 0.45 * fs), 0.7071, fs);
            c.modNoise.prepare (400.0, 0.4, fs);
            c.wear.prepare (2.0, 0.5, fs / controlBlock);
        }
        latencySamples = chans[0].os.latency() + centreHost + designer.latency();
        designInterval = std::max (controlBlock, (int) std::lround (64.0 * fs / 48000.0 / controlBlock) * controlBlock);
        firFadeLength = designInterval;
        agDelay.assign ((size_t) latencySamples + 1, 1.0f);
        for (auto& b : eBuf) b.assign ((size_t) controlBlock, 0.0f);
        for (auto& b : tBuf) b.assign ((size_t) controlBlock, 0.0f);
        topBuf.assign ((size_t) (controlBlock << 3), 0.0f);
        wowFlutter.prepare (fs / controlBlock);
        rsFs.store (fs);

        targets = initial;
        typeFrom = typeTo = std::clamp (initial.type, 0, 2);
        pendingType = -1;
        typeW = 1.0;
        typeStep = (double) controlBlock / (0.05 * fs);
        inputDb.snap (initial.inputDb);
        calDb.snap (calibrationDb[std::clamp (initial.calibration, 0, 3)]);
        satDb.snap (model::saturationDb (initial.saturation));
        current.snap (model::biasCurrent (initial.bias));
        logIps.snap (std::log (model::speedIps[std::clamp (initial.speed, 0, 2)]));
        age.snap (initial.age / 100.0);
        bumpPct.snap (initial.headBump / 100.0);
        lfDb.snap (initial.lf * model::trimDbPerStep);
        hfDb.snap (initial.hf * model::trimDbPerStep);
        wow.snap (initial.wow / 100.0);
        flutter.snap (initial.flutter / 100.0);
        noiseOn.snap (initial.noise ? 1.0 : 0.0);
        noiseAmt.snap (initial.noiseAmount);
        xtalk.snap (initial.crosstalk);
        agDb.snap (initial.autoGain ? 1.0 : 0.0); // AUTO GAIN on/off weight
        reset();
    }

    void TapeEngine::reset()
    {
        std::uint64_t seed = 0xA5A5F00Dull;
        for (auto& c : chans)
        {
            c.pre.reset(); c.de.reset(); c.hissShape.reset();
            c.os.reset();
            c.ja.reset();
            c.delay.reset();
            std::fill (c.firHist.begin(), c.firHist.end(), 0.0f);
            c.firPos = 0;
            for (auto* f : { &c.rolloff, &c.dip, &c.bump, &c.lfShelf, &c.hfShelf, &c.hissLp })
                f->reset();
            c.wearSplit.reset();
            c.xtalk.reset();
            c.hissRng.seed (seed += 0x1234567ull);
            c.modRng.seed (seed += 0x7654321ull);
            c.wearRng.seed (seed += 0x0F0F0F1ull);
            c.modNoise.ic1 = c.modNoise.ic2 = 0.0;
            c.wear.ic1 = c.wear.ic2 = 0.0;
            c.wearPrev = c.wearNow = 0.0;
            c.maxQ = 0.0;
        }
        wowFlutter.reset();
        std::fill (agDelay.begin(), agDelay.end(), 1.0f);
        agPos = 0;
        firFade = 0;
        samplesSinceDesign = 0;
        sampleCounter = 0;
        updateControl (0);
        // start every interpolation at its current value
        inGainPrev = inGainNow; drivePrev = driveNow; makeupPrev = makeupNow; agPrev = agNow;
        tauPrev = tauNow; hissPrev = hissNow; modPrev = modNow; xtPrev = xtNow;
        for (auto& c : chans) c.wearPrev = c.wearNow;
        std::fill (agDelay.begin(), agDelay.end(), (float) agNow);
        designLoss (true);
    }

    double TapeEngine::sensitivityFor (int type, double c, double cOpt) const noexcept
    {
        const auto& t = HysteresisTables::get();
        return 20.0 * std::log10 (t.smallSignalGain (type, c) / t.smallSignalGain (type, std::max (c, cOpt)));
    }

    double TapeEngine::autoGainDbFor (double inDb, double cal, double sat, int type, double cur, double ips) const noexcept
    {
        const auto& ts = model::typeSpec (type);
        const auto& t = HysteresisTables::get();
        const double c = model::reversibility (cur, ts.cOpt);
        // the record amplifier sees the pre-emphasised reference; the tape sees its output
        const double e1k = emphasisMagnitude (1000.0, ips);
        const double ampDb = inDb + 20.0 * std::log10 (e1k * model::zeroVuPeak / model::recordAmpClip);
        const double ga = RecordAmpTable::get().gain (ampDb);
        const double href = dbToLin (inDb + cal + sat) * ga * e1k * ts.hScale / emphasis15At1k;
        const double g = t.gain (type, c, 20.0 * std::log10 (std::max (href, 1.0e-6)));
        const double gn = t.smallSignalGain (type, std::max (c, ts.cOpt));
        return std::clamp (-inDb - 20.0 * std::log10 (std::max (ga * g / gn, 1.0e-6)), -40.0, 40.0);
    }

    void TapeEngine::updateControl (int n) noexcept
    {
        const auto& s = targets;
        inputDb.setTarget (s.inputDb, fs);
        calDb.setTarget (calibrationDb[std::clamp (s.calibration, 0, 3)], fs);
        satDb.setTarget (model::saturationDb (s.saturation), fs);
        current.setTarget (model::biasCurrent (s.bias), fs);
        logIps.setTarget (std::log (model::speedIps[std::clamp (s.speed, 0, 2)]), fs);
        age.setTarget (std::clamp (s.age, 0.0f, 100.0f) / 100.0, fs);
        bumpPct.setTarget (std::clamp (s.headBump, 0.0f, 100.0f) / 100.0, fs);
        lfDb.setTarget (std::clamp (s.lf, -100.0f, 100.0f) * model::trimDbPerStep, fs);
        hfDb.setTarget (std::clamp (s.hf, -100.0f, 100.0f) * model::trimDbPerStep, fs);
        wow.setTarget (std::clamp (s.wow, 0.0f, 100.0f) / 100.0, fs);
        flutter.setTarget (std::clamp (s.flutter, 0.0f, 100.0f) / 100.0, fs);
        noiseOn.setTarget (s.noise ? 1.0 : 0.0, fs);
        noiseAmt.setTarget (std::clamp (s.noiseAmount, 0.0f, 100.0f), fs);
        xtalk.setTarget (std::clamp (s.crosstalk, 0.0f, 100.0f), fs);
        agDb.setTarget (s.autoGain ? 1.0 : 0.0, fs);

        // TAPE TYPE: a 50 ms blend of every type-dependent quantity; a new choice
        // during a blend waits for it to finish
        const int wantType = std::clamp (s.type, 0, 2);
        if (typeW >= 1.0)
        {
            if (pendingType >= 0) { if (pendingType != typeTo) { typeFrom = typeTo; typeTo = pendingType; typeW = 0.0; } pendingType = -1; }
            else if (wantType != typeTo) { typeFrom = typeTo; typeTo = wantType; typeW = 0.0; }
        }
        else if (wantType != typeTo)
            pendingType = wantType;

        const bool filtersMoving = n == 0 || ! exactly (logIps.value, logIps.target) || ! exactly (bumpPct.value, bumpPct.target)
                                   || ! exactly (lfDb.value, lfDb.target) || ! exactly (hfDb.value, hfDb.target);
        for (auto* r : { &inputDb, &calDb, &satDb, &current, &logIps, &age, &bumpPct, &lfDb, &hfDb, &wow, &flutter, &noiseOn, &noiseAmt, &xtalk, &agDb })
            r->advance (n);
        if (typeW < 1.0)
            typeW = std::min (1.0, typeW + typeStep * (n > 0 ? 1.0 : 0.0));

        inGainPrev = inGainNow; drivePrev = driveNow; makeupPrev = makeupNow; agPrev = agNow;
        tauPrev = tauNow; hissPrev = hissNow; modPrev = modNow; xtPrev = xtNow;
        for (auto& c : chans) c.wearPrev = c.wearNow;

        // ---- magnetics
        const auto& tf = model::typeSpec (typeFrom);
        const auto& tt = model::typeSpec (typeTo);
        jaParams.k = typeBlend (tf.kPrime, tt.kPrime);
        jaParams.alpha = typeBlend (tf.alphaPrime, tt.alphaPrime);
        const double I = current.value;
        const double cF = model::reversibility (I, tf.cOpt), cT = model::reversibility (I, tt.cOpt);
        jaParams.c = typeBlend (cF, cT);
        const auto& tab = HysteresisTables::get();
        const double gNorm = typeBlend (tab.smallSignalGain (typeFrom, std::max (cF, tf.cOpt)), tab.smallSignalGain (typeTo, std::max (cT, tt.cOpt)));
        const double hScale = typeBlend (tf.hScale, tt.hScale) / emphasis15At1k;
        const double driveLin = dbToLin (calDb.value + satDb.value);
        driveNow = driveLin * hScale / model::zeroVuPeak;
        makeupNow = model::zeroVuPeak / (driveLin * hScale * gNorm);
        inGainNow = dbToLin (inputDb.value);

        // ---- AUTO GAIN (MODEL_SPEC 8)
        const double ips = std::exp (logIps.value);
        const double agF = autoGainDbFor (inputDb.value, calDb.value, satDb.value, typeFrom, I, ips);
        const double agT = autoGainDbFor (inputDb.value, calDb.value, satDb.value, typeTo, I, ips);
        agNow = dbToLin (agDb.value * typeBlend (agF, agT));
        agValueDb = agDb.value * typeBlend (agF, agT);

        // ---- AGE
        const double a = age.value;
        hdc = model::ageHeadFieldMax * a;
        azimuth = model::azimuthSecondsMax * a;
        gainL = dbToLin (model::ageGainMismatchDb * a);
        gainR = dbToLin (-model::ageGainMismatchDb * a);
        wearHf = model::wearHfRms * a;
        wearBroad = model::wearBroadRms * a;

        // ---- transport
        if (n > 0)
        {
            TransportSettings ts { ips, wow.value, flutter.value, a };
            tauNow = wowFlutter.next (ts);
            for (int ch = 0; ch < numCh; ++ch)
                chans[(size_t) ch].wearNow = chans[(size_t) ch].wear.next (chans[(size_t) ch].wearRng);
        }

        if (filtersMoving)
            updateFilters();

        // ---- noise (MODEL_SPEC 7)
        const double amount = model::noiseAmountGain (noiseAmt.value) * dbToLin (model::ageNoiseDb * a) * noiseOn.value;
        const double snr = model::snrDb (ips) + typeBlend (tf.snrOffsetDb, tt.snrOffsetDb);
        hissNow = model::zeroVuRms * dbToLin (-snr - calDb.value) * amount * hissNorm;
        modNow = dbToLin (model::modulationNoiseDb) * amount;

        // ---- crosstalk
        const double corner = model::crosstalkCornerHz15 * ips / 15.0;
        // the long-wavelength plateau is fixed (it is set at 1 kHz, 15 ips); the corner follows the speed
        const double c15 = model::crosstalkCornerHz15;
        xtNow = numCh > 1 ? model::crosstalkGain1k (xtalk.value) * std::sqrt (1.0 + (1000.0 / c15) * (1000.0 / c15)) : 0.0;
        if (filtersMoving)
            for (auto& c : chans)
                c.xtalk.set (corner, fs);

        // ---- head-loss FIR
        samplesSinceDesign += n;
        if (n > 0)
            designLoss (false);

        // ---- response snapshot (UI)
        rsIps.store (ips, std::memory_order_relaxed);
        rsSpacingL.store (model::ageSpacingMetres * a, std::memory_order_relaxed);
        rsSpacingR.store (model::ageSpacingMetres * a * (1.0 + model::ageRightSpacingExtra), std::memory_order_relaxed);
        rsZone.store (model::biasZoneMetres (I), std::memory_order_relaxed);
        rsBump.store (model::bumpMaxDb * model::bumpSpeedScale (ips) * bumpPct.value, std::memory_order_relaxed);
        rsLf.store (lfDb.value, std::memory_order_relaxed);
        rsHf.store (hfDb.value, std::memory_order_relaxed);
        rsSens.store (typeBlend (sensitivityFor (typeFrom, cF, tf.cOpt), sensitivityFor (typeTo, cT, tt.cOpt)), std::memory_order_relaxed);
        rsAge.store (a, std::memory_order_relaxed);
    }

    void TapeEngine::updateFilters() noexcept
    {
        const double ips = std::exp (logIps.value);
        const double v = model::speedMetres (ips);
        const double tau = model::eqTau (ips);
        const double fb = v / model::bumpWavelengthMetres;
        const double bumpDb = model::bumpMaxDb * model::bumpSpeedScale (ips) * bumpPct.value;
        for (auto& c : chans)
        {
            c.pre.set (tau, model::preEmphasisCeiling, fs, false);
            c.de.set (tau, model::preEmphasisCeiling, fs, true);
            c.hissShape.set (tau, model::hissShelf, fs, false);
            c.rolloff.setHighPass (model::rolloffRatio * fb, model::rolloffQ, fs);
            c.dip.setBell (model::dipRatio * fb, model::dipQ, -model::dipShare * bumpDb, fs);
            c.bump.setBell (fb, model::bumpQ, bumpDb, fs);
            c.lfShelf.setLowShelf (model::lfTrimHz, 0.7071, lfDb.value, fs);
            c.hfShelf.setHighShelf (1.0 / (2.0 * model::pi * tau), 0.7071, hfDb.value, fs);
        }
        // hiss: unit RMS of the shaped noise in 20 Hz - 20 kHz (unit-variance white input)
        double p = 0.0;
        const int steps = 200;
        for (int i = 0; i <= steps; ++i)
        {
            const double hz = 20.0 + (20000.0 - 20.0) * i / steps;
            const double m = std::abs (chans[0].hissShape.response (hz, fs) * chans[0].hissLp.response (hz, fs));
            p += (i == 0 || i == steps ? 0.5 : 1.0) * m * m;
        }
        p *= (20000.0 - 20.0) / steps * 2.0 / fs;
        hissNorm = 1.0 / std::sqrt (std::max (p, 1.0e-12));
    }

    void TapeEngine::designLoss (bool force) noexcept
    {
        const double ips = std::exp (logIps.value);
        const double a = age.value;
        LossParams p[2];
        p[0] = { ips, model::ageSpacingMetres * a, model::biasZoneMetres (current.value) };
        p[1] = { ips, model::ageSpacingMetres * a * (1.0 + model::ageRightSpacingExtra), p[0].biasZone };
        auto differs = [] (const LossParams& x, const LossParams& y)
        {
            return std::abs (x.ips - y.ips) > 1.0e-6 * y.ips || std::abs (x.spacing - y.spacing) > 1.0e-11 || std::abs (x.biasZone - y.biasZone) > 1.0e-11;
        };
        if (force)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                designer.design (p[ch], chans[(size_t) ch].firNew.data());
                chans[(size_t) ch].firOld = chans[(size_t) ch].firNew;
                lossDesigned[ch] = p[ch];
            }
            firFade = 0;
            samplesSinceDesign = 0;
            return;
        }
        if (firFade > 0 || samplesSinceDesign < designInterval)
            return;
        if (! differs (p[0], lossDesigned[0]) && ! differs (p[1], lossDesigned[1]))
            return;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = chans[(size_t) ch];
            std::copy (c.firNew.begin(), c.firNew.end(), c.firOld.begin());
            designer.design (p[ch], c.firNew.data());
            lossDesigned[ch] = p[ch];
        }
        firFade = firFadeLength;
        samplesSinceDesign = 0;
    }

    void TapeEngine::process (const float* const* in, float* const* wet, float* const* repro, int total) noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            recSq[(size_t) ch] = repSq[(size_t) ch] = 0.0;
            recPk[(size_t) ch] = repPk[(size_t) ch] = 0.0;
        }
        double maxQ = 0.0;
        const int osf = 1 << osLog2;
        const double invK = 1.0 / controlBlock;
        const int taps = designer.numTaps();

        for (int start = 0; start < total;)
        {
            const int phase = (int) (sampleCounter % controlBlock);
            if (phase == 0)
                updateControl (controlBlock);
            const int n = std::min (controlBlock - phase, total - start);

            // ---- stage A (host rate): INPUT, RECORD VU, pre-emphasis
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = chans[(size_t) ch];
                const float* x = in[ch] + start;
                float* e = eBuf[(size_t) ch].data();
                double sq = 0.0, pk = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double t = (phase + i + 1) * invK;
                    const double g = inGainPrev + t * (inGainNow - inGainPrev);
                    const double v = std::clamp ((double) x[i], -1.0e4, 1.0e4) * g;
                    sq += v * v;
                    pk = std::max (pk, std::abs (v));
                    e[i] = (float) c.pre.process (v);
                }
                recSq[(size_t) ch] += sq / (model::zeroVuRms * model::zeroVuRms);
                recPk[(size_t) ch] = std::max (recPk[(size_t) ch], pk);
            }

            // ---- stage B (top rate): record amp, drive, hysteresis, make-up, transport.
            // Both channels in one loop: two independent solver chains keep the CPU busy.
            const int nTop = n << osLog2;
            float* u[2] { topBuf.data(), topBuf.data() + (controlBlock << 2) };
            for (int ch = 0; ch < numCh; ++ch)
                chans[(size_t) ch].os.upsample (eBuf[(size_t) ch].data(), u[ch], n);
            {
                const double azL = -azimuth * fsTop, azR = azimuth * fsTop;
                double qMaxL = 0.0, qMaxR = 0.0;
                auto& cl = chans[0];
                auto& cr = chans[1];
                for (int j = 0; j < nTop; ++j)
                {
                    const double t = (phase + (j >> osLog2) + 1) * invK;
                    const double drive = drivePrev + t * (driveNow - drivePrev);
                    const double makeup = makeupPrev + t * (makeupNow - makeupPrev);
                    const double tt = ((phase << osLog2) + j + 1) * (invK / osf);
                    const double base = transportCentreTop + (tauPrev + tt * (tauNow - tauPrev)) * fsTop;
                    const double mL = cl.ja.process ((double) recordAmp (u[0][j]) * drive + hdc, jaParams);
                    if (numCh > 1)
                    {
                        const double mR = cr.ja.process ((double) recordAmp (u[1][j]) * drive + hdc, jaParams);
                        qMaxR = std::max (qMaxR, std::abs (cr.ja.effectiveField()));
                        cr.delay.push ((float) (mR * makeup));
                        u[1][j] = cr.delay.read (base + azR);
                    }
                    qMaxL = std::max (qMaxL, std::abs (cl.ja.effectiveField()));
                    cl.delay.push ((float) (mL * makeup));
                    u[0][j] = cl.delay.read (base + azL);
                }
                maxQ = std::max ({ maxQ, qMaxL, qMaxR });
            }
            for (int ch = 0; ch < numCh; ++ch)
                chans[(size_t) ch].os.downsample (u[ch], tBuf[(size_t) ch].data(), n);

            // ---- stage C (host rate): de-emphasis, noise, losses, repro EQ, wear, mismatch
            float wFade[controlBlock];
            for (int i = 0; i < n; ++i)
            {
                wFade[i] = firFade > 0 ? 1.0f - (float) (firFade - 1) / (float) firFadeLength : 1.0f;
                if (firFade > 0) --firFade;
            }
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = chans[(size_t) ch];
                float* y = tBuf[(size_t) ch].data();
                const double chGain = ch == 0 ? gainL : gainR;
                for (int i = 0; i < n; ++i)
                {
                    const double t = (phase + i + 1) * invK;
                    double v = c.de.process (y[i]);
                    // noise on the tape: hiss (shaped) and modulation noise (signal x band noise)
                    const double hissAmp = hissPrev + t * (hissNow - hissPrev);
                    const double modAmp = modPrev + t * (modNow - modPrev);
                    const double wn = c.hissRng.uniform() * sqrt3;
                    const double mn = c.modNoise.next (c.modRng);
                    if (hissAmp > 0.0 || modAmp > 0.0)
                        v += hissAmp * c.hissLp.process (c.hissShape.process (wn)) + modAmp * mn * v;
                    // head-loss FIR (mirrored history, crossfade after a redesign)
                    if (++c.firPos == taps) c.firPos = 0;
                    c.firHist[(size_t) c.firPos] = (float) v;
                    c.firHist[(size_t) (c.firPos + taps)] = (float) v;
                    const float* hst = c.firHist.data() + c.firPos + 1; // oldest first
                    const float accN = dot (c.firNew.data(), hst, taps);
                    double lv = accN;
                    if (wFade[i] < 1.0f)
                    {
                        const float accO = dot (c.firOld.data(), hst, taps);
                        lv = accO + wFade[i] * (accN - accO);
                    }
                    // repro head LF: roll-off, dip, bump; repro EQ trims
                    lv = c.hfShelf.process (c.lfShelf.process (c.bump.process (c.dip.process (c.rolloff.process (lv)))));
                    // wear variation (contact): HF band and broadband level fluctuate
                    const double w = c.wearPrev + t * (c.wearNow - c.wearPrev);
                    const double lo = c.wearSplit.low (lv);
                    lv = lo * (1.0 + wearBroad * w) + (lv - lo) * (1.0 + wearHf * w);
                    y[i] = (float) (lv * chGain);
                }
            }

            // ---- stage D: crosstalk, REPRO point, AUTO GAIN (delayed by the latency)
            const int L = latencySamples;
            const int ringSize = (int) agDelay.size();
            for (int i = 0; i < n; ++i)
            {
                const double t = (phase + i + 1) * invK;
                const double xg = xtPrev + t * (xtNow - xtPrev);
                double yv[2] { tBuf[0][(size_t) i], numCh > 1 ? tBuf[1][(size_t) i] : 0.0 };
                if (numCh > 1)
                {
                    const double bl = chans[0].xtalk.low (yv[0]), br = chans[1].xtalk.low (yv[1]);
                    yv[0] += xg * br;
                    yv[1] += xg * bl;
                }
                agDelay[(size_t) agPos] = (float) (agPrev + t * (agNow - agPrev));
                const float ag = agDelay[(size_t) ((agPos - L + ringSize) % ringSize)];
                if (++agPos == ringSize) agPos = 0;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const double r = yv[ch];
                    repro[ch][start + i] = (float) r;
                    wet[ch][start + i] = (float) r * ag;
                    repSq[(size_t) ch] += r * r;
                    repPk[(size_t) ch] = std::max (repPk[(size_t) ch], std::abs (r));
                }
            }

            sampleCounter += n;
            start += n;
        }
        for (int ch = 0; ch < 2; ++ch)
            repSq[(size_t) ch] /= model::zeroVuRms * model::zeroVuRms;
        satActivity = ja::knee (maxQ);
    }

    ResponseState TapeEngine::responseState() const noexcept
    {
        ResponseState r;
        r.ips = rsIps.load (std::memory_order_relaxed);
        r.spacingL = rsSpacingL.load (std::memory_order_relaxed);
        r.spacingR = rsSpacingR.load (std::memory_order_relaxed);
        r.biasZone = rsZone.load (std::memory_order_relaxed);
        r.bumpDb = rsBump.load (std::memory_order_relaxed);
        r.lfDb = rsLf.load (std::memory_order_relaxed);
        r.hfDb = rsHf.load (std::memory_order_relaxed);
        r.sensitivityDb = rsSens.load (std::memory_order_relaxed);
        r.age = rsAge.load (std::memory_order_relaxed);
        return r;
    }

    void TapeEngine::responseDb (const double* hz, double* outDb, int count, int channel) const
    {
        const auto st = responseState();
        const double rate = rsFs.load (std::memory_order_relaxed);
        const double v = model::speedMetres (st.ips);
        const double tau = model::eqTau (st.ips);
        const double fb = v / model::bumpWavelengthMetres;
        Svf rolloff, dip, bump, lfS, hfS;
        rolloff.setHighPass (model::rolloffRatio * fb, model::rolloffQ, rate);
        dip.setBell (model::dipRatio * fb, model::dipQ, -model::dipShare * st.bumpDb, rate);
        bump.setBell (fb, model::bumpQ, st.bumpDb, rate);
        lfS.setLowShelf (model::lfTrimHz, 0.7071, st.lfDb, rate);
        hfS.setHighShelf (1.0 / (2.0 * model::pi * tau), 0.7071, st.hfDb, rate);
        const double mis = model::ageGainMismatchDb * st.age;

        float fir[2][129];
        int taps = 0;
        {
            std::lock_guard<std::mutex> lock (uiMutex);
            taps = uiDesigner.numTaps();
            uiDesigner.design ({ st.ips, st.spacingL, st.biasZone }, fir[0]);
            uiDesigner.design ({ st.ips, st.spacingR, st.biasZone }, fir[1]);
        }
        const int centre = (taps - 1) / 2;
        for (int i = 0; i < count; ++i)
        {
            const double f = hz[i];
            const double w = 2.0 * model::pi * f / rate;
            const double common = std::abs (rolloff.response (f, rate) * dip.response (f, rate) * bump.response (f, rate)
                                            * lfS.response (f, rate) * hfS.response (f, rate));
            double pw[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                double a = fir[ch][centre];
                for (int n = 1; n <= centre; ++n)
                    a += 2.0 * fir[ch][centre + n] * std::cos (w * n);
                const double g = std::abs (a) * common * std::pow (10.0, ((ch == 0 ? mis : -mis) + st.sensitivityDb) / 20.0);
                pw[ch] = g * g;
            }
            const double p = channel == 0 ? pw[0] : channel == 1 ? pw[1] : 0.5 * (pw[0] + pw[1]);
            outDb[i] = 10.0 * std::log10 (std::max (p, 1.0e-30));
        }
    }

    double TapeEngine::responseDb (double hz, int channel) const
    {
        double out = 0.0;
        responseDb (&hz, &out, 1, channel);
        return out;
    }
}
