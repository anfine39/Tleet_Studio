#include "PluginProcessor.h"
#include "Parameters.h"
#include "GLUEPresets.h"
#include "PluginEditor.h"

namespace tleet::glue
{
    GlueProcessor::GlueProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                  .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false),
                              params::createLayout(), "GLUE")
    {
        p.threshold  = apvts.getRawParameterValue (params::threshold);
        p.attack     = apvts.getRawParameterValue (params::attack);
        p.release    = apvts.getRawParameterValue (params::release);
        p.ratio      = apvts.getRawParameterValue (params::ratio);
        p.makeup     = apvts.getRawParameterValue (params::makeup);
        p.scHpf      = apvts.getRawParameterValue (params::scHpf);
        p.mix        = apvts.getRawParameterValue (params::mix);
        p.character  = apvts.getRawParameterValue (params::character);
        p.dark       = apvts.getRawParameterValue (params::dark);
        p.crush      = apvts.getRawParameterValue (params::crush);
        p.output     = apvts.getRawParameterValue (params::output);
        p.scExternal = apvts.getRawParameterValue (params::scExternal);
        p.bypass     = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < GLUEPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = GLUEPresets::getNamedResource (GLUEPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    GlueProcessor::~GlueProcessor() = default;

    bool GlueProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

    void GlueProcessor::pushControlsToModel() noexcept
    {
        model.setThresholdDb (params::thresholdToDbfs (p.threshold->load()));
        model.setAttack (juce::roundToInt (p.attack->load()));
        model.setRelease (juce::roundToInt (p.release->load()));
        model.setRatio (juce::roundToInt (p.ratio->load()));
        model.setMakeupDb (p.makeup->load());
        model.setCharacter (juce::roundToInt (p.character->load()));
        model.setDark (p.dark->load() * 0.01f);
        model.setCrush (p.crush->load() * 0.01f);
        model.setScHpf (p.scHpf->load());
    }

    void GlueProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, GlueModel::maxChannels, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        auto makeOversampler = [this] (int channels)
        {
            auto os = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) channels, (size_t) oversamplingFactorLog2,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                true,   // max quality
                true);  // integer latency, so the dry path aligns sample-exactly
            os->initProcessing ((size_t) maxBlock);
            return os;
        };
        oversampling = makeOversampler (preparedChannels);
        keyOversampling = makeOversampler (GlueModel::maxChannels); // same filters: the key stays time-aligned with the audio
        keyScratch.setSize (GlueModel::maxChannels, maxBlock, false, true, false);

        const int latency = (int) std::round (oversampling->getLatencyInSamples());
        setLatencySamples (latency);
        dry.prepare (preparedChannels, maxBlock, latency);

        // Start on the detector source the current state selects, so a fresh
        // start never crossfades from the wrong one.
        const bool keyBusLive = getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
        const bool external = p.scExternal->load() > 0.5f && keyBusLive;
        pushControlsToModel();
        model.setUseExternalKey (external);
        model.prepare (sampleRate * (1 << oversamplingFactorLog2));

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        usingExternal.store (external);
    }

    void GlueProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        if (keyOversampling)
            keyOversampling->reset();
        dry.reset();
        model.reset();
    }

    void GlueProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
                numKey = juce::jmin (keyChannels, GlueModel::maxChannels);
            }
        }

        const bool external = p.scExternal->load() > 0.5f && numKey > 0;
        usingExternal.store (external, std::memory_order_relaxed);
        model.setUseExternalKey (external);

        if (total == 0 || oversampling == nullptr)
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

    void GlueProcessor::processChunk (juce::AudioBuffer<float>& mainBuffer, const juce::AudioBuffer<float>* keyBuffer,
                                      int start, int numSamples, int numChannels, int numKey)
    {
        float* io[GlueModel::maxChannels] {};
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

        // Key: sanitised copy, upsampled with the same filters as the audio.
        const float* keys[GlueModel::maxChannels] {};
        if (keyBuffer != nullptr && numKey > 0)
        {
            for (int k = 0; k < numKey; ++k)
            {
                const float* src = keyBuffer->getReadPointer (k, start);
                float* dst = keyScratch.getWritePointer (k);
                for (int i = 0; i < numSamples; ++i)
                    dst[i] = dsp::sanitize (src[i]);
            }
            juce::dsp::AudioBlock<float> keyBlock (keyScratch);
            auto keyUp = keyOversampling->processSamplesUp (keyBlock.getSubsetChannelBlock (0, (size_t) numKey)
                                                                    .getSubBlock (0, (size_t) numSamples));
            for (int k = 0; k < numKey; ++k)
                keys[k] = keyUp.getChannelPointer ((size_t) k);
        }

        // Wet path at 2x: detector loop -> VCA -> makeup -> output stage.
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* chans[GlueModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            chans[ch] = up.getChannelPointer ((size_t) ch);
        model.process (chans, numChannels, numKey > 0 ? keys : nullptr, numKey, numSamples << oversamplingFactorLog2);
        oversampling->processSamplesDown (block);

        // GR telemetry is the VCA gain actually applied, not an estimate.
        grMeter.pushGainReduction (-juce::Decibels::gainToDecibels (model.takeMinGain(), -100.0f));

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

    juce::AudioProcessorEditor* GlueProcessor::createEditor()
    {
        return new GlueEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::glue::GlueProcessor();
}
#endif
