#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VEILPresets.h"

namespace tleet::veil
{
    VeilProcessor::VeilProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                  .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false),
                              params::createLayout(), "VEIL")
    {
        p.input      = apvts.getRawParameterValue (params::input);
        p.threshold  = apvts.getRawParameterValue (params::threshold);
        p.attack     = apvts.getRawParameterValue (params::attack);
        p.recovery   = apvts.getRawParameterValue (params::recovery);
        p.compress   = apvts.getRawParameterValue (params::compress);
        p.mix        = apvts.getRawParameterValue (params::mix);
        p.output     = apvts.getRawParameterValue (params::output);
        p.tubeDrive  = apvts.getRawParameterValue (params::tubeDrive);
        p.harmonics  = apvts.getRawParameterValue (params::harmonics);
        p.saturation = apvts.getRawParameterValue (params::saturation);
        p.dryWet     = apvts.getRawParameterValue (params::dryWet);
        p.hpf        = apvts.getRawParameterValue (params::hpf);
        p.scHpf      = apvts.getRawParameterValue (params::scHpf);
        p.character  = apvts.getRawParameterValue (params::character);
        p.scExternal = apvts.getRawParameterValue (params::scExternal);
        p.stereoLink = apvts.getRawParameterValue (params::stereoLink);
        p.meterMode  = apvts.getRawParameterValue (params::meterMode);
        p.bypass     = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < VEILPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = VEILPresets::getNamedResource (VEILPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    VeilProcessor::~VeilProcessor() = default;

    bool VeilProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

    float VeilProcessor::takeGrDb (int channel) noexcept
    {
        const auto c = (size_t) juce::jlimit (0, 1, channel);
        const float held = grMeters[c].takeGainReduction();
        return held >= 0.0f ? held : currentGr[c].load (std::memory_order_relaxed);
    }

    void VeilProcessor::pushControlsToModel() noexcept
    {
        model.setThresholdDbfs (params::thresholdToDbfs (p.threshold->load()));
        model.setCompress (p.compress->load());
        model.setAttackMs (p.attack->load());
        model.setRecoveryMs (p.recovery->load());
        model.setMix (p.mix->load() * 0.01f);
        model.setTubeDrive (p.tubeDrive->load() * 0.01f);
        model.setHarmonics (p.harmonics->load() * 0.01f);
        model.setSaturation (p.saturation->load() * 0.01f);
        model.setCharacter (juce::roundToInt (p.character->load()));
        model.setHpf (p.hpf->load());
        model.setScHpf (p.scHpf->load());
        model.setStereoLink (p.stereoLink->load() > 0.5f);
        // meterMode is display only: it is deliberately never passed to the model.
    }

    void VeilProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, VeilModel::maxChannels, getMainBusNumOutputChannels());
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
        keyOversampling = makeOversampler (VeilModel::maxChannels); // same filters: the key stays time-aligned with the audio
        keyScratch.setSize (VeilModel::maxChannels, maxBlock, false, true, false);

        const int latency = (int) std::round (oversampling->getLatencyInSamples());
        setLatencySamples (latency);
        dry.prepare (preparedChannels, maxBlock, latency);

        const bool keyBusLive = getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
        const bool external = p.scExternal->load() > 0.5f && keyBusLive;
        pushControlsToModel();
        model.setUseExternalKey (external);
        model.prepare (sampleRate, 1 << oversamplingFactorLog2);

        inputSmoothed.reset (sampleRate, 0.03);
        inputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.input->load()));
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        dryWetSmoothed.reset (sampleRate, 0.03);
        dryWetSmoothed.setCurrentAndTargetValue (p.dryWet->load() * 0.01f);
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        usingExternal.store (external);
        spectrum.setSampleRate (sampleRate);
        for (auto& g : currentGr)
            g.store (0.0f);
    }

    void VeilProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        if (keyOversampling)
            keyOversampling->reset();
        dry.reset();
        model.reset();
    }

    void VeilProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
                numKey = juce::jmin (keyChannels, VeilModel::maxChannels);
            }
        }

        const bool external = p.scExternal->load() > 0.5f && numKey > 0;
        usingExternal.store (external, std::memory_order_relaxed);
        model.setUseExternalKey (external);

        if (total == 0 || numChannels == 0 || oversampling == nullptr)
            return;

        pushControlsToModel();
        inputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.input->load()));
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        dryWetSmoothed.setTargetValue (p.dryWet->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (mainBuffer, numKey > 0 ? &keyBuffer : nullptr, start, juce::jmin (maxBlock, total - start),
                          numChannels, numKey);
    }

    void VeilProcessor::processChunk (juce::AudioBuffer<float>& mainBuffer, const juce::AudioBuffer<float>* keyBuffer,
                                      int start, int numSamples, int numChannels, int numKey)
    {
        float* io[VeilModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch] = mainBuffer.getWritePointer (ch, start);

        // Sanitise host input, meter it and capture the dry path before anything else.
        float inSum = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
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
        if (numChannels == 1)
            inputMeters[1].pushLevel (inSum, numSamples, inPk);
        inputMeter.pushLevel (inSum / (float) numChannels, numSamples, inPk);
        dry.push (io, numChannels, numSamples);

        // INPUT gain (into the input tube stage), then the audio-path HPF and the coupling high-pass.
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inputSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= g;
        }
        model.preProcess (io, numChannels, numSamples);

        // Key: sanitised copy, upsampled with the same filters as the audio.
        const float* keys[VeilModel::maxChannels] {};
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

        // Wet path at 2x: input tube stage -> detector / vari-mu element -> MIX -> output stage.
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* chans[VeilModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            chans[ch] = up.getChannelPointer ((size_t) ch);
        model.process (chans, numChannels, numKey > 0 ? keys : nullptr, numKey, numSamples << oversamplingFactorLog2);
        oversampling->processSamplesDown (block);
        model.postProcess (io, numChannels, numSamples);

        // GR telemetry: the gain each channel's element actually applied.
        for (int ch = 0; ch < 2; ++ch)
        {
            grMeters[(size_t) ch].pushGainReduction (model.getBlockMaxGrDb (ch));
            currentGr[(size_t) ch].store (model.getCurrentGrDb (ch), std::memory_order_relaxed);
        }

        // OUTPUT gain (last gain on the processed path), DRY/WET (linear
        // crossfade with the latency-aligned plugin input), bypass (fully
        // bypassed = bit-exact delayed input).
        float outSq[VeilModel::maxChannels] {}, outPk[VeilModel::maxChannels] {};
        for (int i = 0; i < numSamples; ++i)
        {
            const float out = outputSmoothed.getNextValue();
            const float dw = dryWetSmoothed.getNextValue();
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = drySample + (io[ch][i] * out - drySample) * dw;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp;
                io[ch][i] = y;
                outSq[ch] += y * y;
                outPk[ch] = juce::jmax (outPk[ch], std::abs (y));
            }
        }
        for (int ch = 0; ch < numChannels; ++ch)
            outputMeters[(size_t) ch].pushLevel (outSq[ch], numSamples, outPk[ch]);
        if (numChannels == 1)
            outputMeters[1].pushLevel (outSq[0], numSamples, outPk[0]);
        outputMeter.pushLevel ((outSq[0] + outSq[1]) / (float) numChannels, numSamples, juce::jmax (outPk[0], outPk[1]));

        spectrum.push (io[0], numChannels > 1 ? io[1] : nullptr, numSamples);
    }

    juce::AudioProcessorEditor* VeilProcessor::createEditor()
    {
        return new VeilEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::veil::VeilProcessor();
}
#endif
