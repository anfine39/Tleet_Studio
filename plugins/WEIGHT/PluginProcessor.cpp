#include "PluginProcessor.h"
#include "WEIGHTPresets.h"

namespace tleet::weight
{
    WeightProcessor::WeightProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                  .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false),
                              params::createLayout(), "WEIGHT")
    {
        p.threshold  = apvts.getRawParameterValue (params::threshold);
        p.attack     = apvts.getRawParameterValue (params::attack);
        p.release    = apvts.getRawParameterValue (params::release);
        p.ratio      = apvts.getRawParameterValue (params::ratio);
        p.makeup     = apvts.getRawParameterValue (params::makeup);
        p.thrust     = apvts.getRawParameterValue (params::thrust);
        p.detector   = apvts.getRawParameterValue (params::detector);
        p.tone       = apvts.getRawParameterValue (params::tone);
        p.scHpf      = apvts.getRawParameterValue (params::scHpf);
        p.scExternal = apvts.getRawParameterValue (params::scExternal);
        p.mix        = apvts.getRawParameterValue (params::mix);
        p.output     = apvts.getRawParameterValue (params::output);
        p.link       = apvts.getRawParameterValue (params::link);
        p.grMode     = apvts.getRawParameterValue (params::grMode);
        p.bypass     = apvts.getRawParameterValue (params::bypass);

        model.setTelemetry (&grLeft, &grRight);

        juce::StringArray factory;
        for (int i = 0; i < WEIGHTPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = WEIGHTPresets::getNamedResource (WEIGHTPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    WeightProcessor::~WeightProcessor() = default;

    bool WeightProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        if (! (in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())))
            return false;

        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! (sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo()))
                return false;
        }
        return true;
    }

    float WeightProcessor::takeGrDb (int channel, GrMode mode) noexcept
    {
        auto& t = getGrTelemetry (channel);
        switch (mode)
        {
            case GrMode::peak:    return t.takePeakDb();
            case GrMode::average: return t.getAverageDb();
            case GrMode::rms:
            default:              return t.getRmsDb();
        }
    }

    void WeightProcessor::pushControlsToModel() noexcept
    {
        model.setThresholdDbfs (params::thresholdToDbfs (p.threshold->load()));
        model.setAttack (juce::roundToInt (p.attack->load()));
        model.setRelease (juce::roundToInt (p.release->load()));
        model.setRatio (juce::roundToInt (p.ratio->load()));
        model.setMakeupDb (p.makeup->load());
        model.setThrust (juce::roundToInt (p.thrust->load()));
        model.setTopology (juce::roundToInt (p.detector->load()));
        model.setTone (juce::roundToInt (p.tone->load()));
        model.setScHpf (p.scHpf->load());
        model.setLink (p.link->load() * 0.01f);
        // grMode is display only: it is deliberately never passed to the model.
    }

    void WeightProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, WeightModel::maxChannels, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        setLatencySamples (0);
        dry.prepare (preparedChannels, maxBlock, 0);
        grLeft.prepare (sampleRate);
        grRight.prepare (sampleRate);

        // Start on the detector source the current state selects, so a fresh
        // start never crossfades from the wrong one.
        const bool keyBusLive = getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
        const bool external = p.scExternal->load() > 0.5f && keyBusLive;
        pushControlsToModel();
        model.setUseExternalKey (external);
        model.prepare (sampleRate);

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        usingExternal.store (external);
    }

    void WeightProcessor::releaseResources()
    {
        dry.reset();
        model.reset();
    }

    void WeightProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int available = buffer.getNumChannels();

        // Main bus: channels 0.. (the main bus always comes first). Never index
        // past what the host actually passed, even if it disagrees with the layout.
        const int mainChannels = juce::jmin (getMainBusNumInputChannels(), available);
        juce::AudioBuffer<float> mainBuffer (buffer.getArrayOfWritePointers(), mainChannels, total);
        const int numChannels = juce::jmin (mainChannels, preparedChannels);
        for (int ch = numChannels; ch < mainChannels; ++ch)
            mainBuffer.clear (ch, 0, total);

        // External key: only when the host has enabled the Sidechain bus with
        // channels (and passed them). Inputs only, never written.
        juce::AudioBuffer<float> keyBuffer;
        int numKey = 0;
        if (getBusCount (true) > 1)
        {
            const int offset = getChannelIndexInProcessBlockBuffer (true, 1, 0);
            const int keyChannels = juce::jmin (getChannelCountOfBus (true, 1), available - offset);
            if (keyChannels > 0)
            {
                keyBuffer.setDataToReferTo (buffer.getArrayOfWritePointers() + offset, keyChannels, total);
                numKey = juce::jmin (keyChannels, WeightModel::maxChannels);
            }
        }

        const bool external = p.scExternal->load() > 0.5f && numKey > 0;
        usingExternal.store (external, std::memory_order_relaxed);
        model.setUseExternalKey (external);

        if (total == 0 || numChannels == 0)
            return;

        pushControlsToModel();
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (mainBuffer, numKey > 0 ? &keyBuffer : nullptr, start, juce::jmin (maxBlock, total - start),
                          numChannels, numKey);
    }

    void WeightProcessor::processChunk (juce::AudioBuffer<float>& mainBuffer, const juce::AudioBuffer<float>* keyBuffer,
                                        int start, int numSamples, int numChannels, int numKey)
    {
        float* io[WeightModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch] = mainBuffer.getWritePointer (ch, start);

        // Sanitise host input and capture the dry path before anything else.
        float inSquares = 0.0f, inPeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                io[ch][i] = dsp::sanitize (io[ch][i]);
                inSquares += io[ch][i] * io[ch][i];
                inPeak = juce::jmax (inPeak, std::abs (io[ch][i]));
            }
        inputMeter.pushLevel (inSquares / (float) numChannels, numSamples, inPeak);
        dry.push (io, numChannels, numSamples);

        // Key: read-only pointers (the model sanitises every key sample it reads).
        const float* keys[WeightModel::maxChannels] {};
        if (keyBuffer != nullptr)
            for (int k = 0; k < numKey; ++k)
                keys[k] = keyBuffer->getReadPointer (k, start);

        // Wet path: detectors -> link -> VCA -> makeup -> output stage.
        model.process (io, numChannels, numKey > 0 ? keys : nullptr, numKey, numSamples);
        grLeft.publish();
        grRight.publish();

        // Mix (linear crossfade with the latency-aligned dry), Output trim
        // (after Mix, on both), then bypass (fully bypassed = bit-exact dry).
        float outSquares = 0.0f, outPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixSmoothed.getNextValue();
            const float trim = outputSmoothed.getNextValue();
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = (drySample + (io[ch][i] - drySample) * m) * trim;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp;
                io[ch][i] = y;
                outSquares += y * y;
                outPeak = juce::jmax (outPeak, std::abs (y));
            }
        }
        outputMeter.pushLevel (outSquares / (float) numChannels, numSamples, outPeak);
    }

    juce::AudioProcessorEditor* WeightProcessor::createEditor()
    {
        // Temporary: the skinned WeightEditor (PluginEditor.cpp) replaces this.
        return new juce::GenericAudioProcessorEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::weight::WeightProcessor();
}
#endif
