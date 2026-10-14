#include "SteelEngine.h"

namespace tleet::steel
{
    // ================================================================== LimiterSlot

    void LimiterSlot::prepare (double sampleRate, int maxBlock, int numChannels, int maxLookaheadHost)
    {
        fs = sampleRate;
        channels = std::clamp (numChannels, 1, maxChannels);
        const auto& set = HalfbandSet::get();
        const int maxOs = maxLookaheadHost * 8 + 8; // 8x, plus the pad
        for (auto& ch : chans)
        {
            for (int s = 0; s < 3; ++s)
            {
                ch.up[s].prepare (set.stage[s]);
                ch.down[s].prepare (set.stage[s]);
            }
            ch.bufA.assign ((size_t) (maxBlock * 8), 0.0f);
            ch.bufB.assign ((size_t) (maxBlock * 8), 0.0f);
            ch.sig.prepare (maxOs + 1);
            ch.rdel.prepare (maxOs + 1);
            ch.smin.prepare (maxOs + 2);
            ch.box.prepare (maxOs + 2);
            ch.tel.prepare (64);
        }
        configure (cfg);
    }

    void LimiterSlot::configure (const SlotConfig& c) noexcept
    {
        cfg = c;
        cfg.osLog2 = std::clamp (cfg.osLog2, 0, 3);
        cfg.mode = std::clamp (cfg.mode, 0, numModes - 1);
        const int L = 1 << cfg.osLog2;
        const auto& spec = modeSpec (cfg.mode);
        osLatencyHost = OversamplingLatency::hostSamples (cfg.osLog2);
        padTop = OversamplingLatency::padTopSamples (cfg.osLog2);
        kneeDb = spec.kneeDb;

        // Lookahead D (oversampled samples). The gain ramps over the last `attack`
        // samples of it; the detector is delayed by the rest so that the ramp ends
        // exactly on the peak (MODEL_SPEC 4.2).
        const int D = cfg.lookaheadHost * L;
        const int attack = (int) std::lround (spec.attackFraction * (double) D);
        attackSupport = attack + 1;
        const int downTop = OversamplingLatency::upTopSamples (cfg.osLog2);
        telDelay = (downTop + L - 1) / L; // the frame that contains the centre of output sample n

        for (auto& ch : chans)
        {
            for (int s = 0; s < 3; ++s)
            {
                ch.up[s].reset();
                ch.down[s].reset();
            }
            ch.sig.setDelay (D + padTop);
            ch.sig.reset();
            ch.rdel.setDelay (D + padTop - attack);
            ch.rdel.reset (1.0f);
            ch.smin.setWindow (attackSupport);
            ch.smin.reset();
            ch.box.configure (attackSupport, spec.boxes);
            ch.tel.setDelay (telDelay);
            ch.tel.reset (1.0f);
            ch.ef = 1.0;
            ch.esDb = 0.0;
            ch.esLin = 1.0f;
        }
        slowDb = totalDb = 0.0;
    }

