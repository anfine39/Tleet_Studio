#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "STEELPresets.h"

namespace tleet::steel
{
    namespace
    {
        constexpr double gainRampSeconds = 0.02;   // INPUT, OUTPUT, CEILING, LINK, AMOUNT
        constexpr double gainCornerSeconds = 0.003; // rounds the corners of the gain ramps
        constexpr double switchRampSeconds = 0.02; // Bypass, DELTA, SOFT CLIP, TRUE PEAK
        constexpr double subFadeSeconds = 0.05;    // SUB FILTER on/off
        constexpr double subGlideSeconds = 0.015;  // SUB FILTER 20/25/30 glide (log domain)
        constexpr float inputClamp = 1.0e4f;       // +80 dBFS: keeps absurd host values finite through INPUT

        float dbOrFloor (float lin) noexcept { return lin > 0.0f ? juce::jmax (-120.0f, 20.0f * std::log10 (lin)) : -120.0f; }
    }

    SteelProcessor::SteelProcessor() : SteelProcessor (std::make_shared<params::TextContext>()) {}

    SteelProcessor::SteelProcessor (std::shared_ptr<params::TextContext> ctx)
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout (ctx), "STEEL"),
          textContext (std::move (ctx))
    {
        auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        p.input          = raw (params::input);
        p.ceiling        = raw (params::ceiling);
        p.mode           = raw (params::mode);
        p.releaseMode    = raw (params::releaseMode);
        p.release        = raw (params::release);
        p.lookahead      = raw (params::lookahead);
        p.truePeak       = raw (params::truePeak);
        p.oversampling   = raw (params::oversampling);
        p.stereoLink     = raw (params::stereoLink);
        p.softClip       = raw (params::softClip);
        p.softClipAmount = raw (params::softClipAmount);
        p.subFilter      = raw (params::subFilter);
        p.delta          = raw (params::delta);
        p.levelMatch     = raw (params::levelMatch);
        p.output         = raw (params::output);
        p.bypass         = raw (params::bypass);
        textContext->truePeak.store (p.truePeak, std::memory_order_release);

        juce::StringArray factory;
        for (int i = 0; i < STEELPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = STEELPresets::getNamedResource (STEELPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    SteelProcessor::~SteelProcessor()
    {
        textContext->truePeak.store (nullptr, std::memory_order_release);
    }

    bool SteelProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    SlotConfig SteelProcessor::currentSlotConfig() const noexcept
    {
        SlotConfig c;
        c.mode = juce::jlimit (0, numModes - 1, juce::roundToInt (p.mode->load()));
        c.osLog2 = juce::jlimit (0, 3, juce::roundToInt (p.oversampling->load()));
        c.lookaheadHost = SteelEngine::lookaheadSamples (p.lookahead->load(), sampleRate);
        return c;
    }

    void SteelProcessor::prepareToPlay (double fs, int samplesPerBlock)
    {
        sampleRate = fs;
        preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        engine.prepare (fs, maxBlock, preparedChannels, currentSlotConfig());
        setLatencySamples (engine.latency());

        for (auto& s : sub)
        {
            s.prepare (fs);
            s.setCutoff (20.0);
        }
        const int subIdx = juce::jlimit (0, 3, juce::roundToInt (p.subFilter->load()));
        subLogTarget = subLogCutoff = std::log (subIdx > 0 ? (double) params::subFilterHz[subIdx] : 20.0);
        for (auto& s : sub)
            s.setCutoff (std::exp (subLogCutoff));
        subGlide = 1.0 - std::exp (-1.0 / (subGlideSeconds * fs));

        for (auto& t : inTp) t.prepare (fs, maxBlock);
        for (auto& t : outTp) t.prepare (fs, maxBlock);
        loudness.prepare (fs, preparedChannels);
        levelMatcher.prepare (fs, preparedChannels);
        outputSpectrum.setSampleRate (fs);

        for (auto* v : { &xBuf, &uBuf, &yBuf, &dryBuf, &refBuf, &gainBuf, &tpBuf, &outPre })
            for (auto& b : *v)
                b.assign ((size_t) maxBlock, 0.0f);
        for (auto* v : { &gBuf, &gAlBuf, &ceilBuf, &clipMixBuf, &clipAmtBuf, &linkBuf, &tpMixBuf })
            v->assign ((size_t) maxBlock, 0.0f);

        inputSmoothed.prepare (fs, gainRampSeconds, gainCornerSeconds, juce::Decibels::decibelsToGain (p.input->load()));
        outputSmoothed.prepare (fs, gainRampSeconds, gainCornerSeconds, juce::Decibels::decibelsToGain (p.output->load()));
        ceilingSmoothed.prepare (fs, gainRampSeconds, gainCornerSeconds, juce::Decibels::decibelsToGain (p.ceiling->load()));
        lmSmoothed.prepare (fs, 0.1, gainCornerSeconds, 1.0f);
        linkSmoothed.reset (fs, gainRampSeconds);
        linkSmoothed.setCurrentAndTargetValue (p.stereoLink->load() * 0.01f);
        clipAmountSmoothed.reset (fs, gainRampSeconds);
        clipAmountSmoothed.setCurrentAndTargetValue (p.softClipAmount->load() * 0.01f);
        for (auto* r : { &bypassRamp, &deltaRamp, &clipRamp, &tpRamp })
            r->prepare (fs, switchRampSeconds);
        subRamp.prepare (fs, subFadeSeconds);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        deltaRamp.snap (p.delta->load() > 0.5f ? 1.0f : 0.0f);
        clipRamp.snap (p.softClip->load() > 0.5f ? 1.0f : 0.0f);
        tpRamp.snap (p.truePeak->load() > 0.5f ? 1.0f : 0.0f);
        subRamp.snap (subIdx > 0 ? 1.0f : 0.0f);

        sliceLength = juce::jmax (1, (int) std::lround (historySliceSeconds * fs));
        sliceFill = 0;
        slicePeak = 0.0f;
        samplesSinceOver = std::numeric_limits<int32_t>::max();
        sliceMinGain = 1.0f;
        sliceMinGainCh = { 1.0f, 1.0f };
        sliceSquares = sliceGrDbSum = 0.0;
        heavyRing.assign ((size_t) std::lround (heavyLimitingWindowSeconds / historySliceSeconds), 0.0);
        heavyPos = 0;
        heavySum = 0.0;
        sliceIndex = 0;
        tpOver = false;
        heavy = false;
        slowDbAvg = totalDbAvg = slowShare = 0.0;
    }

    void SteelProcessor::releaseResources() {}

    void SteelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);
        if (total == 0 || numChannels == 0 || preparedChannels == 0)
            return;

        // structural settings (MODE, OVERSAMPLING, LOOKAHEAD) go to the engine, which
        // crossfades to a new slot; the host learns the new latency now
        engine.setConfig (currentSlotConfig());
        if (engine.latency() != getLatencySamples())
            setLatencySamples (engine.latency());

        inputSmoothed.setTarget (juce::Decibels::decibelsToGain (p.input->load()));
        outputSmoothed.setTarget (juce::Decibels::decibelsToGain (p.output->load()));
        ceilingSmoothed.setTarget (juce::Decibels::decibelsToGain (p.ceiling->load()));
        linkSmoothed.setTargetValue (p.stereoLink->load() * 0.01f);
        clipAmountSmoothed.setTargetValue (p.softClipAmount->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        deltaRamp.setTarget (p.delta->load() > 0.5f ? 1.0f : 0.0f);
        clipRamp.setTarget (p.softClip->load() > 0.5f ? 1.0f : 0.0f);
        tpRamp.setTarget (p.truePeak->load() > 0.5f ? 1.0f : 0.0f);
        const int subIdx = juce::jlimit (0, 3, juce::roundToInt (p.subFilter->load()));
        subRamp.setTarget (subIdx > 0 ? 1.0f : 0.0f);
        if (subIdx > 0)
            subLogTarget = std::log ((double) params::subFilterHz[subIdx]);

        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numChannels);

        // effective release for the tooltip
        const bool autoRel = juce::roundToInt (p.releaseMode->load()) == (int) params::ReleaseMode::autoRelease;
        if (autoRel)
        {
            const auto& spec = modeSpec (engine.activeConfig().mode);
            effectiveRelease.store (std::pow ((double) spec.autoFastMs, 1.0 - slowShare)
                                        * std::pow ((double) spec.autoSlowReleaseMs, slowShare),
                                    std::memory_order_relaxed);
        }
        else
        {
            slowDbAvg = totalDbAvg = slowShare = 0.0;
            effectiveRelease.store (p.release->load(), std::memory_order_relaxed);
        }
    }

    void SteelProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, int numChannels)
    {
        float* io[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch] = buffer.getWritePointer (ch, start);

        float* x[2] {}, *u[2] {}, *y[2] {}, *dry[2] {}, *ref[2] {}, *gain[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            x[ch] = xBuf[(size_t) ch].data();
            u[ch] = uBuf[(size_t) ch].data();
            y[ch] = yBuf[(size_t) ch].data();
            dry[ch] = dryBuf[(size_t) ch].data();
            ref[ch] = refBuf[(size_t) ch].data();
            gain[ch] = gainBuf[(size_t) ch].data();
        }

        // ---- INPUT (dB-linear 20 ms ramp); x keeps the sanitised input for Bypass
        for (int i = 0; i < n; ++i)
            gBuf[(size_t) i] = inputSmoothed.next();
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float s = dsp::sanitize (io[ch][i]);
                x[ch][i] = s;
                u[ch][i] = juce::jlimit (-inputClamp, inputClamp, s) * gBuf[(size_t) i];
            }

        // ---- IN meters and input true peak: after INPUT, before SUB FILTER
        {
            float inSum = 0.0f, inPk = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float sq = 0.0f, pk = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    sq += u[ch][i] * u[ch][i];
                    pk = juce::jmax (pk, std::abs (u[ch][i]));
                }
                inputMeters[(size_t) ch].pushLevel (sq, n, pk);
                inSum += sq;
                inPk = juce::jmax (inPk, pk);
                inTp[(size_t) ch].process (u[ch], tpBuf[(size_t) ch].data(), n);
                float tpk = 0.0f;
                for (int i = 0; i < n; ++i)
                    tpk = juce::jmax (tpk, tpBuf[(size_t) ch][(size_t) i]);
                inTpHold[(size_t) ch].push (tpk);
                if (numChannels == 1)
                {
                    inputMeters[1].pushLevel (sq, n, pk);
                    inTpHold[1].push (tpk);
                }
            }
            inputMeter.pushLevel (inSum / (float) numChannels, n, inPk);
        }

        // ---- SUB FILTER: 24 dB/oct Butterworth HP, log glide between 20/25/30 Hz, 50 ms on/off fade
        if (subRamp.value > 0.0f || subRamp.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                if (std::abs (subLogTarget - subLogCutoff) > 1.0e-7)
                {
                    subLogCutoff += subGlide * (subLogTarget - subLogCutoff);
                    for (int ch = 0; ch < numChannels; ++ch)
                        sub[(size_t) ch].setCutoff (std::exp (subLogCutoff));
                }
                const float w = subRamp.next();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float hp = (float) sub[(size_t) ch].process (u[ch][i]);
                    u[ch][i] = w >= 1.0f ? hp : u[ch][i] + w * (hp - u[ch][i]);
                }
            }
        }
        else
        {
            // off: exact identity; the filter keeps running so it is warm when switched on
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < n; ++i)
                    sub[(size_t) ch].process (u[ch][i]);
        }

        // ---- engine controls
        for (int i = 0; i < n; ++i)
        {
            ceilBuf[(size_t) i] = ceilingSmoothed.next();
            clipMixBuf[(size_t) i] = clipRamp.next();
            clipAmtBuf[(size_t) i] = clipAmountSmoothed.getNextValue();
            linkBuf[(size_t) i] = linkSmoothed.getNextValue();
            tpMixBuf[(size_t) i] = tpRamp.next();
        }
        EngineControls c;
        c.ceiling = ceilBuf.data();
        c.clipMix = clipMixBuf.data();
        c.clipAmount = clipAmtBuf.data();
        c.link = linkBuf.data();
        c.tpMix = tpMixBuf.data();
        c.autoRelease = juce::roundToInt (p.releaseMode->load()) == (int) params::ReleaseMode::autoRelease;
        c.manualReleaseMs = p.release->load();

        EngineOutputs o;
        o.out = y;
        o.dryAligned = dry;
        o.refAligned = ref;
        o.inputGainAligned = gAlBuf.data();
        o.gain = gain;
        engine.process (x, u, gBuf.data(), n, c, o);
        if (c.autoRelease)
        {
            // effective AUTO release: GR-weighted share of the slow branch, both sums averaged over ~0.5 s
            const double a = 1.0 - std::exp (-(double) n / (0.5 * sampleRate));
            slowDbAvg += a * (engine.lastSlowDb() / n - slowDbAvg);
            totalDbAvg += a * (engine.lastTotalDb() / n - totalDbAvg);
            if (totalDbAvg > 0.01)
                slowShare = std::clamp (slowDbAvg / totalDbAvg, 0.0, 1.0);
        }

        // ---- OUTPUT, LEVEL MATCH, DELTA, Bypass
        const bool lmOn = p.levelMatch->load() > 0.5f;
        float outSq[2] {}, outPk[2] {};
        for (int i = 0; i < n; ++i)
        {
            const float og = outputSmoothed.next();
            float pre[2] {}, xin[2] {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                pre[ch] = y[ch][i] * og;
                xin[ch] = x[ch][i];
            }
            levelMatcher.push (xin, pre);
            const float lmTarget = lmOn ? juce::Decibels::decibelsToGain (levelMatcher.getTargetDb()) : 1.0f;
            if (std::abs (lmTarget - lmSmoothed.getTarget()) > 1.0e-6f * lmTarget)
                lmSmoothed.setTarget (lmTarget);
            const float lm = lmSmoothed.next();
            const float dw = deltaRamp.next();
            const float byp = bypassRamp.next();
            const float gAl = gAlBuf[(size_t) i];

            float pk = 0.0f, sq = 0.0f;
            float minGain = 1.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float v = pre[ch] * lm;
                if (dw > 0.0f)
                {
                    // DELTA: what the clipper, limiter and ceiling stage removed, referred to
                    // the plugin input level: (u - y) / INPUT gain (MODEL_SPEC 7.1)
                    const float d = (ref[ch][i] - y[ch][i]) / gAl;
                    v = dw >= 1.0f ? d : v + dw * (d - v);
                }
                const float drySample = dry[ch][i];
                if (byp >= 1.0f)
                    v = drySample; // fully bypassed = bit-exact delayed dry
                else if (byp > 0.0f)
                    v += (drySample - v) * byp;
                io[ch][i] = v;
                outPre[(size_t) ch][(size_t) i] = v;
                outSq[ch] += v * v;
                outPk[ch] = juce::jmax (outPk[ch], std::abs (v));
                pk = juce::jmax (pk, std::abs (v));
                sq += v * v;
                const float g = gain[ch][i];
                minGain = juce::jmin (minGain, g);
                sliceMinGainCh[(size_t) ch] = juce::jmin (sliceMinGainCh[(size_t) ch], g);
            }
            if (gainCapture != nullptr && gainCapture->size() < gainCapture->capacity())
                gainCapture->push_back (minGain);

            // history slice accumulation
            slicePeak = juce::jmax (slicePeak, pk);
            sliceSquares += (double) sq / numChannels;
            sliceMinGain = juce::jmin (sliceMinGain, minGain);
            sliceGrDbSum += minGain < 1.0f ? 20.0 * std::log10 (juce::jmax (minGain, 1.0e-9f)) : 0.0;
            grHold.push (minGain < 1.0f ? -20.0f * std::log10 (juce::jmax (minGain, 1.0e-9f)) : 0.0f);
            if (++sliceFill >= sliceLength)
                endOfSlice();
        }
        lmGainDb.store (juce::Decibels::gainToDecibels (lmSmoothed.getValue(), -120.0f), std::memory_order_relaxed);

        // ---- OUT meters, output true peak, loudness, spectrum
        const float* outPtr[2] {};
        const float* tpPtr[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            outPtr[ch] = outPre[(size_t) ch].data();
            outputMeters[(size_t) ch].pushLevel (outSq[ch], n, outPk[ch]);
            outTp[(size_t) ch].process (outPtr[ch], tpBuf[(size_t) ch].data(), n);
            tpPtr[ch] = tpBuf[(size_t) ch].data();
            float tpk = 0.0f;
            for (int i = 0; i < n; ++i)
                tpk = juce::jmax (tpk, tpPtr[ch][i]);
            outTpHold[(size_t) ch].push (tpk);
            if (numChannels == 1)
            {
                outputMeters[1].pushLevel (outSq[0], n, outPk[0]);
                outTpHold[1].push (tpk);
            }
        }
        outputMeter.pushLevel ((outSq[0] + outSq[1]) / (float) numChannels, n, juce::jmax (outPk[0], outPk[1]));
        loudness.process (outPtr, tpPtr, n);
        outputSpectrum.push (outPtr[0], numChannels > 1 ? outPtr[1] : nullptr, n);

        // TP OVERS: samples since the last output true peak above CEILING + 0.1 dB
        // (the ceiling in force at that sample); active for 2 s after it
        {
            const float tol = juce::Decibels::decibelsToGain (tpOverToleranceDb);
            int last = -1;
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = n - 1; i > last; --i)
                    if (tpPtr[ch][i] > ceilBuf[(size_t) i] * tol)
                    {
                        last = i;
                        break;
                    }
            samplesSinceOver = last >= 0 ? (int64_t) (n - 1 - last) : samplesSinceOver + n;
            tpOver.store (samplesSinceOver < (int64_t) std::lround (tpOverHoldSeconds * sampleRate), std::memory_order_relaxed);
        }
    }

    void SteelProcessor::endOfSlice()
    {
        const float rms = (float) std::sqrt (sliceSquares / (double) sliceFill);
        const float grDb = sliceMinGain < 1.0f ? 20.0f * std::log10 (juce::jmax (sliceMinGain, 1.0e-9f)) : 0.0f;
        history.push ({ dbOrFloor (slicePeak), dbOrFloor (rms), grDb });
        for (int ch = 0; ch < 2; ++ch)
        {
            const float g = sliceMinGainCh[(size_t) (ch < preparedChannels ? ch : 0)];
            currentGr[(size_t) ch].store (g < 1.0f ? 20.0f * std::log10 (juce::jmax (g, 1.0e-9f)) : 0.0f, std::memory_order_relaxed);
        }

        // HEAVY LIMITING: mean applied GR (dB, per sample) over the last 2 s
        const double meanGr = sliceGrDbSum / (double) sliceFill;
        heavySum += meanGr - heavyRing[(size_t) heavyPos];
        heavyRing[(size_t) heavyPos] = meanGr;
        heavyPos = (heavyPos + 1) % (int) heavyRing.size();
        if (heavyPos == 0)
        {
            heavySum = 0.0; // exact re-sum once per window: no drift
            for (auto v : heavyRing)
                heavySum += v;
        }
        heavy.store (heavySum / (double) heavyRing.size() <= -(double) heavyLimitingDb, std::memory_order_relaxed);

        ++sliceIndex;
        sliceFill = 0;
        slicePeak = 0.0f;
        sliceSquares = 0.0;
        sliceGrDbSum = 0.0;
        sliceMinGain = 1.0f;
        sliceMinGainCh = { 1.0f, 1.0f };
    }

    float SteelProcessor::takeOutputTruePeakDb (int channel) noexcept
    {
        return dbOrFloor (outTpHold[(size_t) juce::jlimit (0, 1, channel)].take());
    }

    float SteelProcessor::takeInputTruePeakDb (int channel) noexcept
    {
        return dbOrFloor (inTpHold[(size_t) juce::jlimit (0, 1, channel)].take());
    }

    float SteelProcessor::getCurrentGainReductionDb() const noexcept
    {
        return juce::jmin (currentGr[0].load (std::memory_order_relaxed), currentGr[1].load (std::memory_order_relaxed));
    }

    float SteelProcessor::getCurrentGainReductionDb (int channel) const noexcept
    {
        return currentGr[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    float SteelProcessor::takeGainReductionDb() noexcept
    {
        return -grHold.take();
    }

    juce::AudioProcessorEditor* SteelProcessor::createEditor()
    {
        return new SteelEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::steel::SteelProcessor();
}
#endif
