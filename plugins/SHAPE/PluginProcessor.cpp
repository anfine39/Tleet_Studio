#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SHAPEPresets.h"

namespace tleet::shape
{
    ShapeProcessor::ShapeProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "SHAPE")
    {
        p.lfFreq      = apvts.getRawParameterValue (params::lfFreq);
        p.lfBoost     = apvts.getRawParameterValue (params::lfBoost);
        p.lfAtten     = apvts.getRawParameterValue (params::lfAtten);
        p.hfFreq      = apvts.getRawParameterValue (params::hfFreq);
        p.hfBoost     = apvts.getRawParameterValue (params::hfBoost);
        p.hfBandwidth = apvts.getRawParameterValue (params::hfBandwidth);
        p.hfAttenFreq = apvts.getRawParameterValue (params::hfAttenFreq);
        p.hfAtten     = apvts.getRawParameterValue (params::hfAtten);
        p.drive       = apvts.getRawParameterValue (params::drive);
        p.character   = apvts.getRawParameterValue (params::character);
        p.mix         = apvts.getRawParameterValue (params::mix);
        p.output      = apvts.getRawParameterValue (params::output);
        p.eqIn        = apvts.getRawParameterValue (params::eqIn);
        p.bypass      = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < SHAPEPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = SHAPEPresets::getNamedResource (SHAPEPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    ShapeProcessor::~ShapeProcessor() = default;

    bool ShapeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    EqSettings ShapeProcessor::getEqSettings() const noexcept
    {
        EqSettings s;
        s.lfIndex = juce::jlimit (0, params::numLfFreqs - 1, juce::roundToInt (p.lfFreq->load()));
        s.lfBoost = p.lfBoost->load();
        s.lfAtten = p.lfAtten->load();
        s.hfIndex = juce::jlimit (0, params::numHfFreqs - 1, juce::roundToInt (p.hfFreq->load()));
        s.hfBoost = p.hfBoost->load();
        s.hfBandwidth = p.hfBandwidth->load();
        s.attenIndex = juce::jlimit (0, params::numHfAttenFreqs - 1, juce::roundToInt (p.hfAttenFreq->load()));
        s.hfAtten = p.hfAtten->load();
        s.eqIn = p.eqIn->load() > 0.5f;
        return s;
    }

    void ShapeProcessor::pushControlsToModel() noexcept
    {
        model.setEq (getEqSettings());
        model.setDrive (p.drive->load() * 0.01f);
        model.setCharacter (juce::roundToInt (p.character->load()));
    }

    void ShapeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, ShapeModel::maxChannels, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);
        osLog2 = oversamplingFactorLog2For (sampleRate);

        int latency = 0;
        if (osLog2 > 0)
        {
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) preparedChannels, (size_t) osLog2,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                true,   // max quality
                true);  // integer latency, so the dry path aligns sample-exactly
            oversampling->initProcessing ((size_t) maxBlock);
            latency = (int) std::round (oversampling->getLatencyInSamples());
        }
        else
        {
            oversampling.reset();
        }
        setLatencySamples (latency);
        dry.prepare (preparedChannels, maxBlock, latency);

        pushControlsToModel();
        model.prepare (sampleRate, 1 << osLog2);

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

    void ShapeProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        dry.reset();
        model.reset();
    }

    void ShapeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int mainChannels = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
        juce::AudioBuffer<float> mainBuffer (buffer.getArrayOfWritePointers(), mainChannels, total);
        const int numChannels = juce::jmin (mainChannels, preparedChannels);
        for (int ch = numChannels; ch < mainChannels; ++ch)
            mainBuffer.clear (ch, 0, total);

        if (total == 0 || numChannels == 0 || preparedChannels == 0)
            return;

        pushControlsToModel();
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (mainBuffer, start, juce::jmin (maxBlock, total - start), numChannels);
    }

    void ShapeProcessor::processChunk (juce::AudioBuffer<float>& mainBuffer, int start, int numSamples, int numChannels)
    {
        float* io[ShapeModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch] = mainBuffer.getWritePointer (ch, start);

        // Sanitise host input, meter it and capture the dry path before anything else.
        float inSum = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = dsp::sanitize (io[ch][i]);
                io[ch][i] = x;
                inSum += x * x;
                inPk = juce::jmax (inPk, std::abs (x));
            }
        inputMeter.pushLevel (inSum / (float) numChannels, numSamples, inPk);
        dry.push (io, numChannels, numSamples);

        // Wet path at the internal rate.
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        if (oversampling != nullptr)
        {
            auto up = oversampling->processSamplesUp (block);
            float* chans[ShapeModel::maxChannels] {};
            for (int ch = 0; ch < numChannels; ++ch)
                chans[ch] = up.getChannelPointer ((size_t) ch);
            model.process (chans, numChannels, (int) up.getNumSamples());
            oversampling->processSamplesDown (block);
        }
        else
        {
            model.process (io, numChannels, numSamples);
        }

        // MIX (linear crossfade with the latency-aligned plugin input), OUTPUT
        // (after the blend), bypass (fully bypassed = bit-exact delayed input).
        float outSq = 0.0f, outPk = 0.0f;
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

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = (drySample + (io[ch][i] - drySample) * mx) * out;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp;
                io[ch][i] = y;
                outSq += y * y;
                outPk = juce::jmax (outPk, std::abs (y));
            }
        }
        outputMeter.pushLevel (outSq / (float) numChannels, numSamples, outPk);
    }

    juce::AudioProcessorEditor* ShapeProcessor::createEditor()
    {
        return new ShapeEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::shape::ShapeProcessor();
}
#endif