    void LimiterSlot::inheritEnvelope (const LimiterSlot& other) noexcept
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            auto& ch = chans[(size_t) c];
            const auto& o = other.chans[(size_t) c];
            ch.ef = o.ef;
            ch.esDb = o.esDb;
            ch.esLin = o.esLin;
            ch.box.reset (other.lastAuto ? std::min ((float) o.ef, o.esLin) : (float) o.ef);
        }
        lastAuto = other.lastAuto;
    }

    float LimiterSlot::requiredGain (float a, float T, float kneeLow) const noexcept
    {
        if (a <= kneeLow)
            return 1.0f;
        if (kneeDb <= 0.0f)
            return T / a; // a > T
        const float over = 20.0f * std::log10 (a / T);
        const float half = 0.5f * kneeDb;
        if (over >= half)
            return T / a;
        const float gr = (over + half) * (over + half) / (2.0f * kneeDb); // Giannoulis et al. soft knee, ratio infinity
        return std::pow (10.0f, -0.05f * gr);
    }

    void LimiterSlot::process (const float* const* u, float* const* y, float* const* gainTel, int n,
                               const EngineControls& c, int offset) noexcept
    {
        const int L = 1 << cfg.osLog2;
        const int len = n * L;
        const auto& spec = modeSpec (cfg.mode);

        // release coefficients (MODEL_SPEC 4.3)
        const double fo = fs * L;
        const double tauFast = (c.autoRelease ? spec.autoFastMs : std::clamp (c.manualReleaseMs, 1.0f, 5000.0f)) * 0.001;
        const double aF = 1.0 - std::exp (-1.0 / (tauFast * fo));
        const double aSA = 1.0 - std::exp (-1.0 / (spec.autoSlowAttackMs * 0.001 * fs));
        const double aSR = 1.0 - std::exp (-1.0 / (spec.autoSlowReleaseMs * 0.001 * fs));

        if (c.autoRelease != lastAuto)
        {
            for (auto& ch : chans)
            {
                if (c.autoRelease)
                {
                    // the slow branch starts where the gain is: continuous
                    ch.esDb = -20.0 * std::log10 (std::max (ch.ef, 1.0e-9));
                    ch.esLin = (float) ch.ef;
                }
                else
                {
                    ch.ef = std::min (ch.ef, (double) ch.esLin);
                }
            }
            lastAuto = c.autoRelease;
        }

        // up
        float* os[maxChannels] {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& st = chans[(size_t) ch];
            if (L == 1)
            {
                std::copy (u[ch], u[ch] + n, st.bufA.data());
                os[ch] = st.bufA.data();
                continue;
            }
            const float* src = u[ch];
            int l = n;
            for (int s = 0; s < cfg.osLog2; ++s)
            {
                float* dst = (s & 1) ? st.bufB.data() : st.bufA.data();
                st.up[s].process (src, dst, l);
                src = dst;
                l *= 2;
            }
            os[ch] = const_cast<float*> (src);
        }

        SoftClipCurve clip;
        for (int i = 0; i < n; ++i)
        {
            const float T = c.ceiling[offset + i];
            const float cm = c.clipMix[offset + i];
            const float k = c.link[offset + i];
            if (cm > 0.0f)
                clip.set (T, c.clipAmount[offset + i] * spec.clipScale);
            const float kneeLow = kneeDb > 0.0f ? T * std::pow (10.0f, -kneeDb / 40.0f) : T;
            for (int ch = 0; ch < channels; ++ch)
            {
                chans[(size_t) ch].frameMinEf = (float) chans[(size_t) ch].ef;
                chans[(size_t) ch].frameMinG = 1.0f;
            }

            for (int s = 0; s < L; ++s)
            {
                const int m = i * L + s;
                float g[maxChannels] {}, sd[maxChannels] {};
                for (int ch = 0; ch < channels; ++ch)
                {
                    auto& st = chans[(size_t) ch];
                    const float v = os[ch][m];
                    const float vc = cm > 0.0f ? v + cm * (clip.process (v) - v) : v;
                    const float r = requiredGain (std::abs (vc), T, kneeLow);
                    const float h = st.smin.process (st.rdel.process (r));
                    st.ef = h < st.ef ? (double) h : st.ef + aF * ((double) h - st.ef);
                    float e = (float) st.ef;
                    if (c.autoRelease && st.esLin < e)
                        e = st.esLin;
                    g[ch] = st.box.process (e);
                    sd[ch] = st.sig.process (vc);
                    st.frameMinEf = std::min (st.frameMinEf, (float) st.ef);
                }
                if (channels == 2)
                {
                    // STEREO LINK: each channel moves towards the deeper of the two gains.
                    // Never above its own gain, so each channel keeps its guarantee.
                    const float gm = std::min (g[0], g[1]);
                    g[0] += k * (gm - g[0]);
                    g[1] += k * (gm - g[1]);
                }
                for (int ch = 0; ch < channels; ++ch)
                {
                    os[ch][m] = sd[ch] * g[ch];
                    chans[(size_t) ch].frameMinG = std::min (chans[(size_t) ch].frameMinG, g[ch]);
                }
            }

            for (int ch = 0; ch < channels; ++ch)
            {
                auto& st = chans[(size_t) ch];
                gainTel[ch][i] = st.tel.process (st.frameMinG);
                if (c.autoRelease)
                {
                    // slow branch, at the host rate in dB: holds autoSlowDepth of the fast GR
                    const double grF = -20.0 * std::log10 (std::max (st.frameMinEf, 1.0e-9f));
                    const double tgt = spec.autoSlowDepth * grF;
                    st.esDb += (tgt > st.esDb ? aSA : aSR) * (tgt - st.esDb);
                    st.esLin = (float) std::pow (10.0, -0.05 * st.esDb);
                    // effective-release bookkeeping, weighted by the GR itself so that deep
                    // limiting counts and residual fractions of a dB do not
                    const double grT = std::max (grF, st.esDb); // GR of min (fast, slow)
                    slowDb += st.esDb * grT;
                    totalDb += grT * grT;
                }
            }
        }

        // down (in place is safe: output i is written after inputs 2i, 2i+1 are read)
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& st = chans[(size_t) ch];
            if (L == 1)
            {
                std::copy (os[ch], os[ch] + n, y[ch]);
                continue;
            }
            int l = len;
            float* buf = os[ch];
            for (int s = cfg.osLog2 - 1; s >= 0; --s)
            {
                l /= 2;
                float* dst = s == 0 ? y[ch] : buf;
                st.down[s].process (buf, dst, l);
                buf = dst;
            }
        }
    }

    // ================================================================== CeilingStage

    void CeilingStage::prepare (double sampleRate, int maxBlock, int numChannels)
    {
        channels = std::clamp (numChannels, 1, maxChannels);
        // hold radius: the reconstruction kernel's main region, 0.33 ms (16 samples at 48 kHz)
        hold = std::max (8, (int) std::lround (sampleRate / 3000.0));
        // gain ramp: 0.25 ms (12 samples at 48 kHz)
        boxSupport = std::max (4, (int) std::lround (0.25e-3 * sampleRate));
        alpha = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));
        for (auto& ch : chans)
        {
            ch.tp.prepare (sampleRate, maxBlock);
            ch.tpBuf.assign ((size_t) maxBlock, 0.0f);
            detectorLatency = ch.tp.latency();
            ch.spDelay.prepare (detectorLatency + 1);
            ch.spDelay.setDelay (detectorLatency);
            ch.smin.prepare (boxSupport + 2 * hold + 2);
            ch.smin.setWindow (boxSupport + 2 * hold);
            ch.box.prepare (boxSupport + 2);
            ch.box.configure (boxSupport, 2);
        }
        for (auto& ch : chans)
        {
            ch.sig.prepare (latency() + 1);
            ch.sig.setDelay (latency());
        }
        reset();
    }

    void CeilingStage::reset() noexcept
    {
        for (auto& ch : chans)
        {
            ch.tp.reset();
            ch.spDelay.reset();
            ch.sig.reset();
            ch.smin.reset();
            ch.box.reset (1.0f);
            ch.ef = 1.0;
        }
    }

    void CeilingStage::process (float* const* io, float* const* gain, int n, const EngineControls& c) noexcept
    {
        for (int ch = 0; ch < channels; ++ch)
            chans[(size_t) ch].tp.process (io[ch], chans[(size_t) ch].tpBuf.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float ceil = c.ceiling[i];
            const float w = c.tpMix[i];
            const float k = c.link[i];
            float g[maxChannels] {}, sd[maxChannels] {};
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& st = chans[(size_t) ch];
                const float x = io[ch][i];
                // detector: |x| of the interval's first sample, or its true peak (both A samples late)
                const float sp = st.spDelay.process (std::abs (x));
                const float p = sp + w * (st.tpBuf[(size_t) i] - sp);
                const float r = p > ceil ? ceil / p : 1.0f;
                const float h = st.smin.process (r);
                st.ef = h < st.ef ? (double) h : st.ef + alpha * ((double) h - st.ef);
                g[ch] = st.box.process ((float) st.ef);
                sd[ch] = st.sig.process (x);
            }
            if (channels == 2)
            {
                const float gm = std::min (g[0], g[1]);
                g[0] += k * (gm - g[0]);
                g[1] += k * (gm - g[1]);
            }
            for (int ch = 0; ch < channels; ++ch)
            {
                io[ch][i] = sd[ch] * g[ch];
                gain[ch][i] = g[ch];
            }
        }
    }

    // ================================================================== SteelEngine

    void SteelEngine::prepare (double sampleRate, int maxBlockSize, int numChannels, const SlotConfig& initial)
    {
        fs = sampleRate;
        channels = std::clamp (numChannels, 1, maxChannels);
        maxBlock = std::max (1, maxBlockSize);
        const int maxLa = lookaheadSamples (maxLookaheadMs, fs);
        for (auto& s : slots)
            s.prepare (fs, maxBlock, channels, maxLa);
        stageB.prepare (fs, maxBlock, channels);
        const int maxLatency = maxLa + OversamplingLatency::hostSamples (3) + stageB.latency() + 8;
        for (auto& r : xRing) r.prepare (maxLatency);
        for (auto& r : uRing) r.prepare (maxLatency);
        gRing.prepare (maxLatency);
        for (auto* r : { &dA, &dI, &wRing })
        {
            r->prepare (stageB.latency() + 1);
            r->setDelay (stageB.latency());
        }
        for (auto& r : telRing)
        {
            r.prepare (stageB.latency() + 1);
            r.setDelay (stageB.latency());
        }
        for (auto* v : { &yA, &yI, &tA, &tI, &gB })
            for (auto& b : *v)
                b.assign ((size_t) maxBlock, 0.0f);
        weight.assign ((size_t) maxBlock, 0.0f);
        fadeLen = std::max (1, (int) std::lround (fadeSeconds * fs));
        // raised-cosine weights: no slope discontinuity at either end of the crossfade
        fadeCurve.resize ((size_t) fadeLen + 1);
        for (int i = 0; i <= fadeLen; ++i)
            fadeCurve[(size_t) i] = (float) (0.5 - 0.5 * std::cos (3.14159265358979323846 * i / fadeLen));
        fadeCurve[(size_t) fadeLen] = 1.0f;
        reset (initial);
    }

    void SteelEngine::reset (const SlotConfig& cfg) noexcept
    {
        target = cfg;
        active = 0;
        incoming = -1;
        warm = fadePos = 0;
        slots[0].configure (cfg);
        stageB.reset();
        for (auto& r : xRing) r.reset();
        for (auto& r : uRing) r.reset();
        gRing.reset (1.0f);
        const float d = (float) (slots[0].latency() + stageB.latency());
        dA.reset (d);
        dI.reset (d);
        wRing.reset (0.0f);
        for (auto& r : telRing) r.reset (1.0f);
        slowDbSum = totalDbSum = 0.0;
    }

    void SteelEngine::startSwitch() noexcept
    {
        incoming = 1 - active;
        auto& in = slots[(size_t) incoming];
        in.configure (target);
        in.inheritEnvelope (slots[(size_t) active]);
        // its output is valid once its delay lines and filters hold real input
        warm = in.latency() + 2 * in.osLatency() + 64;
        fadePos = 0;
    }

    void SteelEngine::process (const float* const* x, const float* const* u, const float* inputGain, int n,
                               const EngineControls& c, const EngineOutputs& o) noexcept
    {
        if (incoming < 0 && target != slots[(size_t) active].config())
            startSwitch();

        float* ya[maxChannels] {}, *yi[maxChannels] {}, *ta[maxChannels] {}, *ti[maxChannels] {}, *gb[maxChannels] {};
        for (int ch = 0; ch < channels; ++ch)
        {
            ya[ch] = o.out[ch];
            yi[ch] = yI[(size_t) ch].data();
            ta[ch] = tA[(size_t) ch].data();
            ti[ch] = tI[(size_t) ch].data();
            gb[ch] = gB[(size_t) ch].data();
        }

        auto& act = slots[(size_t) active];
        act.process (u, ya, ta, n, c, 0);
        slowDbSum = act.takeSlowDb();
        totalDbSum = act.takeTotalDb();
        const float dAct = (float) (act.latency() + stageB.latency());
        float dInc = dAct;
        bool completes = false;
        if (incoming >= 0)
        {
            auto& inc = slots[(size_t) incoming];
            inc.process (u, yi, ti, n, c, 0);
            inc.takeSlowDb();
            inc.takeTotalDb();
            dInc = (float) (inc.latency() + stageB.latency());
            for (int i = 0; i < n; ++i)
            {
                float w = 0.0f;
                if (warm > 0)
                    --warm;
                else if (fadePos < fadeLen)
                    w = fadeCurve[(size_t) ++fadePos];
                else
                    w = 1.0f;
                weight[(size_t) i] = w;
            }
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float w = weight[(size_t) i];
                    ya[ch][i] += w * (yi[ch][i] - ya[ch][i]);
                    ta[ch][i] += w * (ti[ch][i] - ta[ch][i]);
                }
            completes = fadePos >= fadeLen;
        }
        else
        {
            std::fill (weight.begin(), weight.begin() + n, 0.0f);
        }

        stageB.process (ya, gb, n, c);

        for (int i = 0; i < n; ++i)
        {
            const float da = dA.process (dAct);
            const float di = dI.process (dInc);
            const float w = wRing.process (weight[(size_t) i]);
            const int ia = (int) da, ii = (int) di;
            for (int ch = 0; ch < channels; ++ch)
            {
                xRing[(size_t) ch].push (x[ch][i]);
                uRing[(size_t) ch].push (u[ch][i]);
                const float xa = xRing[(size_t) ch].read (ia), ua = uRing[(size_t) ch].read (ia);
                if (w <= 0.0f)
                {
                    o.dryAligned[ch][i] = xa;
                    o.refAligned[ch][i] = ua;
                }
                else
                {
                    const float xi = xRing[(size_t) ch].read (ii), ui = uRing[(size_t) ch].read (ii);
                    o.dryAligned[ch][i] = w >= 1.0f ? xi : xa + w * (xi - xa);
                    o.refAligned[ch][i] = w >= 1.0f ? ui : ua + w * (ui - ua);
                }
                o.gain[ch][i] = telRing[(size_t) ch].process (ta[ch][i]) * gb[ch][i];
            }
            gRing.push (inputGain[i]);
            const float ga = gRing.read (ia);
            o.inputGainAligned[i] = w <= 0.0f ? ga : (w >= 1.0f ? gRing.read (ii) : ga + w * (gRing.read (ii) - ga));
        }

        if (completes)
        {
            active = incoming;
            incoming = -1;
        }
    }
}
