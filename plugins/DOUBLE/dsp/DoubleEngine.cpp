#include "DoubleEngine.h"
#include <cmath>
#include <algorithm>

namespace tleet::dbl
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double ln2over1200 = 0.69314718055994530942 / 1200.0;

        // 2^(cents/1200) for |cents| <= ~60: 4th-order series, error < 1e-11 (< 2e-8 cent).
        inline double centsToRatio (double cents) noexcept
        {
            const double x = cents * ln2over1200;
            return 1.0 + x * (1.0 + x * (0.5 + x * (1.0 / 6.0 + x * (1.0 / 24.0))));
        }

        inline double onePole (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (seconds * fs)); }

        // dB -> gain for the tiny humanize level drift (|db| <= 1): series of exp(db ln10/20)
        inline float smallDbToGain (double db) noexcept
        {
            const double x = db * 0.11512925464970228;
            return (float) (1.0 + x * (1.0 + x * (0.5 + x * (1.0 / 6.0))));
        }

        template <typename T>
        inline void glideTo (T& value, T target, T coeff, T snap) noexcept
        {
            value += (target - value) * coeff;
            if (std::abs (target - value) < snap)
                value = target;
        }
    }

    void DoubleEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxBlock = std::max (1, maxBlockSize);
        channels = std::clamp (numChannels, 1, maxChannels);

        // Longest read: centre 60 ms + window/2 + drift + overshoot, plus a splice
        // match window and lag search beyond it. 120 ms covers it with margin.
        ring.prepare ((int) std::ceil (0.12 * fs) + 4 * controlInterval + 64);
        maxTapDelay = (double) ring.capacity() - (double) SincTable::taps - 4.0;

        feed.assign ((size_t) controlInterval, 0.0f);
        wetFilter.prepare (fs);

        spliceLen = std::max (1, (int) std::lround (spliceFadeMs * 0.001 * fs));
        corrWindow = std::max (8, (int) std::lround (correlationWindowMs * 0.001 * fs));
        coarseStep = std::max (1, (int) std::lround (fs / 12000.0));
        refineStride = std::max (1, (int) std::lround (fs / 48000.0));

        aDetune = onePole (detuneGlideMs * 0.001, fs);
        aDelay = onePole (delayGlideMs * 0.001, fs);
        aHumanize = onePole (0.05, fs);
        aPan = onePole (panGlideMs * 0.001, fs);
        levelMixStep = 1.0 / (gainGlideMs * 0.001 * fs);
        fadeStep = (float) (1.0 / (voiceFadeMs * 0.001 * fs));
        diffStep = 1.0 / (diffusionFadeMs * 0.001 * fs);
        apgStep = 1.0 / (diffusionFadeMs * 0.001 * fs);
        sideStep = 1.0 / (0.03 * fs);

        sideG = wetfilter::integratorGain (templates::monoSafeSideHpfHz, fs);
        sideA = 1.0 / (1.0 + sideG * (sideG + wetfilter::butterworthK));

        sliceLength = std::max (1, (int) std::lround (DoubleTelemetry::sliceSeconds * fs));

        for (int s = 0; s < maxVoices; ++s)
        {
            templateGain[(size_t) s] = std::pow (10.0, templates::slots[(size_t) s].gainDb / 20.0);
            auto& v = voices[(size_t) s];
            const auto seed = (std::uint32_t) (0x1234567u + 7919u * (std::uint32_t) s);
            v.pitchDrift.prepare (fs, 0.35, 0.90, 0.22, seed ^ 0xa5a5a5a5u);
            v.timeDrift.prepare (fs, 0.60, 1.50, 0.40, seed ^ 0x5a5a5a5au);
            v.levelDrift.prepare (fs, 0.30, 0.80, 0.20, seed ^ 0x3c3c3c3cu);
            for (int k = 0; k < 2; ++k)
            {
                auto& ap = v.diffusers[(size_t) k];
                ap.length = std::max (1, (int) std::lround (templates::slots[(size_t) s].diffuseMs[k] * 0.001 * fs));
                ap.buffer.assign ((size_t) ap.length, 0.0f);
                ap.pos = 0;
            }
        }

        reset();
    }

    void DoubleEngine::reset() noexcept
    {
        ring.reset();
        wetFilter.setTargets (settings.hpfHz, settings.lpfHz, wetfilter::shelfGain (settings.mode));
        wetFilter.reset();
        sampleCounter = 0;
        sliceCounter = 0;
        sideS1 = sideS2 = 0.0;
        driftMode = -1;

        const auto& mode = templates::modes[(size_t) templates::clampMode (settings.mode)];
        hs = hsTarget = settings.humanize * mode.humanizeDepth;
        diff = diffTarget = mode.diffusion > 0.0f ? 1.0 : 0.0;
        const bool stereoSafe = settings.monoSafe && channels == 2;
        apg = apgTarget = templates::modes[2].diffusion * (stereoSafe ? templates::monoSafeDiffusionShare : 1.0f);
        side = sideTarget = stereoSafe ? 1.0 : 0.0;

        for (auto& v : voices)
        {
            v.uA = v.uB = v.cA = v.cB = 0.0;
            v.splicing = false;
            v.xfPos = 0;
            v.rho = 1.0;
            v.detune = v.detuneTarget = v.cTarget = v.window = v.delta = 0.0;
            v.pan = v.panTarget = v.gL = v.gR = v.gLTarget = v.gRTarget = 0.0;
            v.gain = 0.0;
            v.fade = 0.0f;
            v.inLayout = false;
            v.humanGain = 1.0f;
            v.lastRatio = 1.0;
            v.lastDeltaStep = v.lastCStep = 0.0;
            v.slicePeak = 0.0f;
            v.splices = 0;
            v.pitchDrift.reset();
            v.timeDrift.reset();
            v.levelDrift.reset();
            for (auto& ap : v.diffusers)
            {
                std::fill (ap.buffer.begin(), ap.buffer.end(), 0.0f);
                ap.pos = 0;
            }
        }

        // Start settled on the current settings: voices in the layout are fully in.
        control (ring.position() - 1, false);
        for (auto& v : voices)
        {
            v.fade = v.inLayout ? 1.0f : 0.0f;
            v.detune = v.detuneTarget;
            v.cA = v.cB = v.cTarget;
            v.pan = v.panTarget;
            v.gL = v.gLTarget;
            v.gR = v.gRTarget;
        }
        control (ring.position() - 1, false); // window and floor with the settled state
        levelMix = levelMixTarget;
        normNow = levelNorm (0);
        for (int s = 0; s < maxVoices; ++s)
        {
            auto& v = voices[(size_t) s];
            v.gain = templateGain[(size_t) s] * normNow;
            v.cA = v.cB = v.cTarget;
        }
    }

    // ------------------------------------------------------------------ control

    void DoubleEngine::control (int nowIndex, bool allowSplices) noexcept
    {
        const int mode = templates::clampMode (settings.mode);
        const auto& mt = templates::modes[(size_t) mode];
        const int count = std::clamp (settings.voices, 1, maxVoices);
        const bool stereo = channels == 2;
        const bool stereoSafe = settings.monoSafe && stereo;

        wetFilter.setTargets (settings.hpfHz, settings.lpfHz, wetfilter::shelfGain (mode));
        wetFilter.control();

        if (mode != driftMode)
        {
            driftMode = mode;
            for (auto& v : voices)
            {
                v.pitchDrift.setRate (mt.humanizeRate);
                v.timeDrift.setRate (mt.humanizeRate);
                v.levelDrift.setRate (mt.humanizeRate);
            }
        }

        hsTarget = std::clamp (settings.humanize, 0.0f, 1.0f) * mt.humanizeDepth;
        diffTarget = mt.diffusion > 0.0f ? 1.0 : 0.0;
        apgTarget = templates::modes[2].diffusion * (stereoSafe ? templates::monoSafeDiffusionShare : 1.0f);
        sideTarget = stereoSafe ? 1.0 : 0.0;

        const double hsBound = std::max (hs, hsTarget);
        const double samplesPerMs = 0.001 * fs;
        const double pitchDepthBound = templates::humanizePitchCents * hsBound;
        const double deltaMax = templates::humanizeDelayMs * hsBound * samplesPerMs;

        levelMixTarget = settings.autoLevel ? 1.0 : 0.0;

        for (int s = 0; s < maxVoices; ++s)
        {
            auto& v = voices[(size_t) s];
            const bool wasIn = v.inLayout;
            v.inLayout = s < count;

            v.detuneTarget = templates::voiceDetuneCents (settings.detuneCents, mode, s);

            // micro-pitch window: full span from 1 cent of possible pitch offset, shrinking to 0 with it
            const double rateBound = std::max (std::abs (v.detuneTarget), std::abs (v.detune)) + pitchDepthBound;
            v.window = spliceSpanMs * samplesPerMs * std::min (1.0, rateBound / fullWindowCents);
            const double overshoot = (spliceLen + controlInterval) * (centsToRatio (rateBound) - 1.0);
            const double modulation = 0.5 * v.window + deltaMax;
            const double floor = modulation + overshoot + guardSamples * std::min (1.0, modulation / guardSamples);
            const double requested = templates::voiceDelayMs (settings.delayMs, mode, s) * samplesPerMs;
            v.cTarget = std::clamp (std::max (requested, floor), 0.0, maxCentreDelayMs * samplesPerMs);

            v.panTarget = stereo ? (double) templates::voicePan (settings.width, mode, s, count, stereoSafe) : 0.0;
            const double theta = (v.panTarget + 1.0) * (pi / 4.0);
            v.gLTarget = std::cos (theta);
            v.gRTarget = std::cos (pi / 2.0 - theta); // == gL exactly at the centre

            // A voice entering from silence starts settled on its targets (no glide, no splice).
            if (v.inLayout && ! wasIn && v.fade <= 0.0f)
            {
                v.detune = v.detuneTarget;
                v.cA = v.cB = v.cTarget;
                v.uA = v.uB = 0.0;
                v.splicing = false;
                v.pan = v.panTarget;
                v.gL = v.gLTarget;
                v.gR = v.gRTarget;
                for (auto& ap : v.diffusers)
                    std::fill (ap.buffer.begin(), ap.buffer.end(), 0.0f);
            }

            const bool audible = v.inLayout || v.fade > 0.0f;
            if (! allowSplices || ! audible || v.splicing)
                continue;

            // Splice triggers: ramp left its window, the window shrank, or the centre jumped.
            const double lo = -0.5 * v.window, hi = 0.5 * v.window;
            const double eps = 1.0e-9;
            const bool jump = std::abs (v.cTarget - v.cA) > jumpThresholdMs * samplesPerMs;
            if (v.uA < lo - eps)
                startSplice (v, nowIndex, 0.0, hi, jump ? v.cTarget : v.cA);
            else if (v.uA > hi + eps)
                startSplice (v, nowIndex, lo, 0.0, jump ? v.cTarget : v.cA);
            else if (jump)
            {
                const double span = std::min (jumpSearchMs * samplesPerMs, 0.5 * v.window);
                startSplice (v, nowIndex, std::max (lo, v.uA - span), std::min (hi, v.uA + span), v.cTarget);
            }
        }
    }

    // Auto Level norm `samplesAhead` samples from now (fades and the on/off ramp
    // are linear, so their future values are known). It keeps the summed voice
    // power constant: voices are decorrelated by their delays and detunes, so
    // their powers add. Stereo: a centred voice at sqrt(2) has the dry's level.
    double DoubleEngine::levelNorm (int samplesAhead) const noexcept
    {
        double power = 0.0;
        for (int s = 0; s < maxVoices; ++s)
        {
            const auto& v = voices[(size_t) s];
            const float step = fadeStep * (float) samplesAhead;
            const float fade = v.inLayout ? std::min (1.0f, v.fade + step) : std::max (0.0f, v.fade - step);
            const double shape = fade >= 1.0f ? 1.0 : 0.5 - 0.5 * std::cos (pi * fade);
            power += shape * shape * templateGain[(size_t) s] * templateGain[(size_t) s];
        }
        const double ramp = levelMixStep * samplesAhead;
        const double mix = levelMix < levelMixTarget ? std::min (levelMixTarget, levelMix + ramp)
                                                     : std::max (levelMixTarget, levelMix - ramp);
        constexpr double sqrt2 = 1.4142135623730951;
        return sqrt2 * ((1.0 - mix) + mix / std::sqrt (std::max (power, 1.0e-3)));
    }

    void DoubleEngine::startSplice (Voice& v, int nowIndex, double ra, double rb, double newCentre) noexcept
    {
        // Lags (samples) from tap A to the candidate B positions newCentre + u', u' in [ra, rb].
        const double tauA = v.cA + v.delta + v.uA;
        const double lagLo = (newCentre + ra) - (v.cA + v.uA);
        const double lagHi = (newCentre + rb) - (v.cA + v.uA);
        double rho = 1.0;
        const double lag = bestLag (nowIndex, tauA, lagLo, lagHi, rho);
        v.cB = newCentre;
        v.uB = v.uA + lag - (newCentre - v.cA);
        v.rho = std::clamp (rho, 0.0, 1.0);
        v.xfPos = 0;
        v.splicing = true;
        ++v.splices;
    }

    // Normalised cross-correlation search (WSOLA-style): the lag in [lagLo, lagHi]
    // at which the signal under the new tap best continues the old one.
    double DoubleEngine::bestLag (int now, double tauA, double lagLo, double lagHi, double& rhoOut) const noexcept
    {
        const int base = std::clamp ((int) std::lround (tauA), 0, (int) maxTapDelay);
        int lo = (int) std::ceil (lagLo), hi = (int) std::floor (lagHi);
        lo = std::max (lo, -base);                                  // candidate delay >= 0
        hi = std::min (hi, (int) maxTapDelay - base - corrWindow);  // stay inside the ring
        if (hi < lo)
        {
            rhoOut = 1.0;
            return 0.5 * (lagLo + lagHi);
        }

        // Window as late as possible ("upcoming" samples) without reading the future.
        const int o = std::clamp (std::min (base, base + lo), 0, corrWindow - 1);
        const float* a = ring.run (now, base - o, corrWindow);

        auto energy = [&] (const float* x, int stride)
        {
            double e = 0.0;
            for (int k = 0; k < corrWindow; k += stride)
                e += (double) x[k] * x[k];
            return e;
        };
        auto correlate = [&] (int lag, int stride, double ea)
        {
            const float* b = ring.run (now, base + lag - o, corrWindow);
            double ab = 0.0, bb = 0.0;
            for (int k = 0; k < corrWindow; k += stride)
            {
                ab += (double) a[k] * b[k];
                bb += (double) b[k] * b[k];
            }
            return ab / std::sqrt (ea * bb + 1.0e-30);
        };

        const double eaCoarse = energy (a, coarseStep);
        if (eaCoarse < 1.0e-14 * corrWindow / coarseStep || lagHi - lagLo < 1.0)
        {
            const double mid = 0.5 * (lagLo + lagHi);
            const double eaFine = energy (a, refineStride);
            rhoOut = eaFine > 1.0e-14 ? correlate (std::clamp ((int) std::lround (mid), lo, hi), refineStride, eaFine) : 1.0;
            return mid;
        }

        // Coarse grid (~12 kHz). Keep the three best separated peaks: on a grid a
        // true peak can sit between two points and tie with an edge point.
        constexpr int keep = 3;
        int peakLag[keep] { lo, lo, lo };
        double peakVal[keep] { -2.0, -2.0, -2.0 };
        for (int lag = lo; lag <= hi; lag += coarseStep)
        {
            const double r = correlate (lag, coarseStep, eaCoarse);
            int slot = -1;
            for (int k = 0; k < keep; ++k)
                if (peakVal[k] > -2.0 && std::abs (peakLag[k] - lag) <= 2 * coarseStep)
                    slot = k;                       // same neighbourhood as a kept peak
            if (slot >= 0)
            {
                if (r > peakVal[slot]) { peakVal[slot] = r; peakLag[slot] = lag; }
                continue;
            }
            int worst = 0;
            for (int k = 1; k < keep; ++k)
                if (peakVal[k] < peakVal[worst]) worst = k;
            if (r > peakVal[worst]) { peakVal[worst] = r; peakLag[worst] = lag; }
        }

        // Each kept peak refined +-1 coarse step at full resolution; then a parabola.
        const double eaFine = energy (a, refineStride);
        int bestFine = peakLag[0];
        double best = -2.0;
        for (int k = 0; k < keep; ++k)
        {
            if (peakVal[k] <= -2.0)
                continue;
            const int rLo = std::max (lo, peakLag[k] - coarseStep), rHi = std::min (hi, peakLag[k] + coarseStep);
            for (int lag = rLo; lag <= rHi; ++lag)
            {
                const double r = correlate (lag, refineStride, eaFine);
                if (r > best) { best = r; bestFine = lag; }
            }
        }

        double offset = 0.0;
        if (bestFine > lo && bestFine < hi)
        {
            const double rm = correlate (bestFine - 1, refineStride, eaFine);
            const double rp = correlate (bestFine + 1, refineStride, eaFine);
            const double den = rm - 2.0 * best + rp;
            if (den < -1.0e-12)
                offset = std::clamp (0.5 * (rm - rp) / den, -0.5, 0.5);
        }
        rhoOut = best;
        return std::clamp ((double) bestFine + offset, lagLo, lagHi);
    }

    // ------------------------------------------------------------------ audio

    void DoubleEngine::process (const float* const* in, float* const* wet, int numChannels, int numSamples) noexcept
    {
        const int nch = std::min (numChannels, channels);
        int pos = 0;
        while (pos < numSamples)
        {
            const int phase = (int) (sampleCounter % controlInterval);
            const int n = std::min ({ numSamples - pos, controlInterval - phase, sliceLength - sliceCounter });

            const int nowStart = ring.position(); // ring index the first sample of this run will get
            if (phase == 0)
                control (nowStart - 1, true); // newest sample already in the ring

            // mono voice feed -> wet filters -> ring
            for (int i = 0; i < n; ++i)
                feed[(size_t) i] = nch == 2 ? 0.5f * (in[0][pos + i] + in[1][pos + i]) : in[0][pos + i];
            wetFilter.process (feed.data(), feed.data(), n);
            ring.write (feed.data(), n);

            // shared per-sample smoothing for this run
            for (int i = 0; i < n; ++i)
            {
                hs += (hsTarget - hs) * aHumanize;
                diff = diff < diffTarget ? std::min (diffTarget, diff + diffStep) : std::max (diffTarget, diff - diffStep);
                apg = apg < apgTarget ? std::min (apgTarget, apg + apgStep) : std::max (apgTarget, apg - apgStep);
                side = side < sideTarget ? std::min (sideTarget, side + sideStep) : std::max (sideTarget, side - sideStep);
                hsBuf[(size_t) i] = (float) hs;
                diffBuf[(size_t) i] = (float) diff;
                apgBuf[(size_t) i] = (float) apg;
                sideBuf[(size_t) i] = (float) side;
            }

            // Auto Level norm across this run: exact at its end, linear inside (n <= 32)
            {
                const double normEnd = levelNorm (n);
                for (int i = 0; i < n; ++i)
                    normBuf[(size_t) i] = normNow + (normEnd - normNow) * (double) (i + 1) / (double) n;
                normNow = normEnd;
                levelMix = levelMix < levelMixTarget ? std::min (levelMixTarget, levelMix + levelMixStep * n)
                                                     : std::max (levelMixTarget, levelMix - levelMixStep * n);
            }

            float* outL = wet[0] + pos;
            float* outR = nch == 2 ? wet[1] + pos : nullptr;
            std::fill (outL, outL + n, 0.0f);
            if (outR != nullptr)
                std::fill (outR, outR + n, 0.0f);

            for (int s = 0; s < maxVoices; ++s)
                renderVoice (s, nowStart, n, outL, outR);

            // Mono Safe: high-pass the wet side signal (the mono sum is untouched)
            if (outR != nullptr)
            {
                const bool active = side > 0.0 || sideTarget > 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double m = 0.5 * ((double) outL[i] + outR[i]);
                    const double sd = 0.5 * ((double) outL[i] - outR[i]);
                    const double hp = (sd - (sideG + wetfilter::butterworthK) * sideS1 - sideS2) * sideA;
                    const double v1 = sideG * hp;
                    const double bp = v1 + sideS1;
                    sideS1 = bp + v1;
                    sideS2 += 2.0 * sideG * bp;
                    if (active)
                    {
                        const double sOut = sd + (double) sideBuf[(size_t) i] * (hp - sd);
                        outL[i] = (float) (m + sOut);
                        outR[i] = (float) (m - sOut);
                    }
                }
            }

            pos += n;
            sampleCounter += n;
            sliceCounter += n;
            if (sliceCounter >= sliceLength)
            {
                sliceCounter = 0;
                float peaks[maxVoices] {};
                for (int s = 0; s < maxVoices; ++s)
                {
                    auto& v = voices[(size_t) s];
                    peaks[s] = v.inLayout ? v.slicePeak : 0.0f;
                    v.slicePeak = 0.0f;
                }
                if (telemetry != nullptr)
                    telemetry->pushSlice (peaks);
            }
        }

        publishTelemetry();
    }

    void DoubleEngine::renderVoice (int s, int nowStart, int n, float* outL, float* outR) noexcept
    {
        auto& v = voices[(size_t) s];
        if (! v.inLayout && v.fade <= 0.0f)
            return;

        const double samplesPerMs = 0.001 * fs;
        const double pitchDepth = templates::humanizePitchCents;
        const double delayDepth = templates::humanizeDelayMs * samplesPerMs;
        const double gainDepth = templates::humanizeGainDb;
        const float fadeTarget = v.inLayout ? 1.0f : 0.0f;
        const bool toBus = solo < 0 || solo == s;

        for (int i = 0; i < n; ++i)
        {
            const int now = ring.indexFrom (nowStart, i);
            const double h = hsBuf[(size_t) i];

            // humanize drifts, each in [-1, 1]
            const double dp = v.pitchDrift.next(), dt = v.timeDrift.next(), dg = v.levelDrift.next();

            v.detune += (v.detuneTarget - v.detune) * aDetune;
            const double ratio = centsToRatio (v.detune + dp * pitchDepth * h);
            const double step = 1.0 - ratio;
            v.uA += step;
            if (v.splicing)
                v.uB += step;

            const double delta = dt * delayDepth * h;
            v.lastDeltaStep = delta - v.delta;
            v.delta = delta;
            v.lastRatio = ratio;

            if (! v.splicing)
            {
                const double before = v.cA;
                v.cA += (v.cTarget - v.cA) * aDelay;
                if (std::abs (v.cTarget - v.cA) < 1.0e-9)
                    v.cA = v.cTarget;
                v.lastCStep = v.cA - before;
            }
            else
                v.lastCStep = 0.0;

            const double tauA = std::clamp (v.cA + delta + v.uA, 0.0, maxTapDelay);
            float y = ring.read (now, tauA);

            if (v.splicing)
            {
                const double tauB = std::clamp (v.cB + delta + v.uB, 0.0, maxTapDelay);
                const float yB = ring.read (now, tauB);
                // raised-cosine crossfade, normalised for the measured correlation:
                // a^2 + b^2 + 2 rho a b = 1 (linear for rho = 1, equal power for rho = 0)
                const double t = 0.5 - 0.5 * std::cos (pi * (v.xfPos + 0.5) / spliceLen);
                const double a = 1.0 - t, b = t;
                const double norm = 1.0 / std::sqrt (a * a + b * b + 2.0 * v.rho * a * b);
                y = (float) ((a * y + b * yB) * norm);
                if (++v.xfPos >= spliceLen)
                {
                    v.splicing = false;
                    v.uA = v.uB;
                    v.cA = v.cB;
                }
            }

            // GHOST diffusion: two Schroeder all-passes (flat magnitude), crossfaded in/out.
            // They run in every mode so their state is always current: entering GHOST
            // fades in a live diffuser instead of a stale buffer (no click).
            {
                const float g = apgBuf[(size_t) i];
                float x = y;
                for (auto& ap : v.diffusers)
                {
                    const float delayed = ap.buffer[(size_t) ap.pos];
                    const float w = x + g * delayed;
                    ap.buffer[(size_t) ap.pos] = w;
                    if (++ap.pos >= ap.length)
                        ap.pos = 0;
                    x = delayed - g * w;
                }
                const float d = diffBuf[(size_t) i];
                if (d > 0.0f)
                    y += d * (x - y);
            }

            // level: template x Auto Level, humanize drift, voice-count fade
            v.gain = templateGain[(size_t) s] * normBuf[(size_t) i];
            if (v.fade < fadeTarget || v.fade > fadeTarget)
                v.fade = fadeTarget > v.fade ? std::min (fadeTarget, v.fade + fadeStep) : std::max (fadeTarget, v.fade - fadeStep);
            const float fadeShape = v.fade >= 1.0f ? 1.0f : (v.fade <= 0.0f ? 0.0f : 0.5f - 0.5f * std::cos ((float) pi * v.fade));
            v.humanGain = smallDbToGain (dg * gainDepth * h);
            y *= (float) v.gain * fadeShape * v.humanGain;

            v.slicePeak = std::max (v.slicePeak, std::abs (y));

            glideTo (v.gL, v.gLTarget, aPan, 1.0e-9);
            glideTo (v.gR, v.gRTarget, aPan, 1.0e-9);
            glideTo (v.pan, v.panTarget, aPan, 1.0e-9);
            if (toBus)
            {
                outL[i] += (float) v.gL * y;
                if (outR != nullptr)
                    outR[i] += (float) v.gR * y;
            }
        }

        if (! v.inLayout && v.fade <= 0.0f)
        {
            v.splicing = false;
            v.slicePeak = 0.0f;
        }
    }

    // ------------------------------------------------------------------ telemetry

    DoubleEngine::VoiceInfo DoubleEngine::getVoiceInfo (int s) const noexcept
    {
        VoiceInfo info;
        if (s < 0 || s >= maxVoices)
            return info;
        const auto& v = voices[(size_t) s];
        const double toMs = 1000.0 / fs;
        const bool late = v.splicing && v.xfPos * 2 >= spliceLen; // B dominates the second half of a splice
        const double c = late ? v.cB : v.cA;
        const double u = late ? v.uB : v.uA;
        info.active = v.inLayout;
        info.fade = v.fade;
        info.pan = channels == 2 ? (float) v.pan : 0.0f;
        info.delayMs = (float) ((c + v.delta) * toMs);
        info.tapDelayMs = (float) ((c + v.delta + u) * toMs);
        // playback ratio of the tap = 1 - d(delay)/dn
        const double eff = std::max (1.0e-6, v.lastRatio - v.lastDeltaStep - v.lastCStep);
        info.detuneCents = (float) (1200.0 * std::log2 (eff));
        const float fadeShape = v.fade >= 1.0f ? 1.0f : 0.5f - 0.5f * std::cos ((float) pi * v.fade);
        info.gain = (float) v.gain * fadeShape * v.humanGain * 0.70710678f;
        info.splicing = v.splicing;
        info.splices = v.splices;
        info.windowMs = (float) (v.window * toMs);
        return info;
    }

    void DoubleEngine::publishTelemetry() noexcept
    {
        if (telemetry == nullptr)
            return;
        for (int s = 0; s < maxVoices; ++s)
        {
            const auto info = getVoiceInfo (s);
            DoubleTelemetry::Voice t;
            t.active = info.active;
            t.pan = info.pan;
            t.delayMs = info.delayMs;
            t.detuneCents = info.detuneCents;
            t.gain = info.active ? info.gain : 0.0f;
            telemetry->publishVoice (s, t);
        }
    }
}
