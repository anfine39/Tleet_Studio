#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "DOUBLEPresets.h"

namespace tleet::dbl
{
    DoubleProcessor::DoubleProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "DOUBLE")
    {
        p.voices    = apvts.getRawParameterValue (params::voices);
        p.mode      = apvts.getRawParameterValue (params::mode);
        p.detune    = apvts.getRawParameterValue (params::detune);
        p.delay     = apvts.getRawParameterValue (params::delay);
        p.humanize  = apvts.getRawParameterValue (params::humanize);
        p.width     = apvts.getRawParameterValue (params::width);
        p.hpf       = apvts.getRawParameterValue (params::hpf);
        p.lpf       = apvts.getRawParameterValue (params::lpf);
        p.monoSafe  = apvts.getRawParameterValue (params::monoSafe);
        p.autoLevel = apvts.getRawParameterValue (params::autoLevel);
        p.mix       = apvts.getRawParameterValue (params::mix);
        p.output    = apvts.getRawParameterValue (params::output);
        p.bypass    = apvts.getRawParameterValue (params::bypass);

        engine.setTelemetry (&telemetry);

        juce::StringArray factory;
        for (int i = 0; i < DOUBLEPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = DOUBLEPresets::getNamedResource (DOUBLEPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    DoubleProcessor::~DoubleProcessor() = default;

    bool DoubleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    DoubleEngine::Settings DoubleProcessor::readSettings() const noexcept
    {
        DoubleEngine::Settings s;
        s.voices = params::voiceCount (juce::roundToInt (p.voices->load()));
        s.mode = juce::roundToInt (p.mode->load());
        s.detuneCents = p.detune->load();
        s.delayMs = p.delay->load();
        s.humanize = p.humanize->load() * 0.01f;
        s.width = p.width->load() * 0.01f;
        s.hpfHz = p.hpf->load();
        s.lpfHz = p.lpf->load();
        s.monoSafe = p.monoSafe->load() > 0.5f;
        s.autoLevel = p.autoLevel->load() > 0.5f;
        return s;
    }

    void DoubleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, DoubleEngine::maxChannels, getTotalNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        // Zero latency: voices only ever read the past, so the dry needs no delay.
        setLatencySamples (0);
        dry.prepare (preparedChannels, maxBlock, 0);
        wetBuffer.setSize (preparedChannels, maxBlock, false, true, false);

        engine.setSettings (readSettings());
        engine.prepare (sampleRate, maxBlock, preparedChannels);

        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    }

    void DoubleProcessor::releaseResources()
    {
        dry.reset();
    }

    void DoubleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);

        if (total == 0 || numChannels == 0 || wetBuffer.getNumSamples() == 0)
            return;

        engine.setSettings (readSettings());
        engine.setSoloVoice (soloVoice.load (std::memory_order_relaxed));
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numChannels);
    }

    void DoubleProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
    {
        float* io[DoubleEngine::maxChannels] {};
        float* wet[DoubleEngine::maxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            io[ch] = buffer.getWritePointer (ch, start);
            wet[ch] = wetBuffer.getWritePointer (ch);
        }

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

        // Wet: the doubled voices only (the dry is the Mix's dry side).
        engine.process (io, wet, numChannels, numSamples);

        // Mix (linear crossfade with the aligned dry, as on every TLEET plugin),
        // then Output trim on the mixed signal (like ASH), then bypass.
        float outSquares = 0.0f, outPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixSmoothed.getNextValue();
            const float out = outputSmoothed.getNextValue();
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                float y = (drySample + (wet[ch][i] - drySample) * m) * out;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp; // fully bypassed = bit-exact dry
                io[ch][i] = y;
                outSquares += y * y;
                outPeak = juce::jmax (outPeak, std::abs (y));
            }
        }
        outputMeter.pushLevel (outSquares / (float) numChannels, numSamples, outPeak);
    }

    juce::AudioProcessorEditor* DoubleProcessor::createEditor()
    {
        return new DoubleEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::dbl::DoubleProcessor();
}
#endif
