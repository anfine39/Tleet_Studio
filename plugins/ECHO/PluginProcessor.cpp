#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ECHOPresets.h"

namespace tleet::echo
{
    EchoProcessor::EchoProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "ECHO")
    {
        auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        p.mode          = raw (params::mode);
        p.repeatRate    = raw (params::repeatRate);
        p.intensity     = raw (params::intensity);
        p.echoVolume    = raw (params::echoVolume);
        p.reverbVolume  = raw (params::reverbVolume);
        p.bass          = raw (params::bass);
        p.treble        = raw (params::treble);
        p.timeMode      = raw (params::timeMode);
        p.bpmSource     = raw (params::bpmSource);
        p.manualBpm     = raw (params::manualBpm);
        p.noteDivision  = raw (params::noteDivision);
        p.feel          = raw (params::feel);
        p.drive         = raw (params::drive);
        p.age           = raw (params::age);
        p.wow           = raw (params::wow);
        p.flutter       = raw (params::flutter);
        p.hpf           = raw (params::hpf);
        p.lpf           = raw (params::lpf);
        p.filterMode    = raw (params::filterMode);
        p.stereo        = raw (params::stereo);
        p.duck          = raw (params::duck);
        p.mix           = raw (params::mix);
        p.tapeCondition = raw (params::tapeCondition);
        p.noise         = raw (params::noise);
        p.character     = raw (params::character);
        p.output        = raw (params::output);
        p.bypass        = raw (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < ECHOPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = ECHOPresets::getNamedResource (ECHOPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    EchoProcessor::~EchoProcessor() = default;

    bool EchoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
        return (in == mono && out == mono) || (in == stereo && out == stereo) || (in == mono && out == stereo);
    }

    // ------------------------------------------------------------------ telemetry

    bool EchoProcessor::isHeadActive (int head) const noexcept
    {
        if (! juce::isPositiveAndBelow (head, 3))
            return false;
        const int m = juce::jlimit (0, params::numModes - 1, juce::roundToInt (p.mode->load()));
        return ((params::modeHeadMask[m] >> head) & 1) != 0;
    }

    float EchoProcessor::takeHeadActivity (int head) noexcept
    {
        if (! juce::isPositiveAndBelow (head, 3))
            return 0.0f;
        const float v = engine.takeHeadPeak (head);
        return isHeadActive (head) ? v : 0.0f;
    }

    double EchoProcessor::getHeadDelayMs (int head) const noexcept
    {
        return engine.getHeadDelayMs (juce::jlimit (0, 2, head));
    }

    double EchoProcessor::getEffectiveBpm() const noexcept
    {
        const bool host = juce::roundToInt (p.bpmSource->load()) == (int) params::BpmSource::host;
        return host && hostTempoAvailable.load (std::memory_order_relaxed) ? hostBpm.load (std::memory_order_relaxed)
                                                                          : (double) p.manualBpm->load();
    }

    bool EchoProcessor::isSyncTimeClamped() const noexcept
    {
        const bool sync = juce::roundToInt (p.timeMode->load()) == (int) params::TimeMode::sync;
        return sync && timing::head1Time (true, 0.0, getEffectiveBpm(), juce::roundToInt (p.noteDivision->load()),
                                          juce::roundToInt (p.feel->load())).clamped;
    }

    double EchoProcessor::getTargetHead1Ms() const noexcept { return currentSettings().head1Ms; }

    // ------------------------------------------------------------------ TAP

    void EchoProcessor::setParameterFromMessageThread (const char* id, float plainValue)
    {
        if (auto* r = apvts.getParameter (id))
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->convertTo0to1 (plainValue));
            r->endChangeGesture();
        }
    }

    void EchoProcessor::tap (double timeSeconds)
    {
        const double interval = tapTempo.tap (timeSeconds);
        if (interval <= 0.0)
            return;

        if (juce::roundToInt (p.timeMode->load()) == (int) params::TimeMode::sync)
        {
            const float bpm = juce::jlimit (params::bpmMin, params::bpmMax, (float) std::round (60.0 / interval));
            setParameterFromMessageThread (params::manualBpm, bpm);
            setParameterFromMessageThread (params::bpmSource, (float) params::BpmSource::manual);
        }
        else
        {
            setParameterFromMessageThread (params::repeatRate,
                                           juce::jlimit (params::repeatMinMs, params::repeatMaxMs, (float) (interval * 1000.0)));
        }
    }

    // ------------------------------------------------------------------ audio

    Settings EchoProcessor::currentSettings() const noexcept
    {
        Settings s;
        const int m = juce::jlimit (0, params::numModes - 1, juce::roundToInt (p.mode->load()));
        s.headMask = params::modeHeadMask[m];
        const bool sync = juce::roundToInt (p.timeMode->load()) == (int) params::TimeMode::sync;
        s.head1Ms = timing::head1Time (sync, p.repeatRate->load(), getEffectiveBpm(),
                                       juce::roundToInt (p.noteDivision->load()), juce::roundToInt (p.feel->load())).ms;
        s.intensity = p.intensity->load();
        s.echoVolume = p.echoVolume->load();
        s.reverbVolume = p.reverbVolume->load();
        s.bassDb = p.bass->load();
        s.trebleDb = p.treble->load();
        s.drive = p.drive->load();
        s.age = p.age->load();
        s.wowPct = p.wow->load();
        s.flutterPct = p.flutter->load();
        s.hpfHz = p.hpf->load();
        s.lpfHz = p.lpf->load();
        s.filterMode = juce::jlimit (0, 2, juce::roundToInt (p.filterMode->load()));
        s.stereoPct = p.stereo->load();
        s.duckPct = p.duck->load();
        s.condition = juce::jlimit (0, 2, juce::roundToInt (p.tapeCondition->load()));
        s.noisePct = p.noise->load();
        s.character = juce::jlimit (0, 2, juce::roundToInt (p.character->load()));
        return s;
    }

    void EchoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);
        setLatencySamples (0);
        dry.prepare (preparedChannels, maxBlock, 0);
        wet.setSize (2, maxBlock, false, true, false);

        engine.setSettings (currentSettings());
        engine.prepare (sampleRate, maxBlock);

        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        mixRounded = p.mix->load() * 0.01f;
        outRounded = outputSmoothed.getCurrentValue();
        mixRound = dsp::onePoleCoeff (0.002, sampleRate);
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    }

    void EchoProcessor::releaseResources()
    {
        dry.reset();
    }

    void EchoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numIn = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels(), 2);
        const int numOut = juce::jmin (getMainBusNumOutputChannels(), buffer.getNumChannels(), preparedChannels);
        for (int ch = numOut; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);
        if (total == 0 || numOut == 0 || numIn == 0 || preparedChannels == 0)
            return;

        // Host tempo (any block may report or stop reporting it).
        bool hostHasTempo = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0 && std::isfinite (*bpm))
                {
                    hostBpm.store (juce::jlimit (1.0, 1000.0, *bpm), std::memory_order_relaxed);
                    hostHasTempo = true;
                }
        hostTempoAvailable.store (hostHasTempo, std::memory_order_relaxed);

        engine.setSettings (currentSettings());
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numIn, numOut);
    }

    void EchoProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numIn, int numOut)
    {
        float* io[2] {};
        for (int ch = 0; ch < numOut; ++ch)
            io[ch] = buffer.getWritePointer (ch, start);

        // Sanitise the input and meter it; mono -> stereo duplicates the input.
        float inSum = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
        {
            float squares = 0.0f, peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = dsp::sanitize (io[ch][i]);
                io[ch][i] = x;
                squares += x * x;
                peak = juce::jmax (peak, std::abs (x));
            }
            inputMeters[(size_t) ch].pushLevel (squares, numSamples, peak);
            inSum += squares;
            inPk = juce::jmax (inPk, peak);
        }
        if (numIn == 1)
            inputMeters[1].pushLevel (inSum, numSamples, inPk);
        inputMeter.pushLevel (inSum / (float) numIn, numSamples, inPk);
        if (numIn == 1 && numOut == 2)
            juce::FloatVectorOperations::copy (io[1], io[0], numSamples);
        dry.push (io, numOut, numSamples);

        // Wet path.
        float* wl = wet.getWritePointer (0);
        float* wr = wet.getWritePointer (1);
        engine.process (io[0], numOut > 1 ? io[1] : io[0], wl, wr, numSamples);
        if (numOut == 1)
            for (int i = 0; i < numSamples; ++i)
                wl[i] = 0.5f * (wl[i] + wr[i]); // mono output: the mid of the wet image

        // MIX (linear crossfade with the dry input), OUTPUT (after the blend),
        // bypass (fully bypassed = bit-exact input).
        float outSq[2] {}, outPk[2] {};
        const float* wetCh[2] { wl, wr };
        for (int i = 0; i < numSamples; ++i)
        {
            // OUTPUT: 30 ms exponential (dB-linear) ramp, MIX: 30 ms linear ramp;
            // both with rounded corners (2 ms one pole, snapped to the ramp)
            const float outRamp = outputSmoothed.getNextValue();
            const float od = outRamp - outRounded;
            outRounded = std::abs (od) < 1.0e-6f * outRamp ? outRamp : outRounded + mixRound * od;
            const float out = outRounded;
            const float mixLin = mixSmoothed.getNextValue();
            const float md = mixLin - mixRounded;
            mixRounded = std::abs (md) < 1.0e-6f ? mixLin : mixRounded + mixRound * md;
            const float mx = mixRounded;
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numOut; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = (drySample + (wetCh[ch][i] - drySample) * mx) * out;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp;
                io[ch][i] = y;
                outSq[ch] += y * y;
                outPk[ch] = juce::jmax (outPk[ch], std::abs (y));
            }
        }
        for (int ch = 0; ch < numOut; ++ch)
            outputMeters[(size_t) ch].pushLevel (outSq[ch], numSamples, outPk[ch]);
        if (numOut == 1)
            outputMeters[1].pushLevel (outSq[0], numSamples, outPk[0]);
        outputMeter.pushLevel ((outSq[0] + outSq[1]) / (float) numOut, numSamples, juce::jmax (outPk[0], outPk[1]));
    }

    juce::AudioProcessorEditor* EchoProcessor::createEditor()
    {
        return new EchoEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::echo::EchoProcessor();
}
#endif
