#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ASHPresets.h"

namespace tleet::ash
{
    AshProcessor::AshProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "ASH")
    {
        p.peakReduction = apvts.getRawParameterValue (params::peakReduction);
        p.gain          = apvts.getRawParameterValue (params::gain);
        p.mode          = apvts.getRawParameterValue (params::mode);
        p.mix           = apvts.getRawParameterValue (params::mix);
        p.character     = apvts.getRawParameterValue (params::character);
        p.output        = apvts.getRawParameterValue (params::output);
        p.bypass        = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < ASHPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = ASHPresets::getNamedResource (ASHPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    AshProcessor::~AshProcessor() = default;

    bool AshProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void AshProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jmax (1, getTotalNumOutputChannels());

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) preparedChannels, (size_t) oversamplingFactorLog2,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,   // max quality
            true);  // integer latency, so the dry path aligns sample-exactly
        maxBlock = juce::jmax (1, samplesPerBlock);
        oversampling->initProcessing ((size_t) maxBlock);

        const int latency = (int) std::round (oversampling->getLatencyInSamples());
        setLatencySamples (latency);
        dry.prepare (preparedChannels, maxBlock, latency);

        model.setPeakReduction (p.peakReduction->load());
        model.setLimitMode (p.mode->load() > 0.5f);
        model.setCharacter (p.character->load() * 0.01f);
        model.setMakeupDb (params::gainKnobToDb (p.gain->load()));
        model.prepare (sampleRate * (1 << oversamplingFactorLog2));

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    }

    void AshProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        dry.reset();
        model.reset();
    }

    void AshProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);

        if (total == 0 || oversampling == nullptr)
            return;

        model.setPeakReduction (p.peakReduction->load());
        model.setLimitMode (p.mode->load() > 0.5f);
        model.setCharacter (p.character->load() * 0.01f);
        model.setMakeupDb (params::gainKnobToDb (p.gain->load()));
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numChannels);
    }

    void AshProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
    {
        float* io[AshModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch] = buffer.getWritePointer (ch, start);

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

        // Wet path at 2x.
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* chans[AshModel::maxChannels] {};
        for (size_t ch = 0; ch < up.getNumChannels() && ch < (size_t) AshModel::maxChannels; ++ch)
            chans[ch] = up.getChannelPointer (ch);
        model.process (chans, numChannels, (int) up.getNumSamples());
        oversampling->processSamplesDown (block);

        grMeter.pushGainReduction (-juce::Decibels::gainToDecibels (model.takeMinGain(), -60.0f));

        // Mix (linear crossfade: wet and dry are correlated, so equal-power
        // would bump the level by up to +3 dB at 50 %), output trim, bypass.
        float outSquares = 0.0f, outPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixSmoothed.getNextValue();
            const float out = outputSmoothed.getNextValue();
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = (drySample + (io[ch][i] - drySample) * m) * out;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp; // fully bypassed = bit-exact dry
                io[ch][i] = y;
                outSquares += y * y;
                outPeak = juce::jmax (outPeak, std::abs (y));
            }
        }
        outputMeter.pushLevel (outSquares / (float) numChannels, numSamples, outPeak);
    }

    juce::AudioProcessorEditor* AshProcessor::createEditor()
    {
        return new AshEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::ash::AshProcessor();
}
