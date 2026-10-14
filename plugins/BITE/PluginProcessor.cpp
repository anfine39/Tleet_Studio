#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "BITEPresets.h"

namespace tleet::bite
{
    BiteProcessor::BiteProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "BITE")
    {
        p.input     = apvts.getRawParameterValue (params::input);
        p.output    = apvts.getRawParameterValue (params::output);
        p.attack    = apvts.getRawParameterValue (params::attack);
        p.release   = apvts.getRawParameterValue (params::release);
        p.ratio     = apvts.getRawParameterValue (params::ratio);
        p.mix       = apvts.getRawParameterValue (params::mix);
        p.character = apvts.getRawParameterValue (params::character);
        p.bypass    = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < BITEPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = BITEPresets::getNamedResource (BITEPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    BiteProcessor::~BiteProcessor() = default;

    bool BiteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void BiteProcessor::pushControlsToModel() noexcept
    {
        // Input drives the FET (there is no threshold control); Output is the
        // makeup on the wet path, ahead of Mix.
        model.setInputDb (p.input->load());
        model.setOutputDb (p.output->load());
        model.setAttackMicroseconds (p.attack->load());
        model.setReleaseMilliseconds (p.release->load());
        model.setRatio (juce::roundToInt (p.ratio->load()));
        model.setCharacter (juce::roundToInt (p.character->load()));
    }

    void BiteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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

        pushControlsToModel();
        model.prepare (sampleRate * (1 << oversamplingFactorLog2));

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    }

    void BiteProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        dry.reset();
        model.reset();
    }

    void BiteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);

        if (total == 0 || oversampling == nullptr)
            return;

        pushControlsToModel();
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numChannels);
    }

    void BiteProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
    {
        float* io[BiteModel::maxChannels] {};
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

        // Wet path at 2x: Input -> FET -> amplifiers -> Output (makeup).
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* chans[BiteModel::maxChannels] {};
        for (size_t ch = 0; ch < up.getNumChannels() && ch < (size_t) BiteModel::maxChannels; ++ch)
            chans[ch] = up.getChannelPointer (ch);
        model.process (chans, numChannels, (int) up.getNumSamples());
        oversampling->processSamplesDown (block);

        // GR telemetry is the FET gain actually applied, not an estimate.
        grMeter.pushGainReduction (-juce::Decibels::gainToDecibels (model.takeMinGain(), -100.0f));

        // Mix (linear crossfade with the latency-aligned dry; equal-power would
        // bump correlated signals by up to +3 dB at 50 %), then bypass.
        float outSquares = 0.0f, outPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixSmoothed.getNextValue();
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = drySample + (io[ch][i] - drySample) * m;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp; // fully bypassed = bit-exact dry
                io[ch][i] = y;
                outSquares += y * y;
                outPeak = juce::jmax (outPeak, std::abs (y));
            }
        }
        outputMeter.pushLevel (outSquares / (float) numChannels, numSamples, outPeak);
    }

    juce::AudioProcessorEditor* BiteProcessor::createEditor()
    {
        return new BiteEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::bite::BiteProcessor();
}
#endif
