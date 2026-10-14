#include "PluginProcessor.h"
#include "Parameters.h"
#include "PUNCHPresets.h"
#include "PluginEditor.h"

namespace tleet::punch
{
    PunchProcessor::PunchProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "PUNCH")
    {
        p.threshold = apvts.getRawParameterValue (params::threshold);
        p.ratio     = apvts.getRawParameterValue (params::ratio);
        p.overEasy  = apvts.getRawParameterValue (params::overEasy);
        p.grit      = apvts.getRawParameterValue (params::grit);
        p.mix       = apvts.getRawParameterValue (params::mix);
        p.output    = apvts.getRawParameterValue (params::output);
        p.bypass    = apvts.getRawParameterValue (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < PUNCHPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = PUNCHPresets::getNamedResource (PUNCHPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    PunchProcessor::~PunchProcessor() = default;

    bool PunchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void PunchProcessor::pushControlsToModel() noexcept
    {
        model.setThresholdDb (params::thresholdToDetectorDb (p.threshold->load()));
        model.setSlope (p.ratio->load());
        model.setOverEasy (p.overEasy->load() > 0.5f);
        model.setGrit (p.grit->load() * 0.01f);
    }

    void PunchProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, PunchModel::maxChannels, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) preparedChannels, (size_t) oversamplingFactorLog2,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,   // max quality
            true);  // integer latency, so the dry path aligns sample-exactly
        oversampling->initProcessing ((size_t) maxBlock);

        const int latency = (int) std::round (oversampling->getLatencyInSamples());
        setLatencySamples (latency);
        dry.prepare (preparedChannels, maxBlock, latency);
        dry.reset(); // a re-prepare starts from silence, never from the previous run's history

        pushControlsToModel();
        model.prepare (sampleRate * (1 << oversamplingFactorLog2));

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        detectorLevel.store (-300.0f);
    }

    void PunchProcessor::releaseResources()
    {
        if (oversampling)
            oversampling->reset();
        dry.reset();
        model.reset();
    }

    void PunchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        // Never index past what the host actually passed, even if it disagrees with the layout.
        const int mainChannels = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
        juce::AudioBuffer<float> mainBuffer (buffer.getArrayOfWritePointers(), mainChannels, total);
        const int numChannels = juce::jmin (mainChannels, preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);

        if (total == 0 || oversampling == nullptr || numChannels == 0)
            return;

        pushControlsToModel();
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (mainBuffer, start, juce::jmin (maxBlock, total - start), numChannels);

        detectorLevel.store (model.getDetectorLevelDb(), std::memory_order_relaxed);
    }

    void PunchProcessor::processChunk (juce::AudioBuffer<float>& mainBuffer, int start, int numSamples, int numChannels)
    {
        float* io[PunchModel::maxChannels] {};
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

        // Wet path at 2x: RMS detector -> gain computer -> VCA -> GRIT.
        juce::dsp::AudioBlock<float> block (io, (size_t) numChannels, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* chans[PunchModel::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
            chans[ch] = up.getChannelPointer ((size_t) ch);
        model.process (chans, numChannels, numSamples << oversamplingFactorLog2);
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

    juce::AudioProcessorEditor* PunchProcessor::createEditor()
    {
        return new PunchEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::punch::PunchProcessor();
}
#endif
